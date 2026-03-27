#pragma once

// role: durable app, route, and app-source service for the standalone backend.
// revision: 2026-03-27 task8-rtsp-runtime-validation
// major changes: keeps SQLite-backed app CRUD and source lifecycle handling
// while aligning default RTSP publication addresses with the live runtime
// contract.

#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace insightio::backend {

struct AppRecord {
    std::int64_t app_id{0};
    std::string name;
    std::string description;
    nlohmann::json config_json;
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

struct RouteRecord {
    std::int64_t route_id{0};
    std::int64_t app_id{0};
    std::string route_name;
    std::string target_resource_name;
    nlohmann::json expect_json;
    nlohmann::json config_json;
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

struct AppSourceRecord {
    std::int64_t source_id{0};
    std::int64_t app_id{0};
    std::int64_t route_id{0};
    std::int64_t stream_id{0};
    std::int64_t source_session_id{0};
    std::int64_t active_session_id{0};
    std::string target_name;
    std::string target_resource_name;
    bool rtsp_enabled{false};
    std::string state;
    std::string last_error;
    std::string rtsp_url;
    nlohmann::json resolved_members_json;
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
    SessionResolvedSource source;
    std::optional<SessionRecord> source_session;
    std::optional<SessionRecord> active_session;
};

class AppService {
public:
    AppService(SchemaStore& store,
               SessionService& sessions,
               std::string uri_host = "localhost",
               std::string rtsp_host = "127.0.0.1:8554");

    bool initialize();

    [[nodiscard]] std::vector<AppRecord> list_apps() const;
    [[nodiscard]] std::optional<AppRecord> get_app(std::int64_t app_id) const;

    bool create_app(const std::string& name,
                    const std::string& description,
                    const nlohmann::json& config_json,
                    AppRecord& created,
                    int& error_status,
                    std::string& error_code,
                    std::string& error_message) const;

    bool delete_app(std::int64_t app_id,
                    int& error_status,
                    std::string& error_code,
                    std::string& error_message) const;

    bool list_routes(std::int64_t app_id,
                     std::vector<RouteRecord>& routes,
                     int& error_status,
                     std::string& error_code,
                     std::string& error_message) const;

    bool create_route(std::int64_t app_id,
                      const std::string& route_name,
                      const nlohmann::json& expect_json,
                      const nlohmann::json& config_json,
                      RouteRecord& created,
                      int& error_status,
                      std::string& error_code,
                      std::string& error_message) const;

    bool delete_route(std::int64_t app_id,
                      const std::string& route_name,
                      int& error_status,
                      std::string& error_code,
                      std::string& error_message) const;

    bool list_sources(std::int64_t app_id,
                      std::vector<AppSourceRecord>& sources,
                      int& error_status,
                      std::string& error_code,
                      std::string& error_message) const;

    bool create_source(std::int64_t app_id,
                       const std::optional<std::string>& input,
                       const std::optional<std::int64_t>& session_id,
                       const std::string& target_name,
                       bool rtsp_enabled,
                       AppSourceRecord& created,
                       int& error_status,
                       std::string& error_code,
                       std::string& error_message) const;

    bool start_source(std::int64_t app_id,
                      std::int64_t source_id,
                      AppSourceRecord& updated,
                      int& error_status,
                      std::string& error_code,
                      std::string& error_message) const;

    bool stop_source(std::int64_t app_id,
                     std::int64_t source_id,
                     AppSourceRecord& updated,
                     int& error_status,
                     std::string& error_code,
                     std::string& error_message) const;

    bool rebind_source(std::int64_t app_id,
                       std::int64_t source_id,
                       const std::optional<std::string>& input,
                       const std::optional<std::int64_t>& session_id,
                       bool rtsp_enabled,
                       AppSourceRecord& updated,
                       int& error_status,
                       std::string& error_code,
                       std::string& error_message) const;

private:
    SchemaStore& store_;
    SessionService& sessions_;
    std::string uri_host_;
    std::string rtsp_host_;
};

}  // namespace insightio::backend
