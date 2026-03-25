# User Guide

## Role

- role: operator and developer guide for the checked-in `insight-io` runtime
- status: active
- version: 1
- major changes:
  - 2026-03-25 added initial build, test, and backend startup instructions for
    the bootstrap slice
- past tasks:
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Scope

This guide covers the currently implemented slice only:

- configure and build the backend
- run the focused tests
- start `insightiod`
- query `GET /api/health`

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
  --frontend /tmp/frontend
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
  "db_path": "/tmp/insight-io.sqlite3",
  "frontend_path": "/tmp/frontend",
  "status": "ok",
  "version": "0.1.0"
}
```
