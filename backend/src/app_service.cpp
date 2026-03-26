// role: durable app, route, and app-source implementation for the backend.
// revision: 2026-03-26 pr5-review-fixes
// major changes: cleans grouped source rows when one member route is deleted,
// removes app-owned grouped sessions that would retain stale member metadata,
// propagates app-delete session stop failures, and hardens post-insert reloads.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/app_service.hpp"
#include "insightio/backend/types.hpp"

#include <sqlite3.h>

#include <chrono>
#include <map>
#include <set>
#include <string_view>

namespace insightio::backend {

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

    void bind_int(int index, int value) { sqlite3_bind_int(stmt_, index, value); }
    void bind_int64(int index, std::int64_t value) { sqlite3_bind_int64(stmt_, index, value); }
    void bind_null(int index) { sqlite3_bind_null(stmt_, index); }

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

    std::int64_t col_int64(int index) const { return sqlite3_column_int64(stmt_, index); }

private:
    sqlite3_stmt* stmt_{nullptr};
};

struct ParsedInsightUri {
    std::string host;
    std::string device;
    std::string selector;
};

struct TargetResolution {
    bool is_exact{false};
    std::int64_t route_id{0};
    std::string route_name;
    std::string target_name;
    std::string target_resource_name;
    nlohmann::json expect_json;
    std::vector<RouteRecord> grouped_routes;
};

struct SourceSelection {
    SessionResolvedSource source;
    std::int64_t source_session_id{0};
    std::optional<SessionRecord> session;
};

struct GroupedSourceCleanup {
    std::int64_t source_id{0};
    std::int64_t source_session_id{0};
    std::int64_t active_session_id{0};
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
                       const std::string& selector) {
    if (public_name.empty() || selector.empty()) {
        return {};
    }
    return "insightos://" + uri_host + "/" + public_name + "/" + selector;
}

std::string derive_rtsp_url(const std::string& rtsp_host,
                            const std::string& public_name,
                            const std::string& selector) {
    if (public_name.empty() || selector.empty()) {
        return {};
    }
    return "rtsp://" + rtsp_host + "/" + public_name + "/" + selector;
}

bool starts_with_descendant(std::string_view value, std::string_view prefix) {
    return value.size() > prefix.size() &&
           value.substr(0, prefix.size()) == prefix &&
           value[prefix.size()] == '/';
}

bool parse_insight_uri(std::string_view input,
                       ParsedInsightUri& parsed,
                       std::string& error_message) {
    constexpr std::string_view prefix = "insightos://";
    if (!input.starts_with(prefix)) {
        error_message = "Input must use the insightos:// scheme";
        return false;
    }

    const auto remainder = input.substr(prefix.size());
    const auto first_slash = remainder.find('/');
    if (first_slash == std::string_view::npos || first_slash == 0) {
        error_message = "Input must include host, device, and selector";
        return false;
    }

    parsed.host = std::string(remainder.substr(0, first_slash));
    const auto device_and_selector = remainder.substr(first_slash + 1);
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

bool exec_sql(sqlite3* db, std::string_view sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db, std::string(sql).c_str(), nullptr, nullptr, &error);
    if (rc == SQLITE_OK) {
        return true;
    }
    sqlite3_free(error);
    return false;
}

bool begin_transaction(sqlite3* db) { return exec_sql(db, "BEGIN IMMEDIATE TRANSACTION;"); }
void rollback_transaction(sqlite3* db) { exec_sql(db, "ROLLBACK;"); }
bool commit_transaction(sqlite3* db) { return exec_sql(db, "COMMIT;"); }

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

std::optional<AppRecord> load_app(sqlite3* db, std::int64_t app_id) {
    Stmt query(
        db,
        "SELECT app_id, name, COALESCE(description, ''), COALESCE(config_json, '{}'), "
        "created_at_ms, updated_at_ms "
        "FROM apps WHERE app_id = ?");
    if (!query) {
        return std::nullopt;
    }
    query.bind_int64(1, app_id);
    if (!query.step()) {
        return std::nullopt;
    }

    AppRecord app;
    app.app_id = query.col_int64(0);
    app.name = query.col_text(1);
    app.description = query.col_text(2);
    app.config_json = parse_json(query.col_text(3));
    app.created_at_ms = query.col_int64(4);
    app.updated_at_ms = query.col_int64(5);
    return app;
}

std::vector<RouteRecord> load_routes_for_app(sqlite3* db, std::int64_t app_id) {
    std::vector<RouteRecord> routes;
    Stmt query(
        db,
        "SELECT route_id, app_id, route_name, COALESCE(expect_json, '{}'), "
        "COALESCE(config_json, '{}'), created_at_ms, updated_at_ms "
        "FROM app_routes WHERE app_id = ? ORDER BY route_name");
    if (!query) {
        return routes;
    }
    query.bind_int64(1, app_id);
    while (query.step()) {
        RouteRecord route;
        route.route_id = query.col_int64(0);
        route.app_id = query.col_int64(1);
        route.route_name = query.col_text(2);
        route.expect_json = parse_json(query.col_text(3));
        route.config_json = parse_json(query.col_text(4));
        route.created_at_ms = query.col_int64(5);
        route.updated_at_ms = query.col_int64(6);
        route.target_resource_name = "apps/" + std::to_string(route.app_id) +
                                     "/routes/" + route.route_name;
        routes.push_back(std::move(route));
    }
    return routes;
}

std::optional<RouteRecord> find_route_by_name(sqlite3* db,
                                              std::int64_t app_id,
                                              std::string_view route_name) {
    for (auto& route : load_routes_for_app(db, app_id)) {
        if (route.route_name == route_name) {
            return route;
        }
    }
    return std::nullopt;
}

bool route_name_is_ambiguous(sqlite3* db,
                             std::int64_t app_id,
                             std::string_view route_name,
                             std::int64_t ignored_route_id = 0) {
    for (const auto& route : load_routes_for_app(db, app_id)) {
        if (route.route_id == ignored_route_id) {
            continue;
        }
        if (route.route_name == route_name) {
            return true;
        }
        if (starts_with_descendant(route.route_name, route_name) ||
            starts_with_descendant(route_name, route.route_name)) {
            return true;
        }
    }
    return false;
}

std::optional<SessionResolvedSource> lookup_stream_by_selector(sqlite3* db,
                                                               std::string_view public_name,
                                                               std::string_view selector,
                                                               const std::string& uri_host) {
    Stmt query(
        db,
        "SELECT s.stream_id, d.device_key, d.public_name, s.selector, s.media_kind, "
        "s.shape_kind, COALESCE(s.channel, ''), COALESCE(s.group_key, ''), "
        "COALESCE(s.caps_json, '{}'), COALESCE(s.capture_policy_json, '{}'), "
        "COALESCE(s.members_json, '{}'), COALESCE(s.publications_json, '{}') "
        "FROM streams s "
        "JOIN devices d ON d.device_id = s.device_id "
        "WHERE d.public_name = ? AND s.selector = ? AND s.is_present = 1 "
        "AND d.status != 'offline'");
    if (!query) {
        return std::nullopt;
    }
    query.bind_text(1, public_name);
    query.bind_text(2, selector);
    if (!query.step()) {
        return std::nullopt;
    }

    SessionResolvedSource source;
    source.stream_id = query.col_int64(0);
    source.device_key = query.col_text(1);
    source.public_name = query.col_text(2);
    source.selector = query.col_text(3);
    source.media_kind = query.col_text(4);
    source.shape_kind = query.col_text(5);
    source.channel = query.col_text(6);
    source.group_key = query.col_text(7);
    source.delivered_caps_json = parse_json(query.col_text(8));
    source.capture_policy_json = parse_json(query.col_text(9));
    source.members_json = parse_json(query.col_text(10));
    source.publications_json = parse_json(query.col_text(11));
    source.uri = derive_uri(uri_host, source.public_name, source.selector);
    return source;
}

nlohmann::json source_members_json(const SourceSelection& selection) {
    if (selection.session.has_value() &&
        !selection.session->resolved_members_json.is_null() &&
        !selection.session->resolved_members_json.empty()) {
        return selection.session->resolved_members_json;
    }
    return selection.source.members_json;
}

bool media_matches_expectation(const nlohmann::json& expect_json,
                               std::string_view media_kind) {
    if (!expect_json.is_object() || !expect_json.contains("media")) {
        return true;
    }
    if (!expect_json.at("media").is_string()) {
        return false;
    }
    return expect_json.at("media").get<std::string>() == media_kind;
}

bool validate_exact_target(const TargetResolution& target,
                           const SourceSelection& selection,
                           int& error_status,
                           std::string& error_code,
                           std::string& error_message) {
    if (selection.source.shape_kind != "exact") {
        error_status = 422;
        error_code = "incompatible_source";
        error_message = "Grouped sources must bind through a grouped target root";
        return false;
    }
    if (!media_matches_expectation(target.expect_json, selection.source.media_kind)) {
        error_status = 422;
        error_code = "route_expectation_mismatch";
        error_message = "Resolved source media does not satisfy route expectation";
        return false;
    }
    return true;
}

bool build_grouped_resolution(const TargetResolution& target,
                              const SourceSelection& selection,
                              nlohmann::json& resolved_members_json,
                              int& error_status,
                              std::string& error_code,
                              std::string& error_message) {
    if (selection.source.shape_kind != "grouped") {
        error_status = 422;
        error_code = "incompatible_source";
        error_message = "Exact sources must bind to one declared exact route";
        return false;
    }

    const auto members = source_members_json(selection);
    if (!members.is_array() || members.empty()) {
        error_status = 422;
        error_code = "invalid_grouped_source";
        error_message = "Grouped source is missing fixed member metadata";
        return false;
    }

    std::map<std::string, nlohmann::json> members_by_route;
    for (const auto& member : members) {
        if (!member.is_object() || !member.contains("route") ||
            !member.at("route").is_string()) {
            error_status = 422;
            error_code = "invalid_grouped_source";
            error_message = "Grouped source member metadata is incomplete";
            return false;
        }
        members_by_route.emplace(member.at("route").get<std::string>(), member);
    }

    std::set<std::string> declared_route_names;
    for (const auto& route : target.grouped_routes) {
        declared_route_names.insert(route.route_name);
    }

    if (declared_route_names.size() != members_by_route.size()) {
        error_status = 422;
        error_code = "grouped_target_mismatch";
        error_message =
            "Grouped target routes do not match the selected grouped source members";
        return false;
    }

    resolved_members_json = nlohmann::json::array();
    for (const auto& route : target.grouped_routes) {
        const auto it = members_by_route.find(route.route_name);
        if (it == members_by_route.end()) {
            error_status = 422;
            error_code = "grouped_target_mismatch";
            error_message =
                "Grouped target routes do not match the selected grouped source members";
            return false;
        }

        const auto& member = it->second;
        const auto media = member.value("media", std::string{});
        if (!media_matches_expectation(route.expect_json, media)) {
            error_status = 422;
            error_code = "route_expectation_mismatch";
            error_message =
                "Grouped source member media does not satisfy route expectation";
            return false;
        }

        resolved_members_json.push_back({
            {"route", route.route_name},
            {"route_id", route.route_id},
            {"target_resource_name", route.target_resource_name},
            {"selector", member.value("selector", std::string{})},
            {"media", media},
        });
    }
    return true;
}

std::optional<TargetResolution> resolve_target(sqlite3* db,
                                               std::int64_t app_id,
                                               std::string_view target_name,
                                               int& error_status,
                                               std::string& error_code,
                                               std::string& error_message) {
    if (target_name.empty()) {
        error_status = 400;
        error_code = "bad_request";
        error_message = "Field 'target' must not be empty";
        return std::nullopt;
    }

    auto exact = find_route_by_name(db, app_id, target_name);
    if (exact.has_value()) {
        TargetResolution resolved;
        resolved.is_exact = true;
        resolved.route_id = exact->route_id;
        resolved.route_name = exact->route_name;
        resolved.target_name = std::string(target_name);
        resolved.target_resource_name = exact->target_resource_name;
        resolved.expect_json = exact->expect_json;
        return resolved;
    }

    TargetResolution grouped;
    grouped.target_name = std::string(target_name);
    for (const auto& route : load_routes_for_app(db, app_id)) {
        if (starts_with_descendant(route.route_name, target_name)) {
            grouped.grouped_routes.push_back(route);
        }
    }
    if (grouped.grouped_routes.empty()) {
        error_status = 404;
        error_code = "unknown_target";
        error_message = "Target '" + std::string(target_name) + "' is not declared on this app";
        return std::nullopt;
    }
    return grouped;
}

std::optional<SourceSelection> resolve_selection_from_input(sqlite3* db,
                                                            std::string_view input,
                                                            const std::string& uri_host,
                                                            int& error_status,
                                                            std::string& error_code,
                                                            std::string& error_message) {
    ParsedInsightUri parsed;
    if (!parse_insight_uri(input, parsed, error_message)) {
        error_status = 422;
        error_code = "invalid_input";
        return std::nullopt;
    }
    if (!validate_local_uri_host(parsed.host,
                                 uri_host,
                                 error_status,
                                 error_code,
                                 error_message)) {
        return std::nullopt;
    }

    auto source = lookup_stream_by_selector(db, parsed.device, parsed.selector, uri_host);
    if (!source.has_value()) {
        error_status = 404;
        error_code = "unknown_input";
        error_message = "Input URI does not match a catalog-published source";
        return std::nullopt;
    }

    SourceSelection selection;
    selection.source = std::move(*source);
    return selection;
}

std::optional<SourceSelection> resolve_selection_from_session(SessionService& sessions,
                                                              std::int64_t session_id,
                                                              int& error_status,
                                                              std::string& error_code,
                                                              std::string& error_message) {
    auto session = sessions.get_session(session_id);
    if (!session.has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "Session '" + std::to_string(session_id) + "' not found";
        return std::nullopt;
    }
    if (session->session_kind != "direct") {
        error_status = 422;
        error_code = "invalid_session";
        error_message = "Only direct sessions may be attached through session_id in this slice";
        return std::nullopt;
    }

    SourceSelection selection;
    selection.source = session->source;
    selection.source_session_id = session_id;
    selection.session = *session;
    return selection;
}

bool app_source_target_exists(sqlite3* db,
                              std::int64_t app_id,
                              std::string_view target_name,
                              std::int64_t ignored_source_id = 0) {
    Stmt query(
        db,
        "SELECT 1 FROM app_sources WHERE app_id = ? AND target_name = ? AND source_id != ?");
    if (!query) {
        return false;
    }
    query.bind_int64(1, app_id);
    query.bind_text(2, target_name);
    query.bind_int64(3, ignored_source_id);
    return query.step();
}

std::vector<std::int64_t> active_app_session_ids_for_app(sqlite3* db, std::int64_t app_id) {
    std::vector<std::int64_t> session_ids;
    Stmt query(
        db,
        "SELECT DISTINCT src.active_session_id "
        "FROM app_sources src "
        "JOIN sessions sess ON sess.session_id = src.active_session_id "
        "WHERE src.app_id = ? AND src.active_session_id IS NOT NULL "
        "AND sess.session_kind = 'app'");
    if (!query) {
        return session_ids;
    }
    query.bind_int64(1, app_id);
    while (query.step()) {
        session_ids.push_back(query.col_int64(0));
    }
    return session_ids;
}

std::vector<std::int64_t> active_app_session_ids_for_route(sqlite3* db,
                                                           std::int64_t app_id,
                                                           std::int64_t route_id) {
    std::vector<std::int64_t> session_ids;
    Stmt query(
        db,
        "SELECT DISTINCT src.active_session_id "
        "FROM app_sources src "
        "JOIN sessions sess ON sess.session_id = src.active_session_id "
        "WHERE src.app_id = ? AND src.route_id = ? AND src.active_session_id IS NOT NULL "
        "AND sess.session_kind = 'app'");
    if (!query) {
        return session_ids;
    }
    query.bind_int64(1, app_id);
    query.bind_int64(2, route_id);
    while (query.step()) {
        session_ids.push_back(query.col_int64(0));
    }
    return session_ids;
}

bool resolved_members_reference_route(const nlohmann::json& resolved_members_json,
                                      const RouteRecord& route) {
    if (!resolved_members_json.is_array()) {
        return false;
    }

    for (const auto& member : resolved_members_json) {
        if (!member.is_object()) {
            continue;
        }
        if (member.value("route_id", std::int64_t{0}) == route.route_id) {
            return true;
        }
        if (member.value("route", std::string{}) == route.route_name) {
            return true;
        }
        if (member.value("target_resource_name", std::string{}) ==
            route.target_resource_name) {
            return true;
        }
    }
    return false;
}

std::vector<GroupedSourceCleanup> grouped_sources_referencing_route(sqlite3* db,
                                                                    std::int64_t app_id,
                                                                    const RouteRecord& route) {
    std::vector<GroupedSourceCleanup> impacted;
    Stmt query(
        db,
        "SELECT source_id, COALESCE(source_session_id, 0), "
        "COALESCE(active_session_id, 0), COALESCE(resolved_routes_json, '{}') "
        "FROM app_sources WHERE app_id = ? AND route_id IS NULL");
    if (!query) {
        return impacted;
    }
    query.bind_int64(1, app_id);
    while (query.step()) {
        const auto resolved_members_json = parse_json(query.col_text(3));
        if (!resolved_members_reference_route(resolved_members_json, route)) {
            continue;
        }

        GroupedSourceCleanup cleanup;
        cleanup.source_id = query.col_int64(0);
        cleanup.source_session_id = query.col_int64(1);
        cleanup.active_session_id = query.col_int64(2);
        impacted.push_back(cleanup);
    }
    return impacted;
}

bool delete_app_source_row(sqlite3* db, std::int64_t app_id, std::int64_t source_id) {
    Stmt erase(
        db,
        "DELETE FROM app_sources WHERE app_id = ? AND source_id = ?");
    if (!erase) {
        return false;
    }
    erase.bind_int64(1, app_id);
    erase.bind_int64(2, source_id);
    return erase.exec();
}

bool delete_session_row(sqlite3* db, std::int64_t session_id) {
    Stmt erase(db, "DELETE FROM sessions WHERE session_id = ?");
    if (!erase) {
        return false;
    }
    erase.bind_int64(1, session_id);
    return erase.exec();
}

bool update_session_rtsp(sqlite3* db, std::int64_t session_id, bool rtsp_enabled) {
    Stmt statement(
        db,
        "UPDATE sessions SET rtsp_enabled = CASE WHEN ? THEN 1 ELSE rtsp_enabled END, "
        "updated_at_ms = ? WHERE session_id = ?");
    if (!statement) {
        return false;
    }
    statement.bind_int(1, rtsp_enabled ? 1 : 0);
    statement.bind_int64(2, now_ms());
    statement.bind_int64(3, session_id);
    return statement.exec();
}

std::int64_t insert_app_session(sqlite3* db,
                                std::int64_t app_id,
                                std::string_view target_name,
                                const SessionResolvedSource& source,
                                bool rtsp_enabled,
                                const nlohmann::json& resolved_members_json) {
    const auto timestamp = now_ms();
    const auto request_json = nlohmann::json{
        {"app_id", app_id},
        {"target", std::string(target_name)},
        {"input", source.uri},
        {"rtsp_enabled", rtsp_enabled},
    };

    Stmt insert(
        db,
        "INSERT INTO sessions (stream_id, session_kind, rtsp_enabled, request_json, "
        "resolved_members_json, state, last_error, started_at_ms, stopped_at_ms, "
        "created_at_ms, updated_at_ms) "
        "VALUES (?, 'app', ?, ?, ?, 'active', NULL, ?, NULL, ?, ?)");
    if (!insert) {
        return 0;
    }
    insert.bind_int64(1, source.stream_id);
    insert.bind_int(2, rtsp_enabled ? 1 : 0);
    insert.bind_text(3, request_json.dump());
    insert.bind_json_or_null(4, resolved_members_json);
    insert.bind_int64(5, timestamp);
    insert.bind_int64(6, timestamp);
    insert.bind_int64(7, timestamp);
    if (!insert.exec()) {
        return 0;
    }

    const auto session_id = sqlite3_last_insert_rowid(db);
    upsert_log(db,
               session_id,
               "app_source_create",
               "Created app-owned session for app-source bind",
               {
                   {"app_id", app_id},
                   {"target", std::string(target_name)},
                   {"stream_id", source.stream_id},
               });
    return session_id;
}

bool load_source_row(sqlite3* db,
                     SessionService& sessions,
                     std::int64_t app_id,
                     std::int64_t source_id,
                     const std::string& uri_host,
                     const std::string& rtsp_host,
                     AppSourceRecord& record) {
    Stmt query(
        db,
        "SELECT src.source_id, src.app_id, COALESCE(src.route_id, 0), src.stream_id, "
        "COALESCE(src.source_session_id, 0), COALESCE(src.active_session_id, 0), "
        "src.target_name, src.rtsp_enabled, src.state, "
        "COALESCE(src.resolved_routes_json, '{}'), COALESCE(src.last_error, ''), "
        "src.created_at_ms, src.updated_at_ms, COALESCE(r.route_name, ''), "
        "d.device_key, d.public_name, s.selector, s.media_kind, s.shape_kind, "
        "COALESCE(s.channel, ''), COALESCE(s.group_key, ''), "
        "COALESCE(s.caps_json, '{}'), COALESCE(s.capture_policy_json, '{}'), "
        "COALESCE(s.members_json, '{}'), COALESCE(s.publications_json, '{}') "
        "FROM app_sources src "
        "JOIN streams s ON s.stream_id = src.stream_id "
        "JOIN devices d ON d.device_id = s.device_id "
        "LEFT JOIN app_routes r ON r.app_id = src.app_id AND r.route_id = src.route_id "
        "WHERE src.app_id = ? AND src.source_id = ?");
    if (!query) {
        return false;
    }
    query.bind_int64(1, app_id);
    query.bind_int64(2, source_id);
    if (!query.step()) {
        return false;
    }

    record.source_id = query.col_int64(0);
    record.app_id = query.col_int64(1);
    record.route_id = query.col_int64(2);
    record.stream_id = query.col_int64(3);
    record.source_session_id = query.col_int64(4);
    record.active_session_id = query.col_int64(5);
    record.target_name = query.col_text(6);
    record.rtsp_enabled = query.col_int64(7) != 0;
    record.state = query.col_text(8);
    record.resolved_members_json = parse_json(query.col_text(9));
    record.last_error = query.col_text(10);
    record.created_at_ms = query.col_int64(11);
    record.updated_at_ms = query.col_int64(12);
    const auto route_name = query.col_text(13);
    record.target_resource_name =
        route_name.empty()
            ? std::string{}
            : "apps/" + std::to_string(record.app_id) + "/routes/" + route_name;

    record.source.stream_id = query.col_int64(3);
    record.source.device_key = query.col_text(14);
    record.source.public_name = query.col_text(15);
    record.source.selector = query.col_text(16);
    record.source.media_kind = query.col_text(17);
    record.source.shape_kind = query.col_text(18);
    record.source.channel = query.col_text(19);
    record.source.group_key = query.col_text(20);
    record.source.delivered_caps_json = parse_json(query.col_text(21));
    record.source.capture_policy_json = parse_json(query.col_text(22));
    record.source.members_json = parse_json(query.col_text(23));
    record.source.publications_json = parse_json(query.col_text(24));
    record.source.uri =
        derive_uri(uri_host, record.source.public_name, record.source.selector);

    if (record.source_session_id > 0) {
        record.source_session = sessions.get_session(record.source_session_id);
    }
    if (record.active_session_id > 0) {
        record.active_session = sessions.get_session(record.active_session_id);
    }
    if (record.rtsp_enabled && record.state == "active") {
        record.rtsp_url = derive_rtsp_url(rtsp_host,
                                          record.source.public_name,
                                          record.source.selector);
    }
    return true;
}

std::vector<AppSourceRecord> load_sources_for_app(sqlite3* db,
                                                  SessionService& sessions,
                                                  std::int64_t app_id,
                                                  const std::string& uri_host,
                                                  const std::string& rtsp_host) {
    std::vector<AppSourceRecord> sources;
    Stmt query(
        db,
        "SELECT source_id FROM app_sources WHERE app_id = ? ORDER BY source_id");
    if (!query) {
        return sources;
    }
    query.bind_int64(1, app_id);
    while (query.step()) {
        AppSourceRecord record;
        if (load_source_row(db,
                            sessions,
                            app_id,
                            query.col_int64(0),
                            uri_host,
                            rtsp_host,
                            record)) {
            sources.push_back(std::move(record));
        }
    }
    return sources;
}

}  // namespace

AppService::AppService(SchemaStore& store,
                       SessionService& sessions,
                       std::string uri_host,
                       std::string rtsp_host)
    : store_(store),
      sessions_(sessions),
      uri_host_(std::move(uri_host)),
      rtsp_host_(std::move(rtsp_host)) {}

bool AppService::initialize() {
    Stmt normalize(
        store_.db(),
        "UPDATE app_sources SET state = 'stopped', active_session_id = NULL, "
        "updated_at_ms = ? WHERE state = 'active' OR active_session_id IS NOT NULL");
    if (!normalize) {
        return false;
    }
    normalize.bind_int64(1, now_ms());
    return normalize.exec();
}

std::vector<AppRecord> AppService::list_apps() const {
    std::vector<AppRecord> apps;
    Stmt query(
        store_.db(),
        "SELECT app_id, name, COALESCE(description, ''), COALESCE(config_json, '{}'), "
        "created_at_ms, updated_at_ms "
        "FROM apps ORDER BY app_id");
    if (!query) {
        return apps;
    }
    while (query.step()) {
        AppRecord app;
        app.app_id = query.col_int64(0);
        app.name = query.col_text(1);
        app.description = query.col_text(2);
        app.config_json = parse_json(query.col_text(3));
        app.created_at_ms = query.col_int64(4);
        app.updated_at_ms = query.col_int64(5);
        apps.push_back(std::move(app));
    }
    return apps;
}

std::optional<AppRecord> AppService::get_app(std::int64_t app_id) const {
    return load_app(store_.db(), app_id);
}

bool AppService::create_app(const std::string& name,
                            const std::string& description,
                            const nlohmann::json& config_json,
                            AppRecord& created,
                            int& error_status,
                            std::string& error_code,
                            std::string& error_message) const {
    const auto normalized = slugify(name);
    if (normalized.empty()) {
        error_status = 422;
        error_code = "invalid_app_name";
        error_message = "App name must contain at least one alphanumeric character";
        return false;
    }

    Stmt conflict(store_.db(), "SELECT 1 FROM apps WHERE name = ?");
    if (!conflict) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare app conflict check";
        return false;
    }
    conflict.bind_text(1, normalized);
    if (conflict.step()) {
        error_status = 409;
        error_code = "conflict";
        error_message = "App '" + normalized + "' already exists";
        return false;
    }

    const auto timestamp = now_ms();
    Stmt insert(
        store_.db(),
        "INSERT INTO apps (name, description, config_json, created_at_ms, updated_at_ms) "
        "VALUES (?, ?, ?, ?, ?)");
    if (!insert) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare app insert";
        return false;
    }
    insert.bind_text(1, normalized);
    insert.bind_text_or_null(2, description);
    insert.bind_json_or_null(3, config_json);
    insert.bind_int64(4, timestamp);
    insert.bind_int64(5, timestamp);
    if (!insert.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to insert app";
        return false;
    }

    auto loaded = load_app(store_.db(), sqlite3_last_insert_rowid(store_.db()));
    if (!loaded.has_value()) {
        error_status = 500;
        error_code = "internal";
        error_message = "App insert succeeded but reload failed";
        return false;
    }

    created = std::move(*loaded);
    return true;
}

bool AppService::delete_app(std::int64_t app_id,
                            int& error_status,
                            std::string& error_code,
                            std::string& error_message) const {
    if (!load_app(store_.db(), app_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "App '" + std::to_string(app_id) + "' not found";
        return false;
    }

    for (const auto session_id : active_app_session_ids_for_app(store_.db(), app_id)) {
        SessionRecord ignored;
        int stop_status = 0;
        std::string stop_code;
        std::string stop_message;
        if (!sessions_.stop_session(session_id,
                                    ignored,
                                    stop_status,
                                    stop_code,
                                    stop_message)) {
            error_status = stop_status == 0 ? 500 : stop_status;
            error_code = stop_code.empty() ? "internal" : stop_code;
            error_message = stop_message.empty()
                                ? "Failed to stop app-owned session before deleting app"
                                : stop_message;
            return false;
        }
    }

    Stmt erase(store_.db(), "DELETE FROM apps WHERE app_id = ?");
    if (!erase) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare app delete";
        return false;
    }
    erase.bind_int64(1, app_id);
    if (!erase.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to delete app";
        return false;
    }
    return true;
}

bool AppService::list_routes(std::int64_t app_id,
                             std::vector<RouteRecord>& routes,
                             int& error_status,
                             std::string& error_code,
                             std::string& error_message) const {
    if (!load_app(store_.db(), app_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "App '" + std::to_string(app_id) + "' not found";
        return false;
    }
    routes = load_routes_for_app(store_.db(), app_id);
    return true;
}

bool AppService::create_route(std::int64_t app_id,
                              const std::string& route_name,
                              const nlohmann::json& expect_json,
                              const nlohmann::json& config_json,
                              RouteRecord& created,
                              int& error_status,
                              std::string& error_code,
                              std::string& error_message) const {
    if (!load_app(store_.db(), app_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "App '" + std::to_string(app_id) + "' not found";
        return false;
    }
    if (route_name.empty()) {
        error_status = 422;
        error_code = "invalid_route";
        error_message = "Route name must not be empty";
        return false;
    }
    if (route_name_is_ambiguous(store_.db(), app_id, route_name)) {
        error_status = 409;
        error_code = "ambiguous_route_target";
        error_message =
            "Route declaration would make the public target ambiguous";
        return false;
    }

    const auto timestamp = now_ms();
    Stmt insert(
        store_.db(),
        "INSERT INTO app_routes (app_id, route_name, expect_json, config_json, created_at_ms, updated_at_ms) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    if (!insert) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare route insert";
        return false;
    }
    insert.bind_int64(1, app_id);
    insert.bind_text(2, route_name);
    insert.bind_json_or_null(3, expect_json);
    insert.bind_json_or_null(4, config_json);
    insert.bind_int64(5, timestamp);
    insert.bind_int64(6, timestamp);
    if (!insert.exec()) {
        error_status = 409;
        error_code = "ambiguous_route_target";
        error_message =
            "Route declaration would make the public target ambiguous";
        return false;
    }

    auto loaded = find_route_by_name(store_.db(), app_id, route_name);
    if (!loaded.has_value()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Route insert succeeded but reload failed";
        return false;
    }

    created = std::move(*loaded);
    return true;
}

bool AppService::delete_route(std::int64_t app_id,
                              const std::string& route_name,
                              int& error_status,
                              std::string& error_code,
                              std::string& error_message) const {
    auto route = find_route_by_name(store_.db(), app_id, route_name);
    if (!route.has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message =
            "Route '" + route_name + "' not found on app '" + std::to_string(app_id) + "'";
        return false;
    }

    const auto exact_app_session_ids =
        active_app_session_ids_for_route(store_.db(), app_id, route->route_id);
    const auto grouped_sources =
        grouped_sources_referencing_route(store_.db(), app_id, *route);

    if (!begin_transaction(store_.db())) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to start route delete transaction";
        return false;
    }

    for (const auto session_id : exact_app_session_ids) {
        SessionRecord ignored;
        int stop_status = 0;
        std::string stop_code;
        std::string stop_message;
        if (!sessions_.stop_session(session_id,
                                    ignored,
                                    stop_status,
                                    stop_code,
                                    stop_message)) {
            rollback_transaction(store_.db());
            error_status = stop_status == 0 ? 500 : stop_status;
            error_code = stop_code.empty() ? "internal" : stop_code;
            error_message =
                stop_message.empty() ? "Failed to stop exact route session" : stop_message;
            return false;
        }
    }

    std::set<std::int64_t> grouped_app_session_ids;
    for (const auto& cleanup : grouped_sources) {
        if (cleanup.source_session_id == 0 && cleanup.active_session_id > 0) {
            grouped_app_session_ids.insert(cleanup.active_session_id);
        }
    }

    for (const auto session_id : grouped_app_session_ids) {
        SessionRecord ignored;
        int stop_status = 0;
        std::string stop_code;
        std::string stop_message;
        if (!sessions_.stop_session(session_id,
                                    ignored,
                                    stop_status,
                                    stop_code,
                                    stop_message)) {
            rollback_transaction(store_.db());
            error_status = stop_status == 0 ? 500 : stop_status;
            error_code = stop_code.empty() ? "internal" : stop_code;
            error_message =
                stop_message.empty() ? "Failed to stop grouped source session" : stop_message;
            return false;
        }
    }

    for (const auto& cleanup : grouped_sources) {
        if (!delete_app_source_row(store_.db(), app_id, cleanup.source_id)) {
            rollback_transaction(store_.db());
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to delete grouped app source during route cleanup";
            return false;
        }
    }

    for (const auto session_id : grouped_app_session_ids) {
        if (!delete_session_row(store_.db(), session_id)) {
            rollback_transaction(store_.db());
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to delete grouped app-owned session during route cleanup";
            return false;
        }
    }

    Stmt erase(
        store_.db(),
        "DELETE FROM app_routes WHERE app_id = ? AND route_name = ?");
    if (!erase) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare route delete";
        return false;
    }
    erase.bind_int64(1, app_id);
    erase.bind_text(2, route_name);
    if (!erase.exec()) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to delete route";
        return false;
    }

    if (!commit_transaction(store_.db())) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to commit route delete";
        return false;
    }
    return true;
}

bool AppService::list_sources(std::int64_t app_id,
                              std::vector<AppSourceRecord>& sources,
                              int& error_status,
                              std::string& error_code,
                              std::string& error_message) const {
    if (!load_app(store_.db(), app_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "App '" + std::to_string(app_id) + "' not found";
        return false;
    }
    sources = load_sources_for_app(store_.db(), sessions_, app_id, uri_host_, rtsp_host_);
    return true;
}

bool AppService::create_source(std::int64_t app_id,
                               const std::optional<std::string>& input,
                               const std::optional<std::int64_t>& session_id,
                               const std::string& target_name,
                               bool rtsp_enabled,
                               AppSourceRecord& created,
                               int& error_status,
                               std::string& error_code,
                               std::string& error_message) const {
    if ((input.has_value() && session_id.has_value()) ||
        (!input.has_value() && !session_id.has_value())) {
        error_status = 400;
        error_code = "bad_request";
        error_message = "Exactly one of 'input' or 'session_id' is required";
        return false;
    }
    if (!load_app(store_.db(), app_id).has_value()) {
        error_status = 404;
        error_code = "not_found";
        error_message = "App '" + std::to_string(app_id) + "' not found";
        return false;
    }
    if (app_source_target_exists(store_.db(), app_id, target_name)) {
        error_status = 409;
        error_code = "conflict";
        error_message = "Target '" + target_name + "' already has an active durable binding";
        return false;
    }

    auto target = resolve_target(store_.db(),
                                 app_id,
                                 target_name,
                                 error_status,
                                 error_code,
                                 error_message);
    if (!target.has_value()) {
        return false;
    }

    auto selection = input.has_value()
                         ? resolve_selection_from_input(store_.db(),
                                                        *input,
                                                        uri_host_,
                                                        error_status,
                                                        error_code,
                                                        error_message)
                         : resolve_selection_from_session(sessions_,
                                                          *session_id,
                                                          error_status,
                                                          error_code,
                                                          error_message);
    if (!selection.has_value()) {
        return false;
    }

    nlohmann::json resolved_members_json = nullptr;
    if (target->is_exact) {
        if (!validate_exact_target(*target,
                                   *selection,
                                   error_status,
                                   error_code,
                                   error_message)) {
            return false;
        }
    } else if (!build_grouped_resolution(*target,
                                         *selection,
                                         resolved_members_json,
                                         error_status,
                                         error_code,
                                         error_message)) {
        return false;
    }

    std::int64_t active_session_id = 0;
    if (selection->source_session_id > 0) {
        if (!selection->session.has_value()) {
            error_status = 404;
            error_code = "not_found";
            error_message = "Referenced session is missing";
            return false;
        }

        if (selection->session->state != "active") {
            SessionRecord started;
            if (!sessions_.start_session(selection->source_session_id,
                                         started,
                                         error_status,
                                         error_code,
                                         error_message)) {
                return false;
            }
            selection->session = started;
        }

        if (rtsp_enabled &&
            !update_session_rtsp(store_.db(), selection->source_session_id, true)) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to update referenced session RTSP state";
            return false;
        }
        active_session_id = selection->source_session_id;
    }

    if (!begin_transaction(store_.db())) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to start source transaction";
        return false;
    }

    if (selection->source_session_id == 0) {
        active_session_id = insert_app_session(store_.db(),
                                               app_id,
                                               target_name,
                                               selection->source,
                                               rtsp_enabled,
                                               resolved_members_json);
        if (active_session_id == 0) {
            rollback_transaction(store_.db());
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to create app-owned session";
            return false;
        }
    }

    const auto timestamp = now_ms();
    Stmt insert(
        store_.db(),
        "INSERT INTO app_sources (app_id, route_id, stream_id, source_session_id, active_session_id, "
        "target_name, rtsp_enabled, state, resolved_routes_json, last_error, created_at_ms, updated_at_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 'active', ?, NULL, ?, ?)");
    if (!insert) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare source insert";
        return false;
    }
    insert.bind_int64(1, app_id);
    if (target->is_exact) {
        insert.bind_int64(2, target->route_id);
    } else {
        insert.bind_null(2);
    }
    insert.bind_int64(3, selection->source.stream_id);
    if (selection->source_session_id > 0) {
        insert.bind_int64(4, selection->source_session_id);
    } else {
        insert.bind_null(4);
    }
    insert.bind_int64(5, active_session_id);
    insert.bind_text(6, target_name);
    insert.bind_int(7, rtsp_enabled ? 1 : 0);
    insert.bind_json_or_null(8, resolved_members_json);
    insert.bind_int64(9, timestamp);
    insert.bind_int64(10, timestamp);
    if (!insert.exec()) {
        rollback_transaction(store_.db());
        error_status = 409;
        error_code = "conflict";
        error_message = "Target '" + target_name + "' already has a durable binding";
        return false;
    }

    const auto source_id = sqlite3_last_insert_rowid(store_.db());
    if (!commit_transaction(store_.db())) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to commit source transaction";
        return false;
    }

    if (!load_source_row(store_.db(),
                         sessions_,
                         app_id,
                         source_id,
                         uri_host_,
                         rtsp_host_,
                         created)) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to load created app source";
        return false;
    }
    return true;
}

bool AppService::start_source(std::int64_t app_id,
                              std::int64_t source_id,
                              AppSourceRecord& updated,
                              int& error_status,
                              std::string& error_code,
                              std::string& error_message) const {
    AppSourceRecord existing;
    if (!load_source_row(store_.db(),
                         sessions_,
                         app_id,
                         source_id,
                         uri_host_,
                         rtsp_host_,
                         existing)) {
        error_status = 404;
        error_code = "not_found";
        error_message =
            "Source '" + std::to_string(source_id) + "' not found on this app";
        return false;
    }
    if (existing.state == "active" && existing.active_session_id > 0) {
        updated = std::move(existing);
        return true;
    }

    std::int64_t active_session_id = 0;
    if (existing.source_session_id > 0) {
        auto session = sessions_.get_session(existing.source_session_id);
        if (!session.has_value()) {
            error_status = 404;
            error_code = "not_found";
            error_message = "Referenced source session no longer exists";
            return false;
        }
        if (session->state != "active") {
            SessionRecord started;
            if (!sessions_.start_session(existing.source_session_id,
                                         started,
                                         error_status,
                                         error_code,
                                         error_message)) {
                return false;
            }
        }
        if (existing.rtsp_enabled &&
            !update_session_rtsp(store_.db(), existing.source_session_id, true)) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to update referenced session RTSP state";
            return false;
        }
        active_session_id = existing.source_session_id;
    } else {
        if (!begin_transaction(store_.db())) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to start source transaction";
            return false;
        }
        active_session_id = insert_app_session(store_.db(),
                                               app_id,
                                               existing.target_name,
                                               existing.source,
                                               existing.rtsp_enabled,
                                               existing.resolved_members_json);
        if (active_session_id == 0) {
            rollback_transaction(store_.db());
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to create app-owned session";
            return false;
        }
        if (!commit_transaction(store_.db())) {
            rollback_transaction(store_.db());
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to commit source session transaction";
            return false;
        }
    }

    Stmt update(
        store_.db(),
        "UPDATE app_sources SET active_session_id = ?, state = 'active', "
        "last_error = NULL, updated_at_ms = ? WHERE app_id = ? AND source_id = ?");
    if (!update) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare source start";
        return false;
    }
    update.bind_int64(1, active_session_id);
    update.bind_int64(2, now_ms());
    update.bind_int64(3, app_id);
    update.bind_int64(4, source_id);
    if (!update.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to update source state";
        return false;
    }

    return load_source_row(store_.db(),
                           sessions_,
                           app_id,
                           source_id,
                           uri_host_,
                           rtsp_host_,
                           updated);
}

bool AppService::stop_source(std::int64_t app_id,
                             std::int64_t source_id,
                             AppSourceRecord& updated,
                             int& error_status,
                             std::string& error_code,
                             std::string& error_message) const {
    AppSourceRecord existing;
    if (!load_source_row(store_.db(),
                         sessions_,
                         app_id,
                         source_id,
                         uri_host_,
                         rtsp_host_,
                         existing)) {
        error_status = 404;
        error_code = "not_found";
        error_message =
            "Source '" + std::to_string(source_id) + "' not found on this app";
        return false;
    }

    if (existing.source_session_id == 0 && existing.active_session_id > 0) {
        SessionRecord stopped;
        int stop_status = 0;
        std::string stop_code;
        std::string stop_message;
        if (!sessions_.stop_session(existing.active_session_id,
                                    stopped,
                                    stop_status,
                                    stop_code,
                                    stop_message)) {
            error_status = stop_status;
            error_code = stop_code;
            error_message = stop_message;
            return false;
        }
    }

    Stmt update(
        store_.db(),
        "UPDATE app_sources SET active_session_id = NULL, state = 'stopped', "
        "last_error = NULL, updated_at_ms = ? WHERE app_id = ? AND source_id = ?");
    if (!update) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare source stop";
        return false;
    }
    update.bind_int64(1, now_ms());
    update.bind_int64(2, app_id);
    update.bind_int64(3, source_id);
    if (!update.exec()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to update source state";
        return false;
    }

    return load_source_row(store_.db(),
                           sessions_,
                           app_id,
                           source_id,
                           uri_host_,
                           rtsp_host_,
                           updated);
}

bool AppService::rebind_source(std::int64_t app_id,
                               std::int64_t source_id,
                               const std::optional<std::string>& input,
                               const std::optional<std::int64_t>& session_id,
                               bool rtsp_enabled,
                               AppSourceRecord& updated,
                               int& error_status,
                               std::string& error_code,
                               std::string& error_message) const {
    if ((input.has_value() && session_id.has_value()) ||
        (!input.has_value() && !session_id.has_value())) {
        error_status = 400;
        error_code = "bad_request";
        error_message = "Exactly one of 'input' or 'session_id' is required";
        return false;
    }

    AppSourceRecord existing;
    if (!load_source_row(store_.db(),
                         sessions_,
                         app_id,
                         source_id,
                         uri_host_,
                         rtsp_host_,
                         existing)) {
        error_status = 404;
        error_code = "not_found";
        error_message =
            "Source '" + std::to_string(source_id) + "' not found on this app";
        return false;
    }

    auto target = resolve_target(store_.db(),
                                 app_id,
                                 existing.target_name,
                                 error_status,
                                 error_code,
                                 error_message);
    if (!target.has_value()) {
        return false;
    }

    auto selection = input.has_value()
                         ? resolve_selection_from_input(store_.db(),
                                                        *input,
                                                        uri_host_,
                                                        error_status,
                                                        error_code,
                                                        error_message)
                         : resolve_selection_from_session(sessions_,
                                                          *session_id,
                                                          error_status,
                                                          error_code,
                                                          error_message);
    if (!selection.has_value()) {
        return false;
    }

    nlohmann::json resolved_members_json = nullptr;
    if (target->is_exact) {
        if (!validate_exact_target(*target,
                                   *selection,
                                   error_status,
                                   error_code,
                                   error_message)) {
            return false;
        }
    } else if (!build_grouped_resolution(*target,
                                         *selection,
                                         resolved_members_json,
                                         error_status,
                                         error_code,
                                         error_message)) {
        return false;
    }

    const auto old_app_owned_session_id =
        existing.source_session_id == 0 ? existing.active_session_id : 0;

    std::int64_t new_active_session_id = 0;
    if (selection->source_session_id > 0) {
        if (!selection->session.has_value()) {
            error_status = 404;
            error_code = "not_found";
            error_message = "Referenced session is missing";
            return false;
        }
        if (selection->session->state != "active") {
            SessionRecord started;
            if (!sessions_.start_session(selection->source_session_id,
                                         started,
                                         error_status,
                                         error_code,
                                         error_message)) {
                return false;
            }
        }
        if (rtsp_enabled &&
            !update_session_rtsp(store_.db(), selection->source_session_id, true)) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to update referenced session RTSP state";
            return false;
        }
        new_active_session_id = selection->source_session_id;
    }

    if (!begin_transaction(store_.db())) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to start source transaction";
        return false;
    }

    if (selection->source_session_id == 0) {
        new_active_session_id = insert_app_session(store_.db(),
                                                   app_id,
                                                   existing.target_name,
                                                   selection->source,
                                                   rtsp_enabled,
                                                   resolved_members_json);
        if (new_active_session_id == 0) {
            rollback_transaction(store_.db());
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to create replacement app-owned session";
            return false;
        }
    }

    Stmt update(
        store_.db(),
        "UPDATE app_sources SET route_id = ?, stream_id = ?, source_session_id = ?, "
        "active_session_id = ?, rtsp_enabled = ?, state = 'active', "
        "resolved_routes_json = ?, last_error = NULL, updated_at_ms = ? "
        "WHERE app_id = ? AND source_id = ?");
    if (!update) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare source rebind";
        return false;
    }
    if (target->is_exact) {
        update.bind_int64(1, target->route_id);
    } else {
        update.bind_null(1);
    }
    update.bind_int64(2, selection->source.stream_id);
    if (selection->source_session_id > 0) {
        update.bind_int64(3, selection->source_session_id);
    } else {
        update.bind_null(3);
    }
    update.bind_int64(4, new_active_session_id);
    update.bind_int(5, rtsp_enabled ? 1 : 0);
    update.bind_json_or_null(6, resolved_members_json);
    update.bind_int64(7, now_ms());
    update.bind_int64(8, app_id);
    update.bind_int64(9, source_id);
    if (!update.exec()) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to update source binding";
        return false;
    }

    if (!commit_transaction(store_.db())) {
        rollback_transaction(store_.db());
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to commit source rebind";
        return false;
    }

    if (old_app_owned_session_id > 0 && old_app_owned_session_id != new_active_session_id) {
        SessionRecord stopped;
        int stop_status = 0;
        std::string stop_code;
        std::string stop_message;
        sessions_.stop_session(old_app_owned_session_id,
                               stopped,
                               stop_status,
                               stop_code,
                               stop_message);
    }

    return load_source_row(store_.db(),
                           sessions_,
                           app_id,
                           source_id,
                           uri_host_,
                           rtsp_host_,
                           updated);
}

}  // namespace insightio::backend
