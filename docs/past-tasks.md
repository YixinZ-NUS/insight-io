# Past Tasks

## 2026-03-23 – Extend Orbbec Probe For 720p And 800p Depth Modes

### What Changed

- extended `experiments/orbbec_depth_probe/probe.cpp` to:
  - print D2C-compatible depth-profile lists for selected color modes
  - distinguish strict profile matches from fallback selection
  - probe `1280x800` native depth plus forced D2C cases
  - probe whether `1280x720` aligned depth exists through D2C
- recorded the expanded raw output on branch `orbbec-depth-480p-probe` in
  `2026-03-23-orbbec-depth-probe-extended.txt`
- updated the grouped-source writeup, PRD, architecture note, data-model note,
  REST reference, and runtime feature tracker to reflect the new device result

### Why

- the earlier real-device probe only settled the `480p` aligned-depth case
- the design still needed evidence for whether this device supports aligned
  `720p` depth or a separate aligned `800p` mode
- discovery should not publish new exact-stream variants without device proof

### Results

- the connected device exposes native `1280x800` depth profiles but no native
  `1280x720` depth profile
- `getD2CDepthProfileList` returned no compatible depth profiles for color
  `1280x720@30` in either software or hardware D2C mode
- forcing software or hardware D2C on depth-only `1280x800@30` still delivered
  `1280x800` `y16` depth frames
- no distinct aligned `720p` or aligned `800p` depth output was observed on
  the tested device
- the docs now treat `depth-480p_30` as the only proven special aligned depth
  choice on this Orbbec unit and allow, at most, a short discovery comment for
  operator context rather than new dependency-specific fields

### Verification

- verified feature id `orbbec-depth-720p-800p-sdk-probe`
- built and ran on branch `orbbec-depth-480p-probe`:

```bash
cmake -S experiments/orbbec_depth_probe -B build/orbbec_depth_probe
cmake --build build/orbbec_depth_probe -j4
ORBBEC_SDK_CONFIG=$PWD/experiments/orbbec_depth_probe/vendor/orbbec_sdk/config/OrbbecSDKConfig_v1.0.xml \
  ./build/orbbec_depth_probe/orbbec_depth_probe
```

## 2026-03-23 – Run Real Orbbec Depth-480 Probe And Redesign The Contract

### What Changed

- created the isolated branch `orbbec-depth-480p-probe`
- copied a minimal Orbbec SDK subset into `experiments/orbbec_depth_probe` and
  added a standalone probe harness plus build files
- ran the probe against the connected Orbbec device and recorded the raw output
  on branch `orbbec-depth-480p-probe` in `2026-03-23-orbbec-depth-probe.txt`
- updated the grouped-source writeup, PRD, architecture note, data-model note,
  REST reference, and runtime feature tracker to reflect the real-device result

### Why

- the previous docs still treated aligned-depth-only Orbbec behavior as an open
  question
- the current donor worker disables D2C unless both color and depth are
  user-requested, but that turns out to be stricter than the tested hardware
  requires
- the backend design now needs to encode `depth-480p_30` as a capture-policy
  mapping from native `640x400` depth plus forced D2C, not as a literal native
  `640x480` depth profile lookup

### Results

- the connected device reported color `640x480@30` profiles and native depth
  `640x400@30` profiles, but no native `640x480` depth profile
- depth-only native capture delivered `640x400` `y16` depth frames
- depth-only forced software D2C delivered `640x480` `y16` depth frames with
  zero delivered color frames
- depth-only forced hardware D2C delivered `640x480` `y16` depth frames with
  zero delivered color frames
- color+depth software and hardware D2C also delivered `640x480` `y16` depth
  frames

### Verification

- verified feature id `orbbec-depth-480-sdk-probe`
- built and ran on branch `orbbec-depth-480p-probe`:

```bash
cmake -S experiments/orbbec_depth_probe -B build/orbbec_depth_probe
cmake --build build/orbbec_depth_probe -j4
ORBBEC_SDK_CONFIG=$PWD/experiments/orbbec_depth_probe/vendor/orbbec_sdk/config/OrbbecSDKConfig_v1.0.xml \
  ./build/orbbec_depth_probe/orbbec_depth_probe
```

## 2026-03-23 – Tighten Grouped Runtime, Route Validation, And Join/Pair Docs

### What Changed

- documented the grouped runtime rule across the PRD, architecture note,
  data-model note, REST reference, and runtime diagram:
  - one canonical URI still maps to one delivered stream
  - related URIs from one source group must either resolve to one compatible
    grouped backend mode or reject
  - normal use remains backend-fixed per discovered catalog entry, with no
    bind-time override layer
- tightened wrong-route protection by documenting that non-debug routes should
  declare `media` expectations so obvious misroutes such as depth into a video
  detector are rejected by contract
- reevaluated channel disambiguation from usage only and kept
  `/channel/<name>` in the path instead of moving source selection into a query
  parameter
- documented SDK-level `join()` / `pair()` behavior as helpers above ordinary
  routes so apps can declare color and depth separately, then combine them by
  route name without hardware-specific route setup
- added a real-device Orbbec experiment plan for testing whether
  `depth-480p_30` can run as the only user-requested stream and how grouped
  runtime behaves underneath
- updated the feature trackers to record the new doc-grounding checks and the
  pending runtime investigations

### Why

- the earlier docs described source groups and aligned depth as catalog choices,
  but they still left grouped-runtime conflict handling too implicit
- wrong-route rejection needs a slightly stronger contract than "optional
  expectations" if common app routes are supposed to be safe by default
- from usage alone, channel choice behaves like part of stream identity rather
  than like an optional URI filter
- combined color+depth consumption is easier for app authors if the SDK offers
  a helper above named routes instead of turning paired hardware into a new
  public routing primitive
- exact Orbbec aligned-depth-only behavior still needs real-device evidence, so
  the docs should frame that as an investigation rather than pretending the
  dependency shape is already settled

### Verification

- verified feature ids:
  - `docs-grouped-runtime-rule`
  - `docs-channel-path-and-join-pair-contract`
- reviewed and aligned:
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-23 – Convert Repo To Docs-Only Exact-Stream Design Baseline

### What Changed

- rewrote the core design docs so the project contract is now explicitly:
  - one canonical URI maps to one delivered stream
  - discovery publishes exact stream choices up front
  - route expectations validate compatibility instead of choosing hidden stream
    variants
- adopted the RGBD depth decision that D2C-sensitive outputs are exposed at
  discovery time as separate user choices, for example:
  - `depth-400p_30`
  - `depth-480p_30`
- adopted optional `/channel/<name>` URI disambiguation for stereo or dual-eye
  devices, while keeping discovery responsible for emitting the final full URI
- expanded the lifecycle contract in the PRD, REST reference, and feature
  trackers to cover:
  - direct sessions through REST and `insightos-open`
  - app-first routing
  - session-first attach
  - identical-URI fan-out
  - different-delivery shared-capture behavior
  - runtime rebind
  - runtime and session inspection
- reset the implementation trackers so previously scaffold-backed runtime
  features are no longer marked as passing
- converted the repository to docs-only and removed the outdated checked-in
  implementation scaffold

### Why

- the old scaffold and the current design docs had diverged enough that keeping
  both in one repo created false confidence
- the grouped-source writeup exposed a deeper contract issue:
  route expectations and backend policy were both trying to decide which stream
  the user meant
- for RGBD depth, the D2C choice materially changes the delivered output, so it
  belongs in discovery-visible user choice rather than hidden route policy
- for stereo and dual-eye devices, channel distinction needs an explicit but
  uncommon escape hatch; optional `/channel/<name>` is enough
- deleting the stale implementation makes the repository honest again and turns
  it into a clean baseline for the next implementation round

### Verification

- reviewed and aligned:
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
- inspected the repository tree after deletion to confirm that only docs remain

## 2026-03-23 – Write Up Grouped Source Selection Problems

### What Changed

- added the focused design note
  [GROUPED_SOURCE_SELECTION_WRITEUP.md](design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  to document the unresolved complexity around:
  - left/right source separation
  - native versus D2C-aligned depth
  - grouped-source pairing
  - keeping route declarations simple
- recorded why fake flat names such as `desk-rgbd-color` and a visible
  `<group>` URI path layer are both poor defaults
- documented the recommended direction:
  - keep the base URI readable
  - move left/right and aligned/native distinction into catalog source-variant
    metadata
  - keep `route.expect(...)` semantic
  - let the backend auto-resolve source variants in the common path

### Why

- the current design discussion has a real unresolved tension:
  - users should not need to configure pairing and alignment manually
  - the backend still has to distinguish variants whose caps differ
- aligned depth is not just another label; it can change the delivered caps, so
  the backend must preserve that distinction even if the app API stays simple
- writing the problem down in one focused note is better than encoding a
  premature answer directly into the PRD

### Verification

- reviewed donor grounding for grouped RGBD behavior and D2C policy:
  - [standalone-project-plan.md](/home/yixin/Coding/insightos/docs/plan/standalone-project-plan.md#L179)
  - [TECH_REPORT.md](/home/yixin/Coding/insightos/docs/design_doc/TECH_REPORT.md#L808)
  - [request_support_test.cpp](/home/yixin/Coding/insightos/backend/tests/request_support_test.cpp#L93)
  - [orbbec_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/orbbec_discovery.cpp#L223)
- verified the new repo now contains
  [GROUPED_SOURCE_SELECTION_WRITEUP.md](design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)

## 2026-03-23 – Add Interaction Context And Broader User-Journey Trackers

### What Changed

- expanded the PRD in
  [fullstack-intent-routing-prd.md](prd/fullstack-intent-routing-prd.md)
  with an explicit interaction baseline section that ties `insight-io` to the
  audited donor flows instead of describing the product only in abstract terms
- added [INTERACTION_CONTEXT.md](features/INTERACTION_CONTEXT.md) to explain
  how the old operator demos and SDK integration test map onto the new
  DB-first route-based project
- added the broader tracker
  [runtime-and-app-user-journeys.json](features/runtime-and-app-user-journeys.json)
  to cover:
  - backend bootstrap and health
  - catalog and alias flows
  - direct session flows
  - RTSP and audio verification
  - persisted session restart
  - idle app registration
  - late source injection into declared routes
  - mixed video and RGBD routing
  - stream rename and delivery reuse edge cases
  - browser recovery flows
- revised the public naming direction so the new concept is a route above the
  existing callback chain instead of a full replacement for the SDK callback
  surface
- added explicit feature requirements for:
  - single-URI app launch continuity
  - multi-route CLI launch with named connections
  - explicit route declaration before startup

### Why

- the original feature tracker in this repo captured the new persistence and
  route-connection core, but it did not yet represent the full user interaction
  surface that the donor repo already demonstrates
- the donor demos and the SDK integration test are the best available source of
  truth for what real operator and developer workflows should remain possible
- writing those journeys down now gives the repo a better implementation
  backlog for the SDK and frontend phases
- keeping the app surface visually close to the current SDK lowers migration
  cost and preserves the sample-app style already used across the donor repo
- a route-above-stream model is closer to `dora-rs`, which uses named inputs
  and outputs rather than removing stream names like `frame`, `color`, or
  `depth` from app-facing code

### Verification

- opened and reviewed:
  - `/home/yixin/Coding/insightos/demo_command.md`
  - `/home/yixin/Coding/insightos/demo_command_3min.md`
  - `/home/yixin/Coding/insightos/sdk/tests/app_integration_test.cpp`
- reviewed `dora-rs/dora` interface structure through DeepWiki and confirmed
  that it models routing with named inputs and outputs
- verified the new repo now contains:
  - [fullstack-intent-routing-prd.md](prd/fullstack-intent-routing-prd.md)
  - [INTERACTION_CONTEXT.md](features/INTERACTION_CONTEXT.md)
  - [runtime-and-app-user-journeys.json](features/runtime-and-app-user-journeys.json)

## 2026-03-23 – Bootstrap Standalone `insight-io`

### What Changed

- created a new standalone repository root for the DB-first route-based
  project under `insight-io`
- added repo grounding documents:
  - [fullstack-intent-routing-prd.md](prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [intent-routing-runtime.md](diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-task-list.md](tasks/fullstack-intent-routing-task-list.md)
  - [fullstack-intent-routing-e2e.json](features/fullstack-intent-routing-e2e.json)
- added repo-level agent guidance in [AGENTS.md](../AGENTS.md) so future work
  is grounded on the new docs and uses the feature list as the verification
  scoreboard
- carried over the backend-first scaffold needed to continue implementation:
  - explicit schema in [001_initial.sql](../backend/schema/001_initial.sql)
  - durable app, route, and source persistence in the backend store
  - route-aware REST routes for app route CRUD and source connection
  - focused backend tests for persistence and route validation
- replaced copied repo docs with standalone versions of [README.md](../README.md)
  and [REST.md](REST.md) so the new repository is self-contained

### Why

- the work had to move from an incremental prototype inside `insightos` into a
  clean standalone project with its own git history
- the new repository needs to be grounded by docs first so implementation stays
  aligned with the route-based product framing
- the feature tracker has to remain the single pass/fail scoreboard for future
  implementation slices

### Verification

```bash
cmake -S . -B build
cmake --build build -j4 --target \
  device_store_test \
  rest_server_test

./build/bin/device_store_test
./build/bin/rest_server_test
```
