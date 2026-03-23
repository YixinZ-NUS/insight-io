# Full-Stack Intent Routing Task List

## Ordered Tasks

1. Update the product contract docs so the repo defines target-based routing as
   the intended public story.
2. Add the PRD, architecture note, data-model note, diagram, and feature
   tracker.
3. Introduce SQL migrations and make them the canonical DB schema source.
4. Persist apps, targets, and sources in SQLite.
5. Add target CRUD and `target`-based source injection to the REST API.
6. Add target-kind validation and role-binding computation in the backend.
7. Refactor the high-level SDK to `App::target(...)` plus
   `App::add_source(input, target)`.
8. Update examples and SDK tests to use target routing instead of stream-name
   routing.
9. Scaffold the frontend app/target/source management flows.
10. Run focused verification, update feature pass states, and record completed
    work in `docs/past-tasks.md`.

## Completion Rule

The work is complete only when:

- docs and code agree on the same app/target/source contract
- schema is explicit in SQL migrations
- backend persists app intent
- SDK routes by target
- frontend can express the same app/target/source flow
- feature tracker entries are updated from `false` only after verification
