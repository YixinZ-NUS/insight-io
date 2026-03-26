// role: persisted direct-session implementation for the standalone backend.
// revision: 2026-03-26 task6-serving-runtime-reuse
// major changes: resolves catalog URIs into durable sessions, normalizes
// persisted runtime state on startup, and provides session/status plus
// in-memory serving-runtime reuse inspection.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/session_service.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string_view>

namespace insightio::backend {

class ServingRuntimeRegistry {
public:
    void clear() {
        std::scoped_lock lock(mutex_);
        entries_.clear();
        session_to_stream_.clear();
    }

    SessionRecord::ServingRuntimeView attach(const SessionRecord& session) {
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
        return runtime_view_for(entry);
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
        return runtime_view_for(entry_it->second);
    }

    std::vector<ServingRuntimeSnapshot> snapshot() const {
        std::scoped_lock lock(mutex_);
        std::vector<ServingRuntimeSnapshot> snapshots;
        snapshots.reserve(entries_.size());
        for (const auto& [stream_id, entry] : entries_) {
            ServingRuntimeSnapshot snapshot;
            snapshot.runtime_key = entry.runtime_key;
            snapshot.stream_id = stream_id;
            snapshot.owner_session_id = entry.owner_session_id;
            snapshot.rtsp_enabled = effective_rtsp_locked(entry);
            snapshot.consumer_count = static_cast<int>(entry.consumer_rtsp.size());
            snapshot.source = entry.source;
            snapshot.resolved_members_json = entry.resolved_members_json;
            for (const auto& [session_id, _] : entry.consumer_rtsp) {
                snapshot.consumer_session_ids.push_back(session_id);
            }
            snapshots.push_back(std::move(snapshot));
        }
        return snapshots;
    }

private:
    struct Entry {
        std::string runtime_key;
        SessionResolvedSource source;
        nlohmann::json resolved_members_json;
        std::int64_t owner_session_id{0};
        std::map<std::int64_t, bool> consumer_rtsp;
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

    static SessionRecord::ServingRuntimeView runtime_view_for(const Entry& entry) {
        SessionRecord::ServingRuntimeView view;
        view.runtime_key = entry.runtime_key;
        view.owner_session_id = entry.owner_session_id;
        view.rtsp_enabled = effective_rtsp_locked(entry);
        view.consumer_count = static_cast<int>(entry.consumer_rtsp.size());
        view.shared = view.consumer_count > 1;
        for (const auto& [session_id, _] : entry.consumer_rtsp) {
            view.consumer_session_ids.push_back(session_id);
        }
        return view;
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
        entry.consumer_rtsp.erase(session_id);
        if (entry.consumer_rtsp.empty()) {
            entries_.erase(entry_it);
            return;
        }
        if (entry.owner_session_id == session_id) {
            entry.owner_session_id = entry.consumer_rtsp.begin()->first;
        }
    }

    mutable std::mutex mutex_;
    std::map<std::int64_t, Entry> entries_;
    std::map<std::int64_t, std::int64_t> session_to_stream_;
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
        "SELECT s.stream_id, d.device_key, d.public_name, s.selector, s.media_kind, "
        "s.shape_kind, s.channel, s.group_key, s.caps_json, s.capture_policy_json, "
        "s.members_json, s.publications_json "
        "FROM streams s "
        "JOIN devices d ON d.device_id = s.device_id "
        "WHERE d.public_name = ? AND s.selector = ? AND s.is_present = 1 "
        "AND d.status != 'offline'");
    if (!query) {
        error_status = 500;
        error_code = "internal";
        error_message = "Failed to prepare stream lookup";
        return std::nullopt;
    }

    query.bind_text(1, parsed.device);
    query.bind_text(2, parsed.selector);
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
    result.source.media_kind = query.col_text(4);
    result.source.shape_kind = query.col_text(5);
    result.source.channel = query.col_text(6);
    result.source.group_key = query.col_text(7);
    result.source.delivered_caps_json = parse_json(query.col_text(8));
    result.source.capture_policy_json = parse_json(query.col_text(9));
    result.source.members_json = parse_json(query.col_text(10));
    result.source.publications_json = parse_json(query.col_text(11));
    result.source.uri = derive_uri(uri_host,
                                   result.source.public_name,
                                   result.source.selector);
    return result;
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
    session.source.media_kind = query.col_text(15);
    session.source.shape_kind = query.col_text(16);
    session.source.channel = query.col_text(17);
    session.source.group_key = query.col_text(18);
    session.source.delivered_caps_json = parse_json(query.col_text(19));
    session.source.capture_policy_json = parse_json(query.col_text(20));
    session.source.members_json = parse_json(query.col_text(21));
    session.source.publications_json = parse_json(query.col_text(22));
    session.source.uri = derive_uri(uri_host,
                                    session.source.public_name,
                                    session.source.selector);
    if (session.rtsp_enabled) {
        session.rtsp_url = derive_rtsp_url(rtsp_host,
                                           session.source.public_name,
                                           session.source.selector);
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

}  // namespace

SessionService::SessionService(SchemaStore& store,
                               std::string uri_host,
                               std::string rtsp_host)
    : store_(store),
      uri_host_(std::move(uri_host)),
      rtsp_host_(std::move(rtsp_host)),
      runtime_registry_(std::make_unique<ServingRuntimeRegistry>()) {}

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
    return true;
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
        "COALESCE(s.selector, ''), COALESCE(s.media_kind, ''), COALESCE(s.shape_kind, ''), "
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
        "COALESCE(s.selector, ''), COALESCE(s.media_kind, ''), COALESCE(s.shape_kind, ''), "
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
    snapshot.total_serving_runtimes =
        static_cast<int>(snapshot.serving_runtimes.size());
    return snapshot;
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
        updated.serving_runtime = runtime_registry_->attach(updated);
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
