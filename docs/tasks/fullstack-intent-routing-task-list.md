# Full-Stack Intent Routing Task List

This repository is intentionally docs-only right now. The ordered tasks below
describe the next implementation round, not work that is currently checked in.

## Ordered Tasks

1. Keep the product docs authoritative for the source-shape route-based
   contract.
2. Reintroduce discovery and catalog code backed by `devices` and `streams`
   tables that list exact-member URIs plus any fixed grouped preset URIs,
   including separate `orbbec/depth/400p_30`, `orbbec/depth/480p_30`, and the
   proven grouped preset `orbbec/preset/480p_30`.
3. Check in one canonical SQL schema for `devices`, `streams`, `apps`,
   `app_routes`, `app_sources`, `sessions`, and `session_logs`.
4. Reintroduce direct session APIs and runtime status inspection using the same
   exact URI contract as the app layer.
5. Reintroduce persistent apps, routes, and sources in SQLite, including
   reverse-order attach from `session_id`.
6. Add route validation, grouped-source metadata persistence, grouped preset
   `route_grouped` binds, identical-URI reuse, and different-delivery
   shared-capture behavior in the backend.
7. Add runtime rebind so a route can change bindings without destroying the app
   record.
8. Refactor the high-level SDK to named-route declarations with callbacks plus
   explicit startup source connection, grouped preset grouped-route
   connection, and session attach.
9. Update examples and tests to use exact-member URIs, grouped preset URIs,
   and cover the full lifecycle.
10. Reintroduce the frontend app/route/source management flows.
11. Run focused verification, update feature pass states, and record completed
    work in `docs/past-tasks.md`.

## Completion Rule

The work is complete only when:

- docs and code agree on the same app/route/source contract
- schema is explicit in checked-in SQL
- the durable schema stays limited to `devices`, `streams`, `apps`,
  `app_routes`, `app_sources`, `sessions`, and `session_logs`
- device-specific exact-member and grouped preset choices both live in
  `streams`
- backend persists app intent
- discovery lists exact-member URIs and grouped preset URIs when the grouped
  member set is fixed and proven
- SDK routes by declared route
- SDK can bind one grouped preset URI through `route_grouped` without
  extra SDK-only frame-merge helpers
- direct-session-first and app-first flows are both supported
- same exact URI can be reused safely across multiple consumers
- same capture can back multiple delivery formats such as `/mjpeg` and `/rtsp`
- frontend can express the same app/route/source flow
- feature tracker entries are updated from `false` only after verification
