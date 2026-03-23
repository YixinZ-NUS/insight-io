/// InsightOS backend — DeviceStore implementation.
///
/// Uses raw sqlite3_prepare_v2 / sqlite3_step / sqlite3_finalize to keep
/// the dependency surface minimal.  All multi-statement operations are
/// wrapped in explicit transactions.

#include "insightos/backend/device_store.hpp"
#include "insightos/backend/types.hpp"

#include <sqlite3.h>

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace insightos::backend {

// ─── Helpers ────────────────────────────────────────────────────────────

namespace {

/// RAII wrapper for a prepared statement.
class Stmt {
public:
    Stmt() = default;
    explicit Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            stmt_ = nullptr;
        }
    }
    ~Stmt() {
        if (stmt_) sqlite3_finalize(stmt_);
    }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt(Stmt&& o) noexcept : stmt_(o.stmt_) { o.stmt_ = nullptr; }
    Stmt& operator=(Stmt&& o) noexcept {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = o.stmt_;
        o.stmt_ = nullptr;
        return *this;
    }

    sqlite3_stmt* get() const { return stmt_; }
    explicit operator bool() const { return stmt_ != nullptr; }

    void bind_text(int idx, std::string_view val) {
        sqlite3_bind_text(stmt_, idx, val.data(),
                          static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }
    void bind_int(int idx, int val) {
        sqlite3_bind_int(stmt_, idx, val);
    }
    void bind_int64(int idx, std::int64_t val) {
        sqlite3_bind_int64(stmt_, idx, val);
    }

    bool step() { return sqlite3_step(stmt_) == SQLITE_ROW; }
    bool exec() {
        int rc = sqlite3_step(stmt_);
        return rc == SQLITE_DONE || rc == SQLITE_ROW;
    }

    std::string col_text(int idx) const {
        auto* p = sqlite3_column_text(stmt_, idx);
        return p ? std::string(reinterpret_cast<const char*>(p)) : std::string{};
    }
    int col_int(int idx) const {
        return sqlite3_column_int(stmt_, idx);
    }
    std::int64_t col_int64(int idx) const {
        return sqlite3_column_int64(stmt_, idx);
    }

    void reset() { sqlite3_reset(stmt_); }
    void clear_bindings() { sqlite3_clear_bindings(stmt_); }

private:
    sqlite3_stmt* stmt_{nullptr};
};

std::string data_kind_str(DataKind dk) {
    switch (dk) {
        case DataKind::kFrame:   return "frame";
        case DataKind::kMessage: return "message";
    }
    return "frame";
}

DataKind data_kind_from(std::string_view s) {
    if (s == "message") return DataKind::kMessage;
    return DataKind::kFrame;
}

std::string normalize_public_id(std::string_view value) {
    return slugify(value);
}

std::string unique_public_id(std::string base,
                             const std::set<std::string>& used_public_ids) {
    if (base.empty()) {
        base = "device";
    }
    if (!used_public_ids.count(base)) {
        return base;
    }
    for (int suffix = 1;; ++suffix) {
        auto candidate = base + "-" + std::to_string(suffix);
        if (!used_public_ids.count(candidate)) {
            return candidate;
        }
    }
}

std::string stream_key_for(std::string_view device_key,
                           std::string_view stream_id) {
    return std::string(device_key) + "::" + std::string(stream_id);
}

std::pair<std::string, std::string> normalized_stream_names(
    DeviceKind kind, const StreamInfo& stream) {
    auto stream_id = stream.stream_id.empty() ? stream.name : stream.stream_id;
    if (stream_id.empty()) {
        stream_id = "stream";
    }
    auto public_name = stream.name;
    if (public_name.empty() ||
        (stream.stream_id.empty() && public_name == stream_id)) {
        public_name = default_stream_name(kind, stream_id);
    }
    return {std::move(stream_id), std::move(public_name)};
}

struct StoredStreamNameRow {
    std::string default_name;
    std::string stream_name;
};

std::optional<StoredStreamNameRow> find_stored_stream_name_row(
    sqlite3* db, std::string_view device_key, std::string_view stream_id) {
    Stmt q(db,
           "SELECT default_name, stream_name "
           "FROM device_streams WHERE device_key = ? AND stream_id = ?");
    if (!q) {
        return std::nullopt;
    }
    q.bind_text(1, device_key);
    q.bind_text(2, stream_id);
    if (!q.step()) {
        return std::nullopt;
    }
    return StoredStreamNameRow{
        q.col_text(0),
        q.col_text(1),
    };
}

}  // namespace

// ─── Construction / destruction ─────────────────────────────────────────

DeviceStore::DeviceStore(std::string db_path)
    : db_path_(std::move(db_path)) {}
DeviceStore::~DeviceStore() { close(); }

// ─── open / close ───────────────────────────────────────────────────────

bool DeviceStore::open() {
    if (db_) return true;  // already open
    if (db_path_.empty()) {
        std::cerr << "[DeviceStore] database path is empty\n";
        return false;
    }

    std::error_code ec;
    auto parent = std::filesystem::path(db_path_).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            std::cerr << "[DeviceStore] failed to create parent directory '"
                      << parent.string() << "': " << ec.message() << "\n";
            return false;
        }
    }

    int rc = sqlite3_open_v2(db_path_.c_str(), &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                 SQLITE_OPEN_NOMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[DeviceStore] sqlite3_open failed (rc=" << rc << "): "
                  << (db_ ? sqlite3_errmsg(db_) : "unknown") << "\n";
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }
    exec("PRAGMA foreign_keys = ON");
    exec("PRAGMA busy_timeout = 5000");
    exec("PRAGMA journal_mode = WAL");
    exec("PRAGMA synchronous = NORMAL");
    if (!create_schema()) {
        close();
        return false;
    }
    return true;
}

void DeviceStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool DeviceStore::exec(const char* sql) const {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[DeviceStore] SQL error: " << (err ? err : "unknown")
                  << "\n  statement: " << sql << "\n";
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool DeviceStore::create_schema() {
    const char* ddl = R"SQL(
CREATE TABLE IF NOT EXISTS devices (
    device_key        TEXT PRIMARY KEY,
    device_uuid       TEXT NOT NULL UNIQUE,
    default_public_id TEXT NOT NULL DEFAULT '',
    public_id         TEXT NOT NULL UNIQUE,
    uri               TEXT NOT NULL UNIQUE,
    discovery_order   INTEGER NOT NULL DEFAULT 0,
    kind              TEXT NOT NULL,
    name              TEXT NOT NULL DEFAULT '',
    description       TEXT NOT NULL DEFAULT '',
    state             TEXT NOT NULL DEFAULT 'discovered',
    device_id         TEXT NOT NULL DEFAULT '',
    kind_str          TEXT NOT NULL DEFAULT '',
    hardware_name     TEXT NOT NULL DEFAULT '',
    persistent_key    TEXT NOT NULL DEFAULT '',
    usb_vendor_id     TEXT NOT NULL DEFAULT '',
    usb_product_id    TEXT NOT NULL DEFAULT '',
    usb_serial        TEXT NOT NULL DEFAULT '',
    device_uri        TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS device_streams (
    stream_key    TEXT PRIMARY KEY,
    device_key    TEXT NOT NULL REFERENCES devices(device_key) ON DELETE CASCADE,
    stream_id     TEXT NOT NULL,
    default_name  TEXT NOT NULL DEFAULT '',
    stream_name   TEXT NOT NULL,
    data_kind     TEXT NOT NULL DEFAULT 'frame',
    UNIQUE (device_key, stream_name)
);

CREATE TABLE IF NOT EXISTS stream_caps (
    stream_key    TEXT NOT NULL REFERENCES device_streams(stream_key) ON DELETE CASCADE,
    cap_index    INTEGER NOT NULL,
    format       TEXT NOT NULL,
    width        INTEGER NOT NULL DEFAULT 0,
    height       INTEGER NOT NULL DEFAULT 0,
    fps          INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (stream_key, cap_index)
);

CREATE TABLE IF NOT EXISTS delivery_options (
    device_key      TEXT NOT NULL REFERENCES devices(device_key) ON DELETE CASCADE,
    preset_name     TEXT NOT NULL,
    delivery_name   TEXT NOT NULL,
    PRIMARY KEY (device_key, preset_name, delivery_name)
);

CREATE TABLE IF NOT EXISTS daemon_runs (
    daemon_run_id         TEXT PRIMARY KEY,
    state                 TEXT NOT NULL DEFAULT 'active',
    started_at_ms         INTEGER NOT NULL,
    ended_at_ms           INTEGER NOT NULL DEFAULT 0,
    pid                   INTEGER NOT NULL DEFAULT 0,
    version               TEXT NOT NULL DEFAULT '',
    last_heartbeat_at_ms  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS logical_sessions (
    session_id          TEXT PRIMARY KEY,
    state               TEXT NOT NULL DEFAULT 'pending',
    request_name        TEXT NOT NULL DEFAULT '',
    request_device_uuid TEXT NOT NULL DEFAULT '',
    request_preset_name TEXT NOT NULL DEFAULT '',
    request_delivery_name TEXT NOT NULL DEFAULT '',
    request_origin      TEXT NOT NULL DEFAULT 'unknown',
    request_overrides_json TEXT NOT NULL DEFAULT '{}',
    device_uuid         TEXT NOT NULL DEFAULT '',
    preset_id           TEXT NOT NULL DEFAULT '',
    preset_name         TEXT NOT NULL DEFAULT '',
    delivery_name       TEXT NOT NULL DEFAULT '',
    host                TEXT NOT NULL DEFAULT 'localhost',
    locality            TEXT NOT NULL DEFAULT 'local',
    started_at_ms       INTEGER NOT NULL DEFAULT 0,
    stopped_at_ms       INTEGER NOT NULL DEFAULT 0,
    last_error          TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS capture_sessions (
    capture_session_id   TEXT PRIMARY KEY,
    daemon_run_id        TEXT NOT NULL REFERENCES daemon_runs(daemon_run_id) ON DELETE CASCADE,
    preset_id            TEXT NOT NULL,
    capture_policy_key   TEXT NOT NULL,
    capture_policy_json  TEXT NOT NULL DEFAULT '{}',
    state                TEXT NOT NULL DEFAULT 'pending',
    started_at_ms        INTEGER NOT NULL DEFAULT 0,
    stopped_at_ms        INTEGER NOT NULL DEFAULT 0,
    last_error           TEXT NOT NULL DEFAULT '',
    UNIQUE (daemon_run_id, preset_id, capture_policy_key)
);

CREATE TABLE IF NOT EXISTS delivery_sessions (
    delivery_session_id  TEXT PRIMARY KEY,
    capture_session_id   TEXT NOT NULL REFERENCES capture_sessions(capture_session_id) ON DELETE CASCADE,
    stream_key           TEXT NOT NULL,
    delivery_name        TEXT NOT NULL DEFAULT '',
    transport            TEXT NOT NULL DEFAULT 'ipc',
    promised_format      TEXT NOT NULL DEFAULT '',
    actual_format        TEXT NOT NULL DEFAULT '',
    channel_id           TEXT NOT NULL DEFAULT '',
    rtsp_url             TEXT NOT NULL DEFAULT '',
    state                TEXT NOT NULL DEFAULT 'pending',
    started_at_ms        INTEGER NOT NULL DEFAULT 0,
    stopped_at_ms        INTEGER NOT NULL DEFAULT 0,
    last_error           TEXT NOT NULL DEFAULT '',
    UNIQUE (capture_session_id, stream_key, delivery_name, transport)
);

CREATE TABLE IF NOT EXISTS session_bindings (
    session_id           TEXT NOT NULL REFERENCES logical_sessions(session_id) ON DELETE CASCADE,
    delivery_session_id  TEXT NOT NULL REFERENCES delivery_sessions(delivery_session_id) ON DELETE CASCADE,
    PRIMARY KEY (session_id, delivery_session_id)
);

CREATE TABLE IF NOT EXISTS apps (
    app_id           TEXT PRIMARY KEY,
    name             TEXT NOT NULL DEFAULT '',
    description      TEXT NOT NULL DEFAULT '',
    created_at_ms    INTEGER NOT NULL DEFAULT 0,
    updated_at_ms    INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS schema_migrations (
    version        TEXT PRIMARY KEY,
    applied_at_ms  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS app_targets (
    target_id        TEXT PRIMARY KEY,
    app_id           TEXT NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE,
    target_name      TEXT NOT NULL,
    target_kind      TEXT NOT NULL DEFAULT 'video',
    contract_json    TEXT NOT NULL DEFAULT '{}',
    created_at_ms    INTEGER NOT NULL DEFAULT 0,
    updated_at_ms    INTEGER NOT NULL DEFAULT 0,
    UNIQUE (app_id, target_name)
);

CREATE TABLE IF NOT EXISTS app_sources (
    source_id         TEXT PRIMARY KEY,
    app_id            TEXT NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE,
    target_name       TEXT NOT NULL,
    input             TEXT NOT NULL DEFAULT '',
    canonical_uri     TEXT NOT NULL DEFAULT '',
    request_json      TEXT NOT NULL DEFAULT '{}',
    state             TEXT NOT NULL DEFAULT 'stopped',
    last_error        TEXT NOT NULL DEFAULT '',
    latest_session_id TEXT NOT NULL DEFAULT '',
    created_at_ms     INTEGER NOT NULL DEFAULT 0,
    updated_at_ms     INTEGER NOT NULL DEFAULT 0,
    UNIQUE (app_id, canonical_uri),
    FOREIGN KEY (app_id, target_name)
        REFERENCES app_targets(app_id, target_name)
        ON DELETE CASCADE
);
)SQL";
    return exec(ddl);
}

// ─── Device registry ────────────────────────────────────────────────────

bool DeviceStore::insert_device(const DeviceInfo& dev, const std::string& uuid,
                                int discovery_order) {
    Stmt ins(db_,
        "INSERT INTO devices "
        "(device_key, device_uuid, default_public_id, public_id, uri, discovery_order, kind, "
        " name, description, state, "
        " device_id, kind_str, hardware_name, persistent_key, "
        " usb_vendor_id, usb_product_id, usb_serial, device_uri) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (!ins) return false;

    ins.bind_text(1,  dev.device_key);
    ins.bind_text(2,  uuid);
    ins.bind_text(3,  dev.default_public_id);
    ins.bind_text(4,  dev.public_id);
    ins.bind_text(5,  dev.uri);
    ins.bind_int(6, discovery_order);
    ins.bind_text(7,  to_string(dev.kind));
    ins.bind_text(8,  dev.name);
    ins.bind_text(9,  dev.description);
    ins.bind_text(10, to_string(dev.state));
    ins.bind_text(11, dev.identity.device_id);
    ins.bind_text(12, dev.identity.kind_str);
    ins.bind_text(13, dev.identity.hardware_name);
    ins.bind_text(14, dev.identity.persistent_key);
    ins.bind_text(15, dev.identity.usb_vendor_id);
    ins.bind_text(16, dev.identity.usb_product_id);
    ins.bind_text(17, dev.identity.usb_serial);
    ins.bind_text(18, dev.identity.device_uri);
    if (!ins.exec()) return false;

    // Insert streams and caps
    for (const auto& stream : dev.streams) {
        const auto [stream_id, stream_name] =
            normalized_stream_names(dev.kind, stream);
        const auto stream_key = stream_key_for(dev.device_key, stream_id);
        Stmt s_ins(db_,
            "INSERT INTO device_streams "
            "(stream_key, device_key, stream_id, default_name, stream_name, "
            " data_kind) "
            "VALUES (?,?,?,?,?,?)");
        if (!s_ins) return false;
        s_ins.bind_text(1, stream_key);
        s_ins.bind_text(2, dev.device_key);
        s_ins.bind_text(3, stream_id);
        s_ins.bind_text(4, default_stream_name(dev.kind, stream_id));
        s_ins.bind_text(5, stream_name);
        s_ins.bind_text(6, data_kind_str(stream.data_kind));
        if (!s_ins.exec()) return false;

        for (const auto& cap : stream.supported_caps) {
            Stmt c_ins(db_,
                "INSERT INTO stream_caps "
                "(stream_key, cap_index, format, width, height, fps) "
                "VALUES (?,?,?,?,?,?)");
            if (!c_ins) return false;
            c_ins.bind_text(1, stream_key);
            c_ins.bind_int(2, static_cast<int>(cap.index));
            c_ins.bind_text(3, cap.format);
            c_ins.bind_int(4, static_cast<int>(cap.width));
            c_ins.bind_int(5, static_cast<int>(cap.height));
            c_ins.bind_int(6, static_cast<int>(cap.fps));
            if (!c_ins.exec()) return false;
        }
    }

    return true;
}

bool DeviceStore::replace_devices(const std::vector<DeviceInfo>& devices) {
    return replace_devices_internal(devices, true);
}

bool DeviceStore::replace_devices_internal(
    const std::vector<DeviceInfo>& devices, bool restore_saved_public_ids) {
    struct SavedStreamState {
        std::string default_name;
        std::string stream_name;
    };

    struct SavedDeviceState {
        std::string device_key;
        std::string default_public_id;
        std::string public_id;
        std::map<std::string, SavedStreamState> streams;
    };

    struct NormalizedDeviceState {
        DeviceInfo device;
        std::string explicit_public_id;
    };

    // 1. Read existing device keys and public ids keyed by UUID.
    std::map<std::string, SavedDeviceState> saved_state;
    std::map<std::string, std::string> device_uuid_by_key;
    {
        Stmt q(db_, "SELECT device_uuid, device_key, default_public_id, public_id FROM devices");
        while (q && q.step()) {
            auto device_uuid = q.col_text(0);
            auto device_key = q.col_text(1);
            device_uuid_by_key[device_key] = device_uuid;
            saved_state[device_uuid] = SavedDeviceState{
                std::move(device_key),
                q.col_text(2),
                q.col_text(3),
                {},
            };
        }
    }
    {
        Stmt q(db_,
               "SELECT device_key, stream_id, default_name, stream_name "
               "FROM device_streams");
        while (q && q.step()) {
            const auto device_key = q.col_text(0);
            const auto uuid_it = device_uuid_by_key.find(device_key);
            if (uuid_it == device_uuid_by_key.end()) {
                continue;
            }
            saved_state[uuid_it->second].streams[q.col_text(1)] = SavedStreamState{
                q.col_text(2),
                q.col_text(3),
            };
        }
    }

    std::vector<NormalizedDeviceState> normalized;
    normalized.reserve(devices.size());

    std::set<std::string> used_default_ids;
    for (std::size_t index = 0; index < devices.size(); ++index) {
        DeviceInfo device = devices[index];
        const auto uuid = stable_device_uuid(device);
        const auto saved = saved_state.find(uuid);
        std::string explicit_public_id = normalize_public_id(device.public_id);

        if (saved != saved_state.end()) {
            device.device_key = saved->second.device_key;
        } else if (device.device_key.empty()) {
            device.device_key = stable_device_key(device);
        }

        auto default_public_id = normalize_public_id(device.default_public_id);
        if (default_public_id.empty()) {
            default_public_id = public_device_id_base(device, static_cast<int>(index));
        }
        device.default_public_id = unique_public_id(default_public_id, used_default_ids);
        used_default_ids.insert(device.default_public_id);
        device.public_id.clear();

        if (saved != saved_state.end() && restore_saved_public_ids &&
            !saved->second.public_id.empty() &&
            saved->second.public_id != saved->second.default_public_id) {
            explicit_public_id = normalize_public_id(saved->second.public_id);
        }

        if (saved != saved_state.end() && restore_saved_public_ids) {
            for (auto& stream : device.streams) {
                const auto stream_id =
                    stream.stream_id.empty() ? stream.name : stream.stream_id;
                const auto saved_stream = saved->second.streams.find(stream_id);
                if (saved_stream == saved->second.streams.end()) {
                    continue;
                }
                const auto saved_name =
                    normalize_public_id(saved_stream->second.stream_name);
                const auto saved_default_name =
                    normalize_public_id(saved_stream->second.default_name);
                if (!saved_name.empty() && saved_name != saved_default_name) {
                    stream.name = saved_name;
                }
            }
        }

        normalized.push_back({std::move(device), std::move(explicit_public_id)});
    }

    std::set<std::string> used_public_ids;
    for (auto& entry : normalized) {
        if (entry.explicit_public_id.empty() ||
            entry.explicit_public_id == entry.device.default_public_id) {
            continue;
        }
        entry.device.public_id =
            unique_public_id(entry.explicit_public_id, used_public_ids);
        used_public_ids.insert(entry.device.public_id);
    }

    for (auto& entry : normalized) {
        if (!entry.device.public_id.empty()) {
            continue;
        }
        entry.device.public_id =
            unique_public_id(entry.device.default_public_id, used_public_ids);
        used_public_ids.insert(entry.device.public_id);
    }

    if (!exec("BEGIN IMMEDIATE TRANSACTION")) return false;

    // 2. Delete all existing devices (cascades to streams and caps)
    if (!exec("DELETE FROM devices")) {
        exec("ROLLBACK");
        return false;
    }

    // 3. Re-insert normalized rows with stable internal keys and public ids.
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        const auto& device = normalized[index].device;
        const auto uuid = stable_device_uuid(device);
        if (!insert_device(device, uuid, static_cast<int>(index))) {
            exec("ROLLBACK");
            return false;
        }
    }

    return exec("COMMIT");
}

DeviceInfo DeviceStore::read_device_row(const char* where_clause,
                                        std::string_view bind_value) const {
    std::string sql =
        "SELECT device_key, default_public_id, public_id, uri, device_uuid, discovery_order, "
        "       kind, name, description, state, "
        "       device_id, kind_str, hardware_name, persistent_key, "
        "       usb_vendor_id, usb_product_id, usb_serial, device_uri "
        "FROM devices WHERE ";
    sql += where_clause;

    Stmt q(db_, sql.c_str());
    DeviceInfo dev{};
    if (!q) return dev;
    q.bind_text(1, bind_value);
    if (!q.step()) return dev;

    dev.device_key = q.col_text(0);
    dev.default_public_id = q.col_text(1);
    dev.public_id = q.col_text(2);
    dev.uri = q.col_text(3);
    auto kind_opt = device_kind_from_string(q.col_text(6));
    dev.kind = kind_opt.value_or(DeviceKind::kV4l2);
    dev.name = q.col_text(7);
    dev.description = q.col_text(8);
    // state
    auto state_str = q.col_text(9);
    if (state_str == "activating") dev.state = DeviceState::kActivating;
    else if (state_str == "active") dev.state = DeviceState::kActive;
    else if (state_str == "held") dev.state = DeviceState::kHeld;
    else if (state_str == "error") dev.state = DeviceState::kError;
    else dev.state = DeviceState::kDiscovered;

    dev.identity.device_id      = q.col_text(10);
    dev.identity.kind_str       = q.col_text(11);
    dev.identity.hardware_name  = q.col_text(12);
    dev.identity.persistent_key = q.col_text(13);
    dev.identity.usb_vendor_id  = q.col_text(14);
    dev.identity.usb_product_id = q.col_text(15);
    dev.identity.usb_serial     = q.col_text(16);
    dev.identity.device_uri     = q.col_text(17);

    // Load streams
    {
        Stmt sq(db_,
            "SELECT stream_key, stream_id, stream_name, data_kind "
            "FROM device_streams WHERE device_key = ? ORDER BY stream_id");
        if (sq) {
            sq.bind_text(1, dev.device_key);
            while (sq.step()) {
                StreamInfo si;
                const auto stream_key = sq.col_text(0);
                si.stream_id = sq.col_text(1);
                si.name = sq.col_text(2);
                si.data_kind = data_kind_from(sq.col_text(3));

                // Load caps for this stream
                Stmt cq(db_,
                    "SELECT cap_index, format, width, height, fps "
                    "FROM stream_caps WHERE stream_key = ? "
                    "ORDER BY cap_index");
                if (cq) {
                    cq.bind_text(1, stream_key);
                    while (cq.step()) {
                        ResolvedCaps cap;
                        cap.index  = static_cast<std::uint32_t>(cq.col_int(0));
                        cap.format = cq.col_text(1);
                        cap.width  = static_cast<std::uint32_t>(cq.col_int(2));
                        cap.height = static_cast<std::uint32_t>(cq.col_int(3));
                        cap.fps    = static_cast<std::uint32_t>(cq.col_int(4));
                        si.supported_caps.push_back(std::move(cap));
                    }
                }
                dev.streams.push_back(std::move(si));
            }
        }
    }

    return dev;
}

std::vector<DeviceInfo> DeviceStore::get_devices() const {
    std::vector<DeviceInfo> result;
    Stmt q(db_, "SELECT uri FROM devices ORDER BY discovery_order, uri");
    if (!q) return result;
    std::vector<std::string> uris;
    while (q.step()) {
        uris.push_back(q.col_text(0));
    }
    result.reserve(uris.size());
    for (const auto& uri : uris) {
        auto dev = read_device_row("uri = ?", uri);
        if (!dev.uri.empty()) {
            result.push_back(std::move(dev));
        }
    }
    return result;
}

std::optional<DeviceInfo> DeviceStore::find_by_uri(std::string_view uri) const {
    Stmt q(db_, "SELECT device_key FROM devices WHERE uri = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, uri);
    if (!q.step()) return std::nullopt;
    auto dev = read_device_row("uri = ?", uri);
    if (dev.uri.empty()) return std::nullopt;
    return dev;
}

std::optional<DeviceInfo> DeviceStore::find_by_key(std::string_view key) const {
    Stmt q(db_, "SELECT device_key FROM devices WHERE device_key = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, key);
    if (!q.step()) return std::nullopt;
    auto dev = read_device_row("device_key = ?", key);
    if (dev.device_key.empty()) return std::nullopt;
    return dev;
}

std::optional<DeviceInfo> DeviceStore::find_by_uuid(std::string_view uuid) const {
    Stmt q(db_, "SELECT device_key FROM devices WHERE device_uuid = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, uuid);
    if (!q.step()) return std::nullopt;
    auto dev = read_device_row("device_uuid = ?", uuid);
    if (dev.uri.empty()) return std::nullopt;
    return dev;
}

Result<std::string> DeviceStore::set_alias(std::string_view device_key,
                                           const std::string& alias) {
    auto public_id = normalize_public_id(alias);
    if (public_id.empty()) {
        return Result<std::string>::err(
            {"invalid_alias",
             "Alias must contain at least one alphanumeric character"});
    }

    Stmt find(db_, "SELECT 1 FROM devices WHERE device_key = ?");
    if (!find) {
        return Result<std::string>::err(
            {"internal", "Failed to query device store"});
    }
    find.bind_text(1, device_key);
    if (!find.step()) {
        return Result<std::string>::err(
            {"not_found",
             "Device with key '" + std::string(device_key) + "' not found"});
    }

    Stmt conflict(
        db_, "SELECT 1 FROM devices WHERE public_id = ? AND device_key != ?");
    if (!conflict) {
        return Result<std::string>::err(
            {"internal", "Failed to query device store"});
    }
    conflict.bind_text(1, public_id);
    conflict.bind_text(2, device_key);
    if (conflict.step()) {
        return Result<std::string>::err(
            {"conflict", "Device id '" + public_id + "' is already in use"});
    }

    Stmt update(db_, "UPDATE devices SET public_id = ? WHERE device_key = ?");
    if (!update) {
        return Result<std::string>::err(
            {"internal", "Failed to prepare alias update"});
    }
    update.bind_text(1, public_id);
    update.bind_text(2, device_key);
    if (!update.exec()) {
        return Result<std::string>::err(
            {"internal", "Failed to update device alias"});
    }
    return Result<std::string>::ok(std::move(public_id));
}

Result<std::string> DeviceStore::clear_alias(std::string_view device_key) {
    auto device = find_by_key(device_key);
    if (!device) {
        return Result<std::string>::err(
            {"not_found",
             "Device with key '" + std::string(device_key) + "' not found"});
    }

    std::set<std::string> used_public_ids;
    Stmt q(db_, "SELECT public_id FROM devices WHERE device_key != ?");
    if (!q) {
        return Result<std::string>::err(
            {"internal", "Failed to query device store"});
    }
    q.bind_text(1, device_key);
    while (q.step()) {
        used_public_ids.insert(q.col_text(0));
    }

    auto restored = unique_public_id(device->default_public_id, used_public_ids);
    Stmt update(db_, "UPDATE devices SET public_id = ? WHERE device_key = ?");
    if (!update) {
        return Result<std::string>::err(
            {"internal", "Failed to prepare alias reset"});
    }
    update.bind_text(1, restored);
    update.bind_text(2, device_key);
    if (!update.exec()) {
        return Result<std::string>::err(
            {"internal", "Failed to clear device alias"});
    }
    return Result<std::string>::ok(std::move(restored));
}

Result<std::string> DeviceStore::set_stream_alias(std::string_view device_key,
                                                  std::string_view stream_id,
                                                  const std::string& alias) {
    auto public_name = normalize_public_id(alias);
    if (public_name.empty()) {
        return Result<std::string>::err(
            {"invalid_alias",
             "Alias must contain at least one alphanumeric character"});
    }

    auto row = find_stored_stream_name_row(db_, device_key, stream_id);
    if (!row) {
        return Result<std::string>::err(
            {"not_found",
             "Stream '" + std::string(stream_id) + "' not found on device '" +
                 std::string(device_key) + "'"});
    }

    Stmt conflict(
        db_,
        "SELECT 1 FROM device_streams "
        "WHERE device_key = ? AND stream_name = ? AND stream_id != ?");
    if (!conflict) {
        return Result<std::string>::err(
            {"internal", "Failed to query device store"});
    }
    conflict.bind_text(1, device_key);
    conflict.bind_text(2, public_name);
    conflict.bind_text(3, stream_id);
    if (conflict.step()) {
        return Result<std::string>::err(
            {"conflict",
             "Stream name '" + public_name + "' is already in use on device"});
    }

    Stmt update(
        db_,
        "UPDATE device_streams SET stream_name = ? "
        "WHERE device_key = ? AND stream_id = ?");
    if (!update) {
        return Result<std::string>::err(
            {"internal", "Failed to prepare stream alias update"});
    }
    update.bind_text(1, public_name);
    update.bind_text(2, device_key);
    update.bind_text(3, stream_id);
    if (!update.exec()) {
        return Result<std::string>::err(
            {"internal", "Failed to update stream alias"});
    }
    return Result<std::string>::ok(std::move(public_name));
}

Result<std::string> DeviceStore::clear_stream_alias(std::string_view device_key,
                                                    std::string_view stream_id) {
    auto row = find_stored_stream_name_row(db_, device_key, stream_id);
    if (!row) {
        return Result<std::string>::err(
            {"not_found",
             "Stream '" + std::string(stream_id) + "' not found on device '" +
                 std::string(device_key) + "'"});
    }

    auto restored = normalize_public_id(row->default_name);
    if (restored.empty()) {
        restored = std::string(stream_id);
    }

    Stmt conflict(
        db_,
        "SELECT 1 FROM device_streams "
        "WHERE device_key = ? AND stream_name = ? AND stream_id != ?");
    if (!conflict) {
        return Result<std::string>::err(
            {"internal", "Failed to query device store"});
    }
    conflict.bind_text(1, device_key);
    conflict.bind_text(2, restored);
    conflict.bind_text(3, stream_id);
    if (conflict.step()) {
        return Result<std::string>::err(
            {"conflict",
             "Stream name '" + restored + "' is already in use on device"});
    }

    Stmt update(
        db_,
        "UPDATE device_streams SET stream_name = ? "
        "WHERE device_key = ? AND stream_id = ?");
    if (!update) {
        return Result<std::string>::err(
            {"internal", "Failed to prepare stream alias reset"});
    }
    update.bind_text(1, restored);
    update.bind_text(2, device_key);
    update.bind_text(3, stream_id);
    if (!update.exec()) {
        return Result<std::string>::err(
            {"internal", "Failed to clear stream alias"});
    }
    return Result<std::string>::ok(std::move(restored));
}

// ─── Session CRUD ───────────────────────────────────────────────────────

void DeviceStore::reset_runtime_state() {
    if (!exec("BEGIN IMMEDIATE TRANSACTION")) return;
    if (!exec(
            "UPDATE logical_sessions "
            "SET state = 'stopped', stopped_at_ms = CASE "
            "        WHEN stopped_at_ms != 0 THEN stopped_at_ms "
            "        WHEN started_at_ms != 0 THEN started_at_ms "
            "        ELSE 0 "
            "    END "
            "WHERE state IN ('active', 'starting', 'stopping')")) {
        exec("ROLLBACK");
        return;
    }
    if (!exec("DELETE FROM session_bindings")) {
        exec("ROLLBACK");
        return;
    }
    if (!exec(
            "UPDATE delivery_sessions "
            "SET state = 'stopped' "
            "WHERE state IN ('active', 'starting', 'stopping')")) {
        exec("ROLLBACK");
        return;
    }
    if (!exec(
            "UPDATE capture_sessions "
            "SET state = 'stopped' "
            "WHERE state IN ('active', 'starting', 'stopping')")) {
        exec("ROLLBACK");
        return;
    }
    if (!exec(
            "UPDATE daemon_runs "
            "SET state = 'stopped' "
            "WHERE state = 'active'")) {
        exec("ROLLBACK");
        return;
    }
    if (!exec(
            "UPDATE app_sources "
            "SET state = 'stopped' "
            "WHERE state IN ('active', 'starting', 'stopping', 'error')")) {
        exec("ROLLBACK");
        return;
    }
    exec("COMMIT");
}

bool DeviceStore::upsert_session_row(const SessionRow& row) {
    Stmt ins(db_,
        "INSERT INTO logical_sessions "
        "(session_id, state, request_name, request_device_uuid, "
        " request_preset_name, request_delivery_name, request_origin, "
        " request_overrides_json, device_uuid, preset_id, preset_name, "
        " delivery_name, host, locality, started_at_ms, stopped_at_ms, "
        " last_error) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(session_id) DO UPDATE SET "
        "state = excluded.state, "
        "request_name = excluded.request_name, "
        "request_device_uuid = excluded.request_device_uuid, "
        "request_preset_name = excluded.request_preset_name, "
        "request_delivery_name = excluded.request_delivery_name, "
        "request_origin = excluded.request_origin, "
        "request_overrides_json = excluded.request_overrides_json, "
        "device_uuid = excluded.device_uuid, "
        "preset_id = excluded.preset_id, "
        "preset_name = excluded.preset_name, "
        "delivery_name = excluded.delivery_name, "
        "host = excluded.host, "
        "locality = excluded.locality, "
        "started_at_ms = excluded.started_at_ms, "
        "stopped_at_ms = excluded.stopped_at_ms, "
        "last_error = excluded.last_error");
    if (!ins) return false;

    ins.bind_text(1,  row.session_id);
    ins.bind_text(2,  row.state);
    ins.bind_text(3,  row.request_name);
    ins.bind_text(4,  row.request_device_uuid);
    ins.bind_text(5,  row.request_preset_name);
    ins.bind_text(6,  row.request_delivery_name);
    ins.bind_text(7,  row.request_origin);
    ins.bind_text(8,  row.request_overrides_json);
    ins.bind_text(9,  row.device_uuid);
    ins.bind_text(10, row.preset_id);
    ins.bind_text(11, row.preset_name);
    ins.bind_text(12, row.delivery_name);
    ins.bind_text(13, row.host);
    ins.bind_text(14, row.locality);
    ins.bind_int64(15, row.started_at_ms);
    ins.bind_int64(16, row.stopped_at_ms);
    ins.bind_text(17, row.last_error);
    return ins.exec();
}

bool DeviceStore::replace_session_bindings(
    std::string_view session_id,
    const std::vector<SessionBindingRow>& keys) {
    Stmt del(db_,
        "DELETE FROM session_bindings WHERE session_id = ?");
    if (!del) return false;
    del.bind_text(1, session_id);
    if (!del.exec()) return false;

    Stmt ins(db_,
        "INSERT INTO session_bindings "
        "(session_id, delivery_session_id) "
        "VALUES (?,?)");
    if (!ins) return false;

    for (const auto& key : keys) {
        ins.bind_text(1, key.session_id);
        ins.bind_text(2, key.delivery_session_id);
        if (!ins.exec()) return false;
        ins.reset();
        ins.clear_bindings();
    }
    return true;
}

bool DeviceStore::save_session(
    const SessionRow& row,
    const std::vector<SessionBindingRow>& keys) {
    if (!exec("BEGIN IMMEDIATE TRANSACTION")) return false;
    if (!upsert_session_row(row) ||
        !replace_session_bindings(row.session_id, keys)) {
        exec("ROLLBACK");
        return false;
    }
    return exec("COMMIT");
}

std::optional<SessionRow> DeviceStore::find_session(
    std::string_view session_id) const {
    Stmt q(db_,
        "SELECT session_id, state, request_name, request_device_uuid, "
        "       request_preset_name, request_delivery_name, request_origin, "
        "       request_overrides_json, device_uuid, preset_id, preset_name, "
        "       delivery_name, host, locality, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM logical_sessions WHERE session_id = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, session_id);
    if (!q.step()) return std::nullopt;

    SessionRow r;
    r.session_id          = q.col_text(0);
    r.state               = q.col_text(1);
    r.request_name        = q.col_text(2);
    r.request_device_uuid = q.col_text(3);
    r.request_preset_name = q.col_text(4);
    r.request_delivery_name = q.col_text(5);
    r.request_origin      = q.col_text(6);
    r.request_overrides_json = q.col_text(7);
    r.device_uuid         = q.col_text(8);
    r.preset_id           = q.col_text(9);
    r.preset_name         = q.col_text(10);
    r.delivery_name       = q.col_text(11);
    r.host                = q.col_text(12);
    r.locality            = q.col_text(13);
    r.started_at_ms       = q.col_int64(14);
    r.stopped_at_ms       = q.col_int64(15);
    r.last_error          = q.col_text(16);
    return r;
}

std::vector<SessionRow> DeviceStore::get_sessions() const {
    std::vector<SessionRow> result;
    Stmt q(db_,
        "SELECT session_id, state, request_name, request_device_uuid, "
        "       request_preset_name, request_delivery_name, request_origin, "
        "       request_overrides_json, device_uuid, preset_id, preset_name, "
        "       delivery_name, host, locality, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM logical_sessions ORDER BY session_id");
    if (!q) return result;

    while (q.step()) {
        SessionRow r;
        r.session_id          = q.col_text(0);
        r.state               = q.col_text(1);
        r.request_name        = q.col_text(2);
        r.request_device_uuid = q.col_text(3);
        r.request_preset_name = q.col_text(4);
        r.request_delivery_name = q.col_text(5);
        r.request_origin      = q.col_text(6);
        r.request_overrides_json = q.col_text(7);
        r.device_uuid         = q.col_text(8);
        r.preset_id           = q.col_text(9);
        r.preset_name         = q.col_text(10);
        r.delivery_name       = q.col_text(11);
        r.host                = q.col_text(12);
        r.locality            = q.col_text(13);
        r.started_at_ms       = q.col_int64(14);
        r.stopped_at_ms       = q.col_int64(15);
        r.last_error          = q.col_text(16);
        result.push_back(std::move(r));
    }
    return result;
}

bool DeviceStore::delete_session(std::string_view session_id) {
    // Check existence first
    Stmt check(db_, "SELECT 1 FROM logical_sessions WHERE session_id = ?");
    if (!check) return false;
    check.bind_text(1, session_id);
    if (!check.step()) return false;

    Stmt del(db_, "DELETE FROM logical_sessions WHERE session_id = ?");
    if (!del) return false;
    del.bind_text(1, session_id);
    return del.exec();
}

bool DeviceStore::upsert_app_row(const AppRow& row) {
    Stmt ins(db_,
        "INSERT INTO apps "
        "(app_id, name, description, created_at_ms, updated_at_ms) "
        "VALUES (?,?,?,?,?) "
        "ON CONFLICT(app_id) DO UPDATE SET "
        "name = excluded.name, "
        "description = excluded.description, "
        "created_at_ms = excluded.created_at_ms, "
        "updated_at_ms = excluded.updated_at_ms");
    if (!ins) return false;
    ins.bind_text(1, row.app_id);
    ins.bind_text(2, row.name);
    ins.bind_text(3, row.description);
    ins.bind_int64(4, row.created_at_ms);
    ins.bind_int64(5, row.updated_at_ms);
    return ins.exec();
}

bool DeviceStore::save_app(const AppRow& row) {
    return upsert_app_row(row);
}

std::optional<AppRow> DeviceStore::find_app(std::string_view app_id) const {
    Stmt q(db_,
        "SELECT app_id, name, description, created_at_ms, updated_at_ms "
        "FROM apps WHERE app_id = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, app_id);
    if (!q.step()) return std::nullopt;

    AppRow row;
    row.app_id = q.col_text(0);
    row.name = q.col_text(1);
    row.description = q.col_text(2);
    row.created_at_ms = q.col_int64(3);
    row.updated_at_ms = q.col_int64(4);
    return row;
}

std::vector<AppRow> DeviceStore::get_apps() const {
    std::vector<AppRow> result;
    Stmt q(db_,
        "SELECT app_id, name, description, created_at_ms, updated_at_ms "
        "FROM apps ORDER BY app_id");
    if (!q) return result;
    while (q.step()) {
        AppRow row;
        row.app_id = q.col_text(0);
        row.name = q.col_text(1);
        row.description = q.col_text(2);
        row.created_at_ms = q.col_int64(3);
        row.updated_at_ms = q.col_int64(4);
        result.push_back(std::move(row));
    }
    return result;
}

bool DeviceStore::delete_app(std::string_view app_id) {
    Stmt del(db_, "DELETE FROM apps WHERE app_id = ?");
    if (!del) return false;
    del.bind_text(1, app_id);
    return del.exec();
}

bool DeviceStore::upsert_app_target_row(const AppTargetRow& row) {
    Stmt ins(db_,
        "INSERT INTO app_targets "
        "(target_id, app_id, target_name, target_kind, contract_json, "
        " created_at_ms, updated_at_ms) "
        "VALUES (?,?,?,?,?,?,?) "
        "ON CONFLICT(target_id) DO UPDATE SET "
        "app_id = excluded.app_id, "
        "target_name = excluded.target_name, "
        "target_kind = excluded.target_kind, "
        "contract_json = excluded.contract_json, "
        "created_at_ms = excluded.created_at_ms, "
        "updated_at_ms = excluded.updated_at_ms");
    if (!ins) return false;
    ins.bind_text(1, row.target_id);
    ins.bind_text(2, row.app_id);
    ins.bind_text(3, row.target_name);
    ins.bind_text(4, row.target_kind);
    ins.bind_text(5, row.contract_json);
    ins.bind_int64(6, row.created_at_ms);
    ins.bind_int64(7, row.updated_at_ms);
    return ins.exec();
}

bool DeviceStore::save_app_target(const AppTargetRow& row) {
    return upsert_app_target_row(row);
}

std::optional<AppTargetRow> DeviceStore::find_app_target_by_name(
    std::string_view app_id, std::string_view target_name) const {
    Stmt q(db_,
        "SELECT target_id, app_id, target_name, target_kind, contract_json, "
        "       created_at_ms, updated_at_ms "
        "FROM app_targets WHERE app_id = ? AND target_name = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, app_id);
    q.bind_text(2, target_name);
    if (!q.step()) return std::nullopt;

    AppTargetRow row;
    row.target_id = q.col_text(0);
    row.app_id = q.col_text(1);
    row.target_name = q.col_text(2);
    row.target_kind = q.col_text(3);
    row.contract_json = q.col_text(4);
    row.created_at_ms = q.col_int64(5);
    row.updated_at_ms = q.col_int64(6);
    return row;
}

std::vector<AppTargetRow> DeviceStore::list_app_targets(
    std::string_view app_id) const {
    std::vector<AppTargetRow> result;
    Stmt q(db_,
        "SELECT target_id, app_id, target_name, target_kind, contract_json, "
        "       created_at_ms, updated_at_ms "
        "FROM app_targets WHERE app_id = ? ORDER BY target_name");
    if (!q) return result;
    q.bind_text(1, app_id);
    while (q.step()) {
        AppTargetRow row;
        row.target_id = q.col_text(0);
        row.app_id = q.col_text(1);
        row.target_name = q.col_text(2);
        row.target_kind = q.col_text(3);
        row.contract_json = q.col_text(4);
        row.created_at_ms = q.col_int64(5);
        row.updated_at_ms = q.col_int64(6);
        result.push_back(std::move(row));
    }
    return result;
}

bool DeviceStore::delete_app_target(std::string_view app_id,
                                    std::string_view target_name) {
    Stmt del(db_,
        "DELETE FROM app_targets WHERE app_id = ? AND target_name = ?");
    if (!del) return false;
    del.bind_text(1, app_id);
    del.bind_text(2, target_name);
    return del.exec();
}

bool DeviceStore::upsert_app_source_row(const AppSourceRow& row) {
    Stmt ins(db_,
        "INSERT INTO app_sources "
        "(source_id, app_id, target_name, input, canonical_uri, request_json, "
        " state, last_error, latest_session_id, created_at_ms, updated_at_ms) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(source_id) DO UPDATE SET "
        "app_id = excluded.app_id, "
        "target_name = excluded.target_name, "
        "input = excluded.input, "
        "canonical_uri = excluded.canonical_uri, "
        "request_json = excluded.request_json, "
        "state = excluded.state, "
        "last_error = excluded.last_error, "
        "latest_session_id = excluded.latest_session_id, "
        "created_at_ms = excluded.created_at_ms, "
        "updated_at_ms = excluded.updated_at_ms");
    if (!ins) return false;
    ins.bind_text(1, row.source_id);
    ins.bind_text(2, row.app_id);
    ins.bind_text(3, row.target_name);
    ins.bind_text(4, row.input);
    ins.bind_text(5, row.canonical_uri);
    ins.bind_text(6, row.request_json);
    ins.bind_text(7, row.state);
    ins.bind_text(8, row.last_error);
    ins.bind_text(9, row.latest_session_id);
    ins.bind_int64(10, row.created_at_ms);
    ins.bind_int64(11, row.updated_at_ms);
    return ins.exec();
}

bool DeviceStore::save_app_source(const AppSourceRow& row) {
    return upsert_app_source_row(row);
}

std::optional<AppSourceRow> DeviceStore::find_app_source(
    std::string_view source_id) const {
    Stmt q(db_,
        "SELECT source_id, app_id, target_name, input, canonical_uri, "
        "       request_json, state, last_error, latest_session_id, "
        "       created_at_ms, updated_at_ms "
        "FROM app_sources WHERE source_id = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, source_id);
    if (!q.step()) return std::nullopt;

    AppSourceRow row;
    row.source_id = q.col_text(0);
    row.app_id = q.col_text(1);
    row.target_name = q.col_text(2);
    row.input = q.col_text(3);
    row.canonical_uri = q.col_text(4);
    row.request_json = q.col_text(5);
    row.state = q.col_text(6);
    row.last_error = q.col_text(7);
    row.latest_session_id = q.col_text(8);
    row.created_at_ms = q.col_int64(9);
    row.updated_at_ms = q.col_int64(10);
    return row;
}

std::vector<AppSourceRow> DeviceStore::list_app_sources(
    std::string_view app_id) const {
    std::vector<AppSourceRow> result;
    Stmt q(db_,
        "SELECT source_id, app_id, target_name, input, canonical_uri, "
        "       request_json, state, last_error, latest_session_id, "
        "       created_at_ms, updated_at_ms "
        "FROM app_sources WHERE app_id = ? ORDER BY source_id");
    if (!q) return result;
    q.bind_text(1, app_id);
    while (q.step()) {
        AppSourceRow row;
        row.source_id = q.col_text(0);
        row.app_id = q.col_text(1);
        row.target_name = q.col_text(2);
        row.input = q.col_text(3);
        row.canonical_uri = q.col_text(4);
        row.request_json = q.col_text(5);
        row.state = q.col_text(6);
        row.last_error = q.col_text(7);
        row.latest_session_id = q.col_text(8);
        row.created_at_ms = q.col_int64(9);
        row.updated_at_ms = q.col_int64(10);
        result.push_back(std::move(row));
    }
    return result;
}

bool DeviceStore::delete_app_source(std::string_view source_id) {
    Stmt del(db_, "DELETE FROM app_sources WHERE source_id = ?");
    if (!del) return false;
    del.bind_text(1, source_id);
    return del.exec();
}

std::vector<SessionBindingRow> DeviceStore::get_session_bindings(
    std::string_view session_id) const {
    std::vector<SessionBindingRow> result;
    Stmt q(db_,
        "SELECT session_id, delivery_session_id "
        "FROM session_bindings WHERE session_id = ? "
        "ORDER BY delivery_session_id");
    if (!q) return result;
    q.bind_text(1, session_id);

    while (q.step()) {
        SessionBindingRow r;
        r.session_id = q.col_text(0);
        r.delivery_session_id = q.col_text(1);
        result.push_back(std::move(r));
    }
    return result;
}

bool DeviceStore::start_daemon_run(const DaemonRunRow& row) {
    Stmt ins(db_,
        "INSERT INTO daemon_runs "
        "(daemon_run_id, state, started_at_ms, ended_at_ms, pid, version, "
        " last_heartbeat_at_ms) "
        "VALUES (?,?,?,?,?,?,?) "
        "ON CONFLICT(daemon_run_id) DO UPDATE SET "
        "state = excluded.state, "
        "started_at_ms = excluded.started_at_ms, "
        "ended_at_ms = excluded.ended_at_ms, "
        "pid = excluded.pid, "
        "version = excluded.version, "
        "last_heartbeat_at_ms = excluded.last_heartbeat_at_ms");
    if (!ins) return false;
    ins.bind_text(1, row.daemon_run_id);
    ins.bind_text(2, row.state);
    ins.bind_int64(3, row.started_at_ms);
    ins.bind_int64(4, row.ended_at_ms);
    ins.bind_int64(5, row.pid);
    ins.bind_text(6, row.version);
    ins.bind_int64(7, row.last_heartbeat_at_ms);
    return ins.exec();
}

bool DeviceStore::finish_daemon_run(std::string_view daemon_run_id,
                                    std::int64_t ended_at_ms) {
    Stmt upd(db_,
        "UPDATE daemon_runs "
        "SET state = 'stopped', ended_at_ms = ? "
        "WHERE daemon_run_id = ?");
    if (!upd) return false;
    upd.bind_int64(1, ended_at_ms);
    upd.bind_text(2, daemon_run_id);
    return upd.exec();
}

bool DeviceStore::save_capture_session(const CaptureSessionRow& row) {
    Stmt ins(db_,
        "INSERT INTO capture_sessions "
        "(capture_session_id, daemon_run_id, preset_id, capture_policy_key, "
        " capture_policy_json, state, started_at_ms, stopped_at_ms, last_error) "
        "VALUES (?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(capture_session_id) DO UPDATE SET "
        "daemon_run_id = excluded.daemon_run_id, "
        "preset_id = excluded.preset_id, "
        "capture_policy_key = excluded.capture_policy_key, "
        "capture_policy_json = excluded.capture_policy_json, "
        "state = excluded.state, "
        "started_at_ms = excluded.started_at_ms, "
        "stopped_at_ms = excluded.stopped_at_ms, "
        "last_error = excluded.last_error");
    if (!ins) return false;
    ins.bind_text(1, row.capture_session_id);
    ins.bind_text(2, row.daemon_run_id);
    ins.bind_text(3, row.preset_id);
    ins.bind_text(4, row.capture_policy_key);
    ins.bind_text(5, row.capture_policy_json);
    ins.bind_text(6, row.state);
    ins.bind_int64(7, row.started_at_ms);
    ins.bind_int64(8, row.stopped_at_ms);
    ins.bind_text(9, row.last_error);
    return ins.exec();
}

bool DeviceStore::save_delivery_session(const DeliverySessionRow& row) {
    Stmt ins(db_,
        "INSERT INTO delivery_sessions "
        "(delivery_session_id, capture_session_id, stream_key, delivery_name, "
        " transport, promised_format, actual_format, channel_id, rtsp_url, "
        " state, started_at_ms, stopped_at_ms, last_error) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(delivery_session_id) DO UPDATE SET "
        "capture_session_id = excluded.capture_session_id, "
        "stream_key = excluded.stream_key, "
        "delivery_name = excluded.delivery_name, "
        "transport = excluded.transport, "
        "promised_format = excluded.promised_format, "
        "actual_format = excluded.actual_format, "
        "channel_id = excluded.channel_id, "
        "rtsp_url = excluded.rtsp_url, "
        "state = excluded.state, "
        "started_at_ms = excluded.started_at_ms, "
        "stopped_at_ms = excluded.stopped_at_ms, "
        "last_error = excluded.last_error");
    if (!ins) return false;
    ins.bind_text(1, row.delivery_session_id);
    ins.bind_text(2, row.capture_session_id);
    ins.bind_text(3, row.stream_key);
    ins.bind_text(4, row.delivery_name);
    ins.bind_text(5, row.transport);
    ins.bind_text(6, row.promised_format);
    ins.bind_text(7, row.actual_format);
    ins.bind_text(8, row.channel_id);
    ins.bind_text(9, row.rtsp_url);
    ins.bind_text(10, row.state);
    ins.bind_int64(11, row.started_at_ms);
    ins.bind_int64(12, row.stopped_at_ms);
    ins.bind_text(13, row.last_error);
    return ins.exec();
}

bool DeviceStore::set_capture_session_state(std::string_view capture_session_id,
                                            std::string_view state,
                                            std::int64_t stopped_at_ms) {
    Stmt upd(db_,
        "UPDATE capture_sessions "
        "SET state = ?, stopped_at_ms = ? "
        "WHERE capture_session_id = ?");
    if (!upd) return false;
    upd.bind_text(1, state);
    upd.bind_int64(2, stopped_at_ms);
    upd.bind_text(3, capture_session_id);
    return upd.exec();
}

bool DeviceStore::set_delivery_session_state(std::string_view delivery_session_id,
                                             std::string_view state,
                                             std::int64_t stopped_at_ms) {
    Stmt upd(db_,
        "UPDATE delivery_sessions "
        "SET state = ?, stopped_at_ms = ? "
        "WHERE delivery_session_id = ?");
    if (!upd) return false;
    upd.bind_text(1, state);
    upd.bind_int64(2, stopped_at_ms);
    upd.bind_text(3, delivery_session_id);
    return upd.exec();
}

std::optional<CaptureSessionRow> DeviceStore::find_capture_session(
    std::string_view capture_session_id) const {
    Stmt q(db_,
        "SELECT capture_session_id, daemon_run_id, preset_id, capture_policy_key, "
        "       capture_policy_json, state, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM capture_sessions WHERE capture_session_id = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, capture_session_id);
    if (!q.step()) return std::nullopt;

    CaptureSessionRow row;
    row.capture_session_id = q.col_text(0);
    row.daemon_run_id = q.col_text(1);
    row.preset_id = q.col_text(2);
    row.capture_policy_key = q.col_text(3);
    row.capture_policy_json = q.col_text(4);
    row.state = q.col_text(5);
    row.started_at_ms = q.col_int64(6);
    row.stopped_at_ms = q.col_int64(7);
    row.last_error = q.col_text(8);
    return row;
}

std::optional<CaptureSessionRow> DeviceStore::find_capture_session_by_reuse(
    std::string_view daemon_run_id, std::string_view preset_id,
    std::string_view capture_policy_key) const {
    Stmt q(db_,
        "SELECT capture_session_id, daemon_run_id, preset_id, capture_policy_key, "
        "       capture_policy_json, state, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM capture_sessions "
        "WHERE daemon_run_id = ? AND preset_id = ? AND capture_policy_key = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, daemon_run_id);
    q.bind_text(2, preset_id);
    q.bind_text(3, capture_policy_key);
    if (!q.step()) return std::nullopt;

    CaptureSessionRow row;
    row.capture_session_id = q.col_text(0);
    row.daemon_run_id = q.col_text(1);
    row.preset_id = q.col_text(2);
    row.capture_policy_key = q.col_text(3);
    row.capture_policy_json = q.col_text(4);
    row.state = q.col_text(5);
    row.started_at_ms = q.col_int64(6);
    row.stopped_at_ms = q.col_int64(7);
    row.last_error = q.col_text(8);
    return row;
}

std::vector<CaptureSessionRow> DeviceStore::list_capture_sessions(
    std::string_view daemon_run_id) const {
    std::vector<CaptureSessionRow> result;
    Stmt q(db_,
        "SELECT capture_session_id, daemon_run_id, preset_id, capture_policy_key, "
        "       capture_policy_json, state, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM capture_sessions WHERE daemon_run_id = ? "
        "ORDER BY capture_session_id");
    if (!q) return result;
    q.bind_text(1, daemon_run_id);
    while (q.step()) {
        CaptureSessionRow row;
        row.capture_session_id = q.col_text(0);
        row.daemon_run_id = q.col_text(1);
        row.preset_id = q.col_text(2);
        row.capture_policy_key = q.col_text(3);
        row.capture_policy_json = q.col_text(4);
        row.state = q.col_text(5);
        row.started_at_ms = q.col_int64(6);
        row.stopped_at_ms = q.col_int64(7);
        row.last_error = q.col_text(8);
        result.push_back(std::move(row));
    }
    return result;
}

std::optional<DeliverySessionRow> DeviceStore::find_delivery_session(
    std::string_view delivery_session_id) const {
    Stmt q(db_,
        "SELECT delivery_session_id, capture_session_id, stream_key, "
        "       delivery_name, transport, promised_format, actual_format, "
        "       channel_id, rtsp_url, state, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM delivery_sessions WHERE delivery_session_id = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, delivery_session_id);
    if (!q.step()) return std::nullopt;

    DeliverySessionRow row;
    row.delivery_session_id = q.col_text(0);
    row.capture_session_id = q.col_text(1);
    row.stream_key = q.col_text(2);
    row.delivery_name = q.col_text(3);
    row.transport = q.col_text(4);
    row.promised_format = q.col_text(5);
    row.actual_format = q.col_text(6);
    row.channel_id = q.col_text(7);
    row.rtsp_url = q.col_text(8);
    row.state = q.col_text(9);
    row.started_at_ms = q.col_int64(10);
    row.stopped_at_ms = q.col_int64(11);
    row.last_error = q.col_text(12);
    return row;
}

std::optional<DeliverySessionRow> DeviceStore::find_delivery_session_by_reuse(
    std::string_view capture_session_id, std::string_view stream_key,
    std::string_view delivery_name, std::string_view transport) const {
    Stmt q(db_,
        "SELECT delivery_session_id, capture_session_id, stream_key, "
        "       delivery_name, transport, promised_format, actual_format, "
        "       channel_id, rtsp_url, state, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM delivery_sessions "
        "WHERE capture_session_id = ? AND stream_key = ? AND delivery_name = ? "
        "  AND transport = ?");
    if (!q) return std::nullopt;
    q.bind_text(1, capture_session_id);
    q.bind_text(2, stream_key);
    q.bind_text(3, delivery_name);
    q.bind_text(4, transport);
    if (!q.step()) return std::nullopt;

    DeliverySessionRow row;
    row.delivery_session_id = q.col_text(0);
    row.capture_session_id = q.col_text(1);
    row.stream_key = q.col_text(2);
    row.delivery_name = q.col_text(3);
    row.transport = q.col_text(4);
    row.promised_format = q.col_text(5);
    row.actual_format = q.col_text(6);
    row.channel_id = q.col_text(7);
    row.rtsp_url = q.col_text(8);
    row.state = q.col_text(9);
    row.started_at_ms = q.col_int64(10);
    row.stopped_at_ms = q.col_int64(11);
    row.last_error = q.col_text(12);
    return row;
}

std::vector<DeliverySessionRow> DeviceStore::list_delivery_sessions(
    std::string_view capture_session_id) const {
    std::vector<DeliverySessionRow> result;
    Stmt q(db_,
        "SELECT delivery_session_id, capture_session_id, stream_key, "
        "       delivery_name, transport, promised_format, actual_format, "
        "       channel_id, rtsp_url, state, started_at_ms, stopped_at_ms, "
        "       last_error "
        "FROM delivery_sessions WHERE capture_session_id = ? "
        "ORDER BY delivery_session_id");
    if (!q) return result;
    q.bind_text(1, capture_session_id);
    while (q.step()) {
        DeliverySessionRow row;
        row.delivery_session_id = q.col_text(0);
        row.capture_session_id = q.col_text(1);
        row.stream_key = q.col_text(2);
        row.delivery_name = q.col_text(3);
        row.transport = q.col_text(4);
        row.promised_format = q.col_text(5);
        row.actual_format = q.col_text(6);
        row.channel_id = q.col_text(7);
        row.rtsp_url = q.col_text(8);
        row.state = q.col_text(9);
        row.started_at_ms = q.col_int64(10);
        row.stopped_at_ms = q.col_int64(11);
        row.last_error = q.col_text(12);
        result.push_back(std::move(row));
    }
    return result;
}

}  // namespace insightos::backend
