# insight-io

`insight-io` is the docs-only design home for the DB-first route-based rebuild
that was previously being prototyped in `insightos`.

This repository intentionally keeps only the product and architecture contract:

- PRD, architecture, data model, REST contract, and runtime diagrams
- interaction and end-to-end feature trackers
- past-task notes that record design decisions and future verification paths

The obsolete implementation scaffold has been removed on purpose. Future code
work should restart only after reading and accepting the current doc set.

## Start Here

The centralized entry point is [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md).

## Grounding Order

Before proposing or reintroducing implementation, read these files in order:

1. `AGENTS.md`
2. `docs/README.md`
3. `docs/prd/fullstack-intent-routing-prd.md`
4. `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md`
5. `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`
6. `docs/tasks/fullstack-intent-routing-task-list.md`
7. `docs/features/fullstack-intent-routing-e2e.json`
8. `docs/past-tasks.md`

## Current Scope

The current design contract is built around these choices:

- discovery publishes canonical URIs that resolve to one fixed published source
  shape:
  - exact-member URIs still mean one delivered stream
  - grouped preset URIs may mean one fixed related stream bundle
- a main product objective is to mask heterogeneous hardware details, such as
  D2C on/off behavior, from users and from LLM-based app builders so audio and
  video apps stay easy to develop and reuse
- route declarations stay purpose-first through `app.route(...).expect(...)`
  and validate compatibility instead of choosing hidden stream variants
- grouped route binds use `route_grouped` in REST and
  `app.connect_grouped(...)` in the SDK direction
- RGBD discovery may publish both exact member choices and grouped preset
  choices, for example:
  - `orbbec/depth/400p_30`
  - `orbbec/depth/480p_30`
  - `orbbec/preset/480p_30`
- the simplified durable schema is:
  - `devices`
  - `streams`
  - `apps`
  - `app_routes`
  - `app_sources`
  - `sessions`
  - `session_logs`
- `streams` is also the single per-device preset table for both exact-member
  and grouped preset choices
- the repo assumes a fresh implementation, so it does not require
  migration-history or backward-compat schema layers in v1
- stereo or dual-eye channel disambiguation may use an optional
  `/channel/<name>` path segment, but discovery should normally emit the full
  final URI so users rarely type it by hand
- direct sessions, app-routed sessions, reuse, rebind, and restart are all part
  of the intended lifecycle

## Status

This repo is not currently buildable. The checked-in state is the design
baseline for the next implementation round.
