# Tech Report

## Role

- role: internal implementation report for the standalone `insight-io` rebuild
- status: active
- version: 8
- major changes:
  - 2026-03-26 reran the checked-in direct-session slice on the development
    host, recorded green focused tests plus live direct-session smoke, added a
    direct-session sequence diagram, and moved the handoff to app persistence
  - 2026-03-26 took back redundant `app_sources.target_kind` and
    `app_sources.source_kind`, made session and app-source `stream_id`
    references required, added the missing canonical app-source uniqueness
    indexes, and tightened exact-route ownership to the same app
  - 2026-03-26 resolved the selector/schema review by removing stored
    `selector_key`, adopting compact V4L2 selectors, retaining `orbbec/...`
    namespacing for grouped RGBD selectors, and documenting the next slice
  - 2026-03-26 added a review snapshot of the current slice, a donor-reuse
    status writeup, a schema-keying recommendation for `streams`, and a
    Mermaid backlog for the next runtime slices
  - 2026-03-26 added the persisted discovery catalog slice, including the
    probe-grounded Orbbec depth and grouped preset publication path
  - 2026-03-25 added the first implementation-phase report and Mermaid diagram
    inventory for the bootstrap backend slice
- past tasks:
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`
  - `2026-03-26 – Apply Selector Review And Device-Scoped Stream Keying`
  - `2026-03-26 – Take Back Redundant App-Source Kind Columns`
  - `2026-03-26 – Review Current Scaffold, Discovery Reuse, And Schema Keying`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Current Slice

The current implementation slice is intentionally narrow and infrastructure
first:

- explicit seven-table SQLite schema checked into the repository
- one standalone backend binary, `insightiod`
- persisted catalog reads and alias control for `devices` and `streams`
- direct-session persistence, lifecycle, and status endpoints
- focused tests that prove schema bootstrap, catalog shaping, REST lifecycle
  paths, and direct-session behavior

In concrete HTTP terms, the current backend serves only:

- `GET /api/health`
- `GET /api/devices`
- `GET /api/devices/{device}`
- `POST /api/devices/{device}/alias`
- `POST /api/sessions`
- `GET /api/sessions`
- `GET /api/sessions/{id}`
- `POST /api/sessions/{id}/start`
- `POST /api/sessions/{id}/stop`
- `DELETE /api/sessions/{id}`
- `GET /api/status`

The app, route, grouped bind, reuse, IPC-attach, and frontend portions of the
PRD remain future work.

## Review Snapshot

The current implementation was rechecked against both the code and a live run
on the development host.

Observed from the current audit:

- build succeeds with Orbbec and PipeWire enabled
- `ctest` is green across:
  - `schema_store_test`
  - `catalog_service_test`
  - `session_service_test`
  - `rest_server_test`
- live `insightiod` smoke on this host returned four devices through
  `GET /api/devices`:
  - one V4L2 webcam
  - one Orbbec RGBD camera
  - two PipeWire audio devices
- the live direct-session smoke flow succeeded for
  `insightos://localhost/web-camera/720p_30`:
  - `POST /api/sessions`
  - `GET /api/sessions`
  - `GET /api/sessions/{id}`
  - `GET /api/status`
  - `POST /api/sessions/{id}/stop`
  - backend restart with the same SQLite file
  - `POST /api/sessions/{id}/start`
  - `DELETE /api/sessions/{id}` after the session was unreferenced

Important scope boundary:

- discovery/catalog publication is runtime-verified on the development host
- direct-session REST and persistence are runtime-verified on the development
  host for the checked-in slice
- app, route, and source persistence are not implemented
- session reuse, attach, grouped bind resolution, IPC delivery, RTSP runtime,
  SDK callbacks, and frontend flows are not implemented in this repository yet

This matches the current feature trackers:

- `docs/features/fullstack-intent-routing-e2e.json` now records the verified
  direct-session REST lifecycle slice as passing
- `docs/features/runtime-and-app-user-journeys.json` now marks direct-session
  create and persisted restart as passing
- app/app-source lifecycle, grouped runtime reuse, IPC attach, and frontend
  flows remain `false`

## Donor Reuse Status

### Already Reused

- discovery code shape and implementation strategy were directly ported from
  `../insightos` for:
  - V4L2 discovery
  - PipeWire discovery
  - Orbbec SDK discovery
- the Orbbec SDK linkage itself is reused by building against the donor SDK
  tree under `../insightos/third_party/orbbec_sdk/SDK`
- the `cpp-httplib` integration pattern is also reused

Evidence:

- current build linkage:
  `backend/CMakeLists.txt`
- current discovery files:
  `backend/src/discovery/*.cpp`
- donor counterparts:
  `../insightos/backend/src/discovery/*.cpp`

Concrete comparison points:

- current V4L2 enumeration loop in
  [v4l2_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/v4l2_discovery.cpp)
  matches the donor structure in
  [v4l2_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/v4l2_discovery.cpp)
- current Orbbec enumeration and vendor-skip plumbing in
  [orbbec_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/orbbec_discovery.cpp)
  and [discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/discovery.cpp)
  are direct ports of the donor flow in
  [orbbec_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/orbbec_discovery.cpp)
  and [discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/discovery.cpp)
- current PipeWire enumeration in
  [pipewire_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/pipewire_discovery.cpp)
  closely follows the donor implementation in
  [pipewire_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/pipewire_discovery.cpp)

### Partially Reused

- shared type helpers are ported and trimmed, especially stable device-key
  generation and capability naming
- Orbbec grouped-preset shaping is not copied verbatim from donor code, but it
  is informed by donor probe evidence and the donor runtime contract
- donor duplicate-suppression behavior for Orbbec vendor IDs is reused, but it
  is not yet adapted to the new repo's runtime expectations:
  when Orbbec support is compiled in, the current code suppresses matching V4L2
  vendor IDs before it knows whether SDK discovery actually yielded a usable
  Orbbec catalog device

### Not Yet Reused But Highly Relevant

- local IPC transport:
  donor `memfd` + ring-buffer + eventfd path is present in
  `../insightos/backend/include/insightos/backend/ipc.hpp` and
  `../insightos/backend/src/ipc/ipc.cpp`, but no equivalent code exists yet in
  `insight-io`
- worker/runtime graph:
  donor `session_manager.cpp` plus the V4L2, PipeWire, and Orbbec worker files
  remain reference material only
- request normalization:
  donor `SessionRequest`, URI parsing, and source normalization logic are only
  lightly reintroduced for the current direct-session slice; the broader
  donor request-routing layer remains unported

### Not Suitable To Copy As-Is

- donor device/session store schema and REST/session behavior should not be
  copied directly because they encode the older preset-and-delivery contract
  rather than the new `insight-io` app-target contract
- donor runtime tables and delivery-session persistence are intentionally
  outside the v1 schema for this repository
- donor aggregate discovery behavior should also not be copied forward blindly:
  the inherited Orbbec vendor-skip rule is acceptable for a donor runtime that
  prefers SDK authority, but in `insight-io` it needs a fallback rule that does
  not hide the device when SDK discovery is unavailable or incomplete

## Schema Review

### Current `streams` Keying

The reviewed implementation now stores:

- `selector TEXT NOT NULL`
- `UNIQUE(device_id, selector)`

This keeps selector identity attached to the owning device row instead of
storing a duplicated concatenated identifier.

### Recommendation

The recommendation from the previous review is now the active design:

- keep `selector` as the stable per-device terminal identifier
- enforce `UNIQUE(device_id, selector)` via the parent relation
- derive any compound resource name at the API boundary instead of storing a
  concatenated `selector_key`

If a separate immutable identifier is genuinely needed later, add one opaque
`uid` field rather than duplicating the parent key into a second stored string.

Rationale:

- it removes redundant storage and redundant update logic
- it makes the uniqueness rule explicit at the actual ownership boundary:
  selector uniqueness is per device, not global in the abstract
- it follows the Google API design preference for one canonical resource name
  plus an optional separate immutable `uid`, rather than several overlapping
  identifiers for the same resource

Relevant references:

- Google AIP-122 Resource Names: https://google.aip.dev/122
- Google AIP-148 Standard Fields, especially `uid`: https://google.aip.dev/148

Applied to this project, the clean model is:

- public copied handle: derived `insightos://<host>/<device>/<selector>`
- durable relational uniqueness: `(device_id, selector)`
- optional future opaque id: `stream_uid`

Not recommended:

- treating `selector_key` as if it were a first-class immutable resource id
  when it is currently just `device_key` plus `selector`

### `app_sources` Kind Columns

The next schema takeback is the same pattern in a different table.

The previous schema carried:

- `target_kind`
- `source_kind`

Those fields duplicated information already present in the row:

- exact versus grouped bind kind is derivable from whether `route_id` is
  populated
- URI-backed versus session-backed bind kind is derivable from whether
  `source_session_id` is populated

The active recommendation is now also the active canonical schema:

- remove stored `target_kind`
- remove stored `source_kind`
- keep `target_name` as the only posted target field
- keep `stream_id` required because every durable bind still chooses one exact
  stream row or one grouped preset row
- keep `source_session_id` optional to mark session-backed binds
- keep `active_session_id` optional to record the currently serving runtime

This removes duplicated state without losing any product meaning.

### Missing Canonical Indexes

Another debt in the earlier SQL was that the docs described app-source
uniqueness, but the canonical schema did not actually define the indexes.

The active SQL should keep:

- unique index on `(app_id, target_name)`
- partial unique index on `(app_id, route_id)` where `route_id IS NOT NULL`

Without those indexes, the contract that one target owns at most one active
durable binding is only aspirational.

## Live Findings

### Remaining Schema Debt

The selector cleanup itself is complete in the canonical SQL:

- [001_initial.sql](/home/yixin/Coding/insight-io/backend/schema/001_initial.sql)
  stores `selector` and enforces `UNIQUE(device_id, selector)`
- it does not store `selector_key`
- it makes both `sessions.stream_id` and `app_sources.stream_id` required
- it does not store `app_sources.target_kind` or `app_sources.source_kind`

The remaining concrete schema debt is outside the SQL and lives in checked-in
test and runtime code:

- [session_service.cpp](/home/yixin/Coding/insight-io/backend/src/session_service.cpp)
  still reads sessions through `LEFT JOIN streams` with `COALESCE(...)`
  defaults, which preserves code paths that behave as if `sessions.stream_id`
  could be absent even though the schema now forbids that row shape
- [session_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/session_service_test.cpp)
  now uses the canonical app-route and app-source row shape for the
  delete-conflict case, so the focused test suite is green again

### Discovery Reuse Audit

The rerun now shows three separate states:

- donor discovery backends are clearly reused in structure and code
- catalog shaping for focused tests is functioning
- the real host-discovery path is now working on the development host for the
  webcam, Orbbec, and PipeWire devices, though the Orbbec fallback rule still
  needs a cleaner contract

That means reuse status should be read as:

- discovery/device enumeration:
  substantially reused and now proven on the development host for the current
  connected devices
- Orbbec SDK integration:
  partially reused but live device publication is now proven in `insight-io`
- IPC mechanism:
  not reused yet, but highly relevant
- worker/runtime design:
  not reused yet, but highly relevant as reference material only

### App-Local Route Ownership

One more schema gap remained after removing the redundant kind columns.

The earlier SQL let `app_sources.route_id` point at `app_routes.route_id`
globally, which means one app-source row could reference a route that belongs
to some other app entirely.

The active SQL should instead keep:

- `UNIQUE(app_id, route_id)` on `app_routes`
- `FOREIGN KEY (app_id, route_id) REFERENCES app_routes(app_id, route_id)
  ON DELETE CASCADE` on `app_sources`

That change does two useful things:

- exact-route binds become durably app-local rather than depending on runtime
  discipline
- deleting one declared route cascades only the exact-route app-source rows
  that actually target that route, while grouped rows remain `route_id = NULL`

### Selector Naming Review

The review comment about selector names split into two different cases.

Resolved decision:

- plain V4L2 webcam selectors should be compact terminal identifiers such as
  `720p_30` and `1080p_30`
- grouped Orbbec selectors should keep the `orbbec/...` namespace

Rationale for the V4L2 change:

- `media_kind` already carries `video`, so `video-720p_30` duplicated meaning
- the URI path stays shorter and easier to copy without losing information
- the selector still remains stable within one device catalog

Rationale for keeping `orbbec/...`:

- the same device publishes both exact members and grouped presets, so the
  namespace marks one selector family rather than one media kind
- grouped target vocabulary already uses `orbbec`, `orbbec/color`, and
  `orbbec/depth`, so keeping the selector namespace aligned reduces ambiguity
- dropping the namespace would make grouped RGBD selectors read like generic
  leaf names even though their meaning is tied to the grouped family contract

In short:

- remove redundant prefixes when they only restate media kind
- keep namespaces when they express the grouped-device family contract

## Next Step

The next implementation slice is durable app intent on top of the now-verified
catalog and direct-session base:

- add `POST /api/apps`, `GET /api/apps`, and `GET /api/apps/{id}`
- add app-route persistence and route validation
- add app-source persistence for URI-backed and session-backed binds
- keep grouped-target resolution, reuse, IPC attach, and RTSP runtime work in
  later slices after the durable app/app-source contract is checked in

## Mermaid Diagram Inventory

- [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  documents the durable schema defined by the active data model
- [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  documents the intended control-plane and runtime boundary for the full system
- [bootstrap-health-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/bootstrap-health-sequence.md)
  documents the currently implemented backend bootstrap and health-check path
- [catalog-discovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/catalog-discovery-sequence.md)
  documents discovery refresh, persistence, catalog reads, and alias updates
- [direct-session-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/direct-session-sequence.md)
  documents create, restart, status, and delete flow for one direct session

## Recommended Mermaid Backlog

To keep the remaining implementation explainable, the next diagrams worth
adding are:

- grouped target bind sequence:
  app-source bind using one grouped preset URI and one grouped `target`
- existing-session attach sequence:
  attach `session_id` to one app-local target without recreating capture
- shared-runtime reuse sequence:
  two consumers on the same URI with additive RTSP publication
- restart recovery sequence:
  persisted sessions/apps/sources on backend restart
- route rebind sequence:
  switch one app route from one source/session to another without deleting the
  app record
- runtime worker graph:
  capture worker, publication phase, IPC publisher, RTSP publisher, and reuse
  ownership boundaries
- discovery versus runtime responsibility sequence:
  prove where catalog shaping ends and runtime realization begins
- Orbbec fallback and duplicate-suppression sequence:
  show when SDK discovery wins, when generic V4L2 fallback is allowed, and when
  duplicate hiding applies

## Implementation Notes

- the checked-in schema already uses the v1 durable table inventory from the
  active docs instead of reviving donor-only runtime tables
- the bootstrap server deliberately keeps the runtime surface small so later
  feature slices can add discovery and session logic without first undoing a
  mismatched baseline
- the connected Orbbec device currently exposes incomplete raw SDK discovery in
  this environment, so the catalog synthesizes `orbbec/depth/400p_30`,
  `orbbec/depth/480p_30`, and `orbbec/preset/480p_30` for serial
  `AY27552002M` from the documented 2026-03-23 probe evidence rather than
  regressing the public contract
- the donor `cpp-httplib` integration pattern was reused, but the runtime
  contract remains grounded in the `insight-io` docs rather than donor REST
  behavior
- the donor discovery code is already reused substantially, but the donor IPC,
  request-normalization, and worker/session-manager layers are still pending
  integration work rather than checked-in reuse
