# insight-io Docs Hub

## Role

- role: central entry for the active `insight-io` design set
- status: active
- version: 1
- major changes:
  - 2026-03-24 added a centralized reading order
  - 2026-03-24 standardized grouped bind naming to `route_grouped`
  - 2026-03-24 simplified the durable schema to catalog, app intent, session,
    and log tables
- past tasks:
  - `2026-03-24 – Separate Catalog Publication From Runtime Ownership And Rename Route APIs`
  - `2026-03-24 – Simplify The Durable Data Model And Add A Docs Hub`

## Read Order

1. [Full-Stack Intent Routing PRD](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
2. [Intent Routing Architecture](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
3. [Intent Routing Data Model](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
4. [REST API Reference](/home/yixin/Coding/insight-io/docs/REST.md)
5. [Full-Stack Intent Routing Task List](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
6. [Feature Tracker: End To End](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
7. [Feature Tracker: Runtime And User Journeys](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
8. [Past Tasks](/home/yixin/Coding/insight-io/docs/past-tasks.md)

## Current Contract

- one canonical URI selects one fixed catalog-published source shape
- exact-member URIs still mean one delivered stream
- grouped preset URIs may mean one fixed related stream bundle
- discovery publishes selectable choices; sessions and workers realize them
  later
- grouped app binds use `route_grouped` in REST and
  `connect_grouped(...)` in the SDK direction
- the durable schema should stay minimal:
  - `devices`
  - `streams`
  - `apps`
  - `app_routes`
  - `app_sources`
  - `sessions`
  - `session_logs`
- `streams` is also the single per-device preset table for exact-member and
  grouped preset choices
- the schema is greenfield: no migration-history table or backward-compat
  schema layer is required in v1
- lower-level capture, delivery, and worker reuse graphs stay runtime-only and
  are surfaced through status and logs rather than their own durable tables

## Doc Map

| File | Role | Status |
|------|------|--------|
| `docs/prd/fullstack-intent-routing-prd.md` | product contract and user flows | active |
| `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md` | control-plane, runtime, and responsibility split | active |
| `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md` | minimal durable schema, PK/FK plan, and runtime boundary | active |
| `docs/REST.md` | public HTTP contract | active |
| `docs/tasks/fullstack-intent-routing-task-list.md` | next implementation round | active |
| `docs/features/fullstack-intent-routing-e2e.json` | narrow implementation scoreboard | active |
| `docs/features/runtime-and-app-user-journeys.json` | broader lifecycle coverage tracker | active |
| `docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md` | grouped-source rationale and Orbbec evidence | active |
| `docs/features/INTERACTION_CONTEXT.md` | donor-grounded interaction framing | active |
| `docs/diagram/intent-routing-runtime.md` | runtime/control-plane diagram | active |
| `docs/past-tasks.md` | change log and verification index | active |

## Status Rules

- active docs describe the current contract
- solved investigations should be marked `resolved` and moved behind the active
  entry points when they stop being daily references
- stale or superseded docs should be archived instead of silently drifting
