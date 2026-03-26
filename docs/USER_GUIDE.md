# User Guide

## Role

- role: operator and developer guide for the checked-in `insight-io` runtime
- status: active
- version: 6
- major changes:
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
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`
  - `2026-03-26 – Review Current Scaffold, Discovery Reuse, And Schema Keying`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Scope

This guide covers the currently implemented slice only:

- configure and build the backend
- run the focused tests
- start `insightiod`
- query `GET /api/health`
- inspect `GET /api/devices`
- update `POST /api/devices/{device}/alias`
- create, inspect, start, stop, and delete direct sessions through the REST API
- inspect `/api/status`

App routing, persistent app/source management, frontend management, and SDK
callbacks are not yet available in this checked-in slice.

## Build

```bash
cmake -S . -B build
cmake --build build -j4
```

Expected binaries:

- `build/bin/insightiod`
- `build/bin/schema_store_test`
- `build/bin/catalog_service_test`
- `build/bin/session_service_test`
- `build/bin/rest_server_test`

## Test

```bash
ctest --test-dir build --output-on-failure
```

Current checked-in result on the development host:

- `schema_store_test`: pass
- `catalog_service_test`: pass
- `session_service_test`: pass
- `rest_server_test`: pass

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

The current checked-in test scope is intentionally narrow:

- schema bootstrap
- persisted catalog shaping
- direct-session lifecycle and status verification
- REST health, catalog, alias, and direct-session surfaces

Current audit result:

- `ctest` is green
- focused coverage now includes schema bootstrap, catalog shaping, direct
  session lifecycle, restart normalization, delete-conflict handling, and REST
  surface checks

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
session smoke flow, `sessions` and `session_logs` also gain rows as expected.

### Duplicate Orbbec Suppression

When Orbbec SDK discovery is enabled, the backend first asks the Orbbec path
for its vendor IDs and then skips matching V4L2 USB nodes during generic V4L2
enumeration. This is the current guard against the known issue where the
Orbbec camera can otherwise look like a plain V4L2 camera in Linux device
lists.

If the Orbbec SDK is unavailable at build or run time, that suppression path is
not active, so the hardware may reappear only through the generic V4L2 route.

Current implementation caveat:

- the skip list is keyed off the compiled-in Orbbec vendor set before the code
  knows whether SDK discovery actually returned a usable Orbbec device
- in practice, that means a future fallback rule is still needed so generic
  V4L2 visibility is not hidden when SDK-backed discovery is absent or
  incomplete

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
REST and persistence slice on this host. Media-worker reuse, app/app-source
flows, grouped attach, and active RTSP publication runtime are still future
work.
