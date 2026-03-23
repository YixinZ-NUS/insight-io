/// InsightOS backend — IPC ring buffer implementation.
///
/// Ported from donor iocontroller src/ipc/ipc.cpp (commit 4032eb4).
/// Namespace: insightos::backend::ipc.
/// Removed spdlog and trace_timer dependencies; uses fprintf for warnings.

#include "insightos/backend/ipc.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace insightos::backend {

inline std::string errno_to_string(int err) {
    return std::string(std::strerror(err));
}

}  // namespace insightos::backend

namespace insightos::backend::ipc {

// ─── Channel ────────────────────────────────────────────────────────────

Channel::~Channel() { close(); }

Channel::Channel(Channel&& other) noexcept { *this = std::move(other); }

Channel& Channel::operator=(Channel&& other) noexcept {
    if (this == &other) return *this;
    close();
    id_              = std::move(other.id_);
    memfd_           = other.memfd_;
    shm_             = other.shm_;
    shm_size_        = other.shm_size_;
    header_          = other.header_;
    reader_eventfds_ = std::move(other.reader_eventfds_);

    other.memfd_    = -1;
    other.shm_      = nullptr;
    other.shm_size_ = 0;
    other.header_   = nullptr;
    other.reader_eventfds_.clear();
    return *this;
}

int Channel::dup_memfd(std::string& err) const {
    if (memfd_ < 0) { err = "memfd not available"; return -1; }
    const int dupfd = ::dup(memfd_);
    if (dupfd < 0) err = "dup(memfd): " + errno_to_string(errno);
    return dupfd;
}

int Channel::dup_reader_eventfd(size_t idx, std::string& err) const {
    if (idx >= reader_eventfds_.size()) { err = "reader index out of range"; return -1; }
    const int fd = reader_eventfds_[idx];
    if (fd < 0) { err = "reader eventfd invalid"; return -1; }
    const int dupfd = ::dup(fd);
    if (dupfd < 0) err = "dup(eventfd): " + errno_to_string(errno);
    return dupfd;
}

std::vector<int> Channel::dup_reader_eventfds(std::string& err) const {
    std::vector<int> fds;
    fds.reserve(reader_eventfds_.size());
    for (size_t i = 0; i < reader_eventfds_.size(); ++i) {
        std::string local_err;
        const int dupfd = dup_reader_eventfd(i, local_err);
        if (dupfd < 0) {
            err = local_err;
            for (int fd : fds) { if (fd >= 0) ::close(fd); }
            fds.clear();
            return fds;
        }
        fds.push_back(dupfd);
    }
    return fds;
}

void Channel::close() {
    if (shm_ && shm_ != MAP_FAILED) ::munmap(shm_, shm_size_);
    shm_ = nullptr; shm_size_ = 0; header_ = nullptr;
    for (int fd : reader_eventfds_) { if (fd >= 0) ::close(fd); }
    reader_eventfds_.clear();
    if (memfd_ >= 0) ::close(memfd_);
    memfd_ = -1;
}

// ─── Writer ─────────────────────────────────────────────────────────────

Writer::~Writer() { close(); }

Writer::Writer(Writer&& other) noexcept { *this = std::move(other); }

Writer& Writer::operator=(Writer&& other) noexcept {
    if (this == &other) return *this;
    close();
    memfd_        = other.memfd_;
    shm_          = other.shm_;
    shm_size_     = other.shm_size_;
    header_       = other.header_;
    slot_size_    = other.slot_size_;
    payload_size_ = other.payload_size_;
    {
        std::lock_guard lock(other.eventfds_mu_);
        eventfds_ = std::move(other.eventfds_);
        other.eventfds_.clear();
    }
    other.memfd_        = -1;
    other.shm_          = nullptr;
    other.shm_size_     = 0;
    other.header_       = nullptr;
    other.slot_size_    = 0;
    other.payload_size_ = 0;
    return *this;
}

bool Writer::write(const uint8_t* data, size_t size, int64_t pts_ns,
                   int64_t dts_ns, uint32_t flags) {
    if (!is_ready()) return false;
    if (!data && size > 0) return false;

    size_t write_size = size;
    if (write_size > payload_size_) {
        std::fprintf(stderr, "IPC write truncated from %zu to %zu bytes\n",
                     write_size, payload_size_);
        write_size = payload_size_;
    }

    const uint64_t seq = header_->write_seq.load(std::memory_order_relaxed);
    auto* slot = slot_for_seq(seq);
    if (!slot) return false;

    uint8_t* slot_data = reinterpret_cast<uint8_t*>(slot) + sizeof(SlotHeader);
    if (write_size > 0) std::memcpy(slot_data, data, write_size);

    slot->pts_ns = static_cast<uint64_t>(pts_ns);
    slot->dts_ns = static_cast<uint64_t>(dts_ns);
    slot->size   = static_cast<uint32_t>(write_size);
    slot->flags  = flags;

    std::atomic_thread_fence(std::memory_order_release);
    slot->seq.store(seq, std::memory_order_release);
    header_->write_seq.store(seq + 1, std::memory_order_release);

    notify_readers();
    return true;
}

SlotHeader* Writer::slot_for_seq(uint64_t seq) const {
    if (!header_) return nullptr;
    const uint32_t slots = header_->buffer_slots;
    if (slots == 0) return nullptr;
    const uint32_t idx = static_cast<uint32_t>(seq % slots);
    const size_t offset = slot_offset(slot_size_, idx);
    auto* base = static_cast<uint8_t*>(shm_);
    return reinterpret_cast<SlotHeader*>(base + offset);
}

void Writer::notify_readers() {
    std::lock_guard lock(eventfds_mu_);
    for (int fd : eventfds_) {
        if (fd < 0) continue;
        uint64_t one = 1;
        const ssize_t r = ::write(fd, &one, sizeof(one));
        (void)r;
    }
}

int Writer::add_reader_eventfd(int fd) {
    if (fd < 0) return -1;
    int dupfd = ::dup(fd);
    if (dupfd < 0) return -1;
    int flags = ::fcntl(dupfd, F_GETFD);
    if (flags != -1) ::fcntl(dupfd, F_SETFD, flags | FD_CLOEXEC);
    std::lock_guard lock(eventfds_mu_);
    eventfds_.push_back(dupfd);
    return dupfd;
}

bool Writer::remove_reader_eventfd(int fd) {
    std::lock_guard lock(eventfds_mu_);
    auto it = std::find(eventfds_.begin(), eventfds_.end(), fd);
    if (it == eventfds_.end()) return false;
    ::close(*it);
    eventfds_.erase(it);
    return true;
}

void Writer::close() {
    if (shm_ && shm_ != MAP_FAILED) ::munmap(shm_, shm_size_);
    shm_ = nullptr; shm_size_ = 0; header_ = nullptr;
    slot_size_ = 0; payload_size_ = 0;
    {
        std::lock_guard lock(eventfds_mu_);
        for (int fd : eventfds_) { if (fd >= 0) ::close(fd); }
        eventfds_.clear();
    }
    if (memfd_ >= 0) ::close(memfd_);
    memfd_ = -1;
}

// ─── Reader ─────────────────────────────────────────────────────────────

Reader::~Reader() { close(); }

Reader::Reader(Reader&& other) noexcept { *this = std::move(other); }

Reader& Reader::operator=(Reader&& other) noexcept {
    if (this == &other) return *this;
    close();
    memfd_         = other.memfd_;
    eventfd_       = other.eventfd_;
    shm_           = other.shm_;
    shm_size_      = other.shm_size_;
    header_        = other.header_;
    slot_size_     = other.slot_size_;
    buffer_slots_  = other.buffer_slots_;
    last_read_seq_ = other.last_read_seq_;

    other.memfd_         = -1;
    other.eventfd_       = -1;
    other.shm_           = nullptr;
    other.shm_size_      = 0;
    other.header_        = nullptr;
    other.slot_size_     = 0;
    other.buffer_slots_  = 0;
    other.last_read_seq_ = kInvalidSeq;
    return *this;
}

void Reader::clear_event() {
    if (eventfd_ < 0) return;
    uint64_t count = 0;
    const ssize_t r = ::read(eventfd_, &count, sizeof(count));
    (void)r;
}

std::optional<FrameSpan> Reader::read() {
    if (!is_ready()) return std::nullopt;

    const uint64_t write_seq = header_->write_seq.load(std::memory_order_acquire);
    if (write_seq == 0) return std::nullopt;

    uint64_t next_seq = 0;
    if (last_read_seq_ == kInvalidSeq) {
        next_seq = (write_seq > buffer_slots_) ? write_seq - buffer_slots_ : 0;
    } else {
        next_seq = last_read_seq_ + 1;
    }

    if (next_seq >= write_seq) return std::nullopt;
    if (write_seq - next_seq > buffer_slots_) next_seq = write_seq - buffer_slots_;

    auto* slot = slot_for_seq(next_seq);
    if (!slot) return std::nullopt;

    const uint64_t slot_seq = slot->seq.load(std::memory_order_acquire);
    if (slot_seq != next_seq) return std::nullopt;

    const uint8_t* slot_data = reinterpret_cast<const uint8_t*>(slot) + sizeof(SlotHeader);
    last_read_seq_ = next_seq;
    return FrameSpan{slot_data, slot->size,
                     static_cast<int64_t>(slot->pts_ns),
                     static_cast<int64_t>(slot->dts_ns),
                     slot->flags};
}

SlotHeader* Reader::slot_for_seq(uint64_t seq) const {
    if (!header_ || buffer_slots_ == 0) return nullptr;
    const uint32_t idx = static_cast<uint32_t>(seq % buffer_slots_);
    const size_t offset = slot_offset(slot_size_, idx);
    auto* base = static_cast<uint8_t*>(shm_);
    return reinterpret_cast<SlotHeader*>(base + offset);
}

void Reader::close() {
    if (shm_ && shm_ != MAP_FAILED) ::munmap(shm_, shm_size_);
    shm_ = nullptr; shm_size_ = 0; header_ = nullptr;
    slot_size_ = 0; buffer_slots_ = 0; last_read_seq_ = kInvalidSeq;
    if (eventfd_ >= 0) ::close(eventfd_);
    eventfd_ = -1;
    if (memfd_ >= 0) ::close(memfd_);
    memfd_ = -1;
}

// ─── Free functions ─────────────────────────────────────────────────────

Result<Channel> create_channel(const ChannelSpec& spec) {
    if (spec.buffer_slots == 0)
        return Result<Channel>::err({"invalid_ipc", "buffer_slots must be > 0"});
    if (spec.max_payload_bytes == 0)
        return Result<Channel>::err({"invalid_ipc", "max_payload_bytes must be > 0"});

    Channel channel;
    channel.id_ = spec.channel_id;

    std::string name = "insightos-" + spec.channel_id;
    if (name.size() > 128) name.resize(128);

    channel.memfd_ = ::memfd_create(name.c_str(), MFD_CLOEXEC);
    if (channel.memfd_ < 0)
        return Result<Channel>::err({"ipc_error", "memfd_create: " + errno_to_string(errno)});

    const size_t slot_size  = sizeof(SlotHeader) + spec.max_payload_bytes;
    const size_t total_size = sizeof(RingHeader) + slot_size * spec.buffer_slots;
    if (total_size < sizeof(RingHeader) || total_size < slot_size)
        return Result<Channel>::err({"ipc_error", "invalid IPC size"});

    if (::ftruncate(channel.memfd_, static_cast<off_t>(total_size)) < 0)
        return Result<Channel>::err({"ipc_error", "ftruncate: " + errno_to_string(errno)});

    void* map = ::mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, channel.memfd_, 0);
    if (map == MAP_FAILED)
        return Result<Channel>::err({"ipc_error", "mmap: " + errno_to_string(errno)});

    channel.shm_      = map;
    channel.shm_size_ = total_size;
    channel.header_   = static_cast<RingHeader*>(map);

    channel.header_->magic       = kRingMagic;
    channel.header_->version     = kRingVersion;
    channel.header_->buffer_slots = spec.buffer_slots;
    channel.header_->slot_size   = static_cast<uint32_t>(slot_size);
    channel.header_->write_seq.store(0, std::memory_order_relaxed);
    channel.header_->overflow_policy = 0;

    auto* base = static_cast<uint8_t*>(map);
    for (uint32_t i = 0; i < spec.buffer_slots; ++i) {
        auto* slot = reinterpret_cast<SlotHeader*>(base + slot_offset(slot_size, i));
        slot->seq.store(kInvalidSeq, std::memory_order_relaxed);
        slot->pts_ns = 0;
        slot->size   = 0;
        slot->flags  = 0;
    }

    if (spec.reader_count > 0) {
        channel.reader_eventfds_.resize(spec.reader_count, -1);
        for (uint32_t i = 0; i < spec.reader_count; ++i) {
            const int efd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (efd < 0)
                return Result<Channel>::err({"ipc_error", "eventfd: " + errno_to_string(errno)});
            channel.reader_eventfds_[i] = efd;
        }
    }

    return Result<Channel>::ok(std::move(channel));
}

Result<std::shared_ptr<Writer>> attach_writer(int memfd, std::vector<int> eventfds) {
    if (memfd < 0)
        return Result<std::shared_ptr<Writer>>::err({"ipc_error", "invalid memfd"});

    struct stat st;
    if (::fstat(memfd, &st) < 0)
        return Result<std::shared_ptr<Writer>>::err({"ipc_error", "fstat: " + errno_to_string(errno)});
    if (st.st_size <= 0)
        return Result<std::shared_ptr<Writer>>::err({"ipc_error", "memfd size is zero"});

    void* map = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                       PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (map == MAP_FAILED)
        return Result<std::shared_ptr<Writer>>::err({"ipc_error", "mmap: " + errno_to_string(errno)});

    auto* header = static_cast<RingHeader*>(map);
    if (header->magic != kRingMagic || header->version != kRingVersion) {
        ::munmap(map, static_cast<size_t>(st.st_size));
        return Result<std::shared_ptr<Writer>>::err({"ipc_error", "invalid IPC header"});
    }

    auto writer = std::make_shared<Writer>();
    writer->memfd_        = memfd;
    writer->shm_          = map;
    writer->shm_size_     = static_cast<size_t>(st.st_size);
    writer->header_       = header;
    writer->slot_size_    = header->slot_size;
    writer->payload_size_ = (writer->slot_size_ > sizeof(SlotHeader))
                            ? writer->slot_size_ - sizeof(SlotHeader) : 0;
    writer->eventfds_     = std::move(eventfds);

    return Result<std::shared_ptr<Writer>>::ok(std::move(writer));
}

Result<std::shared_ptr<Reader>> attach_reader(int memfd, int eventfd) {
    if (memfd < 0)
        return Result<std::shared_ptr<Reader>>::err({"ipc_error", "invalid memfd"});

    struct stat st;
    if (::fstat(memfd, &st) < 0)
        return Result<std::shared_ptr<Reader>>::err({"ipc_error", "fstat: " + errno_to_string(errno)});
    if (st.st_size <= 0)
        return Result<std::shared_ptr<Reader>>::err({"ipc_error", "memfd size is zero"});

    void* map = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                       PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (map == MAP_FAILED)
        return Result<std::shared_ptr<Reader>>::err({"ipc_error", "mmap: " + errno_to_string(errno)});

    auto* header = static_cast<RingHeader*>(map);
    if (header->magic != kRingMagic || header->version != kRingVersion) {
        ::munmap(map, static_cast<size_t>(st.st_size));
        return Result<std::shared_ptr<Reader>>::err({"ipc_error", "invalid IPC header"});
    }

    auto reader = std::make_shared<Reader>();
    reader->memfd_         = memfd;
    reader->eventfd_       = eventfd;
    reader->shm_           = map;
    reader->shm_size_      = static_cast<size_t>(st.st_size);
    reader->header_        = header;
    reader->slot_size_     = header->slot_size;
    reader->buffer_slots_  = header->buffer_slots;
    reader->last_read_seq_ = kInvalidSeq;

    return Result<std::shared_ptr<Reader>>::ok(std::move(reader));
}

}  // namespace insightos::backend::ipc
