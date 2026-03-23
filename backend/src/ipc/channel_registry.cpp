/// InsightOS backend — Channel registry implementation.

#include "insightos/backend/channel_registry.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

namespace insightos::backend {

Result<std::shared_ptr<ipc::Writer>> ChannelRegistry::get_or_create(
    const DeliveryKey& key, uint32_t buffer_slots, size_t slot_size) {
    std::lock_guard lock(mutex_);

    auto it = channels_.find(key);
    if (it != channels_.end() && it->second.writer &&
        it->second.writer->is_ready()) {
        return Result<std::shared_ptr<ipc::Writer>>::ok(it->second.writer);
    }

    // Create a new IPC channel and attach a writer.
    ipc::ChannelSpec spec{
        .channel_id  = key.to_string(),
        .buffer_slots = buffer_slots,
        .max_payload_bytes = slot_size,
        .reader_count = 0,
    };

    auto ch_result = ipc::create_channel(spec);
    if (!ch_result.ok()) {
        return Result<std::shared_ptr<ipc::Writer>>::err(ch_result.error());
    }

    auto channel = std::make_unique<ipc::Channel>(std::move(ch_result.value()));

    std::string err;
    int memfd = channel->dup_memfd(err);
    if (memfd < 0) {
        return Result<std::shared_ptr<ipc::Writer>>::err({"ipc_error", err});
    }

    auto eventfds = channel->dup_reader_eventfds(err);
    if (!err.empty()) {
        ::close(memfd);
        return Result<std::shared_ptr<ipc::Writer>>::err({"ipc_error", err});
    }

    auto writer_result = ipc::attach_writer(memfd, std::move(eventfds));
    if (!writer_result.ok()) {
        return Result<std::shared_ptr<ipc::Writer>>::err(writer_result.error());
    }

    Entry entry;
    entry.channel_id = key.to_string();
    entry.channel = std::move(channel);
    entry.writer = writer_result.value();

    auto writer = entry.writer;
    channels_[key] = std::move(entry);
    return Result<std::shared_ptr<ipc::Writer>>::ok(std::move(writer));
}

Result<ChannelRegistry::ConsumerFds> ChannelRegistry::add_consumer(
    const DeliveryKey& key) {
    std::lock_guard lock(mutex_);

    auto it = channels_.find(key);
    if (it == channels_.end() || !it->second.channel || !it->second.writer) {
        return Result<ConsumerFds>::err({"ipc_error", "channel not ready"});
    }

    std::string err;
    const int memfd = it->second.channel->dup_memfd(err);
    if (memfd < 0) {
        return Result<ConsumerFds>::err({"ipc_error", err});
    }

    const int eventfd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (eventfd < 0) {
        ::close(memfd);
        return Result<ConsumerFds>::err({"ipc_error", "eventfd() failed"});
    }

    const int writer_eventfd = it->second.writer->add_reader_eventfd(eventfd);
    if (writer_eventfd < 0) {
        ::close(memfd);
        ::close(eventfd);
        return Result<ConsumerFds>::err(
            {"ipc_error", "failed to register reader eventfd"});
    }

    ++it->second.consumer_count;
    ConsumerFds fds;
    fds.channel_id = it->second.channel_id;
    fds.memfd = memfd;
    fds.eventfd = eventfd;
    fds.writer_eventfd = writer_eventfd;
    return Result<ConsumerFds>::ok(std::move(fds));
}

bool ChannelRegistry::remove_consumer(const DeliveryKey& key, int writer_eventfd) {
    std::lock_guard lock(mutex_);
    auto it = channels_.find(key);
    if (it == channels_.end() || !it->second.writer) return false;
    if (!it->second.writer->remove_reader_eventfd(writer_eventfd)) return false;
    if (it->second.consumer_count > 0) {
        --it->second.consumer_count;
    }
    return true;
}

void ChannelRegistry::remove(const DeliveryKey& key) {
    std::lock_guard lock(mutex_);
    channels_.erase(key);
}

std::shared_ptr<ipc::Writer> ChannelRegistry::find(const DeliveryKey& key) const {
    std::lock_guard lock(mutex_);
    auto it = channels_.find(key);
    if (it == channels_.end()) return nullptr;
    return it->second.writer;
}

std::string ChannelRegistry::channel_id(const DeliveryKey& key) const {
    std::lock_guard lock(mutex_);
    auto it = channels_.find(key);
    if (it == channels_.end()) return {};
    return it->second.channel_id;
}

std::uint32_t ChannelRegistry::consumer_count(const DeliveryKey& key) const {
    std::lock_guard lock(mutex_);
    auto it = channels_.find(key);
    if (it == channels_.end()) return 0;
    return it->second.consumer_count;
}

}  // namespace insightos::backend
