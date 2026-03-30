// role: persisted direct-session implementation for the standalone backend.
// revision: 2026-03-27 task10-developer-runtime-surface
// major changes: resolves catalog URIs into durable sessions, keeps alias-led
// canonical URIs in session and runtime status snapshots, and provides
// session/status plus runtime-owned worker + IPC attach inspection while
// hardening RTSP runtime error handling and control-socket request intake.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/session_service.hpp"

#include "insightio/backend/ipc.hpp"
#include "insightio/backend/result.hpp"
#include "insightio/backend/runtime_paths.hpp"
#include "insightio/backend/unix_socket.hpp"

#include "publication/rtsp_publisher.hpp"
#include "workers/synthetic_worker.hpp"
#include "workers/v4l2_worker.hpp"

#ifdef INSIGHTIO_HAS_PIPEWIRE
#include "workers/pipewire_worker.hpp"
#endif

#ifdef INSIGHTIO_HAS_ORBBEC
#include "workers/orbbec_worker.hpp"
#endif

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <sys/eventfd.h>
#include <string_view>
#include <sys/epoll.h>
#include <poll.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>

namespace insightio::backend {

class ServingRuntimeRegistry {
public:
    ServingRuntimeRegistry(std::string socket_path, std::string rtsp_host)
        : socket_path_(std::move(socket_path)), rtsp_host_(std::move(rtsp_host)) {
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    }

    ~ServingRuntimeRegistry() {
        stop();
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
        }
    }

    bool start(std::string& err) {
        std::scoped_lock lock(mutex_);
        if (running_) {
            return true;
        }

        const auto parent = std::filesystem::path(socket_path_).parent_path();
        std::error_code fs_error;
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, fs_error);
            if (fs_error) {
                err = fs_error.message();
                return false;
            }
        }

        auto listen_res = ipc::create_listen_socket(socket_path_);
        if (!listen_res.ok()) {
            err = listen_res.error().message;
            return false;
        }

        listen_fd_ = listen_res.value();
        stop_requested_ = false;
        running_ = true;
        accept_thread_ = std::thread([this]() { accept_loop(); });
        return true;
    }

    void stop() {
        {
            std::scoped_lock lock(mutex_);
            if (!running_) {
                return;
            }
            stop_requested_ = true;
            if (listen_fd_ >= 0) {
                ::close(listen_fd_);
                listen_fd_ = -1;
            }
        }

        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        std::scoped_lock lock(mutex_);
        cleanup_all_connections_locked();
        stop_all_entries_locked();
        running_ = false;
        stop_requested_ = false;
        if (!socket_path_.empty()) {
            ::unlink(socket_path_.c_str());
        }
    }

    [[nodiscard]] const std::string& socket_path() const {
        return socket_path_;
    }

    void clear() {
        std::scoped_lock lock(mutex_);
        cleanup_all_connections_locked();
        stop_all_entries_locked();
        session_to_stream_.clear();
    }

    Result<SessionRecord::ServingRuntimeView> attach(const SessionRecord& session) {
        std::scoped_lock lock(mutex_);

        if (const auto it = session_to_stream_.find(session.session_id);
            it != session_to_stream_.end() && it->second != session.source.stream_id) {
            detach_locked(session.session_id);
        }

        auto& entry = entries_[session.source.stream_id];
        if (entry.runtime_key.empty()) {
            entry.runtime_key = runtime_key_for(session.source.stream_id);
            entry.source = session.source;
            entry.resolved_members_json = resolved_members_for(session);
            entry.owner_session_id = session.session_id;
            entry.state = "starting";
            auto setup = realize_runtime_locked(entry, session);
            if (!setup.ok()) {
                entries_.erase(session.source.stream_id);
                return Result<SessionRecord::ServingRuntimeView>::err(setup.error());
            }
        }
        if (entry.source.stream_id == 0) {
            entry.source = session.source;
        }
        if (entry.resolved_members_json.is_null() || entry.resolved_members_json.empty()) {
            entry.resolved_members_json = resolved_members_for(session);
        }

        entry.consumer_rtsp[session.session_id] = session.rtsp_enabled;
        if (entry.owner_session_id == 0) {
            entry.owner_session_id = session.session_id;
        }
        session_to_stream_[session.session_id] = session.source.stream_id;
        reconcile_rtsp_publication_locked(entry);
        return Result<SessionRecord::ServingRuntimeView>::ok(runtime_view_for_locked(entry));
    }

    void detach(std::int64_t session_id) {
        std::scoped_lock lock(mutex_);
        detach_locked(session_id);
    }

    std::optional<SessionRecord::ServingRuntimeView> view_for_session(
        std::int64_t session_id) const {
        std::scoped_lock lock(mutex_);
        const auto session_it = session_to_stream_.find(session_id);
        if (session_it == session_to_stream_.end()) {
            return std::nullopt;
        }
        const auto entry_it = entries_.find(session_it->second);
        if (entry_it == entries_.end()) {
            return std::nullopt;
        }
        return runtime_view_for_locked(entry_it->second);
    }

    std::vector<ServingRuntimeSnapshot> snapshot() const {
        std::scoped_lock lock(mutex_);
        std::vector<ServingRuntimeSnapshot> snapshots;
        snapshots.reserve(entries_.size());
        for (const auto& [stream_id, entry] : entries_) {
            ServingRuntimeSnapshot view;
            view.runtime_key = entry.runtime_key;
            view.stream_id = stream_id;
            view.owner_session_id = entry.owner_session_id;
            view.state = entry.state;
            view.last_error = entry.last_error;
            view.rtsp_enabled = effective_rtsp_locked(entry);
            view.consumer_count = static_cast<int>(entry.consumer_rtsp.size());
            view.ipc_socket_path = socket_path_;
            view.ipc_channels = channel_views_locked(entry);
            view.rtsp_publication = rtsp_view_for_locked(entry);
            view.source = entry.source;
            view.resolved_members_json = entry.resolved_members_json;
            for (const auto& [session_id, _] : entry.consumer_rtsp) {
                view.consumer_session_ids.push_back(session_id);
            }
            snapshots.push_back(std::move(view));
        }
        return snapshots;
    }

private:
    struct ChannelState {
        std::string channel_id;
        std::string stream_name;
        std::string route_name;
        std::string selector;
        std::string media_kind;
        ResolvedCaps delivered_caps;
        nlohmann::json delivered_caps_json;
        std::unique_ptr<ipc::Channel> channel;
        std::shared_ptr<ipc::Writer> writer;
        std::atomic_uint64_t frames_published{0};
        std::atomic_int attached_consumers{0};
        std::atomic_bool first_frame{true};
    };

    struct RtspPublicationState {
        std::string publication_id;
        std::string stream_name;
        std::string selector;
        std::string url;
        std::string publication_profile;
        std::string transport;
        std::string promised_format;
        std::string actual_format;
        std::string last_error;
        bool desired{false};
        bool supported{false};
        std::shared_ptr<RtspPublisher> publisher;
        std::atomic_uint64_t frames_forwarded{0};
    };

    struct Entry {
        std::string runtime_key;
        SessionResolvedSource source;
        nlohmann::json resolved_members_json;
        std::int64_t owner_session_id{0};
        std::string state;
        std::string last_error;
        std::map<std::int64_t, bool> consumer_rtsp;
        std::vector<std::shared_ptr<ChannelState>> channels;
        std::unordered_map<std::string, std::shared_ptr<ChannelState>> channel_aliases;
        std::shared_ptr<RtspPublicationState> rtsp_publication;
        std::shared_ptr<CaptureWorker> worker;
    };

    struct ActiveConnection {
        std::int64_t session_id{0};
        std::shared_ptr<ChannelState> channel;
        int leased_eventfd{-1};
        int writer_eventfd{-1};
        int client_fd{-1};
    };

    struct RuntimeChannelPlan {
        std::string stream_name;
        std::string route_name;
        std::string selector;
        std::string media_kind;
        ResolvedCaps delivered_caps;
        std::size_t max_payload_bytes{0};
    };

    enum class WorkerKind { kSynthetic, kV4l2, kPipeWire, kOrbbec };

    struct RuntimePlan {
        WorkerKind kind{WorkerKind::kSynthetic};
        std::string worker_name;
        std::string device_uri;
        std::vector<RuntimeChannelPlan> channels;
        SyntheticWorkerConfig synthetic_cfg;
        V4l2WorkerConfig v4l2_cfg;
#ifdef INSIGHTIO_HAS_PIPEWIRE
        PipeWireWorkerConfig pipewire_cfg;
#endif
#ifdef INSIGHTIO_HAS_ORBBEC
        OrbbecWorkerConfig orbbec_cfg;
#endif
    };

    static std::string runtime_key_for(std::int64_t stream_id) {
        return "stream:" + std::to_string(stream_id);
    }

    static nlohmann::json resolved_members_for(const SessionRecord& session) {
        if (!session.resolved_members_json.is_null() &&
            !session.resolved_members_json.empty()) {
            return session.resolved_members_json;
        }
        if (!session.source.members_json.is_null() && !session.source.members_json.empty()) {
            return session.source.members_json;
        }
        return nullptr;
    }

    static bool effective_rtsp_locked(const Entry& entry) {
        return std::any_of(entry.consumer_rtsp.begin(),
                           entry.consumer_rtsp.end(),
                           [](const auto& consumer) { return consumer.second; });
    }

    static bool has_ipc_consumers_locked(const Entry& entry) {
        return std::any_of(entry.channels.begin(),
                           entry.channels.end(),
                           [](const auto& channel) {
                               return channel->attached_consumers.load(
                                          std::memory_order_relaxed) > 0;
                           });
    }

    std::string rtsp_url_for_channel_locked(const Entry& entry,
                                            const ChannelState& channel) const {
        return "rtsp://" + rtsp_host_ + "/" + entry.source.public_name + "/" +
               entry.source.stream_public_name;
    }

    std::optional<RtspPublicationRuntimeView> rtsp_view_for_locked(
        const Entry& entry) const {
        if (!entry.rtsp_publication) {
            return std::nullopt;
        }

        const bool desired = entry.rtsp_publication->desired;
        if (!desired) {
            return std::nullopt;
        }
        const bool supported = entry.rtsp_publication->supported;
        const auto publisher = std::atomic_load_explicit(
            &entry.rtsp_publication->publisher, std::memory_order_acquire);
        RtspPublicationRuntimeView view;
        view.publication_id = entry.rtsp_publication->publication_id;
        view.stream_name = entry.rtsp_publication->stream_name;
        view.selector = entry.rtsp_publication->selector;
        view.url = entry.rtsp_publication->url;
        view.publication_profile = entry.rtsp_publication->publication_profile;
        view.transport = entry.rtsp_publication->transport;
        view.promised_format = entry.rtsp_publication->promised_format;
        view.actual_format = entry.rtsp_publication->actual_format;
        view.last_error = entry.rtsp_publication->last_error;
        view.frames_forwarded =
            entry.rtsp_publication->frames_forwarded.load(std::memory_order_relaxed);

        if (!supported) {
            view.state = "error";
            return view;
        }

        if (publisher && publisher->is_running()) {
            view.state = "active";
            return view;
        }

        const auto publisher_error =
            publisher ? publisher->last_error() : std::string{};
        view.state = publisher_error.empty() && view.last_error.empty() ? "starting"
                                                                        : "error";
        if (!publisher_error.empty()) {
            view.last_error = publisher_error;
        }
        return view;
    }

    SessionRecord::ServingRuntimeView runtime_view_for_locked(const Entry& entry) const {
        SessionRecord::ServingRuntimeView view;
        view.runtime_key = entry.runtime_key;
        view.owner_session_id = entry.owner_session_id;
        view.state = entry.state;
        view.last_error = entry.last_error;
        view.rtsp_enabled = effective_rtsp_locked(entry);
        view.consumer_count = static_cast<int>(entry.consumer_rtsp.size());
        view.shared = view.consumer_count > 1;
        view.ipc_socket_path = socket_path_;
        view.ipc_channels = channel_views_locked(entry);
        view.rtsp_publication = rtsp_view_for_locked(entry);
        for (const auto& [session_id, _] : entry.consumer_rtsp) {
            view.consumer_session_ids.push_back(session_id);
        }
        return view;
    }

    static ResolvedCaps caps_from_json(const nlohmann::json& json) {
        ResolvedCaps caps;
        if (!json.is_object()) {
            return caps;
        }
        caps.format = json.value("format", std::string{});
        if (json.contains("sample_rate")) {
            caps.width = json.value("sample_rate", 0U);
            caps.height = json.value("channels", 0U);
            caps.fps = 0;
        } else {
            caps.width = json.value("width", 0U);
            caps.height = json.value("height", 0U);
            caps.fps = json.value("fps", 0U);
        }
        return caps;
    }

    static std::size_t bytes_per_sample(std::string_view format) {
        if (format == "u8") {
            return 1;
        }
        if (format == "s16le" || format == "s16be") {
            return 2;
        }
        if (format == "s24le" || format == "s24be" || format == "s32le" ||
            format == "s32be" || format == "f32le" || format == "f32be") {
            return 4;
        }
        return 2;
    }

    static std::size_t payload_size_for_caps(const ResolvedCaps& caps) {
        if (caps.is_audio()) {
            const auto sample_rate = std::max<std::uint32_t>(caps.sample_rate(), 8000);
            const auto channels = std::max<std::uint32_t>(caps.channels(), 1);
            const auto samples = std::max<std::uint32_t>(sample_rate / 20, 256);
            return std::max<std::size_t>(
                static_cast<std::size_t>(samples) * channels *
                    bytes_per_sample(caps.format),
                4096);
        }

        const auto width = std::max<std::uint32_t>(caps.width, 16);
        const auto height = std::max<std::uint32_t>(caps.height, 16);
        if (is_compressed_video(caps.format)) {
            return std::max<std::size_t>(
                static_cast<std::size_t>(width) * height, 128 * 1024);
        }
        if (caps.format == "rgb24" || caps.format == "bgr24") {
            return static_cast<std::size_t>(width) * height * 3;
        }
        if (caps.format == "rgba" || caps.format == "bgra") {
            return static_cast<std::size_t>(width) * height * 4;
        }
        if (caps.format == "nv12" || caps.format == "nv21" || caps.format == "yuv420" ||
            caps.format == "yvu420") {
            return static_cast<std::size_t>(width) * height * 3 / 2;
        }
        if (caps.format == "yuyv" || caps.format == "uyvy" ||
            is_depth_format(caps.format)) {
            return static_cast<std::size_t>(width) * height * 2;
        }
        return static_cast<std::size_t>(width) * height;
    }

    static bool is_test_device(std::string_view uri) {
        return uri.starts_with("test:");
    }

    static std::string member_value(const nlohmann::json& members,
                                    std::string_view route_name,
                                    std::string_view field) {
        if (!members.is_array()) {
            return {};
        }
        for (const auto& member : members) {
            if (!member.is_object()) {
                continue;
            }
            if (member.value("route", std::string{}) == route_name) {
                return member.value(std::string(field), std::string{});
            }
        }
        return {};
    }

    static std::string orbbec_route_for_stream(std::string_view stream_name) {
        return "orbbec/" + std::string(stream_name);
    }

#ifdef INSIGHTIO_HAS_PIPEWIRE
    static std::optional<std::uint32_t> parse_unsigned_suffix(std::string_view value,
                                                              std::string_view prefix) {
        if (!value.starts_with(prefix) || value.size() <= prefix.size()) {
            return std::nullopt;
        }
        try {
            return static_cast<std::uint32_t>(
                std::stoul(std::string(value.substr(prefix.size()))));
        } catch (...) {
            return std::nullopt;
        }
    }
#endif

    static Result<RuntimePlan> build_runtime_plan(const SessionRecord& session) {
        const auto& capture = session.source.capture_policy_json;
        if (!capture.is_object()) {
            return Result<RuntimePlan>::err(
                {"runtime_config_invalid", "capture_policy_json must be an object"});
        }

        RuntimePlan plan;
        plan.worker_name = runtime_key_for(session.source.stream_id);
        plan.device_uri = capture.value("device_uri", std::string{});
        const auto driver = capture.value("driver", std::string{});
        const auto members = resolved_members_for(session);

        auto add_channel = [&](std::string stream_name,
                               std::string route_name,
                               std::string selector,
                               std::string media_kind,
                               const ResolvedCaps& delivered_caps) {
            RuntimeChannelPlan channel;
            channel.stream_name = std::move(stream_name);
            channel.route_name = std::move(route_name);
            channel.selector = std::move(selector);
            channel.media_kind = std::move(media_kind);
            channel.delivered_caps = delivered_caps;
            channel.max_payload_bytes = payload_size_for_caps(delivered_caps);
            plan.channels.push_back(std::move(channel));
        };

        if (driver == "v4l2") {
            const auto selected = caps_from_json(capture.at("selected_caps"));
            const auto stream_name =
                capture.value("stream_id", session.source.channel.empty()
                                               ? std::string{"image"}
                                               : session.source.channel);
            add_channel(stream_name,
                        stream_name,
                        session.source.selector,
                        session.source.media_kind,
                        selected);

            if (is_test_device(plan.device_uri)) {
                plan.kind = WorkerKind::kSynthetic;
                plan.synthetic_cfg.name = plan.worker_name;
                plan.synthetic_cfg.streams.push_back({stream_name, selected});
            } else {
                if (!plan.device_uri.starts_with("v4l2:")) {
                    return Result<RuntimePlan>::err(
                        {"runtime_config_invalid", "v4l2 runtime requires v4l2: device_uri"});
                }
                plan.kind = WorkerKind::kV4l2;
                plan.v4l2_cfg = {
                    .name = plan.worker_name,
                    .device_path = plan.device_uri.substr(5),
                    .caps = selected,
                };
            }
            return Result<RuntimePlan>::ok(std::move(plan));
        }

        if (driver == "pipewire") {
            const auto selected = caps_from_json(capture.at("selected_caps"));
            const auto stream_name =
                capture.value("stream_id", session.source.channel.empty()
                                               ? std::string{"audio"}
                                               : session.source.channel);
            add_channel(stream_name,
                        stream_name,
                        session.source.selector,
                        session.source.media_kind,
                        selected);

            if (is_test_device(plan.device_uri)) {
                plan.kind = WorkerKind::kSynthetic;
                plan.synthetic_cfg.name = plan.worker_name;
                plan.synthetic_cfg.streams.push_back({stream_name, selected});
            } else {
#ifdef INSIGHTIO_HAS_PIPEWIRE
                const auto node_id = parse_unsigned_suffix(plan.device_uri, "pw:");
                if (!node_id.has_value()) {
                    return Result<RuntimePlan>::err(
                        {"runtime_config_invalid", "pipewire runtime requires pw:<node_id> device_uri"});
                }
                plan.kind = WorkerKind::kPipeWire;
                plan.pipewire_cfg = {
                    .name = plan.worker_name,
                    .node_id = *node_id,
                    .caps = selected,
                };
#else
                return Result<RuntimePlan>::err(
                    {"runtime_unsupported", "PipeWire runtime support was not compiled in"});
#endif
            }
            return Result<RuntimePlan>::ok(std::move(plan));
        }

        if (driver == "orbbec") {
            const auto mode = capture.value("mode", std::string{});
            if (mode == "grouped_preset") {
                const auto color_caps = caps_from_json(capture.at("color_caps"));
                const auto depth_native = caps_from_json(capture.at("depth_native_caps"));
                const auto depth_delivered =
                    capture.contains("depth_delivered_caps")
                        ? caps_from_json(capture.at("depth_delivered_caps"))
                        : depth_native;
                add_channel("color",
                            member_value(members, "orbbec/color", "route").empty()
                                ? "orbbec/color"
                                : member_value(members, "orbbec/color", "route"),
                            member_value(members, "orbbec/color", "selector"),
                            member_value(members, "orbbec/color", "media"),
                            color_caps);
                add_channel("depth",
                            member_value(members, "orbbec/depth", "route").empty()
                                ? "orbbec/depth"
                                : member_value(members, "orbbec/depth", "route"),
                            member_value(members, "orbbec/depth", "selector"),
                            member_value(members, "orbbec/depth", "media"),
                            depth_delivered);

                if (is_test_device(plan.device_uri)) {
                    plan.kind = WorkerKind::kSynthetic;
                    plan.synthetic_cfg.name = plan.worker_name;
                    plan.synthetic_cfg.streams = {
                        {"color", color_caps},
                        {"depth", depth_delivered},
                    };
                } else {
#ifdef INSIGHTIO_HAS_ORBBEC
                    plan.kind = WorkerKind::kOrbbec;
                    plan.orbbec_cfg.name = plan.worker_name;
                    plan.orbbec_cfg.uri = plan.device_uri;
                    plan.orbbec_cfg.streams = {
                        {"color", color_caps},
                        {"depth", depth_native},
                    };
                    const auto d2c = capture.value("d2c", std::string{"off"});
                    if (d2c == "hardware") {
                        plan.orbbec_cfg.d2c = D2CMode::kHardware;
                    } else if (d2c == "software") {
                        plan.orbbec_cfg.d2c = D2CMode::kSoftware;
                    }
#else
                    return Result<RuntimePlan>::err(
                        {"runtime_unsupported", "Orbbec runtime support was not compiled in"});
#endif
                }
                return Result<RuntimePlan>::ok(std::move(plan));
            }

            const auto selected = caps_from_json(capture.at("selected_caps"));
            const auto stream_name =
                capture.value("stream_id", session.source.channel.empty()
                                               ? std::string{"color"}
                                               : session.source.channel);
            add_channel(stream_name,
                        orbbec_route_for_stream(stream_name),
                        session.source.selector,
                        session.source.media_kind,
                        selected);

            if (is_test_device(plan.device_uri)) {
                plan.kind = WorkerKind::kSynthetic;
                plan.synthetic_cfg.name = plan.worker_name;
                plan.synthetic_cfg.streams.push_back({stream_name, selected});
            } else {
#ifdef INSIGHTIO_HAS_ORBBEC
                plan.kind = WorkerKind::kOrbbec;
                plan.orbbec_cfg.name = plan.worker_name;
                plan.orbbec_cfg.uri = plan.device_uri;
                ResolvedCaps worker_caps = selected;
                if ((mode == "aligned_depth" || capture.value("d2c", std::string{}) != "off") &&
                    capture.contains("native_caps")) {
                    worker_caps = caps_from_json(capture.at("native_caps"));
                }
                plan.orbbec_cfg.streams.push_back({stream_name, worker_caps});
                const auto d2c = capture.value("d2c", std::string{"off"});
                if (d2c == "hardware") {
                    plan.orbbec_cfg.d2c = D2CMode::kHardware;
                } else if (d2c == "software") {
                    plan.orbbec_cfg.d2c = D2CMode::kSoftware;
                }
#else
                return Result<RuntimePlan>::err(
                    {"runtime_unsupported", "Orbbec runtime support was not compiled in"});
#endif
            }
            return Result<RuntimePlan>::ok(std::move(plan));
        }

        return Result<RuntimePlan>::err(
            {"runtime_config_invalid", "unsupported driver in capture_policy_json"});
    }

    static Result<std::shared_ptr<CaptureWorker>> create_worker(const RuntimePlan& plan) {
        switch (plan.kind) {
            case WorkerKind::kSynthetic:
                return Result<std::shared_ptr<CaptureWorker>>::ok(
                    std::make_shared<SyntheticWorker>(plan.synthetic_cfg));
            case WorkerKind::kV4l2:
                return Result<std::shared_ptr<CaptureWorker>>::ok(
                    std::make_shared<V4l2Worker>(plan.v4l2_cfg));
            case WorkerKind::kPipeWire:
#ifdef INSIGHTIO_HAS_PIPEWIRE
                return Result<std::shared_ptr<CaptureWorker>>::ok(
                    std::make_shared<PipeWireWorker>(plan.pipewire_cfg));
#else
                return Result<std::shared_ptr<CaptureWorker>>::err(
                    {"runtime_unsupported", "PipeWire runtime support was not compiled in"});
#endif
            case WorkerKind::kOrbbec:
#ifdef INSIGHTIO_HAS_ORBBEC
                return Result<std::shared_ptr<CaptureWorker>>::ok(
                    std::make_shared<OrbbecWorker>(plan.orbbec_cfg));
#else
                return Result<std::shared_ptr<CaptureWorker>>::err(
                    {"runtime_unsupported", "Orbbec runtime support was not compiled in"});
#endif
        }
        return Result<std::shared_ptr<CaptureWorker>>::err(
            {"runtime_config_invalid", "unknown runtime worker kind"});
    }

    Result<void> realize_runtime_locked(Entry& entry, const SessionRecord& session) {
        auto plan_res = build_runtime_plan(session);
        if (!plan_res.ok()) {
            entry.state = "error";
            entry.last_error = plan_res.error().message;
            return Result<void>::err(plan_res.error());
        }
        auto plan = std::move(plan_res.value());

        entry.channels.clear();
        entry.channel_aliases.clear();
        if (!entry.rtsp_publication) {
            entry.rtsp_publication = std::make_shared<RtspPublicationState>();
        }

        for (const auto& channel_plan : plan.channels) {
            ipc::ChannelSpec spec{
                .channel_id = entry.runtime_key + ":" + channel_plan.stream_name,
                .buffer_slots = 8,
                .max_payload_bytes = channel_plan.max_payload_bytes,
                .reader_count = 0,
            };
            auto channel_res = ipc::create_channel(spec);
            if (!channel_res.ok()) {
                entry.state = "error";
                entry.last_error = channel_res.error().message;
                return Result<void>::err(channel_res.error());
            }

            auto channel = std::make_unique<ipc::Channel>(std::move(channel_res.value()));
            std::string dup_err;
            const int memfd = channel->dup_memfd(dup_err);
            if (memfd < 0) {
                entry.state = "error";
                entry.last_error = dup_err;
                return Result<void>::err({"ipc_error", dup_err});
            }

            auto writer_res = ipc::attach_writer(memfd, {});
            if (!writer_res.ok()) {
                ::close(memfd);
                entry.state = "error";
                entry.last_error = writer_res.error().message;
                return Result<void>::err(writer_res.error());
            }

            auto state = std::make_shared<ChannelState>();
            state->channel_id = spec.channel_id;
            state->stream_name = channel_plan.stream_name;
            state->route_name = channel_plan.route_name;
            state->selector = channel_plan.selector;
            state->media_kind = channel_plan.media_kind;
            state->delivered_caps = channel_plan.delivered_caps;
            state->delivered_caps_json = {
                {"format", channel_plan.delivered_caps.format},
                {"named", channel_plan.delivered_caps.to_named()},
            };
            if (channel_plan.delivered_caps.is_audio()) {
                state->delivered_caps_json["sample_rate"] =
                    channel_plan.delivered_caps.sample_rate();
                state->delivered_caps_json["channels"] =
                    channel_plan.delivered_caps.channels();
            } else {
                state->delivered_caps_json["width"] = channel_plan.delivered_caps.width;
                state->delivered_caps_json["height"] = channel_plan.delivered_caps.height;
                state->delivered_caps_json["fps"] = channel_plan.delivered_caps.fps;
            }
            state->channel = std::move(channel);
            state->writer = writer_res.value();
            entry.channel_aliases[state->stream_name] = state;
            if (!state->route_name.empty()) {
                entry.channel_aliases[state->route_name] = state;
            }
            if (!state->selector.empty()) {
                entry.channel_aliases[state->selector] = state;
            }
            entry.channel_aliases[state->channel_id] = state;
            entry.channels.push_back(std::move(state));
        }

        auto worker_res = create_worker(plan);
        if (!worker_res.ok()) {
            entry.state = "error";
            entry.last_error = worker_res.error().message;
            return Result<void>::err(worker_res.error());
        }

        auto worker = std::move(worker_res.value());
        std::unordered_map<std::string, std::shared_ptr<ChannelState>> callback_channels;
        for (const auto& channel : entry.channels) {
            callback_channels[channel->stream_name] = channel;
        }
        auto rtsp_publication = entry.rtsp_publication;
        worker->set_frame_callback(
            [callback_channels = std::move(callback_channels),
             rtsp_publication = std::move(rtsp_publication)](
                const std::string& stream_name,
                const uint8_t* data,
                size_t size,
                int64_t pts_ns,
                uint32_t flags) {
                const auto it = callback_channels.find(stream_name);
                if (it == callback_channels.end() || !it->second || !it->second->writer) {
                    return;
                }
                const uint32_t runtime_flags =
                    it->second->first_frame.exchange(false) ? flags | ipc::kFlagCapsChange
                                                            : flags;
                if (it->second->writer->write(data, size, pts_ns, 0, runtime_flags)) {
                    it->second->frames_published.fetch_add(1, std::memory_order_relaxed);
                }
                if (rtsp_publication) {
                    auto publisher = std::atomic_load_explicit(
                        &rtsp_publication->publisher, std::memory_order_acquire);
                    if (publisher) {
                        if (publisher->publish(data, size, pts_ns, flags)) {
                            rtsp_publication->frames_forwarded.fetch_add(
                                1, std::memory_order_relaxed);
                        }
                    }
                }
            });

        entry.worker = std::move(worker);
        entry.state = "ready";
        entry.last_error.clear();
        return Result<void>::success();
    }

    static std::vector<IpcChannelRuntimeView> channel_views_locked(const Entry& entry) {
        std::vector<IpcChannelRuntimeView> channels;
        channels.reserve(entry.channels.size());
        for (const auto& channel : entry.channels) {
            IpcChannelRuntimeView view;
            view.channel_id = channel->channel_id;
            view.stream_name = channel->stream_name;
            view.route_name = channel->route_name;
            view.selector = channel->selector;
            view.media_kind = channel->media_kind;
            view.delivered_caps_json = channel->delivered_caps_json;
            view.attached_consumer_count =
                channel->attached_consumers.load(std::memory_order_relaxed);
            view.frames_published =
                channel->frames_published.load(std::memory_order_relaxed);
            channels.push_back(std::move(view));
        }
        return channels;
    }

    static void reset_channel_runtime_state_locked(Entry& entry) {
        for (auto& channel : entry.channels) {
            channel->frames_published.store(0, std::memory_order_relaxed);
            channel->first_frame.store(true, std::memory_order_relaxed);
            if (channel->writer) {
                channel->writer->reset();
            }
        }
    }

    static void pause_idle_worker_locked(Entry& entry) {
        if (entry.worker && entry.worker->is_running()) {
            entry.worker->stop();
        }
        reset_channel_runtime_state_locked(entry);
        entry.state = "ready";
        entry.last_error.clear();
    }

    void reconcile_rtsp_publication_locked(Entry& entry) {
        if (!entry.rtsp_publication) {
            entry.rtsp_publication = std::make_shared<RtspPublicationState>();
        }

        auto& publication = *entry.rtsp_publication;
        publication.desired = effective_rtsp_locked(entry);

        if (!publication.desired) {
            auto publisher = std::atomic_exchange_explicit(
                &publication.publisher,
                std::shared_ptr<RtspPublisher>{},
                std::memory_order_acq_rel);
            if (publisher) {
                publisher->stop();
            }
            publication.frames_forwarded.store(0, std::memory_order_relaxed);
            publication.last_error.clear();
            publication.supported = false;
            if (!has_ipc_consumers_locked(entry) && entry.worker &&
                entry.worker->is_running()) {
                pause_idle_worker_locked(entry);
            }
            return;
        }

        publication.publication_id = entry.runtime_key + ":rtsp";
        publication.publication_profile = "default";
        publication.transport = "rtsp";

        if (entry.channels.size() != 1) {
            auto publisher = std::atomic_exchange_explicit(
                &publication.publisher,
                std::shared_ptr<RtspPublisher>{},
                std::memory_order_acq_rel);
            if (publisher) {
                publisher->stop();
            }
            publication.supported = false;
            publication.last_error =
                "RTSP publication currently supports exact single-channel sources only";
            return;
        }

        const auto& channel = *entry.channels.front();
        publication.stream_name = channel.stream_name;
        publication.selector = channel.selector;
        publication.actual_format = channel.delivered_caps.format;
        publication.url = rtsp_url_for_channel_locked(entry, channel);

        const auto plan = build_rtsp_publication_plan(channel.delivered_caps);
        if (!plan.has_value()) {
            auto publisher = std::atomic_exchange_explicit(
                &publication.publisher,
                std::shared_ptr<RtspPublisher>{},
                std::memory_order_acq_rel);
            if (publisher) {
                publisher->stop();
            }
            publication.supported = false;
            publication.last_error =
                "RTSP publication does not support delivered format '" +
                channel.delivered_caps.format + "'";
            return;
        }

        publication.supported = true;
        publication.promised_format = plan->promised_format;
        publication.actual_format = plan->actual_format;

        auto publisher = std::atomic_load_explicit(
            &publication.publisher, std::memory_order_acquire);
        if (!publisher || !publisher->is_running()) {
            publication.frames_forwarded.store(0, std::memory_order_relaxed);
            auto next_publisher = std::make_shared<RtspPublisher>(
                entry.runtime_key, publication.url, *plan);
            std::string publisher_error;
            if (!next_publisher->start(publisher_error)) {
                std::atomic_store_explicit(&publication.publisher,
                                           std::shared_ptr<RtspPublisher>{},
                                           std::memory_order_release);
                publication.last_error = publisher_error;
                return;
            }
            std::atomic_store_explicit(
                &publication.publisher, next_publisher, std::memory_order_release);
            publisher = std::move(next_publisher);
        }

        if (entry.worker && !entry.worker->is_running()) {
            reset_channel_runtime_state_locked(entry);
            std::string worker_error;
            if (!entry.worker->start(worker_error)) {
                auto failed_publisher = std::atomic_exchange_explicit(
                    &publication.publisher,
                    std::shared_ptr<RtspPublisher>{},
                    std::memory_order_acq_rel);
                if (failed_publisher) {
                    failed_publisher->stop();
                }
                publication.last_error = worker_error;
                return;
            }
            entry.state = "active";
            entry.last_error.clear();

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const bool emitted_frames = std::any_of(
                entry.channels.begin(),
                entry.channels.end(),
                [](const auto& channel_state) {
                    return channel_state->frames_published.load(
                               std::memory_order_relaxed) > 0;
                });
            if (!entry.worker->is_running() && !emitted_frames) {
                auto failed_publisher = std::atomic_exchange_explicit(
                    &publication.publisher,
                    std::shared_ptr<RtspPublisher>{},
                    std::memory_order_acq_rel);
                if (failed_publisher) {
                    failed_publisher->stop();
                }
                publication.last_error = "capture worker exited before publishing frames";
                entry.state = "ready";
                return;
            }
        }

        publication.last_error.clear();
    }

    Result<ActiveConnection> attach_ipc_consumer_locked(std::int64_t session_id,
                                                        std::string_view stream_name) {
        const auto session_it = session_to_stream_.find(session_id);
        if (session_it == session_to_stream_.end()) {
            return Result<ActiveConnection>::err(
                {"not_found", "active runtime not found for session"});
        }
        const auto entry_it = entries_.find(session_it->second);
        if (entry_it == entries_.end()) {
            return Result<ActiveConnection>::err(
                {"not_found", "serving runtime not found"});
        }

        auto& entry = entry_it->second;
        if (entry.state != "active" && entry.state != "ready") {
            return Result<ActiveConnection>::err(
                {"runtime_not_ready", "serving runtime is not active"});
        }
        bool worker_just_started = false;
        if (entry.worker && !entry.worker->is_running()) {
            reset_channel_runtime_state_locked(entry);
            std::string worker_error;
            if (!entry.worker->start(worker_error)) {
                entry.state = "error";
                entry.last_error = worker_error;
                return Result<ActiveConnection>::err(
                    {"runtime_start_failed", worker_error});
            }
            entry.state = "active";
            entry.last_error.clear();
            worker_just_started = true;
        }
        if (worker_just_started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const bool emitted_frames = std::any_of(
                entry.channels.begin(),
                entry.channels.end(),
                [](const auto& channel) {
                    return channel->frames_published.load(std::memory_order_relaxed) > 0;
                });
            if (!entry.worker->is_running() && !emitted_frames) {
                entry.state = "error";
                entry.last_error = "capture worker exited before publishing frames";
                return Result<ActiveConnection>::err(
                    {"runtime_start_failed", entry.last_error});
            }
        }

        std::shared_ptr<ChannelState> channel;
        if (stream_name.empty()) {
            if (entry.channels.size() != 1) {
                return Result<ActiveConnection>::err(
                    {"missing_stream_name",
                     "stream_name is required for grouped or multi-stream sessions"});
            }
            channel = entry.channels.front();
        } else {
            const auto alias_it = entry.channel_aliases.find(std::string(stream_name));
            if (alias_it == entry.channel_aliases.end()) {
                return Result<ActiveConnection>::err(
                    {"not_found", "requested stream_name is not available on this session"});
            }
            channel = alias_it->second;
        }

        const int eventfd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (eventfd < 0) {
            return Result<ActiveConnection>::err(
                {"ipc_error", "failed to allocate reader eventfd"});
        }

        const int writer_eventfd = channel->writer->add_reader_eventfd(eventfd);
        if (writer_eventfd < 0) {
            ::close(eventfd);
            return Result<ActiveConnection>::err(
                {"ipc_error", "failed to register reader eventfd"});
        }

        channel->attached_consumers.fetch_add(1, std::memory_order_relaxed);
        ActiveConnection connection;
        connection.session_id = session_id;
        connection.channel = channel;
        connection.leased_eventfd = eventfd;
        connection.writer_eventfd = writer_eventfd;
        connection.client_fd = -1;
        entry.channel_aliases[channel->channel_id] = channel;
        return Result<ActiveConnection>::ok(std::move(connection));
    }

    bool register_connection_locked(const ActiveConnection& connection) {
        if (epoll_fd_ < 0 || connection.client_fd < 0) {
            return false;
        }

        epoll_event event{};
        event.events = EPOLLRDHUP | EPOLLHUP | EPOLLERR;
        event.data.fd = connection.client_fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, connection.client_fd, &event) < 0) {
            return false;
        }
        connections_[connection.client_fd] = connection;
        return true;
    }

    void cleanup_connection_locked(const ActiveConnection& connection) {
        if (connection.channel) {
            connection.channel->writer->remove_reader_eventfd(connection.writer_eventfd);
            connection.channel->attached_consumers.fetch_sub(1, std::memory_order_relaxed);
        }
        const auto session_it = session_to_stream_.find(connection.session_id);
        if (session_it != session_to_stream_.end()) {
            const auto entry_it = entries_.find(session_it->second);
            if (entry_it != entries_.end() &&
                !effective_rtsp_locked(entry_it->second) &&
                !has_ipc_consumers_locked(entry_it->second)) {
                pause_idle_worker_locked(entry_it->second);
            }
        }
        if (connection.leased_eventfd >= 0) {
            ::close(connection.leased_eventfd);
        }
        if (epoll_fd_ >= 0 && connection.client_fd >= 0) {
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, connection.client_fd, nullptr);
        }
        if (connection.client_fd >= 0) {
            ::close(connection.client_fd);
        }
    }

    void cleanup_connections_for_session_locked(std::int64_t session_id) {
        std::vector<int> doomed;
        for (const auto& [fd, connection] : connections_) {
            if (connection.session_id == session_id) {
                doomed.push_back(fd);
            }
        }
        for (const int fd : doomed) {
            const auto it = connections_.find(fd);
            if (it == connections_.end()) {
                continue;
            }
            cleanup_connection_locked(it->second);
            connections_.erase(it);
        }
    }

    void cleanup_all_connections_locked() {
        std::vector<int> doomed;
        doomed.reserve(connections_.size());
        for (const auto& [fd, _] : connections_) {
            doomed.push_back(fd);
        }
        for (const int fd : doomed) {
            const auto it = connections_.find(fd);
            if (it == connections_.end()) {
                continue;
            }
            cleanup_connection_locked(it->second);
            connections_.erase(it);
        }
    }

    void poll_disconnects() {
        if (epoll_fd_ < 0) {
            return;
        }

        epoll_event events[16];
        const int count = ::epoll_wait(epoll_fd_, events, 16, 0);
        if (count <= 0) {
            return;
        }

        std::scoped_lock lock(mutex_);
        for (int index = 0; index < count; ++index) {
            if ((events[index].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) == 0) {
                continue;
            }
            const auto it = connections_.find(events[index].data.fd);
            if (it == connections_.end()) {
                continue;
            }
            cleanup_connection_locked(it->second);
            connections_.erase(it);
        }
    }

    void stop_entry_locked(Entry& entry) {
        if (entry.rtsp_publication && entry.rtsp_publication->publisher) {
            entry.rtsp_publication->publisher->stop();
            entry.rtsp_publication->publisher.reset();
        }
        if (entry.worker) {
            entry.worker->stop();
            entry.worker.reset();
        }
        entry.channel_aliases.clear();
        entry.channels.clear();
        entry.state = "stopped";
    }

    void stop_all_entries_locked() {
        for (auto& [_, entry] : entries_) {
            stop_entry_locked(entry);
        }
        entries_.clear();
    }

    void accept_loop() {
        while (true) {
            {
                std::scoped_lock lock(mutex_);
                if (stop_requested_) {
                    break;
                }
            }

            poll_disconnects();

            pollfd descriptor{};
            {
                std::scoped_lock lock(mutex_);
                descriptor.fd = listen_fd_;
            }
            descriptor.events = POLLIN;

            const int poll_result = ::poll(&descriptor, 1, 200);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (poll_result == 0) {
                continue;
            }

            int listen_fd = -1;
            {
                std::scoped_lock lock(mutex_);
                listen_fd = listen_fd_;
            }
            if (listen_fd < 0) {
                continue;
            }

            auto accept_res = ipc::accept_socket(listen_fd);
            if (!accept_res.ok()) {
                if (accept_res.error().code == "socket_eagain") {
                    continue;
                }
                continue;
            }

            const int client_fd = accept_res.value();
            handle_client(client_fd);
        }
    }

    bool handle_client(int client_fd) {
        pollfd descriptor{};
        descriptor.fd = client_fd;
        descriptor.events = POLLIN;
        while (true) {
            const int poll_result = ::poll(&descriptor, 1, 2000);
            if (poll_result > 0) {
                break;
            }
            if (poll_result == 0) {
                ::close(client_fd);
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            ::close(client_fd);
            return false;
        }

        auto message_res = ipc::recv_message(client_fd, 4096, 0);
        if (!message_res.ok() || message_res.value().payload.empty()) {
            ::close(client_fd);
            return false;
        }

        nlohmann::json request;
        try {
            request = nlohmann::json::parse(message_res.value().payload);
        } catch (...) {
            const auto payload =
                nlohmann::json{{"status", "error"}, {"error", "invalid JSON"}}.dump();
            ipc::send_message(client_fd, payload, {});
            ::close(client_fd);
            return false;
        }

        std::int64_t session_id = 0;
        if (request.contains("session_id") && request.at("session_id").is_number_integer()) {
            session_id = request.at("session_id").get<std::int64_t>();
        } else if (request.contains("session_id") &&
                   request.at("session_id").is_string()) {
            try {
                session_id = std::stoll(request.at("session_id").get<std::string>());
            } catch (...) {
                session_id = 0;
            }
        }
        if (session_id <= 0) {
            const auto payload =
                nlohmann::json{{"status", "error"}, {"error", "missing session_id"}}.dump();
            ipc::send_message(client_fd, payload, {});
            ::close(client_fd);
            return false;
        }

        const auto stream_name = request.value("stream_name", std::string{});

        const auto attach_res = [&]() {
            std::scoped_lock lock(mutex_);
            return attach_ipc_consumer_locked(session_id, stream_name);
        }();
        if (!attach_res.ok()) {
            const auto payload = nlohmann::json{
                {"status", "error"},
                {"code", attach_res.error().code},
                {"error", attach_res.error().message},
            };
            ipc::send_message(client_fd, payload.dump(), {});
            ::close(client_fd);
            return false;
        }

        auto connection = attach_res.value();
        connection.client_fd = client_fd;

        const auto channel = connection.channel;
        nlohmann::json payload = {
            {"status", "ok"},
            {"channel_id", channel->channel_id},
            {"stream",
             {
                 {"stream_id", channel->stream_name},
                 {"stream_name", channel->stream_name},
                 {"route_name", channel->route_name},
                 {"selector", channel->selector},
                 {"media_kind", channel->media_kind},
                 {"delivered_caps_json", channel->delivered_caps_json},
             }},
        };

        std::string dup_error;
        const int memfd = channel->channel->dup_memfd(dup_error);
        if (memfd < 0) {
            std::scoped_lock lock(mutex_);
            cleanup_connection_locked(connection);
            return false;
        }

        const int eventfd = connection.leased_eventfd;
        if (eventfd < 0) {
            ::close(memfd);
            {
                std::scoped_lock lock(mutex_);
                cleanup_connection_locked(connection);
            }
            return false;
        }

        const auto send_res = ipc::send_message(client_fd, payload.dump(), {memfd, eventfd});
        ::close(memfd);
        if (!send_res.ok()) {
            std::scoped_lock lock(mutex_);
            cleanup_connection_locked(connection);
            return false;
        }
        ::close(connection.leased_eventfd);
        connection.leased_eventfd = -1;
        {
            std::scoped_lock lock(mutex_);
            if (!register_connection_locked(connection)) {
                cleanup_connection_locked(connection);
                return false;
            }
        }
        return true;
    }

    void detach_locked(std::int64_t session_id) {
        const auto session_it = session_to_stream_.find(session_id);
        if (session_it == session_to_stream_.end()) {
            return;
        }

        const auto stream_id = session_it->second;
        const auto entry_it = entries_.find(stream_id);
        session_to_stream_.erase(session_it);
        if (entry_it == entries_.end()) {
            return;
        }

        auto& entry = entry_it->second;
        cleanup_connections_for_session_locked(session_id);
        entry.consumer_rtsp.erase(session_id);
        if (entry.consumer_rtsp.empty()) {
            stop_entry_locked(entry);
            entries_.erase(entry_it);
            return;
        }
        if (entry.owner_session_id == session_id) {
            entry.owner_session_id = entry.consumer_rtsp.begin()->first;
        }
        reconcile_rtsp_publication_locked(entry);
    }

    mutable std::mutex mutex_;
    std::string socket_path_;
    std::map<std::int64_t, Entry> entries_;
    std::map<std::int64_t, std::int64_t> session_to_stream_;
    int epoll_fd_{-1};
    int listen_fd_{-1};
    bool running_{false};
    bool stop_requested_{false};
    std::string rtsp_host_;
    std::thread accept_thread_;
    std::unordered_map<int, ActiveConnection> connections_;
};

namespace {

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            stmt_ = nullptr;
        }
    }

    ~Stmt() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    explicit operator bool() const { return stmt_ != nullptr; }
    sqlite3_stmt* get() const { return stmt_; }

    void bind_text(int index, std::string_view value) {
        sqlite3_bind_text(stmt_, index, value.data(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    void bind_text_or_null(int index, std::string_view value) {
        if (value.empty()) {
            sqlite3_bind_null(stmt_, index);
        } else {
            bind_text(index, value);
        }
    }

    void bind_json_or_null(int index, const nlohmann::json& value) {
        if (value.is_null() || value.empty()) {
            sqlite3_bind_null(stmt_, index);
        } else {
            const auto dumped = value.dump();
            bind_text(index, dumped);
        }
    }

    void bind_int(int index, int value) {
        sqlite3_bind_int(stmt_, index, value);
    }

    void bind_int64(int index, std::int64_t value) {
        sqlite3_bind_int64(stmt_, index, value);
    }

    bool step() { return sqlite3_step(stmt_) == SQLITE_ROW; }

    bool exec() {
        const int rc = sqlite3_step(stmt_);
        return rc == SQLITE_DONE || rc == SQLITE_ROW;
    }

    std::string col_text(int index) const {
        const auto* text = sqlite3_column_text(stmt_, index);
        return text == nullptr ? std::string{}
                               : std::string(reinterpret_cast<const char*>(text));
    }

    std::int64_t col_int64(int index) const {
        return sqlite3_column_int64(stmt_, index);
    }

private:
    sqlite3_stmt* stmt_{nullptr};
};

struct ParsedInsightUri {
    std::string host;
    std::string device;
    std::string selector;
};

struct StreamLookupResult {
    SessionResolvedSource source;
};

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json parse_json(const std::string& text) {
    if (text.empty()) {
        return nlohmann::json::object();
    }
    try {
        return nlohmann::json::parse(text);
    } catch (...) {
        return nlohmann::json::object();
    }
}

std::string derive_uri(const std::string& uri_host,
                       const std::string& public_name,
                       const std::string& stream_public_name) {
    if (public_name.empty() || stream_public_name.empty()) {
        return {};
    }
    return "insightos://" + uri_host + "/" + public_name + "/" + stream_public_name;
}

std::string derive_rtsp_url(const std::string& rtsp_host,
                            const std::string& public_name,
                            const std::string& stream_public_name) {
    if (public_name.empty() || stream_public_name.empty()) {
        return {};
    }
    return "rtsp://" + rtsp_host + "/" + public_name + "/" + stream_public_name;
}

bool parse_insight_uri(std::string_view input,
                       ParsedInsightUri& parsed,
                       std::string& error_message) {
    constexpr std::string_view prefix = "insightos://";
    if (!input.starts_with(prefix)) {
        error_message = "Input must use the insightos:// scheme";
        return false;
    }

    const std::string_view remainder = input.substr(prefix.size());
    const auto first_slash = remainder.find('/');
    if (first_slash == std::string_view::npos || first_slash == 0) {
        error_message = "Input must include host, device, and selector";
        return false;
    }

    parsed.host = std::string(remainder.substr(0, first_slash));
    const std::string_view device_and_selector = remainder.substr(first_slash + 1);
    const auto second_slash = device_and_selector.find('/');
    if (second_slash == std::string_view::npos || second_slash == 0 ||
        second_slash + 1 >= device_and_selector.size()) {
        error_message = "Input must include both device and selector path segments";
        return false;
    }

    parsed.device = std::string(device_and_selector.substr(0, second_slash));
    parsed.selector = std::string(device_and_selector.substr(second_slash + 1));

    if (parsed.device.empty() || parsed.selector.empty()) {
        error_message = "Input must include both device and selector";
        return false;
    }

    if (parsed.selector.find('?') != std::string::npos ||
        parsed.selector.find('#') != std::string::npos) {
        error_message = "Input selector must not include query or fragment components";
        return false;
    }

    return true;
}

bool validate_local_uri_host(std::string_view actual_host,
                             std::string_view expected_host,
                             int& error_status,
                             std::string& error_code,
                             std::string& error_message) {
    if (actual_host == expected_host) {
        return true;
    }

    error_status = 422;
    error_code = "invalid_input";
    error_message = "Input URI host must match the local catalog host '" +
                    std::string(expected_host) + "'";
    return false;
}

std::optional<StreamLookupResult> lookup_stream(sqlite3* db,
                                                const ParsedInsightUri& parsed,
                                                const std::string& uri_host,
                                                int& error_status,
                                                std::string& error_code,
                                                std::string& error_message) {
    Stmt query(
        db,
        "SELECT s.stream_id, d.device_key, d.public_name, s.selector, s.public_name, "
        "s.media_kind, s.shape_kind, s.channel, s.group_key, s.caps_json, "
        "s.capture_policy_json, s.members_json, s.publications_json "
        "FROM streams s "
        "JOIN devices d ON d.device_id = s.device_id "
        "WHERE d.public_name = ? AND (s.public_name = ? OR s.selector = ?) "
        "AND s.is_present = 1 "
        "AND d.status != 'offline'");
    if (!query) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare stream lookup";
        return std::nullopt;
    }

    query.bind_text(1, parsed.device);
    query.bind_text(2, parsed.selector);
    query.bind_text(3, parsed.selector);
    if (!query.step()) {
        error_status = 404;
        error_code = "unknown_input";
        error_message = "Input URI does not match a catalog-published source";
        return std::nullopt;
    }

    StreamLookupResult result;
    result.source.stream_id = query.col_int64(0);
    result.source.device_key = query.col_text(1);
    result.source.public_name = query.col_text(2);
    result.source.selector = query.col_text(3);
    result.source.stream_public_name = query.col_text(4);
    result.source.stream_default_name = result.source.selector;
    result.source.media_kind = query.col_text(5);
    result.source.shape_kind = query.col_text(6);
    result.source.channel = query.col_text(7);
    result.source.group_key = query.col_text(8);
    result.source.delivered_caps_json = parse_json(query.col_text(9));
    result.source.capture_policy_json = parse_json(query.col_text(10));
    result.source.members_json = parse_json(query.col_text(11));
    result.source.publications_json = parse_json(query.col_text(12));
    result.source.uri = derive_uri(uri_host,
                                   result.source.public_name,
                                   result.source.stream_public_name);
    return result;
}

std::optional<SessionResolvedSource> lookup_stream_by_id(sqlite3* db,
                                                         std::int64_t stream_id,
                                                         const std::string& uri_host) {
    if (stream_id <= 0) {
        return std::nullopt;
    }

    Stmt query(
        db,
        "SELECT s.stream_id, d.device_key, d.public_name, s.selector, s.public_name, "
        "s.media_kind, s.shape_kind, s.channel, s.group_key, s.caps_json, "
        "s.capture_policy_json, s.members_json, s.publications_json "
        "FROM streams s "
        "JOIN devices d ON d.device_id = s.device_id "
        "WHERE s.stream_id = ?");
    if (!query) {
        return std::nullopt;
    }

    query.bind_int64(1, stream_id);
    if (!query.step()) {
        return std::nullopt;
    }

    SessionResolvedSource source;
    source.stream_id = query.col_int64(0);
    source.device_key = query.col_text(1);
    source.public_name = query.col_text(2);
    source.selector = query.col_text(3);
    source.stream_public_name = query.col_text(4);
    source.stream_default_name = source.selector;
    source.media_kind = query.col_text(5);
    source.shape_kind = query.col_text(6);
    source.channel = query.col_text(7);
    source.group_key = query.col_text(8);
    source.delivered_caps_json = parse_json(query.col_text(9));
    source.capture_policy_json = parse_json(query.col_text(10));
    source.members_json = parse_json(query.col_text(11));
    source.publications_json = parse_json(query.col_text(12));
    source.uri = derive_uri(uri_host, source.public_name, source.stream_public_name);
    return source;
}

SessionRecord hydrate_session(const Stmt& query,
                              const std::string& uri_host,
                              const std::string& rtsp_host) {
    SessionRecord session;
    session.session_id = query.col_int64(0);
    session.session_kind = query.col_text(1);
    session.rtsp_enabled = query.col_int64(2) != 0;
    session.request_json = parse_json(query.col_text(3));
    session.resolved_members_json = parse_json(query.col_text(4));
    session.state = query.col_text(5);
    session.last_error = query.col_text(6);
    session.started_at_ms = query.col_int64(7);
    session.stopped_at_ms = query.col_int64(8);
    session.created_at_ms = query.col_int64(9);
    session.updated_at_ms = query.col_int64(10);
    session.source.stream_id = query.col_int64(11);
    session.source.device_key = query.col_text(12);
    session.source.public_name = query.col_text(13);
    session.source.selector = query.col_text(14);
    session.source.stream_public_name = query.col_text(15);
    session.source.stream_default_name = session.source.selector;
    session.source.media_kind = query.col_text(16);
    session.source.shape_kind = query.col_text(17);
    session.source.channel = query.col_text(18);
    session.source.group_key = query.col_text(19);
    session.source.delivered_caps_json = parse_json(query.col_text(20));
    session.source.capture_policy_json = parse_json(query.col_text(21));
    session.source.members_json = parse_json(query.col_text(22));
    session.source.publications_json = parse_json(query.col_text(23));
    session.source.uri = derive_uri(uri_host,
                                    session.source.public_name,
                                    session.source.stream_public_name);
    if (session.rtsp_enabled) {
        session.rtsp_url = derive_rtsp_url(rtsp_host,
                                           session.source.public_name,
                                           session.source.stream_public_name);
    }
    return session;
}

bool upsert_log(sqlite3* db,
                std::int64_t session_id,
                std::string_view event_type,
                std::string_view message,
                const nlohmann::json& payload = nullptr) {
    Stmt statement(
        db,
        "INSERT INTO session_logs (session_id, level, event_type, message, payload_json, created_at_ms) "
        "VALUES (?, 'info', ?, ?, ?, ?)");
    if (!statement) {
        return false;
    }
    statement.bind_int64(1, session_id);
    statement.bind_text(2, event_type);
    statement.bind_text(3, message);
    statement.bind_json_or_null(4, payload);
    statement.bind_int64(5, now_ms());
    return statement.exec();
}

bool update_session_rtsp(sqlite3* db, std::int64_t session_id, bool rtsp_enabled) {
    Stmt statement(
        db,
        "UPDATE sessions SET rtsp_enabled = ?, updated_at_ms = ? WHERE session_id = ?");
    if (!statement) {
        return false;
    }
    statement.bind_int(1, rtsp_enabled ? 1 : 0);
    statement.bind_int64(2, now_ms());
    statement.bind_int64(3, session_id);
    return statement.exec();
}

bool delete_session_row(sqlite3* db, std::int64_t session_id) {
    Stmt statement(db, "DELETE FROM sessions WHERE session_id = ?");
    if (!statement) {
        return false;
    }
    statement.bind_int64(1, session_id);
    return statement.exec();
}

bool mark_session_stopped_with_error(sqlite3* db,
                                     std::int64_t session_id,
                                     std::string_view last_error) {
    Stmt statement(
        db,
        "UPDATE sessions SET state = 'stopped', last_error = ?, stopped_at_ms = ?, "
        "updated_at_ms = ? WHERE session_id = ?");
    if (!statement) {
        return false;
    }
    const auto timestamp = now_ms();
    statement.bind_text_or_null(1, last_error);
    statement.bind_int64(2, timestamp);
    statement.bind_int64(3, timestamp);
    statement.bind_int64(4, session_id);
    return statement.exec();
}

}  // namespace

SessionService::SessionService(SchemaStore& store,
                               std::string uri_host,
                               std::string rtsp_host)
    : store_(store),
      uri_host_(std::move(uri_host)),
      rtsp_host_(std::move(rtsp_host)),
      runtime_registry_(std::make_unique<ServingRuntimeRegistry>(
          default_ipc_socket_path(store.database_path()),
          rtsp_host_)) {}

SessionService::~SessionService() = default;

bool SessionService::initialize() {
    Stmt normalize(
        store_.db(),
        "UPDATE sessions SET state = 'stopped', stopped_at_ms = COALESCE(stopped_at_ms, ?), "
        "updated_at_ms = ? WHERE state = 'active'");
    if (!normalize) {
        return false;
    }
    const auto timestamp = now_ms();
    normalize.bind_int64(1, timestamp);
    normalize.bind_int64(2, timestamp);
    if (!normalize.exec()) {
        return false;
    }
    runtime_registry_->clear();
    std::string start_error;
    return runtime_registry_->start(start_error);
}

std::vector<SessionRecord> SessionService::list_sessions() const {
    std::vector<SessionRecord> sessions;
    Stmt query(
        store_.db(),
        "SELECT sess.session_id, sess.session_kind, sess.rtsp_enabled, sess.request_json, "
        "sess.resolved_members_json, sess.state, COALESCE(sess.last_error, ''), "
        "COALESCE(sess.started_at_ms, 0), COALESCE(sess.stopped_at_ms, 0), "
        "sess.created_at_ms, sess.updated_at_ms, "
        "COALESCE(s.stream_id, 0), COALESCE(d.device_key, ''), COALESCE(d.public_name, ''), "
        "COALESCE(s.selector, ''), COALESCE(s.public_name, ''), "
        "COALESCE(s.media_kind, ''), COALESCE(s.shape_kind, ''), "
        "COALESCE(s.channel, ''), COALESCE(s.group_key, ''), "
        "COALESCE(s.caps_json, '{}'), COALESCE(s.capture_policy_json, '{}'), "
        "COALESCE(s.members_json, '{}'), COALESCE(s.publications_json, '{}') "
        "FROM sessions sess "
        "LEFT JOIN streams s ON s.stream_id = sess.stream_id "
        "LEFT JOIN devices d ON d.device_id = s.device_id "
        "ORDER BY sess.session_id");
    if (!query) {
        return sessions;
    }

    while (query.step()) {
        sessions.push_back(enrich_runtime(hydrate_session(query, uri_host_, rtsp_host_)));
    }
    return sessions;
}

std::optional<SessionRecord> SessionService::get_session(std::int64_t session_id) const {
    Stmt query(
        store_.db(),
        "SELECT sess.session_id, sess.session_kind, sess.rtsp_enabled, sess.request_json, "
        "sess.resolved_members_json, sess.state, COALESCE(sess.last_error, ''), "
        "COALESCE(sess.started_at_ms, 0), COALESCE(sess.stopped_at_ms, 0), "
        "sess.created_at_ms, sess.updated_at_ms, "
        "COALESCE(s.stream_id, 0), COALESCE(d.device_key, ''), COALESCE(d.public_name, ''), "
        "COALESCE(s.selector, ''), COALESCE(s.public_name, ''), "
        "COALESCE(s.media_kind, ''), COALESCE(s.shape_kind, ''), "
        "COALESCE(s.channel, ''), COALESCE(s.group_key, ''), "
        "COALESCE(s.caps_json, '{}'), COALESCE(s.capture_policy_json, '{}'), "
        "COALESCE(s.members_json, '{}'), COALESCE(s.publications_json, '{}') "
        "FROM sessions sess "
        "LEFT JOIN streams s ON s.stream_id = sess.stream_id "
        "LEFT JOIN devices d ON d.device_id = s.device_id "
        "WHERE sess.session_id = ?");
    if (!query) {
        return std::nullopt;
    }
    query.bind_int64(1, session_id);
    if (!query.step()) {
        return std::nullopt;
    }
    return enrich_runtime(hydrate_session(query, uri_host_, rtsp_host_));
}

RuntimeStatusSnapshot SessionService::runtime_status() const {
    RuntimeStatusSnapshot snapshot;
    snapshot.sessions = list_sessions();
    snapshot.total_sessions = static_cast<int>(snapshot.sessions.size());
    for (const auto& session : snapshot.sessions) {
        if (session.state == "active") {
            ++snapshot.active_sessions;
        } else if (session.state == "stopped") {
            ++snapshot.stopped_sessions;
        }
    }
    snapshot.serving_runtimes = runtime_registry_->snapshot();
    for (auto& runtime : snapshot.serving_runtimes) {
        const auto current_source =
            lookup_stream_by_id(store_.db(), runtime.stream_id, uri_host_);
        if (!current_source.has_value()) {
            continue;
        }
        runtime.source = *current_source;
        if ((runtime.resolved_members_json.is_null() ||
             runtime.resolved_members_json.empty()) &&
            !runtime.source.members_json.is_null() &&
            !runtime.source.members_json.empty()) {
            runtime.resolved_members_json = runtime.source.members_json;
        }
    }
    snapshot.total_serving_runtimes =
        static_cast<int>(snapshot.serving_runtimes.size());
    return snapshot;
}

const std::string& SessionService::ipc_socket_path() const {
    return runtime_registry_->socket_path();
}

bool SessionService::create_direct_session(const std::string& input,
                                          bool rtsp_enabled,
                                          SessionRecord& created,
                                          int& error_status,
                                          std::string& error_code,
                                          std::string& error_message) const {
    ParsedInsightUri parsed;
    if (!parse_insight_uri(input, parsed, error_message)) {
        error_status = 422;
        error_code = "invalid_input";
        return false;
    }
    if (!validate_local_uri_host(parsed.host,
                                 uri_host_,
                                 error_status,
                                 error_code,
                                 error_message)) {
        return false;
    }

    const auto stream = lookup_stream(store_.db(),
                                      parsed,
                                      uri_host_,
                                      error_status,
                                      error_code,
                                      error_message);
    if (!stream.has_value()) {
        return false;
    }

    const auto timestamp = now_ms();
    const auto request_json = nlohmann::json{
        {"input", input},
        {"rtsp_enabled", rtsp_enabled},
    };
    const auto resolved_members =
        stream->source.members_json.is_array() ? stream->source.members_json
                                               : nlohmann::json::array();

    Stmt insert(
        store_.db(),
        "INSERT INTO sessions (stream_id, session_kind, rtsp_enabled, request_json, "
        "resolved_members_json, state, last_error, started_at_ms, stopped_at_ms, "
        "created_at_ms, updated_at_ms) "
        "VALUES (?, 'direct', ?, ?, ?, 'active', NULL, ?, NULL, ?, ?)");
    if (!insert) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare direct session insert";
        return false;
    }

    insert.bind_int64(1, stream->source.stream_id);
    insert.bind_int(2, rtsp_enabled ? 1 : 0);
    insert.bind_text(3, request_json.dump());
    insert.bind_json_or_null(4, resolved_members);
    insert.bind_int64(5, timestamp);
    insert.bind_int64(6, timestamp);
    insert.bind_int64(7, timestamp);
    if (!insert.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to create direct session";
        return false;
    }

    const auto session_id = sqlite3_last_insert_rowid(store_.db());
    if (!attach_session_runtime(session_id,
                                created,
                                error_status,
                                error_code,
                                error_message)) {
        delete_session_row(store_.db(), session_id);
        return false;
    }
    upsert_log(store_.db(),
               session_id,
               "session_created",
               "Created direct session",
               nlohmann::json{
                   {"input", input},
                   {"rtsp_enabled", rtsp_enabled},
                   {"runtime_reused",
                    created.serving_runtime.has_value() &&
                        created.serving_runtime->shared},
               });
    return true;
}

bool SessionService::start_session(std::int64_t session_id,
                                   SessionRecord& updated,
                                   int& error_status,
                                   std::string& error_code,
                                   std::string& error_message) const {
    if (!get_session(session_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "Session '" + std::to_string(session_id) + "' not found";
        return false;
    }

    Stmt statement(
        store_.db(),
        "UPDATE sessions SET state = 'active', started_at_ms = ?, stopped_at_ms = NULL, "
        "updated_at_ms = ?, last_error = NULL WHERE session_id = ?");
    if (!statement) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare session start";
        return false;
    }

    const auto timestamp = now_ms();
    statement.bind_int64(1, timestamp);
    statement.bind_int64(2, timestamp);
    statement.bind_int64(3, session_id);
    if (!statement.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to start session";
        return false;
    }

    if (!attach_session_runtime(session_id,
                                updated,
                                error_status,
                                error_code,
                                error_message)) {
        mark_session_stopped_with_error(store_.db(), session_id, error_message);
        return false;
    }
    upsert_log(store_.db(),
               session_id,
               "session_started",
               "Started direct session",
               nlohmann::json{
                   {"runtime_reused",
                    updated.serving_runtime.has_value() &&
                        updated.serving_runtime->shared},
               });
    return true;
}

bool SessionService::stop_session(std::int64_t session_id,
                                  SessionRecord& updated,
                                  int& error_status,
                                  std::string& error_code,
                                  std::string& error_message) const {
    if (!get_session(session_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "Session '" + std::to_string(session_id) + "' not found";
        return false;
    }

    Stmt statement(
        store_.db(),
        "UPDATE sessions SET state = 'stopped', stopped_at_ms = ?, updated_at_ms = ? "
        "WHERE session_id = ?");
    if (!statement) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare session stop";
        return false;
    }

    const auto timestamp = now_ms();
    statement.bind_int64(1, timestamp);
    statement.bind_int64(2, timestamp);
    statement.bind_int64(3, session_id);
    if (!statement.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to stop session";
        return false;
    }

    runtime_registry_->detach(session_id);
    upsert_log(store_.db(),
               session_id,
               "session_stopped",
               "Stopped direct session");

    updated = *get_session(session_id);
    return true;
}

bool SessionService::delete_session(std::int64_t session_id,
                                    int& error_status,
                                    std::string& error_code,
                                    std::string& error_message) const {
    if (!get_session(session_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "Session '" + std::to_string(session_id) + "' not found";
        return false;
    }

    Stmt referenced(
        store_.db(),
        "SELECT 1 FROM app_sources WHERE source_session_id = ? OR active_session_id = ? "
        "LIMIT 1");
    if (!referenced) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare session reference check";
        return false;
    }
    referenced.bind_int64(1, session_id);
    referenced.bind_int64(2, session_id);
    if (referenced.step()) {
        error_status = 409;
        error_code = "conflict";
        error_message =
            "Session '" + std::to_string(session_id) + "' is still referenced";
        return false;
    }

    Stmt statement(store_.db(), "DELETE FROM sessions WHERE session_id = ?");
    if (!statement) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare session delete";
        return false;
    }
    statement.bind_int64(1, session_id);
    if (!statement.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to delete session";
        return false;
    }
    runtime_registry_->detach(session_id);
    return true;
}

bool SessionService::attach_session_runtime(std::int64_t session_id,
                                            SessionRecord& updated,
                                            int& error_status,
                                            std::string& error_code,
                                            std::string& error_message) const {
    const auto session = get_session(session_id);
    if (!session.has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "Session '" + std::to_string(session_id) + "' not found";
        return false;
    }
    if (session->source.stream_id == 0) {
        error_status = 500;
        error_code = "internal";
        error_message = "Session is missing resolved source metadata";
        return false;
    }

    updated = *session;
    if (updated.state == "active") {
        auto attach_result = runtime_registry_->attach(updated);
        if (!attach_result.ok()) {
            error_status = 500;
            error_code = attach_result.error().code;
            error_message = attach_result.error().message;
            return false;
        }
        updated.serving_runtime = std::move(attach_result.value());
    } else {
        runtime_registry_->detach(session_id);
        updated.serving_runtime.reset();
    }
    return true;
}

bool SessionService::ensure_session_rtsp(std::int64_t session_id,
                                         SessionRecord& updated,
                                         int& error_status,
                                         std::string& error_code,
                                         std::string& error_message) const {
    auto session = get_session(session_id);
    if (!session.has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "Session '" + std::to_string(session_id) + "' not found";
        return false;
    }

    if (!session->rtsp_enabled &&
        !update_session_rtsp(store_.db(), session_id, true)) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to update session RTSP state";
        return false;
    }

    if (!attach_session_runtime(session_id,
                                updated,
                                error_status,
                                error_code,
                                error_message)) {
        return false;
    }
    upsert_log(store_.db(),
               session_id,
               "session_rtsp_enabled",
               "Enabled RTSP publication intent for active session");
    return true;
}

SessionRecord SessionService::enrich_runtime(SessionRecord session) const {
    if (session.state == "active") {
        session.serving_runtime = runtime_registry_->view_for_session(session.session_id);
    } else {
        session.serving_runtime.reset();
    }
    return session;
}

}  // namespace insightio::backend
