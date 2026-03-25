# Full-Stack Intent Routing PRD

## Role

- role: product contract for the DB-first intent-routing rebuild
- status: active
- version: 6
- major changes:
  - 2026-03-25 removed stale source variant/group response fields, made RTSP
    publication metadata queryable from the catalog, and fixed
    `DELETE /api/sessions/{id}` to return `409 Conflict` while referenced
  - 2026-03-25 clarified that direct sessions are standalone session-first
    sessions and that multi-device apps declare app-local logical input routes
  - 2026-03-25 replaced public `route` / `route_grouped` bind inputs with one
    app-local `target` surface and explicit ambiguity guardrails
  - 2026-03-25 reframed RTSP as optional publication intent rather than a peer
    to implicit local IPC attach
  - 2026-03-25 removed `/channel/...` from the active URI grammar
- past tasks:
  - `2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics`
  - `2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

## Summary

`insight-io` is a DB-first route-based project for routing discoverable source
URIs into app-defined routes and direct sessions.

The public URI shape remains `insightos://`, and the URI contract is now
selector-oriented:

```text
insightos://<host>/<device>/<selector>
```

Examples:

- `insightos://localhost/front-camera/video-720p_30`
- `insightos://localhost/desk-rgbd/orbbec/color/480p_30`
- `insightos://localhost/desk-rgbd/orbbec/depth/400p_30`
- `insightos://localhost/desk-rgbd/orbbec/depth/480p_30`
- `insightos://localhost/desk-rgbd/orbbec/preset/480p_30`

The key product rules are:

- discovery publishes the exact derived URIs that users and apps copy
- the app declares routes by purpose, not by runtime stream name
- one derived URI selects one fixed catalog-published source shape
- RTSP publication is optional durable bind/session intent, not part of source
  identity
- the catalog may expose queryable RTSP publication metadata for a source shape
  using the same `/<device>/<selector>` path as the derived `insightos://` URI
  with the configured RTSP host
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
- identical URI plus publication requirements may fan out to multiple
  consumers through reuse
- RTSP publication is additive:
  one shared serving runtime may expose RTSP when one or more active consumers
  request it
- local SDK attach always uses IPC in v1, but that is implicit and not a
  posted field
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

- reintroducing durable capture, delivery, or publication worker tables in v1
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
- app flows become target-based, so users connect `input + target` or attach
  `session_id` to one app-local target
- apps no longer register runtime stream names directly in route declarations
- the source URI is authoritative for source identity; route expectations only
  validate that choice
- RTSP publication intent is selected separately from URI identity

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
4. User may optionally request RTSP publication with `rtsp_enabled = true`.
5. Backend creates or reuses capture and serving runtime accordingly, and
   publishes `rtsp_url` when RTSP publication is enabled. The RTSP URL should
   keep the same `/<device>/<selector>` path as the selected `insightos://`
   URI while replacing `localhost` with the configured RTSP host.
6. User monitors RTSP or local IPC output and later stops the session.

### 3. Create App And Routes

1. User creates an app.
2. User defines one or more routes.
3. Each route declares one app-local logical input role plus optional semantic
   expectations.

Examples:

- `yolov5` expects video
- `front-camera` expects video
- `rear-camera` expects video
- `orbbec/color` expects video
- `orbbec/depth` expects depth

Multi-device rule:

- routes describe what the app wants to consume, not which discovered device or
  globally unique resource happens to satisfy that role today
- for one app using two V4L2 cameras plus one Orbbec, declare app-local routes
  such as `front-camera`, `rear-camera`, `orbbec/color`, and `orbbec/depth`
- route declaration alone does not attach runtime; callbacks begin only after a
  later app-source bind becomes active on that target

### 4. Start App First, Then Bind Source URI To Target Intent

1. User picks one listed URI from the catalog.
2. For one exact member URI, user connects it to one route with:

```json
{
  "input": "insightos://localhost/front-camera/video-720p_30",
  "target": "vision/detector"
}
```

or:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/depth/480p_30",
  "target": "orbbec/depth"
}
```

3. For one grouped preset URI, user may bind it to a grouped target root with:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
  "target": "orbbec"
}
```

4. User may optionally request RTSP publication with `rtsp_enabled = true`.
5. Backend normalizes the URI plus optional RTSP publication into the existing
   `SessionRequest`.
6. Backend resolves either:
   - one concrete exact stream identity from the URI itself, or
   - one concrete preset member set from the URI itself
7. Backend validates route expectations against the resolved source metadata.
8. Backend creates one ordinary logical session or one grouped logical session.
9. Backend returns the resolved source identity, member bindings when grouped,
   publication state, and serving-session metadata to the SDK.

V1 boundary:

- app-source binds do not accept the old RTSP-qualified `insightos://.../rtsp`
  form
- local SDK attach is still IPC-only in v1
- RTSP publication is optional state on the selected session, not part of the
  selected source URI
- raw `rtsp://` input remains a future ingest/import path

### 5. Start Stream First, Then Bind It To A Target

Direct-session meaning:

- a direct session is a standalone or session-first runtime created from one
  selected URI before any app target is involved
- apps that merely declare matching routes do not receive frames from that
  session until they create an app-source bind by `session_id` or by the same
  `input` URI

1. User starts a direct session first through CLI or REST.
2. User later creates or finds an app target.
3. User posts one app-source bind using `session_id` plus `target`.
4. Backend validates that the existing session is compatible with that target.
5. In v1, the referenced session must remain IPC-capable because local SDK
   attach still uses IPC.
6. Grouped preset sessions may attach through the same `target` surface, so one app can
   start from `orbbec/preset/...` without managing `/color` and `/depth`
   individually.
7. The bind keeps both:
   - `source_session_id` for the session the user chose
   - `active_session_id` for the session actually serving the app

### 6. Fan-Out And Publication Divergence

1. The same exact URI may be used in multiple places, including multiple routes
   across one or more apps.
2. When the URI and publication requirements are identical, the runtime should
   reuse the same serving session when possible so all consumers see the same
   frame sequence.
3. When one consumer requests RTSP publication and another does not, the
   backend may still share capture and serving runtime by enabling RTSP as an
   additive publication on the shared session when lifecycle rules allow it.

### 7. Run Routed App Logic

1. App declares callbacks on named routes.
2. SDK attaches using the existing IPC contract.
3. SDK surfaces one callback chain per route.

### 8. Inspect Runtime And Session State

1. User inspects `GET /api/status` for shared capture and publication reuse.
2. User inspects `GET /api/sessions` and `GET /api/sessions/{id}` for direct
   runtime state.
3. User inspects `GET /api/apps/{id}/sources` for target bindings, latest
   session ids, and target-level errors.

### 9. Change Routing At Runtime

1. User replaces the URI bound to one route or grouped target without deleting
   the app.
2. User may also bind a different existing logical session to one route or
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

Baseline rule:

- non-debug routes should declare `media`

Examples:

- `yolov5`:
  - `media = video`
- `front-camera`:
  - `media = video`
- `rear-camera`:
  - `media = video`
- `orbbec/color`:
  - `media = video`
- `orbbec/depth`:
  - `media = depth`

Important rule:

- route expectations reject incompatible URIs
- non-debug routes without `media` are underspecified and should be avoided
- route expectations do not auto-upgrade `orbbec/depth/400p_30` into
  `orbbec/depth/480p_30`, or silently expand an exact member URI into a grouped
  preset behind the user’s back
- if an app needs fixed left/right roles or other repeated devices, encode
  those roles in `route_name`, for example `front-camera` or `rear-camera`,
  rather than adding a special route-side selector field in v1
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

Delete rule:

- `DELETE /api/sessions/{id}` must return `409 Conflict` when any `app_source`
  still references that session through `source_session_id` or
  `active_session_id`
- only unreferenced sessions may be deleted; successful delete also removes
  `session_logs`

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
  "route_name": "vision/detector",
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
  "target": "vision/detector"
}
```

URI-backed grouped request:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
  "target": "orbbec"
}
```

Session-backed exact request:

```json
{
  "session_id": 42,
  "target": "vision/detector"
}
```

Session-backed grouped request:

```json
{
  "session_id": 43,
  "target": "orbbec"
}
```

### Source Response Requirements

- source identity
- derived URI
- RTSP publication flag
- RTSP URL when published
- source kind
- source session id when session-backed
- active session id when a serving runtime exists
- target name
- exact-route diagnostic name when applicable, formatted as
  `apps/{app}/routes/{route}`
- resolved stream id
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

app.route("vision/detector")
    .expect(insightos::Video{})
    .on_caps([](const insightos::Caps& caps) { /* video */ })
    .on_frame([](const insightos::Frame& frame) { /* video */ });

app.route("orbbec/color")
    .expect(insightos::Video{})
    .on_frame(handle_color);

app.route("orbbec/depth")
    .expect(insightos::Depth{})
    .on_frame(handle_depth);

app.bind_source(
    "vision/detector",
    "insightos://localhost/front-camera/video-720p_30");

app.bind_source(
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
- `App::bind_source(target, input)` for explicit startup source binding
- `App::bind_source(target, session_id)` for reverse-order binding to an
  already running direct or grouped session
- `App::rebind(...)` for runtime replacement of one existing target binding

The SDK methods are thin conveniences over the same app-source REST surface:

- `bind_source(...)` creates URI-backed or session-backed app-source binds
- the REST surface exists so operators and external controllers can drive a
  running app without linking the SDK
- local SDK attach remains IPC-only in v1 even when the selected session came
  from a session-first flow

Why `source_session_id` and `active_session_id` both matter:

- `source_session_id` preserves the upstream session the user pointed at
- `active_session_id` captures the session that is actually serving the app now
- they are equal for direct IPC attach
- they may diverge when a restarted or regrouped serving path is materialized
  separately from the selected upstream session

Expected grouped preset behavior:

- routes are still declared independently with ordinary `route(...).expect(...)`
  chains
- apps can set up generic `video` and `depth` routes without prior hardware
  knowledge, then bind one grouped preset URI to a grouped target root such as
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
  vision/detector=insightos://localhost/front-camera/video-720p_30 \
  orbbec=insightos://localhost/desk-rgbd/orbbec/preset/480p_30
```

Backend Handshake:

1. SDK creates the app record with `POST /api/apps`.
2. SDK declares the full route manifest before connecting any sources.
3. SDK validates the CLI route connections against that declared route
   manifest.
4. SDK posts one source-create request per startup target connection.
5. SDK fetches the resolved source records.
6. SDK attaches through the existing IPC contract.
7. If one startup target connection fails, the SDK reports which target failed
   and keeps the app process alive unless the app explicitly requested
   fail-fast startup.

## Success Criteria

### Product

- users no longer manage runtime stream names when declaring routes
- one URI means one fixed published source shape
- discovery exposes exact depth choices and any proven grouped preset choices
  when D2C changes delivered caps
- grouped preset URIs such as `orbbec/preset/480p_30` can activate multiple
  intent-first routes without an extra SDK-only frame-merge layer
- grouped preset sessions can attach through the same app-source surface, so an
  app can start from one grouped preset choice without separately managing
  `/color` and `/depth`
- dependent sources can still be recognized as related for grouped runtime
  planning without separate public source-group id fields
- grouped sources either share one compatible grouped runtime or reject with a
  compatibility error
- direct-session-first and app-first flows are both supported
- identical exact URIs can be reused safely across multiple consumers
- RTSP publication can be added without changing source identity
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
- grouped-source metadata is preserved without leaking hardware pairing policy
  into the public route contract
- grouped preset fan-out is represented through server-side target resolution
  instead of an SDK-only frame-join helper
- app-source creation is the single REST control surface for both URI-backed
  connects and session-backed attaches
- RTSP publication intent is durable on app-source and session records, but it
  is posted as resource state rather than tunneled into the source URI or query
  string
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
- the catalog can expose a queryable RTSP publication URL whose path mirrors
  the selected `insightos://` URI while using the configured RTSP host
- local SDK attach is IPC-only in v1, while future remote or LAN RTSP
  consumption can be added without changing the core app-source resource shape
- current scope therefore implies one-way interoperability:
  local `insightos://` selections can be published toward `rtsp://` consumers,
  but raw `rtsp://` inputs do not become first-class `insightos://` catalog
  sources without explicit ingest/import design
- deleting a referenced session returns `409 Conflict` instead of silently
  detaching dependent app sources
- logical session reuse and runtime-only capture/publication planning remain
  aligned with the active contract
- feature tracker exists and can be updated mechanically as implementation
  advances
