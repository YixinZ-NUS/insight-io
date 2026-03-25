# RTSP Publication Reuse Writeup

## Role

- role: explain why the active contract no longer promises separate IPC versus
  RTSP delivery sessions for the same selected source
- status: active
- version: 1
- major changes:
  - 2026-03-25 documented that the old inferred-`delivery_name` split is no
    longer the public reuse contract after the RTSP publication redesign
- past tasks:
  - `2026-03-25 – Document RTSP Publication Reuse After Delivery-Name Removal`

## Summary

The older donor-grounded rule was:

- same `uri` plus same inferred `delivery_name` should reuse one delivery
  session when possible
- same `uri` plus different inferred `delivery_name`, such as `ipc` versus
  `rtsp`, should split delivery sessions while still being eligible for shared
  capture reuse

That is not the current `insight-io` contract anymore.

The active contract now says:

- local SDK attach is implicit IPC behavior, not one client-posted or
  durable-user-facing transport choice
- RTSP is optional publication state on a selected bind or session through
  `rtsp_enabled`
- the reuse key at the contract level is one selected `uri` plus publication
  requirements
- RTSP publication may be additive on top of one shared serving runtime when
  lifecycle rules allow it

So the old "different `delivery_name` implies different delivery session" rule
is no longer the public promise.

## Why The Old Rule Changed

The donor stack still exposes the older split internally.

Examples:

- the donor session manager still maps `delivery_name = rtsp` to a different
  transport kind than local attach, with `rtsp` versus `ipc` branching in
  [session_manager.cpp](/home/yixin/Coding/insightos/backend/src/session_manager.cpp#L201)
- the donor delivery-session table is still keyed by
  `capture_session_id + stream_key + delivery_name + transport` in
  [device_store.cpp](/home/yixin/Coding/insightos/backend/src/device_store.cpp#L338)

That older model was useful when the public contract still treated delivery as
an explicit request dimension.

The active `insight-io` docs intentionally moved away from that because:

- `ipc` is not a meaningful user-facing choice for local SDK app attach in v1
- `rtsp` is better modeled as optional publication state than as a peer source
  identity or posted transport field
- the public contract should not force one particular runtime row split when one
  shared serving runtime can satisfy both local IPC consumers and RTSP
  publication

## Active Contract

The current rule is stated in:

- [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md#L305)
- [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md#L260)
- [REST.md](/home/yixin/Coding/insight-io/docs/REST.md#L229)

In plain terms:

- if two consumers select the same `uri` and ask for the same publication
  requirements, they should reuse runtime when possible and observe the same
  frame sequence
- if one consumer asks for RTSP publication and another does not, the backend
  may still share capture and serving runtime by enabling RTSP as an additive
  publication on that shared serving path
- the current contract no longer guarantees a separate IPC delivery session and
  a separate RTSP delivery session for the same source

## What Still Remains True

- capture reuse is still source-side and should not split just because RTSP is
  requested
- local apps still consume through IPC in v1
- future raw `rtsp://` ingest is still a separate import problem and does not
  automatically become a cataloged `insightos://` source

## Implementation Boundary

This writeup changes the public interpretation, not the freedom of the runtime
implementation.

The backend may still internally create distinct publisher objects, sinks, or
worker subcomponents for IPC and RTSP. What changed is the durable and REST/API
contract:

- callers ask for one source plus optional `rtsp_enabled`
- callers do not ask for `ipc` versus `rtsp` as peer delivery modes
- the runtime is free to satisfy that with one shared serving path plus RTSP
  publication, or with a more split internal topology, as long as the public
  behavior matches the active docs
