#pragma once

// role: base capture-worker abstraction for runtime-owned device capture.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor callback-based worker contract so serving
// runtimes can fan captured frames into IPC channels without donor session
// tables.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/types.hpp"

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace insightio::backend {

class CaptureWorker {
public:
    explicit CaptureWorker(std::string name) : name_(std::move(name)) {}
    virtual ~CaptureWorker() { stop(); }

    CaptureWorker(const CaptureWorker&) = delete;
    CaptureWorker& operator=(const CaptureWorker&) = delete;

    using FrameCallback = std::function<void(const std::string& stream_name,
                                             const uint8_t* data,
                                             size_t size,
                                             int64_t pts_ns,
                                             uint32_t flags)>;

    void set_frame_callback(FrameCallback callback) { frame_callback_ = std::move(callback); }

    bool start(std::string& err) {
        if (running_.load()) {
            return true;
        }
        stop_requested_.store(false);

        std::promise<std::optional<std::string>> started;
        auto future = started.get_future();
        thread_ = std::thread(
            [this](std::promise<std::optional<std::string>> promise) {
                auto setup_err = setup();
                promise.set_value(setup_err);
                if (setup_err.has_value()) {
                    return;
                }
                running_.store(true);
                run();
                cleanup();
                running_.store(false);
            },
            std::move(started));

        const auto maybe_err = future.get();
        if (maybe_err.has_value()) {
            err = *maybe_err;
            stop_requested_.store(true);
            if (thread_.joinable()) {
                thread_.join();
            }
            return false;
        }
        return true;
    }

    void stop() {
        stop_requested_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
        running_.store(false);
    }

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] bool is_running() const { return running_.load(); }

protected:
    virtual std::optional<std::string> setup() = 0;
    virtual void run() = 0;
    virtual void cleanup() = 0;

    [[nodiscard]] bool stop_requested() const { return stop_requested_.load(); }

    void deliver_frame(const std::string& stream_name,
                       const uint8_t* data,
                       size_t size,
                       int64_t pts_ns,
                       uint32_t flags = 0) {
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

}  // namespace insightio::backend
