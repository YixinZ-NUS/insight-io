# Full-Stack Intent Routing Task List

## Ordered Tasks

1. Update the product contract docs so the repo defines route-based routing as
   the intended public story.
2. Add the PRD, architecture note, data-model note, diagram, and feature
   tracker.
3. Introduce SQL migrations and make them the canonical DB schema source.
4. Persist apps, routes, and sources in SQLite.
5. Add route CRUD and `route`-based source connection to the REST API.
6. Add single-stream URI validation, grouped-source resolution, and semantic
   route expectation validation in the backend.
7. Refactor the high-level SDK to route-scoped callback chains plus explicit
   startup source connection.
8. Update examples and SDK tests to use route-based routing with one URI per
   stream.
9. Scaffold the frontend app/route/source management flows.
10. Run focused verification, update feature pass states, and record completed
    work in `docs/past-tasks.md`.

## Completion Rule

The work is complete only when:

- docs and code agree on the same app/route/source contract
- schema is explicit in SQL migrations
- backend persists app intent
- each listed URI resolves to one stream at the app layer
- SDK routes by declared route
- frontend can express the same app/route/source flow
- feature tracker entries are updated from `false` only after verification
