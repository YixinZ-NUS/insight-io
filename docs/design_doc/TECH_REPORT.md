# Tech Report

## Role

- role: internal implementation report for the standalone `insight-io` rebuild
- status: active
- version: 4
- major changes:
  - 2026-03-26 resolved the selector/schema review by removing stored
    `selector_key`, adopting compact V4L2 selectors, retaining `orbbec/...`
    namespacing for grouped RGBD selectors, and documenting the next slice
  - 2026-03-26 added a review snapshot of the current scaffold, a donor-reuse
    status writeup, a schema-keying recommendation for `streams`, and a
    Mermaid backlog for the next runtime slices
  - 2026-03-26 added the persisted discovery catalog slice, including the
    probe-grounded Orbbec depth and grouped preset publication path
  - 2026-03-25 added the first implementation-phase report and Mermaid diagram
    inventory for the bootstrap backend slice
- past tasks:
  - `2026-03-26 – Review Current Scaffold, Discovery Reuse, And Schema Keying`
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Current Slice

The current implementation slice is intentionally narrow and infrastructure
first:

- explicit seven-table SQLite schema checked into the repository
- one standalone backend binary, `insightiod`
- persisted catalog reads and alias control for `devices` and `streams`
- focused tests that prove schema bootstrap, catalog shaping, and server startup

In concrete HTTP terms, the current backend serves only:

- `GET /api/health`
- `GET /api/devices`
- `GET /api/devices/{device}`
- `POST /api/devices/{device}/alias`

Everything else in the PRD remains future work.

## Review Snapshot

The current implementation was rechecked against both the code and a live run
on the development host.

Observed from the live review run:

- build succeeds with Orbbec and PipeWire enabled
- focused tests pass
- the catalog currently persisted four devices on this machine:
  - one Orbbec device
  - one V4L2 webcam
  - two PipeWire audio sources
- the Orbbec device published the expected review-critical selectors:
  - `orbbec/depth/400p_30`
  - `orbbec/depth/480p_30`
  - `orbbec/preset/480p_30`

Important scope boundary:

- discovery/catalog publication is real and runtime-tested
- direct sessions are not implemented
- app, route, and source persistence are not implemented
- session reuse, attach, grouped bind resolution, IPC delivery, RTSP runtime,
  SDK callbacks, and frontend flows are not implemented in this repository yet

This matches the current feature trackers:

- `docs/features/fullstack-intent-routing-e2e.json` currently has 11 passing
  entries and 15 remaining `false`
- the passing entries are docs and catalog/discovery slice checks
- the remaining `false` entries are app/session/runtime lifecycle work

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

### Partially Reused

- shared type helpers are ported and trimmed, especially stable device-key
  generation and capability naming
- Orbbec grouped-preset shaping is not copied verbatim from donor code, but it
  is informed by donor probe evidence and the donor runtime contract

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
  donor `SessionRequest`, URI parsing, and source normalization logic remain
  unported, which is why direct-session and app-source flows are still absent

### Not Suitable To Copy As-Is

- donor device/session store schema and REST/session behavior should not be
  copied directly because they encode the older preset-and-delivery contract
  rather than the new `insight-io` app-target contract
- donor runtime tables and delivery-session persistence are intentionally
  outside the v1 schema for this repository

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

The next implementation slice remains direct sessions:

- `POST /api/sessions`
- `GET /api/sessions`
- `GET /api/sessions/{id}`
- `DELETE /api/sessions/{id}` with the documented `409 Conflict` guard
- runtime-backed verification that one selected catalog URI normalizes into one
  persisted direct logical session

## Mermaid Diagram Inventory

- [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  documents the durable schema defined by the active data model
- [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  documents the intended control-plane and runtime boundary for the full system
- [bootstrap-health-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/bootstrap-health-sequence.md)
  documents the currently implemented backend bootstrap and health-check path
- [catalog-discovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/catalog-discovery-sequence.md)
  documents discovery refresh, persistence, catalog reads, and alias updates

## Recommended Mermaid Backlog

To keep the remaining implementation explainable, the next diagrams worth
adding are:

- direct session creation sequence:
  `POST /api/sessions` from URI normalization through runtime/session creation
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

## Implementation Notes

- the checked-in schema already uses the v1 durable table inventory from the
  active docs instead of reviving donor-only runtime tables
- the bootstrap server deliberately keeps the runtime surface small so later
  feature slices can add discovery and session logic without first undoing a
  mismatched scaffold
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
