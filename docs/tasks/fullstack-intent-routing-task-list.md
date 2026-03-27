# Full-Stack Intent Routing Task List

## Role

- role: ordered implementation backlog for the active intent-routing contract
- status: active
- version: 17
- major changes:
  - 2026-03-27 added the checked-in PipeWire audio example app, live-verified
    mono and stereo exact-selector behavior on the current host, and refreshed
    the example-app docs to keep the task closeout coherent
  - 2026-03-27 simplified the checked-in example startup path so the example
    apps can now start either with a startup URI or idle for later REST
    injection, confirmed omitted app names derive from the executable name,
    and closed the remaining Mermaid backlog
  - 2026-03-27 closed task 9 with the checked-in route-oriented SDK, grouped
    target fan-out, exact and grouped `session_id` attach, runtime rebind, and
    example apps; closed task 10 with the repo-native browser route-builder UI;
    closed tasks 11 and 12 with focused SDK/browser tests, live hardware
    runtime verification, and a full feature-tracker sweep
  - 2026-03-27 closed task 7 with live-verified IPC attach, idle-worker
    teardown, and repo-native unix-socket control flow, closed the first task
    8 slice with exact single-channel RTSP publication plus strict FFmpeg
    validation, and moved the next handoff to SDK callback delivery
  - 2026-03-26 closed task 6 by adding in-memory serving-runtime reuse across
    direct sessions and app-owned sources, exposing serving-runtime topology in
    `GET /api/status`, and moving the next handoff to donor IPC delivery
  - 2026-03-26 closed task 5 by fixing grouped-member-route delete cleanup and
    refreshed the remaining handoff toward session reuse, IPC delivery, RTSP
    runtime, SDK callbacks, and frontend flows
  - 2026-03-26 marked the direct-session REST and status slice complete in the
    checked-in backend and moved the next handoff to durable apps, routes, and
    sources
  - 2026-03-26 documented the reviewed selector/schema cleanup before the
    direct-session implementation landed
  - 2026-03-26 reintroduced persisted discovery, alias control, grouped Orbbec
    catalog shaping, and queryable RTSP publication metadata
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
  - `2026-03-27 – Add PipeWire Audio Example And Verify Mono/Stereo Selectors`
  - `2026-03-27 – Simplify Example Startup Paths And Close Mermaid Backlog`
  - `2026-03-27 – Complete Task-7 IPC Hardening And Task-8 Exact RTSP Publication`
  - `2026-03-26 – Add Serving Runtime Reuse And Runtime-Status Topology`
  - `2026-03-26 – Close Grouped Route Delete Cleanup And Refresh Runtime Handoff`
  - `2026-03-26 – Review App Route Source Persistence Slice And Reproduce Grouped Route Delete Bug`
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`
  - `2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics`
  - `2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

The current worktree covers the full documented v1 surface: the explicit
schema, persisted discovery and catalog reads, device alias updates, direct
session REST/status flow, durable app/route/source CRUD and lifecycle
endpoints, shared-runtime reuse, local IPC attach, exact RTSP publication, the
route-oriented SDK, the repo-native browser UI, example apps including
PipeWire audio consumption, focused automated tests, and live hardware
verification on the development host.

The example apps now explicitly cover both startup modes:

- startup with one CLI-posted URI or `session:<id>`
- idle startup with later `POST /api/apps/{id}/sources` injection

Completed slices:

- tasks 1 through 12 are now implemented and verification-backed in the
  current worktree

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
   reverse-order exact and grouped attach from `session_id`, and close the
   grouped-member-route delete cleanup gap so grouped app sources cannot retain
   stale member resolution after one member route is removed.
6. Add serving-session reuse in the backend so identical `stream_id` plus
   compatible publication intent can share one serving path across direct
   sessions and app-owned sources, and surface that reuse clearly in
   `GET /api/status`.
7. Port the donor IPC delivery path from `../insightos` into `insight-io`
   runtime form, reusing the `memfd` + ring-buffer + `eventfd` transport and
   control-server patterns while keeping attach lifecycle keyed by the new
   session and app-source contract rather than donor delivery tables.
8. Add RTSP runtime and the runtime-only post-capture publication phase so one
   serving runtime can add RTSP publication, choose output profile/codec
   handling, and describe publication state without adding durable runtime
   tables.
9. Refactor the high-level SDK to named-route declarations with real callback
   delivery, explicit startup binding, exact and grouped `session_id` attach,
   and grouped target fan-out over the same REST-backed control plane.
10. Reintroduce the frontend app/route/source flows, including catalog browse,
   route declaration, grouped-target binding, source lifecycle, reuse/status
   inspection, and restart recovery.
11. Expand examples, smoke tests, and runtime verification to cover session
   reuse, IPC delivery, grouped preset callback delivery, additive RTSP
   publication, and browser-driven flows on the development hardware.
12. Flip feature pass states only after the corresponding tests and live
    verification have run, record the exact verification path in
    `docs/past-tasks.md`, and keep the user guide plus Mermaid diagrams in sync
    with the active implementation slice.

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
- deleting one grouped member route must not leave one grouped app-source row
  or grouped app-owned session behind with stale resolved-member metadata
- local SDK attach remains IPC-only in v1 while future remote or LAN RTSP
  consumption remains a separate path
- frontend can express the same app/route/source flow
- feature tracker entries are updated from `false` only after verification
