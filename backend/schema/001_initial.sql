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
    cap_index     INTEGER NOT NULL,
    format        TEXT NOT NULL,
    width         INTEGER NOT NULL DEFAULT 0,
    height        INTEGER NOT NULL DEFAULT 0,
    fps           INTEGER NOT NULL DEFAULT 0,
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
    session_id             TEXT PRIMARY KEY,
    state                  TEXT NOT NULL DEFAULT 'pending',
    request_name           TEXT NOT NULL DEFAULT '',
    request_device_uuid    TEXT NOT NULL DEFAULT '',
    request_preset_name    TEXT NOT NULL DEFAULT '',
    request_delivery_name  TEXT NOT NULL DEFAULT '',
    request_origin         TEXT NOT NULL DEFAULT 'unknown',
    request_overrides_json TEXT NOT NULL DEFAULT '{}',
    device_uuid            TEXT NOT NULL DEFAULT '',
    preset_id              TEXT NOT NULL DEFAULT '',
    preset_name            TEXT NOT NULL DEFAULT '',
    delivery_name          TEXT NOT NULL DEFAULT '',
    host                   TEXT NOT NULL DEFAULT 'localhost',
    locality               TEXT NOT NULL DEFAULT 'local',
    started_at_ms          INTEGER NOT NULL DEFAULT 0,
    stopped_at_ms          INTEGER NOT NULL DEFAULT 0,
    last_error             TEXT NOT NULL DEFAULT ''
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
