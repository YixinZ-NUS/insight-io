# Past Tasks

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
