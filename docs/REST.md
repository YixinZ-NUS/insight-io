# REST API Reference

`insight-io` exposes a DB-first route-based API. Users choose a listed URI and
connect it to an app-declared route. Route declarations are purpose-first. They
should include semantic expectations for normal use, but they do not use raw
runtime stream names as the primary contract.

Important rule:

- one canonical URI maps to one delivered stream

## Current API Index

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/health` | liveness and version |
| `GET` | `/api/devices` | list public devices, presets, and canonical URIs |
| `GET` | `/api/devices/{device}` | full detail for one public device |
| `POST` | `/api/devices/{device}/alias` | change the public device alias |
| `POST` | `/api/sessions` | create one direct logical session from an exact URI |
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
| `POST` | `/api/apps/{id}/sources` | connect one canonical URI to one app route |
| `POST` | `/api/apps/{id}/sources/{source_id}/start` | restart one persisted app source |
| `POST` | `/api/apps/{id}/sources/{source_id}/stop` | stop one running app source |
| `POST` | `/api/apps/{id}/sources/{source_id}/rebind` | replace one route binding at runtime |
| `POST` | `/api/apps/{id}/routes/{route}/attach-session` | bind one existing session to a route |
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
  "route_name": "orbbec-depth",
  "expect": {
    "media": "depth"
  }
}
```

## App Source Contract

Connect a listed canonical URI to a declared route:

```json
{
  "input": "insightos://localhost/front-camera/video-720p_30/mjpeg",
  "route": "yolov5"
}
```

For grouped devices, discovery must still provide exact stream URIs:

```json
{
  "input": "insightos://localhost/desk-rgbd/depth-480p_30",
  "route": "orbbec-depth"
}
```

Rules:

- `route` is required
- `input` must be a canonical URI already exposed by the catalog
- the URI itself must already identify the exact delivered stream
- route expectations are checked against resolved source metadata
- the request does not override grouped-device behavior beyond choosing a
  different canonical URI
- if a catalog entry needs extra explanation, discovery may include a short
  human-readable comment, but clients must treat that as informational only
- duplicate canonical URIs are allowed across routes and apps
- one route may own at most one active binding at a time
- identical canonical URIs should reuse runtime where possible
- URIs that differ only by delivery, such as `/mjpeg` and `/rtsp`, should stay
  separate delivery sessions while still being eligible for shared capture reuse
- when multiple entries from one source group are active, the backend must
  resolve them to one compatible grouped runtime or reject the newer request
- the public contract does not add dependency-specific source metadata until
  device investigation justifies it

Optional advanced channel disambiguation stays in the path:

```text
insightos://<host>/<device>/<stream-preset>/channel/<channel>
insightos://<host>/<device>/<stream-preset>/channel/<channel>/<delivery>
```

Discovery should normally emit the final full URI so users rarely type this
manually. The channel stays in the path rather than in a query param because it
identifies the exact stream, not an optional filter.

## Direct Session Contract

Direct sessions use the same exact canonical URI contract:

```json
{
  "input": "insightos://localhost/desk-rgbd/depth-400p_30/rtsp"
}
```

This flow is the basis for:

- `insightos-open`
- RTSP monitoring
- session-first workflows that later attach to app routes

Normal-use rule:

- the chosen canonical URI fixes the backend behavior for that direct session
- advanced grouped-device overrides are not part of the direct-session request

## Attach Existing Session Contract

Attach an already-running direct session to an app route:

```json
{
  "session_id": 42
}
```

Rules:

- the existing session must still be valid
- the existing session must satisfy the route expectation
- the durable app-source record stores the attached session relation as well as
  the resolved exact stream identity

## Runtime Rebind Contract

Replace the exact URI bound to one route:

```json
{
  "input": "insightos://localhost/desk-rgbd/depth-400p_30"
}
```

Rules:

- rebind validates the replacement before switching durable state
- rebind may stop obsolete route-owned runtime after the replacement succeeds
- rebind must preserve app and route identity

## Source Response Notes

App-source responses include:

- `route`
- `resolved_exact_stream_id`
- `resolved_source_variant_id`
- `resolved_source_group_id` when present
- `resolved_member_kind`
- `resolved_channel` when present
- `delivered_caps_json`
- `capture_policy_json`

The backend still uses the existing session graph under the routing layer. A
successful app-source connect creates one ordinary logical session and records
which resolved exact stream was connected to the route.

Current boundary:

- grouped-device dependency behavior remains documented through existing source
  group and capture policy metadata only
- tested Orbbec aligned depth confirms the API can keep one-stream delivery
  while relying on capture policy rather than a literal native `480p` depth
  profile lookup
- the same device did not expose a compatible `1280x720` D2C depth path and did
  not show a distinct aligned `1280x800` output, so the API should not invent
  those aligned variants for that unit

## Restart Behavior

- app, route, and source records are durable
- startup normalizes persisted source runtime state back to `stopped`
- `POST /api/apps/{id}/sources/{source_id}/start` creates a fresh runtime
  session for that persisted source intent
- the same durable pattern applies to attached-session routes, but restart must
  revalidate the referenced session rather than assuming the old runtime still
  exists
- the same durable pattern applies to logical sessions through
  `POST /api/sessions/{id}/start`
