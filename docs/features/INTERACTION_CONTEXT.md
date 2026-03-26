# Interaction Context

## Role

- role: donor-grounded interaction framing for the active `insight-io` design
- status: active
- version: 6
- major changes:
  - 2026-03-26 aligned selector examples with the reviewed contract: V4L2
    selectors are plain `720p_30` style while grouped Orbbec selectors keep
    the `orbbec/...` namespace
  - 2026-03-25 clarified that direct sessions are standalone session-first
    sessions and that multi-device apps declare app-local logical input routes
  - 2026-03-25 replaced public grouped/exact bind selection with one
    app-local `target` surface
  - 2026-03-25 reframed RTSP as optional publication intent rather than a peer
    to implicit local IPC attach
  - 2026-03-25 removed `/channel/...` from the active URI grammar
- past tasks:
  - `2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

This note explains what `insight-io` should feel like from a user and operator
point of view. It is grounded on the audited workflows from the donor project
and restates them in the new DB-first route-based shape.

## Source Material

The interaction baseline comes from:

- `/home/yixin/Coding/insightos/demo_command.md`
- `/home/yixin/Coding/insightos/demo_command_3min.md`
- `/home/yixin/Coding/insightos/sdk/tests/app_integration_test.cpp`

Those files cover almost all currently exercised user-visible behavior:

- build and launch
- health and catalog inspection
- device aliasing
- direct session creation through REST and `insightos-open`
- shared capture and publication reuse
- RTSP verification
- backend restart and persisted session recovery
- idle app startup
- later source injection into a running app
- mixed-source attach
- stream rename edge cases while a delivery is already live

## What Stays The Same In `insight-io`

`insight-io` keeps the same lower-level runtime model:

- public URIs still look like
  `insightos://<host>/<device>/<selector>`
- devices still come from discovery and catalog listing
- device aliases still create the human-usable names that appear in the base
  URI
- logical, capture, and delivery sessions still exist and are still
  inspectable
- local attach still happens through the existing IPC contract
- RTSP and audio delivery flows still exist for operator-facing and debugging
  scenarios
- RTSP publication intent is now selected separately from the URI identity
- restart still preserves durable intent while runtime state is rebuilt
- future remote or LAN RTSP consumption remains separate from the v1 SDK attach
  path

## What Changes In `insight-io`

The application layer is no longer stream-name-first.

In the donor repo, the app workflow looks like:

1. Start an idle app such as `idle_mixed_monitor`
2. Fetch its `app_id`
3. Inject a source with only `input`
4. Let the app match callbacks using stream names such as `frame`, `color`, or
   `depth`

In `insight-io`, the same workflow becomes:

1. Start or create an app record
2. Declare routes such as `yolov5`, `orbbec/color`, `orbbec/depth`,
   `stereo/left-detector`
3. Describe route expectations only when needed
4. Bind a source with either `input` plus `target` or `session_id` plus
   `target`
5. Let the backend resolve one source identity from that selection
6. Let the app consume one callback chain per route

Those route names are app-local logical input roles. For an app that consumes
two V4L2 cameras plus one Orbbec, the preferred declarations are roles such as
`front-camera`, `rear-camera`, `orbbec/color`, and `orbbec/depth`, not posted
device aliases or globally unique route strings.

Example:

```json
{
  "input": "insightos://localhost/front-camera/720p_30",
  "target": "yolov5"
}
```

Grouped-source example:

```json
{
  "input": "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
  "target": "orbbec"
}
```

Recommended SDK shape:

```cpp
app.route("orbbec/color")
    .expect(insightos::Video{})
    .on_caps([](const insightos::Caps& caps) { /* unchanged */ })
    .on_frame([](const insightos::Frame& frame) { /* unchanged */ });

app.route("orbbec/depth")
    .expect(insightos::Depth{})
    .on_caps([](const insightos::Caps& caps) { /* unchanged */ })
    .on_frame([](const insightos::Frame& frame) { /* unchanged */ });
```

That keeps the callback shape compact while removing stream-name selection from the
normal route-based API.

## Main User Journeys

### 1. Operator Bootstraps The Runtime

This remains close to `demo_command.md`:

1. Build the project
2. Start RTSP infrastructure if needed
3. Start the backend
4. Verify `/api/health`
5. Inspect `/api/devices`
6. Apply device aliases to produce stable derived URIs

The route-based project depends on this flow because route connection still
starts from listed URIs.

### 2. Operator Uses Direct Session Flows

This also remains a first-class part of the product:

1. Create direct sessions through `POST /api/sessions` or `insightos-open`
2. Optionally request RTSP publication with `rtsp_enabled = true`
3. Inspect session metadata
4. Verify RTSP or audio output
5. Stop sessions
6. Restart the backend
7. Rehydrate persisted sessions explicitly
8. Optionally attach an already-running direct IPC session to an app route or
   grouped target later

These flows matter because `insight-io` is not only an app builder. It still
needs an inspectable media runtime beneath the app layer.

Important boundary:

- a direct session is standalone or session-first runtime intent, not an
  implicit app subscription
- declaring compatible routes does not make an app consume that session
- an app starts receiving frames only after it creates its own bind by `input`
  or `session_id`

### 3. Developer Runs An Idle App And Connects Sources Later

This is the main app-developer flow carried forward from
`app_integration_test.cpp` and `demo_command_3min.md`:

1. Start an app with no initial source
2. Retrieve its `app_id`
3. Declare its routes
4. Connect one or more listed URIs into those routes
5. Observe one callback chain per route in the app
6. Stop, restart, or replace routed sources without destroying the app record

The difference is that source routing is explicit in the data model instead of
being reconstructed from a runtime-only registry.

### 4. Developer Builds Mixed-Source Apps

`insight-io` should support the same kinds of mixed-source apps seen in the
donor repo, but with clearer contracts:

- multiple independent video routes in one app
- multiple device roles in one app, for example `front-camera`,
  `rear-camera`, `orbbec/color`, and `orbbec/depth`
- related routes such as color + depth in one app
- grouped preset binds that can activate related routes such as
  `orbbec/color` and `orbbec/depth` together from one catalog-published preset
  URI
- grouped preset sessions that can attach through the same grouped target
  surface without forcing the app to bind `/color` and `/depth` individually
- separate depth choices such as `orbbec/depth/400p_30` and
  `orbbec/depth/480p_30`
- stereo left + right routes in one app
- explicit rejection when a source cannot satisfy a route's semantic
  expectation
- restart-safe app records that keep declared intent after the backend exits
- same exact URI reused across multiple consumers
- additive RTSP publication on the same source while sharing capture or serving
  runtime when possible

### 5. Operator Audits Rename And Reuse Edge Cases

The donor demos show an important edge case:

- renaming a stream alias does not rename a running delivery in place
- a newly reused delivery still exposes the already-live public stream name
- the renamed stream appears only after the old delivery is stopped and a fresh
  delivery is created

`insight-io` should keep this behavior documented because route-based routing
sits above it and does not erase the underlying session and publication
semantics.

## Exact Source Discovery Implications

The discovery catalog is now responsible for exposing exact member and grouped
preset choices up
front.

That means:

- one listed URI always maps to one fixed published source shape
- discovery publishes that source shape; sessions realize it later
- if D2C changes depth output from `400p` to `480p`, discovery lists both
  choices separately
- if a grouped RGBD preset is proven end to end, discovery may also list a
  fixed grouped preset URI such as `orbbec/preset/480p_30`
- RTSP publication intent is persisted separately from the URI on app and
  session records
- grouped-device runtime behavior remains fixed by the chosen catalog entry in
  normal use; the current docs do not add richer dependency metadata until
  device investigation completes

## Why The Feature Trackers Need To Be Broader

The original `fullstack-intent-routing-e2e.json` file mostly tracks the new
data model and route-connection core. That is necessary but incomplete.

To represent the actual product, the repo also needs feature coverage for:

- bootstrap and health flows
- catalog and alias flows
- direct session and restart flows
- idle app discovery flows
- multi-route source connection flows
- session-first attach flows
- heterogeneous-hardware abstraction at discovery time
- same-URI fan-out
- same-source RTSP publication behavior
- runtime rebind
- route-connection visibility in API responses
- rename and reuse edge cases
- future browser builder flows

The additional tracker in this directory fills that gap.
