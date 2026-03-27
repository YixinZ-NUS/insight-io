// role: high-level route-oriented SDK implementation for task-9 callbacks.
// revision: 2026-03-27 task9-sdk-review-fixes
// major changes: adds route declaration, startup CLI binds, running-app REST
// control, exact and grouped IPC attach, per-route callback fanout, and fixes
// omitted-name derivation for the `bind_from_cli()` plus `connect()` path. See
// docs/past-tasks.md.

#include "insightos/app.hpp"

#include "insightio/backend/ipc.hpp"
#include "insightio/backend/types.hpp"
#include "insightio/backend/unix_socket.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <poll.h>
#include <set>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace insightos {

namespace {

using insightio::backend::ipc::Reader;

volatile std::sig_atomic_t g_cli_stop_requested = 0;

void cli_signal_handler(int) {
    g_cli_stop_requested = 1;
}

class ScopedCliSignals {
public:
    ScopedCliSignals() {
        g_cli_stop_requested = 0;
        install(SIGINT, old_sigint_);
        install(SIGTERM, old_sigterm_);
    }

    ~ScopedCliSignals() {
        restore(SIGINT, old_sigint_);
        restore(SIGTERM, old_sigterm_);
        g_cli_stop_requested = 0;
    }

    [[nodiscard]] bool stop_requested() const {
        return g_cli_stop_requested != 0;
    }

private:
    static void install(int signo, struct sigaction& previous) {
        struct sigaction action{};
        action.sa_handler = cli_signal_handler;
        sigemptyset(&action.sa_mask);
        ::sigaction(signo, &action, &previous);
    }

    static void restore(int signo, const struct sigaction& previous) {
        ::sigaction(signo, &previous, nullptr);
    }

    struct sigaction old_sigint_{};
    struct sigaction old_sigterm_{};
};

struct RouteCallbacks {
    std::function<void(const Caps&)> on_caps;
    std::function<void(const Frame&)> on_frame;
    std::function<void()> on_stop;

    [[nodiscard]] bool has_any() const {
        return static_cast<bool>(on_caps) || static_cast<bool>(on_frame) ||
               static_cast<bool>(on_stop);
    }
};

struct RouteDeclaration {
    std::string route_name;
    Expectation expectation;
    RouteCallbacks callbacks;
    std::int64_t route_id{0};
};

struct BindRequest {
    std::string target;
    std::optional<std::string> input;
    std::optional<std::int64_t> session_id;
    bool rtsp_enabled{false};
};

struct HttpResponse {
    bool ok{false};
    int status{0};
    nlohmann::json body;
    std::string error;
};

struct ChannelPlan {
    std::string attach_name;
    std::string route_name;
    std::string channel_id;
    std::string stream_name;
    std::string selector;
    std::string media_kind;
    nlohmann::json delivered_caps_json;
};

struct SourcePlan {
    std::int64_t source_id{0};
    std::int64_t session_id{0};
    std::string target;
    std::string state;
    std::string socket_path;
    std::string fingerprint;
    std::vector<ChannelPlan> channels;
};

struct ActiveChannel {
    std::int64_t source_id{0};
    std::int64_t session_id{0};
    std::string target;
    std::string route_name;
    std::string fingerprint;
    std::string channel_id;
    int control_fd{-1};
    std::shared_ptr<Reader> reader;
    Caps caps;
    RouteCallbacks callbacks;
    std::uint64_t sequence{0};
    bool stop_emitted{false};
};

std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::int64_t wall_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string derive_default_app_name(const std::string& program_name) {
    const auto base = std::filesystem::path(program_name).filename().string();
    const auto slug = insightio::backend::slugify(base);
    if (!slug.empty()) {
        return slug;
    }
    return "app-" + std::to_string(::getpid());
}

std::optional<std::int64_t> parse_session_binding(std::string_view value) {
    constexpr std::string_view prefix = "session:";
    if (!value.starts_with(prefix) || value.size() <= prefix.size()) {
        return std::nullopt;
    }
    try {
        return std::stoll(std::string(value.substr(prefix.size())));
    } catch (...) {
        return std::nullopt;
    }
}

nlohmann::json expectation_to_json(const Expectation& expectation) {
    nlohmann::json json = nlohmann::json::object();
    if (!expectation.media.empty()) {
        json["media"] = expectation.media;
    }
    if (!expectation.channel_name.empty()) {
        json["channel"] = expectation.channel_name;
    }
    return json;
}

Caps caps_from_json(std::string route_name,
                    std::string media_kind,
                    std::string channel_id,
                    std::string stream_name,
                    std::string selector,
                    const nlohmann::json& caps_json,
                    std::int64_t source_id,
                    std::int64_t session_id) {
    Caps caps;
    caps.route_name = std::move(route_name);
    caps.media_kind = std::move(media_kind);
    caps.channel_id = std::move(channel_id);
    caps.stream_name = std::move(stream_name);
    caps.selector = std::move(selector);
    caps.format = caps_json.value("format", std::string{});
    caps.width = caps_json.value("width", 0U);
    caps.height = caps_json.value("height", 0U);
    caps.fps = caps_json.value("fps", 0U);
    caps.sample_rate = caps_json.value("sample_rate", 0U);
    caps.channels = caps_json.value("channels", 0U);
    caps.source_id = source_id;
    caps.session_id = session_id;
    return caps;
}

std::string response_error(const httplib::Result& result) {
    if (!result) {
        return "HTTP request failed";
    }
    try {
        const auto body = nlohmann::json::parse(result->body);
        if (body.contains("message") && body.at("message").is_string()) {
            return body.at("message").get<std::string>();
        }
        if (body.contains("error") && body.at("error").is_string()) {
            return body.at("error").get<std::string>();
        }
    } catch (...) {
    }
    return "HTTP request returned status " + std::to_string(result->status);
}

std::string source_key(std::int64_t source_id, std::string_view route_name) {
    return std::to_string(source_id) + ":" + std::string(route_name);
}

bool is_descendant(std::string_view value, std::string_view prefix) {
    return value.size() > prefix.size() && value.substr(0, prefix.size()) == prefix &&
           value[prefix.size()] == '/';
}

}  // namespace

struct App::Impl {
    explicit Impl(Options options_in)
        : options(std::move(options_in)) {
        if (const char* env = std::getenv("INSIGHTIO_BACKEND_HOST");
            env && options.backend_host == "127.0.0.1") {
            options.backend_host = env;
        }
        if (const char* env = std::getenv("INSIGHTIO_BACKEND_PORT");
            env && options.backend_port == 18180) {
            try {
                options.backend_port =
                    static_cast<std::uint16_t>(std::stoul(env));
            } catch (...) {
            }
        }
    }

    Options options;
    std::vector<RouteDeclaration> routes;
    std::vector<BindRequest> startup_binds;
    std::map<std::string, ActiveChannel> active_channels;
    std::atomic_bool stop_requested{false};
    mutable std::mutex state_mutex;
    mutable std::mutex error_mutex;
    std::optional<std::int64_t> app_id_value;
    std::string app_name_value;
    std::string last_error_value;
    std::string program_name{"app"};

    void set_last_error(std::string message) {
        std::lock_guard lock(error_mutex);
        last_error_value = std::move(message);
    }

    void clear_last_error() {
        std::lock_guard lock(error_mutex);
        last_error_value.clear();
    }

    [[nodiscard]] std::string last_error() const {
        std::lock_guard lock(error_mutex);
        return last_error_value;
    }

    void set_app_identity(std::int64_t app_id, std::string name) {
        std::lock_guard lock(state_mutex);
        app_id_value = app_id;
        app_name_value = std::move(name);
    }

    void clear_app_identity() {
        std::lock_guard lock(state_mutex);
        app_id_value.reset();
        app_name_value.clear();
    }

    [[nodiscard]] std::optional<std::int64_t> app_id() const {
        std::lock_guard lock(state_mutex);
        return app_id_value;
    }

    [[nodiscard]] std::string app_name() const {
        std::lock_guard lock(state_mutex);
        return app_name_value;
    }

    [[nodiscard]] std::string request_base() const {
        return options.backend_host + ":" + std::to_string(options.backend_port);
    }

    HttpResponse request_json(std::string_view method,
                              const std::string& path,
                              const nlohmann::json* body = nullptr) const {
        httplib::Client client(options.backend_host, options.backend_port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(5, 0);
        client.set_write_timeout(5, 0);

        httplib::Result result;
        if (method == "GET") {
            result = client.Get(path.c_str());
        } else if (method == "POST") {
            const auto payload = body == nullptr ? std::string{"{}"} : body->dump();
            result = client.Post(path.c_str(), payload, "application/json");
        } else if (method == "DELETE") {
            result = client.Delete(path.c_str());
        } else {
            return HttpResponse{false, 0, {}, "unsupported HTTP method"};
        }

        if (!result) {
            return HttpResponse{false, 0, {}, "HTTP request to " + request_base() + path +
                                                   " failed"};
        }

        nlohmann::json parsed = nlohmann::json::object();
        if (!result->body.empty()) {
            try {
                parsed = nlohmann::json::parse(result->body);
            } catch (...) {
                parsed = nlohmann::json::object();
            }
        }

        if (result->status < 200 || result->status >= 300) {
            return HttpResponse{false, result->status, parsed, response_error(result)};
        }

        return HttpResponse{true, result->status, parsed, {}};
    }

    RouteDeclaration* find_route(std::string_view route_name) {
        for (auto& route : routes) {
            if (route.route_name == route_name) {
                return &route;
            }
        }
        return nullptr;
    }

    const RouteDeclaration* find_route(std::string_view route_name) const {
        for (const auto& route : routes) {
            if (route.route_name == route_name) {
                return &route;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool route_has_callbacks(std::string_view route_name) const {
        const auto* route = find_route(route_name);
        return route != nullptr && route->callbacks.has_any();
    }

    void queue_bind(BindRequest request) {
        for (auto& existing : startup_binds) {
            if (existing.target == request.target) {
                existing = std::move(request);
                return;
            }
        }
        startup_binds.push_back(std::move(request));
    }

    bool create_app_record() {
        std::string desired_name = options.name.empty()
                                       ? derive_default_app_name(program_name)
                                       : insightio::backend::slugify(options.name);
        if (desired_name.empty()) {
            desired_name = "app-" + std::to_string(::getpid());
        }

        nlohmann::json body = {
            {"name", desired_name},
        };
        if (!options.description.empty()) {
            body["description"] = options.description;
        }

        const auto response = request_json("POST", "/api/apps", &body);
        if (!response.ok) {
            set_last_error("failed to create app '" + desired_name + "': " + response.error);
            return false;
        }

        const auto app_id = response.body.value("app_id", std::int64_t{0});
        if (app_id <= 0) {
            set_last_error("backend did not return a valid app_id");
            return false;
        }
        set_app_identity(app_id, response.body.value("name", desired_name));
        return true;
    }

    void delete_app_record() {
        const auto current_app_id = app_id();
        if (!current_app_id.has_value()) {
            return;
        }
        (void)request_json("DELETE", "/api/apps/" + std::to_string(*current_app_id));
    }

    bool declare_routes() {
        const auto current_app_id = app_id();
        if (!current_app_id.has_value()) {
            set_last_error("app_id is missing before route declaration");
            return false;
        }
        for (auto& route : routes) {
            nlohmann::json body = {
                {"route_name", route.route_name},
            };
            const auto expect = expectation_to_json(route.expectation);
            if (!expect.empty()) {
                body["expect"] = expect;
            }
            const auto response = request_json(
                "POST",
                "/api/apps/" + std::to_string(*current_app_id) + "/routes",
                &body);
            if (!response.ok) {
                set_last_error("failed to declare route '" + route.route_name +
                               "': " + response.error);
                return false;
            }
            route.route_id = response.body.value("route_id", std::int64_t{0});
        }
        return true;
    }

    bool create_or_queue_bind(BindRequest request) {
        if (!app_id().has_value()) {
            queue_bind(std::move(request));
            return true;
        }

        nlohmann::json body = {
            {"target", request.target},
        };
        if (request.input.has_value()) {
            body["input"] = *request.input;
        }
        if (request.session_id.has_value()) {
            body["session_id"] = *request.session_id;
        }
        if (request.rtsp_enabled) {
            body["rtsp_enabled"] = true;
        }

        const auto response = request_json(
            "POST",
            "/api/apps/" + std::to_string(*app_id()) + "/sources",
            &body);
        if (!response.ok) {
            set_last_error("failed to bind target '" + request.target + "': " +
                           response.error);
            return false;
        }
        return true;
    }

    bool rebind_target(BindRequest request) {
        if (!app_id().has_value()) {
            queue_bind(std::move(request));
            return true;
        }

        const auto sources = request_json(
            "GET", "/api/apps/" + std::to_string(*app_id()) + "/sources");
        if (!sources.ok || !sources.body.contains("sources") ||
            !sources.body.at("sources").is_array()) {
            set_last_error("failed to inspect existing sources for rebind");
            return false;
        }

        std::int64_t source_id = 0;
        for (const auto& source : sources.body.at("sources")) {
            if (source.value("target", std::string{}) == request.target) {
                source_id = source.value("source_id", std::int64_t{0});
                break;
            }
        }
        if (source_id <= 0) {
            set_last_error("target '" + request.target + "' has no durable source to rebind");
            return false;
        }

        nlohmann::json body = nlohmann::json::object();
        if (request.input.has_value()) {
            body["input"] = *request.input;
        }
        if (request.session_id.has_value()) {
            body["session_id"] = *request.session_id;
        }
        if (request.rtsp_enabled) {
            body["rtsp_enabled"] = true;
        }

        const auto response = request_json(
            "POST",
            "/api/apps/" + std::to_string(*app_id()) + "/sources/" +
                std::to_string(source_id) + ":rebind",
            &body);
        if (!response.ok) {
            set_last_error("failed to rebind target '" + request.target + "': " +
                           response.error);
            return false;
        }
        return true;
    }

    [[nodiscard]] std::vector<std::string> grouped_roots() const {
        std::map<std::string, int> prefix_counts;
        std::set<std::string> route_names;
        std::vector<std::string> ordered_prefixes;
        for (const auto& route : routes) {
            route_names.insert(route.route_name);
            std::size_t slash = route.route_name.find('/');
            while (slash != std::string::npos) {
                const auto prefix = route.route_name.substr(0, slash);
                if (!prefix_counts.contains(prefix)) {
                    ordered_prefixes.push_back(prefix);
                }
                prefix_counts[prefix] += 1;
                slash = route.route_name.find('/', slash + 1);
            }
        }

        std::vector<std::string> roots;
        for (const auto& prefix : ordered_prefixes) {
            if (prefix_counts[prefix] > 1 && !route_names.contains(prefix)) {
                roots.push_back(prefix);
            }
        }
        return roots;
    }

    [[nodiscard]] std::vector<std::string> implicit_cli_targets() const {
        const auto roots = grouped_roots();
        std::vector<std::string> targets = roots;
        for (const auto& route : routes) {
            bool hidden_by_group = false;
            for (const auto& root : roots) {
                if (is_descendant(route.route_name, root)) {
                    hidden_by_group = true;
                    break;
                }
            }
            if (!hidden_by_group) {
                targets.push_back(route.route_name);
            }
        }
        return targets;
    }

    [[nodiscard]] std::set<std::string> explicit_cli_targets() const {
        std::set<std::string> targets;
        for (const auto& route : routes) {
            targets.insert(route.route_name);
        }
        for (const auto& root : grouped_roots()) {
            targets.insert(root);
        }
        return targets;
    }

    [[nodiscard]] std::string route_name_for_selector(
        const nlohmann::json& members_json,
        std::string_view selector) const {
        if (!members_json.is_array()) {
            return {};
        }
        for (const auto& member : members_json) {
            if (!member.is_object()) {
                continue;
            }
            if (member.value("selector", std::string{}) == selector) {
                return member.value("route", std::string{});
            }
        }
        return {};
    }

    [[nodiscard]] std::string fingerprint_for_source(
        std::int64_t session_id,
        std::string_view socket_path,
        const std::vector<ChannelPlan>& channels) const {
        std::string fingerprint =
            std::to_string(session_id) + "|" + std::string(socket_path);
        for (const auto& channel : channels) {
            fingerprint += "|" + channel.route_name + ":" + channel.channel_id;
        }
        return fingerprint;
    }

    [[nodiscard]] std::vector<SourcePlan> source_plans_from_json(
        const nlohmann::json& body) const {
        std::vector<SourcePlan> plans;
        if (!body.contains("sources") || !body.at("sources").is_array()) {
            return plans;
        }

        for (const auto& source_json : body.at("sources")) {
            SourcePlan plan;
            plan.source_id = source_json.value("source_id", std::int64_t{0});
            plan.target = source_json.value("target", std::string{});
            plan.state = source_json.value("state", std::string{});
            plan.session_id = source_json.value("active_session_id", std::int64_t{0});

            if (plan.state != "active" || plan.session_id <= 0 ||
                !source_json.contains("active_session") ||
                !source_json.at("active_session").is_object()) {
                plan.fingerprint = "inactive";
                plans.push_back(std::move(plan));
                continue;
            }

            const auto& active_session = source_json.at("active_session");
            if (!active_session.contains("serving_runtime") ||
                !active_session.at("serving_runtime").is_object()) {
                plan.fingerprint = "inactive";
                plans.push_back(std::move(plan));
                continue;
            }

            const auto& runtime = active_session.at("serving_runtime");
            plan.socket_path = runtime.value("ipc_socket_path", std::string{});
            if (plan.socket_path.empty() || !runtime.contains("ipc_channels") ||
                !runtime.at("ipc_channels").is_array()) {
                plan.fingerprint = "inactive";
                plans.push_back(std::move(plan));
                continue;
            }

            const bool grouped =
                source_json.contains("resolved_members_json") &&
                source_json.at("resolved_members_json").is_array() &&
                !source_json.at("resolved_members_json").empty();

            const auto& members_json =
                source_json.value("resolved_members_json", nlohmann::json::array());
            const auto& runtime_channels = runtime.at("ipc_channels");
            if (grouped) {
                for (const auto& channel_json : runtime_channels) {
                    ChannelPlan channel;
                    channel.channel_id = channel_json.value("channel_id", std::string{});
                    channel.stream_name =
                        channel_json.value("stream_name", std::string{});
                    channel.selector = channel_json.value("selector", std::string{});
                    channel.media_kind =
                        channel_json.value("media_kind", std::string{});
                    channel.delivered_caps_json =
                        channel_json.value("delivered_caps_json",
                                           nlohmann::json::object());
                    channel.route_name =
                        channel_json.value("route_name", std::string{});
                    if (channel.route_name.empty()) {
                        channel.route_name =
                            route_name_for_selector(members_json, channel.selector);
                    }
                    if (channel.route_name.empty() ||
                        !route_has_callbacks(channel.route_name)) {
                        continue;
                    }
                    channel.attach_name =
                        channel_json.value("route_name", std::string{});
                    if (channel.attach_name.empty()) {
                        channel.attach_name = channel.stream_name;
                    }
                    plan.channels.push_back(std::move(channel));
                }
            } else if (runtime_channels.size() == 1 &&
                       route_has_callbacks(plan.target)) {
                const auto& channel_json = runtime_channels.front();
                ChannelPlan channel;
                channel.route_name = plan.target;
                channel.attach_name = {};
                channel.channel_id = channel_json.value("channel_id", std::string{});
                channel.stream_name =
                    channel_json.value("stream_name", std::string{});
                channel.selector = channel_json.value("selector", std::string{});
                channel.media_kind =
                    channel_json.value("media_kind", std::string{});
                channel.delivered_caps_json =
                    channel_json.value("delivered_caps_json",
                                       nlohmann::json::object());
                plan.channels.push_back(std::move(channel));
            }

            plan.fingerprint =
                fingerprint_for_source(plan.session_id, plan.socket_path, plan.channels);
            plans.push_back(std::move(plan));
        }

        return plans;
    }

    void detach_source(std::int64_t source_id, bool emit_stop) {
        std::vector<std::string> doomed;
        for (const auto& [key, channel] : active_channels) {
            if (channel.source_id == source_id) {
                doomed.push_back(key);
            }
        }

        for (const auto& key : doomed) {
            auto it = active_channels.find(key);
            if (it == active_channels.end()) {
                continue;
            }
            if (emit_stop && !it->second.stop_emitted && it->second.callbacks.on_stop) {
                it->second.callbacks.on_stop();
                it->second.stop_emitted = true;
            }
            it->second.reader.reset();
            if (it->second.control_fd >= 0) {
                ::close(it->second.control_fd);
                it->second.control_fd = -1;
            }
            active_channels.erase(it);
        }
    }

    void detach_all(bool emit_stop) {
        std::vector<std::int64_t> source_ids;
        for (const auto& [_, channel] : active_channels) {
            source_ids.push_back(channel.source_id);
        }
        std::sort(source_ids.begin(), source_ids.end());
        source_ids.erase(std::unique(source_ids.begin(), source_ids.end()),
                         source_ids.end());
        for (const auto source_id : source_ids) {
            detach_source(source_id, emit_stop);
        }
    }

    bool attach_channel_for_plan(const SourcePlan& source, const ChannelPlan& channel) {
        auto connect_res =
            insightio::backend::ipc::connect_socket(source.socket_path);
        if (!connect_res.ok()) {
            set_last_error("failed to connect to IPC socket '" + source.socket_path +
                           "' for route '" + channel.route_name + "'");
            return false;
        }

        const int control_fd = connect_res.value();
        nlohmann::json request = {
            {"session_id", source.session_id},
        };
        if (!channel.attach_name.empty()) {
            request["stream_name"] = channel.attach_name;
        }
        auto send_res = insightio::backend::ipc::send_message(control_fd, request.dump(), {});
        if (!send_res.ok()) {
            ::close(control_fd);
            set_last_error("failed to send IPC attach request for route '" +
                           channel.route_name + "'");
            return false;
        }

        auto recv_res = insightio::backend::ipc::recv_message(control_fd, 4096, 2);
        if (!recv_res.ok()) {
            ::close(control_fd);
            set_last_error("failed to receive IPC attach response for route '" +
                           channel.route_name + "'");
            return false;
        }

        auto message = std::move(recv_res.value());
        nlohmann::json response = nlohmann::json::object();
        try {
            response = nlohmann::json::parse(message.payload);
        } catch (...) {
            for (const int fd : message.fds) {
                ::close(fd);
            }
            ::close(control_fd);
            set_last_error("IPC attach response was not valid JSON for route '" +
                           channel.route_name + "'");
            return false;
        }

        if (response.value("status", std::string{}) != "ok") {
            for (const int fd : message.fds) {
                ::close(fd);
            }
            ::close(control_fd);
            set_last_error("IPC attach rejected route '" + channel.route_name +
                           "': " + response.value("error", std::string{"unknown error"}));
            return false;
        }
        if (message.fds.size() < 2) {
            for (const int fd : message.fds) {
                ::close(fd);
            }
            ::close(control_fd);
            set_last_error("IPC attach for route '" + channel.route_name +
                           "' did not include memfd/eventfd");
            return false;
        }

        auto reader_res =
            insightio::backend::ipc::attach_reader(message.fds[0], message.fds[1]);
        if (!reader_res.ok()) {
            ::close(message.fds[0]);
            ::close(message.fds[1]);
            for (std::size_t index = 2; index < message.fds.size(); ++index) {
                ::close(message.fds[index]);
            }
            ::close(control_fd);
            set_last_error("failed to attach IPC reader for route '" +
                           channel.route_name + "'");
            return false;
        }

        for (std::size_t index = 2; index < message.fds.size(); ++index) {
            ::close(message.fds[index]);
        }

        nlohmann::json delivered_caps_json = channel.delivered_caps_json;
        if (response.contains("stream") && response.at("stream").is_object()) {
            const auto& stream = response.at("stream");
            delivered_caps_json =
                stream.value("delivered_caps_json", delivered_caps_json);
        }

        const auto* route = find_route(channel.route_name);
        if (route == nullptr) {
            ::close(control_fd);
            set_last_error("route '" + channel.route_name +
                           "' disappeared before IPC attach completed");
            return false;
        }

        ActiveChannel active;
        active.source_id = source.source_id;
        active.session_id = source.session_id;
        active.target = source.target;
        active.route_name = channel.route_name;
        active.fingerprint = source.fingerprint;
        active.channel_id =
            response.value("channel_id", channel.channel_id);
        active.control_fd = control_fd;
        active.reader = reader_res.value();
        active.callbacks = route->callbacks;
        active.caps = caps_from_json(channel.route_name,
                                     channel.media_kind,
                                     active.channel_id,
                                     channel.stream_name,
                                     channel.selector,
                                     delivered_caps_json,
                                     source.source_id,
                                     source.session_id);

        if (active.callbacks.on_caps) {
            active.callbacks.on_caps(active.caps);
        }

        active_channels[source_key(source.source_id, channel.route_name)] =
            std::move(active);
        return true;
    }

    bool attach_source(const SourcePlan& source) {
        if (source.state != "active" || source.channels.empty()) {
            return true;
        }
        for (const auto& channel : source.channels) {
            if (!attach_channel_for_plan(source, channel)) {
                detach_source(source.source_id, true);
                return false;
            }
        }
        return true;
    }

    bool refresh_sources() {
        const auto current_app_id = app_id();
        if (!current_app_id.has_value()) {
            set_last_error("cannot refresh sources before the app exists");
            return false;
        }

        const auto response = request_json(
            "GET", "/api/apps/" + std::to_string(*current_app_id) + "/sources");
        if (!response.ok) {
            set_last_error("failed to refresh app sources: " + response.error);
            return false;
        }

        const auto plans = source_plans_from_json(response.body);
        std::set<std::int64_t> desired_sources;
        for (const auto& plan : plans) {
            desired_sources.insert(plan.source_id);

            std::string existing_fingerprint;
            bool has_existing = false;
            for (const auto& [_, channel] : active_channels) {
                if (channel.source_id == plan.source_id) {
                    existing_fingerprint = channel.fingerprint;
                    has_existing = true;
                    break;
                }
            }

            if (plan.state != "active" || plan.channels.empty()) {
                if (has_existing) {
                    detach_source(plan.source_id, true);
                }
                continue;
            }

            if (has_existing && existing_fingerprint == plan.fingerprint) {
                continue;
            }

            if (has_existing) {
                detach_source(plan.source_id, true);
            }

            if (!attach_source(plan)) {
                return false;
            }
        }

        std::vector<std::int64_t> stale_sources;
        for (const auto& [_, channel] : active_channels) {
            if (!desired_sources.contains(channel.source_id)) {
                stale_sources.push_back(channel.source_id);
            }
        }
        std::sort(stale_sources.begin(), stale_sources.end());
        stale_sources.erase(std::unique(stale_sources.begin(), stale_sources.end()),
                            stale_sources.end());
        for (const auto source_id : stale_sources) {
            detach_source(source_id, true);
        }

        return true;
    }

    bool pump_frames_once(int timeout_ms) {
        if (active_channels.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            return true;
        }

        std::vector<pollfd> descriptors;
        std::vector<std::string> keys;
        descriptors.reserve(active_channels.size());
        keys.reserve(active_channels.size());
        for (const auto& [key, channel] : active_channels) {
            if (!channel.reader) {
                continue;
            }
            pollfd descriptor{};
            descriptor.fd = channel.reader->event_fd();
            descriptor.events = POLLIN;
            descriptors.push_back(descriptor);
            keys.push_back(key);
        }

        if (descriptors.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            return true;
        }

        const int rc = ::poll(descriptors.data(),
                              static_cast<nfds_t>(descriptors.size()),
                              timeout_ms);
        if (rc < 0) {
            if (errno == EINTR) {
                return true;
            }
            set_last_error(std::string("poll failed: ") + std::strerror(errno));
            return false;
        }
        if (rc == 0) {
            return true;
        }

        for (std::size_t index = 0; index < descriptors.size(); ++index) {
            if ((descriptors[index].revents & POLLIN) == 0) {
                continue;
            }

            const auto key = keys[index];
            auto it = active_channels.find(key);
            if (it == active_channels.end() || !it->second.reader) {
                continue;
            }

            it->second.reader->clear_event();
            while (true) {
                auto span = it->second.reader->read();
                if (!span.has_value()) {
                    break;
                }

                const auto now_steady = steady_now_ns();
                const auto now_wall = wall_now_ns();

                if ((span->flags & Frame::kFlagCapsChange) != 0 &&
                    it->second.callbacks.on_caps) {
                    it->second.callbacks.on_caps(it->second.caps);
                }

                Frame frame;
                frame.data = span->data;
                frame.size = span->size;
                frame.pts_ns = span->pts_ns;
                frame.dts_ns = span->dts_ns;
                frame.flags = span->flags;
                frame.sequence = ++it->second.sequence;
                frame.route_name = it->second.caps.route_name;
                frame.media_kind = it->second.caps.media_kind;
                frame.channel_id = it->second.caps.channel_id;
                frame.stream_name = it->second.caps.stream_name;
                frame.selector = it->second.caps.selector;
                frame.format = it->second.caps.format;
                frame.width = it->second.caps.width;
                frame.height = it->second.caps.height;
                frame.fps = it->second.caps.fps;
                frame.sample_rate = it->second.caps.sample_rate;
                frame.channels = it->second.caps.channels;
                frame.source_id = it->second.caps.source_id;
                frame.session_id = it->second.caps.session_id;
                frame.received_steady_ns = now_steady;
                frame.received_wall_ns = now_wall;

                if (it->second.callbacks.on_frame) {
                    it->second.callbacks.on_frame(frame);
                }
                if (frame.has_end_of_stream() && !it->second.stop_emitted &&
                    it->second.callbacks.on_stop) {
                    it->second.callbacks.on_stop();
                    it->second.stop_emitted = true;
                }
            }
        }

        return true;
    }

    bool apply_startup_binds() {
        bool had_failure = false;
        for (const auto& bind : startup_binds) {
            if (!create_or_queue_bind(bind)) {
                had_failure = true;
                if (options.fail_fast_startup) {
                    return false;
                }
            }
        }
        return !had_failure || !options.fail_fast_startup;
    }

    int run_loop() {
        auto next_refresh = std::chrono::steady_clock::now();
        while (!stop_requested.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_refresh) {
                if (!refresh_sources()) {
                    return 1;
                }
                next_refresh = now + std::chrono::milliseconds(options.refresh_interval_ms);
            }
            if (!pump_frames_once(50)) {
                return 1;
            }
        }
        return 0;
    }
};

Video::Video() {
    media = "video";
}

Video& Video::channel(std::string value) {
    channel_name = std::move(value);
    return *this;
}

Depth::Depth() {
    media = "depth";
}

Depth& Depth::channel(std::string value) {
    channel_name = std::move(value);
    return *this;
}

Audio::Audio() {
    media = "audio";
}

Audio& Audio::channel(std::string value) {
    channel_name = std::move(value);
    return *this;
}

bool Frame::has_end_of_stream() const {
    return (flags & kFlagEndOfStream) != 0;
}

App::AppRoute::AppRoute(App* app, std::size_t route_index)
    : app_(app),
      route_index_(route_index) {}

App::AppRoute& App::AppRoute::expect(const Expectation& expectation) {
    if (app_ && app_->impl_ && route_index_ < app_->impl_->routes.size()) {
        app_->impl_->routes[route_index_].expectation = expectation;
    }
    return *this;
}

App::AppRoute& App::AppRoute::on_caps(std::function<void(const Caps&)> callback) {
    if (app_ && app_->impl_ && route_index_ < app_->impl_->routes.size()) {
        app_->impl_->routes[route_index_].callbacks.on_caps = std::move(callback);
    }
    return *this;
}

App::AppRoute& App::AppRoute::on_frame(
    std::function<void(const Frame&)> callback) {
    if (app_ && app_->impl_ && route_index_ < app_->impl_->routes.size()) {
        app_->impl_->routes[route_index_].callbacks.on_frame = std::move(callback);
    }
    return *this;
}

App::AppRoute& App::AppRoute::on_stop(std::function<void()> callback) {
    if (app_ && app_->impl_ && route_index_ < app_->impl_->routes.size()) {
        app_->impl_->routes[route_index_].callbacks.on_stop = std::move(callback);
    }
    return *this;
}

App::App()
    : App(Options{}) {}

App::App(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

App::App(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

App::~App() = default;
App::App(App&&) noexcept = default;
App& App::operator=(App&&) noexcept = default;

App::AppRoute App::route(std::string route_name) {
    if (!impl_) {
        return AppRoute(this, 0);
    }

    if (const auto* existing = impl_->find_route(route_name); existing != nullptr) {
        const auto index = static_cast<std::size_t>(existing - impl_->routes.data());
        return AppRoute(this, index);
    }

    impl_->routes.push_back(RouteDeclaration{
        .route_name = std::move(route_name),
    });
    return AppRoute(this, impl_->routes.size() - 1);
}

App& App::bind_source(std::string target, std::string input, bool rtsp_enabled) {
    if (!impl_) {
        return *this;
    }
    (void)impl_->create_or_queue_bind(BindRequest{
        .target = std::move(target),
        .input = std::move(input),
        .session_id = std::nullopt,
        .rtsp_enabled = rtsp_enabled,
    });
    return *this;
}

App& App::bind_source(std::string target,
                      std::int64_t session_id,
                      bool rtsp_enabled) {
    if (!impl_) {
        return *this;
    }
    (void)impl_->create_or_queue_bind(BindRequest{
        .target = std::move(target),
        .input = std::nullopt,
        .session_id = session_id,
        .rtsp_enabled = rtsp_enabled,
    });
    return *this;
}

bool App::rebind(std::string target, std::string input, bool rtsp_enabled) {
    if (!impl_) {
        return false;
    }
    return impl_->rebind_target(BindRequest{
        .target = std::move(target),
        .input = std::move(input),
        .session_id = std::nullopt,
        .rtsp_enabled = rtsp_enabled,
    });
}

bool App::rebind(std::string target,
                 std::int64_t session_id,
                 bool rtsp_enabled) {
    if (!impl_) {
        return false;
    }
    return impl_->rebind_target(BindRequest{
        .target = std::move(target),
        .input = std::nullopt,
        .session_id = session_id,
        .rtsp_enabled = rtsp_enabled,
    });
}

bool App::bind_from_cli(int argc, char** argv, int start_index) {
    if (!impl_) {
        return false;
    }
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        impl_->program_name = argv[0];
    }

    const auto implicit_targets = impl_->implicit_cli_targets();
    const auto explicit_targets = impl_->explicit_cli_targets();
    std::set<std::string> seen_targets;

    for (int index = start_index; index < argc; ++index) {
        const std::string arg = argv[index];
        std::string target;
        std::string value;

        const auto equals = arg.find('=');
        if (equals == std::string::npos) {
            if (implicit_targets.size() != 1) {
                impl_->set_last_error(
                    "bare startup sources are allowed only when exactly one implicit target exists");
                return false;
            }
            target = implicit_targets.front();
            value = arg;
        } else {
            target = arg.substr(0, equals);
            value = arg.substr(equals + 1);
            if (!explicit_targets.contains(target)) {
                impl_->set_last_error("unknown startup target '" + target + "'");
                return false;
            }
        }

        if (!seen_targets.insert(target).second) {
            impl_->set_last_error("duplicate startup target '" + target + "'");
            return false;
        }

        if (const auto session_id = parse_session_binding(value); session_id.has_value()) {
            bind_source(target, *session_id);
        } else {
            bind_source(target, value);
        }
    }

    return true;
}

int App::connect() {
    if (!impl_) {
        return 1;
    }

    impl_->stop_requested.store(false);
    impl_->clear_last_error();
    impl_->detach_all(false);

    if (impl_->routes.empty()) {
        impl_->set_last_error("at least one route must be declared before connect()");
        return 1;
    }

    if (!impl_->create_app_record()) {
        impl_->detach_all(false);
        impl_->clear_app_identity();
        return 1;
    }

    int exit_code = 0;
    if (!impl_->declare_routes()) {
        exit_code = 1;
    } else if (!impl_->apply_startup_binds()) {
        exit_code = 1;
    } else {
        exit_code = impl_->run_loop();
    }

    impl_->detach_all(false);
    if (impl_->options.delete_on_shutdown) {
        impl_->delete_app_record();
    }
    impl_->clear_app_identity();
    return exit_code;
}

int App::run() {
    ScopedCliSignals signals;
    std::thread signal_thread([this, &signals]() {
        while (impl_ && !impl_->stop_requested.load()) {
            if (signals.stop_requested()) {
                request_stop();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    const int result = connect();
    request_stop();
    if (signal_thread.joinable()) {
        signal_thread.join();
    }
    return result;
}

int App::run(int argc, char** argv) {
    if (impl_ && argc > 0) {
        impl_->program_name = argv[0];
    }
    if (!bind_from_cli(argc, argv, 1)) {
        return 1;
    }
    return run();
}

void App::request_stop() {
    if (impl_) {
        impl_->stop_requested.store(true);
    }
}

std::optional<std::int64_t> App::app_id() const {
    if (!impl_) {
        return std::nullopt;
    }
    return impl_->app_id();
}

std::string App::app_name() const {
    if (!impl_) {
        return {};
    }
    return impl_->app_name();
}

std::string App::last_error() const {
    if (!impl_) {
        return {};
    }
    return impl_->last_error();
}

}  // namespace insightos
