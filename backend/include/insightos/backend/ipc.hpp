#pragma once

/// InsightOS backend — IPC ring buffer (memfd-backed SPMC ring with eventfd).
///
/// Ported from donor iocontroller/ipc.hpp (commit 4032eb4).
/// Namespace: insightos::backend::ipc (was iocontroller::ipc).
/// Uses insightos::backend::Result instead of iocontroller::Result.
/// Removed spdlog/trace_timer dependencies.

#include "insightos/backend/result.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace insightos::backend::ipc {

constexpr uint32_t kRingMagic   = 0x474E4952;  // 'RING'
constexpr uint32_t kRingVersion = 2;
constexpr uint64_t kInvalidSeq  = 0xFFFFFFFFFFFFFFFFULL;

// Slot flags for in-band signalling.
constexpr uint32_t kFlagCapsChange  = 0x01;
constexpr uint32_t kFlagStateChange = 0x02;
constexpr uint32_t kFlagEndOfStream = 0x04;

struct RingHeader {
    uint32_t magic{0};
    uint32_t version{0};
    uint32_t buffer_slots{0};
    uint32_t slot_size{0};
    std::atomic<uint64_t> write_seq{0};
    uint8_t overflow_policy{0};
    uint8_t reserved[47]{};
};

struct SlotHeader {
    std::atomic<uint64_t> seq{0};
    uint64_t pts_ns{0};
    uint64_t dts_ns{0};
    uint32_t size{0};
    uint32_t flags{0};
};

/// Byte offset of slot `idx` within the shared-memory region.
inline size_t slot_offset(size_t slot_size, uint32_t idx) {
    return sizeof(RingHeader) + slot_size * static_cast<size_t>(idx);
}

/// Zero-copy view into a ring slot.
struct FrameSpan {
    const uint8_t* data{nullptr};
    size_t size{0};
    int64_t pts_ns{0};
    int64_t dts_ns{0};
    uint32_t flags{0};
};

struct ChannelSpec {
    std::string channel_id;
    uint32_t buffer_slots{4};
    size_t max_payload_bytes{0};
    uint32_t reader_count{1};
};

class Channel {
public:
    Channel() = default;
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&& other) noexcept;
    Channel& operator=(Channel&& other) noexcept;

    const std::string& id() const { return id_; }
    size_t reader_count() const { return reader_eventfds_.size(); }

    int dup_memfd(std::string& err) const;
    int dup_reader_eventfd(size_t idx, std::string& err) const;
    std::vector<int> dup_reader_eventfds(std::string& err) const;

private:
    friend Result<Channel> create_channel(const ChannelSpec& spec);
    void close();

    std::string id_;
    int memfd_{-1};
    void* shm_{nullptr};
    size_t shm_size_{0};
    RingHeader* header_{nullptr};
    std::vector<int> reader_eventfds_{};
};

class Writer {
public:
    Writer() = default;
    ~Writer();

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&&) noexcept;
    Writer& operator=(Writer&&) noexcept;

    bool is_ready() const { return header_ != nullptr; }
    bool write(const uint8_t* data, size_t size, int64_t pts_ns,
               int64_t dts_ns = 0, uint32_t flags = 0);

    int add_reader_eventfd(int fd);
    bool remove_reader_eventfd(int fd);

private:
    friend Result<std::shared_ptr<Writer>>
    attach_writer(int memfd, std::vector<int> eventfds);
    void close();
    SlotHeader* slot_for_seq(uint64_t seq) const;
    void notify_readers();

    int memfd_{-1};
    void* shm_{nullptr};
    size_t shm_size_{0};
    RingHeader* header_{nullptr};
    size_t slot_size_{0};
    size_t payload_size_{0};
    mutable std::mutex eventfds_mu_;
    std::vector<int> eventfds_{};
};

class Reader {
public:
    Reader() = default;
    ~Reader();

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Reader(Reader&&) noexcept;
    Reader& operator=(Reader&&) noexcept;

    bool is_ready() const { return header_ != nullptr; }
    int event_fd() const { return eventfd_; }

    void clear_event();
    std::optional<FrameSpan> read();

private:
    friend Result<std::shared_ptr<Reader>>
    attach_reader(int memfd, int eventfd);
    void close();
    SlotHeader* slot_for_seq(uint64_t seq) const;

    int memfd_{-1};
    int eventfd_{-1};
    void* shm_{nullptr};
    size_t shm_size_{0};
    RingHeader* header_{nullptr};
    size_t slot_size_{0};
    uint32_t buffer_slots_{0};
    uint64_t last_read_seq_{kInvalidSeq};
};

Result<Channel> create_channel(const ChannelSpec& spec);
Result<std::shared_ptr<Writer>> attach_writer(int memfd, std::vector<int> eventfds);
Result<std::shared_ptr<Reader>> attach_reader(int memfd, int eventfd);

}  // namespace insightos::backend::ipc
