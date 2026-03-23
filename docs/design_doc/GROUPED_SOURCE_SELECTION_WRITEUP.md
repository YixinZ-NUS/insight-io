# Grouped Source Selection Writeup

## Summary

This note records the resolved design direction for grouped sources in
`insight-io`.

The adopted choices are:

- keep route declarations purpose-first
- make discovery publish exact stream choices up front
- keep the contract that one canonical URI maps to one delivered stream
- treat route expectations as validation, not hidden stream selection
- move D2C-sensitive depth differences into discovery-visible choices
- keep channel disambiguation in the URI path rather than in query params when
  it is required

One main objective is to mask heterogeneous hardware details from users,
including LLMs that use `insight-io` as a development substrate for reusable
audio/video apps.

## Final Decisions

### 1. Exact URI Beats Backend Guessing

The system no longer relies on route expectations to decide which concrete
variant the user meant.

Instead:

- discovery publishes the exact canonical URI
- the user copies or selects that URI
- the backend validates the URI against the route
- the backend does not silently swap one nearby variant for another

This closes the ambiguity that existed when route expectations and backend
policy were trying to choose the stream together.

### 2. RGBD D2C Is Resolved At Discovery Time

For the targeted 480p family:

- D2C off yields a `400p` depth output
- D2C on yields a `480p` depth output

That means the user-visible choice should also be split there.

Discovery must expose separate exact stream entries such as:

- `insightos://localhost/desk-rgbd/depth-400p_30`
- `insightos://localhost/desk-rgbd/depth-480p_30`

This lets the user choose the delivered output directly.

Implications:

- `Depth{}` validates that the selected URI is a depth stream
- the route layer no longer needs `require_alignment()`
- the resolved metadata still records the capture policy that produced that
  stream

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

Raw output was recorded in:

- the probe branch `orbbec-depth-480p-probe`, in files named
  `2026-03-23-orbbec-depth-probe.txt` and
  `2026-03-23-orbbec-depth-probe-extended.txt`

### 2A. Redesign After Real Orbbec Probe

The probe changes the design in an important way:

- `depth-480p_30` should remain a valid public exact-stream choice
- the backend must not implement that URI by naively looking for a native
  `640x480` depth sensor profile
- instead, the backend should treat `depth-480p_30` as a capture-policy-driven
  output: request the native `640x400` depth stream, force D2C mode, and
  deliver the resulting `640x480` aligned depth frames
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

- one canonical URI still delivers one stream
- normal use can still treat `depth-480p_30` as backend-fixed behavior
- no new dependency-specific discovery or source-response fields are required
  for this contract
- if operator explanation is useful, discovery may surface a short
  human-readable comment on unusual entries such as `depth-480p_30`; that note
  is informative only and must not become new structured dependency metadata

### 3. Keep Path-Based Channel Disambiguation

Dual-eye and stereo devices still need an explicit way to separate left and
right when the stream preset alone is not enough.

The preferred shape is an optional path suffix:

```text
insightos://<host>/<device>/<stream-preset>/channel/<channel>
insightos://<host>/<device>/<stream-preset>/channel/<channel>/<delivery>
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
  an optional filter instead of part of the canonical stream choice
- the path form composes more cleanly with the optional delivery suffix and
  keeps URI equality easier to reason about in copy/paste flows

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
app.route("orbbec-depth")
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
- it complicates canonical URI equality checks in common copy/paste flows
- it composes less cleanly with the optional delivery suffix
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
stream choices rather than making the route layer infer them.

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

### 3. Exact stream choice

Concrete selectable source under one device:

- `color-480p_30`
- `depth-400p_30`
- `depth-480p_30`
- `video-720p_30/channel/left`
- `video-720p_30/channel/right`

The exact stream choice is real runtime identity. It must exist in catalog
metadata, even if users do not usually type it manually.

## Practical Guidance

### Keep The Route API Semantic

```cpp
app.route("yolov5").expect(insightos::Video{})
app.route("orbbec-depth").expect(insightos::Depth{})
app.route("stereo-left-detector").expect(insightos::Video{}.channel("left"))
```

Routes for known processing logic should also stay explicit enough to reject
obvious misroutes:

```cpp
app.route("yolov5").expect(insightos::Video{})
app.route("depth-overlay").expect(insightos::Depth{})
```

### Keep Discovery Exact

- expose separate 400p and 480p depth URIs
- expose channel-qualified URIs when left/right are both present
- persist the resolved exact stream identity and delivered caps
- treat grouped runtime behavior as fixed per discovered entry in normal use
- for tested Orbbec aligned depth, map delivered `480p` depth to a native
  `400p` depth request plus forced D2C in backend capture policy
- do not invent a `720p` aligned depth URI for the tested Orbbec unit; no
  compatible `1280x720` D2C depth path was observed
- treat `800p` depth as native on the tested Orbbec unit unless future
  device-specific evidence proves a separate aligned `800p` variant
- if helpful, discovery can show a short comment on `depth-480p_30` explaining
  that it is aligned depth realized from native `640x400` plus D2C

### Keep Advanced Channel Selection Optional

It is possible to support a dedicated `/channel/<name>` URI suffix.

That is acceptable because:

- it solves real dual-eye ambiguity
- it is only needed on a minority of devices
- discovery can emit the final URI so manual typing is uncommon

It should remain optional, not the default path shape for every device.

### Keep `join()` / `pair()` Above Ordinary Routes

Expected SDK behavior:

- apps still declare ordinary routes such as `color` and `depth`
- the user or frontend still connects those routes separately using exact URIs
- `join(name)` combines already-declared route names into one higher-level
  callback
- `pair(a, b)` is shorthand for a two-route join
- these helpers do not choose hardware, infer D2C policy, or replace route
  expectations
- apps can stay hardware-agnostic by declaring only `Video{}` and `Depth{}`
  expectations, then combining those routes later by name
- the combined callback fires only when both sides have frames close enough in
  `pts_ns`; otherwise the ordinary route callbacks continue independently
