#pragma once

// role: runtime-only RTSP publication helper for exact serving runtimes.
// revision: 2026-03-27 task8-rtsp-runtime-validation
// major changes: adds RTSP publication planning plus a small ffmpeg-backed
// publisher that can expose one capture stream through an external RTSP server
// without adding durable publication tables, and hardens pipe-write error
// reporting so backpressure cannot silently truncate runtime output.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/types.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace insightio::backend {

struct RtspPublicationPlan {
    std::string publication_profile{"default"};
    std::string transport{"rtsp"};
    std::string promised_format;
    std::string actual_format;
    std::vector<std::string> input_args;
    std::vector<std::string> output_args;
};

std::optional<RtspPublicationPlan> build_rtsp_publication_plan(
    const ResolvedCaps& caps);

class RtspPublisher {
public:
    RtspPublisher(std::string name,
                  std::string url,
                  RtspPublicationPlan plan);
    ~RtspPublisher();

    RtspPublisher(const RtspPublisher&) = delete;
    RtspPublisher& operator=(const RtspPublisher&) = delete;

    bool start(std::string& err);
    void stop();
    bool publish(const uint8_t* data, size_t size, int64_t pts_ns, uint32_t flags);

    [[nodiscard]] bool is_running() const;
    [[nodiscard]] std::string last_error() const;
    [[nodiscard]] const std::string& url() const { return url_; }
    [[nodiscard]] const RtspPublicationPlan& plan() const { return plan_; }

private:
    bool poll_process_locked() const;
    void stop_locked() const;
    std::string drain_stderr_locked() const;
    static bool set_nonblocking(int fd, std::string& err);
    bool wait_for_write_ready_locked(int timeout_ms, std::string& err) const;

    std::string name_;
    std::string url_;
    RtspPublicationPlan plan_;

    mutable std::mutex mutex_;
    mutable int stdin_fd_{-1};
    mutable int stderr_fd_{-1};
    mutable pid_t pid_{-1};
    mutable bool running_{false};
    mutable std::string last_error_;
};

}  // namespace insightio::backend
