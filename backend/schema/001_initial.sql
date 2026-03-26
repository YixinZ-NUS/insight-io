-- role: canonical v1 durable schema for the standalone insight-io rebuild.
-- revision: 2026-03-26 selector-schema-review
-- major changes: removes redundant streams.selector_key storage, scopes
-- selector uniqueness to each device, and keeps the seven-table v1 schema.
-- See docs/past-tasks.md for the implementation record.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS devices (
    device_id INTEGER PRIMARY KEY,
    device_key TEXT NOT NULL UNIQUE,
    public_name TEXT NOT NULL UNIQUE,
    driver TEXT NOT NULL,
    status TEXT NOT NULL,
    metadata_json TEXT,
    last_seen_at_ms INTEGER,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS streams (
    stream_id INTEGER PRIMARY KEY,
    device_id INTEGER NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    selector TEXT NOT NULL,
    media_kind TEXT NOT NULL,
    shape_kind TEXT NOT NULL CHECK (shape_kind IN ('exact', 'grouped')),
    channel TEXT,
    group_key TEXT,
    caps_json TEXT NOT NULL,
    capture_policy_json TEXT,
    members_json TEXT,
    publications_json TEXT NOT NULL,
    is_present INTEGER NOT NULL DEFAULT 1,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    UNIQUE (device_id, selector)
);

CREATE TABLE IF NOT EXISTS apps (
    app_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT,
    config_json TEXT,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS app_routes (
    route_id INTEGER PRIMARY KEY,
    app_id INTEGER NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE,
    route_name TEXT NOT NULL,
    expect_json TEXT,
    config_json TEXT,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    UNIQUE (app_id, route_name)
);

CREATE TABLE IF NOT EXISTS sessions (
    session_id INTEGER PRIMARY KEY,
    stream_id INTEGER REFERENCES streams(stream_id) ON DELETE RESTRICT,
    session_kind TEXT NOT NULL,
    rtsp_enabled INTEGER NOT NULL DEFAULT 0,
    request_json TEXT NOT NULL,
    resolved_members_json TEXT,
    state TEXT NOT NULL,
    last_error TEXT,
    started_at_ms INTEGER,
    stopped_at_ms INTEGER,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS app_sources (
    source_id INTEGER PRIMARY KEY,
    app_id INTEGER NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE,
    route_id INTEGER REFERENCES app_routes(route_id) ON DELETE RESTRICT,
    stream_id INTEGER REFERENCES streams(stream_id) ON DELETE RESTRICT,
    source_session_id INTEGER REFERENCES sessions(session_id) ON DELETE RESTRICT,
    active_session_id INTEGER REFERENCES sessions(session_id) ON DELETE RESTRICT,
    target_kind TEXT NOT NULL,
    target_name TEXT NOT NULL,
    source_kind TEXT NOT NULL,
    rtsp_enabled INTEGER NOT NULL DEFAULT 0,
    state TEXT NOT NULL,
    resolved_routes_json TEXT,
    last_error TEXT,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS session_logs (
    log_id INTEGER PRIMARY KEY,
    session_id INTEGER NOT NULL REFERENCES sessions(session_id) ON DELETE CASCADE,
    level TEXT NOT NULL,
    event_type TEXT NOT NULL,
    message TEXT NOT NULL,
    payload_json TEXT,
    created_at_ms INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_streams_device_id ON streams(device_id);
CREATE INDEX IF NOT EXISTS idx_app_routes_app_id ON app_routes(app_id);
CREATE INDEX IF NOT EXISTS idx_app_sources_app_id ON app_sources(app_id);
CREATE INDEX IF NOT EXISTS idx_app_sources_route_id ON app_sources(route_id);
CREATE INDEX IF NOT EXISTS idx_app_sources_stream_id ON app_sources(stream_id);
CREATE INDEX IF NOT EXISTS idx_app_sources_source_session_id ON app_sources(source_session_id);
CREATE INDEX IF NOT EXISTS idx_app_sources_active_session_id ON app_sources(active_session_id);
CREATE INDEX IF NOT EXISTS idx_sessions_stream_id ON sessions(stream_id);
CREATE INDEX IF NOT EXISTS idx_session_logs_session_id ON session_logs(session_id);
