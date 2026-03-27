#pragma once

// role: memfd-backed local IPC ring transport for attachable serving runtime.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor SPMC ring buffer plus eventfd signalling into
// the standalone insight-io backend for task-7 local attach.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/result.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace insightio::backend::ipc {

constexpr uint32_t kRingMagic = 0x474E4952;
constexpr uint32_t kRingVersion = 2;
constexpr uint64_t kInvalidSeq = 0xFFFFFFFFFFFFFFFFULL;

constexpr uint32_t kFlagCapsChange = 0x01;
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

inline size_t slot_offset(size_t slot_size, uint32_t idx) {
    return sizeof(RingHeader) + slot_size * static_cast<size_t>(idx);
}

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

    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] size_t reader_count() const { return reader_eventfds_.size(); }

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

    [[nodiscard]] bool is_ready() const { return header_ != nullptr; }
    void reset();
    bool write(const uint8_t* data,
               size_t size,
               int64_t pts_ns,
               int64_t dts_ns = 0,
               uint32_t flags = 0);

    int add_reader_eventfd(int fd);
    bool remove_reader_eventfd(int fd);

private:
    friend Result<std::shared_ptr<Writer>> attach_writer(int memfd,
                                                         std::vector<int> eventfds);
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

    [[nodiscard]] bool is_ready() const { return header_ != nullptr; }
    [[nodiscard]] int event_fd() const { return eventfd_; }

    void clear_event();
    std::optional<FrameSpan> read();

private:
    friend Result<std::shared_ptr<Reader>> attach_reader(int memfd, int eventfd);
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

}  // namespace insightio::backend::ipc
