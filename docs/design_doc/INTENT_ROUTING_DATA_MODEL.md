# Intent Routing Data Model

## Role

- role: define the minimal durable schema for `insight-io`
- status: active
- version: 5
- major changes:
  - 2026-03-24 removed migration-history-table and other compatibility
    requirements from the greenfield schema
  - 2026-03-24 clarified that per-device presets stay in `streams` rather than
    a separate preset table
  - 2026-03-24 simplified the durable schema to catalog, app intent, session,
    and log tables
  - 2026-03-24 replaced `route_namespace` with `route_grouped`
  - 2026-03-24 moved capture, delivery, and worker reuse details out of the
    SQL model and into runtime/status responsibility
- past tasks:
  - `2026-03-24 – Separate Catalog Publication From Runtime Ownership And Rename Route APIs`
  - `2026-03-24 – Simplify The Durable Data Model And Add A Docs Hub`

## Summary

The durable schema should stay small.

Beyond `devices`, `streams`, `sessions`, `session_logs`, and `apps`, the only
additional business tables needed for the current contract are:

- `app_routes`
- `app_sources`

That gives one minimal durable schema:

- `devices`
- `streams`
- `apps`
- `app_routes`
- `app_sources`
- `sessions`
- `session_logs`

Everything lower-level than that, such as grouped capture planning, shared
delivery reuse, RTSP publisher state, worker processes, and attach handles,
should stay runtime-only unless a concrete persistence need is proven later.

Because this repo is intended to be implemented fresh, the schema does not need
a migration-history table or any backward-compatibility scaffolding in v1. One
checked-in canonical SQL schema is enough to start.

## Design Rules

- one canonical URI selects one fixed catalog-published source shape
- discovery publishes devices and streams
- apps declare intent through routes
- app sources bind either:
  - one route to one selected stream, or
  - one `route_grouped` value to one grouped preset stream, or
  - one route to an existing `session_id`
- sessions record client-visible session history
- logs record what happened
- lower-level capture and delivery internals are runtime structures, not first
  class SQL tables in v1

## Why The Schema Can Stay Small

The current product has four durable concerns:

1. What can the user select?
2. What did the user declare for the app?
3. What session exists or existed?
4. What happened while it ran?

Those concerns map cleanly to:

- catalog: `devices`, `streams`
- app intent: `apps`, `app_routes`, `app_sources`
- session history: `sessions`
- audit trail: `session_logs`

There is no current product need for separate durable tables for:

- `session_bindings`
- `capture_sessions`
- `delivery_sessions`
- `daemon_runs`
- grouped preset members as their own rows
- route-group declarations as their own rows

Those all add complexity faster than they add product value. The runtime can
still expose them through `/api/status`, but they do not need to be the
authoritative long-lived schema.

## Device Presets Stay In `streams`

Per-device presets do not need their own table in v1.

Why:

- every user-selectable preset is already a catalog-published source shape
- exact-member presets and grouped presets both need the same core fields:
  canonical URI, media kind, channel, capture policy, deliveries, and optional
  grouped members
- splitting presets into a second table would force either:
  - duplicated metadata across preset and stream tables, or
  - an unnecessary preset-to-stream expansion layer before app binding

Decision:

- keep one `streams` row per selectable exact member or grouped preset
- use `device_id` to scope those rows to one device
- use `shape_kind` to distinguish exact versus grouped
- use `members_json` only when one grouped preset row needs to describe its
  fixed members

This means `streams` is already the single per-device preset table.

## Table Inventory

## Catalog Tables

### `devices`

Purpose:

- stores one discovered device identity and its public alias

Primary key:

- `device_id INTEGER PRIMARY KEY`

Important columns:

- `device_key TEXT NOT NULL UNIQUE`
- `public_name TEXT NOT NULL UNIQUE`
- `driver TEXT NOT NULL`
- `status TEXT NOT NULL`
- `metadata_json TEXT`
- `last_seen_at_ms INTEGER`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

Notes:

- `device_key` is the stable discovery identity
- `public_name` is the user-facing alias used inside canonical URIs
- a separate `device_aliases` table is not needed in v1

### `streams`

Purpose:

- stores one catalog-published selectable source shape

Primary key:

- `stream_id INTEGER PRIMARY KEY`

Foreign keys:

- `device_id INTEGER NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE`

Important columns:

- `canonical_uri TEXT NOT NULL UNIQUE`
- `selector TEXT NOT NULL`
- `media_kind TEXT NOT NULL`
- `shape_kind TEXT NOT NULL`
- `channel TEXT`
- `group_key TEXT`
- `caps_json TEXT NOT NULL`
- `capture_policy_json TEXT`
- `members_json TEXT`
- `deliveries_json TEXT NOT NULL`
- `is_present INTEGER NOT NULL DEFAULT 1`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

Checks:

- `shape_kind IN ('exact', 'grouped')`
- `members_json IS NULL` for most exact-member entries
- `members_json IS NOT NULL` only for fixed grouped presets such as
  `orbbec/preset/480p_30`

Notes:

- `group_key` is enough for RGBD or stereo grouping; a separate `stream_groups`
  table is not needed yet
- `streams` is also the only per-device preset table; a separate `presets`
  table is not needed in v1
- discovery should upsert by `canonical_uri` so ids stay stable across refresh

## App Tables

### `apps`

Purpose:

- one durable application record

Primary key:

- `app_id INTEGER PRIMARY KEY`

Important columns:

- `name TEXT NOT NULL UNIQUE`
- `description TEXT`
- `config_json TEXT`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

### `app_routes`

Purpose:

- one declared intent route on one app

Primary key:

- `route_id INTEGER PRIMARY KEY`

Foreign keys:

- `app_id INTEGER NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE`

Important columns:

- `route_name TEXT NOT NULL`
- `expect_json TEXT`
- `config_json TEXT`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

Indexes and constraints:

- `UNIQUE (app_id, route_name)`

Notes:

- non-debug routes should normally include `media` in `expect_json`
- route names remain business-facing, for example `yolov5`, `orbbec/color`,
  or `orbbec/depth`

### `app_sources`

Purpose:

- one durable binding record for one route or one grouped route target

Primary key:

- `source_id INTEGER PRIMARY KEY`

Foreign keys:

- `app_id INTEGER NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE`
- `route_id INTEGER REFERENCES app_routes(route_id) ON DELETE CASCADE`
- `stream_id INTEGER REFERENCES streams(stream_id)`
- `source_session_id INTEGER REFERENCES sessions(session_id)`
- `active_session_id INTEGER REFERENCES sessions(session_id)`

Important columns:

- `target_kind TEXT NOT NULL`
- `route_grouped TEXT`
- `source_kind TEXT NOT NULL`
- `state TEXT NOT NULL`
- `resolved_routes_json TEXT`
- `last_error TEXT`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

Checks:

- `target_kind IN ('route', 'grouped')`
- `source_kind IN ('stream', 'session')`
- if `target_kind = 'route'`, then `route_id IS NOT NULL` and
  `route_grouped IS NULL`
- if `target_kind = 'grouped'`, then `route_id IS NULL` and
  `route_grouped IS NOT NULL`
- if `source_kind = 'stream'`, then `stream_id IS NOT NULL` and
  `source_session_id IS NULL`
- if `source_kind = 'session'`, then `source_session_id IS NOT NULL`

Indexes and constraints:

- partial unique index on `(app_id, route_id)` where `route_id IS NOT NULL`
- partial unique index on `(app_id, route_grouped)` where `route_grouped IS NOT NULL`

Notes:

- `route_grouped` is the grouped bind target name, for example `orbbec`
- `resolved_routes_json` records grouped member-to-route resolution for grouped
  presets
- `active_session_id` is the latest runtime session currently serving this
  binding
- this table replaces the need for a dedicated route-group table in v1

## Session And Log Tables

### `sessions`

Purpose:

- one durable logical session record

Primary key:

- `session_id INTEGER PRIMARY KEY`

Foreign keys:

- `stream_id INTEGER NOT NULL REFERENCES streams(stream_id)`

Important columns:

- `session_kind TEXT NOT NULL`
- `request_json TEXT NOT NULL`
- `resolved_stream_name TEXT`
- `state TEXT NOT NULL`
- `last_error TEXT`
- `started_at_ms INTEGER`
- `stopped_at_ms INTEGER`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

Checks:

- `session_kind IN ('direct', 'app')`

Notes:

- direct sessions and app-created sessions share one table
- `app_sources.active_session_id` is enough to answer which binding is using
  which live session
- this table does not need to embed capture/delivery worker rows

### `session_logs`

Purpose:

- append-only session event and audit trail

Primary key:

- `log_id INTEGER PRIMARY KEY`

Foreign keys:

- `session_id INTEGER NOT NULL REFERENCES sessions(session_id) ON DELETE CASCADE`

Important columns:

- `level TEXT NOT NULL`
- `event_type TEXT NOT NULL`
- `message TEXT NOT NULL`
- `payload_json TEXT`
- `created_at_ms INTEGER NOT NULL`

Recommended indexes:

- index on `(session_id, created_at_ms)`

Notes:

- a single log table is enough for session lifecycle, runtime failures,
  rebinds, restarts, and grouped-compatibility rejections
- separate app logs or worker logs are not needed in the durable schema yet

## PK And FK Strategy

Use surrogate integer primary keys for joins and performance, and keep public
identifiers as unique business keys.

That means:

- `device_id`, `stream_id`, `app_id`, `route_id`, `source_id`, `session_id`,
  and `log_id` are integer primary keys
- `device_key`, `public_name`, and `canonical_uri` stay unique business
  identifiers
- route names stay unique only inside one app

Recommended foreign-key graph:

- `streams.device_id -> devices.device_id`
- `app_routes.app_id -> apps.app_id`
- `app_sources.app_id -> apps.app_id`
- `app_sources.route_id -> app_routes.route_id`
- `app_sources.stream_id -> streams.stream_id`
- `app_sources.source_session_id -> sessions.session_id`
- `app_sources.active_session_id -> sessions.session_id`
- `sessions.stream_id -> streams.stream_id`
- `session_logs.session_id -> sessions.session_id`

Deletion rules:

- deleting an app cascades to `app_routes` and `app_sources`
- deleting a device cascades to `streams`
- deleting a session cascades to `session_logs`
- deleting a route should cascade only to exact-route `app_sources`; grouped
  bindings remain controlled by `route_grouped`

## What Stays Runtime-Only

These remain runtime structures or status snapshots, not durable tables:

- grouped compatibility planning
- shared capture and delivery reuse graphs
- worker process ids and restart handles
- RTSP publisher state
- local IPC attach handles
- donor-style low-level stream attachment details

The durable DB answers what should exist and what happened. The runtime answers
how the current process graph is realizing it right now.
