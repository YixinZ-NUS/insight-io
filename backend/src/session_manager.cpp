/// InsightOS backend — Session manager implementation.
///
/// Coordinates CaptureSession and DeliverySession lifecycle.
/// CaptureSession is ref-counted: multiple DeliverySessions sharing
/// the same device_uri + preset share one CaptureSession.
///
/// Worker and publisher integration are stubs (M2-workers milestone).

#include "insightos/backend/session.hpp"
#include "insightos/backend/channel_registry.hpp"
#include "insightos/backend/discovery.hpp"
#include "insightos/backend/request_support.hpp"
#include "insightos/backend/runtime_paths.hpp"
#include "insightos/backend/worker.hpp"
#include "workers/v4l2_worker.hpp"
#include "workers/synthetic_worker.hpp"
#ifdef INSIGHTOS_HAS_PIPEWIRE
#include "workers/pipewire_worker.hpp"
#endif
#ifdef INSIGHTOS_HAS_ORBBEC
#include "workers/orbbec_worker.hpp"
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace insightos::backend {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string session_error_text(const Error& error) {
    if (error.code.empty()) {
        return error.message;
    }
    if (error.message.empty()) {
        return error.code;
    }
    return error.code + ": " + error.message;
}

bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool is_test_device_uri(std::string_view uri) {
    return uri.rfind("test:", 0) == 0;
}

bool stream_is_audio(const StreamState& stream) {
    return stream.fps == 0 || stream.sample_rate != 0 || stream.channels != 0 ||
           is_audio_format(stream.actual_format) ||
           is_compressed_audio(stream.actual_format);
}

SessionRequest parse_request_json(std::string_view request_json) {
    if (request_json.empty()) return {};
    try {
        const auto parsed = nlohmann::json::parse(request_json);
        auto request = session_request_from_json(parsed, RequestOrigin::kHttpApi);
        if (request.ok()) {
            return request.value();
        }
    } catch (const nlohmann::json::exception&) {
    }
    return {};
}

const StreamState* find_stream_by_name(const StreamSession& session,
                                       std::string_view stream_name) {
    for (const auto& stream : session.streams) {
        if (stream.stream_name == stream_name) {
            return &stream;
        }
    }
    return nullptr;
}

Result<std::vector<AppSourceView::Binding>> compute_target_bindings(
    const StreamSession& session, std::string_view target_kind) {
    std::vector<AppSourceView::Binding> bindings;

    auto bind = [&bindings](std::string role, const StreamState& stream) {
        bindings.push_back(AppSourceView::Binding{
            std::move(role),
            stream.stream_id,
            stream.stream_name,
        });
    };

    if (target_kind.empty() || target_kind == "video") {
        if (const auto* frame = find_stream_by_name(session, "frame")) {
            bind("primary", *frame);
            return Result<std::vector<AppSourceView::Binding>>::ok(
                std::move(bindings));
        }
        if (const auto* color = find_stream_by_name(session, "color")) {
            bind("primary", *color);
            return Result<std::vector<AppSourceView::Binding>>::ok(
                std::move(bindings));
        }
        for (const auto& stream : session.streams) {
            if (!stream_is_audio(stream)) {
                bind("primary", stream);
                return Result<std::vector<AppSourceView::Binding>>::ok(
                    std::move(bindings));
            }
        }
        return Result<std::vector<AppSourceView::Binding>>::err(
            {"incompatible_target",
             "Target kind 'video' requires one non-audio stream"});
    }

    if (target_kind == "audio") {
        if (const auto* audio = find_stream_by_name(session, "audio")) {
            bind("audio", *audio);
            return Result<std::vector<AppSourceView::Binding>>::ok(
                std::move(bindings));
        }
        for (const auto& stream : session.streams) {
            if (stream_is_audio(stream)) {
                bind("audio", stream);
                return Result<std::vector<AppSourceView::Binding>>::ok(
                    std::move(bindings));
            }
        }
        return Result<std::vector<AppSourceView::Binding>>::err(
            {"incompatible_target",
             "Target kind 'audio' requires one audio stream"});
    }

    if (target_kind == "rgbd") {
        const auto* color = find_stream_by_name(session, "color");
        const auto* depth = find_stream_by_name(session, "depth");
        if (!color || !depth) {
            return Result<std::vector<AppSourceView::Binding>>::err(
                {"incompatible_target",
                 "Target kind 'rgbd' requires both color and depth streams"});
        }
        bind("color", *color);
        bind("depth", *depth);
        if (const auto* ir = find_stream_by_name(session, "ir")) {
            bind("ir", *ir);
        }
        return Result<std::vector<AppSourceView::Binding>>::ok(
            std::move(bindings));
    }

    return Result<std::vector<AppSourceView::Binding>>::err(
        {"bad_request", "Unknown target kind '" + std::string(target_kind) + "'"});
}

size_t bytes_per_unit(std::string_view format) {
    if (format == "rgb24" || format == "bgr24") return 3;
    if (format == "rgba" || format == "bgra" || format == "s32le" ||
        format == "s32be" || format == "f32le" || format == "f32be") {
        return 4;
    }
    if (format == "yuyv" || format == "uyvy" || format == "y16" ||
        format == "z16" || format == "gray16" || format == "s16le" ||
        format == "s16be") {
        return 2;
    }
    if (format == "gray" || format == "gray8" || format == "u8") return 1;
    if (format == "s24le" || format == "s24be") return 3;
    return 0;
}

uint32_t default_buffer_slots(const StreamState& stream) {
    if (stream.fps == 0 || is_audio_format(stream.actual_format) ||
        is_compressed_audio(stream.actual_format)) {
        return 8;
    }
    return 4;
}

size_t estimate_payload_bytes(const StreamState& stream) {
    const size_t unit = bytes_per_unit(stream.actual_format);
    const size_t width = static_cast<size_t>(std::max(stream.actual_width, 1U));
    const size_t height = static_cast<size_t>(std::max(stream.actual_height, 1U));

    if (stream.fps == 0 || is_audio_format(stream.actual_format) ||
        is_compressed_audio(stream.actual_format)) {
        const size_t estimate = width * height * std::max<size_t>(unit, 2);
        return std::max<size_t>(estimate, 256 * 1024);
    }

    size_t estimate = width * height * std::max<size_t>(unit, 1);
    if (is_compressed_video(stream.actual_format)) {
        estimate = std::max<size_t>(estimate, width * height * 4);
    }
    return std::max<size_t>(estimate, 512 * 1024);
}

std::optional<std::vector<std::string>> rtsp_input_args(const StreamState& stream) {
    const std::string fps = std::to_string(stream.fps == 0 ? 30 : stream.fps);
    const std::string video_size =
        std::to_string(stream.actual_width) + "x" +
        std::to_string(stream.actual_height);

    if (stream.actual_format == "mjpeg") {
        return std::vector<std::string>{
            "-f", "mjpeg",
            "-framerate", fps,
            "-i", "pipe:0",
        };
    }
    if (stream.actual_format == "h264") {
        return std::vector<std::string>{"-f", "h264", "-i", "pipe:0"};
    }
    if (stream.actual_format == "h265" || stream.actual_format == "hevc") {
        return std::vector<std::string>{"-f", "hevc", "-i", "pipe:0"};
    }

    std::string pixel_format;
    if (stream.actual_format == "yuyv") pixel_format = "yuyv422";
    else if (stream.actual_format == "uyvy") pixel_format = "uyvy422";
    else if (stream.actual_format == "nv12") pixel_format = "nv12";
    else if (stream.actual_format == "nv21") pixel_format = "nv21";
    else if (stream.actual_format == "rgb24") pixel_format = "rgb24";
    else if (stream.actual_format == "bgr24") pixel_format = "bgr24";
    else if (stream.actual_format == "gray8") pixel_format = "gray";
    else if (stream.actual_format == "y16" || stream.actual_format == "gray16" ||
             stream.actual_format == "z16") pixel_format = "gray16le";

    if (pixel_format.empty()) return std::nullopt;

    return std::vector<std::string>{
        "-f", "rawvideo",
        "-pixel_format", pixel_format,
        "-video_size", video_size,
        "-framerate", fps,
        "-i", "pipe:0",
    };
}

std::string session_state_to_string(SessionState state) {
    switch (state) {
        case SessionState::kPending:  return "pending";
        case SessionState::kStarting: return "starting";
        case SessionState::kActive:   return "active";
        case SessionState::kStopping: return "stopping";
        case SessionState::kStopped:  return "stopped";
        case SessionState::kError:    return "error";
    }
    return "unknown";
}

std::string request_origin_to_string(RequestOrigin origin) {
    switch (origin) {
        case RequestOrigin::kUri: return "uri";
        case RequestOrigin::kHttpApi: return "http_api";
        case RequestOrigin::kFrontend: return "frontend";
        case RequestOrigin::kSdk: return "sdk";
        case RequestOrigin::kInternal: return "internal";
        case RequestOrigin::kUnknown: break;
    }
    return "unknown";
}

RequestOrigin request_origin_from_string(std::string_view origin) {
    if (origin == "uri") return RequestOrigin::kUri;
    if (origin == "http_api") return RequestOrigin::kHttpApi;
    if (origin == "frontend") return RequestOrigin::kFrontend;
    if (origin == "sdk") return RequestOrigin::kSdk;
    if (origin == "internal") return RequestOrigin::kInternal;
    return RequestOrigin::kUnknown;
}

std::string transport_kind_to_string(TransportKind transport_kind) {
    return transport_kind == TransportKind::kRtsp ? "rtsp" : "ipc";
}

std::string preset_id_for(std::string_view device_key,
                          std::string_view preset_name) {
    return std::string(device_key) + "::" + std::string(preset_name);
}

std::string stream_key_for(std::string_view device_key,
                           std::string_view stream_id) {
    return std::string(device_key) + "::" + std::string(stream_id);
}

TransportKind transport_kind_for(std::string_view delivery_name) {
    return delivery_name == "rtsp" ? TransportKind::kRtsp
                                   : TransportKind::kIpc;
}

std::string capture_policy_json_for(const ResolvedSession& resolved) {
    nlohmann::json policy = nlohmann::json::object();
    policy["device_uri"] = resolved.device_uri;
    policy["d2c"] = to_string(resolved.d2c);

    auto streams = nlohmann::json::array();
    for (const auto& stream : resolved.streams) {
        // Capture reuse is keyed by source-side worker policy only. Promise
        // changes like `mjpeg` vs `rtsp` must fan out through delivery rows
        // without splitting capture reuse.
        streams.push_back({
            {"stream_id", stream.stream_id},
            {"caps", stream.chosen_caps.to_named()},
        });
    }
    policy["streams"] = std::move(streams);
    return policy.dump();
}

SessionRow make_session_row(std::string session_id,
                            const SessionRequest& request) {
    SessionRow row;
    row.session_id = std::move(session_id);
    row.state = "pending";
    row.request_name = request.selector.name;
    row.request_device_uuid = request.selector.device_uuid;
    row.request_preset_name = request.preset_name;
    row.request_delivery_name = request.delivery_name.value_or("");
    row.request_origin = request_origin_to_string(request.origin);

    nlohmann::json overrides = nlohmann::json::object();
    if (request.overrides.audio_rate) {
        overrides["audio_rate"] = *request.overrides.audio_rate;
    }
    if (request.overrides.audio_format) {
        overrides["audio_format"] = *request.overrides.audio_format;
    }
    if (request.overrides.channels) {
        overrides["channels"] = *request.overrides.channels;
    }
    if (request.overrides.depth_alignment) {
        overrides["depth_alignment"] = *request.overrides.depth_alignment;
    }
    if (request.overrides.must_match) {
        overrides["must_match"] = true;
    }
    row.request_overrides_json = overrides.dump();
    return row;
}

SessionRequest request_from_row(const SessionRow& row) {
    SessionRequest request;
    request.selector.name = row.request_name;
    request.selector.device_uuid = row.request_device_uuid;
    request.preset_name = row.request_preset_name;
    if (!row.request_delivery_name.empty()) {
        request.delivery_name = row.request_delivery_name;
    }
    request.origin = request_origin_from_string(row.request_origin);

    nlohmann::json overrides = nlohmann::json::object();
    try {
        overrides = nlohmann::json::parse(row.request_overrides_json);
    } catch (const nlohmann::json::exception&) {
        return request;
    }

    if (overrides.contains("audio_rate") && overrides["audio_rate"].is_number_unsigned()) {
        request.overrides.audio_rate = overrides["audio_rate"].get<std::uint32_t>();
    }
    if (overrides.contains("audio_format") && overrides["audio_format"].is_string()) {
        request.overrides.audio_format = overrides["audio_format"].get<std::string>();
    }
    if (overrides.contains("channels") && overrides["channels"].is_number_unsigned()) {
        request.overrides.channels = overrides["channels"].get<std::uint32_t>();
    }
    if (overrides.contains("depth_alignment") &&
        overrides["depth_alignment"].is_string()) {
        request.overrides.depth_alignment =
            overrides["depth_alignment"].get<std::string>();
    }
    if (overrides.contains("must_match") && overrides["must_match"].is_boolean()) {
        request.overrides.must_match = overrides["must_match"].get<bool>();
    }
    return request;
}

SessionRequest restart_request_from_row(const SessionRow& row) {
    auto request = request_from_row(row);

    if (request.selector.device_uuid.empty() && !row.device_uuid.empty()) {
        request.selector.device_uuid = row.device_uuid;
    }
    if (!request.selector.device_uuid.empty()) {
        request.selector.name.clear();
    }

    return request;
}

std::vector<SessionBindingRow> session_binding_rows_from(
    std::string_view session_id,
    const std::vector<std::string>& delivery_session_ids) {
    std::vector<SessionBindingRow> rows;
    rows.reserve(delivery_session_ids.size());
    for (const auto& delivery_session_id : delivery_session_ids) {
        rows.push_back(SessionBindingRow{
            std::string(session_id),
            delivery_session_id,
        });
    }
    return rows;
}

bool is_local_ipc_consumable_source(const NormalizedSourceInput& source) {
    if (!source.is_local) {
        return false;
    }

    const auto delivery = source.request.delivery_name.value_or("");
    return delivery != "rtsp";
}

}  // namespace

// ─── CaptureKey ─────────────────────────────────────────────────────────

bool CaptureReuseKey::operator<(const CaptureReuseKey& o) const {
    if (preset_id != o.preset_id) return preset_id < o.preset_id;
    return capture_policy_key < o.capture_policy_key;
}

bool CaptureReuseKey::operator==(const CaptureReuseKey& o) const {
    return preset_id == o.preset_id &&
           capture_policy_key == o.capture_policy_key;
}

std::string CaptureReuseKey::to_string() const {
    return preset_id + "|" + capture_policy_key;
}

// ─── DeliveryKey ────────────────────────────────────────────────────────

bool PublicationKey::operator<(const PublicationKey& o) const {
    if (capture_session_id != o.capture_session_id) {
        return capture_session_id < o.capture_session_id;
    }
    if (stream_key != o.stream_key) return stream_key < o.stream_key;
    if (delivery_name != o.delivery_name) return delivery_name < o.delivery_name;
    return transport_kind < o.transport_kind;
}

bool PublicationKey::operator==(const PublicationKey& o) const {
    return capture_session_id == o.capture_session_id &&
           stream_key == o.stream_key &&
           delivery_name == o.delivery_name &&
           transport_kind == o.transport_kind;
}

std::string PublicationKey::to_string() const {
    return slugify(capture_session_id) + "__" +
           slugify(delivery_name) + "__" +
           transport_kind_to_string(transport_kind) + "__" +
           slugify(public_stream_name.empty() ? stream_key : public_stream_name);
}

// ─── CaptureSession ────────────────────────────────────────────────────

CaptureSession::CaptureSession(std::string capture_session_id,
                               CaptureReuseKey key, const DeviceInfo& device,
                               const ResolvedSession& resolution)
    : capture_session_id_(std::move(capture_session_id)),
      key_(std::move(key)),
      device_(device),
      resolution_(resolution) {}

bool CaptureSession::start() {
    if (state_ == SessionState::kActive && worker_) {
        return true;
    }

    state_ = SessionState::kStarting;
    const auto& res = resolution_;

    {
        std::lock_guard lock(frame_counts_mutex_);
        frame_counts_.clear();
        for (const auto& stream : res.streams) {
            frame_counts_.try_emplace(stream.stream_id, 0);
        }
    }

    if (is_test_device_uri(device_.uri)) {
        SyntheticWorkerConfig wcfg;
        wcfg.name = res.name;
        for (const auto& stream : res.streams) {
            wcfg.streams.push_back(
                SyntheticStreamConfig{stream.stream_id, stream.chosen_caps});
        }
        worker_ = std::make_unique<SyntheticWorker>(std::move(wcfg));
    } else switch (device_.kind) {
    case DeviceKind::kV4l2: {
        if (res.streams.empty()) {
            state_ = SessionState::kError;
            return false;
        }
        V4l2WorkerConfig wcfg;
        wcfg.name = res.name;
        wcfg.device_path = device_.uri.rfind("v4l2:", 0) == 0
            ? device_.uri.substr(5)
            : device_.uri;
        wcfg.caps = res.streams.front().chosen_caps;
        worker_ = std::make_unique<V4l2Worker>(std::move(wcfg));
        break;
    }
#ifdef INSIGHTOS_HAS_PIPEWIRE
    case DeviceKind::kPipeWire: {
        if (res.streams.empty()) {
            state_ = SessionState::kError;
            return false;
        }
        PipeWireWorkerConfig wcfg;
        wcfg.name = res.name;
        wcfg.node_id = static_cast<uint32_t>(std::stoul(device_.uri.substr(3)));
        wcfg.caps = res.streams.front().chosen_caps;
        worker_ = std::make_unique<PipeWireWorker>(std::move(wcfg));
        break;
    }
#endif
#ifdef INSIGHTOS_HAS_ORBBEC
    case DeviceKind::kOrbbec: {
        OrbbecWorkerConfig wcfg;
        wcfg.name = res.name;
        wcfg.uri = device_.uri;
        wcfg.d2c = res.d2c;
        for (const auto& stream : res.streams) {
            wcfg.streams.push_back(OrbbecStreamConfig{stream.stream_id,
                                                      stream.chosen_caps});
        }
        worker_ = std::make_unique<OrbbecWorker>(std::move(wcfg));
        break;
    }
#endif
    default:
        std::fprintf(stderr,
                     "CaptureSession: unsupported device kind for %s\n",
                     key_.to_string().c_str());
        state_ = SessionState::kError;
        return false;
    }

    if (!worker_) {
        state_ = SessionState::kError;
        return false;
    }

    worker_->set_frame_callback(
        [this](const std::string& stream_name, const uint8_t* data, size_t size,
               int64_t pts_ns, uint32_t flags) {
            record_frame(stream_name);

            std::vector<StreamSink> sinks;
            {
                std::lock_guard lock(sinks_mutex_);
                auto it = sinks_.find(stream_name);
                if (it != sinks_.end()) {
                    sinks.reserve(it->second.size());
                    for (const auto& entry : it->second) {
                        sinks.push_back(entry.second);
                    }
                }
            }

            for (const auto& sink : sinks) {
                sink(data, size, pts_ns, flags);
            }
        });

    std::string err;
    if (!worker_->start(err)) {
        std::fprintf(stderr,
                     "CaptureSession: worker start failed for %s: %s\n",
                     key_.to_string().c_str(), err.c_str());
        worker_.reset();
        state_ = SessionState::kError;
        return false;
    }

    std::fprintf(stderr, "CaptureSession: started worker for %s\n",
                 key_.to_string().c_str());
    state_ = SessionState::kActive;
    return true;
}

void CaptureSession::stop() {
    state_ = SessionState::kStopping;
    if (worker_) {
        worker_->stop();
        worker_.reset();
    }
    std::fprintf(stderr, "CaptureSession: stopped %s\n",
                 key_.to_string().c_str());
    state_ = SessionState::kStopped;
}

void CaptureSession::record_frame(std::string_view stream_name) {
    std::lock_guard lock(frame_counts_mutex_);
    ++frame_counts_[std::string(stream_name)];
}

std::uint64_t CaptureSession::frame_count(std::string_view stream_name) const {
    std::lock_guard lock(frame_counts_mutex_);
    auto it = frame_counts_.find(std::string(stream_name));
    return it == frame_counts_.end() ? 0 : it->second;
}

std::uint64_t CaptureSession::add_sink(std::string_view stream_name, StreamSink sink) {
    std::lock_guard lock(sinks_mutex_);
    const auto sink_id = next_sink_id_++;
    sinks_[std::string(stream_name)].push_back({sink_id, std::move(sink)});
    return sink_id;
}

void CaptureSession::remove_sink(std::string_view stream_name, std::uint64_t sink_id) {
    std::lock_guard lock(sinks_mutex_);
    auto it = sinks_.find(std::string(stream_name));
    if (it == sinks_.end()) return;

    auto& sink_list = it->second;
    sink_list.erase(
        std::remove_if(sink_list.begin(), sink_list.end(),
                       [sink_id](const auto& entry) { return entry.first == sink_id; }),
        sink_list.end());
    if (sink_list.empty()) {
        sinks_.erase(it);
    }
}

bool CaptureSession::release() {
    return --ref_count_ <= 0;
}

// ─── DeliverySession ───────────────────────────────────────────────────

DeliverySession::DeliverySession(
    std::string delivery_session_id, PublicationKey key,
    const ResolvedSession::StreamResolution& stream_res,
    std::shared_ptr<CaptureSession> capture)
    : delivery_session_id_(std::move(delivery_session_id)),
      key_(std::move(key)),
      capture_(std::move(capture)) {
    stream_state_.stream_id = stream_res.stream_id;
    stream_state_.stream_name = stream_res.stream_name;
    stream_state_.promised_format = stream_res.promised_format;
    stream_state_.actual_format = stream_res.chosen_caps.format;
    stream_state_.actual_width = stream_res.chosen_caps.width;
    stream_state_.actual_height = stream_res.chosen_caps.height;
    stream_state_.sample_rate = stream_res.chosen_caps.sample_rate();
    stream_state_.channels = stream_res.chosen_caps.channels();
    stream_state_.fps = stream_res.chosen_caps.fps;

    // D2C-aligned depth frames are delivered in color resolution even when the
    // sensor-native depth profile is smaller. Expose the delivered dimensions
    // here so status, JSON, and IPC allocation match the actual frame shape.
    if (capture_ &&
        capture_->resolution().d2c != D2CMode::kOff &&
        stream_state_.stream_id == "depth") {
        for (const auto& sibling : capture_->resolution().streams) {
            if (sibling.stream_id == "color") {
                stream_state_.actual_width = sibling.chosen_caps.width;
                stream_state_.actual_height = sibling.chosen_caps.height;
                break;
            }
        }
    }

    stream_state_.transport = transport_kind_to_string(key_.transport_kind);
}

bool DeliverySession::release() {
    return --ref_count_ <= 0;
}

bool DeliverySession::start(ChannelRegistry& registry) {
    if (state_ == SessionState::kActive) {
        return true;
    }

    state_ = SessionState::kStarting;

    if (key_.transport_kind == TransportKind::kRtsp) {
        auto cap = capture_;
        if (!cap || cap->device().kind == DeviceKind::kPipeWire) {
            std::fprintf(stderr,
                         "DeliverySession: RTSP not supported for audio streams\n");
            state_ = SessionState::kError;
            return false;
        }

        auto input_args = rtsp_input_args(stream_state_);
        if (!input_args) {
            std::fprintf(stderr,
                         "DeliverySession: unsupported RTSP source format %s for %s/%s\n",
                         stream_state_.actual_format.c_str(),
                         cap->resolution().name.c_str(),
                         stream_state_.stream_id.c_str());
            state_ = SessionState::kError;
            return false;
        }

        const std::string rtsp_path =
            cap->resolution().name + "/" + stream_state_.stream_name;
        const std::string rtsp_url = "rtsp://127.0.0.1:8554/" + rtsp_path;
        stream_state_.rtsp_url = rtsp_url;

        int pipe_fds[2] = {-1, -1};
        if (::pipe(pipe_fds) != 0) {
            std::fprintf(stderr,
                         "DeliverySession: pipe() failed for RTSP: %s\n",
                         std::strerror(errno));
            state_ = SessionState::kError;
            return false;
        }

        const pid_t pid = fork();
        if (pid == 0) {
            ::dup2(pipe_fds[0], STDIN_FILENO);
            ::close(pipe_fds[0]);
            ::close(pipe_fds[1]);
            (void)std::freopen("/dev/null", "w", stdout);
            (void)std::freopen("/dev/null", "w", stderr);
            setsid();

            std::vector<std::string> args = {"ffmpeg", "-nostdin"};
            args.insert(args.end(), input_args->begin(), input_args->end());
            args.insert(args.end(), {
                "-c:v", "libx264",
                "-preset", "ultrafast",
                "-tune", "zerolatency",
                "-g", std::to_string(stream_state_.fps == 0 ? 30 : stream_state_.fps),
                "-f", "rtsp",
                "-rtsp_transport", "tcp",
                rtsp_url,
            });

            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.data()));
            argv.push_back(nullptr);
            ::execvp("ffmpeg", argv.data());
            _exit(127);
        }

        ::close(pipe_fds[0]);
        if (pid < 0) {
            ::close(pipe_fds[1]);
            std::fprintf(stderr,
                         "DeliverySession: fork() failed for RTSP\n");
            state_ = SessionState::kError;
            return false;
        }

        rtsp_stdin_fd_ = pipe_fds[1];
        set_nonblocking(rtsp_stdin_fd_);
        rtsp_pid_ = pid;
        rtsp_sink_id_ = cap->add_sink(
            stream_state_.stream_id,
            [fd = rtsp_stdin_fd_](const uint8_t* data, size_t size,
                                  int64_t, uint32_t flags) {
                if (fd < 0 || data == nullptr || size == 0 ||
                    (flags & ipc::kFlagEndOfStream) != 0) {
                    return;
                }

                size_t offset = 0;
                while (offset < size) {
                    ssize_t written = ::write(fd, data + offset, size - offset);
                    if (written > 0) {
                        offset += static_cast<size_t>(written);
                        continue;
                    }
                    if (written < 0 && errno == EINTR) {
                        continue;
                    }
                    if (written < 0 &&
                        (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    break;
                }
            });

        std::fprintf(stderr,
                     "DeliverySession: RTSP ffmpeg started pid=%d url=%s\n",
                     static_cast<int>(pid), rtsp_url.c_str());
    } else {
        auto writer_res =
            registry.get_or_create(key_, default_buffer_slots(stream_state_),
                                   estimate_payload_bytes(stream_state_));
        if (!writer_res.ok()) {
            std::fprintf(stderr,
                         "DeliverySession: IPC channel creation failed for %s: %s\n",
                         key_.to_string().c_str(),
                         writer_res.error().message.c_str());
            state_ = SessionState::kError;
            return false;
        }

        ipc_writer_ = writer_res.value();
        auto cap = capture_;
        if (!cap || !ipc_writer_ || !ipc_writer_->is_ready()) {
            state_ = SessionState::kError;
            return false;
        }

        ipc_sink_id_ = cap->add_sink(
            stream_state_.stream_id,
            [writer = ipc_writer_](const uint8_t* data, size_t size, int64_t pts_ns,
                                   uint32_t flags) {
                if (!writer || !writer->is_ready()) return;
                writer->write(data, size, pts_ns, pts_ns, flags);
            });
    }

    state_ = SessionState::kActive;
    return true;
}

void DeliverySession::stop() {
    state_ = SessionState::kStopping;

    if (capture_ && rtsp_sink_id_ != 0) {
        capture_->remove_sink(stream_state_.stream_id, rtsp_sink_id_);
        rtsp_sink_id_ = 0;
    }
    if (ipc_writer_) {
        ipc_writer_->write(nullptr, 0, 0, 0, ipc::kFlagEndOfStream);
    }
    if (capture_ && ipc_sink_id_ != 0) {
        capture_->remove_sink(stream_state_.stream_id, ipc_sink_id_);
        ipc_sink_id_ = 0;
    }
    ipc_writer_.reset();

    if (rtsp_stdin_fd_ >= 0) {
        ::close(rtsp_stdin_fd_);
        rtsp_stdin_fd_ = -1;
    }

    if (rtsp_pid_ > 0) {
        int status = 0;
        bool exited = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            pid_t waited = ::waitpid(rtsp_pid_, &status, WNOHANG);
            if (waited == rtsp_pid_) {
                exited = true;
                break;
            }
            ::usleep(50000);
        }
        if (!exited) {
            ::kill(rtsp_pid_, SIGTERM);
            for (int attempt = 0; attempt < 20; ++attempt) {
                pid_t waited = ::waitpid(rtsp_pid_, &status, WNOHANG);
                if (waited == rtsp_pid_) {
                    exited = true;
                    break;
                }
                ::usleep(50000);
            }
        }
        if (!exited) {
            ::kill(rtsp_pid_, SIGKILL);
            ::waitpid(rtsp_pid_, &status, 0);
        }
        std::fprintf(stderr,
                     "DeliverySession: stopped RTSP ffmpeg pid=%d\n",
                     static_cast<int>(rtsp_pid_));
        rtsp_pid_ = -1;
    }

    state_ = SessionState::kStopped;
}

// ─── SessionManager ────────────────────────────────────────────────────

SessionManager::SessionManager(std::string db_path)
    : store_(db_path.empty() ? default_database_path() : std::move(db_path)),
      channels_(std::make_unique<ChannelRegistry>()) {
    if (!store_.open()) {
        throw std::runtime_error("failed to open device store at " + store_.path());
    }
    store_.reset_runtime_state();
    daemon_run_id_ = generate_runtime_id("run");
    if (!store_.start_daemon_run(DaemonRunRow{
            .daemon_run_id = daemon_run_id_,
            .state = "active",
            .started_at_ms = now_ms(),
            .ended_at_ms = 0,
            .pid = static_cast<std::int64_t>(::getpid()),
            .version = "0.1.0",
            .last_heartbeat_at_ms = now_ms(),
        })) {
        throw std::runtime_error("failed to create daemon run in device store");
    }
}

SessionManager::~SessionManager() {
    const auto stopped_at = now_ms();
    std::lock_guard lock(mutex_);

    for (auto& [delivery_session_id, delivery] : delivery_sessions_) {
        if (!delivery) continue;
        delivery->stop();
        channels_->remove(delivery->key());
        store_.set_delivery_session_state(delivery_session_id, "stopped",
                                          stopped_at);
    }
    delivery_sessions_.clear();

    for (auto& [capture_session_id, capture] : capture_sessions_) {
        if (!capture) continue;
        capture->stop();
        store_.set_capture_session_state(capture_session_id, "stopped",
                                         stopped_at);
    }
    capture_sessions_.clear();

    for (auto row : store_.get_sessions()) {
        if (row.state == "active" || row.state == "starting" ||
            row.state == "stopping") {
            row.state = "stopped";
            row.stopped_at_ms = stopped_at;
        }
        store_.save_session(row, {});
    }

    store_.finish_daemon_run(daemon_run_id_, stopped_at);
}

/// Refresh devices_cache_ from the store and rebuild the catalog.
/// Must be called while holding mutex_.
void SessionManager::reload_devices_locked() {
    devices_cache_ = store_.get_devices();
    catalog_.build_from_discovery(devices_cache_);
}

bool SessionManager::initialize() {
    std::lock_guard lock(mutex_);
    auto result = discover_all();
    if (!store_.replace_devices(result.devices)) {
        return false;
    }
    reload_devices_locked();
    return true;
}

bool SessionManager::refresh() {
    std::lock_guard lock(mutex_);
    auto result = discover_all();
    if (!store_.replace_devices(result.devices)) {
        return false;
    }
    reload_devices_locked();
    // Active sessions are preserved — they reference device copies
    return true;
}

void SessionManager::set_ipc_socket_path(std::string path) {
    std::lock_guard lock(mutex_);
    ipc_socket_path_ = std::move(path);
}

Result<std::string> SessionManager::set_device_alias(const std::string& device_id,
                                                      const std::string& alias) {
    std::lock_guard lock(mutex_);

    const auto* ep = catalog_.find_endpoint(device_id);
    if (!ep) {
        return Result<std::string>::err({"not_found",
                                         "Device '" + device_id + "' not found"});
    }
    const auto device_key = ep->device_key;

    auto res = store_.set_alias(device_key, alias);
    if (!res.ok()) {
        return Result<std::string>::err(res.error());
    }

    reload_devices_locked();
    const auto* new_ep = catalog_.find_by_key(device_key);
    return Result<std::string>::ok(new_ep ? new_ep->name : device_id);
}

Result<std::string> SessionManager::clear_device_alias(const std::string& device_id) {
    std::lock_guard lock(mutex_);

    const auto* ep = catalog_.find_endpoint(device_id);
    if (!ep) {
        return Result<std::string>::err({"not_found",
                                         "Device '" + device_id + "' not found"});
    }
    const auto device_key = ep->device_key;

    auto res = store_.clear_alias(device_key);
    if (!res.ok()) {
        return Result<std::string>::err(res.error());
    }

    reload_devices_locked();
    const auto* new_ep = catalog_.find_by_key(device_key);
    return Result<std::string>::ok(new_ep ? new_ep->name : device_id);
}

Result<std::string> SessionManager::set_stream_alias(
    const std::string& device_id, const std::string& stream_name,
    const std::string& alias) {
    std::lock_guard lock(mutex_);

    const auto* ep = catalog_.find_endpoint(device_id);
    if (!ep) {
        return Result<std::string>::err({"not_found",
                                         "Device '" + device_id + "' not found"});
    }

    const auto* device = find_device(ep->device_uri);
    if (!device) {
        return Result<std::string>::err({"not_found",
                                         "Device '" + device_id + "' is offline"});
    }

    const auto stream_it = std::find_if(
        device->streams.begin(), device->streams.end(),
        [&stream_name](const StreamInfo& stream) {
            return stream.name == stream_name;
        });
    if (stream_it == device->streams.end()) {
        return Result<std::string>::err(
            {"not_found",
             "Stream '" + stream_name + "' not found for device '" +
                 device_id + "'"});
    }

    const auto stream_id = stream_it->stream_id;
    auto res = store_.set_stream_alias(ep->device_key, stream_id, alias);
    if (!res.ok()) {
        return Result<std::string>::err(res.error());
    }

    reload_devices_locked();
    const auto* updated_device = find_device(ep->device_uri);
    if (!updated_device) {
        return Result<std::string>::err(
            {"internal", "Updated device disappeared from cache"});
    }
    const auto updated_stream_it = std::find_if(
        updated_device->streams.begin(), updated_device->streams.end(),
        [&stream_id](const StreamInfo& stream) {
            return stream.stream_id == stream_id;
        });
    return Result<std::string>::ok(
        updated_stream_it != updated_device->streams.end()
            ? updated_stream_it->name
            : res.value());
}

Result<std::string> SessionManager::clear_stream_alias(
    const std::string& device_id, const std::string& stream_name) {
    std::lock_guard lock(mutex_);

    const auto* ep = catalog_.find_endpoint(device_id);
    if (!ep) {
        return Result<std::string>::err({"not_found",
                                         "Device '" + device_id + "' not found"});
    }

    const auto* device = find_device(ep->device_uri);
    if (!device) {
        return Result<std::string>::err({"not_found",
                                         "Device '" + device_id + "' is offline"});
    }

    const auto stream_it = std::find_if(
        device->streams.begin(), device->streams.end(),
        [&stream_name](const StreamInfo& stream) {
            return stream.name == stream_name;
        });
    if (stream_it == device->streams.end()) {
        return Result<std::string>::err(
            {"not_found",
             "Stream '" + stream_name + "' not found for device '" +
                 device_id + "'"});
    }

    const auto stream_id = stream_it->stream_id;
    auto res = store_.clear_stream_alias(ep->device_key, stream_id);
    if (!res.ok()) {
        return Result<std::string>::err(res.error());
    }

    reload_devices_locked();
    const auto* updated_device = find_device(ep->device_uri);
    if (!updated_device) {
        return Result<std::string>::err(
            {"internal", "Updated device disappeared from cache"});
    }
    const auto updated_stream_it = std::find_if(
        updated_device->streams.begin(), updated_device->streams.end(),
        [&stream_id](const StreamInfo& stream) {
            return stream.stream_id == stream_id;
        });
    return Result<std::string>::ok(
        updated_stream_it != updated_device->streams.end()
            ? updated_stream_it->name
            : res.value());
}

const std::vector<DeviceInfo>& SessionManager::devices() const {
    return devices_cache_;
}

const DeviceInfo* SessionManager::find_device(const std::string& uri) const {
    for (auto& d : devices_cache_) {
        if (d.uri == uri) return &d;
    }
    return nullptr;
}

const CatalogEndpoint* SessionManager::current_endpoint_locked(
    std::string_view device_uuid) const {
    if (!device_uuid.empty()) {
        if (const auto* endpoint = catalog_.find_by_uuid(device_uuid)) {
            return endpoint;
        }
    }
    return nullptr;
}

std::string SessionManager::current_device_name_locked(
    std::string_view device_uuid, std::string_view fallback) const {
    if (const auto* endpoint = current_endpoint_locked(device_uuid)) {
        return endpoint->name;
    }
    return std::string(fallback);
}

std::string SessionManager::generate_session_id() const {
    // 8-byte random hex
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uint64_t val = rng();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << val;
    return oss.str();
}

std::string SessionManager::generate_runtime_id(std::string_view prefix) const {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << prefix << '-'
        << std::hex << std::setfill('0') << std::setw(16) << rng();
    return oss.str();
}

Result<StreamSession> SessionManager::activate_session_locked(
    SessionRow& row, const SessionRequest& request,
    std::vector<std::string>& delivery_session_ids) {
    auto resolved_or_err = catalog_.resolve(request, devices_cache_);
    if (auto* err = std::get_if<ResolutionError>(&resolved_or_err)) {
        return Result<StreamSession>::err({err->code, err->message});
    }
    auto& resolved = std::get<ResolvedSession>(resolved_or_err);

    auto* device = find_device(resolved.device_uri);
    if (!device) {
        return Result<StreamSession>::err({
            "device_offline",
            "Device '" + resolved.device_uri + "' disappeared after resolution",
        });
    }

    const auto started_at = now_ms();
    const auto preset_id = preset_id_for(resolved.device_key, resolved.preset);
    const auto capture_policy_json = capture_policy_json_for(resolved);
    CaptureReuseKey cap_key{preset_id, capture_policy_json};

    auto capture_row = store_.find_capture_session_by_reuse(
        daemon_run_id_, cap_key.preset_id, cap_key.capture_policy_key);
    const auto capture_session_id = capture_row
        ? capture_row->capture_session_id
        : generate_runtime_id("cap");

    auto cap_it = capture_sessions_.find(capture_session_id);
    std::shared_ptr<CaptureSession> capture;
    if (cap_it != capture_sessions_.end()) {
        capture = cap_it->second;
    } else {
        capture = std::make_shared<CaptureSession>(
            capture_session_id, cap_key, *device, resolved);
        if (!capture->start()) {
            return Result<StreamSession>::err({
                "capture_start_failed",
                "Failed to start capture worker for '" + cap_key.to_string() + "'",
            });
        }
        capture_sessions_[capture_session_id] = capture;
        if (!store_.save_capture_session(CaptureSessionRow{
                .capture_session_id = capture_session_id,
                .daemon_run_id = daemon_run_id_,
                .preset_id = preset_id,
                .capture_policy_key = cap_key.capture_policy_key,
                .capture_policy_json = capture_policy_json,
                .state = "active",
                .started_at_ms =
                    capture_row && capture_row->started_at_ms != 0
                        ? capture_row->started_at_ms
                        : started_at,
                .stopped_at_ms = 0,
                .last_error = "",
            })) {
            capture->stop();
            capture_sessions_.erase(capture_session_id);
            return Result<StreamSession>::err({
                "internal",
                "Failed to persist capture session '" + capture_session_id + "'",
            });
        }
    }
    capture->add_ref();

    auto rollback = [&](const std::string& code, const std::string& message)
        -> Result<StreamSession> {
        release_delivery_sessions_locked(delivery_session_ids);
        if (capture && delivery_session_ids.empty() && capture->release()) {
            capture->stop();
            store_.set_capture_session_state(capture->capture_session_id(),
                                             "stopped", now_ms());
            capture_sessions_.erase(capture->capture_session_id());
        }
        return Result<StreamSession>::err({code, message});
    };

    for (const auto& stream_res : resolved.streams) {
        const auto stream_key = stream_key_for(resolved.device_key,
                                               stream_res.stream_id);
        const auto transport_kind = transport_kind_for(resolved.delivery);
        const auto transport = transport_kind_to_string(transport_kind);
        PublicationKey publication_key{
            capture_session_id,
            stream_key,
            resolved.delivery,
            transport_kind,
            stream_res.stream_name,
        };

        auto delivery_row = store_.find_delivery_session_by_reuse(
            capture_session_id, stream_key, resolved.delivery, transport);
        const auto delivery_session_id = delivery_row
            ? delivery_row->delivery_session_id
            : generate_runtime_id("del");

        auto d_it = delivery_sessions_.find(delivery_session_id);
        if (d_it == delivery_sessions_.end()) {
            auto delivery = std::make_shared<DeliverySession>(
                delivery_session_id, publication_key, stream_res, capture);
            delivery->add_ref();
            if (!delivery->start(*channels_)) {
                return rollback(
                    "delivery_start_failed",
                    "Failed to start delivery '" + publication_key.to_string() + "'");
            }

            const auto& stream_state = delivery->stream_state();
            if (!store_.save_delivery_session(DeliverySessionRow{
                    .delivery_session_id = delivery_session_id,
                    .capture_session_id = capture_session_id,
                    .stream_key = stream_key,
                    .delivery_name = resolved.delivery,
                    .transport = transport,
                    .promised_format = stream_state.promised_format,
                    .actual_format = stream_state.actual_format,
                    .channel_id = stream_state.transport == "ipc"
                        ? channels_->channel_id(publication_key)
                        : std::string{},
                    .rtsp_url = stream_state.rtsp_url,
                    .state = "active",
                    .started_at_ms =
                        delivery_row && delivery_row->started_at_ms != 0
                            ? delivery_row->started_at_ms
                            : started_at,
                    .stopped_at_ms = 0,
                    .last_error = "",
                })) {
                delivery->stop();
                channels_->remove(publication_key);
                return rollback(
                    "internal",
                    "Failed to persist delivery session '" +
                        delivery_session_id + "'");
            }

            auto [inserted_it, _] =
                delivery_sessions_.emplace(delivery_session_id, delivery);
            d_it = inserted_it;
        } else {
            d_it->second->add_ref();
        }

        delivery_session_ids.push_back(delivery_session_id);
    }

    row.state = "active";
    row.device_uuid = resolved.device_uuid;
    row.preset_id = preset_id;
    row.preset_name = resolved.preset;
    row.delivery_name = resolved.delivery;
    row.started_at_ms = started_at;
    row.stopped_at_ms = 0;
    row.last_error.clear();
    return Result<StreamSession>::ok(
        make_session_view_locked(row, delivery_session_ids));
}

void SessionManager::release_delivery_sessions_locked(
    const std::vector<std::string>& delivery_session_ids) {
    std::set<std::string> capture_session_ids;

    for (const auto& delivery_session_id : delivery_session_ids) {
        auto d_it = delivery_sessions_.find(delivery_session_id);
        if (d_it == delivery_sessions_.end()) {
            continue;
        }

        if (auto capture = d_it->second->capture_session()) {
            capture_session_ids.insert(capture->capture_session_id());
        }

        if (d_it->second->release()) {
            const auto publication_key = d_it->second->key();
            d_it->second->stop();
            store_.set_delivery_session_state(delivery_session_id, "stopped",
                                              now_ms());
            channels_->remove(publication_key);
            delivery_sessions_.erase(d_it);
        }
    }

    for (const auto& capture_session_id : capture_session_ids) {
        auto cap_it = capture_sessions_.find(capture_session_id);
        if (cap_it == capture_sessions_.end()) {
            continue;
        }

        if (cap_it->second->release()) {
            cap_it->second->stop();
            store_.set_capture_session_state(capture_session_id, "stopped",
                                             now_ms());
            capture_sessions_.erase(cap_it);
        }
    }
}

std::vector<std::string> SessionManager::load_delivery_session_ids_locked(
    const SessionRow& row) const {
    std::vector<std::string> delivery_session_ids;
    for (const auto& binding : store_.get_session_bindings(row.session_id)) {
        delivery_session_ids.push_back(binding.delivery_session_id);
    }
    return delivery_session_ids;
}

bool SessionManager::save_session_locked(
    const SessionRow& row, const std::vector<std::string>& delivery_session_ids) {
    return store_.save_session(
        row, session_binding_rows_from(row.session_id, delivery_session_ids));
}

StreamSession SessionManager::make_session_view_locked(
    const SessionRow& row,
    const std::vector<std::string>& delivery_session_ids) const {
    StreamSession result;
    result.session_id = row.session_id;
    result.state = row.state;
    result.request = request_from_row(row);
    result.name = current_device_name_locked(row.device_uuid, row.request_name);
    result.device_uuid = row.device_uuid;
    result.preset = row.preset_name;
    result.delivery = row.delivery_name;
    result.host = row.host;
    result.locality = row.locality;
    result.last_error = row.last_error;

    if (row.state != "active") {
        return result;
    }

    for (const auto& delivery_session_id : delivery_session_ids) {
        auto d_it = delivery_sessions_.find(delivery_session_id);
        if (d_it == delivery_sessions_.end()) {
            continue;
        }

        auto stream_state = d_it->second->stream_state();
        const auto publication_key = d_it->second->key();
        if (auto cap = d_it->second->capture_session()) {
            stream_state.frame_count = cap->frame_count(stream_state.stream_id);
            result.capture_session_id = cap->capture_session_id();
        }
        if (stream_state.transport == "ipc") {
            stream_state.consumer_count = channels_->consumer_count(publication_key);
            if (!ipc_socket_path_.empty()) {
                result.ipc_descriptors[stream_state.stream_name] = IpcDescriptor{
                    ipc_socket_path_,
                    row.session_id,
                    stream_state.stream_name,
                    channels_->channel_id(publication_key),
                };
            }
        }
        result.streams.push_back(std::move(stream_state));
    }
    return result;
}

Result<StreamSession> SessionManager::create_session_locked(
    const SessionRequest& request) {
    auto row = make_session_row(generate_session_id(), request);
    std::vector<std::string> delivery_session_ids;
    auto activated = activate_session_locked(row, request, delivery_session_ids);
    if (!activated.ok()) {
        return activated;
    }

    if (!save_session_locked(row, delivery_session_ids)) {
        release_delivery_sessions_locked(delivery_session_ids);
        return Result<StreamSession>::err(
            {"internal", "Failed to persist session '" + row.session_id + "'"});
    }

    return activated;
}

std::variant<StreamSession, ResolutionError>
SessionManager::create_session(const SessionRequest& request) {
    std::lock_guard lock(mutex_);

    auto created = create_session_locked(request);
    if (!created.ok()) {
        return ResolutionError{created.error().code, created.error().message, {}};
    }
    return created.value();
}

std::optional<StreamSession> SessionManager::get_session(
    const std::string& session_id) const {
    std::lock_guard lock(mutex_);

    auto row = store_.find_session(session_id);
    if (!row) {
        return std::nullopt;
    }
    return make_session_view_locked(*row, load_delivery_session_ids_locked(*row));
}

std::vector<StreamSession> SessionManager::list_sessions() const {
    std::lock_guard lock(mutex_);

    auto rows = store_.get_sessions();
    std::vector<StreamSession> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(
            make_session_view_locked(row, load_delivery_session_ids_locked(row)));
    }
    return result;
}

Result<StreamSession> SessionManager::start_session_locked(
    const std::string& session_id) {
    auto row = store_.find_session(session_id);
    if (!row) {
        return Result<StreamSession>::err({"not_found", "session_id not found"});
    }

    if (row->state == "active") {
        return Result<StreamSession>::ok(
            make_session_view_locked(*row, load_delivery_session_ids_locked(*row)));
    }

    auto request = restart_request_from_row(*row);
    std::vector<std::string> delivery_session_ids;
    auto activated =
        activate_session_locked(*row, request, delivery_session_ids);
    if (!activated.ok()) {
        row->state = "stopped";
        row->stopped_at_ms = now_ms();
        row->last_error = session_error_text(activated.error());
        if (!save_session_locked(*row, {})) {
            std::fprintf(stderr,
                         "SessionManager: failed to persist last_error for "
                         "session %s\n",
                         row->session_id.c_str());
        }
        return activated;
    }
    if (!save_session_locked(*row, delivery_session_ids)) {
        release_delivery_sessions_locked(delivery_session_ids);
        return Result<StreamSession>::err(
            {"internal", "Failed to persist restarted session"});
    }
    return activated;
}

Result<StreamSession> SessionManager::start_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    return start_session_locked(session_id);
}

Result<StreamSession> SessionManager::stop_session_locked(
    const std::string& session_id) {
    auto row = store_.find_session(session_id);
    if (!row) {
        return Result<StreamSession>::err({"not_found", "session_id not found"});
    }

    if (row->state != "active") {
        return Result<StreamSession>::ok(
            make_session_view_locked(*row, load_delivery_session_ids_locked(*row)));
    }

    auto delivery_session_ids = load_delivery_session_ids_locked(*row);
    release_delivery_sessions_locked(delivery_session_ids);
    row->state = "stopped";
    row->stopped_at_ms = now_ms();
    if (!save_session_locked(*row, {})) {
        return Result<StreamSession>::err(
            {"internal", "Failed to persist stopped session"});
    }
    return Result<StreamSession>::ok(make_session_view_locked(*row, {}));
}

Result<StreamSession> SessionManager::stop_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    return stop_session_locked(session_id);
}

AppSourceView SessionManager::make_app_source_view_locked(
    const AppSourceRow& source) const {
    AppSourceView view;
    view.source_id = source.source_id;
    view.target_name = source.target_name;
    view.input = source.input;
    view.canonical_uri = source.canonical_uri;
    view.state = source.state;
    view.last_error = source.last_error;
    view.created_at_ms = source.created_at_ms;
    view.updated_at_ms = source.updated_at_ms;
    view.request = parse_request_json(source.request_json);

    if (auto target = store_.find_app_target_by_name(source.app_id, source.target_name)) {
        view.target_kind = target->target_kind;
    }

    if (!source.latest_session_id.empty()) {
        auto row = store_.find_session(source.latest_session_id);
        if (row) {
            view.session = make_session_view_locked(
                *row, load_delivery_session_ids_locked(*row));
            if (!view.target_kind.empty() && view.session) {
                auto bindings =
                    compute_target_bindings(*view.session, view.target_kind);
                if (bindings.ok()) {
                    view.bindings = std::move(bindings.value());
                }
            }
        }
    }
    return view;
}

AppTargetView SessionManager::make_app_target_view_locked(
    const AppTargetRow& target) const {
    AppTargetView view;
    view.target_id = target.target_id;
    view.target_name = target.target_name;
    view.target_kind = target.target_kind;
    view.contract_json = target.contract_json;
    view.created_at_ms = target.created_at_ms;
    view.updated_at_ms = target.updated_at_ms;
    return view;
}

RuntimeAppView SessionManager::make_app_view_locked(
    const AppRow& app) const {
    RuntimeAppView view;
    view.app_id = app.app_id;
    view.name = app.name;
    view.description = app.description;
    view.created_at_ms = app.created_at_ms;
    view.updated_at_ms = app.updated_at_ms;

    auto targets = store_.list_app_targets(app.app_id);
    view.targets.reserve(targets.size());
    for (const auto& target : targets) {
        view.targets.push_back(make_app_target_view_locked(target));
    }

    auto sources = store_.list_app_sources(app.app_id);
    view.sources.reserve(sources.size());
    for (const auto& source : sources) {
        view.sources.push_back(make_app_source_view_locked(source));
    }
    return view;
}

Result<RuntimeAppView> SessionManager::create_app(std::string name,
                                                  std::string description) {
    std::lock_guard lock(mutex_);

    AppRow app;
    app.app_id = generate_runtime_id("app");
    if (name.empty()) {
        app.name = app.app_id;
    } else {
        app.name = std::move(name);
    }
    app.description = std::move(description);
    app.created_at_ms = now_ms();
    app.updated_at_ms = app.created_at_ms;
    if (!store_.save_app(app)) {
        return Result<RuntimeAppView>::err(
            {"internal", "Failed to persist app '" + app.app_id + "'"});
    }
    return Result<RuntimeAppView>::ok(make_app_view_locked(app));
}

std::optional<RuntimeAppView> SessionManager::get_app(
    const std::string& app_id) const {
    std::lock_guard lock(mutex_);

    auto app = store_.find_app(app_id);
    if (!app) {
        return std::nullopt;
    }
    return make_app_view_locked(*app);
}

std::vector<RuntimeAppView> SessionManager::list_apps() const {
    std::lock_guard lock(mutex_);

    std::vector<RuntimeAppView> apps;
    auto rows = store_.get_apps();
    apps.reserve(rows.size());
    for (const auto& app : rows) {
        apps.push_back(make_app_view_locked(app));
    }
    return apps;
}

std::optional<std::vector<AppTargetView>> SessionManager::list_app_targets(
    const std::string& app_id) const {
    std::lock_guard lock(mutex_);

    if (!store_.find_app(app_id)) {
        return std::nullopt;
    }

    auto rows = store_.list_app_targets(app_id);
    std::vector<AppTargetView> targets;
    targets.reserve(rows.size());
    for (const auto& row : rows) {
        targets.push_back(make_app_target_view_locked(row));
    }
    return targets;
}

Result<AppTargetView> SessionManager::create_app_target(
    const std::string& app_id, std::string target_name,
    std::string target_kind) {
    std::lock_guard lock(mutex_);

    if (!store_.find_app(app_id)) {
        return Result<AppTargetView>::err(
            {"not_found", "App '" + app_id + "' not found"});
    }
    if (target_name.empty()) {
        return Result<AppTargetView>::err(
            {"bad_request", "'target_name' must be a non-empty string"});
    }
    if (target_kind.empty()) {
        target_kind = "video";
    }
    if (target_kind != "video" && target_kind != "audio" &&
        target_kind != "rgbd") {
        return Result<AppTargetView>::err(
            {"bad_request", "Unknown target kind '" + target_kind + "'"});
    }
    if (store_.find_app_target_by_name(app_id, target_name)) {
        return Result<AppTargetView>::err(
            {"conflict",
             "Target '" + target_name + "' already exists in app '" + app_id +
                 "'"});
    }

    AppTargetRow row;
    row.target_id = generate_runtime_id("target");
    row.app_id = app_id;
    row.target_name = std::move(target_name);
    row.target_kind = std::move(target_kind);
    row.created_at_ms = now_ms();
    row.updated_at_ms = row.created_at_ms;
    row.contract_json =
        nlohmann::json{{"kind", row.target_kind}}.dump();
    if (!store_.save_app_target(row)) {
        return Result<AppTargetView>::err(
            {"internal",
             "Failed to persist target '" + row.target_name + "' in app '" +
                 app_id + "'"});
    }
    return Result<AppTargetView>::ok(make_app_target_view_locked(row));
}

bool SessionManager::delete_app_target(const std::string& app_id,
                                       const std::string& target_name) {
    std::lock_guard lock(mutex_);

    if (!store_.find_app(app_id)) {
        return false;
    }
    auto sources = store_.list_app_sources(app_id);
    for (const auto& source : sources) {
        if (source.target_name == target_name) {
            return false;
        }
    }
    return store_.delete_app_target(app_id, target_name);
}

std::optional<std::vector<AppSourceView>> SessionManager::list_app_sources(
    const std::string& app_id) const {
    std::lock_guard lock(mutex_);

    if (!store_.find_app(app_id)) {
        return std::nullopt;
    }

    auto rows = store_.list_app_sources(app_id);
    std::vector<AppSourceView> sources;
    sources.reserve(rows.size());
    for (const auto& source : rows) {
        sources.push_back(make_app_source_view_locked(source));
    }
    return sources;
}

Result<AppSourceView> SessionManager::add_app_source(
    const std::string& app_id, std::string source_input,
    std::string target_name) {
    std::lock_guard lock(mutex_);

    if (!store_.find_app(app_id)) {
        return Result<AppSourceView>::err(
            {"not_found", "App '" + app_id + "' not found"});
    }
    if (target_name.empty()) {
        return Result<AppSourceView>::err(
            {"bad_request", "'target' must be a non-empty string"});
    }
    auto target = store_.find_app_target_by_name(app_id, target_name);
    if (!target) {
        return Result<AppSourceView>::err(
            {"not_found",
             "Target '" + target_name + "' not found in app '" + app_id + "'"});
    }

    auto normalized =
        normalize_source_input(source_input, RequestOrigin::kHttpApi);
    if (!normalized.ok()) {
        return Result<AppSourceView>::err(normalized.error());
    }
    if (!is_local_ipc_consumable_source(normalized.value())) {
        return Result<AppSourceView>::err(
            {"unsupported_source",
             "Source must resolve to a local IPC-consumable session"});
    }

    for (const auto& existing : store_.list_app_sources(app_id)) {
        if (existing.canonical_uri == normalized.value().canonical_uri) {
            return Result<AppSourceView>::err(
                {"conflict",
                 "Source '" + normalized.value().canonical_uri +
                     "' already exists in app '" + app_id + "'"});
        }
    }

    auto created = create_session_locked(normalized.value().request);
    if (!created.ok()) {
        return Result<AppSourceView>::err(created.error());
    }
    auto bindings = compute_target_bindings(created.value(), target->target_kind);
    if (!bindings.ok()) {
        destroy_session_locked(created.value().session_id);
        return Result<AppSourceView>::err(bindings.error());
    }

    AppSourceRow source;
    source.source_id = generate_runtime_id("src");
    source.app_id = app_id;
    source.target_name = std::move(target_name);
    source.input = std::move(source_input);
    source.canonical_uri = normalized.value().canonical_uri;
    source.state = created.value().state;
    source.created_at_ms = now_ms();
    source.updated_at_ms = source.created_at_ms;
    source.request_json = normalized.value().request_json.dump();
    source.latest_session_id = created.value().session_id;

    if (!store_.save_app_source(source)) {
        destroy_session_locked(created.value().session_id);
        return Result<AppSourceView>::err(
            {"internal",
             "Failed to persist source '" + source.source_id + "' in app '" +
                 app_id + "'"});
    }
    return Result<AppSourceView>::ok(make_app_source_view_locked(source));
}

Result<AppSourceView> SessionManager::change_app_source_state_locked(
    const std::string& app_id, const std::string& source_id,
    bool start, std::string last_error) {
    if (!store_.find_app(app_id)) {
        return Result<AppSourceView>::err(
            {"not_found", "App '" + app_id + "' not found"});
    }
    auto source = store_.find_app_source(source_id);
    if (!source || source->app_id != app_id) {
        return Result<AppSourceView>::err(
            {"not_found",
             "Source '" + source_id + "' not found in app '" + app_id + "'"});
    }

    if (start) {
        auto started = start_session_locked(source->latest_session_id);
        if (!started.ok()) {
            source->last_error = started.error().message;
            source->updated_at_ms = now_ms();
            store_.save_app_source(*source);
            return Result<AppSourceView>::err(started.error());
        }
        source->state = started.value().state;
        source->last_error.clear();
        source->updated_at_ms = now_ms();
        store_.save_app_source(*source);
        return Result<AppSourceView>::ok(make_app_source_view_locked(*source));
    }

    auto stopped = stop_session_locked(source->latest_session_id);
    if (!stopped.ok()) {
        source->last_error = stopped.error().message;
        source->updated_at_ms = now_ms();
        store_.save_app_source(*source);
        return Result<AppSourceView>::err(stopped.error());
    }
    source->state = stopped.value().state;
    source->last_error = std::move(last_error);
    source->updated_at_ms = now_ms();
    store_.save_app_source(*source);
    return Result<AppSourceView>::ok(make_app_source_view_locked(*source));
}

Result<AppSourceView> SessionManager::start_app_source(
    const std::string& app_id, const std::string& source_id) {
    std::lock_guard lock(mutex_);
    return change_app_source_state_locked(app_id, source_id, true, {});
}

Result<AppSourceView> SessionManager::stop_app_source(
    const std::string& app_id, const std::string& source_id,
    std::string last_error) {
    std::lock_guard lock(mutex_);
    return change_app_source_state_locked(app_id, source_id, false,
                                          std::move(last_error));
}

bool SessionManager::destroy_app(const std::string& app_id) {
    std::lock_guard lock(mutex_);

    if (!store_.find_app(app_id)) {
        return false;
    }

    for (const auto& source : store_.list_app_sources(app_id)) {
        if (!source.latest_session_id.empty()) {
            destroy_session_locked(source.latest_session_id);
        }
    }

    return store_.delete_app(app_id);
}

bool SessionManager::destroy_session_locked(const std::string& session_id) {
    auto row = store_.find_session(session_id);
    if (!row) {
        return false;
    }

    auto delivery_session_ids = load_delivery_session_ids_locked(*row);
    if (row->state == "active" && !delivery_session_ids.empty()) {
        release_delivery_sessions_locked(delivery_session_ids);
    }
    return store_.delete_session(session_id);
}

bool SessionManager::destroy_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    return destroy_session_locked(session_id);
}

std::vector<SessionManager::StatusEntry> SessionManager::get_status() const {
    std::lock_guard lock(mutex_);

    std::map<std::string, StatusEntry> grouped;

    for (const auto& [capture_session_id, cap] : capture_sessions_) {
        StatusEntry entry;
        entry.name = current_device_name_locked(cap->resolution().device_uuid,
                                                cap->resolution().name);
        entry.device_uuid = cap->resolution().device_uuid;
        entry.preset = cap->resolution().preset;
        entry.capture_session_id = capture_session_id;
        entry.capture_ref_count = static_cast<std::uint32_t>(cap->ref_count());
        grouped[capture_session_id] = std::move(entry);
    }

    for (const auto& row : store_.get_sessions()) {
        if (row.state != "active") {
            continue;
        }

        const auto delivery_session_ids = load_delivery_session_ids_locked(row);
        for (const auto& delivery_session_id : delivery_session_ids) {
            auto d_it = delivery_sessions_.find(delivery_session_id);
            if (d_it == delivery_sessions_.end()) {
                continue;
            }
            auto cap = d_it->second->capture_session();
            if (!cap) {
                continue;
            }
            grouped[cap->capture_session_id()].session_ids.push_back(row.session_id);
            break;
        }
    }

    for (const auto& [_, del] : delivery_sessions_) {
        auto cap = del->capture_session();
        if (!cap) continue;
        auto& entry = grouped[cap->capture_session_id()];

        auto stream_state = del->stream_state();
        stream_state.frame_count = cap->frame_count(stream_state.stream_id);
        if (stream_state.transport == "ipc") {
            stream_state.consumer_count = channels_->consumer_count(del->key());
        }
        entry.delivery_sessions.push_back(DeliveryStatus{
            del->key().delivery_name,
            stream_state.stream_name,
            session_state_to_string(del->state()),
            static_cast<std::uint32_t>(del->ref_count()),
            std::move(stream_state),
        });
    }

    std::vector<StatusEntry> result;
    result.reserve(grouped.size());
    for (auto& [_, entry] : grouped) {
        result.push_back(std::move(entry));
    }
    return result;
}

Result<SessionManager::IpcConsumerLease> SessionManager::attach_ipc_consumer(
    const std::string& session_id, const std::string& stream_name) {
    std::lock_guard lock(mutex_);

    auto row = store_.find_session(session_id);
    if (!row) {
        return Result<IpcConsumerLease>::err(
            {"not_found", "session_id not found"});
    }
    if (row->state != "active") {
        return Result<IpcConsumerLease>::err(
            {"session_inactive", "session is not active"});
    }

    auto delivery_session_ids = load_delivery_session_ids_locked(*row);
    PublicationKey match;
    std::shared_ptr<DeliverySession> delivery;
    for (const auto& delivery_session_id : delivery_session_ids) {
        auto d_it = delivery_sessions_.find(delivery_session_id);
        if (d_it == delivery_sessions_.end()) continue;
        if (d_it->second->stream_state().transport != "ipc") continue;
        if (stream_name.empty() ||
            d_it->second->stream_state().stream_name == stream_name) {
            match = d_it->second->key();
            delivery = d_it->second;
            break;
        }
    }
    if (!delivery) {
        return Result<IpcConsumerLease>::err(
            {"not_found", "ipc stream not found for session"});
    }

    auto consumer_res = channels_->add_consumer(match);
    if (!consumer_res.ok()) {
        return Result<IpcConsumerLease>::err(consumer_res.error());
    }

    auto lease_fds = std::move(consumer_res.value());
    IpcConsumerLease lease;
    lease.key = match;
    lease.stream_state = delivery->stream_state();
    lease.stream_state.consumer_count = channels_->consumer_count(match);
    lease.channel_id = lease_fds.channel_id;
    lease.memfd = lease_fds.memfd;
    lease.eventfd = lease_fds.eventfd;
    lease.writer_eventfd = lease_fds.writer_eventfd;
    return Result<IpcConsumerLease>::ok(std::move(lease));
}

void SessionManager::detach_ipc_consumer(const PublicationKey& key, int writer_eventfd) {
    std::lock_guard lock(mutex_);
    if (!channels_) return;
    channels_->remove_consumer(key, writer_eventfd);
}

}  // namespace insightos::backend
