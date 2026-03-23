# Grouped Source Selection Writeup

## Summary

This note captures the unresolved design problem around grouped sources in
`insight-io`.

The product goal is clear:

- keep the user-facing and app-facing configuration simple
- keep route declarations purpose-first
- avoid forcing users to think in raw runtime stream names

The hard runtime reality is also clear:

- some devices expose multiple related source variants
- some of those variants must be told apart explicitly
- some variants change caps in meaningful ways

This is most visible in two families:

- stereo or dual-eye cameras:
  - `left`
  - `right`
- RGBD depth sources:
  - native depth
  - D2C-aligned depth

Aligned depth is not just another label. It can change the delivered caps of
the depth stream, so it must remain distinguishable in backend resolution and
runtime metadata.

## Problem Statement

The route model should let applications declare:

```cpp
app.route("yolov5").expect(insightos::Video{})
app.route("scene-depth").expect(insightos::Depth{})
```

without forcing the application author to also spell:

- `left`
- `right`
- `color`
- `depth`
- `aligned`
- `native`
- `same_group_as(...)`

in the common path.

However, the backend still has to answer these questions deterministically:

1. Which concrete source variant should this route receive?
2. Is that source part of a grouped device?
3. If it is grouped, is the choice ambiguous?
4. Does the selected variant have different caps from another nearby variant?
5. Do two connected routes need to come from the same underlying source group?

## Why The Current Ideas Are Not Good Enough

### 1. Flat path names such as `desk-rgbd-color`

This is not a good public contract.

Problems:

- it fuses the mutable device alias with a concrete source variant
- it creates fake device ids instead of describing one device with multiple
  selectable outputs
- it makes rename behavior harder to reason about
- it does not scale well to aligned-vs-native depth, left-vs-right, or future
  variants

### 2. Visible `<group>` in the URI path

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

### 3. Mandatory query selection in all cases

Example:

```text
insightos://localhost/desk-rgbd/480p_30?source=depth
```

This is acceptable as an escape hatch, but not ideal as the primary user story.

Problems:

- it exposes source-variant detail too early
- it still requires the user to understand which member they need
- it does not reduce complexity enough for the common path

### 4. Route expectations with explicit low-level constraints

Example:

```cpp
app.route("scene-depth")
    .expect(insightos::Depth{}
                .same_group_as("scene-color")
                .require_alignment())
```

This is technically workable, but not ideal as the primary experience.

Problems:

- it makes the route declaration visibly configuration-heavy
- it forces application code to care about pairing and alignment policy
- it leaks backend grouping logic into the normal SDK flow

## Hard Requirements

Any final design must satisfy all of these:

1. Route declarations stay purpose-first.
2. Left and right channels can be told apart.
3. Native depth and aligned depth can be told apart.
4. Aligned depth and native depth can expose different caps without ambiguity.
5. The backend can still enforce same-device or same-group constraints when
   needed.
6. The common user flow does not require manual pairing logic.
7. The public URI stays readable.

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

### 3. Source variant

Concrete selectable source under one device + preset:

- `video`
- `depth-native`
- `depth-aligned`
- `left`
- `right`

The source variant is real runtime identity. It must exist in catalog metadata,
even if users do not usually type it manually.

## Recommended Direction

Use a hybrid design:

### A. Keep the base URI simple

Keep:

```text
insightos://<host>/<device>/<preset>
insightos://<host>/<device>/<preset>/<delivery>
```

This remains the primary public shape.

### B. Publish source variants in the catalog

For each device + preset entry, the catalog should expose zero or more source
variants with metadata such as:

- `source_variant_id`
- `source_group_id`
- `member_kind`
- `channel`
- `alignment`
- `caps`
- `is_default_for_expectation`

Examples:

- `member_kind = video`
- `member_kind = depth`, `alignment = native`
- `member_kind = depth`, `alignment = aligned`
- `member_kind = video`, `channel = left`
- `member_kind = video`, `channel = right`

### C. Make `route.expect(...)` semantic, not low-level

Good primary API:

```cpp
app.route("yolov5").expect(insightos::Video{})
app.route("scene-depth").expect(insightos::Depth{})
app.route("stereo-left-detector").expect(insightos::StereoLeft{})
```

Better grouped-source expectations for common use:

```cpp
app.route("scene").expect(insightos::RgbdAligned{})
app.route("stereo").expect(insightos::StereoPair{})
```

This keeps the common path short.

### D. Let the backend resolve the concrete source variant

The backend should choose the concrete source variant from catalog metadata
based on:

- the route expectation
- the selected device + preset
- policy defaults
- ambiguity rules

Examples:

- `Video{}` on a plain camera resolves to the default video source variant
- `Depth{}` on an RGBD device may resolve to `depth-aligned` if that is the
  product default for the chosen preset
- `StereoLeft{}` resolves to the left-eye source variant

### E. Reserve explicit source selection for advanced paths only

If the automatic choice is ambiguous, allow an advanced override, but do not
make it the normal contract.

Possible forms:

- query selector:
  - `?source=depth-aligned`
- REST field:
  - `"source_variant": "depth-aligned"`
- opaque catalog handle:
  - `"source_variant_id": "..."`

The first is human-readable and good for debugging.
The last is cleaner for frontend-driven flows.

## Recommended Default Policy

To reduce user complexity, the product should define defaults per device family
and preset.

Examples:

### RGBD

- `RgbdAligned{}`:
  - choose aligned depth when the preset supports it
- `Depth{}`:
  - choose the product-default depth variant for that preset
- `Video{}` on an RGBD device:
  - choose the color/video variant, not depth

### Stereo

- `StereoPair{}`:
  - return a paired grouped source
- `StereoLeft{}`:
  - choose left
- `StereoRight{}`:
  - choose right

This means the common user rarely needs to specify a source variant manually.

## Why D2C-Aligned Depth Needs To Stay Explicit In Metadata

Aligned depth cannot be treated as a cosmetic toggle.

Reasons:

- it may change width and height
- it may change the valid caps set
- it may affect whether a route is compatible with an expected processing path
- it may need to share capture policy with a related color source

Therefore the backend must preserve:

- which depth variant was selected
- whether it was aligned or native
- what caps actually apply to that variant

Even if the app does not spell this out directly, the system must store it.

## Practical Recommendation

For the next design round:

1. Keep `route.expect(...)`.
2. Do not require `on_stream(...)`.
3. Do not add `<group>` to the URI path.
4. Do not flatten variants into fake device aliases such as `desk-rgbd-color`.
5. Keep the base URI readable.
6. Move left/right and aligned/native distinction into catalog source-variant
   metadata.
7. Let the backend auto-resolve variants for common expectations.
8. Keep an advanced explicit variant selector only for debug and edge cases.

## Open Questions

1. Should the advanced override be a human-readable query selector or an opaque
   catalog-generated source-variant id?
2. Should `Depth{}` pick native or aligned depth by default for each preset?
3. Should `RgbdAligned{}` and `StereoPair{}` be first-class grouped
   expectations in v1, or should they wait until the simpler single-route
   design is implemented?
