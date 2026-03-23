# Full-Stack Intent Routing PRD

## Summary

`insight-io` is a DB-first route-based project for routing discoverable exact
stream URIs into app-defined routes.

The public URI shape remains `insightos://`, but the canonical contract is now
explicitly exact-stream oriented:

```text
insightos://<host>/<device>/<stream-preset>
insightos://<host>/<device>/<stream-preset>/<delivery>
insightos://<host>/<device>/<stream-preset>/channel/<channel>
insightos://<host>/<device>/<stream-preset>/channel/<channel>/<delivery>
```

Examples:

- `insightos://localhost/front-camera/video-720p_30/mjpeg`
- `insightos://localhost/desk-rgbd/color-480p_30`
- `insightos://localhost/desk-rgbd/depth-400p_30`
- `insightos://localhost/desk-rgbd/depth-480p_30`
- `insightos://localhost/stereo-cam/video-720p_30/channel/left`
- `insightos://localhost/stereo-cam/video-720p_30/channel/right`

The key product rules are:

- discovery publishes the exact canonical URIs that users and apps copy
- the app declares routes by purpose, not by runtime stream name
- one canonical URI maps to one delivered stream
- route expectations validate compatibility; they do not choose hidden stream
  variants
- related exact URIs may belong to the same source group
- if backend processing changes delivered caps, discovery must expose separate
  user-visible stream choices rather than hiding that difference behind route
  policy
- in normal use, grouped-device runtime behavior is fixed by the discovered
  catalog entry; users choose a different URI rather than overriding capture
  policy at bind time
- identical canonical URIs may fan out to multiple consumers through reuse
- different delivery suffixes such as `/mjpeg` and `/rtsp` create distinct
  delivery sessions while still being eligible for shared capture reuse

One main product objective is to mask heterogeneous hardware details, such as
D2C on/off behavior, from users. That includes LLM-assisted developers who
should be able to build and reuse audio/video apps without learning per-device
quirks first.

## Product Goals

1. Replace the runtime-only app registry with a durable app/route/source model
   backed by an explicit SQL schema.
2. Replace stream-name-first high-level app routing with purpose-first route
   routing.
3. Make the copied URI itself exact enough that one URI always means one
   delivered stream.
4. Mask heterogeneous hardware details from users and LLM-assisted app builders
   by moving device-specific choices into discovery-visible exact stream
   selection.
5. Preserve the existing session graph, reuse semantics, and inspectable media
   runtime underneath the app layer.
6. Cover the full lifecycle: discovery, direct sessions, app routing, reverse
   attach, reuse, reroute, restart, inspection, and stop.

## Non-Goals

- changing the existing logical/capture/delivery session model
- replacing `memfd` + ring buffer local IPC with a different transport
- adding auth, cloud tenancy, or remote app-side media attach in this pass
- making applications name runtime stream ids directly in normal route
  declaration
- hiding materially different delivered depth modes behind one route-level
  alignment toggle

## Personas

### Application Developer

Needs to declare processing routes such as `yolov5`, `orbbec-color`,
`orbbec-depth`, `stereo-left-detector`, or `stereo-right-detector`, then attach
backend-routed frames without managing runtime stream names directly.

### Operator / User

Needs to browse listed exact URIs, pick the right stream for the task, use that
URI through direct-session tools or app routes, inspect runtime state, reroute
when needed, and recover durable configuration after backend restart.

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
- discovery must list the exact stream choices up front, including distinct
  depth outputs when delivered caps differ
- app flows become route-based, so users connect `input + route`
- apps no longer register runtime stream names directly in route declarations
- the exact URI is authoritative for stream identity; route expectations only
  validate that choice
- dual-eye or stereo disambiguation may use an optional `/channel/<name>`
  suffix, but discovery should normally emit the full exact URI so users rarely
  type it manually

## Core User Flows

### 1. Discover Devices And Exact URIs

1. User starts the backend and checks health.
2. User lists discovered devices.
3. User inspects the catalog entries for one device.
4. Discovery returns one exact URI per delivered stream choice.

Examples:

- `color-480p_30`
- `depth-400p_30`
- `depth-480p_30`
- `video-720p_30/channel/left`
- `video-720p_30/channel/right`

For RGBD depth, the delivered shape is discovery-visible:

- `depth-400p_30` means native depth output
- `depth-480p_30` means aligned depth output

The user chooses between those outputs directly instead of toggling D2C
implicitly through the route contract.

Current design boundary:

- `depth-480p_30` remains one delivered depth stream
- on the tested Orbbec device, aligned `480p` depth was produced from a
  depth-only request with forced D2C and no delivered color frames
- the same device exposed no compatible `1280x720` D2C depth path and no
  distinct aligned `1280x800` depth output
- backend planning for `depth-480p_30` must therefore be capture-policy-driven,
  not a literal search for a native `480p` depth sensor profile
- discovery should therefore keep `depth-480p_30` as the special aligned choice
  on that device, avoid inventing `depth-720p_30`, and treat `depth-800p_30`
  as native depth unless future evidence proves otherwise
- if extra explanation is useful, discovery may show a short operator-facing
  comment on unusual entries such as `depth-480p_30`; that note is informative
  only and does not introduce new dependency-specific fields

### 2. Create Direct Sessions

1. User takes one exact URI from the catalog.
2. User starts capture either with `insightos-open` or `POST /api/sessions`.
3. Backend normalizes the URI into `SessionRequest`.
4. Backend creates or reuses capture and delivery runtime as appropriate.
5. User monitors RTSP or local IPC output and later stops the session.

### 3. Create App And Routes

1. User creates an app.
2. User defines one or more routes.
3. Each route declares purpose and optional semantic expectations.

Examples:

- `yolov5` expects video
- `orbbec-depth` expects depth
- `stereo-left-detector` expects video from the `left` channel

### 4. Start App First, Then Connect Exact URI To Route

1. User picks one listed canonical URI from the catalog.
2. User connects it to one route with:

```json
{
  "input": "insightos://localhost/front-camera/video-720p_30/mjpeg",
  "route": "yolov5"
}
```

or:

```json
{
  "input": "insightos://localhost/desk-rgbd/depth-480p_30",
  "route": "orbbec-depth"
}
```

3. Backend normalizes the URI into the existing `SessionRequest`.
4. Backend resolves one concrete exact stream identity from the URI itself.
5. Backend validates the route expectations against the resolved source
   metadata.
6. Backend creates one ordinary logical session.
7. Backend returns the resolved source identity and source-group metadata to
   the SDK.

### 5. Start Stream First, Then Attach It To A Route

1. User starts a direct session first through CLI or REST.
2. User later creates or finds an app and route.
3. User attaches the existing logical session to that route with `session_id`.
4. Backend validates that the existing session is compatible with the route.
5. SDK attaches to the already-running stream without forcing the user to stop
   and recreate it first.

### 6. Fan-Out And Delivery Divergence

1. The same exact URI may be used in multiple places, including multiple routes
   across one or more apps.
2. When the canonical URI is identical, the runtime should reuse the same
   delivery session when possible so all consumers see the same frame sequence.
3. When URIs differ only by delivery, such as `/mjpeg` versus `/rtsp`, they
   should remain separate delivery sessions while still being eligible for
   shared capture reuse.

### 7. Run Routed App Logic

1. App declares route-scoped callbacks.
2. SDK attaches using the existing `session_id + stream_name` IPC contract.
3. SDK surfaces one callback chain per route.

### 8. Inspect Runtime And Session State

1. User inspects `GET /api/status` for shared capture and delivery reuse.
2. User inspects `GET /api/sessions` and `GET /api/sessions/{id}` for direct
   runtime state.
3. User inspects `GET /api/apps/{id}/sources` for route bindings, latest
   session ids, and route-level errors.

### 9. Change Routing At Runtime

1. User replaces the URI bound to one route without deleting the app.
2. User may also attach a different existing logical session to that route.
3. Backend validates the replacement, updates the durable binding, and stops
   obsolete route-owned runtime when appropriate.

### 10. Stop Capture

1. User stops a direct session or app-owned source.
2. Durable app and route declarations remain.
3. Restart later creates a fresh runtime session rather than reviving stale
   OS handles.

### 11. Grouped Runtime Rule

1. Some exact URIs are independent. Others belong to grouped devices such as
   RGBD or stereo hardware.
2. One canonical URI still delivers one stream, but multiple active URIs from
   the same source group may need one compatible grouped backend mode.
3. When grouped URIs are compatible, the session manager should resolve them to
   one compatible grouped runtime.
4. When grouped URIs would require conflicting runtime behavior, the backend
   should reject the newer request instead of silently changing what an already
   selected canonical URI means.
5. Normal use does not expose a bind-time override for grouped capture policy.
   Users select a different discovered URI instead.

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

Baseline rule:

- non-debug routes should declare `media`

Examples:

- `yolov5`:
  - `media = video`
- `orbbec-depth`:
  - `media = depth`
- `stereo-left-detector`:
  - `media = video`
  - `channel = left`

Important rule:

- route expectations reject incompatible URIs
- non-debug routes without `media` are underspecified and should be avoided
- route expectations do not auto-upgrade `depth-400p_30` into
  `depth-480p_30`, or infer `left` versus `right`, behind the user’s back
- if delivered caps differ, that difference must already be visible in the
  discovery catalog and canonical URI choice

## Public API Direction

### Device APIs

- `GET /api/health`
- `GET /api/devices`
- `GET /api/devices/{device}`
- `POST /api/devices/{device}/alias`

### Direct Session APIs

- `POST /api/sessions`
- `GET /api/sessions`
- `GET /api/sessions/{id}`
- `POST /api/sessions/{id}/start`
- `POST /api/sessions/{id}/stop`
- `DELETE /api/sessions/{id}`

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
- `POST /api/apps/{id}/sources/{source_id}/rebind`
- `POST /api/apps/{id}/routes/{route}/attach-session`

### Route Creation Request

```json
{
  "route_name": "yolov5",
  "expect": {
    "media": "video"
  }
}
```

Example depth route request:

```json
{
  "route_name": "orbbec-depth",
  "expect": {
    "media": "depth"
  }
}
```

### Source Connection Request

```json
{
  "input": "insightos://localhost/front-camera/video-720p_30/mjpeg",
  "route": "yolov5"
}
```

### Attach Existing Session Request

```json
{
  "session_id": 42
}
```

### Source Response Requirements

- source identity
- canonical URI
- route name
- resolved stream id
- resolved source variant id
- resolved member kind
- resolved channel when present
- resolved source group id when present
- delivered caps
- capture policy metadata
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

app.route("orbbec-color")
    .expect(insightos::Video{})
    .on_frame(handle_color);

app.route("orbbec-depth")
    .expect(insightos::Depth{})
    .on_frame(handle_depth);

app.join("rgbd")
    .inputs("orbbec-color", "orbbec-depth")
    .on_frames(handle_rgbd);

app.pair("orbbec-color", "orbbec-depth")
    .on_frames(handle_rgbd);
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
- `App::join(name)` for a named multi-route join helper
- `App::pair(left_route, right_route)` as two-input sugar over `join`
- `JoinScope::inputs(route_a, route_b, ...)`
- `JoinScope::on_frames(...)`
- `App::connect(route_name, input)` for explicit startup source connection
- `App::attach(route_name, session_id)` for reverse-order binding to an already
  running direct session

Expected joined behavior:

- routes are still declared and connected independently
- `join()` and `pair()` are SDK helpers above ordinary routes, not new source
  identities
- apps can set up generic `video` and `depth` routes without prior hardware
  knowledge, then join them by route name later
- the joined callback waits until each named route has an active source
- the joined callback emits when frames from the named routes are close enough
  in `pts_ns`, using a default tolerance of one frame interval
- if matching frames are not available, ordinary per-route callbacks still fire
  independently
- `pair(a, b)` is shorthand for a two-input `join()`

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
  yolov5=insightos://localhost/front-camera/video-720p_30/mjpeg \
  orbbec-color=insightos://localhost/desk-rgbd/color-480p_30 \
  orbbec-depth=insightos://localhost/desk-rgbd/depth-480p_30
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
- one canonical URI means one delivered stream
- discovery exposes 400p and 480p depth as separate user-visible choices when
  D2C changes delivered caps
- optional `/channel/<name>` exists for channel-disambiguated devices, but most
  users consume the fully generated discovery URI without hand-editing it
- dependent sources can be related through source-group metadata
- grouped sources either share one compatible grouped runtime or reject with a
  compatibility error
- direct-session-first and app-first flows are both supported
- identical exact URIs can be reused safely across multiple consumers
- different delivery suffixes remain distinct delivery sessions
- backend restart preserves apps, routes, and sources while runtime state is
  normalized back to `stopped`

### Technical

- schema is defined in SQL migrations, not only inline C++
- app/route/source persistence is DB-backed
- grouped-source metadata and channel distinctions are preserved without
  leaking hardware pairing policy into the public route contract
- normal use does not change grouped capture policy at bind time; a different
  behavior must come from a different discovered URI
- capture policy may legitimately map delivered caps to a different underlying
  native sensor profile, as in the tested Orbbec `depth-480p_30` case
- discovery does not synthesize aligned variants that the device has not proven,
  such as `depth-720p_30` on the tested Orbbec unit
- non-debug routes declare enough expectation metadata to reject obvious
  misroutes such as depth into a video detector
- the resolved exact stream identity is persisted explicitly enough to survive
  restart without ambiguity
- existing logical/capture/delivery session reuse remains intact
- feature tracker exists and can be updated mechanically as implementation
  advances
