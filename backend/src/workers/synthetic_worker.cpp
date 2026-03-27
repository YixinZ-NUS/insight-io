// role: synthetic capture worker implementation for task-7 runtime tests.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: emits stable frame payloads for `test:` URIs so attach,
// fanout, and grouped-runtime tests can run without live devices.
// See docs/past-tasks.md for verification history.

#include "synthetic_worker.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace insightio::backend {

namespace {

std::size_t bytes_per_unit(std::string_view format) {
    if (format == "rgb24" || format == "bgr24") {
        return 3;
    }
    if (format == "rgba" || format == "bgra" || format == "s32le" ||
        format == "f32le") {
        return 4;
    }
    if (format == "yuyv" || format == "uyvy" || format == "y16" ||
        format == "z16" || format == "gray16" || format == "s16le") {
        return 2;
    }
    return 1;
}

}  // namespace

SyntheticWorker::SyntheticWorker(SyntheticWorkerConfig cfg)
    : CaptureWorker(cfg.name),
      cfg_(std::move(cfg)) {}

std::optional<std::string> SyntheticWorker::setup() {
    if (cfg_.streams.empty()) {
        return "synthetic worker requires at least one stream";
    }

    payloads_.clear();
    payloads_.reserve(cfg_.streams.size());
    for (const auto& stream : cfg_.streams) {
        payloads_.push_back(make_payload(stream));
    }
    return std::nullopt;
}

void SyntheticWorker::run() {
    while (!stop_requested()) {
        const auto cycle_started = std::chrono::steady_clock::now();
        const auto pts_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                cycle_started.time_since_epoch())
                .count();

        for (std::size_t index = 0; index < cfg_.streams.size(); ++index) {
            auto& payload = payloads_[index];
            if (!payload.empty()) {
                payload[0] = static_cast<std::uint8_t>(sequence_ & 0xffU);
            }
            deliver_frame(cfg_.streams[index].stream_name,
                          payload.data(),
                          payload.size(),
                          pts_ns);
        }

        ++sequence_;
        std::this_thread::sleep_until(cycle_started + frame_period());
    }
}

void SyntheticWorker::cleanup() {
    payloads_.clear();
}

std::chrono::milliseconds SyntheticWorker::frame_period() const {
    std::uint32_t max_fps = 0;
    for (const auto& stream : cfg_.streams) {
        max_fps = std::max(max_fps, stream.caps.fps);
    }
    if (max_fps == 0) {
        return std::chrono::milliseconds(20);
    }
    return std::chrono::milliseconds(std::max<std::uint32_t>(1, 1000 / max_fps));
}

std::vector<std::uint8_t> SyntheticWorker::make_payload(
    const SyntheticStreamConfig& stream) const {
    if (stream.caps.is_audio()) {
        const auto sample_rate = std::max<std::uint32_t>(stream.caps.sample_rate(), 8000);
        const auto channels = std::max<std::uint32_t>(stream.caps.channels(), 1);
        const auto samples = sample_rate / 50;
        const auto bytes =
            static_cast<std::size_t>(samples) * channels *
            std::max<std::size_t>(bytes_per_unit(stream.caps.format), 2);
        return std::vector<std::uint8_t>(std::max<std::size_t>(bytes, 512), 0);
    }

    const auto width = std::max<std::uint32_t>(stream.caps.width, 16);
    const auto height = std::max<std::uint32_t>(stream.caps.height, 16);
    std::size_t bytes = static_cast<std::size_t>(width) * height *
                        bytes_per_unit(stream.caps.format);
    if (is_compressed_video(stream.caps.format)) {
        bytes = std::max<std::size_t>(bytes / 8, 2048);
    }
    return std::vector<std::uint8_t>(std::max<std::size_t>(bytes, 1024), 0);
}

}  // namespace insightio::backend
