# Tech Report

## Role

- role: internal implementation report for the standalone `insight-io` rebuild
- status: active
- version: 1
- major changes:
  - 2026-03-25 added the first implementation-phase report and Mermaid diagram
    inventory for the bootstrap backend slice
- past tasks:
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

## Current Slice

The current implementation slice is intentionally narrow and infrastructure
first:

- explicit seven-table SQLite schema checked into the repository
- one standalone backend binary, `insightiod`
- one runtime-tested `GET /api/health` surface
- focused tests that prove schema bootstrap and server startup

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

## Implementation Notes

- the checked-in schema already uses the v1 durable table inventory from the
  active docs instead of reviving donor-only runtime tables
- the bootstrap server deliberately keeps the runtime surface small so later
  feature slices can add discovery and session logic without first undoing a
  mismatched scaffold
- the donor `cpp-httplib` integration pattern was reused, but the runtime
  contract remains grounded in the `insight-io` docs rather than donor REST
  behavior
