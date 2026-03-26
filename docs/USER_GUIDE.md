# User Guide

## Role

- role: operator and developer guide for the checked-in `insight-io` runtime
- status: active
- version: 4
- major changes:
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

Catalog discovery, session creation, app routing, frontend management, and SDK
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
- `build/bin/rest_server_test`

## Test

```bash
ctest --test-dir build --output-on-failure
```

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
  "status": "ok",
  "version": "0.1.0"
}
```

## Device Catalog

```bash
curl -s http://127.0.0.1:18180/api/devices | jq
```

On the current development machine, the catalog should show:

- one V4L2 webcam with selectors such as `720p_30`
- one Orbbec device with selectors including `orbbec/depth/400p_30`,
  `orbbec/depth/480p_30`, and `orbbec/preset/480p_30`
- PipeWire audio sources when PipeWire discovery is enabled

## Device Alias

```bash
curl -s -X POST http://127.0.0.1:18180/api/devices/web-camera/alias \
  -H 'Content-Type: application/json' \
  -d '{"public_name":"front-camera"}' | jq
```

After aliasing, both the derived `insightos://` URI and
`publications_json.rtsp.url` should use `front-camera` in the device path.

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
- REST health, catalog, and alias surfaces

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

On the current development machine, the review run observed four catalog
devices:

- one Orbbec device published as `sv1301s-u3`
- one V4L2 webcam published as `web-camera`
- two PipeWire audio sources

The observed Orbbec catalog included:

- `orbbec/depth/400p_30`
- `orbbec/depth/480p_30`
- `orbbec/preset/480p_30`

The observed V4L2 webcam catalog included selectors such as:

- `720p_30`
- `1080p_30`
- `2160p_30`

### SQLite Review

```bash
sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT device_id, device_key, public_name, driver, status \
   FROM devices ORDER BY public_name;"

sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT stream_id, device_id, selector, media_kind, shape_kind \
   FROM streams ORDER BY device_id, selector;"
```

This confirms that the current runtime is persisting only the `devices` and
`streams` portions of the seven-table schema.

### Duplicate Orbbec Suppression

When Orbbec SDK discovery is enabled, the backend first asks the Orbbec path
for its vendor IDs and then skips matching V4L2 USB nodes during generic V4L2
enumeration. This is the current guard against the known issue where the
Orbbec camera can otherwise look like a plain V4L2 camera in Linux device
lists.

If the Orbbec SDK is unavailable at build or run time, that suppression path is
not active, so the hardware may reappear only through the generic V4L2 route.
