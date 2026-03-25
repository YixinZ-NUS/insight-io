# Full-Stack Intent Routing Task List

## Role

- role: ordered implementation backlog for the active intent-routing contract
- status: active
- version: 7
- major changes:
  - 2026-03-25 reintroduced the first buildable backend slice with the explicit
    v1 schema, health endpoint, and focused tests
  - 2026-03-25 added queryable RTSP publication metadata expectations and fixed
    referenced-session delete behavior
  - 2026-03-25 added a runtime-only post-capture publication-planning task for
    codec/profile handling after capture
  - 2026-03-25 replaced public grouped/exact bind selection with one
    app-local `target` surface
  - 2026-03-25 reframed RTSP as optional publication intent rather than a peer
    to implicit local IPC attach
  - 2026-03-25 removed `/channel/...` from the active URI grammar
- past tasks:
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`
  - `2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics`
  - `2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

The bootstrap implementation has restarted. The current checked-in code covers
the explicit v1 schema, a versioned health endpoint, and focused build/runtime
tests. The ordered tasks below describe the remaining feature work.

## Ordered Tasks

1. Keep the product docs authoritative for the source-shape route-based
   contract.
2. Reintroduce discovery and catalog code backed by `devices` and `streams`
   tables that list exact-member URIs plus any fixed grouped preset URIs and
   their optional publication metadata, including queryable
   `publications_json.rtsp.url` values that mirror the source selector path on
   the configured RTSP host, plus separate
   `orbbec/depth/400p_30`, `orbbec/depth/480p_30`, and the proven grouped
   preset `orbbec/preset/480p_30`.
3. Check in one SQL schema for `devices`, `streams`, `apps`,
   `app_routes`, `app_sources`, `sessions`, and `session_logs`.
4. Reintroduce direct session APIs and runtime status inspection using the same
   exact URI contract as the app layer plus durable RTSP publication intent and
   `409` delete protection while a session is still referenced by app sources.
5. Reintroduce persistent apps, routes, and sources in SQLite, including
   reverse-order exact and grouped attach from `session_id`.
6. Add route validation, grouped-source metadata persistence, grouped target
   resolution behind one public `target` field, session-backed binds under the
   same app-source surface, identical-URI reuse, and additive RTSP publication
   behavior in the backend.
7. Add a runtime-only post-capture publication phase that manages publication
   profile selection, passthrough versus transcode decisions, protocol-specific
   publication description, and publication fanout without adding new durable
   runtime tables.
8. Add runtime rebind so a route can change bindings without destroying the app
   record.
9. Refactor the high-level SDK to named-route declarations with callbacks plus
   explicit startup source binding, exact and grouped session attach through
   the same `bind_source(...)` surface, and IPC-only local attach.
10. Update examples and tests to use exact-member URIs, grouped preset URIs,
   explicit RTSP publication intent, and cover the full lifecycle.
11. Reintroduce the frontend app/route/source management flows.
12. Run focused verification, update feature pass states, and record completed
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
- RTSP publication intent is persisted separately from the selected URI on
  app-source and session records
- catalog publication metadata exposes a predictable RTSP URL for each source
  shape, even though actual reachability still depends on active publication
  state
- SDK routes by declared route
- SDK can bind one grouped preset URI through the same public `target` surface
  without extra SDK-only frame-merge helpers
- SDK can attach one grouped preset session through the same grouped target
  surface without forcing per-member bind management
- direct-session-first and app-first flows are both supported
- same exact URI can be reused safely across multiple consumers
- same capture can back local IPC attach and optional RTSP publication
- runtime includes a post-capture publication phase for output profile and
  codec/publication handling without reintroducing durable delivery tables
- deleting a referenced session returns `409 Conflict` instead of silently
  detaching dependent app sources
- local SDK attach remains IPC-only in v1 while future remote or LAN RTSP
  consumption remains a separate path
- frontend can express the same app/route/source flow
- feature tracker entries are updated from `false` only after verification
