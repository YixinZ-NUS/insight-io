# Full-Stack Intent Routing Task List

This repository is intentionally docs-only right now. The ordered tasks below
describe the next implementation round, not work that is currently checked in.

## Ordered Tasks

1. Keep the product docs authoritative for the exact-stream route-based
   contract.
2. Reintroduce discovery and catalog code that lists one exact URI per
   delivered stream, including separate `depth-400p_30` and `depth-480p_30`
   choices when D2C changes delivered caps.
3. Reintroduce SQL migrations and make them the canonical DB schema source.
4. Reintroduce direct session APIs and runtime status inspection using the same
   exact URI contract as the app layer.
5. Reintroduce persistent apps, routes, and sources in SQLite, including
   reverse-order attach from `session_id`.
6. Add route validation, grouped-source metadata persistence, identical-URI
   reuse, and different-delivery shared-capture behavior in the backend.
7. Add runtime rebind so a route can change bindings without destroying the app
   record.
8. Refactor the high-level SDK to route-scoped callback chains plus explicit
   startup source connection and session attach.
9. Update examples and tests to use exact-stream URIs and cover the full
   lifecycle.
10. Reintroduce the frontend app/route/source management flows.
11. Run focused verification, update feature pass states, and record completed
    work in `docs/past-tasks.md`.

## Completion Rule

The work is complete only when:

- docs and code agree on the same app/route/source contract
- schema is explicit in SQL migrations
- backend persists app intent
- discovery lists one exact URI per delivered stream
- SDK routes by declared route
- direct-session-first and app-first flows are both supported
- same exact URI can be reused safely across multiple consumers
- same capture can back multiple delivery formats such as `/mjpeg` and `/rtsp`
- frontend can express the same app/route/source flow
- feature tracker entries are updated from `false` only after verification
