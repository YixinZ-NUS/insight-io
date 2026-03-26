# insight-io

## Role

- role: repository entry point for the standalone `insight-io` rebuild
- status: active
- version: 4
- major changes:
  - 2026-03-26 reintroduced persisted device discovery, exact source-shape
    catalog listing, device alias updates, and queryable RTSP publication
    metadata backed by the connected webcam, Orbbec device, and PipeWire nodes
  - 2026-03-25 reintroduced the first buildable backend slice with an explicit
    SQL schema, a versioned health endpoint, focused tests, and runtime
    verification
  - 2026-03-25 added a user guide and a tech report entry point for the new
    implementation phase
- past tasks:
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

`insight-io` is the standalone home of the DB-first route-based rebuild that
was previously being prototyped in `insightos`.

The current checked-in implementation is intentionally narrow:

- one buildable backend executable: `insightiod`
- one checked-in v1 schema covering `devices`, `streams`, `apps`,
  `app_routes`, `app_sources`, `sessions`, and `session_logs`
- one versioned `GET /api/health` endpoint
- persisted `GET /api/devices` and `GET /api/devices/{device}` catalog
  surfaces with derived `insightos://` URIs and `publications_json.rtsp.url`
- `POST /api/devices/{device}/alias` for stable public device naming
- focused schema, catalog, and REST tests

Discovery, direct sessions, durable app routing, reuse, grouped preset
resolution, SDK, and frontend flows remain to be reintroduced in later
feature-sized slices.

## Start Here

- design baseline: [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
- operator and developer steps: [docs/USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
- internal implementation notes: [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)

## Grounding Order

Before proposing or extending implementation, read these files in order:

1. `AGENTS.md`
2. `docs/README.md`
3. `docs/prd/fullstack-intent-routing-prd.md`
4. `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md`
5. `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`
6. `docs/tasks/fullstack-intent-routing-task-list.md`
7. `docs/features/fullstack-intent-routing-e2e.json`
8. `docs/past-tasks.md`

## Current Contract

The active product contract remains doc-led:

- one derived `insightos://<host>/<device>/<selector>` URI selects one fixed
  catalog-published source shape
- discovery publishes exact-member URIs and may also publish grouped preset
  URIs when the member bundle is fixed and proven
- apps declare app-local logical routes and bind sources through one public
  `target` surface
- RTSP is optional publication intent, not part of source identity
- the durable schema stays limited to:
  - `devices`
  - `streams`
  - `apps`
  - `app_routes`
  - `app_sources`
  - `sessions`
  - `session_logs`

## Status

The repository is now buildable for the bootstrap slice, but it is not yet a
feature-complete implementation of the full intent-routing contract.
