# Intent Routing Data Model

## Summary

This document defines the durable data model for the route-based rebuild. The
goal is to make SQL schema and runtime ownership explicit before the code is
refactored.

## Discovery Catalog Model

Discovery is not an app table, but it defines the exact stream identity that
app and session records must persist.

Each discoverable stream entry should expose:

- `canonical_uri`
- `exact_stream_id`
- `source_variant_id`
- `source_group_id`
- `member_kind`
- `channel`
- `delivered_caps_json`
- `capture_policy_json`
- `supported_deliveries`

Current boundary:

- grouped dependency behavior is described through the existing
  `source_group_id`, `member_kind`, and `capture_policy_json` fields
- the public model does not add dependency-specific fields until real-device
  investigation justifies them
- `capture_policy_json` must be able to describe cases where delivered caps
  differ from the underlying native sensor profile, such as tested Orbbec
  aligned depth
- the tested Orbbec unit only justifies a capture-policy-backed
  `depth-480p_30` entry; it does not justify a discovered `depth-720p_30` or a
  separate aligned `depth-800p_30` entry on current evidence

Important rule:

- one `canonical_uri` maps to one delivered stream

Examples:

- `color-480p_30`
- `depth-400p_30`
- `depth-480p_30`
- `video-720p_30/channel/left`
- `video-720p_30/channel/right`

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

Notes:

- non-debug routes should include `media` in `expect_json`

### `app_sources`

One bound source per route.

Suggested fields:

- `source_id`
- `app_id`
- `route_name`
- `source_mode`
- `input_uri`
- `canonical_uri`
- `attached_session_id`
- `resolved_exact_stream_id`
- `resolved_source_variant_id`
- `resolved_source_group_id`
- `resolved_member_kind`
- `resolved_channel`
- `delivered_caps_json`
- `capture_policy_json`
- `request_json`
- `state`
- `last_error`
- `latest_session_id`
- `created_at_ms`
- `updated_at_ms`

Constraints:

- unique `(app_id, route_name)`
- foreign key to `apps`
- `route_name` must resolve to an `app_routes` row in the same app

Notes:

- `source_mode = managed_uri` means the app binding owns a URI-driven session
- `source_mode = attached_session` means the route points at an existing direct
  logical session
- `canonical_uri` remains the exact stream identity even when the route is
  attached from an existing `session_id`

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
- stores the latest resolved exact stream identity
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

Every app-level source connection resolves to one concrete exact stream
identity.

That resolved identity may carry:

- `resolved_exact_stream_id`
- `resolved_source_variant_id`
- `resolved_source_group_id`
- `resolved_member_kind`
- `resolved_channel`
- `delivered_caps_json`
- `capture_policy_json`

This allows the backend to model:

- one video source
- one native depth source
- one aligned depth source
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
backend can preserve and use:

- channel constraints
- grouped-source metadata for discovery, inspection, and runtime orchestration
- grouped runtime compatibility rules

Alignment is no longer a route-level selector.

If D2C changes the delivered caps, discovery must split that into separate
exact stream entries, for example:

- `depth-400p_30`
- `depth-480p_30`

These are route-to-source validation rules, not callback-shape rules.

Grouped runtime rule:

- one canonical URI still maps to one delivered stream
- multiple active entries from one source group may still need one compatible
  backend mode
- if grouped requests conflict, the backend should reject rather than mutate the
  meaning of an existing canonical URI
- normal use does not add a bind-time override layer on top of the chosen
  catalog entry

## Reuse And Reverse-Order Binding

The durable app model must also represent:

- identical canonical URIs reused across multiple routes or apps
- a route attached to an already-running direct session
- route rebind from one exact URI or `session_id` to another

Those flows rely on:

- non-unique `canonical_uri` across `app_sources`
- `source_mode`
- `attached_session_id`
- `latest_session_id`

## Why This Shape

- keeps durable app orchestration separate from durable media runtime
- preserves the existing session graph instead of inventing a second one
- keeps source intent queryable after restart
- avoids pushing raw runtime stream names into route declarations
- preserves exact stream identity across restart without ambiguity
- leaves room for optional `/channel/<name>` disambiguation only when needed
- keeps the current public data model stable while Orbbec grouped-device
  behavior is still being investigated
- makes the schema explicit and migration-friendly
