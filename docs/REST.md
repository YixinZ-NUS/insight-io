# REST API Reference

## Role

- role: public HTTP contract for `insight-io`
- status: active
- version: 13
- major changes:
  - 2026-03-27 documented the completed task-9 SDK and browser slice, added
    the frontend entrypoint plus catalog refresh endpoint, and made the
    Google-AIP-style custom methods such as `:start`, `:stop`, and `:rebind`
    the canonical documented form while preserving slash aliases in code
  - 2026-03-26 closed grouped-member-route delete cleanup for grouped binds,
    documented the cleanup semantics, and kept the current app lifecycle
    endpoint set as the active API index
  - 2026-03-26 removed redundant stored app-source kind fields from the active
    schema guidance and documented bind-kind inference from existing foreign
    keys instead
  - 2026-03-26 aligned device-catalog selector naming with the reviewed
    contract and removed redundant `selector_key` from the checked-in response
    shape
  - 2026-03-26 documented the catalog and alias request/response shape now used
    by the checked-in implementation slice
  - 2026-03-25 removed stale source variant/group response fields, made RTSP
    publication metadata queryable, and fixed session-delete conflict behavior
  - 2026-03-25 clarified that direct sessions are standalone session-first
    sessions and that multi-device apps declare app-local logical input routes
  - 2026-03-25 replaced public `route` / `route_grouped` bind inputs with one
    app-local `target` field and explicit ambiguity guardrails
  - 2026-03-25 reframed RTSP as optional durable publication intent rather
    than a peer to implicit local IPC attach
  - 2026-03-25 removed `/channel/...` from the active URI contract
- past tasks:
  - `2026-03-27 – Complete Task-9 SDK, Browser Flows, And Runtime Verification`
  - `2026-03-26 – Close Grouped Route Delete Cleanup And Refresh Runtime Handoff`
  - `2026-03-26 – Review App Route Source Persistence Slice And Reproduce Grouped Route Delete Bug`
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`
  - `2026-03-26 – Apply Selector Review And Device-Scoped Stream Keying`
  - `2026-03-26 – Take Back Redundant App-Source Kind Columns`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics`
  - `2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

`insight-io` exposes a DB-first route-based API. Users choose a listed URI and
bind it to one app-declared target. Route declarations are purpose-first. They
should include semantic expectations for normal use, but they do not use raw
runtime stream names as the primary contract.

Important rule:

- one derived URI selects one fixed catalog-published source shape

Resource-oriented API note:

- the canonical custom-method form follows the Google API Improvement Proposal
  style for resource-oriented APIs, so the documented start/stop/rebind and
  refresh actions use `:verb` suffixes
- compatibility aliases with slash paths such as `/start`, `/stop`, and
  `/rebind` are still accepted by the checked-in backend so existing tooling
  does not break

## Current API Index

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/health` | liveness and version |
| `GET` | `/` | serve the repo-native browser UI when frontend assets are configured |
| `GET` | `/api/devices` | list public devices, presets, and derived URIs |
| `POST` | `/api/devices:refresh` | rerun discovery and return the refreshed catalog |
| `GET` | `/api/devices/{device}` | full detail for one public device |
| `POST` | `/api/devices/{device}/alias` | change the public device alias |
| `POST` | `/api/sessions` | create one direct logical session from one selected URI |
| `GET` | `/api/sessions` | list logical sessions |
| `GET` | `/api/sessions/{id}` | inspect one logical session |
| `POST` | `/api/sessions/{id}:start` | rehydrate one persisted logical session |
| `POST` | `/api/sessions/{id}:stop` | stop one logical session |
| `DELETE` | `/api/sessions/{id}` | destroy one unreferenced logical session |
| `POST` | `/api/apps` | create one durable app record |
| `GET` | `/api/apps` | list durable apps |
| `GET` | `/api/apps/{id}` | inspect one durable app |
| `DELETE` | `/api/apps/{id}` | delete one durable app plus its owned routes and source bindings |
| `POST` | `/api/apps/{id}/routes` | declare one app-local route |
| `GET` | `/api/apps/{id}/routes` | list routes on one app |
| `GET` | `/api/apps/{id}/routes/{route}` | inspect one route detail |
| `DELETE` | `/api/apps/{id}/routes/{route}` | delete one app-local route |
| `GET` | `/api/apps/{id}/sources` | list durable app-source bindings |
| `GET` | `/api/apps/{id}/sources/{source_id}` | inspect one source detail |
| `POST` | `/api/apps/{id}/sources` | create one exact, grouped, or session-backed app-source bind |
| `POST` | `/api/apps/{id}/sources/{source_id}:start` | recreate runtime state for one durable source |
| `POST` | `/api/apps/{id}/sources/{source_id}:stop` | stop runtime state while keeping the durable source row |
| `POST` | `/api/apps/{id}/sources/{source_id}:rebind` | move one durable source record to a replacement URI or session |
| `GET` | `/api/status` | inspect shared capture and publication state |

## Browser UI

- `GET /` serves the checked-in static frontend when `insightiod` knows a
  frontend directory
- `insightiod` now auto-detects the repo-root `frontend/` directory when run
  from the repository root
- `/static/*` serves the bundled HTML, CSS, and JavaScript assets for the
  browser route-builder flow

## Grouped Route Delete Cleanup

Status: `resolved`

- deleting one grouped member route now removes any grouped `app_sources` row
  whose `resolved_members_json` references that route
- if that grouped bind owned one app-created grouped session, the backend also
  deletes that session so stale resolved-member metadata is not retained
- the verified live route-delete path is:
  `DELETE /api/apps/{id}/routes/orbbec%2Fdepth` after binding
  `insightos://localhost/sv1301s-u3/orbbec/preset/480p_30` to target `orbbec`

## Device Catalog Publication Notes

Catalog entries returned by `GET /api/devices` and `GET /api/devices/{device}`
should include one derived `uri` plus `publications_json`.

Rules:

- `publications_json.rtsp.url` is queryable publication metadata for that source
  shape
- catalog responses expose `selector` only; they do not need a redundant
  concatenated `selector_key`
- single-stream V4L2 selectors stay compact, for example `720p_30`
- grouped Orbbec selectors keep the `orbbec/...` namespace so grouped preset
  selectors and grouped target names stay aligned
- the RTSP URL should keep the same `/<device>/<selector>` path as the derived
  `insightos://` URI while replacing `localhost` with the configured RTSP host
- the presence of that URL does not by itself guarantee an active RTSP
  publisher; reachability still depends on current runtime publication state

## Device Alias Request

The checked-in alias request body is:

```json
{
  "public_name": "front-camera"
}
```

Rules:

- `public_name` must contain at least one alphanumeric character after slug
  normalization
- the normalized alias must be unique across devices
- the response returns the updated device object and re-derives both
  `uri` and `publications_json.rtsp.url` on the new public device name

## App Create Contract

Create one durable app record:

```json
{
  "name": "vision-runner"
}
```

Optional fields:

- `description`
- `config_json`

Current endpoints:

- `POST /api/apps`
- `GET /api/apps`
- `GET /api/apps/{id}`
- `DELETE /api/apps/{id}`

Rules:

- `name` is required
- the backend slug-normalizes `name`
- the normalized app name must be unique
- deleting an app removes owned routes and app-source rows, and stops any
  app-owned runtime sessions still active for that app

## App Route Request Contract

This section describes the current REST payload for
`POST /api/apps/{id}/routes`. It is not an alternative to the SDK
`app.route(...)` API. In the normal SDK flow,
`app.route(...).expect(...)` serializes into this request shape.

Create routes before binding sources:

```json
{
  "route_name": "vision/detector",
  "expect": {
    "media": "video"
  }
}
```

Semantic expectation keys may include:

- `media`

Rules:

- non-debug routes should include `media`
- route expectations exist to reject obvious misroutes such as depth into a
  video-only route
- route names are app-local target keys; they may be flat or hierarchical
- route names should describe the app's logical input role rather than the
  discovered device alias or one global resource name
- declaring a route does not start callbacks by itself; one target stays idle
  until one app-source bind becomes active
- guardrail:
  an app must not declare both one exact route `x` and any route below `x/`,
  because that would make the public bind target `x` ambiguous

Example:

```json
{
  "route_name": "orbbec/depth",
  "expect": {
    "media": "depth"
  }
}
```

Multi-device example for one app that consumes two V4L2 cameras plus one
Orbbec:

```json
[
  {
    "route_name": "front-camera",
    "expect": {
      "media": "video"
    }
  },
  {
    "route_name": "rear-camera",
    "expect": {
      "media": "video"
    }
  },
  {
    "route_name": "orbbec/color",
    "expect": {
      "media": "video"
    }
  },
  {
    "route_name": "orbbec/depth",
    "expect": {
      "media": "depth"
    }
  }
]
```

That keeps the app contract role-first:

- `front-camera` and `rear-camera` are app-local roles that may later bind to
  whichever V4L2 device URIs satisfy them
- `orbbec/color` and `orbbec/depth` stay ordinary routes, while grouped preset
  binds may still target the shared root `orbbec`

Current endpoints:

- `POST /api/apps/{id}/routes`
- `GET /api/apps/{id}/routes`
- `GET /api/apps/{id}/routes/{route}`
- `DELETE /api/apps/{id}/routes/{route}`

## App Source Bind Contract

Bind one listed URI to one app-local target:

```json
{
  "input": "insightos://localhost/front-camera/720p_30",
  "target": "vision/detector"
}
```

For grouped devices, discovery must still provide exact-member URIs:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/480p_30",
  "target": "orbbec/depth"
}
```

When discovery publishes a fixed grouped preset choice, one source bind may fan
out into the grouped target root:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
  "target": "orbbec"
}
```

Bind one existing session to one exact target:

```json
{
  "session_id": 42,
  "target": "vision/detector"
}
```

Bind one existing grouped session to the grouped target root:

```json
{
  "session_id": 43,
  "target": "orbbec"
}
```

Rules:

- `target` is required
- exactly one of `input` or `session_id` is required
- `input` must be a catalog-published `insightos://` URI already exposed by
  the catalog
- the `insightos://` host must match the locally configured catalog host
- the URI itself must already identify the fixed published source shape
- raw `rtsp://` input is not part of the v1 source-selection contract; RTSP is
  a publication of a selected `insightos://` source, not a posted source URI
- `target` is app-local; the backend first resolves an exact route match, and
  otherwise resolves a grouped target root when declared routes exist below
  `target/...`
- exact-route diagnostics should use resource-shaped names such as
  `apps/{app}/routes/{route}` rather than requiring callers to post
  `app-name/route-name`
- guardrail:
  route declaration or bind must fail if an app would make one posted target
  ambiguous, for example both exact route `orbbec` and grouped routes below
  `orbbec/...`
- exact-member URIs validate against one route's expectation
- grouped preset URIs validate against the declared routes under the chosen
  grouped target root, for example `orbbec/color` and `orbbec/depth`
- session-backed binds validate against the referenced session's resolved
  source shape and member set
- grouped-session binds use the same `target` surface, so one app can start
  from `orbbec/preset/...` without separately managing `/color` and `/depth`
- the request does not override grouped-device behavior beyond choosing a
  different URI or existing session
- `rtsp_enabled` is optional and defaults to `false`
- when `rtsp_enabled = true`, the runtime should publish an `rtsp_url` for the
  serving session using the backend default RTSP profile and the same
  `/<device>/<selector>` path as the selected `insightos://` URI, with the
  configured RTSP host replacing `localhost`
- local IPC attach is implicit for local SDK consumers; it is not a posted
  field and there is no public `ipc` delivery selector
- if a catalog entry needs extra explanation, discovery may include a short
  human-readable comment, but clients must treat that as informational only
- duplicate URIs are allowed across targets and apps
- one exact route or grouped target root may own at most one active binding at
  a time
- identical URI and publication requirements should reuse runtime where
  possible
- one request needing RTSP publication and one request not needing it may still
  share capture and serving runtime when lifecycle rules allow the shared
  session to expose RTSP as an additive publication
- when multiple entries from one source group are active, the backend must
  resolve them to one compatible grouped runtime or reject the newer request
- the public contract does not add dependency-specific source metadata until
  device investigation justifies it
- local SDK attach always uses IPC in v1
- session-backed binds in v1 should therefore reference sessions the backend can
  serve locally through IPC
- future remote or LAN RTSP consumption can be added later without changing the
  app-source resource shape

Current endpoints:

- `GET /api/apps/{id}/sources`
- `GET /api/apps/{id}/sources/{source_id}`
- `POST /api/apps/{id}/sources`
- `POST /api/apps/{id}/sources/{source_id}:start`
- `POST /api/apps/{id}/sources/{source_id}:stop`
- `POST /api/apps/{id}/sources/{source_id}:rebind`

Current response shape highlights:

- `target`
- `target_resource_name` on exact binds
- `uri`
- `state`
- `rtsp_enabled`
- `resolved_exact_stream_id`
- `resolved_members_json` on grouped binds
- `source_session_id` when session-backed
- `active_session_id`
- `source_session` and/or `active_session` when available

## Direct Session Contract

Direct sessions use the same URI contract:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/400p_30",
  "rtsp_enabled": true
}
```

A direct session is a standalone, session-first session created from one
selected URI before any app target is involved. It is not app-exclusive, but it
also does not feed matching routes automatically.

This flow is the basis for:

- `insightos-open`
- RTSP monitoring
- session-first workflows that later bind to app targets

Normal-use rule:

- the chosen URI fixes the backend source behavior for that direct session
- the `insightos://` host must match the locally configured catalog host
- `direct` means standalone or session-first:
  the session may later be bound to one app target through `session_id`
- `rtsp_enabled` is optional and defaults to `false`
- when `rtsp_enabled = true`, the runtime publishes an `rtsp_url` using the
  backend default RTSP profile and the same `/<device>/<selector>` path as the
  selected `insightos://` URI, with the configured RTSP host replacing
  `localhost`
- local IPC attach is not a client-posted field
- apps that merely declare compatible routes do not receive callbacks from this
  session until they create one app-source bind by `input` or `session_id`
- when an app later binds the same `session_id` or independently binds the same
  URI, the backend should reuse runtime where possible, but the app still
  needs its own active bind to receive frames
- a grouped preset direct session may legitimately surface multiple related
  members when discovery published that preset as one fixed bundled choice
- advanced grouped-device overrides are not part of the direct-session request
- raw `rtsp://` ingest remains a future import path rather than a v1 session
  input shape

## Session Delete Contract

Rules for `DELETE /api/sessions/{id}`:

- return `409 Conflict` when any app source still references the session through
  `source_session_id` or `active_session_id`
- do not silently detach or rewrite app-source bindings during delete
- when the session is unreferenced, delete the session and cascade delete its
  `session_logs`

## Runtime Rebind Contract

Replace the current URI-backed or session-backed source bound to one exact
target or grouped target root:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/400p_30",
  "rtsp_enabled": false
}
```

Rules:

- exactly one of `input` or `session_id` is required
- rebind validates the replacement before switching durable state
- rebind may stop obsolete app-owned runtime after the replacement succeeds
- rebind must preserve app and target identity

## Source Response Notes

App-source responses include:

- `uri`
- `target`
- `target_resource_name` when the target resolves to one exact route, formatted
  as `apps/{app}/routes/{route}`
- `state`
- `last_error`
- `rtsp_enabled`
- `rtsp_url` when active and published
- `source_session_id` when session-backed
- `active_session_id` when a runtime session exists
- `resolved_exact_stream_id`
- `resolved_members_json` when grouped
- `delivered_caps_json`
- `capture_policy_json`

The backend still uses the existing session graph under the routing layer. A
successful app-source bind creates one ordinary logical session and records
which resolved exact stream was connected to the target. A successful
session-backed bind records both the referenced `source_session_id` and the
currently serving `active_session_id`. Grouped binds additionally record their
member-to-route mapping in `resolved_members_json`.

Bind-kind inference rules:

- if `target_resource_name` is present, the bind resolves to one exact declared
  route
- if `target_resource_name` is absent and `resolved_members_json` is present,
  the bind resolves to one grouped target root
- if `source_session_id` is present, the bind is session-backed
- if `source_session_id` is absent, the bind is URI-backed

Why both session ids are exposed:

- `source_session_id` tells the caller which previously selected session the
  bind refers to
- `active_session_id` tells the caller which session is actually serving the
  bind right now
- they are equal for direct local IPC attach
- they may differ when the selected source and the serving app session are not
  the same runtime, for example after restart, rebind, or regrouped serving
  replacement

Current boundary:

- grouped-device dependency behavior remains documented through source-group,
  capture-policy, and grouped-member metadata
- tested Orbbec aligned depth confirms the API can keep exact member delivery
  for `orbbec/depth/480p_30` while relying on capture policy rather than a
  literal native `480p` depth profile lookup
- the sibling `insightos` live RGBD proximity-capture flow also confirms the
  API should allow a grouped preset choice such as `orbbec/preset/480p_30`
  when the bundled color + aligned-depth behavior is fixed and
  catalog-published
- the same device did not expose a compatible `1280x720` D2C depth path and did
  not show a distinct aligned `1280x800` output, so the API should not invent
  those aligned variants for that unit

## Restart Behavior

- app, route, and source records are durable
- startup normalizes persisted source runtime state back to `stopped`
- `POST /api/apps/{id}/sources/{source_id}:start` creates a fresh runtime
  session for that persisted source intent
- the same durable pattern applies to session-backed binds, but restart must
  revalidate the referenced session rather than assuming the old runtime still
  exists
- the same durable pattern applies to logical sessions through
  `POST /api/sessions/{id}:start`

## SDK Relationship

`app.bind_source(...)` is the SDK convenience over
`POST /api/apps/{id}/sources`, and `app.rebind(...)` is the SDK convenience
over the source rebind endpoint. The REST endpoint is the external-control
surface for creating and changing app binds without embedding the SDK in the
caller.

Current interop boundary:

- `insightos://` remains the local selector namespace
- `rtsp://` remains the publication namespace for remote or LAN consumption
- the current scope supports exporting local `insightos://` selections toward
  RTSP consumers
- the reverse direction does not become a first-class `insightos://` catalog
  source automatically; that would need explicit RTSP ingest/import design
