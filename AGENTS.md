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
- Treat the current repository as docs-only unless the docs are explicitly
  updated to reintroduce implementation.
- Keep the canonical source address shape and session graph described in the
  PRD and architecture docs unless those docs are updated first.
- Keep the contract that one canonical URI maps to one delivered stream.
- Keep discovery responsible for exposing exact stream choices, including
  separate depth entries when backend processing changes delivered caps.
- Use `docs/features/fullstack-intent-routing-e2e.json` as the implementation
  scoreboard and `docs/features/runtime-and-app-user-journeys.json` as the
  broader lifecycle tracker:
  - new end-to-end work must add or update feature entries
  - leave `passes` as `false` until verification has actually run
  - when a feature is implemented, record the exact verification path in
    `docs/past-tasks.md` before flipping `passes` to `true`
- Add durable architecture diagrams under `docs/diagram/`.
