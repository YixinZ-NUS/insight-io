# Intent Routing Data Model

## Role

- role: define the minimal durable schema for `insight-io`
- status: active
- version: 11
- major changes:
  - 2026-03-25 removed stale source variant/group response fields, clarified
    queryable RTSP publication metadata, and made session delete conflict
    semantics explicit
  - 2026-03-25 clarified that route names describe app-local logical input
    roles and that `session_kind = direct` means standalone session-first
    runtime intent
  - 2026-03-25 replaced public `route` / `route_grouped` binds with one
    app-local `target` surface and reserved grouped target roots
  - 2026-03-25 replaced durable `delivery_name` with durable `rtsp_enabled`
    and publication metadata while keeping local IPC attach implicit
  - 2026-03-25 removed `/channel/...` from the active public URI grammar
  - 2026-03-24 removed stored `canonical_uri` from `streams` and made public
    `uri` values derived from stable catalog identity plus the current device
    alias
  - 2026-03-24 added grouped-session durable member resolution and aligned
    session-backed binds with the same app-source surface as URI-backed binds
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
  - `2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics`
  - `2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`
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
publication reuse, RTSP publisher state, worker processes, and attach handles,
should stay runtime-only unless a concrete persistence need is proven later.

Because this repo is intended to be implemented fresh, the schema does not need
a migration-history table or any backward-compatibility scaffolding in v1. One
checked-in SQL schema is enough to start.

## ER Diagram

See the dedicated Mermaid ER diagram at
[intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md).

## Design Rules

- one derived `uri` selects one fixed catalog-published source shape
- derived `uri` values identify source shape only
- RTSP publication intent is durable; local IPC attach is implicit for local
  SDK consumers and is not a posted field
- discovery publishes devices and streams
- apps declare intent through routes
- app sources bind either:
  - one app-local `target` to one selected `uri`, or
  - one app-local `target` to an existing `session_id`
- the backend resolves whether `target` is one exact route or one grouped
  target root
- app route declarations must reserve grouped roots:
  an app must not declare both one exact route `x` and any route below `x/`
- RTSP publication intent is durable bind/session state, not its own durable
  worker graph
- catalog publication metadata may expose a queryable RTSP URL that keeps the
  same `/<device>/<selector>` path as the derived `insightos://` URI while
  using the configured RTSP host
- local SDK attach uses IPC in v1 even when future remote or LAN RTSP
  consumption is added later
- sessions record client-visible session history
- logs record what happened
- lower-level capture and publication internals are runtime structures, not
  first
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
  stable selector identity, media kind, channel, capture policy, optional
  publication metadata, and optional grouped members
- splitting presets into a second table would force either:
  - duplicated metadata across preset and stream tables, or
  - an unnecessary preset-to-stream expansion layer before app binding

Decision:

- keep one `streams` row per selectable exact member or grouped preset
- use `device_id` to scope those rows to one device
- keep a stable `selector_key` for each row so durable ids survive device alias
  changes
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
- `public_name` is the user-facing alias used inside derived public `uri`
  values
- a separate `device_aliases` table is not needed in v1

### `streams`

Purpose:

- stores one catalog-published selectable source shape

Primary key:

- `stream_id INTEGER PRIMARY KEY`

Foreign keys:

- `device_id INTEGER NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE`

Important columns:

- `selector_key TEXT NOT NULL UNIQUE`
- `selector TEXT NOT NULL`
- `media_kind TEXT NOT NULL`
- `shape_kind TEXT NOT NULL`
- `channel TEXT`
- `group_key TEXT`
- `caps_json TEXT NOT NULL`
- `capture_policy_json TEXT`
- `members_json TEXT`
- `publications_json TEXT NOT NULL`
- `is_present INTEGER NOT NULL DEFAULT 1`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

Checks:

- `shape_kind IN ('exact', 'grouped')`
- `members_json IS NULL` for most exact-member entries
- `members_json IS NOT NULL` only for fixed grouped presets such as
  `orbbec/preset/480p_30`

Notes:

- `uri` is derived from the current device alias plus `selector`; it is not the
  durable DB key for this row
- publication is not encoded in the stored source identity; discovery can
  publish one derived `uri` plus `publications_json` describing optional
  published forms such as RTSP. A minimal shape is
  `{\"rtsp\":{\"url\":\"rtsp://<rtsp-host>/<device>/<selector>\",\"profile\":\"default\"}}`;
  the RTSP URL should mirror the `insightos://` path after swapping the scheme
  and host
- `group_key` is an internal grouping hint for runtime planning; it should not
  be exposed as a public source-group id field
- `streams` is also the only per-device preset table; a separate `presets`
  table is not needed in v1
- discovery should upsert by `selector_key` so ids stay stable across alias
  changes and refresh

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
- route names stay unique only inside one app
- route names should model the app's logical input roles, for example
  `front-camera`, `rear-camera`, `orbbec/color`, and `orbbec/depth` for one app
  consuming two V4L2 cameras plus one Orbbec
- one app must not declare both one exact route `x` and any other route below
  `x/`, because that would make the public bind target `x` ambiguous

### `app_sources`

Purpose:

- one durable binding record for one app-local target

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
- `target_name TEXT NOT NULL`
- `source_kind TEXT NOT NULL`
- `rtsp_enabled INTEGER NOT NULL DEFAULT 0`
- `state TEXT NOT NULL`
- `resolved_routes_json TEXT`
- `last_error TEXT`
- `created_at_ms INTEGER NOT NULL`
- `updated_at_ms INTEGER NOT NULL`

Checks:

- `target_kind IN ('route', 'grouped')`
- `source_kind IN ('uri', 'session')`
- if `target_kind = 'route'`, then `route_id IS NOT NULL`
- if `target_kind = 'grouped'`, then `route_id IS NULL`
- if `source_kind = 'uri'`, then `stream_id IS NOT NULL` and
  `source_session_id IS NULL`
- if `source_kind = 'session'`, then `source_session_id IS NOT NULL` and
  `stream_id IS NOT NULL`

Indexes and constraints:

- partial unique index on `(app_id, route_id)` where `route_id IS NOT NULL`
- unique index on `(app_id, target_name)`

Notes:

- `target_name` is the app-local bind key posted by clients as `target`
- for exact binds, `target_name` matches one declared route name
- for grouped binds, `target_name` is the grouped target root, for example
  `orbbec`
- `rtsp_enabled` is the durable request to publish the serving session over
  RTSP; when true the runtime should expose `rtsp_url` using the backend
  default RTSP profile and the same `/<device>/<selector>` path as the bound
  `insightos://` URI, with the configured RTSP host replacing `localhost`
- local IPC attach is implicit for local app consumers and is not stored here
- `resolved_routes_json` records grouped member-to-route resolution for grouped
  presets and grouped-session attaches
- `source_session_id` records which existing session the user asked to bind
- `active_session_id` is the latest runtime session currently serving this
  binding; in v1 it should stay IPC-capable for local SDK attach
- this table replaces the need for a dedicated route-group table in v1

Why both session ids matter:

- `source_session_id` preserves user intent: which existing session was chosen
  as the upstream source for this bind
- `active_session_id` records the session currently serving the app bind right
  now
- they are equal when an app can attach to the selected session directly
- they differ when the backend must realize a different serving session for the
  app
- they also differ across app-source restart or rebind cycles, because the
  selected upstream session may stay the same while the currently serving
  session is replaced

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
- `rtsp_enabled INTEGER NOT NULL DEFAULT 0`
- `request_json TEXT NOT NULL`
- `resolved_members_json TEXT`
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
- `session_kind = 'direct'` means one standalone or session-first session
  created before any app target bind exists
- `stream_id` points at the selected exact-member or grouped-preset catalog row
- `rtsp_enabled` is the durable request to publish this session over RTSP
- local IPC attach is not a client-posted field; it remains implicit when the
  serving runtime is local and attachable
- `resolved_members_json` records fixed member resolution for grouped sessions
  and stays `NULL` for most exact-member sessions
- `app_sources.active_session_id` is enough to answer which binding is using
  which live session
- this table does not need to embed capture/delivery worker rows
- declaring compatible routes on an app does not consume a direct session until
  one app-source bind points at that session or the same selected URI

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
- `device_key`, `public_name`, and `selector_key` stay unique business
  identifiers
- public `uri` values are derived outputs, not stored business identifiers
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
- deleting a session must be rejected with `409 Conflict` when any
  `app_sources` row still references it through `source_session_id` or
  `active_session_id`; only unreferenced sessions may be deleted, and then
  delete cascades to `session_logs`
- deleting a route should cascade only to exact-route `app_sources`; grouped
  bindings remain controlled by `target_name`

## What Stays Runtime-Only

These remain runtime structures or status snapshots, not durable tables:

- grouped compatibility planning
- shared capture and publication reuse graphs
- worker process ids and restart handles
- RTSP publisher state
- local IPC attach handles
- donor-style low-level stream attachment details

The durable DB answers what should exist and what happened. The runtime answers
how the current process graph is realizing it right now.
