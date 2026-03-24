# Full-Stack Intent Routing PRD

## Role

- role: product contract for the DB-first intent-routing rebuild
- status: active
- version: 3
- major changes:
  - 2026-03-24 made `delivery_name` inferred during normalization rather than
    client-posted
  - 2026-03-24 made public `uri` values derived source identifiers and moved
    `delivery_name` into durable bind/session intent
  - 2026-03-24 unified URI-backed and session-backed app binds under the
    `POST /api/apps/{id}/sources` surface
  - 2026-03-24 clarified local SDK attach stays IPC-only in v1 while future
    remote or LAN RTSP consumption remains a separate path
- past tasks:
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

## Summary

`insight-io` is a DB-first route-based project for routing discoverable source
URIs into app-defined routes and direct sessions.

The public URI shape remains `insightos://`, and the URI contract is now
selector-oriented:

```text
insightos://<host>/<device>/<selector>
insightos://<host>/<device>/<selector>/channel/<channel>
```

Examples:

- `insightos://localhost/front-camera/video-720p_30`
- `insightos://localhost/desk-rgbd/orbbec/color/480p_30`
- `insightos://localhost/desk-rgbd/orbbec/depth/400p_30`
- `insightos://localhost/desk-rgbd/orbbec/depth/480p_30`
- `insightos://localhost/desk-rgbd/orbbec/preset/480p_30`
- `insightos://localhost/stereo-cam/video-720p_30/channel/left`
- `insightos://localhost/stereo-cam/video-720p_30/channel/right`

The key product rules are:

- discovery publishes the exact derived URIs that users and apps copy
- the app declares routes by purpose, not by runtime stream name
- one derived URI selects one fixed catalog-published source shape
- delivery is durable bind/session intent, not part of source identity
- delivery is inferred from source locality and scheme, then stored durably
- route expectations validate compatibility; they do not choose hidden stream
  variants
- exact-member URIs still map to one delivered stream
- grouped preset URIs may map to one fixed related stream bundle
- related URIs may belong to the same source group
- discovery publishes source-shape choices and metadata; sessions realize them
  at runtime
- if backend processing changes delivered caps, discovery must expose separate
  user-visible stream choices rather than hiding that difference behind route
  policy
- in normal use, grouped-device runtime behavior is fixed by the discovered
  catalog entry; users choose a different URI rather than overriding capture
  policy at bind time
- identical URI plus delivery intent combinations may fan out to multiple
  consumers through reuse
- different delivery intents such as `ipc` and `rtsp` create distinct delivery
  sessions while still being eligible for shared capture reuse
- local SDK attach always uses IPC in v1
- future remote or LAN RTSP consumption remains planned, but it is not part of
  the v1 SDK attach contract

One main product objective is to mask heterogeneous hardware details, such as
D2C on/off behavior, from users. That includes LLM-assisted developers who
should be able to build and reuse audio/video apps without learning per-device
quirks first.

## Product Goals

1. Replace the runtime-only app registry with a durable app/route/source model
   backed by an explicit SQL schema.
2. Replace stream-name-first high-level app routing with purpose-first route
   routing.
3. Make the copied URI exact enough that one URI always means one fixed
   published source shape.
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
- reintroducing a separate SDK-only frame-merge helper layer instead of making
  grouped preset routing first-class

## Personas

### Application Developer

Needs to declare processing routes such as `yolov5`, `orbbec/color`,
`orbbec/depth`, `stereo/left-detector`, or `stereo/right-detector`, then attach
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
- discovery must list exact member choices up front, and may also list fixed
  grouped preset choices when the delivered member set is stable and proven
- app flows become route-based, so users connect `input + route` or attach
  `session_id` to a route or grouped target
- apps no longer register runtime stream names directly in route declarations
- the source URI is authoritative for source identity; route expectations only
  validate that choice
- delivery intent is selected separately from URI identity
- dual-eye or stereo disambiguation may use an optional `/channel/<name>`
  suffix, but discovery should normally emit the full exact URI so users rarely
  type it manually

## Core User Flows

### 1. Discover Devices And Exact URIs

1. User starts the backend and checks health.
2. User lists discovered devices.
3. User inspects the catalog entries for one device.
4. Discovery returns exact member URIs and, when supported, fixed grouped
   preset URIs.

Examples:

- `orbbec/color/480p_30`
- `orbbec/depth/400p_30`
- `orbbec/depth/480p_30`
- `orbbec/preset/480p_30`
- `video-720p_30/channel/left`
- `video-720p_30/channel/right`

For RGBD depth, the delivered shape is discovery-visible:

- `orbbec/depth/400p_30` means native depth output
- `orbbec/depth/480p_30` means aligned depth output
- `orbbec/preset/480p_30` means the proven bundled color + aligned-depth
  output for the 480p family

The user chooses between those outputs directly instead of toggling D2C
implicitly through the route contract.

Current design boundary:

- `orbbec/depth/480p_30` remains one delivered depth stream
- on the tested Orbbec device, aligned `480p` depth was produced from a
  depth-only request with forced D2C and no delivered color frames
- the same device also proved that one grouped RGBD request can deliver color
  plus aligned depth together through one stable preset flow
- the same device exposed no compatible `1280x720` D2C depth path and no
  distinct aligned `1280x800` depth output
- backend planning for `orbbec/depth/480p_30` must therefore be
  capture-policy-driven, not a literal search for a native `480p` depth sensor
  profile
- discovery should therefore keep `orbbec/depth/480p_30` as the special aligned choice
  on that device, avoid inventing `depth-720p_30`, and treat `depth-800p_30`
  as native depth unless future evidence proves otherwise
- discovery may additionally publish `orbbec/preset/480p_30` as the fixed
  bundled color + aligned-depth choice because that exact grouped behavior was
  proven end to end on the sibling `insightos` stack
- if extra explanation is useful, discovery may show a short operator-facing
  comment on unusual entries such as `orbbec/depth/480p_30` or
  `orbbec/preset/480p_30`; that note is informative only and does not
  introduce new dependency-specific fields

### 2. Create Direct Sessions

1. User takes one selected URI from the catalog.
2. User starts capture either with `insightos-open` or `POST /api/sessions`.
3. Backend normalizes the URI into `SessionRequest`.
4. Backend infers `delivery_name` from the selected source address and creates
   or reuses capture and delivery runtime accordingly.
5. User monitors RTSP or local IPC output and later stops the session.

### 3. Create App And Routes

1. User creates an app.
2. User defines one or more routes.
3. Each route declares purpose and optional semantic expectations.

Examples:

- `yolov5` expects video
- `orbbec/depth` expects depth
- `stereo/left-detector` expects video from the `left` channel

### 4. Start App First, Then Connect Source URI To Route Intent

1. User picks one listed URI from the catalog.
2. For one exact member URI, user connects it to one route with:

```json
{
  "input": "insightos://localhost/front-camera/video-720p_30",
  "route": "yolov5"
}
```

or:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/480p_30",
  "route": "orbbec/depth"
}
```

3. For one grouped preset URI, user may connect it to a grouped route target with:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
  "route_grouped": "orbbec"
}
```

4. Backend normalizes the URI and inferred `delivery_name` into the existing
   `SessionRequest`.
5. Backend resolves either:
   - one concrete exact stream identity from the URI itself, or
   - one concrete preset member set from the URI itself
6. Backend validates route expectations against the resolved source metadata.
7. Backend creates one ordinary logical session or one grouped logical session.
8. Backend returns the resolved source identity, member bindings when grouped,
   and source-group metadata to the SDK.

V1 boundary:

- app-source binds do not accept the old RTSP-qualified `insightos://.../rtsp`
  form
- locally resolved `insightos://` app binds infer `ipc`
- non-local or `rtsp://` app binds would infer `rtsp`, but local SDK attach is
  still IPC-only in v1
- a local SDK app therefore still needs an IPC-capable `active_session_id` or
  the bind must be rejected
- future remote or LAN RTSP app consumption can reuse the same resource shape
  without making `delivery_name` client-posted again

### 5. Start Stream First, Then Attach It To A Route

1. User starts a direct session first through CLI or REST.
2. User later creates or finds an app target.
3. User posts one app-source bind using `session_id` plus either `route` or
   `route_grouped`.
4. Backend validates that the existing session is compatible with that target.
5. In v1, the referenced session must remain IPC-capable because local SDK
   attach still uses IPC.
6. Grouped preset sessions may attach through `route_grouped`, so one app can
   start from `orbbec/preset/...` without managing `/color` and `/depth`
   individually.
7. The bind keeps both:
   - `source_session_id` for the session the user chose
   - `active_session_id` for the session actually serving the app

### 6. Fan-Out And Delivery Divergence

1. The same exact URI may be used in multiple places, including multiple routes
   across one or more apps.
2. When the URI and inferred `delivery_name` are identical, the runtime should reuse
   the same delivery session when possible so all consumers see the same frame
   sequence.
3. When the URI is identical but inferred `delivery_name` differs, such as `ipc` versus
   `rtsp`, those requests should remain separate delivery sessions while still
   being eligible for shared capture reuse.

### 7. Run Routed App Logic

1. App declares callbacks on named routes.
2. SDK attaches using the existing IPC contract.
3. SDK surfaces one callback chain per route.

### 8. Inspect Runtime And Session State

1. User inspects `GET /api/status` for shared capture and delivery reuse.
2. User inspects `GET /api/sessions` and `GET /api/sessions/{id}` for direct
   runtime state.
3. User inspects `GET /api/apps/{id}/sources` for route bindings, latest
   session ids, and route-level errors.

### 9. Change Routing At Runtime

1. User replaces the URI bound to one route or grouped target without deleting
   the app.
2. User may also attach a different existing logical session to one route or
   grouped target.
3. Backend validates the replacement, updates the durable binding, and stops
   obsolete app-owned runtime when appropriate.

### 10. Stop Capture

1. User stops a direct session or app-owned source.
2. Durable app and route declarations remain.
3. Restart later creates a fresh runtime session rather than reviving stale
   OS handles.

### 11. Grouped Runtime Rule

1. Some exact URIs are independent. Others belong to grouped devices such as
   RGBD or stereo hardware.
2. One URI still means one fixed published source shape, but multiple
   active URIs from the same source group may need one compatible grouped
   backend mode.
3. When grouped URIs are compatible, the session manager should resolve them to
   one compatible grouped runtime.
4. When grouped URIs would require conflicting runtime behavior, the backend
   should reject the newer request instead of silently changing what an already
   selected URI means.
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
- `orbbec/depth`:
  - `media = depth`
- `stereo/left-detector`:
  - `media = video`
  - `channel = left`

Important rule:

- route expectations reject incompatible URIs
- non-debug routes without `media` are underspecified and should be avoided
- route expectations do not auto-upgrade `orbbec/depth/400p_30` into
  `orbbec/depth/480p_30`, infer `left` versus `right`, or silently expand an
  exact member URI into a grouped preset behind the user’s back
- if delivered caps differ, that difference must already be visible in the
  discovery catalog and URI choice

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
  "route_name": "orbbec/depth",
  "expect": {
    "media": "depth"
  }
}
```

### Source Connection Request

```json
{
  "input": "insightos://localhost/front-camera/video-720p_30",
  "route": "yolov5"
}
```

URI-backed grouped request:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
  "route_grouped": "orbbec"
}
```

Session-backed exact request:

```json
{
  "session_id": 42,
  "route": "yolov5"
}
```

Session-backed grouped request:

```json
{
  "session_id": 43,
  "route_grouped": "orbbec"
}
```

### Source Response Requirements

- source identity
- derived URI
- delivery name
- source kind
- source session id when session-backed
- route name
- grouped route target when grouped
- resolved stream id
- resolved source variant id
- resolved member kind
- resolved channel when present
- resolved source group id when present
- resolved member mappings when grouped
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

app.route("orbbec/color")
    .expect(insightos::Video{})
    .on_frame(handle_color);

app.route("orbbec/depth")
    .expect(insightos::Depth{})
    .on_frame(handle_depth);

app.connect("yolov5", "insightos://localhost/front-camera/video-720p_30");

app.connect_grouped(
    "orbbec",
    "insightos://localhost/desk-rgbd/orbbec/preset/480p_30");
```

This keeps the callback chain compact:

- `route(name)`
- `expect(...)`
- `on_caps(...)`
- `on_frame(...)`
- `on_stop()`

Required SDK changes:

- `App::route(name)` returns one `AppRoute` declaration
- `AppRoute::expect(expectation)`
- `AppRoute::on_caps(...)`
- `AppRoute::on_frame(...)`
- `AppRoute::on_stop(...)`
- `App::connect(route_name, input)` for explicit startup source connection
- `App::connect_grouped(route_grouped, input)` for grouped preset startup
  connection
- `App::attach(route_name, session_id)` for reverse-order binding to an already
  running direct session
- `App::attach_grouped(route_grouped, session_id)` for reverse-order grouped
  binding to an already running grouped direct session

The SDK methods are thin conveniences over the same app-source REST surface:

- `connect(...)` and `connect_grouped(...)` create URI-backed app-source binds
- `attach(...)` and `attach_grouped(...)` create session-backed app-source
  binds
- the REST surface exists so operators and external controllers can drive a
  running app without linking the SDK
- local SDK attach remains IPC-only in v1 even when the selected session came
  from a session-first flow

Why `source_session_id` and `active_session_id` both matter:

- `source_session_id` preserves the upstream session the user pointed at
- `active_session_id` captures the session that is actually serving the app now
- they are equal for direct IPC attach
- they may diverge when a future RTSP-backed or restarted serving path is
  materialized separately from the selected upstream session

Expected grouped preset behavior:

- routes are still declared independently with ordinary `route(...).expect(...)`
  chains
- apps can set up generic `video` and `depth` routes without prior hardware
  knowledge, then bind one grouped preset URI to a grouped route target such as
  `orbbec`
- the grouped preset bind fans out only to declared routes under that grouped
  target
  whose expectations match the resolved member metadata
- ordinary per-route callbacks still fire independently
- the grouped preset URI itself remains catalog-published and does not require
  the app to spell pairing or alignment policy in code

For command-line startup:

1. App declares its routes in code.
2. The CLI parser reads argv after route declaration.
3. If exactly one startup target exists, whether one route or one grouped
   target, one bare URI argument connects to that target.
4. A grouped-route app that only exposes one grouped target such as `orbbec`
   may therefore start from one bare grouped preset URI.
5. If more than one startup target exists, each startup source connection must
   be spelled `target=insightos://...`, where `target` is either a route name
   or a grouped route target.
6. Unknown route names fail before backend interaction.
7. Duplicate route connections fail before backend interaction.
8. Missing routes leave the app idle; they do not guess a fallback.

Example:

```bash
./build/bin/multi_route_app \
  yolov5=insightos://localhost/front-camera/video-720p_30 \
  orbbec=insightos://localhost/desk-rgbd/orbbec/preset/480p_30
```

Backend Handshake:

1. SDK creates the app record with `POST /api/apps`.
2. SDK declares the full route manifest before connecting any sources.
3. SDK validates the CLI route connections against that declared route
   manifest.
4. SDK posts one source-create request per startup route or grouped-route
   connection.
5. SDK fetches the resolved source records.
6. SDK attaches through the existing IPC contract.
7. If one startup route connection fails, the SDK reports which route failed
   and keeps the app process alive unless the app explicitly requested
   fail-fast startup.

## Success Criteria

### Product

- users no longer manage runtime stream names when declaring routes
- one URI means one fixed published source shape
- discovery exposes exact depth choices and any proven grouped preset choices
  when D2C changes delivered caps
- optional `/channel/<name>` exists for channel-disambiguated devices, but most
  users consume the fully generated discovery URI without hand-editing it
- grouped preset URIs such as `orbbec/preset/480p_30` can activate multiple
  intent-first routes without an extra SDK-only frame-merge layer
- grouped preset sessions can attach through the same app-source surface, so an
  app can start from one grouped preset choice without separately managing
  `/color` and `/depth`
- dependent sources can be related through source-group metadata
- grouped sources either share one compatible grouped runtime or reject with a
  compatibility error
- direct-session-first and app-first flows are both supported
- identical exact URIs can be reused safely across multiple consumers
- different delivery intents remain distinct delivery sessions
- backend restart preserves apps, routes, and sources while runtime state is
  normalized back to `stopped`

### Technical

- schema is defined in checked-in SQL, not only inline C++
- the durable schema stays minimal:
  - `devices`
  - `streams`
  - `apps`
  - `app_routes`
  - `app_sources`
  - `sessions`
  - `session_logs`
- device-specific exact-member and grouped preset choices both live in the
  `streams` table; no separate preset table is required in v1
- grouped-source metadata and channel distinctions are preserved without
  leaking hardware pairing policy into the public route contract
- grouped preset fan-out is represented explicitly as a `route_grouped` bind
  instead of an SDK-only frame-join helper
- app-source creation is the single REST control surface for both URI-backed
  connects and session-backed attaches
- `delivery_name` is durable on app-source and session records, but inferred
  during normalization rather than posted by clients
- normal use does not change grouped capture policy at bind time; a different
  behavior must come from a different discovered URI
- capture policy may legitimately map delivered caps to a different underlying
  native sensor profile, as in the tested Orbbec `orbbec/depth/480p_30` case
- discovery does not synthesize aligned variants that the device has not proven,
  such as `depth-720p_30` on the tested Orbbec unit
- non-debug routes declare enough expectation metadata to reject obvious
  misroutes such as depth into a video detector
- the resolved exact stream identity is persisted explicitly enough to survive
  restart without ambiguity
- local SDK attach is IPC-only in v1, while future remote or LAN RTSP
  consumption can be added without changing the core app-source resource shape
- current scope therefore implies one-way interoperability:
  local `insightos://` selections can be published toward `rtsp://` consumers,
  but raw `rtsp://` inputs do not become first-class `insightos://` catalog
  sources without explicit ingest/import design
- existing logical/capture/delivery session reuse remains intact
- feature tracker exists and can be updated mechanically as implementation
  advances
