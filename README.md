# insight-io

`insight-io` is the docs-only design home for the DB-first route-based rebuild
that was previously being prototyped in `insightos`.

This repository intentionally keeps only the product and architecture contract:

- PRD, architecture, data model, REST contract, and runtime diagrams
- interaction and end-to-end feature trackers
- past-task notes that record design decisions and future verification paths

The obsolete implementation scaffold has been removed on purpose. Future code
work should restart only after reading and accepting the current doc set.

## Grounding Order

Before proposing or reintroducing implementation, read these files in order:

1. `AGENTS.md`
2. `docs/prd/fullstack-intent-routing-prd.md`
3. `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md`
4. `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`
5. `docs/tasks/fullstack-intent-routing-task-list.md`
6. `docs/features/fullstack-intent-routing-e2e.json`
7. `docs/past-tasks.md`

## Current Scope

The current design contract is built around these choices:

- discovery publishes exact canonical URIs where one URI maps to one delivered
  stream
- a main product objective is to mask heterogeneous hardware details, such as
  D2C on/off behavior, from users and from LLM-based app builders so audio and
  video apps stay easy to develop and reuse
- route declarations stay purpose-first and validate compatibility, but do not
  choose hidden stream variants
- RGBD depth modes whose delivered caps differ are split at discovery time
  into separate user-visible choices such as `depth-400p_30` and
  `depth-480p_30`
- stereo or dual-eye channel disambiguation may use an optional
  `/channel/<name>` path segment, but discovery should normally emit the full
  final URI so users rarely type it by hand
- direct sessions, app-routed sessions, reuse, rebind, and restart are all part
  of the intended lifecycle

## Status

This repo is not currently buildable. The checked-in state is the design
baseline for the next implementation round.
