# Intent Routing Data Model

## Summary

This document defines the durable data model for the route-based rebuild. The
goal is to make SQL schema and runtime ownership explicit before the code is
refactored.

## Durable Tables

### `schema_migrations`

Tracks applied SQL migrations.

Suggested fields:

- `version`
- `applied_at_ms`

### `apps`

One persistent application record.

Suggested fields:

- `app_id`
- `name`
- `description`
- `created_at_ms`
- `updated_at_ms`

### `app_routes`

One declared route per app.

Suggested fields:

- `route_id`
- `app_id`
- `route_name`
- `expect_json`
- `config_json`
- `created_at_ms`
- `updated_at_ms`

Constraints:

- unique `(app_id, route_name)`

### `app_sources`

One connected source URI per app.

Suggested fields:

- `source_id`
- `app_id`
- `route_name`
- `input`
- `canonical_uri`
- `resolved_source_id`
- `resolved_source_member`
- `resolved_source_group_id`
- `request_json`
- `state`
- `last_error`
- `latest_session_id`
- `created_at_ms`
- `updated_at_ms`

Constraints:

- unique `(app_id, canonical_uri)`
- foreign key to `apps`
- `route_name` must resolve to an `app_routes` row in the same app

## Existing Durable Runtime Graph

The current runtime graph remains valid and stays durable:

- `logical_sessions`
- `session_bindings`
- `capture_sessions`
- `delivery_sessions`
- `daemon_runs`

## Ownership Rules

### `apps`

- durable application identity
- no live worker ownership

### `app_routes`

- durable declared intent
- stores semantic expectations for that route
- no runtime media ownership

### `app_sources`

- durable source connection intent
- references the latest logical session by id
- stores the latest resolved source identity
- does not own low-level OS handles

### `logical_sessions`

- durable client-visible session request and resolution
- still the source of truth for session history and restart intent

### `capture_sessions` and `delivery_sessions`

- durable runtime graph nodes
- still scoped by `daemon_run`
- still normalized to `stopped` on backend startup

## Startup Normalization

On backend startup:

- `logical_sessions` stay durable but inactive
- `session_bindings` are cleared
- old `capture_sessions` and `delivery_sessions` are marked stopped
- `app_sources.state` is normalized to `stopped`
- `app_sources.latest_session_id` remains as history only until an explicit
  restart creates a fresh logical session

## Source Identity Model

Every app-level source connection resolves to one concrete source identity.

That resolved identity may carry:

- `resolved_source_id`
- `resolved_source_member`
- `resolved_source_group_id`

This allows the backend to model:

- one video source
- one depth source
- one stereo-left source
- one stereo-right source
- two or more related sources that belong to the same source group

The app route itself does not declare raw runtime stream names. It declares
purpose plus semantic expectations through `expect_json`.

## Dependent Sources And Grouped Devices

Some sources are independent. Others are related:

- color + depth from one RGBD device
- left + right from one stereo device

For those cases, the data model must preserve source-group information so the
backend can enforce:

- same-group constraints
- channel constraints
- alignment constraints such as D2C requirements

These are route-to-source validation rules, not callback-shape rules.

## Why This Shape

- keeps durable app orchestration separate from durable media runtime
- preserves the existing session graph instead of inventing a second one
- keeps source intent queryable after restart
- avoids pushing raw runtime stream names into route declarations
- leaves room for grouped-source constraints without making the URI path deeper
- makes the schema explicit and migration-friendly
