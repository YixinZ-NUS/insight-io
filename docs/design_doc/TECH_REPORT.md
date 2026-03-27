# Tech Report

## Role

- role: internal implementation report for the standalone `insight-io` rebuild
- status: active
- version: 20
- major changes:
  - 2026-03-27 added the checked-in PipeWire audio example app, live-verified
    direct stereo startup plus idle mono late bind on the current host,
    documented why `audio/mono` and `audio/stereo` remain separate exact
    selectors, and tightened the example auto-stop wording to match observed
    asynchronous shutdown behavior
  - 2026-03-27 simplified the checked-in example startup path so each example
    now supports both explicit startup binds and idle startup plus later REST
    source injection, confirmed omitted app names derive from the executable
    name, and closed the remaining Mermaid backlog with four new diagrams
  - 2026-03-27 closed task 9 with the route-oriented SDK, grouped target
    callback fan-out, exact and grouped `session_id` attach, runtime rebind,
    and live-verified example apps; closed task 10 with the repo-native
    browser route-builder UI; and closed tasks 11 and 12 with focused SDK and
    browser tests plus live feature verification on the development host
  - 2026-03-27 reverified live Orbbec persistence after a manual replug,
    recorded that the same SQLite file reloads the same 21 `sv1301s-u3`
    selectors after restart, documented the intentional public IR omission, and
    documented why the public Orbbec depth contract stays normalized to `y16`
  - 2026-03-27 rechecked live Orbbec publication against the donor daemon,
    restored donor-style depth-family format mapping in Orbbec discovery plus
    the 480p catalog probe, confirmed the current host now republishes exact
    depth selectors plus `orbbec/preset/480p_30`, and recorded that raw IR
    discovery remains intentionally outside the current public catalog
  - 2026-03-27 closed task 7 by porting IPC attach into the shared serving
    runtime, fixing idle-worker teardown so exact sessions release devices when
    the last local consumer disconnects, closed the first task-8 slice with
    exact single-channel RTSP publication on a configurable daemon RTSP port,
    vendored mediamtx into this repo, and refreshed the donor-reuse status
  - 2026-03-26 closed task 6 by adding in-memory serving-runtime reuse keyed by
    `stream_id`, exposing serving-runtime topology in session responses plus
    `GET /api/status`, runtime-verifying shared exact-URI reuse on the current
    host, and moving the next implementation handoff to donor IPC delivery
  - 2026-03-26 fixed the Orbbec duplicate-suppression fallback gap, added a
    focused aggregate-discovery regression test, confirmed the current host
    still enumerates one Orbbec plus one V4L2 webcam without duplication, and
    updated donor-reuse notes to reflect the new fallback boundary
  - 2026-03-26 rechecked the task-5 slice against live host behavior,
    corrected tracker underclaims for four already-verified control-plane
    features, confirmed the current hardware inventory on the development
    host, and expanded the task-6 handoff into an explicit start order
  - 2026-03-26 reviewed the three post-task-5 follow-ups, confirmed SQLite
    `FULLMUTEX`, confirmed Orbbec pipeline-profile fallback, recorded pure D2C
    capability gating as a remaining TODO, and refreshed the donor-reuse
    status
  - 2026-03-26 closed grouped-route delete cleanup, runtime-verified the fix
    on the development host, refreshed the donor-reuse writeup, added a
    grouped-route-delete sequence diagram, and rewrote the next-slice handoff
  - 2026-03-26 reran the checked-in direct-session slice on the development
    host, recorded green focused tests plus live direct-session smoke, added a
    direct-session sequence diagram, and moved the handoff to app persistence
  - 2026-03-26 took back redundant `app_sources.target_kind` and
    `app_sources.source_kind`, made session and app-source `stream_id`
    references required, added the missing canonical app-source uniqueness
    indexes, and tightened exact-route ownership to the same app
  - 2026-03-26 resolved the selector/schema review by removing stored
    `selector_key`, adopting compact V4L2 selectors, retaining `orbbec/...`
    namespacing for grouped RGBD selectors, and documenting the next slice
  - 2026-03-26 added a review snapshot of the current slice, a donor-reuse
    status writeup, a schema-keying recommendation for `streams`, and a
    Mermaid backlog for the next runtime slices
  - 2026-03-26 added the persisted discovery catalog slice, including the
    proven Orbbec depth and grouped preset publication path
  - 2026-03-25 added the first implementation-phase report and Mermaid diagram
    inventory for the bootstrap backend slice
- past tasks:
  - `2026-03-27 – Add PipeWire Audio Example And Verify Mono/Stereo Selectors`
  - `2026-03-27 – Simplify Example Startup Paths And Close Mermaid Backlog`
  - `2026-03-27 – Complete Task-9 SDK, Browser Flows, And Runtime Verification`
  - `2026-03-27 – Reverify Live Orbbec Persistence And Document Public Y16 Depth Contract`
  - `2026-03-27 – Restore Live Orbbec Depth And Grouped Catalog Publication`
  - `2026-03-27 – Complete Task-7 IPC Hardening And Task-8 Exact RTSP Publication`
  - `2026-03-26 – Add Serving Runtime Reuse And Runtime-Status Topology`
  - `2026-03-26 – Fix Orbbec Duplicate Suppression Fallback And Add Discovery Regression Coverage`
  - `2026-03-26 – Recheck Task-5 State, Correct Tracker Underclaims, And Detail Task-6 Start Order`
  - `2026-03-26 – Review Post-Task-5 Follow-Ups And Refresh Donor Reuse Status`
  - `2026-03-26 – Close Grouped Route Delete Cleanup And Refresh Runtime Handoff`
  - `2026-03-26 – Review App Route Source Persistence Slice And Reproduce Grouped Route Delete Bug`
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`
  - `2026-03-26 – Apply Selector Review And Device-Scoped Stream Keying`
  - `2026-03-26 – Take Back Redundant App-Source Kind Columns`
  - `2026-03-26 – Review Current Scaffold, Discovery Reuse, And Schema Keying`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Current Slice

The current implementation slice now covers the full documented v1 surface:

- explicit seven-table SQLite schema checked into the repository
- one standalone backend binary, `insightiod`
- persisted catalog reads and alias control for `devices` and `streams`
- direct-session persistence, lifecycle, and status endpoints
- durable app CRUD
- durable app-route CRUD with ambiguity guards
- durable app-source create/list/start/stop/rebind for exact, grouped, and
  session-backed binds
- in-memory serving-runtime reuse across matching active direct sessions and
  app-owned sessions
- local IPC attach over the unix control socket using the donor-grounded
  `memfd` + ring-buffer + `eventfd` transport
- exact single-channel RTSP publication layered on top of the same shared
  serving runtime
- the route-oriented SDK under [sdk/](/home/yixin/Coding/insight-io/sdk)
  with named routes, grouped target fan-out, exact and grouped `session_id`
  attach, and runtime rebind
- the repo-native browser UI under
  [frontend/](/home/yixin/Coding/insight-io/frontend) for catalog browse,
  app create, route declaration, source bind/rebind/start/stop, and restart
  recovery
- example apps for webcam latency, PipeWire audio monitoring, Orbbec grouped
  overlay, and mixed-device routing under
  [examples/](/home/yixin/Coding/insight-io/examples) that can now either
  start with a CLI bind or start idle for later REST injection, while deriving
  the default app name from the executable when `--app-name` is omitted
- runtime-status inspection that surfaces serving-runtime ownership, consumer
  session ids, resolved source metadata, additive RTSP intent, IPC channel
  facts, and runtime RTSP publication details
- focused tests that prove schema bootstrap, catalog shaping, REST lifecycle
  paths, app-source behavior, direct-session behavior, IPC runtime teardown,
  SDK callback delivery, runtime rebind, idle-until-bind behavior, and
  browser/static serving
- live verification on this host for:
  - bare exact-URI CLI startup
  - bare exact-URI CLI startup for PipeWire stereo audio
  - idle example startup with omitted app name plus later REST bind for the
    webcam latency app
  - idle PipeWire audio example startup plus later REST bind for mono audio
  - idle grouped-preset startup plus later REST bind for Orbbec overlay at
    `480p` and `720p`
  - idle mixed-device startup plus later REST bind for one exact webcam source
    and one grouped Orbbec preset
  - grouped preset CLI startup at `480p` and `720p`
  - exact and grouped `session_id` attach
  - late REST bind for exact video and exact depth
  - runtime rebind from V4L2 webcam to Orbbec color
  - mixed-device fan-in
  - browser create/route/bind/start/stop/restart flow

In concrete HTTP terms, the current backend now serves:

- `GET /`
- `GET /api/health`
- `GET /api/devices`
- `POST /api/devices:refresh`
- `GET /api/devices/{device}`
- `POST /api/devices/{device}/alias`
- `POST /api/sessions`
- `GET /api/sessions`
- `GET /api/sessions/{id}`
- `POST /api/sessions/{id}:start`
- `POST /api/sessions/{id}:stop`
- `DELETE /api/sessions/{id}`
- `POST /api/apps`
- `GET /api/apps`
- `GET /api/apps/{id}`
- `DELETE /api/apps/{id}`
- `POST /api/apps/{id}/routes`
- `GET /api/apps/{id}/routes`
- `GET /api/apps/{id}/routes/{route}`
- `DELETE /api/apps/{id}/routes/{route}`
- `GET /api/apps/{id}/sources`
- `GET /api/apps/{id}/sources/{source_id}`
- `POST /api/apps/{id}/sources`
- `POST /api/apps/{id}/sources/{source_id}:start`
- `POST /api/apps/{id}/sources/{source_id}:stop`
- `POST /api/apps/{id}/sources/{source_id}:rebind`
- `GET /api/status`

## Review Snapshot

The current implementation was rechecked against both the code and a live run
on the development host.

Observed from the current audit:

- build succeeds with Orbbec and PipeWire enabled
- `ctest` is green across:
  - `schema_store_test`
  - `catalog_service_test`
  - `discovery_test`
  - `session_service_test`
  - `rest_server_test`
  - `app_service_test`
  - `ipc_runtime_test`
- live `insightiod` smoke on this host returned four devices through
  `GET /api/devices`:
  - one V4L2 webcam
  - one SDK-backed Orbbec RGBD device
  - two PipeWire audio devices
- both current PipeWire audio devices publish both `audio/mono` and
  `audio/stereo` at `s16le` `48000`; these remain separate exact selectors
  because they deliver different channel counts
- the Orbbec duplicate-suppression fallback fix is now verified in two ways:
  - focused `discovery_test` proves V4L2 fallback stays visible when Orbbec
    SDK discovery is empty or throws, and proves suppression activates once a
    usable Orbbec device is discovered
  - live `GET /api/devices` on this host still returns exactly one SDK-backed
    Orbbec device plus one V4L2 webcam, with no duplicate V4L2 shadow entry
    for the Orbbec camera
- a follow-up live rerun on 2026-03-27 confirmed the same `sv1301s-u3`
  device now republishes:
  - exact color selectors such as `orbbec/color/480p_30`
  - exact depth selectors including `orbbec/depth/400p_30`,
    `orbbec/depth/480p_30`, and native `320x200` and `800p` depth families
  - grouped `orbbec/preset/480p_30`
- a manual replug follow-up rerun against the same SQLite file then confirmed
  that a restart still reloads the same 21 live `sv1301s-u3` selectors from
  the catalog on this host
- raw Orbbec SDK/config enumeration still contains depth-family formats such as
  `Y10`, `Y11`, `Y12`, and `Y14`, but the checked-in public depth contract
  stays normalized to `y16` because:
  - the checked-in worker selects those profiles under one depth-like match and
    reports live first frames as `format=y16`
  - the bundled Orbbec SDK examples request or inspect `OB_FORMAT_Y16` for
    depth in `Sample-DepthViewer`, `Sample-AlignFilterViewer`,
    `Sample-PostProcessing`, and `Sample-DepthUnitControl`
  - the donor `rgbd_proximity_capture` example also accepts only
    `y16/gray16/z16` for depth consumption
- raw Orbbec discovery now matches the donor daemon for `color`, `depth`, and
  `ir` on this host, but the checked-in public catalog still intentionally
  omits `ir` because the active v1 docs only define color/depth exact members
  plus grouped preset publication and no first-class IR consumer path
- exact IPC attach is now live-verified through the repo-native unix control
  socket for:
  - `insightos://localhost/web-camera/720p_30`
  - `insightos://localhost/web-camera-mono/audio/mono`
  - `insightos://localhost/sv1301s-u3/orbbec/color/480p_30`
- the checked-in
  [pipewire_audio_monitor](/home/yixin/Coding/insight-io/examples/pipewire_audio_monitor.cpp)
  example is now live-verified in both startup modes:
  - direct CLI startup on `insightos://localhost/web-camera-mono/audio/stereo`
    delivered `channels = 2`
  - idle startup plus later REST bind on
    `insightos://localhost/web-camera-mono/audio/mono` delivered
    `channels = 1`
- idle IPC teardown is now live-verified:
  - after the last local IPC consumer disconnects, the runtime returns to
    `state = ready`
  - `attached_consumer_count` returns to `0`
  - `frames_published` resets to `0`
  - the webcam becomes available again to `v4l2-ctl`
- the live direct-session smoke flow succeeded for
  `insightos://localhost/web-camera/720p_30`:
  - `POST /api/sessions`
  - `GET /api/sessions`
  - `GET /api/sessions/{id}`
  - `GET /api/status`
  - `POST /api/sessions/{id}:stop`
  - backend restart with the same SQLite file
  - `POST /api/sessions/{id}:start`
  - `DELETE /api/sessions/{id}` after the session was unreferenced
- the live app/route/source smoke flow also succeeded on this host:
  - `POST /api/apps`
  - `GET /api/apps`
  - `POST /api/apps/{id}/routes`
  - `GET /api/apps/{id}/routes`
  - `POST /api/apps/{id}/sources` with exact URI input
  - `POST /api/apps/{id}/sources/{source_id}:stop`
  - `POST /api/apps/{id}/sources/{source_id}:start`
  - `POST /api/apps/{id}/sources/{source_id}:rebind`
  - backend restart with the same SQLite file
  - `GET /api/apps`
  - `GET /api/apps/{id}/sources` showing persisted rows normalized to `stopped`
- route rebind release is now live-verified:
  - one app source can rebind from `web-camera/720p_30` to
    `sv1301s-u3/orbbec/color/480p_30`
  - `GET /api/status` then drops the webcam runtime and keeps only the Orbbec
    runtime
  - the webcam becomes available again to `v4l2-ctl`
- serving-runtime reuse is now live-verified for one repeated exact URI:
  - one direct session for `insightos://localhost/web-camera/720p_30`
    reports one `serving_runtime` with `consumer_count = 1`
  - a second direct session for the same URI with `rtsp_enabled = true`
    reports the same `runtime_key` with `consumer_count = 2`,
    `shared = true`, and additive `rtsp_enabled = true`
  - one URI-backed app source on `yolov5` for the same URI creates one
    app-owned logical session whose nested active-session metadata reports the
    same shared runtime with `consumer_count = 3`
  - `GET /api/status` now returns one `serving_runtimes` entry for that exact
    URI with `owner_session_id`, `consumer_session_ids`, resolved source
    metadata, and additive RTSP intent
- exact RTSP publication is now live-verified on a non-default daemon RTSP
  port:
  - the daemon was started with `--rtsp-host 127.0.0.1 --rtsp-port 18554`
  - the vendored `third_party/mediamtx/mediamtx` server was started on
    `:18554`
  - a second direct session for `web-camera/720p_30` with
    `rtsp_enabled = true` upgraded the shared runtime to
    `rtsp_publication.state = active`
  - strict FFmpeg validation against
    `rtsp://127.0.0.1:18554/web-camera/720p_30` completed cleanly with an
    empty `errors.log`
  - stopping the RTSP-requiring session returned the still-shared runtime to
    `state = ready` with `rtsp_publication = null`
- session-backed bind verification also succeeded:
  - `POST /api/sessions`
  - `POST /api/apps/{id}/sources` with `session_id`
  - `DELETE /api/sessions/{id}` returning `409 Conflict`
- exact source-response identity is now live-verified:
  - `POST /api/apps/{id}/sources` returns `resolved_exact_stream_id`
  - exact-source responses also include `target`, `uri`, `state`,
    `rtsp_enabled`, and nested active-session metadata
- exact app-source stop/start preservation is now live-verified:
  - `POST /api/apps/{id}/sources/{source_id}:stop` keeps the durable row and
    clears only `active_session_id`
  - `POST /api/apps/{id}/sources/{source_id}:start` recreates runtime state
    and links a fresh app-owned session to the same durable source row
- exact route expectation rejection is now live-verified:
  - `POST /api/apps/{id}/sources` returns `422 Unprocessable Content`
    with `route_expectation_mismatch` when `web-camera/720p_30` is bound to
    `orbbec/depth`
- URI host validation is now live on both paths:
  - `POST /api/sessions` rejects `insightos://not-local/...`
  - `POST /api/apps/{id}/sources` rejects `insightos://not-local/...`
- grouped bind verification succeeded against the live Orbbec preset:
  - `POST /api/apps/{id}/sources` with
    `insightos://localhost/sv1301s-u3/orbbec/preset/480p_30`
  - grouped `resolved_members_json` persisted and returned
- grouped member-route delete cleanup is now live-verified:
  - deleting `orbbec/depth` from an app with one grouped `orbbec` bind
    returns `204 No Content`
  - `GET /api/apps/{id}/sources` returns no grouped bind afterwards
  - the grouped app-owned session returns `404 Not Found`
  - SQLite no longer retains the grouped bind row or the linked app-owned
    grouped session row

Important scope boundary:

- discovery/catalog publication is runtime-verified on the development host
- direct-session REST and persistence are runtime-verified on the development
  host for the checked-in slice
- app, route, and source persistence are runtime-verified on the development
  host for the current worktree slice
- serving-runtime reuse, IPC attach, idle-worker teardown, and exact
  single-channel RTSP publication are now implemented in this repository
- SDK callbacks and frontend flows are now implemented and verification-backed
  in this repository

This matches the current feature trackers:

- `docs/features/fullstack-intent-routing-e2e.json` now records the full
  documented feature set as passing after the SDK, browser, grouped attach,
  rebind, and runtime-verification sweep
- `docs/features/runtime-and-app-user-journeys.json` now marks direct-session
  create, persisted restart, referenced-session delete conflict, exact
  source-response identity, app-source stop/start preservation, idle-app route
  declaration, grouped-route delete cleanup, the additive RTSP shared-runtime
  journey, SDK callback delivery, runtime rebind, and browser restart recovery
  as passing where those flows were actually verified

## Donor Reuse Status

This section was rechecked against the current `../insightos` tree while
closing tasks 7 and 8.

### Already Reused

- discovery code shape and implementation strategy were directly ported from
  `../insightos` for:
  - V4L2 discovery
  - PipeWire discovery
  - Orbbec SDK discovery
- the local IPC transport and unix control-socket pattern are now ported from
  donor form into repo-native serving-runtime ownership:
  - `backend/include/insightio/backend/ipc.hpp`
  - `backend/src/ipc/ipc.cpp`
  - `backend/include/insightio/backend/unix_socket.hpp`
  - `backend/src/ipc/unix_socket.cpp`
- donor RTSP publication planning and ffmpeg-publisher structure are now
  reused in narrowed form for exact single-channel runtime publication:
  - `backend/src/publication/rtsp_publisher.cpp`
- the donor `mediamtx` payload is now vendored under
  `third_party/mediamtx/`
- the Orbbec SDK linkage itself is reused by building against the donor SDK
  tree under `../insightos/third_party/orbbec_sdk/SDK`
- the `cpp-httplib` integration pattern is also reused
- current `backend/CMakeLists.txt` still reflects that split correctly:
  donor-backed discovery, IPC transport, and SDK linkage are in the build
  graph, while donor delivery tables and `session_manager.cpp` semantics are
  still not compiled into `insight-io`

Evidence:

- current build linkage:
  `backend/CMakeLists.txt`
- current discovery files:
  `backend/src/discovery/*.cpp`
- donor counterparts:
  `../insightos/backend/src/discovery/*.cpp`

Concrete comparison points:

- current V4L2 enumeration loop in
  [v4l2_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/v4l2_discovery.cpp)
  matches the donor structure in
  [v4l2_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/v4l2_discovery.cpp)
- current Orbbec enumeration and conditional vendor-skip plumbing in
  [orbbec_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/orbbec_discovery.cpp)
  and [discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/discovery.cpp)
  are direct ports of the donor flow in
  [orbbec_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/orbbec_discovery.cpp)
  and [discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/discovery.cpp)
- current PipeWire enumeration in
  [pipewire_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/pipewire_discovery.cpp)
  closely follows the donor implementation in
  [pipewire_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/pipewire_discovery.cpp)

### Partially Reused

- shared type helpers are ported and trimmed, especially stable device-key
  generation and capability naming
- donor request-normalization ideas are reused only in narrow form:
  the current `SessionService` and `AppService` each keep their own
  `insightos://` parsing and local-host validation rather than importing the
  donor request layer wholesale
- Orbbec grouped-preset shaping is not copied verbatim from donor code, but it
  is informed by donor probe evidence and the donor runtime contract
- donor duplicate-suppression behavior for Orbbec vendor IDs is reused in
  narrowed form:
  `insight-io` still suppresses matching V4L2 nodes by vendor ID, but only
  after SDK-backed Orbbec discovery has actually yielded at least one usable
  Orbbec catalog device

### Not Yet Reused But Highly Relevant

- worker/runtime graph:
  donor `session_manager.cpp` plus the V4L2, PipeWire, and Orbbec worker files
  remain reference material only
- request normalization:
  donor `SessionRequest`, URI parsing, and source normalization logic are only
  lightly reintroduced for the current direct-session slice; the broader
  donor request-routing layer remains unported
- worker design:
  donor `session_manager.cpp` and the worker files remain the right reference
  set for reuse, attach, publication, and runtime planning, but the new
  `insight-io` control plane should still keep the new route/app-source
  contract rather than reviving donor request or store semantics

### Not Suitable To Copy As-Is

- donor device/session store schema and REST/session behavior should not be
  copied directly because they encode the older preset-and-delivery contract
  rather than the new `insight-io` app-target contract
- donor runtime tables and delivery-session persistence are intentionally
  outside the v1 schema for this repository
- donor aggregate discovery behavior should still not be copied forward
  blindly:
  the inherited Orbbec vendor-skip rule needed one repo-native adjustment here
  so SDK discovery failure or emptiness no longer hides generic V4L2 fallback

## Schema Review

### Current `streams` Keying

The reviewed implementation now stores:

- `selector TEXT NOT NULL`
- `UNIQUE(device_id, selector)`

This keeps selector identity attached to the owning device row instead of
storing a duplicated concatenated identifier.

### Recommendation

The recommendation from the previous review is now the active design:

- keep `selector` as the stable per-device terminal identifier
- enforce `UNIQUE(device_id, selector)` via the parent relation
- derive any compound resource name at the API boundary instead of storing a
  concatenated `selector_key`

If a separate immutable identifier is genuinely needed later, add one opaque
`uid` field rather than duplicating the parent key into a second stored string.

Rationale:

- it removes redundant storage and redundant update logic
- it makes the uniqueness rule explicit at the actual ownership boundary:
  selector uniqueness is per device, not global in the abstract
- it follows the Google API design preference for one canonical resource name
  plus an optional separate immutable `uid`, rather than several overlapping
  identifiers for the same resource

Relevant references:

- Google AIP-121 Resource-oriented Design: https://google.aip.dev/121
- Google AIP-122 Resource Names: https://google.aip.dev/122
- Google AIP-136 Custom Methods: https://google.aip.dev/136
- Google AIP-148 Standard Fields, especially `uid`: https://google.aip.dev/148

Applied to this project, the clean model is:

- public copied handle: derived `insightos://<host>/<device>/<selector>`
- durable relational uniqueness: `(device_id, selector)`
- optional future opaque id: `stream_uid`

Not recommended:

- treating `selector_key` as if it were a first-class immutable resource id
  when it is currently just `device_key` plus `selector`

### `app_sources` Kind Columns

The next schema takeback is the same pattern in a different table.

The previous schema carried:

- `target_kind`
- `source_kind`

Those fields duplicated information already present in the row:

- exact versus grouped bind kind is derivable from whether `route_id` is
  populated
- URI-backed versus session-backed bind kind is derivable from whether
  `source_session_id` is populated

The active recommendation is now also the active canonical schema:

- remove stored `target_kind`
- remove stored `source_kind`
- keep `target_name` as the only posted target field
- keep `stream_id` required because every durable bind still chooses one exact
  stream row or one grouped preset row
- keep `source_session_id` optional to mark session-backed binds
- keep `active_session_id` optional to record the currently serving runtime

This removes duplicated state without losing any product meaning.

### Missing Canonical Indexes

Another debt in the earlier SQL was that the docs described app-source
uniqueness, but the canonical schema did not actually define the indexes.

The active SQL should keep:

- unique index on `(app_id, target_name)`
- partial unique index on `(app_id, route_id)` where `route_id IS NOT NULL`

Without those indexes, the contract that one target owns at most one active
durable binding is only aspirational.

## Live Findings

### Remaining Schema Debt

The selector cleanup itself is complete in the canonical SQL:

- [001_initial.sql](/home/yixin/Coding/insight-io/backend/schema/001_initial.sql)
  stores `selector` and enforces `UNIQUE(device_id, selector)`
- it does not store `selector_key`
- it makes both `sessions.stream_id` and `app_sources.stream_id` required
- it does not store `app_sources.target_kind` or `app_sources.source_kind`

The remaining concrete schema debt is outside the SQL and lives in checked-in
test and runtime code:

- [session_service.cpp](/home/yixin/Coding/insight-io/backend/src/session_service.cpp)
  still reads sessions through `LEFT JOIN streams` with `COALESCE(...)`
  defaults, which preserves code paths that behave as if `sessions.stream_id`
  could be absent even though the schema now forbids that row shape
- [session_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/session_service_test.cpp)
  now uses the canonical app-route and app-source row shape for the
  delete-conflict case, so the focused test suite is green again

### Discovery Reuse Audit

The rerun now shows three separate states:

- donor discovery backends are clearly reused in structure and code
- catalog shaping for focused tests is functioning
- the real host-discovery path is now working on the development host for the
  webcam, Orbbec, and PipeWire devices, and the Orbbec fallback boundary is now
  covered by a focused regression test plus a live host smoke

That means reuse status should be read as:

- discovery/device enumeration:
  substantially reused and now proven on the development host for the current
  connected devices
- Orbbec SDK integration:
  partially reused but live device publication is now proven in `insight-io`
- IPC mechanism:
  not reused yet, but highly relevant
- worker/runtime design:
  not reused yet, but highly relevant as reference material only

### App-Local Route Ownership

One more schema gap remained after removing the redundant kind columns.

The earlier SQL let `app_sources.route_id` point at `app_routes.route_id`
globally, which means one app-source row could reference a route that belongs
to some other app entirely.

The active SQL should instead keep:

- `UNIQUE(app_id, route_id)` on `app_routes`
- `FOREIGN KEY (app_id, route_id) REFERENCES app_routes(app_id, route_id)
  ON DELETE CASCADE` on `app_sources`

That change does two useful things:

- exact-route binds become durably app-local rather than depending on runtime
  discipline
- deleting one declared route cascades only the exact-route app-source rows
  that actually target that route, while grouped rows remain `route_id = NULL`

### Selector Naming Review

The review comment about selector names split into two different cases.

Resolved decision:

- plain V4L2 webcam selectors should be compact terminal identifiers such as
  `720p_30` and `1080p_30`
- grouped Orbbec selectors should keep the `orbbec/...` namespace

Rationale for the V4L2 change:

- `media_kind` already carries `video`, so `video-720p_30` duplicated meaning
- the URI path stays shorter and easier to copy without losing information
- the selector still remains stable within one device catalog

Rationale for keeping `orbbec/...`:

- the same device publishes both exact members and grouped presets, so the
  namespace marks one selector family rather than one media kind
- grouped target vocabulary already uses `orbbec`, `orbbec/color`, and
  `orbbec/depth`, so keeping the selector namespace aligned reduces ambiguity
- dropping the namespace would make grouped RGBD selectors read like generic
  leaf names even though their meaning is tied to the grouped family contract

In short:

- remove redundant prefixes when they only restate media kind
- keep namespaces when they express the grouped-device family contract

## Current Conclusion

Tasks 1 through 12 are now closed in the current worktree.

The main implementation takeaway is that the active design held up without
needing another surface split:

1. the existing REST-backed app/route/source control plane was sufficient for
   both the SDK and the browser client
2. SDK delivery could stay on the current IPC runtime rather than inventing a
   parallel callback transport
3. grouped preset callback delivery and grouped `session_id` attach both fit
   behind the same public `target` surface
4. the browser flow could stay a thin control-plane client over the same
   resource model instead of introducing UI-only endpoints
5. live hardware verification on webcam plus Orbbec was enough to close the
   remaining tracker gaps once the task-9/browser slice landed

## Mermaid Diagram Inventory

- [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  documents the durable schema defined by the active data model
- [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  documents the intended control-plane and runtime boundary for the full system
- [bootstrap-health-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/bootstrap-health-sequence.md)
  documents the currently implemented backend bootstrap and health-check path
- [catalog-discovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/catalog-discovery-sequence.md)
  documents discovery refresh, persistence, catalog reads, and alias updates
- [direct-session-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/direct-session-sequence.md)
  documents create, restart, status, and delete flow for one direct session
- [app-route-source-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/app-route-source-sequence.md)
  documents app create, route declaration, source bind, stop/start, and rebind
- [sdk-idle-rest-bind-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/sdk-idle-rest-bind-sequence.md)
  documents idle SDK startup, later REST bind, IPC attach, and route callback
  delivery
- [browser-route-builder-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/browser-route-builder-sequence.md)
  documents the repo-native browser flow for catalog browse, app create,
  route declaration, source bind, source restart, and restart recovery
- [grouped-route-delete-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/grouped-route-delete-sequence.md)
  documents grouped bind cleanup when one member route is deleted
- [exact-session-attach-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/exact-session-attach-sequence.md)
  documents exact `session_id` attach from one existing direct session into
  one app-local route
- [grouped-session-attach-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/grouped-session-attach-sequence.md)
  documents grouped `session_id` attach from one grouped direct session into
  one grouped target root
- [browser-restart-recovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/browser-restart-recovery-sequence.md)
  documents the browser-specific persisted-source reload and explicit restart
  flow after backend restart
- [discovery-runtime-boundary-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/discovery-runtime-boundary-sequence.md)
  documents the discovery-versus-runtime responsibility boundary and the
  guarantee that discovery publishes catalog rows without starting runtime
- [shared-serving-runtime-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/shared-serving-runtime-sequence.md)
  documents shared exact-URI runtime reuse plus additive RTSP intent in the
  current task-6 slice
- [exact-rtsp-publication-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/exact-rtsp-publication-sequence.md)
  documents exact single-channel RTSP publication on top of the shared runtime
- [ipc-idle-teardown-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/ipc-idle-teardown-sequence.md)
  documents IPC attach, idle disconnect, worker stop, and clean restart

## Recommended Mermaid Backlog

Status: `resolved`

The previously recommended backlog is now checked in as:

- [exact-session-attach-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/exact-session-attach-sequence.md)
- [grouped-session-attach-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/grouped-session-attach-sequence.md)
- [browser-restart-recovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/browser-restart-recovery-sequence.md)
- [discovery-runtime-boundary-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/discovery-runtime-boundary-sequence.md)

## Implementation Notes

- the checked-in schema already uses the v1 durable table inventory from the
  active docs instead of reviving donor-only runtime tables
- the bootstrap server deliberately keeps the runtime surface small so later
  feature slices can add discovery and session logic without first undoing a
  mismatched baseline
- the connected Orbbec device now exposes raw `color`, `depth`, and `ir`
  streams in the current backend and in the donor daemon on this host, and the
  checked-in catalog again republishes the documented exact depth selectors and
  grouped `orbbec/preset/480p_30` for the proven `sv1301s-u3` family
  (`2bc5:0614`)
- even though the underlying SDK config also lists raw depth-family names such
  as `Y10`, `Y11`, `Y12`, and `Y14`, the checked-in public contract continues
  to publish Orbbec depth as `y16` because that matches the delivered worker
  type and the supported SDK/example consumer surface
- serial-specific gating is gone, but pure SDK D2C capability gating is not yet
  the authoritative publication rule:
  the current catalog still accepts the proven family/hardcoded 480p path, and
  replacing that with a pure capability probe remains follow-up work
- the donor `cpp-httplib` integration pattern was reused, but the runtime
  contract remains grounded in the `insight-io` docs rather than donor REST
  behavior
- donor reuse status is currently split:
  V4L2, PipeWire, and Orbbec discovery are substantially donor-derived, the
  Orbbec SDK is now vendored locally under this repo, and the donor IPC,
  request-normalization, and worker/session-manager layers are still pending
  integration work rather than checked-in reuse
- grouped-route delete cleanup is now handled in repo-native
  `AppService` lifecycle logic before the route row is removed:
  grouped binds are discovered through `resolved_routes_json`, removed, and any
  linked app-owned grouped session is deleted in the same transaction
