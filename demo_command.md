# Demo Commands

## Role

- role: exact fresh-DB transcript for the current host, with `/api/dev/*` or
  CLI shown as the recommended path and canonical `/api/*` shown inline as the
  alternative
- status: active
- revision: 2026-03-30 canonical-route-declare-only
- major changes:
  - 2026-03-30 removed route mutation from the recommended thin `/api/dev/*`
    path, kept route declaration on canonical `/api/apps/{id}/routes`, and
    rewrote the mixed dev/demo transcript so sources still use the friendlier
    thin surface while routes stay low-level
  - 2026-03-30 reorganized the demo around one fresh default-port daemon run
    so `/api/dev/*`, CLI, and canonical `/api/*` appear side by side by
    workflow instead of in separate sections
  - 2026-03-30 replaced the old large response dumps with short result
    docstrings that were rechecked on the live host against a fresh SQLite file
  - 2026-03-30 made `POST /routes` versus `POST /sources` explicit, added the
    current request JSON shapes and meanings, and moved CLI defaults ahead of
    optional flags such as `--app-name`, `--max-frames`, and startup
    `insightos://...` binds
- past tasks:
  - `2026-03-30 – Reorganize Demo Commands Around Integrated Dev, CLI, And Canonical Flows`
  - `2026-03-30 – Refresh Demo Commands For Fresh Thin Developer Surface Verification`
  - `2026-03-27 – Revalidate Task-10 Developer Surface, Correct Overclaim, And Add Dev Demo Alternatives`
  - `2026-03-27 – Add PipeWire Audio Example And Verify Mono/Stereo Selectors`
  - `2026-03-27 – Complete Task-9 SDK, Browser Flows, And Runtime Verification`
  - `2026-03-27 – Simplify Example Startup Paths And Close Mermaid Backlog`

Use `/api/dev/*` or CLI for day-to-day demo work. Route declaration is the one
intentional exception: keep `POST /api/apps/{id}/routes` as the low-level
canonical contract because it mirrors SDK `app.route(...).expect(...)`. Use
canonical `/api/*` elsewhere when you want the fuller payload shape from
[REST.md](/home/yixin/Coding/insight-io/docs/REST.md).

All commands below are self-contained. When an `app_id`, `route_id`,
`source_id`, or `session_id` appears, it is the ID returned by the earlier
command in this same fresh-DB transcript.

This rerun used one fresh SQLite file on the current host. If the live
inventory differs, rerun `POST /api/dev/catalog:refresh` before trusting the
hard-coded IDs below. On this host, if the expected Orbbec device is missing,
retry discovery once and check the USB connection before treating that as a
software regression.

## Build

```bash
cmake --build build -j4
```

```text
"""
Build is up to date on this rerun.
The daemon, tests, and example apps are all present in `build/bin/`.
"""
```

```bash
ctest --test-dir build --output-on-failure
```

```text
"""
All focused tests pass on this rerun: 8 out of 8 green in about 30 seconds.
"""
```

## Fresh Demo Daemon

```bash
rm -f /tmp/insight-io-demo-18180.sqlite3
```

```text
"""
Removes the old demo DB so the IDs and names below come from one clean run.
"""
```

```bash
./build/bin/insightiod --db-path /tmp/insight-io-demo-18180.sqlite3 --rtsp-port 18640
```

```text
"""
Starts the daemon on the default backend address `127.0.0.1:18180`.
CLI examples below therefore need no `--backend-host` or `--backend-port`.
RTSP URLs on this run use port `18640`.
"""
```

## Browse The Catalog

Recommended thin refresh:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/catalog:refresh | jq '.devices[] | {name, driver, stream_count: (.streams | length)}'
```

```text
"""
Refresh returns four devices on this host:
- one SDK-backed Orbbec device `sv1301s-u3`
- one V4L2 webcam `web-camera`
- two PipeWire audio devices
The fresh stream counts on this rerun are 22, 9, 2, and 2 respectively.
"""
```

Recommended thin health:

```bash
curl -s http://127.0.0.1:18180/api/dev/health | jq
```

```text
"""
Thin health reports `status = ok`, `device_count = 4`, `session_count = 0`,
and `active_sessions = 0` before any demo session is created.
"""
```

Recommended thin URI browse:

```bash
curl -s http://127.0.0.1:18180/api/dev/uris | jq '.uris[] | select(.stream_id == 21 or .stream_id == 29 or .stream_id == 34 or .stream_id == 35) | {stream_id, device, name, driver, media, uri}'
```

```text
"""
These are the four main demo IDs from the fresh run:
- `21`: `insightos://localhost/sv1301s-u3/orbbec/preset/480p_30`
- `29`: `insightos://localhost/web-camera/720p_30`
- `34`: `insightos://localhost/web-camera-mono/audio/mono`
- `35`: `insightos://localhost/web-camera-mono/audio/stereo`
"""
```

Alternative canonical browse:

```bash
curl -s http://127.0.0.1:18180/api/devices | jq '.devices[] | {public_name, driver, source_count: (.sources | length)}'
```

```text
"""
Canonical `/api/devices` returns the fuller device-source records.
Later in this transcript, after alias changes, the same command shows
`front-camera` and `desk-rgbd` instead of the original public names.
"""
```

## `POST /routes` Versus `POST /sources`

`POST /routes` declares app-local logical targets. It does not start a session,
open a device, or attach callbacks by itself. Route declaration stays on the
canonical `/api/apps/{id}/routes` contract even in developer demos because it
is the REST form of SDK `app.route(...).expect(...)`.

Most developers will not call `POST /routes` manually in normal SDK app flows.
Use it only when the app row itself was created over REST and still needs its
input contract declared before a later source bind.

Canonical route JSON:

```json
{ "route_name": "camera", "expect": { "media": "video" } }
```

Meaning:

- `camera` is the app-local target name
- `expect.media` is the route expectation only
- route creation leaves the target idle until a source is bound

`POST /sources` binds one real upstream source to one previously declared
target and is what actually creates or reuses runtime state.

URI-backed source JSON:

```json
{ "input": "insightos://localhost/web-camera/720p_30", "target": "camera" }
```

Session-backed source JSON:

```json
{ "session_id": 1, "target": "camera" }
```

Meaning:

- `target` is always the app-local route or grouped target root
- `input` binds directly from one catalog-published `insightos://` URI
- `session_id` reuses one previously created direct session instead
- grouped sources still use `POST /sources`; the grouped example below binds
  preset `480p_30` to grouped target root `orbbec`

## Direct Session: Recommended `/api/dev/*`, Alternative `/api/*`

Recommended thin direct session create:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/sessions -H 'Content-Type: application/json' -d '{"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":false}' | jq '{session_id, device, stream, media, state, requested_uri}'
```

```text
"""
Creates direct session `1`.
Thin response shows the human-facing device and stream names directly:
`web-camera`, `720p_30`, `media = video`, `state = active`.
"""
```

Alternative canonical direct session create:

```bash
curl -s -X POST http://127.0.0.1:18180/api/sessions -H 'Content-Type: application/json' -d '{"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":false}' | jq '{session_id, state, input_uri, resolved_exact_stream_id, selector: .resolved_source.selector, runtime_key: .serving_runtime.runtime_key, consumer_count: .serving_runtime.consumer_count}'
```

```text
"""
Creates direct session `2` on the same exact webcam stream.
Canonical response exposes the richer runtime fields:
`resolved_exact_stream_id = 29`, `selector = 720p_30`, `runtime_key = stream:29`.
Because session `1` already exists, this second direct session lands on the
same runtime with `consumer_count = 2`.
"""
```

## App, Route, And Source

Recommended thin app create:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/apps -H 'Content-Type: application/json' -d '{"name":"dev-runner"}' | jq '{app_id, name}'
```

```text
"""
Creates app `1` named `dev-runner`.
Use this thin path for small manual demos and browser-like control.
"""
```

Low-level route declare for thin app `1`:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps/1/routes -H 'Content-Type: application/json' -d '{"route_name":"camera","expect":{"media":"video"}}' | jq '{route_name, expect}'
```

```text
"""
Declares one logical target `camera` on app `1`.
This low-level step is only needed here because the app was created over REST.
It still does not start any callback or runtime yet.
"""
```

Recommended thin session-backed source bind:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/apps/1/sources -H 'Content-Type: application/json' -d '{"session_id":1,"target":"camera"}' | jq '{source_id, target, source_session_id, active_session_id, device, stream, media, state}'
```

```text
"""
Creates source `1` on app `1`.
Because this bind points at `session_id = 1`, both `source_session_id` and
`active_session_id` are `1`, and the thin response resolves to
`web-camera / 720p_30`.
"""
```

## Grouped Orbbec Bind

Recommended thin grouped app create:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/apps -H 'Content-Type: application/json' -d '{"name":"rgbd-dev-runner"}' | jq '{app_id, name}'
```

```text
"""
Creates app `3` named `rgbd-dev-runner`.
This app will declare grouped RGBD routes and then bind one grouped preset.
"""
```

Low-level grouped route declare for thin app `3`:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps/3/routes -H 'Content-Type: application/json' -d '{"route_name":"orbbec/color","expect":{"media":"video"}}' | jq '{route_name, expect}'
```

```text
"""
Declares grouped member route `orbbec/color` with `expect.media = video`.
"""
```

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps/3/routes -H 'Content-Type: application/json' -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}' | jq '{route_name, expect}'
```

```text
"""
Declares grouped member route `orbbec/depth` with `expect.media = depth`.
Together these routes make grouped target root `orbbec` valid for `/sources`.
"""
```

Recommended thin grouped source bind:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/apps/3/sources -H 'Content-Type: application/json' -d '{"input":"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30","target":"orbbec"}' | jq '{source_id, target, device, stream, media, state}'
```

```text
"""
Creates grouped source `3` on app `3`.
The bound upstream stream is `orbbec/preset/480p_30`, `media = grouped`,
and the bind becomes active immediately.
"""
```

Recommended thin grouped source inspect:

```bash
curl -s http://127.0.0.1:18180/api/dev/apps/3 | jq '.sources[0] | {target, selector, media, members: [.members[] | {route, selector, media}]}'
```

```text
"""
The grouped bind fans out exactly as expected on this rerun:
- `orbbec/color` gets `orbbec/color/480p_30`
- `orbbec/depth` gets `orbbec/depth/480p_30`
This is the clearest place to see why grouped routes are declared first and
the grouped source is posted later.
"""
```

## Alias Changes: Recommended `/api/dev/*`, Alternative `/api/*`

Recommended thin webcam alias:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/devices/web-camera/alias -H 'Content-Type: application/json' -d '{"name":"front-camera"}' | jq '{name, default_name, driver}'
```

```text
"""
Renames the webcam device from `web-camera` to `front-camera`.
The default name remains `web-camera`.
"""
```

Recommended thin webcam stream alias:

```bash
curl -s -X POST http://127.0.0.1:18180/api/dev/streams/29/alias -H 'Content-Type: application/json' -d '{"name":"main-preview"}' | jq '{stream_id, name, default_name, selector, uri}'
```

```text
"""
Renames stream `29` from `720p_30` to `main-preview`.
The selector stays `720p_30`, but the current canonical URI becomes
`insightos://localhost/front-camera/main-preview`.
"""
```

Alternative canonical Orbbec alias:

```bash
curl -s -X POST http://127.0.0.1:18180/api/devices/sv1301s-u3/alias -H 'Content-Type: application/json' -d '{"public_name":"desk-rgbd"}' | jq '{public_name, default_name, driver}'
```

```text
"""
Canonical alias endpoints use `public_name` instead of thin `name`.
This renames the Orbbec device from `sv1301s-u3` to `desk-rgbd`.
"""
```

```bash
curl -s -X POST http://127.0.0.1:18180/api/streams/21/alias -H 'Content-Type: application/json' -d '{"public_name":"main-preset"}' | jq '{stream_id, public_name, default_name, selector, uri}'
```

```text
"""
Renames grouped stream `21` to `main-preset`.
Its selector stays `orbbec/preset/480p_30`, while the current URI becomes
`insightos://localhost/desk-rgbd/main-preset`.
"""
```

Check the alias-aware URI view:

```bash
curl -s http://127.0.0.1:18180/api/dev/uris | jq '.uris[] | select(.stream_id==21 or .stream_id==29) | {stream_id, device, name, uri}'
```

```text
"""
After the alias walkthrough, the thin URI view reports:
- stream `29` as `front-camera / main-preview`
- stream `21` as `desk-rgbd / main-preset`
Already-created sessions still remember their original `requested_uri`, but
the current canonical URI surface switches to the new aliases immediately.
"""
```

## Runtime Inspect: Recommended `/api/dev/runtime`, Alternative `/api/status`

Recommended thin runtime snapshot:

```bash
curl -s http://127.0.0.1:18180/api/dev/runtime | jq '{sessions: [.sessions[] | {session_id, device, stream, state, requested_uri}], serving_runtimes: [.serving_runtimes[] | {runtime_key, device, stream, consumer_count, rtsp_enabled}]}'
```

```text
"""
Thin runtime shows alias-aware current names plus original request URIs.
On this rerun it shows two live runtimes:
- `stream:29` for `front-camera / main-preview` with `consumer_count = 3`
- `stream:21` for `desk-rgbd / main-preset` with `consumer_count = 1`
"""
```

Alternative canonical status snapshot:

```bash
curl -s http://127.0.0.1:18180/api/status | jq '{total_sessions, active_sessions, total_serving_runtimes, serving_runtimes: [.serving_runtimes[] | {runtime_key, selector: .resolved_source.selector, consumer_count, owner_session_id}]}'
```

```text
"""
Canonical status shows the same two runtimes, but by selector and owner
session id instead of the thinner alias-oriented shape:
- `stream:29` owns selector `720p_30` with owner session `1`
- `stream:21` owns selector `orbbec/preset/480p_30` with owner session `4`
"""
```

## CLI: Default First, Then Optional Features

Recommended idle CLI startup with no flags:

```bash
./build/bin/v4l2_latency_monitor
```

```text
"""
No immediate terminal output appears while the app is idle.
The app name defaults to the executable name, `v4l2-latency-monitor`.
It declares route `camera` automatically, but creates no source yet.
"""
```

Inspect the idle app by name:

```bash
curl -s http://127.0.0.1:18180/api/dev/apps | jq '.apps[] | select(.name=="v4l2-latency-monitor")'
```

```text
"""
While the process is running, `/api/dev/apps` lists one row named
`v4l2-latency-monitor`.
To activate it later, post one source bind to its `camera` target by using the
same `/sources` flow shown above.
"""
```

Optional feature: custom app name while staying idle:

```bash
./build/bin/v4l2_latency_monitor --app-name=frontcam-demo
```

```text
"""
Uses a custom durable app name instead of the executable-derived default.
This is the easiest way to make the later REST target stable when multiple
copies of the same example might run at once.
"""
```

Inspect the custom-name idle app by name:

```bash
curl -s http://127.0.0.1:18180/api/dev/apps | jq '.apps[] | select(.name=="frontcam-demo")'
```

```text
"""
While that process is running, `/api/dev/apps` lists one row named
`frontcam-demo`.
Only the durable app name changed; the example still exposes its usual idle
`camera` route until you post one source bind.
"""
```

Optional feature: startup `insightos://...` bind:

```bash
./build/bin/v4l2_latency_monitor --app-name=frontcam-startup insightos://localhost/front-camera/main-preview
```

```text
"""
Startup URI skips the later `/sources` POST and begins streaming immediately.
On this rerun it printed camera caps right away and started emitting frame
timing lines from `front-camera / main-preview`.
"""
```

Optional feature: short auto-stop run with `--max-frames`:

```bash
./build/bin/v4l2_latency_monitor --app-name=frontcam-five2 --max-frames=5 insightos://localhost/front-camera/main-preview
```

```text
"""
`--max-frames` turns the example into a short bounded run instead of a long
manual one. On this rerun the command printed startup caps and first-frame
timing, then exited cleanly on its own.
"""
```

Check that the short-run app is gone afterwards:

```bash
curl -s http://127.0.0.1:18180/api/dev/apps | jq '.apps[] | select(.name=="frontcam-five2")'
```

```text
"""
This returns no row on the rerun above.
The short `--max-frames` run cleaned itself up instead of leaving a persistent
idle app behind.
"""
```
