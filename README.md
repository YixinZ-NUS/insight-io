# insight-io

`insight-io` is a fresh standalone project scaffold for the DB-first
intent-routing architecture that was being prototyped in `insightos`.

This repo is intentionally narrower than the original codebase:

- backend-first CMake C++20 project under `backend/`
- explicit docs for PRD, architecture, data model, tasks, and feature status
- no inherited git history from the original repo
- feature progress tracked in JSON and expected to drive implementation order

## Grounding Order

Before proposing or implementing changes, read these files in order:

1. `AGENTS.md`
2. `docs/prd/fullstack-intent-routing-prd.md`
3. `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md`
4. `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`
5. `docs/tasks/fullstack-intent-routing-task-list.md`
6. `docs/features/fullstack-intent-routing-e2e.json`
7. `docs/past-tasks.md`

## Current Scope

This repo currently includes:

- durable backend app, target, and source persistence in SQLite
- target-aware REST scaffolding for app target CRUD and source injection
- backend tests covering restart persistence and target validation
- a checked-in schema artifact under `backend/schema/`

Still pending:

- high-level SDK target API
- frontend builder flows
- remaining feature tracker items marked `passes: false`

## Build

```bash
cmake -S . -B build
cmake --build build -j4 --target \
  device_store_test \
  rest_server_test
```

The backend continues to use the same donor-grounded media/runtime design that
motivated the original prototype, but this repo is the new standalone home for
the target-routing project.
