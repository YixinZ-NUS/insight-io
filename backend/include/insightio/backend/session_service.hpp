#pragma once

// role: persisted direct-session service for the standalone backend.
// revision: 2026-03-27 developer-rest-and-stream-aliases
// major changes: keeps direct-session runtime handling while exposing stream
// aliases as the canonical public URI segment for the thin developer-facing
// REST surface.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/schema_store.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace insightio::backend {

class ServingRuntimeRegistry;

struct SessionResolvedSource {
    std::int64_t stream_id{0};
    std::string device_key;
    std::string public_name;
    std::string selector;
    std::string stream_public_name;
    std::string stream_default_name;
    std::string uri;
    std::string media_kind;
    std::string shape_kind;
    std::string channel;
    std::string group_key;
    nlohmann::json delivered_caps_json;
    nlohmann::json capture_policy_json;
    nlohmann::json members_json;
    nlohmann::json publications_json;
};

struct IpcChannelRuntimeView {
    std::string channel_id;
    std::string stream_name;
    std::string route_name;
    std::string selector;
    std::string media_kind;
    nlohmann::json delivered_caps_json;
    int attached_consumer_count{0};
    std::uint64_t frames_published{0};
};

struct RtspPublicationRuntimeView {
    std::string publication_id;
    std::string stream_name;
    std::string selector;
    std::string url;
    std::string state;
    std::string publication_profile;
    std::string transport;
    std::string promised_format;
    std::string actual_format;
    std::string last_error;
    std::uint64_t frames_forwarded{0};
};

struct SessionRecord {
    std::int64_t session_id{0};
    std::string session_kind;
    bool rtsp_enabled{false};
    nlohmann::json request_json;
    nlohmann::json resolved_members_json;
    std::string state;
    std::string last_error;
    std::int64_t started_at_ms{0};
    std::int64_t stopped_at_ms{0};
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
    SessionResolvedSource source;
    std::string rtsp_url;
    struct ServingRuntimeView {
        std::string runtime_key;
        std::int64_t owner_session_id{0};
        std::string state;
        std::string last_error;
        bool rtsp_enabled{false};
        bool shared{false};
        int consumer_count{0};
        std::string ipc_socket_path;
        std::vector<IpcChannelRuntimeView> ipc_channels;
        std::optional<RtspPublicationRuntimeView> rtsp_publication;
        std::vector<std::int64_t> consumer_session_ids;
    };
    std::optional<ServingRuntimeView> serving_runtime;
};

struct ServingRuntimeSnapshot {
    std::string runtime_key;
    std::int64_t stream_id{0};
    std::int64_t owner_session_id{0};
    std::string state;
    std::string last_error;
    bool rtsp_enabled{false};
    int consumer_count{0};
    std::string ipc_socket_path;
    std::vector<IpcChannelRuntimeView> ipc_channels;
    std::optional<RtspPublicationRuntimeView> rtsp_publication;
    std::vector<std::int64_t> consumer_session_ids;
    SessionResolvedSource source;
    nlohmann::json resolved_members_json;
};

struct RuntimeStatusSnapshot {
    int total_sessions{0};
    int active_sessions{0};
    int stopped_sessions{0};
    int total_serving_runtimes{0};
    std::vector<SessionRecord> sessions;
    std::vector<ServingRuntimeSnapshot> serving_runtimes;
};

class SessionService {
public:
    SessionService(SchemaStore& store,
                   std::string uri_host = "localhost",
                   std::string rtsp_host = "127.0.0.1:8554");
    ~SessionService();

    bool initialize();

    [[nodiscard]] std::vector<SessionRecord> list_sessions() const;
    [[nodiscard]] std::optional<SessionRecord> get_session(std::int64_t session_id) const;
    [[nodiscard]] RuntimeStatusSnapshot runtime_status() const;
    [[nodiscard]] const std::string& ipc_socket_path() const;

    bool create_direct_session(const std::string& input,
                               bool rtsp_enabled,
                               SessionRecord& created,
                               int& error_status,
                               std::string& error_code,
                               std::string& error_message) const;

    bool start_session(std::int64_t session_id,
                       SessionRecord& updated,
                       int& error_status,
                       std::string& error_code,
                       std::string& error_message) const;

    bool stop_session(std::int64_t session_id,
                      SessionRecord& updated,
                      int& error_status,
                      std::string& error_code,
                      std::string& error_message) const;

    bool delete_session(std::int64_t session_id,
                        int& error_status,
                        std::string& error_code,
                        std::string& error_message) const;

    bool attach_session_runtime(std::int64_t session_id,
                                SessionRecord& updated,
                                int& error_status,
                                std::string& error_code,
                                std::string& error_message) const;

    bool ensure_session_rtsp(std::int64_t session_id,
                             SessionRecord& updated,
                             int& error_status,
                             std::string& error_code,
                             std::string& error_message) const;

private:
    [[nodiscard]] SessionRecord enrich_runtime(SessionRecord session) const;

    SchemaStore& store_;
    std::string uri_host_;
    std::string rtsp_host_;
    std::unique_ptr<ServingRuntimeRegistry> runtime_registry_;
};

}  // namespace insightio::backend
