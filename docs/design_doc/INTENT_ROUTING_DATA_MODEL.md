# Intent Routing Data Model

## Summary

This document defines the durable data model for the target-routing rebuild.
The goal is to make SQL schema and runtime ownership explicit before the code is
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

### `app_targets`

One declared target per app.

Suggested fields:

- `target_id`
- `app_id`
- `target_name`
- `target_kind`
- `contract_json`
- `created_at_ms`
- `updated_at_ms`

Constraints:

- unique `(app_id, target_name)`

### `app_sources`

One bound source URI per app.

Suggested fields:

- `source_id`
- `app_id`
- `target_name`
- `input`
- `canonical_uri`
- `request_json`
- `state`
- `last_error`
- `latest_session_id`
- `created_at_ms`
- `updated_at_ms`

Constraints:

- unique `(app_id, canonical_uri)`
- foreign key to `apps`
- `target_name` must resolve to an `app_targets` row in the same app

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

### `app_targets`

- durable declared intent
- no runtime media ownership

### `app_sources`

- durable source binding intent
- references the latest logical session by id
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
  restart creates a fresh logical session or fresh bindings

## Role Binding Model

Role bindings are computed from `app_targets.target_kind` plus the resolved
session stream list.

### `video`

- role `primary`

### `audio`

- role `audio`

### `rgbd`

- roles `color` and `depth`
- optional role `ir`

The durable schema does not need a separate binding table in the first pass.
Bindings can be derived at runtime from the current target kind and resolved
session stream set.

## Why This Shape

- keeps durable app orchestration separate from durable media runtime
- preserves the existing session graph instead of inventing a second one
- keeps source intent queryable after restart
- makes the schema explicit and migration-friendly
