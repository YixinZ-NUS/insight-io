# Post-Capture Publication Phase Writeup

## Role

- role: explain why `insight-io` needs a runtime-only publication phase after
  capture to manage output profiles, codec handling, and protocol-specific
  negotiation
- status: active
- version: 1
- major changes:
  - 2026-03-25 documented that capture planning and publication planning should
    stay separate, with publication kept runtime-only in v1
- past tasks:
  - `2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase`

## Summary

`insight-io` should have a distinct post-capture phase after worker capture.

That phase should be runtime-only, not a new durable schema layer in v1.

Recommended responsibility split:

- capture workers own source-side realization:
  device open, chosen caps, D2C or grouped-worker policy, and frame production
- publication phase owns consumer-facing realization:
  passthrough versus repacketize versus transcode, publication profile
  selection, protocol-specific description, and publication fanout

Preferred name:

- `publication phase` or `publication plan`

Avoid:

- exposing a new durable `delivery_sessions` table in `insight-io` v1
- mixing capture choice and output promise back into the public URI contract
- using `delivery` as the main public term when the real concerns are
  publication state, output profile, and transport-specific description

## Why It Is Needed

The current docs already separate source selection from publication intent.

That makes a runtime publication phase necessary because:

- one selected `uri` fixes the capture-side source shape, but does not fully
  define the final consumer-facing media promise
- the same capture may feed multiple consumers with different publication
  requirements
- RTSP publication has protocol-specific description concerns that do not belong
  inside the capture worker
- future codec choices such as H.264, H.265, MJPEG, or raw should not force new
  hardware capture workers when one capture output can be transformed or
  republished

The donor material points in the same direction:

- the donor standalone plan explicitly calls out that capture caps and promised
  delivery format were mixed together in confusing ways, and that "same preset,
  different delivery promises in parallel" was not modeled well enough in the
  old surface in
  [standalone-project-plan.md](/home/yixin/Coding/insightos/docs/plan/standalone-project-plan.md#L31)
- the donor runtime rationale keeps capture policy separate from promise-only
  fields such as `rtsp` versus `mjpeg` in
  [DB_RUNTIME_GRAPH_RATIONALE.md](/home/yixin/Coding/insightos/docs/design_doc/DB_RUNTIME_GRAPH_RATIONALE.md#L100)
- the donor workflow notes that actual delivered format may differ per stream
  and must be inspected after resolution in
  [WORKFLOW_OVERVIEW.md](/home/yixin/Coding/insightos/docs/WORKFLOW_OVERVIEW.md#L471)

## What The Publication Phase Should Own

The publication phase should decide, per selected source plus consumer or
publication requirement:

- whether capture output can be passed through directly
- whether one lightweight repacketization step is needed
- whether a codec transform is needed
- what concrete publication format is actually served
- what protocol metadata must be exposed to the consumer

Examples:

- local IPC app attach:
  no real negotiation, but the app still needs actual `Caps` and frame format
- RTSP publication:
  the server needs one concrete media description, codec configuration, and URL
- future remote or LAN consumers:
  publication phase may need profile-specific path, codec, and endpoint rules

## Negotiation Boundary

The publication phase should own protocol-specific negotiation or advertisement,
not the capture worker.

For the current scope:

- local IPC is mostly not negotiated:
  the backend exposes actual caps and the SDK adapts
- RTSP publication is where protocol-level media description belongs
- codec-specific parameters such as H.264/H.265 profile-level choices, payload
  mode, or similar publication details belong here if `insight-io` exposes them
  later

This is the clean place for RTSP/SDP concerns because RTSP publication is a
consumer-facing promise, not a hardware-capture choice.

## V1 Recommendation

For v1:

- keep `rtsp_enabled` as the only durable public publication flag
- keep the new publication phase runtime-only
- do not add capture tables or delivery tables back into the durable schema
- do not add general client-posted codec negotiation yet
- expose actual publication facts through runtime/status and session/source
  responses

Useful runtime-only fields later:

- `publication_profile`
- `transport`
- `promised_format`
- `actual_format`
- `rtsp_url`
- `ipc_descriptor`

## Future Extension Path

If v2 needs explicit codec choice, add it as publication intent separate from
source identity and capture policy.

Preferred shape:

- one optional `publication_profile` or `output_profile`
- still separate from `uri`
- still separate from capture policy
- still able to fan out one capture into multiple publication plans

That preserves the current product rule:

- capture answers "what source shape did the user pick?"
- publication answers "how is that source actually exposed to this consumer?"
