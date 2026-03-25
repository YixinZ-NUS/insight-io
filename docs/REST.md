# REST API Reference

## Role

- role: public HTTP contract for `insight-io`
- status: active
- version: 3
- major changes:
  - 2026-03-24 made `delivery_name` inferred during normalization rather than
    posted by clients
  - 2026-03-24 made `uri` a derived source identifier and moved delivery into
    durable `delivery_name`
  - 2026-03-24 replaced the separate route-scoped attach endpoint with one
    app-source create surface for both URI-backed and session-backed binds
  - 2026-03-24 clarified local SDK attach is IPC-only in v1 while future
    remote or LAN RTSP consumption remains separate
- past tasks:
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

`insight-io` exposes a DB-first route-based API. Users choose a listed URI and
connect it to an app-declared route. Route declarations are purpose-first. They
should include semantic expectations for normal use, but they do not use raw
runtime stream names as the primary contract.

Important rule:

- one derived URI selects one fixed catalog-published source shape

## Current API Index

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/health` | liveness and version |
| `GET` | `/api/devices` | list public devices, presets, and derived URIs |
| `GET` | `/api/devices/{device}` | full detail for one public device |
| `POST` | `/api/devices/{device}/alias` | change the public device alias |
| `POST` | `/api/sessions` | create one direct logical session from one selected URI |
| `GET` | `/api/sessions` | list logical sessions |
| `GET` | `/api/sessions/{id}` | inspect one logical session |
| `POST` | `/api/sessions/{id}/start` | rehydrate one persisted logical session |
| `POST` | `/api/sessions/{id}/stop` | stop one logical session |
| `DELETE` | `/api/sessions/{id}` | destroy one logical session |
| `POST` | `/api/apps` | create one durable app record |
| `GET` | `/api/apps` | list durable apps |
| `GET` | `/api/apps/{id}` | inspect one app |
| `DELETE` | `/api/apps/{id}` | delete one app and its owned records |
| `POST` | `/api/apps/{id}/routes` | create one route on an app |
| `GET` | `/api/apps/{id}/routes` | list one app's routes |
| `DELETE` | `/api/apps/{id}/routes/{route}` | delete one unused route |
| `GET` | `/api/apps/{id}/sources` | list one app's sources |
| `POST` | `/api/apps/{id}/sources` | create one app-source bind from one URI or one existing session |
| `POST` | `/api/apps/{id}/sources/{source_id}/start` | restart one persisted app source |
| `POST` | `/api/apps/{id}/sources/{source_id}/stop` | stop one running app source |
| `POST` | `/api/apps/{id}/sources/{source_id}/rebind` | replace one route or grouped-route binding at runtime |
| `GET` | `/api/status` | inspect shared capture and delivery state |

## App Route Request Contract

This section describes the REST payload for `POST /api/apps/{id}/routes`.
It is not an alternative to the SDK `app.route(...)` API. In the normal SDK
flow, `app.route(...).expect(...)` serializes into this request shape.

Create routes before connecting sources:

```json
{
  "route_name": "yolov5",
  "expect": {
    "media": "video"
  }
}
```

Semantic expectation keys may include:

- `media`
- `channel`

Rules:

- non-debug routes should include `media`
- route expectations exist to reject obvious misroutes such as depth into a
  video-only route

Example:

```json
{
  "route_name": "orbbec/depth",
  "expect": {
    "media": "depth"
  }
}
```

## App Source Create / Attach Contract

Connect one listed URI to a declared route:

```json
{
  "input": "insightos://localhost/front-camera/video-720p_30",
  "route": "yolov5"
}
```

For grouped devices, discovery must still provide exact-member URIs:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/480p_30",
  "route": "orbbec/depth"
}
```

When discovery publishes a fixed grouped preset choice, one source bind may fan
out into a grouped route target:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
  "route_grouped": "orbbec"
}
```

Attach one existing session to one exact route:

```json
{
  "session_id": 42,
  "route": "yolov5"
}
```

Attach one existing grouped session to one grouped target:

```json
{
  "session_id": 43,
  "route_grouped": "orbbec"
}
```

Rules:

- exactly one of `route` or `route_grouped` is required
- exactly one of `input` or `session_id` is required
- `input` must be a catalog-published URI already exposed by the catalog
- the URI itself must already identify the fixed published source shape
- the URI selects one catalog entry; the backend infers `delivery_name` during
  normalization and persists it on the bind
- locally resolved `insightos://` sources infer `ipc`
- non-local or `rtsp://` sources infer `rtsp`
- exact-member URIs validate against one route's expectation
- grouped preset URIs validate against the declared routes under the chosen
  grouped target, for example `orbbec/color` and `orbbec/depth`
- session-backed binds validate against the referenced session's resolved
  source shape and member set
- grouped-session attaches use the same `route_grouped` surface, so one app can
  start from `orbbec/preset/...` without separately managing `/color` and
  `/depth`
- the request does not override grouped-device behavior beyond choosing a
  different URI or existing session
- if a catalog entry needs extra explanation, discovery may include a short
  human-readable comment, but clients must treat that as informational only
- duplicate URIs are allowed across routes and apps
- one route or grouped target may own at most one active binding at a time
- identical URI plus `delivery_name` combinations should reuse runtime where
  possible
- identical URIs with different delivery intents, such as `ipc` and `rtsp`,
  should stay separate delivery sessions while still being eligible for shared
  capture reuse
- when multiple entries from one source group are active, the backend must
  resolve them to one compatible grouped runtime or reject the newer request
- the public contract does not add dependency-specific source metadata until
  device investigation justifies it
- local SDK attach always uses IPC in v1
- session-backed binds in v1 should therefore reference IPC-capable sessions
- URI-backed app binds that infer `rtsp` are therefore future-facing unless the
  backend also materializes an IPC-capable `active_session_id`
- future remote or LAN RTSP consumption can be added later without changing the
  app-source resource shape

Optional advanced channel disambiguation stays in the path:

```text
insightos://<host>/<device>/<stream-preset>/channel/<channel>
```

Discovery should normally emit the final full URI so users rarely type this
manually. The channel stays in the path rather than in a query param because it
identifies the exact stream, not an optional filter.

## Direct Session Contract

Direct sessions use the same URI contract:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/400p_30"
}
```

This flow is the basis for:

- `insightos-open`
- RTSP monitoring
- session-first workflows that later attach to app routes

Normal-use rule:

- the chosen URI fixes the backend source behavior for that direct session
- the backend infers `delivery_name` from the selected source address and
  locality policy, then stores it on the session
- locally resolved `insightos://` sources infer `ipc`
- non-local or `rtsp://` sources infer `rtsp`
- a grouped preset direct session may legitimately surface multiple related
  members when discovery published that preset as one fixed bundled choice
- advanced grouped-device overrides are not part of the direct-session request

## Runtime Rebind Contract

Replace the current URI-backed or session-backed source bound to one route or
grouped target:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/400p_30"
}
```

Rules:

- exactly one of `input` or `session_id` is required
- rebind validates the replacement before switching durable state
- rebind may stop obsolete app-owned runtime after the replacement succeeds
- rebind must preserve app and target identity

## Source Response Notes

App-source responses include:

- `source_kind`
- `uri`
- `route`
- `route_grouped` when grouped
- `delivery_name`
- `source_session_id` when session-backed
- `active_session_id` when a runtime session exists
- `resolved_exact_stream_id`
- `resolved_source_variant_id`
- `resolved_source_group_id` when present
- `resolved_member_kind`
- `resolved_channel` when present
- `resolved_members_json` when grouped
- `delivered_caps_json`
- `capture_policy_json`

The backend still uses the existing session graph under the routing layer. A
successful app-source connect creates one ordinary logical session and records
which resolved exact stream was connected to the route. A successful
session-backed bind records both the referenced `source_session_id` and the
currently serving `active_session_id`.

Why both session ids are exposed:

- `source_session_id` tells the caller which previously selected session the
  bind refers to
- `active_session_id` tells the caller which session is actually serving the
  bind right now
- they are equal for direct local IPC attach
- they may differ when the selected source and the serving app session are not
  the same runtime, for example future RTSP-to-IPC bridging

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
- `POST /api/apps/{id}/sources/{source_id}/start` creates a fresh runtime
  session for that persisted source intent
- the same durable pattern applies to session-backed binds, but restart must
  revalidate the referenced session rather than assuming the old runtime still
  exists
- the same durable pattern applies to logical sessions through
  `POST /api/sessions/{id}/start`

## SDK Relationship

`app.connect(...)`, `app.connect_grouped(...)`, `app.attach(...)`, and
`app.attach_grouped(...)` are SDK conveniences over `POST /api/apps/{id}/sources`.
The REST endpoint is the external-control surface for creating and changing app
binds without embedding the SDK in the caller.

Current interop boundary:

- `insightos://` remains the local selector namespace
- `rtsp://` remains the remote/LAN transport namespace
- the current scope supports exporting local `insightos://` selections toward
  RTSP consumers
- the reverse direction does not become a first-class `insightos://` catalog
  source automatically; that would need explicit RTSP ingest/import design
