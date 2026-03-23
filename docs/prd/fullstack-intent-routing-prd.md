# Full-Stack Intent Routing PRD

## Summary

InsightOS will be repositioned as a DB-first full-stack project for routing
discoverable media sources into app-defined processing targets. Users will keep
using the existing canonical source URI format:

```text
insightos://<host>/<device>/<preset>
insightos://<host>/<device>/<preset>/<delivery>
```

The major product change is at the app orchestration layer:

- apps declare targets by usage, not by public stream name
- users bind a listed URI to one target with `target: "<usage-name>"`
- the backend resolves the URI into the existing session model, validates that
  the source can satisfy the target contract, and routes the right stream set
  into the app

The project remains grounded on the current standalone contract:

- backend stays a CMake C++20 project under `backend/`
- backend remains an on-demand user service
- `insightos-open` remains a thin executable over reusable library logic
- public URI shape, HTTP mirror rules, local IPC transport, RTSP transport, and
  the capture/delivery session split stay intact

## Product Goals

1. Replace the runtime-only app registry with a durable app/target/source model
   backed by an explicit SQL schema.
2. Replace stream-name-based high-level app routing with target-based routing.
3. Give the frontend a first-class persistent application model instead of a
   thin session launcher.
4. Preserve the existing donor-grounded media engine, local IPC path, and RTSP
   path while changing orchestration and persistence.

## Non-Goals

- changing the canonical `insightos://` URI grammar
- changing HTTP mirror conversion rules
- changing the existing logical/capture/delivery session model
- replacing `memfd` + ring buffer local IPC with a different transport
- adding auth, cloud tenancy, or remote app-side media attach in this pass

## Personas

### Application Developer

Needs to declare processing targets such as `yolov5`, `yolov8`, `rgbd-view`,
and `orbbec-pointcloud`, then attach backend-routed frames without manually
managing device stream names.

### Operator / User

Needs to browse listed URIs, bind a source to a target from the frontend or
REST, start or stop it, and recover that configuration after backend restart.

## Interaction Baseline

The proposed `insight-io` experience is not a greenfield UX invention. It is a
reframing of the already-audited user interactions from the donor workflow into
a persistent target-routing product.

The baseline comes from three existing references:

- `demo_command.md`: full operator flow for build, runtime start, catalog
  inspection, aliasing, session creation, RTSP verification, restart, and
  recovery
- `demo_command_3min.md`: short operator flow for aliasing, mixed monitoring,
  `insightos-open`, restart, idle app injection, and rename edge cases
- `sdk/tests/app_integration_test.cpp`: test-level flow for idle app startup,
  later source injection, mixed-source attach, and callback selection behavior

In `insight-io`, those same interaction families remain, but the app-facing
portion changes shape:

- operator/runtime flows still begin with build, backend startup, health, and
  device catalog inspection
- device aliases still matter because they produce stable, human-usable
  canonical URIs
- direct session flows still exist for `insightos-open`, RTSP, AAC, restart,
  and low-level debugging
- app flows become target-based, so users bind `input + target` instead of
  picking a stream name
- idle apps still expose an `app_id`, but source injection now lands on
  declared targets such as `yolov5`, `yolov8`, `rgbd-view`, and
  `orbbec-pointcloud`

## Core User Flows

### 1. Create App And Targets

1. User creates an app.
2. User defines one or more targets.
3. Each target declares an input kind:
   - `video`
   - `audio`
   - `rgbd`

### 2. Bind Source URI To Target

1. User picks a listed canonical URI from the catalog.
2. User binds it to a target with:

```json
{
  "input": "insightos://localhost/desk-rgbd/480p_30/mjpeg",
  "target": "rgbd-view"
}
```

3. Backend normalizes the URI into the existing `SessionRequest`.
4. Backend validates target compatibility and creates one ordinary logical
   session.
5. Backend returns role bindings that tell the SDK how the source was mapped to
   the target.

### 3. Run Routed App Logic

1. App declares route-scoped stream callbacks.
2. SDK attaches using the existing `session_id + stream_name` IPC contract.
3. SDK surfaces route-aware callbacks while keeping the familiar stream-role
   callback shape.

## Target Contract Rules

### `video`

- binds `frame`
- if `frame` does not exist, binds `color`
- if neither exists, binds the first non-audio stream
- emits one role: `primary`

### `audio`

- binds `audio`
- emits one role: `audio`

### `rgbd`

- requires `color` and `depth`
- includes `ir` when present
- emits roles: `color`, `depth`, and optionally `ir`

## Public API Direction

### App APIs

- `POST /api/apps`
- `GET /api/apps`
- `GET /api/apps/{id}`
- `DELETE /api/apps/{id}`
- `POST /api/apps/{id}/targets`
- `GET /api/apps/{id}/targets`
- `DELETE /api/apps/{id}/targets/{target}`
- `POST /api/apps/{id}/sources`
- `GET /api/apps/{id}/sources`
- `POST /api/apps/{id}/sources/{source_id}/start`
- `POST /api/apps/{id}/sources/{source_id}/stop`

### Source Injection Request

```json
{
  "input": "insightos://localhost/front-camera/720p_30/mjpeg",
  "route": "yolov5"
}
```

### Source Response Requirements

- source identity
- canonical URI
- target name
- target kind
- source state
- last error
- binding list:
  - `role`
  - `stream_id`
  - `stream_name`
- embedded session metadata when a logical session exists

## SDK Direction

The high-level SDK should stay visually close to the current examples. The new
concept should be introduced one level above `on_stream(...)`, not by replacing
`Caps`, `Frame`, or the callback chain.

Recommended shape:

```cpp
insightos::App app;

app.route("yolov5")
    .on_stream("frame")
    .on_frame([](const insightos::Frame& frame) { /* video */ });

app.route("rgbd-view")
    .on_stream("color")
    .on_frame([](const insightos::Frame& frame) { /* color */ });

app.route("rgbd-view")
    .on_stream("depth")
    .on_frame([](const insightos::Frame& frame) { /* depth */ });
```

This keeps the current callback look:

- `on_stream("frame")`
- `on_stream("color")`
- `on_stream("depth")`
- `on_caps(const insightos::Caps&)`
- `on_frame(const insightos::Frame&)`
- `on_stop()`

Required SDK changes:

- `App::route(name)` as the new app-intent scope
- `RouteScope::on_stream(name)`
- `App::add_source(input, route)`
- optional `App::route(name).device(device_name).on_stream(name)` only if
  route-local device disambiguation is still needed

Compatibility direction:

- keep top-level `App::on_stream(...)` as an implicit default route
- keep `App(std::string source)` and `App::add_source(std::string source)` for
  single-source convenience
- add explicit multi-source forms that bind URIs to routes

Recommended CLI behavior:

- single-route apps should still launch with one URI argument
- multi-route apps should accept named route bindings such as
  `yolov5=insightos://...`
- bare positional URI arguments may be accepted only when the number and order
  of declared routes make the binding unambiguous

## Success Criteria

### Product

- users no longer manage public stream names when binding app sources
- apps no longer register high-level handlers by stream name
- backend restart preserves apps, targets, and sources, while runtime state is
  normalized back to `stopped`

### Technical

- schema is defined in SQL migrations, not only inline C++
- app/target/source persistence is DB-backed
- existing logical/capture/delivery session reuse remains intact
- feature tracker exists and can be updated mechanically as implementation
  advances
