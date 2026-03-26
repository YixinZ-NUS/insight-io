# insight-io Docs Hub

## Role

- role: central entry for the active `insight-io` design set
- status: active
- version: 18
- major changes:
  - 2026-03-26 reviewed the three post-task-5 follow-ups, confirmed SQLite
    `FULLMUTEX` and Orbbec pipeline-profile fallback in code, and recorded pure
    D2C capability gating as remaining TODO while refreshing the donor-reuse
    status
  - 2026-03-26 closed the grouped-member-route delete cleanup gap, added a
    grouped-route-delete sequence diagram, and refreshed the next-slice handoff
    toward reuse, IPC, RTSP runtime, SDK callbacks, and frontend work
  - 2026-03-26 synced the docs set to the checked-in direct-session slice,
    added a direct-session sequence diagram, and moved the handoff to app,
    route, and source persistence
  - 2026-03-26 took back redundant `app_sources.target_kind` and
    `app_sources.source_kind`, made durable bind kind inferred from the row
    shape itself, made app-source uniqueness explicit in the canonical SQL,
    and scoped exact-route binds to one app-local route owner
  - 2026-03-26 aligned the active docs with the reviewed selector contract:
    plain V4L2 selectors such as `720p_30`, retained `orbbec/...` namespacing
    for grouped RGBD families, and removed redundant stored `selector_key`
  - 2026-03-26 reintroduced the persisted discovery catalog, alias control, and
    runtime-verified exact/grouped source listing for the connected hardware
  - 2026-03-25 reintroduced the first buildable backend slice, added a user
    guide, and added a tech report plus bootstrap sequence diagram for the
    implementation phase
  - 2026-03-25 removed stale variant/group identity fields from the active
    contract, made catalog RTSP publication metadata queryable, and defined
    referenced-session delete as `409 Conflict`
  - 2026-03-25 added a writeup that recommends a runtime-only post-capture
    publication phase for codec and protocol-specific publication work
  - 2026-03-25 added an RTSP publication reuse writeup explaining why the
    active contract no longer promises separate IPC versus RTSP delivery
    sessions
  - 2026-03-25 clarified that direct sessions remain standalone until bound and
    that multi-device apps declare app-local logical input routes
  - 2026-03-25 replaced public `route` / `route_grouped` bind inputs with one
    app-local `target` surface and reserved grouped target roots
  - 2026-03-25 removed `/channel/...` from the active URI grammar and kept
    grouped target resolution server-side
  - 2026-03-25 reframed RTSP as optional publication intent, not as a peer to
    implicit local IPC attach
  - 2026-03-24 made `delivery_name` inferred from source locality and scheme
    rather than client-posted, while keeping it durable in storage
  - 2026-03-24 made public `uri` values derived rather than durable DB keys
  - 2026-03-24 made delivery durable bind/session intent and unified
    session-first binds under the app-source surface
  - 2026-03-24 clarified local SDK attach stays IPC-only while future LAN RTSP
    consumption remains planned
  - 2026-03-24 added a centralized reading order
  - 2026-03-24 standardized grouped bind naming to `route_grouped`
  - 2026-03-24 simplified the durable schema to catalog, app intent, session,
    and log tables
- past tasks:
  - `2026-03-26 – Review Post-Task-5 Follow-Ups And Refresh Donor Reuse Status`
  - `2026-03-26 – Close Grouped Route Delete Cleanup And Refresh Runtime Handoff`
  - `2026-03-26 – Review App Route Source Persistence Slice And Reproduce Grouped Route Delete Bug`
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`
  - `2026-03-26 – Apply Selector Review And Device-Scoped Stream Keying`
  - `2026-03-26 – Take Back Redundant App-Source Kind Columns`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`
  - `2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics`
  - `2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase`
  - `2026-03-25 – Document RTSP Publication Reuse After Delivery-Name Removal`
  - `2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`
  - `2026-03-24 – Separate Catalog Publication From Runtime Ownership And Rename Route APIs`
  - `2026-03-24 – Simplify The Durable Data Model And Add A Docs Hub`

## Read Order

1. [Full-Stack Intent Routing PRD](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
2. [Intent Routing Architecture](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
3. [Intent Routing Data Model](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
4. [REST API Reference](/home/yixin/Coding/insight-io/docs/REST.md)
5. [Full-Stack Intent Routing Task List](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
6. [Feature Tracker: End To End](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
7. [Feature Tracker: Runtime And User Journeys](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
8. [Past Tasks](/home/yixin/Coding/insight-io/docs/past-tasks.md)
9. [User Guide](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
10. [Tech Report](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)

## Current Contract

- one derived `uri` selects one fixed catalog-published source shape
- `uri` is derived from stable catalog identity plus the current device public
  name; it is not a durable DB key
- app-source requests use one app-local `target` field; the backend resolves
  whether that target is one exact route or one grouped target root
- route names stay app-local and should model logical input roles such as
  `front-camera`, `rear-camera`, `orbbec/color`, and `orbbec/depth`
- grouped target roots are reserved:
  an app must not declare both one exact route `x` and any route below `x/`
- exact-member URIs still mean one delivered stream
- grouped preset URIs may mean one fixed related stream bundle
- the current worktree now serves health, device catalog, alias, direct
  session lifecycle, app/route/source lifecycle, and runtime-status endpoints
- the current worktree rejects `insightos://` inputs whose host does not match
  the configured local catalog host on both direct-session and app-source
  creation paths
- deleting one grouped member route now removes any grouped durable bind that
  resolved through that route and deletes the associated app-owned grouped
  session so stale resolved-member metadata is not retained
- discovery publishes selectable choices; sessions and workers realize them
  later
- RTSP is optional durable publication intent on `app_sources` and `sessions`
- `streams.publications_json` may expose a queryable `rtsp.url` for the same
  source shape; that URL should keep the same `/<device>/<selector>` path as
  the derived `insightos://` URI while replacing `localhost` with the
  configured RTSP host
- same `uri` plus the same publication requirements may share one serving path;
  RTSP publication may be additive on shared runtime when lifecycle rules allow
- runtime still needs a post-capture publication phase for output profile,
  codec, and protocol-description work, but that phase stays runtime-only in v1
- local SDK attach always uses IPC, but that is implicit and not a posted
  field
- `DELETE /api/sessions/{id}` must return `409 Conflict` while any app source
  still references that session
- direct sessions are standalone session-first runtime intent; declaring a
  matching route does not consume them until a later app-source bind exists
- future remote or LAN RTSP consumption remains planned, but it is not part of
  the v1 SDK attach contract
- the durable schema should stay minimal:
  - `devices`
  - `streams`
  - `apps`
  - `app_routes`
  - `app_sources`
  - `sessions`
  - `session_logs`
- `streams` is also the single per-device preset table for exact-member and
  grouped preset choices
- the schema is greenfield: no migration-history table or backward-compat
  schema layer is required in v1
- lower-level capture, publication, and worker reuse graphs stay runtime-only and
  are surfaced through status and logs rather than their own durable tables
- single-stream V4L2 selectors stay compact, for example `720p_30` or
  `1080p_30`, because media kind and device identity already disambiguate them
- grouped-device selectors may stay namespaced, for example
  `orbbec/depth/480p_30`, so exact members and grouped presets share the same
  family vocabulary as grouped app targets such as `orbbec`
- `streams` uniqueness should be enforced at the ownership boundary with
  `UNIQUE(device_id, selector)` rather than by storing a concatenated
  `selector_key`
- `app_sources` should not store extra `target_kind` or `source_kind` columns;
  exact versus grouped bind kind is inferred from `route_id`, and URI-backed
  versus session-backed bind kind is inferred from `source_session_id`
- exact app-route binds should be owned by the same app row they serve, using
  one composite route reference `(app_id, route_id) -> app_routes(app_id,
  route_id)` so deleting a route cascades only the exact-route bindings that
  use it

## Doc Map

| File | Role | Status |
|------|------|--------|
| `docs/prd/fullstack-intent-routing-prd.md` | product contract and user flows | active |
| `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md` | control-plane, runtime, and responsibility split | active |
| `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md` | minimal durable schema, PK/FK plan, and runtime boundary | active |
| `docs/design_doc/POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md` | why capture and publication planning stay separate at runtime | active |
| `docs/design_doc/RTSP_PUBLICATION_REUSE_WRITEUP.md` | why the active contract treats RTSP as additive publication state | active |
| `docs/REST.md` | public HTTP contract | active |
| `docs/tasks/fullstack-intent-routing-task-list.md` | next implementation round | active |
| `docs/features/fullstack-intent-routing-e2e.json` | narrow implementation scoreboard | active |
| `docs/features/runtime-and-app-user-journeys.json` | broader lifecycle coverage tracker | active |
| `docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md` | grouped-source rationale and Orbbec evidence | active |
| `docs/features/INTERACTION_CONTEXT.md` | donor-grounded interaction framing | active |
| `docs/USER_GUIDE.md` | build, test, and runtime steps for the checked-in slice | active |
| `docs/design_doc/TECH_REPORT.md` | internal implementation notes and Mermaid inventory | active |
| `docs/diagram/intent-routing-er.md` | Mermaid ER diagram for the simplified durable schema | active |
| `docs/diagram/intent-routing-runtime.md` | runtime/control-plane diagram | active |
| `docs/diagram/bootstrap-health-sequence.md` | sequence diagram for backend bootstrap and health | active |
| `docs/diagram/catalog-discovery-sequence.md` | sequence diagram for discovery refresh and alias-backed catalog reads | active |
| `docs/diagram/direct-session-sequence.md` | sequence diagram for direct-session create, restart, and delete flow | active |
| `docs/diagram/app-route-source-sequence.md` | sequence diagram for app create, route declaration, bind, stop/start, and rebind flow | active |
| `docs/diagram/grouped-route-delete-sequence.md` | sequence diagram for grouped bind cleanup when one member route is deleted | active |
| `docs/past-tasks.md` | change log and verification index | active |

## Status Rules

- active docs describe the current contract
- solved investigations should be marked `resolved` and moved behind the active
  entry points when they stop being daily references
- stale or superseded docs should be archived instead of silently drifting
