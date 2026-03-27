// role: PipeWire worker implementation for task-7 local attach runtime.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor PipeWire audio worker into repo-native
// runtime form for IPC-backed local audio attach.
// See docs/past-tasks.md for verification history.

#ifdef INSIGHTIO_HAS_PIPEWIRE

#include "pipewire_worker.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#pragma GCC diagnostic pop

namespace insightio::backend {

namespace {

struct PipeWireFormatInfo {
    spa_audio_format spa_fmt{SPA_AUDIO_FORMAT_UNKNOWN};
    int bytes_per_sample{0};

    static std::optional<PipeWireFormatInfo> from_string(std::string_view sample_format) {
        if (sample_format == "s16le") {
            return PipeWireFormatInfo{SPA_AUDIO_FORMAT_S16_LE, 2};
        }
        if (sample_format == "s32le") {
            return PipeWireFormatInfo{SPA_AUDIO_FORMAT_S32_LE, 4};
        }
        if (sample_format == "f32le") {
            return PipeWireFormatInfo{SPA_AUDIO_FORMAT_F32_LE, 4};
        }
        if (sample_format == "s24le") {
            return PipeWireFormatInfo{SPA_AUDIO_FORMAT_S24_32_LE, 4};
        }
        return std::nullopt;
    }
};

}  // namespace

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

struct PipeWireWorker::PipeWireCapture {
    pw_main_loop* loop{nullptr};
    pw_context* context{nullptr};
    pw_core* core{nullptr};
    pw_stream* stream{nullptr};
    spa_hook stream_listener {};
    pw_stream_events stream_events {};

    std::deque<std::vector<uint8_t>> pending {};
    bool ready{false};
    std::string last_error {};

    uint32_t sample_rate{0};
    uint32_t channels{0};
    spa_audio_format spa_fmt{SPA_AUDIO_FORMAT_UNKNOWN};
    std::array<const spa_pod*, 1> params {};
    std::vector<uint8_t> params_buffer {};

    static void on_state_changed(void* data,
                                 enum pw_stream_state,
                                 enum pw_stream_state state,
                                 const char* error) {
        auto* self = static_cast<PipeWireCapture*>(data);
        if (error != nullptr) {
            self->last_error = error;
        }
        if (state == PW_STREAM_STATE_ERROR) {
            std::fprintf(stderr, "PipeWire stream error: %s\n", error ? error : "unknown");
            return;
        }
        if (state == PW_STREAM_STATE_STREAMING || state == PW_STREAM_STATE_PAUSED) {
            self->ready = true;
        }
    }

    static void on_process(void* data) {
        auto* self = static_cast<PipeWireCapture*>(data);
        if (self->stream == nullptr) {
            return;
        }

        pw_buffer* buffer = pw_stream_dequeue_buffer(self->stream);
        if (buffer == nullptr) {
            return;
        }

        spa_buffer* spa_buffer_value = buffer->buffer;
        if (spa_buffer_value == nullptr || spa_buffer_value->n_datas == 0) {
            pw_stream_queue_buffer(self->stream, buffer);
            return;
        }

        spa_data& data_value = spa_buffer_value->datas[0];
        if (data_value.data == nullptr || data_value.chunk == nullptr) {
            pw_stream_queue_buffer(self->stream, buffer);
            return;
        }

        const size_t size = data_value.chunk->size;
        const size_t offset = data_value.chunk->offset;
        const uint8_t* source = static_cast<const uint8_t*>(data_value.data) + offset;
        if (size > 0) {
            self->pending.emplace_back(source, source + size);
        }
        pw_stream_queue_buffer(self->stream, buffer);
    }

    std::optional<std::string> open(uint32_t node_id,
                                    spa_audio_format format,
                                    uint32_t sample_rate_in,
                                    uint32_t channels_in) {
        static std::once_flag pipewire_once;
        std::call_once(pipewire_once, []() { pw_init(nullptr, nullptr); });

        sample_rate = sample_rate_in;
        channels = channels_in;
        spa_fmt = format;

        loop = pw_main_loop_new(nullptr);
        if (loop == nullptr) {
            return "pw_main_loop_new failed";
        }

        context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
        if (context == nullptr) {
            return "pw_context_new failed";
        }

        core = pw_context_connect(context, nullptr, 0);
        if (core == nullptr) {
            return "pw_context_connect failed (is PipeWire running?)";
        }

        stream = pw_stream_new(core,
                               "insightio-audio-capture",
                               pw_properties_new(nullptr, nullptr));
        if (stream == nullptr) {
            return "pw_stream_new failed";
        }

        stream_events = pw_stream_events {};
        stream_events.version = PW_VERSION_STREAM_EVENTS;
        stream_events.state_changed = &PipeWireCapture::on_state_changed;
        stream_events.process = &PipeWireCapture::on_process;
        pw_stream_add_listener(stream, &stream_listener, &stream_events, this);

        params_buffer.resize(1024);
        spa_pod_builder builder =
            SPA_POD_BUILDER_INIT(params_buffer.data(),
                                 static_cast<uint32_t>(params_buffer.size()));

        spa_audio_info_raw info {};
        info.format = spa_fmt;
        info.rate = sample_rate;
        info.channels = channels;
        if (channels == 1) {
            info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        } else if (channels == 2) {
            info.position[0] = SPA_AUDIO_CHANNEL_FL;
            info.position[1] = SPA_AUDIO_CHANNEL_FR;
        }

        params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
        if (params[0] == nullptr) {
            return "spa_format_audio_raw_build failed";
        }

        const auto flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS);
        const int connect_result =
            pw_stream_connect(stream, PW_DIRECTION_INPUT, node_id, flags, params.data(), params.size());
        if (connect_result < 0) {
            return "pw_stream_connect failed";
        }

        for (int attempt = 0; attempt < 50; ++attempt) {
            pw_loop_iterate(pw_main_loop_get_loop(loop), 0);
            if (ready) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!ready) {
            return last_error.empty() ? "PipeWire stream not ready"
                                      : ("PipeWire stream not ready: " + last_error);
        }

        return std::nullopt;
    }

    void close() {
        if (stream != nullptr) {
            pw_stream_disconnect(stream);
            spa_hook_remove(&stream_listener);
            pw_stream_destroy(stream);
            stream = nullptr;
        }
        if (core != nullptr) {
            pw_core_disconnect(core);
            core = nullptr;
        }
        if (context != nullptr) {
            pw_context_destroy(context);
            context = nullptr;
        }
        if (loop != nullptr) {
            pw_main_loop_destroy(loop);
            loop = nullptr;
        }
        pending.clear();
        ready = false;
        last_error.clear();
        params_buffer.clear();
        params[0] = nullptr;
        stream_events = pw_stream_events {};
    }

    [[nodiscard]] int iterate(int timeout_ms) {
        if (loop == nullptr) {
            return -1;
        }
        return pw_loop_iterate(pw_main_loop_get_loop(loop), timeout_ms);
    }

    std::optional<std::vector<uint8_t>> pop_buffer() {
        if (pending.empty()) {
            return std::nullopt;
        }
        auto output = std::move(pending.front());
        pending.pop_front();
        return output;
    }
};

#pragma GCC diagnostic pop

PipeWireWorker::PipeWireWorker(PipeWireWorkerConfig cfg)
    : CaptureWorker(cfg.name),
      cfg_(std::move(cfg)) {}

PipeWireWorker::~PipeWireWorker() { stop(); }

std::optional<std::string> PipeWireWorker::setup() {
    if (cfg_.node_id == 0) {
        return "pw_node_id is zero";
    }

    const uint32_t sample_rate = cfg_.caps.width;
    const uint32_t channels = cfg_.caps.height;
    if (sample_rate == 0 || channels == 0) {
        return "invalid sample_rate/channels";
    }

    const auto format = PipeWireFormatInfo::from_string(cfg_.caps.format);
    if (!format.has_value()) {
        return "unsupported pipewire format: " + cfg_.caps.format;
    }

    capture_ = std::make_unique<PipeWireCapture>();
    const auto err = capture_->open(cfg_.node_id,
                                    format->spa_fmt,
                                    sample_rate,
                                    channels);
    if (err.has_value()) {
        return err;
    }

    std::fprintf(stderr,
                 "PipeWire worker '%s' setup: node=%u %s\n",
                 cfg_.name.c_str(),
                 cfg_.node_id,
                 cfg_.caps.to_named().c_str());
    return std::nullopt;
}

void PipeWireWorker::run() {
    const uint32_t sample_rate = cfg_.caps.width;
    const uint32_t channels = cfg_.caps.height;
    const auto format = PipeWireFormatInfo::from_string(cfg_.caps.format);
    const int bytes_per_sample = format.has_value() ? format->bytes_per_sample : 2;
    const int64_t ns_per_sample = static_cast<int64_t>(1'000'000'000LL / sample_rate);
    uint64_t sample_index = 0;

    while (!stop_requested()) {
        if (capture_->iterate(50) < 0) {
            std::fprintf(stderr,
                         "PipeWire worker '%s' iterate error\n",
                         cfg_.name.c_str());
            break;
        }

        while (auto buffer = capture_->pop_buffer()) {
            const size_t size = buffer->size();
            if (size == 0) {
                continue;
            }

            const size_t samples =
                (bytes_per_sample == 0 || channels == 0)
                    ? 0
                    : size / (static_cast<size_t>(bytes_per_sample) * channels);
            const int64_t pts_ns = static_cast<int64_t>(sample_index) * ns_per_sample;
            sample_index += samples;

            deliver_frame("audio", buffer->data(), buffer->size(), pts_ns);
        }
    }
}

void PipeWireWorker::cleanup() {
    if (capture_) {
        capture_->close();
    }
    capture_.reset();
}

}  // namespace insightio::backend

#endif  // INSIGHTIO_HAS_PIPEWIRE
