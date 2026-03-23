#pragma once

/// InsightOS backend — Channel registry for IPC writer lookup.
///
/// Simple in-memory map: DeliveryKey -> IPC channel + writer.
/// Delivery sessions publish frames into these channels and the control socket
/// duplicates descriptors for local consumers on demand.

#include "insightos/backend/ipc.hpp"
#include "insightos/backend/result.hpp"
#include "insightos/backend/session.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>

namespace insightos::backend {

class ChannelRegistry {
public:
    struct ConsumerFds {
        std::string channel_id;
        int memfd{-1};
        int eventfd{-1};
        int writer_eventfd{-1};
    };

    /// Get or create a writer for a delivery key.
    Result<std::shared_ptr<ipc::Writer>> get_or_create(const DeliveryKey& key,
                                                       uint32_t buffer_slots,
                                                       size_t slot_size);

    /// Add a new consumer and duplicate its memfd + eventfd.
    Result<ConsumerFds> add_consumer(const DeliveryKey& key);

    /// Remove a consumer from writer fanout.
    bool remove_consumer(const DeliveryKey& key, int writer_eventfd);

    /// Remove a writer entry.
    void remove(const DeliveryKey& key);

    /// Find an existing writer (returns nullptr if not found).
    std::shared_ptr<ipc::Writer> find(const DeliveryKey& key) const;

    std::string channel_id(const DeliveryKey& key) const;
    std::uint32_t consumer_count(const DeliveryKey& key) const;

private:
    struct Entry {
        std::string channel_id;
        std::unique_ptr<ipc::Channel> channel;
        std::shared_ptr<ipc::Writer> writer;
        std::uint32_t consumer_count{0};
    };

    mutable std::mutex mutex_;
    std::map<DeliveryKey, Entry> channels_;
};

}  // namespace insightos::backend
