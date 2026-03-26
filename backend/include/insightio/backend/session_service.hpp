#pragma once

// role: persisted direct-session service for the standalone backend.
// revision: 2026-03-26 direct-session-slice
// major changes: resolves catalog URIs into durable logical session records,
// normalizes persisted runtime state on startup, and exposes session/status
// read models for the REST layer.

#include "insightio/backend/schema_store.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace insightio::backend {

struct SessionResolvedSource {
    std::int64_t stream_id{0};
    std::string device_key;
    std::string public_name;
    std::string selector;
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
};

struct RuntimeStatusSnapshot {
    int total_sessions{0};
    int active_sessions{0};
    int stopped_sessions{0};
    std::vector<SessionRecord> sessions;
};

class SessionService {
public:
    SessionService(SchemaStore& store,
                   std::string uri_host = "localhost",
                   std::string rtsp_host = "127.0.0.1");

    bool initialize();

    [[nodiscard]] std::vector<SessionRecord> list_sessions() const;
    [[nodiscard]] std::optional<SessionRecord> get_session(std::int64_t session_id) const;
    [[nodiscard]] RuntimeStatusSnapshot runtime_status() const;

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

private:
    SchemaStore& store_;
    std::string uri_host_;
    std::string rtsp_host_;
};

}  // namespace insightio::backend
