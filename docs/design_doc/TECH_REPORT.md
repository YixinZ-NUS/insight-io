# Tech Report

## Role

- role: internal implementation report for the standalone `insight-io` rebuild
- status: active
- version: 2
- major changes:
  - 2026-03-26 added the persisted discovery catalog slice, including the
    probe-grounded Orbbec depth and grouped preset publication path
  - 2026-03-25 added the first implementation-phase report and Mermaid diagram
    inventory for the bootstrap backend slice
- past tasks:
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Current Slice

The current implementation slice is intentionally narrow and infrastructure
first:

- explicit seven-table SQLite schema checked into the repository
- one standalone backend binary, `insightiod`
- one runtime-tested `GET /api/health` surface
- persisted catalog reads and alias control for `devices` and `streams`
- focused tests that prove schema bootstrap, catalog shaping, and server startup

This keeps the code aligned with the documented contract while leaving room to
reintroduce discovery, direct sessions, durable app routing, grouped preset
resolution, and SDK/frontend work in later slices.

## Mermaid Diagram Inventory

- [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  documents the durable schema defined by the active data model
- [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  documents the intended control-plane and runtime boundary for the full system
- [bootstrap-health-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/bootstrap-health-sequence.md)
  documents the currently implemented backend bootstrap and health-check path
- [catalog-discovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/catalog-discovery-sequence.md)
  documents discovery refresh, persistence, catalog reads, and alias updates

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
