-- role: canonical v1 durable schema for the standalone insight-io rebuild.
-- revision: 2026-03-27 developer-rest-and-stream-aliases
-- major changes: adds durable per-stream public aliases for developer-facing
-- canonical URIs while keeping stable internal selector identity, and keeps
-- the existing app-route ambiguity guards plus exact-bind/session consistency.
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
    public_name TEXT NOT NULL,
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
    UNIQUE (device_id, selector),
    UNIQUE (device_id, public_name)
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
    UNIQUE (app_id, route_name),
    UNIQUE (app_id, route_id)
);

CREATE TABLE IF NOT EXISTS sessions (
    session_id INTEGER PRIMARY KEY,
    stream_id INTEGER NOT NULL REFERENCES streams(stream_id) ON DELETE RESTRICT,
    session_kind TEXT NOT NULL CHECK (session_kind IN ('direct', 'app')),
    rtsp_enabled INTEGER NOT NULL DEFAULT 0,
    request_json TEXT NOT NULL,
    resolved_members_json TEXT,
    state TEXT NOT NULL,
    last_error TEXT,
    started_at_ms INTEGER,
    stopped_at_ms INTEGER,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    UNIQUE (session_id, stream_id)
);

CREATE TABLE IF NOT EXISTS app_sources (
    source_id INTEGER PRIMARY KEY,
    app_id INTEGER NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE,
    route_id INTEGER,
    stream_id INTEGER NOT NULL REFERENCES streams(stream_id) ON DELETE RESTRICT,
    source_session_id INTEGER,
    active_session_id INTEGER,
    target_name TEXT NOT NULL,
    rtsp_enabled INTEGER NOT NULL DEFAULT 0,
    state TEXT NOT NULL,
    resolved_routes_json TEXT,
    last_error TEXT,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    FOREIGN KEY (app_id, route_id)
        REFERENCES app_routes(app_id, route_id)
        ON DELETE CASCADE,
    FOREIGN KEY (source_session_id, stream_id)
        REFERENCES sessions(session_id, stream_id)
        ON DELETE RESTRICT,
    FOREIGN KEY (active_session_id, stream_id)
        REFERENCES sessions(session_id, stream_id)
        ON DELETE RESTRICT
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
CREATE UNIQUE INDEX IF NOT EXISTS idx_app_sources_app_target_name
    ON app_sources(app_id, target_name);
CREATE UNIQUE INDEX IF NOT EXISTS idx_app_sources_app_route_id_not_null
    ON app_sources(app_id, route_id)
    WHERE route_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_sessions_stream_id ON sessions(stream_id);
CREATE INDEX IF NOT EXISTS idx_session_logs_session_id ON session_logs(session_id);

CREATE TRIGGER IF NOT EXISTS trg_app_routes_no_ambiguous_target_insert
BEFORE INSERT ON app_routes
FOR EACH ROW
WHEN EXISTS (
    SELECT 1
    FROM app_routes existing
    WHERE existing.app_id = NEW.app_id
      AND (
          substr(existing.route_name, 1, length(NEW.route_name) + 1) =
              NEW.route_name || '/'
          OR substr(NEW.route_name, 1, length(existing.route_name) + 1) =
              existing.route_name || '/'
      )
)
BEGIN
    SELECT RAISE(ABORT, 'ambiguous_route_target');
END;

CREATE TRIGGER IF NOT EXISTS trg_app_routes_no_ambiguous_target_update
BEFORE UPDATE OF route_name, app_id ON app_routes
FOR EACH ROW
WHEN EXISTS (
    SELECT 1
    FROM app_routes existing
    WHERE existing.route_id != NEW.route_id
      AND existing.app_id = NEW.app_id
      AND (
          substr(existing.route_name, 1, length(NEW.route_name) + 1) =
              NEW.route_name || '/'
          OR substr(NEW.route_name, 1, length(existing.route_name) + 1) =
              existing.route_name || '/'
      )
)
BEGIN
    SELECT RAISE(ABORT, 'ambiguous_route_target');
END;

CREATE TRIGGER IF NOT EXISTS trg_app_sources_exact_target_matches_route_insert
BEFORE INSERT ON app_sources
FOR EACH ROW
WHEN NEW.route_id IS NOT NULL
AND COALESCE((
        SELECT route_name
        FROM app_routes
        WHERE app_id = NEW.app_id
          AND route_id = NEW.route_id
    ), '') != NEW.target_name
BEGIN
    SELECT RAISE(ABORT, 'exact_target_must_match_route_name');
END;

CREATE TRIGGER IF NOT EXISTS trg_app_sources_exact_target_matches_route_update
BEFORE UPDATE OF app_id, route_id, target_name ON app_sources
FOR EACH ROW
WHEN NEW.route_id IS NOT NULL
AND COALESCE((
        SELECT route_name
        FROM app_routes
        WHERE app_id = NEW.app_id
          AND route_id = NEW.route_id
    ), '') != NEW.target_name
BEGIN
    SELECT RAISE(ABORT, 'exact_target_must_match_route_name');
END;

CREATE TRIGGER IF NOT EXISTS trg_app_sources_grouped_target_not_exact_route_insert
BEFORE INSERT ON app_sources
FOR EACH ROW
WHEN NEW.route_id IS NULL
AND EXISTS (
    SELECT 1
    FROM app_routes
    WHERE app_id = NEW.app_id
      AND route_name = NEW.target_name
)
BEGIN
    SELECT RAISE(ABORT, 'grouped_target_conflicts_with_exact_route');
END;

CREATE TRIGGER IF NOT EXISTS trg_app_sources_grouped_target_not_exact_route_update
BEFORE UPDATE OF app_id, route_id, target_name ON app_sources
FOR EACH ROW
WHEN NEW.route_id IS NULL
AND EXISTS (
    SELECT 1
    FROM app_routes
    WHERE app_id = NEW.app_id
      AND route_name = NEW.target_name
)
BEGIN
    SELECT RAISE(ABORT, 'grouped_target_conflicts_with_exact_route');
END;
