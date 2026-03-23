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
- allow an optional `/channel/<channel>` path suffix only when channel
  disambiguation is required

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

### 3. Channel Disambiguation May Use `/channel/<name>`

Dual-eye and stereo devices still need an explicit way to separate left and
right when the stream preset alone is not enough.

That is allowed through an optional path suffix:

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

### Keep Discovery Exact

- expose separate 400p and 480p depth URIs
- expose channel-qualified URIs when left/right are both present
- persist the resolved exact stream identity and delivered caps

### Keep Advanced Channel Selection Optional

It is possible to support a dedicated `/channel/<name>` URI suffix.

That is acceptable because:

- it solves real dual-eye ambiguity
- it is only needed on a minority of devices
- discovery can emit the final URI so manual typing is uncommon

It should remain optional, not the default path shape for every device.
