#pragma once

/// InsightOS backend — SQLite device & session store.
///
/// Thin wrapper around a SQLite database that serves as the single source of
/// truth for the discovered device registry, public device ids, and logical
/// session records. Runtime-only state (capture workers, delivery sessions
/// owning OS resources) stays in C++ maps and is reconciled against the DB on
/// startup.
///
/// All public methods assume the caller already holds
/// SessionManager::mutex_.  The database is opened with
/// SQLITE_OPEN_NOMUTEX so SQLite itself does no locking.
///
/// Design source: docs/plan/standalone-project-plan.md

#include "insightos/backend/result.hpp"
#include "insightos/backend/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;  // forward-declare; avoids exposing <sqlite3.h> in public header

namespace insightos::backend {

// ─── Logical session row — flat representation stored in SQLite ──────────

struct LogicalSessionRow {
    std::string session_id;
    std::string state{"pending"};

    // Original request fields
    std::string request_name;
    std::string request_device_uuid;
    std::string request_preset_name;
    std::string request_delivery_name;
    std::string request_origin{"unknown"};
    std::string request_overrides_json{"{}"};

    // Resolved fields
    std::string device_uuid;
    std::string preset_id;
    std::string preset_name;
    std::string delivery_name;

    std::string host{"localhost"};
    std::string locality{"local"};
    std::int64_t started_at_ms{0};
    std::int64_t stopped_at_ms{0};
    std::string last_error;
};

using SessionRow = LogicalSessionRow;

struct SessionBindingRow {
    std::string session_id;
    std::string delivery_session_id;
};

struct DaemonRunRow {
    std::string daemon_run_id;
    std::string state{"active"};
    std::int64_t started_at_ms{0};
    std::int64_t ended_at_ms{0};
    std::int64_t pid{0};
    std::string version;
    std::int64_t last_heartbeat_at_ms{0};
};

struct CaptureSessionRow {
    std::string capture_session_id;
    std::string daemon_run_id;
    std::string preset_id;
    std::string capture_policy_key;
    std::string capture_policy_json{"{}"};
    std::string state{"pending"};
    std::int64_t started_at_ms{0};
    std::int64_t stopped_at_ms{0};
    std::string last_error;
};

struct DeliverySessionRow {
    std::string delivery_session_id;
    std::string capture_session_id;
    std::string stream_key;
    std::string delivery_name;
    std::string transport{"ipc"};
    std::string promised_format;
    std::string actual_format;
    std::string channel_id;
    std::string rtsp_url;
    std::string state{"pending"};
    std::int64_t started_at_ms{0};
    std::int64_t stopped_at_ms{0};
    std::string last_error;
};

struct AppRow {
    std::string app_id;
    std::string name;
    std::string description;
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

struct AppTargetRow {
    std::string target_id;
    std::string app_id;
    std::string target_name;
    std::string target_kind;
    std::string contract_json{"{}"};
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

struct AppSourceRow {
    std::string source_id;
    std::string app_id;
    std::string target_name;
    std::string input;
    std::string canonical_uri;
    std::string request_json{"{}"};
    std::string state{"stopped"};
    std::string last_error;
    std::string latest_session_id;
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

// ─── DeviceStore — SQLite-backed store ──────────────────────────────────

class DeviceStore {
public:
    explicit DeviceStore(std::string db_path);
    ~DeviceStore();

    DeviceStore(const DeviceStore&) = delete;
    DeviceStore& operator=(const DeviceStore&) = delete;

    /// Create or open the database and apply the schema.
    bool open();

    /// Close the database handle (idempotent).
    void close();

    const std::string& path() const { return db_path_; }

    /// Runtime-owned resources cannot survive process restart. Normalize any
    /// persisted sessions back to a stopped state and clear session bindings.
    void reset_runtime_state();

    // ── Device registry ─────────────────────────────────────────────────

    /// Full replace of the device table, preserving public_id values matched
    /// by device UUID and discovery order from the latest scan.
    bool replace_devices(const std::vector<DeviceInfo>& devices);

    /// Return all devices.
    std::vector<DeviceInfo> get_devices() const;

    /// Find a single device by its URI.
    std::optional<DeviceInfo> find_by_uri(std::string_view uri) const;

    /// Find a single device by its internal key.
    std::optional<DeviceInfo> find_by_key(std::string_view key) const;

    /// Find a single device by its stable UUID.
    std::optional<DeviceInfo> find_by_uuid(std::string_view uuid) const;

    /// Set the public device id for a device identified by internal key.
    Result<std::string> set_alias(std::string_view device_key,
                                  const std::string& alias);

    /// Restore the discovered default public device id for a device.
    Result<std::string> clear_alias(std::string_view device_key);

    /// Set the public stream name for one canonical stream id on a device.
    Result<std::string> set_stream_alias(std::string_view device_key,
                                         std::string_view stream_id,
                                         const std::string& alias);

    /// Restore the default public stream name for one canonical stream id.
    Result<std::string> clear_stream_alias(std::string_view device_key,
                                           std::string_view stream_id);

    // ── Session CRUD ────────────────────────────────────────────────────

    bool save_session(const SessionRow& row,
                      const std::vector<SessionBindingRow>& keys);
    std::optional<SessionRow> find_session(std::string_view session_id) const;
    std::vector<SessionRow> get_sessions() const;
    bool delete_session(std::string_view session_id);

    std::vector<SessionBindingRow> get_session_bindings(
        std::string_view session_id) const;

    bool start_daemon_run(const DaemonRunRow& row);
    bool finish_daemon_run(std::string_view daemon_run_id,
                           std::int64_t ended_at_ms);
    bool save_capture_session(const CaptureSessionRow& row);
    bool save_delivery_session(const DeliverySessionRow& row);
    bool set_capture_session_state(std::string_view capture_session_id,
                                   std::string_view state,
                                   std::int64_t stopped_at_ms = 0);
    bool set_delivery_session_state(std::string_view delivery_session_id,
                                    std::string_view state,
                                    std::int64_t stopped_at_ms = 0);
    std::optional<CaptureSessionRow> find_capture_session(
        std::string_view capture_session_id) const;
    std::optional<CaptureSessionRow> find_capture_session_by_reuse(
        std::string_view daemon_run_id, std::string_view preset_id,
        std::string_view capture_policy_key) const;
    std::vector<CaptureSessionRow> list_capture_sessions(
        std::string_view daemon_run_id) const;
    std::optional<DeliverySessionRow> find_delivery_session(
        std::string_view delivery_session_id) const;
    std::optional<DeliverySessionRow> find_delivery_session_by_reuse(
        std::string_view capture_session_id, std::string_view stream_key,
        std::string_view delivery_name, std::string_view transport) const;
    std::vector<DeliverySessionRow> list_delivery_sessions(
        std::string_view capture_session_id) const;

    bool save_app(const AppRow& row);
    std::optional<AppRow> find_app(std::string_view app_id) const;
    std::vector<AppRow> get_apps() const;
    bool delete_app(std::string_view app_id);

    bool save_app_target(const AppTargetRow& row);
    std::optional<AppTargetRow> find_app_target_by_name(
        std::string_view app_id, std::string_view target_name) const;
    std::vector<AppTargetRow> list_app_targets(std::string_view app_id) const;
    bool delete_app_target(std::string_view app_id,
                           std::string_view target_name);

    bool save_app_source(const AppSourceRow& row);
    std::optional<AppSourceRow> find_app_source(std::string_view source_id) const;
    std::vector<AppSourceRow> list_app_sources(std::string_view app_id) const;
    bool delete_app_source(std::string_view source_id);

private:
    std::string db_path_;
    sqlite3* db_{nullptr};

    /// Execute a SQL statement that returns no rows.
    bool exec(const char* sql) const;

    /// Apply the full DDL schema.
    bool create_schema();

    /// Read a single DeviceInfo from the devices/streams/caps tables.
    DeviceInfo read_device_row(const char* where_clause,
                               std::string_view bind_value) const;

    /// Helper: insert one DeviceInfo (caller wraps in transaction).
    bool insert_device(const DeviceInfo& dev, const std::string& uuid,
                       int discovery_order);
    bool replace_devices_internal(const std::vector<DeviceInfo>& devices,
                                  bool restore_saved_public_ids);

    bool upsert_session_row(const SessionRow& row);
    bool replace_session_bindings(std::string_view session_id,
                                  const std::vector<SessionBindingRow>& keys);
    bool upsert_app_row(const AppRow& row);
    bool upsert_app_target_row(const AppTargetRow& row);
    bool upsert_app_source_row(const AppSourceRow& row);
};

}  // namespace insightos::backend
