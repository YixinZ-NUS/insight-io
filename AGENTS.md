# insight-io Agent Notes

Before proposing or implementing changes, read these documents in order:

1. `docs/README.md`
2. `docs/prd/fullstack-intent-routing-prd.md`
3. `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md`
4. `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`
5. `docs/tasks/fullstack-intent-routing-task-list.md`
6. `docs/features/fullstack-intent-routing-e2e.json`
7. `docs/past-tasks.md`

## Working Rules

- Treat this repo as the standalone home of the DB-first target-routing
  project, not as a patch layer on the older `insightos` tree.
- Treat the current repository as docs-only unless the docs are explicitly
  updated to reintroduce implementation.
- Keep the canonical source address shape and session graph described in the
  PRD and architecture docs unless those docs are updated first.
- Prefer the simpler durable schema documented in
  `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`:
  - keep `devices`, `streams`, `apps`, `app_routes`, `app_sources`,
    `sessions`, and `session_logs`
  - treat `streams` as the single per-device preset table
  - do not add migration-history or backward-compat schema layers in v1
  - do not add lower-level runtime tables for capture, delivery, or worker
    runs unless a concrete persistence need has been proven
- Keep the contract that one canonical URI selects one fixed catalog-published
  source shape:
  - exact-member URIs still resolve to one delivered stream
  - grouped preset URIs may resolve to one fixed related stream bundle
- Keep discovery responsible for exposing exact member choices and any grouped
  preset choices, including separate depth entries when backend processing
  changes delivered caps.
- Keep discovery and runtime responsibilities separate:
  - discovery and the catalog publish selectable source shapes and metadata
  - logical, capture, delivery, and worker sessions own runtime realization,
    reuse, and lifecycle
- Use `route_grouped` and `connect_grouped(...)` for grouped route binding
  terminology; avoid reintroducing `route_namespace` or `connect_namespace`.
- Use `docs/features/fullstack-intent-routing-e2e.json` as the implementation
  scoreboard and `docs/features/runtime-and-app-user-journeys.json` as the
  broader lifecycle tracker:
  - new end-to-end work must add or update feature entries
  - leave `passes` as `false` until verification has actually run
  - when a feature is implemented, record the exact verification path in
    `docs/past-tasks.md` before flipping `passes` to `true`
- When you touch a doc or implementation file, keep a short header comment or
  front-matter block that states the file's role, version or revision marker,
  and major changes, with pointers to the relevant `docs/past-tasks.md`
  entries.
- After any major design or implementation change, sweep the related docs in
  the same change:
  - update investigation or problem-statement docs with their current status,
    such as `open`, `intermittent`, `resolved`, or `archived`
  - periodically archive stale or superseded docs so solved material does not
    read like current guidance
- Add durable architecture diagrams under `docs/diagram/`.
