# Grouped Source Selection Writeup

## Role

- role: grouped-source design rationale and real-device evidence
- status: active
- version: 2
- major changes:
  - 2026-03-24 updated grouped-source wording to use derived `uri` identity
    with separate durable `delivery_name`
  - 2026-03-24 aligned grouped preset binds and grouped-session attach under
    the same grouped target surface
- past tasks:
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

## Summary

This note records the resolved design direction for grouped sources in
`insight-io`.

The adopted choices are:

- keep route declarations purpose-first
- make discovery publish exact member choices up front and grouped preset
  choices when the bundled members are fixed and proven
- keep the contract that one URI selects one fixed
  catalog-published source shape
- treat route expectations as validation, not hidden stream selection
- move D2C-sensitive depth differences into discovery-visible choices
- keep channel disambiguation in the URI path rather than in query params when
  it is required
- keep delivery intent separate from URI identity so local IPC attach and
  future RTSP consumption do not distort source selection

One main objective is to mask heterogeneous hardware details from users,
including LLMs that use `insight-io` as a development substrate for reusable
audio/video apps.

## Final Decisions

### 1. Exact URI Beats Backend Guessing

The system no longer relies on route expectations to decide which concrete
variant the user meant.

Instead:

- discovery publishes the exact derived URI
- discovery publishes the selected source shape and metadata
- the user copies or selects that URI
- the backend validates the URI against the route
- sessions and workers realize that published choice later
- the backend does not silently swap one nearby variant for another

This closes the ambiguity that existed when route expectations and backend
policy were trying to choose the stream together.

### 2. RGBD D2C Is Resolved At Discovery Time

For the targeted 480p family:

- D2C off yields a `400p` depth output
- D2C on yields a `480p` depth output
- the grouped 480p flow can also deliver color plus aligned depth together
  through one stable preset request

That means the user-visible choice should be split there, while still allowing
one grouped preset choice when discovery can prove the full bundled behavior.

Discovery must expose separate exact member entries such as:

- `insightos://localhost/desk-rgbd/orbbec/depth/400p_30`
- `insightos://localhost/desk-rgbd/orbbec/depth/480p_30`

Discovery may also expose one fixed grouped preset entry:

- `insightos://localhost/desk-rgbd/orbbec/preset/480p_30`

This lets the user choose the delivered output directly, or pick the proven
bundled color + aligned-depth flow directly.

Implications:

- `Depth{}` validates that the selected exact member URI is a depth stream
- the route layer no longer needs `require_alignment()`
- the resolved metadata still records the capture policy that produced that
  stream or grouped preset member set

Real-device result:

- a probe run on 2026-03-23 against device `AY27552002M` found no native
  `640x480` or `1280x720` depth profile; the native depth families were
  `640x400` and `1280x800`
- forcing software or hardware D2C on a depth-only `640x400@30` request still
  produced `640x480` `y16` depth frames
- those depth-only `480p` D2C cases delivered zero color frames, even though
  aligned `640x480` depth was produced
- color+depth D2C for `640x480` also produced `640x480` depth, using a native
  `640x400` depth profile under the hood
- no D2C depth profiles were returned for color `1280x720@30` in either
  software or hardware D2C mode
- forcing D2C on depth-only `1280x800@30` still delivered `1280x800` depth
  frames, so no distinct aligned `800p` output was observed on this device
- the sibling `insightos` live RGBD proximity-capture flow on 2026-03-24 also
  proved that one grouped `480p_30` request can deliver color plus aligned
  depth together in a stable way that is directly useful to applications

Raw output was recorded in:

- the probe branch `orbbec-depth-480p-probe`, in files named
  `2026-03-23-orbbec-depth-probe.txt` and
  `2026-03-23-orbbec-depth-probe-extended.txt`

### 2A. Redesign After Real Orbbec Probe

The probe changes the design in an important way:

- `orbbec/depth/480p_30` should remain a valid public exact-member choice
- the backend must not implement that URI by naively looking for a native
  `640x480` depth sensor profile
- instead, the backend should treat `orbbec/depth/480p_30` as a
  capture-policy-driven output: request the native `640x400` depth stream,
  force D2C mode, and deliver the resulting `640x480` aligned depth frames
- the backend may additionally treat `orbbec/preset/480p_30` as a
  capture-policy-driven grouped preset that delivers color plus aligned depth
  together, because the sibling live app flow proved that exact bundled
  behavior end to end
- `depth-720p_30` should not be published for the tested device, because no
  compatible `1280x720` D2C depth path was observed
- `depth-800p_30` may still be published as native depth when the device
  supports it, but it should not be relabeled as an aligned-depth variant on
  the current evidence
- the current donor worker guard that ignores D2C unless both color and depth
  are user-requested is therefore an implementation limitation, not a proven
  hardware requirement

What remains unknown:

- whether the device internally activates color hardware as a hidden helper when
  depth-only D2C is enabled
- whether every Orbbec model behaves the same way as the tested device

Those unknowns do not block the public contract:

- one URI still means one fixed published source shape
- normal use can still treat `orbbec/depth/480p_30` and
  `orbbec/preset/480p_30` as backend-fixed behavior
- no new dependency-specific discovery or source-response fields are required
  for this contract
- if operator explanation is useful, discovery may surface a short
  human-readable comment on unusual entries such as
  `orbbec/depth/480p_30` or `orbbec/preset/480p_30`; that note is informative
  only and must not become new structured dependency metadata

### 3. Keep Path-Based Channel Disambiguation

Dual-eye and stereo devices still need an explicit way to separate left and
right when the stream preset alone is not enough.

The preferred shape is an optional path suffix:

```text
insightos://<host>/<device>/<stream-preset>/channel/<channel>
```

Examples:

- `insightos://localhost/stereo-cam/video-720p_30/channel/left`
- `insightos://localhost/stereo-cam/video-720p_30/channel/right`

This should not be the common user story.

Preferred behavior:

- discovery emits the full final URI
- the frontend shows it as a ready-made choice
- users rarely type `/channel/...` manually

Usage-based decision:

- keep `/channel/<name>` in the path because the channel is part of the exact
  stream identity
- do not move this to a query parameter such as
  `insightos://host/device/preset?source=left`, because query syntax reads like
  an optional filter instead of part of the source choice
- the path form keeps URI equality easier to reason about in copy/paste flows

### 4. Source Groups Stay In Metadata

The grouped relationship remains metadata, not a required visible path layer.

Catalog and resolved-source metadata should still expose:

- `source_group_id`
- `member_kind`
- `channel`
- `delivered_caps`
- `capture_policy`

That preserves:

- color/depth pairing checks
- left/right validation
- restart-safe exact stream identity
- route rejection when the user picks the wrong stream
- grouped-runtime compatibility decisions
- capture-policy-driven delivered caps that differ from the underlying native
  sensor profile

## Why Earlier Options Were Rejected

### Flat path names such as `desk-rgbd-color`

This is not a good public contract.

Problems:

- it fuses the mutable device alias with a concrete source variant
- it creates fake device ids instead of describing one device with multiple
  selectable outputs
- it makes rename behavior harder to reason about
- it does not scale well to aligned-vs-native depth, left-vs-right, or future
  variants

### Visible `<group>` in the URI path

Example:

```text
insightos://host/<group>/<device>/<preset>
```

This is also not a good default.

Problems:

- it makes the URI harder to read
- it leaks internal grouping structure into the stable public path
- it still needs another discriminator for left/right or aligned/native depth
- it makes casual copy/paste and demo usage worse

### Route expectations with explicit low-level alignment logic

Example:

```cpp
app.route("orbbec/depth")
    .expect(insightos::Depth{})
```

Keeping grouped-source metadata is fine.

Making the route declare hardware pairing or D2C policy is not.

Problems:

- it makes the route declaration visibly configuration-heavy
- it forces application code to care about pairing and alignment policy
- it leaks backend grouping logic into the normal SDK flow

### Query params such as `?source=left`

This is not a good default either.

Problems:

- it makes exact stream identity look like an optional modifier
- it complicates URI equality checks in common copy/paste flows
- users already rely on discovery-generated final URIs, so the path form is not
  meaningfully harder to use

## Hard Requirements The Final Design Satisfies

Any final design must satisfy all of these:

1. Route declarations stay purpose-first.
2. Left and right channels can be told apart.
3. Native depth and aligned depth can be told apart.
4. Aligned depth and native depth can expose different caps without ambiguity.
5. The backend can still preserve same-device and same-group relationships
   internally when needed.
6. The common user flow does not require manual pairing logic.
7. The public URI stays readable.

The adopted solution satisfies those requirements by making discovery list exact
member choices and, when the member set is fixed and proven, grouped preset
choices rather than making the route layer infer them.

## Better Mental Model

The system should distinguish three things:

### 1. Public device alias

Human-facing stable name:

- `front-camera`
- `desk-rgbd`
- `stereo-cam`

### 2. Public preset

Human-facing capture choice:

- `720p_30`
- `480p_30`
- `stereo`

### 3. Exact source choice

Concrete selectable source under one device:

- `orbbec/color/480p_30`
- `orbbec/depth/400p_30`
- `orbbec/depth/480p_30`
- `orbbec/preset/480p_30`
- `video-720p_30/channel/left`
- `video-720p_30/channel/right`

The exact source choice is the real runtime identity. It must exist in catalog
metadata, even if users do not usually type it manually.

## Practical Guidance

### Keep The Route API Semantic

```cpp
app.route("yolov5").expect(insightos::Video{})
app.route("orbbec/color").expect(insightos::Video{})
app.route("orbbec/depth").expect(insightos::Depth{})
app.route("stereo/left-detector").expect(insightos::Video{}.channel("left"))
```

Routes for known processing logic should also stay explicit enough to reject
obvious misroutes:

```cpp
app.route("yolov5").expect(insightos::Video{})
app.route("depth-overlay").expect(insightos::Depth{})
```

### Keep Discovery Exact

- expose separate 400p and 480p depth URIs
- expose a grouped preset URI only when the bundled members are fixed and
  proven useful, as in `orbbec/preset/480p_30`
- expose channel-qualified URIs when left/right are both present
- persist the resolved exact stream identity and delivered caps
- treat grouped runtime behavior as fixed per discovered entry in normal use
- for tested Orbbec aligned depth, map delivered `480p` depth to a native
  `400p` depth request plus forced D2C in backend capture policy
- do not invent a `720p` aligned depth URI for the tested Orbbec unit; no
  compatible `1280x720` D2C depth path was observed
- treat `800p` depth as native on the tested Orbbec unit unless future
  device-specific evidence proves a separate aligned `800p` variant
- if helpful, discovery can show a short comment on `orbbec/depth/480p_30` or
  `orbbec/preset/480p_30` explaining how D2C or grouped capture policy is
  realized underneath

### Keep Advanced Channel Selection Optional

It is possible to support a dedicated `/channel/<name>` URI suffix.

That is acceptable because:

- it solves real dual-eye ambiguity
- it is only needed on a minority of devices
- discovery can emit the final URI so manual typing is uncommon

It should remain optional, not the default path shape for every device.

### Keep Grouped Preset Routing First-Class

Expected SDK behavior:

- apps still declare ordinary routes such as `orbbec/color` and
  `orbbec/depth`
- the user or frontend may still connect those routes separately using exact
  member URIs
- when discovery publishes a fixed grouped preset such as
  `orbbec/preset/480p_30`, one source bind should fan out to a grouped route target
  such as `orbbec`
- ordinary per-route callbacks remain the primary app surface; the SDK does not
  need a separate SDK-only frame-merge helper for this flow
- apps can stay hardware-agnostic by declaring only `Video{}` and `Depth{}`
  expectations, then binding them through catalog-published preset or exact
  member choices
- the combined callback fires only when both sides have frames close enough in
  `pts_ns`; otherwise the ordinary route callbacks continue independently
