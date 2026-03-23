# Full-Stack Intent Routing PRD

## Summary

`insight-io` is a DB-first full-stack project for routing discoverable source
URIs into app-defined routes. The public URI format remains:

```text
insightos://<host>/<device>/<preset>
insightos://<host>/<device>/<preset>/<delivery>
```

When a preset exposes multiple related sources, the URI stays readable by
keeping the device alias and preset in the path and using a selector in query
parameters:

```text
insightos://<host>/<device>/<preset>?source=<member>
```

Examples:

- `insightos://localhost/front-camera/720p_30/mjpeg`
- `insightos://localhost/desk-rgbd/480p_30?source=color`
- `insightos://localhost/desk-rgbd/480p_30?source=depth`
- `insightos://localhost/stereo-cam/720p_30?source=left`
- `insightos://localhost/stereo-cam/720p_30?source=right`

The key product rules are:

- the app declares routes by purpose, not by runtime stream name
- one connected URI feeds one route
- related URIs may belong to the same source group
- group-level policies such as D2C remain backend concerns, not app callback
  vocabulary

## Product Goals

1. Replace the runtime-only app registry with a durable app/route/source model
   backed by an explicit SQL schema.
2. Replace stream-name-first high-level app routing with purpose-first route
   routing.
3. Keep the public URI readable while allowing source selection for dependent
   or multi-channel devices.
4. Give the frontend a first-class persistent application model instead of a
   thin session launcher.

## Non-Goals

- changing the canonical `insightos://` base grammar
- changing HTTP mirror conversion rules
- changing the existing logical/capture/delivery session model
- replacing `memfd` + ring buffer local IPC with a different transport
- adding auth, cloud tenancy, or remote app-side media attach in this pass
- making applications name runtime stream ids directly in normal route
  declaration

## Personas

### Application Developer

Needs to declare processing routes such as `yolov5`, `scene-color`,
`scene-depth`, `stereo-left-detector`, or `stereo-right-detector`, then attach
backend-routed frames without managing runtime stream names directly.

### Operator / User

Needs to browse listed URIs, connect one URI to one route from the frontend or
REST, start or stop it, and recover that configuration after backend restart.

## Interaction Baseline

The proposed `insight-io` experience is a reframing of the already-audited
user interactions from the donor workflow into a persistent route-based
product.

The baseline comes from:

- `demo_command.md`
- `demo_command_3min.md`
- `sdk/tests/app_integration_test.cpp`

In `insight-io`, those same interaction families remain, but the app-facing
portion changes shape:

- operator/runtime flows still begin with build, backend startup, health, and
  device catalog inspection
- device aliases still matter because they produce stable, human-usable base
  URIs
- direct session flows still exist for `insightos-open`, RTSP, AAC, restart,
  and low-level debugging
- app flows become route-based, so users connect `input + route`
- apps no longer register runtime stream names directly in route declarations
- when a device exposes related sources, the catalog lists them as separate URI
  options sharing one readable device alias and preset

## Core User Flows

### 1. Create App And Routes

1. User creates an app.
2. User defines one or more routes.
3. Each route declares purpose and optional semantic expectations.

Examples:

- `yolov5` expects video
- `scene-depth` expects depth
- `scene-depth` may also require the same source group as `scene-color`
- `stereo-left-detector` expects video from the `left` channel

### 2. Connect Source URI To Route

1. User picks one listed canonical URI from the catalog.
2. User connects it to one route with:

```json
{
  "input": "insightos://localhost/front-camera/720p_30/mjpeg",
  "route": "yolov5"
}
```

or:

```json
{
  "input": "insightos://localhost/desk-rgbd/480p_30?source=depth",
  "route": "scene-depth"
}
```

3. Backend normalizes the URI into the existing `SessionRequest`.
4. Backend resolves the source member selected by the URI.
5. Backend validates the route expectations against the resolved source
   metadata.
6. Backend creates one ordinary logical session.
7. Backend returns the resolved source identity and source-group metadata to
   the SDK.

### 3. Run Routed App Logic

1. App declares route-scoped callbacks.
2. SDK attaches using the existing `session_id + stream_name` IPC contract.
3. SDK surfaces one callback chain per route.

## Route Expectation Model

Route declarations should stay purpose-first, but the system still needs
machine-checkable expectations so obvious routing mistakes can be rejected.

Recommended expectation shape:

- media kind:
  - `video`
  - `audio`
  - `depth`
  - `infrared`
- optional channel constraint:
  - `left`
  - `right`
- optional source-group constraint:
  - `same_group_as`
- optional alignment constraint:
  - `alignment_required`

Examples:

- `yolov5`:
  - `media = video`
- `scene-depth`:
  - `media = depth`
  - `same_group_as = scene-color`
  - `alignment_required = true`
- `stereo-left-detector`:
  - `media = video`
  - `channel = left`

## Public API Direction

### App APIs

- `POST /api/apps`
- `GET /api/apps`
- `GET /api/apps/{id}`
- `DELETE /api/apps/{id}`
- `POST /api/apps/{id}/routes`
- `GET /api/apps/{id}/routes`
- `DELETE /api/apps/{id}/routes/{route}`
- `POST /api/apps/{id}/sources`
- `GET /api/apps/{id}/sources`
- `POST /api/apps/{id}/sources/{source_id}/start`
- `POST /api/apps/{id}/sources/{source_id}/stop`

### Route Creation Request

```json
{
  "route_name": "yolov5",
  "expect": {
    "media": "video"
  }
}
```

Example with dependent-source constraints:

```json
{
  "route_name": "scene-depth",
  "expect": {
    "media": "depth",
    "same_group_as": "scene-color",
    "alignment_required": true
  }
}
```

### Source Connection Request

```json
{
  "input": "insightos://localhost/front-camera/720p_30/mjpeg",
  "route": "yolov5"
}
```

### Source Response Requirements

- source identity
- canonical URI
- route name
- resolved source id
- resolved source member
- resolved source group id when present
- source state
- last error
- embedded session metadata when a logical session exists

## SDK Direction

The high-level SDK should no longer expose stream-name selection in the normal
route-based API. A route represents one connected source path for one purpose.

Recommended shape:

```cpp
insightos::App app;

app.route("yolov5")
    .expect(insightos::Video{})
    .on_caps([](const insightos::Caps& caps) { /* video */ })
    .on_frame([](const insightos::Frame& frame) { /* video */ });

app.route("scene-color")
    .expect(insightos::Video{})
    .on_frame(handle_color);

app.route("scene-depth")
    .expect(insightos::Depth{}
                .same_group_as("scene-color")
                .require_alignment())
    .on_frame(handle_depth);
```

This keeps the callback chain compact:

- `route(name)`
- `expect(...)`
- `on_caps(...)`
- `on_frame(...)`
- `on_stop()`

Required SDK changes:

- `App::route(name)` as the route scope
- `RouteScope::expect(expectation)`
- `RouteScope::on_caps(...)`
- `RouteScope::on_frame(...)`
- `RouteScope::on_stop(...)`
- `App::connect(route_name, input)` for explicit startup source connection

For command-line startup:

1. App declares its routes in code.
2. The CLI parser reads argv after route declaration.
3. If exactly one route exists, one bare URI argument connects to that route.
4. If more than one route exists, each startup source connection must be
   spelled `route_name=insightos://...`.
5. Unknown route names fail before backend interaction.
6. Duplicate route connections fail before backend interaction.
7. Missing routes leave the app idle; they do not guess a fallback.

Example:

```bash
./build/bin/multi_route_app \
  yolov5=insightos://localhost/front-camera/720p_30/mjpeg \
  scene-color='insightos://localhost/desk-rgbd/480p_30?source=color' \
  scene-depth='insightos://localhost/desk-rgbd/480p_30?source=depth'
```

Backend Handshake:

1. SDK creates the app record with `POST /api/apps`.
2. SDK declares the full route manifest before connecting any sources.
3. SDK validates the CLI route connections against that declared route
   manifest.
4. SDK posts one source-connect request per startup route connection.
5. SDK fetches the resolved source records.
6. SDK attaches through the existing `session_id + stream_name` IPC contract.
7. If one startup route connection fails, the SDK reports which route failed
   and keeps the app process alive unless the app explicitly requested
   fail-fast startup.

## Success Criteria

### Product

- users no longer manage runtime stream names when declaring routes
- one connected URI feeds one route
- dependent sources can be related through source-group metadata
- backend restart preserves apps, routes, and sources while runtime state is
  normalized back to `stopped`

### Technical

- schema is defined in SQL migrations, not only inline C++
- app/route/source persistence is DB-backed
- related-source constraints such as `same_group_as` and alignment are
  expressible
- existing logical/capture/delivery session reuse remains intact
- feature tracker exists and can be updated mechanically as implementation
  advances
