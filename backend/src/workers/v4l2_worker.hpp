#pragma once

// role: V4L2 capture worker for task-7 local serving runtime.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor V4L2 mmap capture worker into repo-native
// runtime form so exact camera sessions can publish to IPC.
// See docs/past-tasks.md for verification history.

#include "worker.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace insightio::backend {

struct V4l2WorkerConfig {
    std::string name;
    std::string device_path;
    ResolvedCaps caps;
};

class V4l2Worker final : public CaptureWorker {
public:
    explicit V4l2Worker(V4l2WorkerConfig cfg);

protected:
    std::optional<std::string> setup() override;
    void run() override;
    void cleanup() override;

private:
    static int xioctl(int fd, unsigned long request, void* arg);
    static uint32_t fourcc_from_format(const std::string& fmt);

    struct MmapBuffer {
        void* start{nullptr};
        size_t length{0};
    };

    bool queue_buffer(int index, std::string& err) noexcept;
    void requeue_buffer_best_effort(int index) noexcept;

    V4l2WorkerConfig cfg_;
    int fd_{-1};
    std::vector<MmapBuffer> buffers_;
    uint32_t actual_width_{0};
    uint32_t actual_height_{0};
    bool streaming_{false};
};

}  // namespace insightio::backend
