#pragma once

/// InsightOS backend — V4L2 capture worker.
///
/// Ported from donor src/workers/v4l2_capture.hpp, v4l2_capture_worker.hpp
/// (commit 4032eb4). Merged V4l2Capture + V4l2CaptureWorker into a single
/// V4l2Worker class. Uses callback-based frame delivery.

#include "insightos/backend/types.hpp"
#include "insightos/backend/worker.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace insightos::backend {

struct V4l2WorkerConfig {
    std::string name;           // Worker name / stream name
    std::string device_path;    // "/dev/video0"
    ResolvedCaps caps;          // format, width, height, fps
};

class V4l2Worker final : public CaptureWorker {
public:
    explicit V4l2Worker(V4l2WorkerConfig cfg);

protected:
    std::optional<std::string> setup() override;
    void run() override;
    void cleanup() override;

private:
    /// Retry-tolerant ioctl wrapper.
    static int xioctl(int fd, unsigned long request, void* arg);

    /// Convert format string to V4L2 fourcc.
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

}  // namespace insightos::backend
