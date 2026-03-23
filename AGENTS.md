# insight-io Agent Notes

Before proposing or implementing changes, read these documents in order:

1. `docs/prd/fullstack-intent-routing-prd.md`
2. `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md`
3. `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`
4. `docs/tasks/fullstack-intent-routing-task-list.md`
5. `docs/features/fullstack-intent-routing-e2e.json`
6. `docs/past-tasks.md`

## Working Rules

- Treat this repo as the standalone home of the DB-first target-routing
  project, not as a patch layer on the older `insightos` tree.
- Keep the backend a CMake C++20 project under `backend/`.
- Keep the backend as an on-demand user-service-style process model.
- Keep the canonical source address shape and session graph described in the
  PRD and architecture docs unless those docs are updated first.
- Use `docs/features/fullstack-intent-routing-e2e.json` as the implementation
  scoreboard:
  - new end-to-end work must add or update feature entries
  - leave `passes` as `false` until verification has actually run
  - when a feature is implemented, record the exact verification path in
    `docs/past-tasks.md` before flipping `passes` to `true`
- Add durable architecture diagrams under `docs/diagram/`.
