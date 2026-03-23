#pragma once

/// InsightOS backend — Base capture worker.
///
/// Ported from donor src/workers/device_worker.hpp (commit 4032eb4).
/// Key change: callback-based frame delivery instead of direct IPC writes.
/// Workers call frame_callback_ with captured data; the session layer wires
/// callbacks to IPC writers or RTSP publishers.

#include "insightos/backend/ipc.hpp"
#include "insightos/backend/types.hpp"

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace insightos::backend {

class CaptureWorker {
public:
    explicit CaptureWorker(std::string name) : name_(std::move(name)) {}
    virtual ~CaptureWorker() { stop(); }

    CaptureWorker(const CaptureWorker&) = delete;
    CaptureWorker& operator=(const CaptureWorker&) = delete;

    /// Frame delivery callback.
    using FrameCallback = std::function<void(const std::string& stream_name,
                                              const uint8_t* data, size_t size,
                                              int64_t pts_ns, uint32_t flags)>;

    void set_frame_callback(FrameCallback cb) { frame_callback_ = std::move(cb); }

    /// Start the worker thread.
    /// Launches thread -> calls setup() -> waits for result via future.
    bool start(std::string& err) {
        if (running_.load()) return true;
        stop_requested_.store(false);

        std::promise<std::optional<std::string>> started;
        auto fut = started.get_future();

        thread_ = std::thread(
            [this](std::promise<std::optional<std::string>> p) {
                auto setup_err = setup();
                p.set_value(setup_err);
                if (setup_err) return;

                running_.store(true);
                run();
                cleanup();
                running_.store(false);
            },
            std::move(started));

        const auto maybe_err = fut.get();
        if (maybe_err) {
            err = *maybe_err;
            stop_requested_.store(true);
            if (thread_.joinable()) thread_.join();
            return false;
        }
        return true;
    }

    /// Stop the worker thread (blocks until joined).
    void stop() {
        stop_requested_.store(true);
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

    std::string_view name() const { return name_; }
    bool is_running() const { return running_.load(); }

protected:
    /// Device-specific initialization (called on the worker thread).
    /// Return nullopt on success, or an error message on failure.
    virtual std::optional<std::string> setup() = 0;

    /// The capture loop. Should loop until stop_requested() returns true.
    virtual void run() = 0;

    /// Cleanup hook called after run() returns.
    virtual void cleanup() = 0;

    bool stop_requested() const { return stop_requested_.load(); }

    /// Deliver a frame via the callback.
    void deliver_frame(const std::string& stream_name,
                       const uint8_t* data, size_t size,
                       int64_t pts_ns, uint32_t flags = 0) {
        if (frame_callback_) {
            frame_callback_(stream_name, data, size, pts_ns, flags);
        }
    }

private:
    std::string name_;
    std::atomic_bool running_{false};
    std::atomic_bool stop_requested_{false};
    std::thread thread_;
    FrameCallback frame_callback_;
};

}  // namespace insightos::backend
