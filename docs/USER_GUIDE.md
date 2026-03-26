# User Guide

## Role

- role: operator and developer guide for the checked-in `insight-io` runtime
- status: active
- version: 11
- major changes:
  - 2026-03-26 added serving-runtime reuse across matching direct sessions and
    app-owned sources, documented the new `serving_runtime` and
    `serving_runtimes` status fields, and added a live shared-runtime smoke
    walkthrough
  - 2026-03-26 fixed the Orbbec duplicate-suppression fallback gap, added a
    focused discovery regression test, and updated the operator guide so it no
    longer describes generic V4L2 fallback as an open caveat when SDK-backed
    Orbbec discovery is absent
  - 2026-03-26 rechecked the task-5 control-plane slice on the development
    host, confirmed exact source responses now expose
    `resolved_exact_stream_id`, confirmed app-source stop/start preserves the
    durable declaration, confirmed referenced-session delete returns
    `409 Conflict`, and added a focused control-plane review walkthrough
  - 2026-03-26 closed grouped-member-route delete cleanup in the guide,
    replaced the old bug reproduction with a live cleanup smoke test, and kept
    the app/route/source walkthrough aligned with the current backend
  - 2026-03-26 reran the checked-in direct-session slice on the development
    host: focused tests are green, live discovery returns the webcam, Orbbec,
    and PipeWire devices, and the guide now includes a direct-session smoke
    walkthrough
  - 2026-03-26 aligned the guide with the reviewed selector contract: V4L2
    selectors are plain `720p_30` style, Orbbec selectors remain namespaced,
    and SQLite inspection now shows device-scoped selector uniqueness
  - 2026-03-26 added a review-backed walkthrough for verifying the current
    discovery/catalog slice on the real development host and called out the
    current hardware observations plus the Orbbec duplicate-suppression rule
  - 2026-03-26 added catalog browsing and alias commands for the persisted
    discovery slice
  - 2026-03-25 added initial build, test, and backend startup instructions for
    the bootstrap slice
- past tasks:
  - `2026-03-26 – Add Serving Runtime Reuse And Runtime-Status Topology`
  - `2026-03-26 – Fix Orbbec Duplicate Suppression Fallback And Add Discovery Regression Coverage`
  - `2026-03-26 – Recheck Task-5 State, Correct Tracker Underclaims, And Detail Task-6 Start Order`
  - `2026-03-26 – Close Grouped Route Delete Cleanup And Refresh Runtime Handoff`
  - `2026-03-26 – Review App Route Source Persistence Slice And Reproduce Grouped Route Delete Bug`
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`
  - `2026-03-26 – Review Current Scaffold, Discovery Reuse, And Schema Keying`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Scope

This guide covers the current worktree slice only:

- configure and build the backend
- run the focused tests
- start `insightiod`
- query `GET /api/health`
- inspect `GET /api/devices`
- update `POST /api/devices/{device}/alias`
- create, inspect, start, stop, and delete direct sessions through the REST API
- create, inspect, and delete apps
- create, inspect, and delete routes
- create exact, grouped, and session-backed app-source bindings
- stop, restart, and rebind app sources
- inspect `/api/status`

SDK callbacks, IPC attach, active RTSP serving runtime, and frontend
management are still not available in this worktree slice.

## Build

```bash
cmake -S . -B build
cmake --build build -j4
```

Expected binaries:

- `build/bin/insightiod`
- `build/bin/schema_store_test`
- `build/bin/catalog_service_test`
- `build/bin/discovery_test`
- `build/bin/session_service_test`
- `build/bin/rest_server_test`
- `build/bin/app_service_test`

## Test

```bash
ctest --test-dir build --output-on-failure
```

Current checked-in result on the development host:

- `schema_store_test`: pass
- `catalog_service_test`: pass
- `discovery_test`: pass
- `session_service_test`: pass
- `rest_server_test`: pass
- `app_service_test`: pass

## Run

```bash
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18180 \
  --db-path /tmp/insight-io.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1
```

Notes:

- `--db-path` points to the SQLite file initialized from
  [001_initial.sql](/home/yixin/Coding/insight-io/backend/schema/001_initial.sql)
- `--frontend` is accepted now so later frontend slices can use the same CLI,
  but no static frontend is served yet

## Health Check

```bash
curl -s http://127.0.0.1:18180/api/health
```

Expected response shape:

```json
{
  "catalog_device_count": 4,
  "db_path": "/tmp/insight-io.sqlite3",
  "frontend_path": "/tmp/frontend",
  "session_count": 0,
  "status": "ok",
  "version": "0.1.0"
}
```

## Device Catalog

```bash
curl -s http://127.0.0.1:18180/api/devices | jq
```

On the current development machine, the catalog currently shows:

- one V4L2 webcam with selectors such as `720p_30`
- one Orbbec device with selectors including `orbbec/depth/400p_30`,
  `orbbec/depth/480p_30`, and `orbbec/preset/480p_30`
- two PipeWire audio sources when PipeWire discovery is enabled

## Device Alias

```bash
curl -s -X POST http://127.0.0.1:18180/api/devices/web-camera/alias \
  -H 'Content-Type: application/json' \
  -d '{"public_name":"front-camera"}' | jq
```

After aliasing, both the derived `insightos://` URI and
`publications_json.rtsp.url` should use `front-camera` in the device path.

## Direct Session Smoke Test

Create one direct session from a listed URI:

```bash
curl -s -X POST http://127.0.0.1:18180/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":true}' | jq
```

Inspect the persisted direct-session slice:

```bash
curl -s http://127.0.0.1:18180/api/sessions | jq
curl -s http://127.0.0.1:18180/api/sessions/1 | jq
curl -s http://127.0.0.1:18180/api/status | jq
```

Exercise the remaining lifecycle endpoints:

```bash
curl -s -X POST http://127.0.0.1:18180/api/sessions/1/stop \
  -H 'Content-Type: application/json' -d '{}' | jq

curl -s -X POST http://127.0.0.1:18180/api/sessions/1/start \
  -H 'Content-Type: application/json' -d '{}' | jq

curl -s -X POST http://127.0.0.1:18180/api/sessions/1/stop \
  -H 'Content-Type: application/json' -d '{}' | jq

curl -i -X DELETE http://127.0.0.1:18180/api/sessions/1
```

To verify restart normalization, stop the session, restart `insightiod` with
the same `--db-path`, and then inspect `GET /api/sessions/1` before calling
`POST /api/sessions/1/start` again.

## Review Walkthrough

Use this when verifying what is actually implemented today rather than what the
full docs backlog intends later.

### Build And Focused Tests

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The current worktree test scope is intentionally narrow:

- schema bootstrap
- persisted catalog shaping
- direct-session lifecycle and status verification
- durable app, route, and app-source persistence plus lifecycle verification
- REST health, catalog, alias, direct-session, and app/route/source surfaces

Current audit result:

- `ctest` is green
- focused coverage now includes schema bootstrap, catalog shaping, direct
  session lifecycle, app/route/source lifecycle, restart normalization,
  delete-conflict handling, grouped-route delete cleanup, and REST surface
  checks

### Live Discovery Review

```bash
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18183 \
  --db-path /tmp/insight-io-review.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18183/api/health | jq
curl -s http://127.0.0.1:18183/api/devices | jq
```

Current audit result on the development host:

- `GET /api/devices` returned four online devices on this host:
  - one V4L2 webcam
  - one Orbbec camera
  - two PipeWire audio devices
- the webcam retained compact selectors such as `720p_30`
- the Orbbec device exposed `orbbec/depth/400p_30`,
  `orbbec/depth/480p_30`, and `orbbec/preset/480p_30`
- the direct-session smoke flow using `insightos://localhost/web-camera/720p_30`
  succeeded for create, inspect, status, stop, restart, and delete

### App-Source Control-Plane Review

```bash
app_id=$(curl -s -X POST http://127.0.0.1:18183/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"guide-review-app"}' | jq -r '.app_id')

curl -s -X POST http://127.0.0.1:18183/api/apps/${app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"yolov5","expect":{"media":"video"}}'

source_json=$(curl -s -X POST http://127.0.0.1:18183/api/apps/${app_id}/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"yolov5"}')
printf '%s\n' "${source_json}" | jq

source_id=$(printf '%s' "${source_json}" | jq -r '.source_id')

curl -s -X POST http://127.0.0.1:18183/api/apps/${app_id}/sources/${source_id}/stop \
  -H 'Content-Type: application/json' -d '{}' | jq

curl -s -X POST http://127.0.0.1:18183/api/apps/${app_id}/sources/${source_id}/start \
  -H 'Content-Type: application/json' -d '{}' | jq

mismatch_app_id=$(curl -s -X POST http://127.0.0.1:18183/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"guide-mismatch-app"}' | jq -r '.app_id')

curl -s -X POST http://127.0.0.1:18183/api/apps/${mismatch_app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}'

curl -s -i -X POST http://127.0.0.1:18183/api/apps/${mismatch_app_id}/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"orbbec/depth"}'

session_id=$(curl -s -X POST http://127.0.0.1:18183/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30"}' | jq -r '.session_id')

bind_app_id=$(curl -s -X POST http://127.0.0.1:18183/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"guide-delete-conflict-app"}' | jq -r '.app_id')

curl -s -X POST http://127.0.0.1:18183/api/apps/${bind_app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"cam","expect":{"media":"video"}}'

curl -s -X POST http://127.0.0.1:18183/api/apps/${bind_app_id}/sources \
  -H 'Content-Type: application/json' \
  -d "{\"session_id\":${session_id},\"target\":\"cam\"}" | jq

curl -s -i -X DELETE http://127.0.0.1:18183/api/sessions/${session_id}
```

Current audit result on the development host:

- exact app-source responses now include `target`, `uri`, `state`,
  `rtsp_enabled`, `resolved_exact_stream_id`, and nested session metadata
- `POST /api/apps/{id}/sources/{source_id}/stop` keeps the durable source row
  and clears only the active runtime link
- `POST /api/apps/{id}/sources/{source_id}/start` recreates runtime state and
  links a fresh app-owned session to the same durable source row
- `POST /api/apps/{id}/sources` now returns `422 Unprocessable Content` with
  `route_expectation_mismatch` when source media and route expectation disagree
- `DELETE /api/sessions/{id}` now returns `409 Conflict` while one app-source
  row still references that session

### SQLite Review

```bash
sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT device_id, device_key, public_name, driver, status \
   FROM devices ORDER BY public_name;"

sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT stream_id, device_id, selector, media_kind, shape_kind \
   FROM streams ORDER BY device_id, selector;"
```

During discovery-only runs this confirms that the current runtime is
persisting the catalog rows in `devices` and `streams`. During the direct
session and app-source smoke flows, `apps`, `app_routes`, `app_sources`,
`sessions`, and `session_logs` also gain rows as expected.

### Duplicate Orbbec Suppression

When Orbbec SDK discovery is enabled, the backend first asks the Orbbec path
for usable SDK-backed devices and only then skips matching V4L2 USB nodes
during generic V4L2 enumeration. This is the current guard against the known
issue where the Orbbec camera can otherwise look like a plain V4L2 camera in
Linux device lists.

If the Orbbec SDK is unavailable at build or run time, that suppression path is
not active, so the hardware may reappear only through the generic V4L2 route.

## Direct Session Slice

The current backend now exposes these session endpoints:

- `POST /api/sessions`
- `GET /api/sessions`
- `GET /api/sessions/{id}`
- `POST /api/sessions/{id}/start`
- `POST /api/sessions/{id}/stop`
- `DELETE /api/sessions/{id}`
- `GET /api/status`

The focused tests plus the live smoke flow above verify the direct-session
REST and persistence slice on this host. Serving-runtime reuse is now also
implemented and inspectable, but IPC attach, SDK callbacks, and active RTSP
publication runtime are still future work.

## Shared Runtime Reuse Smoke Test

Use this when verifying task 6 on the development host.

```bash
first=$(curl -s -X POST http://127.0.0.1:18180/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":false}')

second=$(curl -s -X POST http://127.0.0.1:18180/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":true}')

app_id=$(curl -s -X POST http://127.0.0.1:18180/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"runtime-reuse-guide"}' | jq -r '.app_id')

curl -s -X POST http://127.0.0.1:18180/api/apps/${app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"yolov5","expect":{"media":"video"}}' | jq

source_json=$(curl -s -X POST http://127.0.0.1:18180/api/apps/${app_id}/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"yolov5","rtsp_enabled":false}')

printf '%s\n' "${first}" | jq '.serving_runtime'
printf '%s\n' "${second}" | jq '.serving_runtime'
printf '%s\n' "${source_json}" | jq '.active_session.serving_runtime'

curl -s http://127.0.0.1:18180/api/status | jq '.serving_runtimes'
```

Expected observations on the current host:

- the first direct session reports one `serving_runtime` with
  `consumer_count = 1`
- the second direct session reports the same `runtime_key` with
  `consumer_count = 2`, `shared = true`, and `rtsp_enabled = true`
- the URI-backed app source creates one app-owned logical session whose nested
  `active_session.serving_runtime` reports the same shared runtime with
  `consumer_count = 3`
- `GET /api/status` reports `total_serving_runtimes = 1` and one runtime entry
  with `owner_session_id`, `consumer_session_ids`, resolved source metadata,
  and additive RTSP intent for the shared path

## App Route Source Smoke Test

Create one idle app and inspect the durable record:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"vision-runner"}' | jq

curl -s http://127.0.0.1:18180/api/apps | jq
```

Declare exact and grouped-capable routes:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"yolov5","expect":{"media":"video"}}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/color","expect":{"media":"video"}}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}' | jq

curl -s http://127.0.0.1:18180/api/apps/1/routes | jq
```

Create one exact app-source bind:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"yolov5"}' | jq
```

Create one grouped app-source bind from the live Orbbec preset:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30","target":"orbbec"}' | jq
```

Exercise source lifecycle:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources/1/stop \
  -H 'Content-Type: application/json' -d '{}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources/1/start \
  -H 'Content-Type: application/json' -d '{}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources/1/rebind \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/1080p_30"}' | jq
```

Session-backed attach and delete-conflict verification:

```bash
curl -s -X POST http://127.0.0.1:18180/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30"}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources \
  -H 'Content-Type: application/json' \
  -d '{"session_id":2,"target":"yolov5"}' | jq

curl -i -X DELETE http://127.0.0.1:18180/api/sessions/2
```

Host-validation checks on both direct-session and app-source paths:

```bash
curl -s -X POST http://127.0.0.1:18180/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://not-local/web-camera/720p_30"}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://not-local/web-camera/720p_30","target":"yolov5"}' | jq
```

Restart normalization for apps and sources:

```bash
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18180 \
  --db-path /tmp/insight-io.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18180/api/apps | jq
curl -s http://127.0.0.1:18180/api/apps/1/sources | jq
```

Expected behavior:

- apps remain present after restart
- durable source rows remain present after restart
- `AppService::initialize()` normalizes persisted source runtime state to
  `stopped`

## Grouped Route Delete Cleanup Smoke Test

Status: `resolved`

The grouped member-route delete path is now verified on the development host.

Run this after binding one grouped Orbbec preset:

```bash
curl -s -X POST http://127.0.0.1:18180/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"rgbd-review"}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/color","expect":{"media":"video"}}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}' | jq

curl -s -X POST http://127.0.0.1:18180/api/apps/1/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30","target":"orbbec"}' | jq

curl -i -X DELETE http://127.0.0.1:18180/api/apps/1/routes/orbbec%2Fdepth
curl -s http://127.0.0.1:18180/api/apps/1/sources | jq
curl -i http://127.0.0.1:18180/api/sessions/1

sqlite3 /tmp/insight-io.sqlite3 \
  "SELECT source_id, target_name, state, resolved_routes_json FROM app_sources;"

sqlite3 /tmp/insight-io.sqlite3 \
  "SELECT session_id, state, resolved_members_json FROM sessions;"
```

Expected result:

- `DELETE /api/apps/1/routes/orbbec%2Fdepth` returns `204 No Content`
- `GET /api/apps/1/sources` returns an empty `sources` array for that app
- the grouped app-owned session returns `404 Not Found`
- SQLite no longer retains the grouped bind row or the app-owned grouped
  session row for that binding
