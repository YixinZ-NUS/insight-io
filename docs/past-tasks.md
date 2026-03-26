# Past Tasks

## 2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow

### What Changed

- reintroduced the persisted discovery stack for the standalone backend:
  - shared discovery/catalog types
    [types.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/types.hpp)
    and [types.cpp](/home/yixin/Coding/insight-io/backend/src/types.cpp)
  - discovery entry points
    [discovery.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/discovery.hpp),
    [discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/discovery.cpp),
    [v4l2_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/v4l2_discovery.cpp),
    [orbbec_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/orbbec_discovery.cpp),
    and [pipewire_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/pipewire_discovery.cpp)
  - persisted catalog service
    [catalog.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/catalog.hpp)
    and [catalog.cpp](/home/yixin/Coding/insight-io/backend/src/catalog.cpp)
- extended the HTTP surface so the checked-in backend now serves:
  - `GET /api/devices`
  - `GET /api/devices/{device}`
  - `POST /api/devices/{device}/alias`
- grounded the Orbbec catalog on the active docs and prior probe evidence:
  - V4L2 discovery skips Orbbec USB vendor nodes when Orbbec SDK discovery is
    active
  - the connected Orbbec serial `AY27552002M` now publishes
    `orbbec/depth/400p_30`, `orbbec/depth/480p_30`, and
    `orbbec/preset/480p_30`
  - grouped and aligned depth entries expose queryable RTSP metadata using the
    same selector path as the derived `insightos://` URI
- added focused verification coverage in
  [catalog_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/catalog_service_test.cpp)
  and updated
  [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
  to cover catalog listing, grouped Orbbec selectors, RTSP metadata, and alias
  updates
- updated the repo/operator docs:
  - [README.md](/home/yixin/Coding/insight-io/README.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  - [catalog-discovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/catalog-discovery-sequence.md)
- updated only verified tracker entries in:
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)

### Why

- direct sessions, app binds, reuse, and restart all depend on a stable source
  catalog first, so `devices` and `streams` had to become real persisted
  resources before the session and app layers come back
- the connected machine includes the exact hardware mix described in the docs:
  one webcam, one Orbbec device, and PipeWire sources, which made this the
  right slice to runtime-verify against real hardware rather than only donor
  assumptions
- the active grouped-source docs already document probe-backed depth behavior
  for Orbbec serial `AY27552002M`, so discovery should preserve that public
  contract even when raw SDK enumeration is incomplete in the current runtime

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18182 \
  --db-path /tmp/insight-io-catalog-verify.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18182/api/health
curl -s http://127.0.0.1:18182/api/devices | jq
curl -s http://127.0.0.1:18182/api/devices/sv1301s-u3 | jq
curl -s -X POST http://127.0.0.1:18182/api/devices/web-camera/alias \
  -H 'Content-Type: application/json' \
  -d '{"public_name":"front-camera"}' | jq
curl -s http://127.0.0.1:18182/api/devices/front-camera | jq
```

## 2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice

### What Changed

- reintroduced a buildable standalone backend scaffold in this repository:
  - top-level [CMakeLists.txt](/home/yixin/Coding/insight-io/CMakeLists.txt)
  - backend build file [backend/CMakeLists.txt](/home/yixin/Coding/insight-io/backend/CMakeLists.txt)
  - explicit schema [001_initial.sql](/home/yixin/Coding/insight-io/backend/schema/001_initial.sql)
  - SQLite bootstrap layer
    [schema_store.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/schema_store.hpp)
    and [schema_store.cpp](/home/yixin/Coding/insight-io/backend/src/schema_store.cpp)
  - backend HTTP surface
    [rest_server.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/rest_server.hpp),
    [rest_server.cpp](/home/yixin/Coding/insight-io/backend/src/api/rest_server.cpp),
    and [main.cpp](/home/yixin/Coding/insight-io/backend/src/main.cpp)
- added focused verification targets:
  - [schema_store_test.cpp](/home/yixin/Coding/insight-io/backend/tests/schema_store_test.cpp)
  - [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
- updated repository guidance and reporting:
  - [README.md](/home/yixin/Coding/insight-io/README.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  - [bootstrap-health-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/bootstrap-health-sequence.md)
- updated the broader runtime tracker so `runtime-build-and-test` now records
  verified pass state in
  [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

### Why

- the repository had no implementation left, so every later feature was blocked
  on first making `insight-io` buildable again
- the active docs require one explicit seven-table schema, so the bootstrap
  slice checks that in before higher-level discovery or app-routing code lands
- using a very small runtime surface keeps the new code aligned with the
  documented contract instead of dragging donor-only behavior back into the new
  repo prematurely

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18181 \
  --db-path /tmp/insight-io-live.sqlite3 \
  --frontend /tmp/frontend

curl -s http://127.0.0.1:18181/api/health
```

## 2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics

### What Changed

- updated the active docs hub, PRD, architecture note, data-model note, REST
  reference, task list, and feature trackers so the active contract now:
  - removes stale source-variant and source-group id fields from public source
    responses
  - keeps catalog publication metadata in `streams.publications_json` and makes
    `publications_json.rtsp.url` queryable
  - defines the RTSP publication URL as the same `/<device>/<selector>` path as
    the derived `insightos://` URI with the configured RTSP host replacing
    `localhost`
  - returns `409 Conflict` from `DELETE /api/sessions/{id}` while any
    `app_source` still references that session through `source_session_id` or
    `active_session_id`
- updated the implementation trackers so the new catalog RTSP metadata behavior
  and referenced-session delete conflict are both called out as future runtime
  checks

### Why

- the active contract had already moved away from variant/group id response
  fields, but several active docs still exposed them and made the schema read
  heavier than the current design actually is
- catalog publication metadata needed one explicit rule so RTSP addresses are
  predictable and queryable without turning RTSP into part of source identity
- session delete needed a hard contract because silently detaching or rewriting
  app-source references would create ambiguous runtime ownership and surprise
  callers

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase

### What Changed

- added
  [POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md)
  to record the recommendation that `insight-io` should keep a distinct
  runtime-only publication phase after capture for output profile, codec, and
  protocol-specific publication work
- updated
  [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  so the active architecture now explicitly separates capture workers from a
  post-capture publication phase
- updated [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md),
  [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md),
  [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md), and
  [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  so the new runtime boundary is visible in the diagram, backlog, hub, and docs
  tracker

### Why

- the current contract already separates capture-side source choice from RTSP
  publication intent, so a runtime publication phase is the clean place for
  output profile selection and codec/publication handling
- the donor material also separates source-side capture policy from
  promise-specific publication work, which supports keeping this boundary in the
  runtime rather than reintroducing durable delivery tables
- local IPC and future RTSP/LAN consumers need different publication handling,
  but that should not force duplicate capture workers or a more complex durable
  schema in v1

### Verification

- reviewed and aligned:
  - [POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)

## 2026-03-25 – Document RTSP Publication Reuse After Delivery-Name Removal

### What Changed

- added
  [RTSP_PUBLICATION_REUSE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/RTSP_PUBLICATION_REUSE_WRITEUP.md)
  to explain that the old donor-style rule
  "`ipc` versus `rtsp` implies separate delivery sessions" is no longer the
  active public contract
- updated [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md) so
  the docs hub now points readers at that writeup and summarizes the active
  additive RTSP publication rule
- updated
  [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  so the docs tracker explicitly verifies the writeup and the new reuse rule

### Why

- the active `insight-io` docs now model RTSP as optional publication state via
  `rtsp_enabled`, not as one inferred peer delivery mode alongside implicit IPC
- that means the older statement "different inferred `delivery_name` values must
  remain separate delivery sessions" no longer describes the public contract
- the donor runtime may still split publishers internally, but the API should
  only promise shared runtime plus additive RTSP publication when possible

### Verification

- reviewed and aligned:
  - [RTSP_PUBLICATION_REUSE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/RTSP_PUBLICATION_REUSE_WRITEUP.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)

## 2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations

### What Changed

- updated the active docs hub, PRD, architecture note, data-model note, REST
  reference, interaction note, and feature trackers so the current contract now
  says:
  - a direct session is one standalone or session-first runtime created from a
    selected URI before any app target is involved
  - declaring one compatible route does not make an app consume that direct
    session automatically
  - an app starts receiving frames only after one app-source bind becomes
    active by `input` or `session_id`
  - route names stay app-local and should describe logical input roles rather
    than discovered device aliases or one globally unique route string
  - a multi-device app that consumes two V4L2 cameras plus one Orbbec should
    declare app-local routes such as `front-camera`, `rear-camera`,
    `orbbec/color`, and `orbbec/depth`
- updated the broader user-journey tracker to add an explicit future runtime
  check that direct sessions stay idle for apps until a bind exists

### Why

- "direct session" was still easy to misread as capture-only or as implicitly
  attached to any app that had already declared matching routes
- multi-device apps needed one explicit naming rule so route declarations stay
  stable when the discovered device URIs or aliases change underneath

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent

### What Changed

- updated the active PRD, architecture note, data-model note, REST reference,
  grouped-source writeup, interaction note, task list, diagrams, and feature
  trackers so the current contract now says:
  - public app-source binds post one app-local `target` field instead of
    separate `route` and `route_grouped` inputs
  - grouped versus exact target resolution is hidden behind server-side target
    resolution
  - apps must reserve grouped roots:
    one app can not declare both one exact route `x` and any route below `x/`
  - local SDK guidance now uses `bind_source(...)` and `rebind(...)`
  - `/channel/...` is removed from the active v1 URI grammar
  - RTSP is modeled as optional durable publication intent through
    `rtsp_enabled` rather than as a peer to implicit local IPC attach
  - raw `rtsp://` input remains a future ingest/import path rather than a v1
    source-selection shape
- updated the durable schema docs and ER diagram so:
  - `streams.deliveries_json` becomes `streams.publications_json`
  - `app_sources.route_grouped` becomes `app_sources.target_name`
  - `app_sources.delivery_name` and `sessions.delivery_name` become
    `rtsp_enabled`
- updated the runtime diagram and tracker language to reflect additive RTSP
  publication on shared runtime instead of separate `ipc` versus `rtsp`
  delivery-intent branches

### Why

- a public split between exact and grouped bind methods leaked an internal
  distinction the backend can resolve from one posted target name
- `ipc` is not a meaningful user-facing publication choice for local SDK app
  binds; it is the fixed local attach mechanism
- RTSP publication changes durable resource state, so it belongs in the
  resource body and schema rather than in a query string such as `?rtsp=on`

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds

### What Changed

- updated the active PRD, architecture note, data-model note, REST reference,
  grouped-source writeup, interaction note, task list, diagrams, and feature
  trackers so the current contract now says:
  - public `uri` values are derived source identifiers rather than durable DB
    keys
  - `delivery_name` is inferred during normalization and then persisted on
    `app_sources` and `sessions` rather than being part of stored source
    identity
  - `POST /api/apps/{id}/sources` is the single app-control surface for both
    URI-backed connects and session-backed attaches
  - grouped-session attach uses the same `route_grouped` surface as grouped
    preset URI binds
  - local SDK attach remains IPC-only in v1
  - future remote or LAN RTSP consumption remains planned as a separate path
- updated the grouped-startup wording so an app with one grouped target can
  start from one bare grouped preset URI without separately managing
  `/color` and `/depth`
- updated the ER and runtime diagrams to reflect derived `uri`, durable
  `delivery_name`, unified app-source binds, and IPC-only local attach

### Why

- tying delivery into the public URI shape made app-route binds ambiguous once
  local SDK attach was constrained to IPC
- a separate route-scoped `attach-session` endpoint was under-modeled once
  grouped-session attach and external app control were both in scope
- the split between `source_session_id` and `active_session_id` needed to be
  documented explicitly to avoid future confusion when remote or LAN RTSP
  consumption is added later

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-24 – Add Mermaid ER Diagram For The Simplified Schema

### What Changed

- added [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  with a Mermaid `erDiagram` for the active durable schema
- linked the ER diagram from
  [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  and [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)

### Why

- the simplified schema was documented textually, but it still benefited from
  one visual PK/FK map
- the docs set already had a runtime diagram; the schema now has a matching ER
  view

### Verification

- reviewed and aligned:
  - [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)

## 2026-03-24 – Simplify The Durable Data Model And Add A Docs Hub

### What Changed

- added [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md) as the
  centralized entry point for the active design set and updated
  [README.md](/home/yixin/Coding/insight-io/README.md) and
  [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md) to route readers there
  first
- renamed grouped bind terminology across the active contract from
  `route_namespace` / `connect_namespace(...)` to
  `route_grouped` / `connect_grouped(...)`
- rewrote
  [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  so the durable schema is now explicitly minimal:
  - `devices`
  - `streams`
  - `apps`
  - `app_routes`
  - `app_sources`
  - `sessions`
  - `session_logs`
- removed migration-history-table and backward-compat schema requirements from
  the active design because `insight-io` is expected to start from a fresh
  implementation
- kept per-device exact-member and grouped preset choices inside `streams`
  instead of splitting them into a separate preset table
- removed lower-level runtime tables from the active durable-schema design and
  treated capture, delivery, reuse, RTSP, IPC attach, and worker details as
  runtime-only status concerns instead
- updated the architecture note, PRD, REST reference, runtime diagram, task
  list, interaction note, and feature trackers to match the new grouped-bind
  naming and smaller schema boundary

### Why

- `route_namespace` was still describing the mechanism more than the business
  meaning; `route_grouped` is plainer
- the old data-model note was still pulling runtime internals into the durable
  relational design and was heavier than the current product contract needs
- a greenfield repo should not carry migration-history or compatibility
  scaffolding before the first real implementation even exists
- a separate preset table would duplicate the same device-scoped catalog fields
  already needed by `streams`
- the repo also lacked a single obvious entry point, which made the design set
  harder to navigate than it should be

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [README.md](/home/yixin/Coding/insight-io/README.md)
  - [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)

## 2026-03-24 – Separate Catalog Publication From Runtime Ownership And Rename Route APIs

### What Changed

- reviewed `git blame` on the PRD and data-model contract and found the
  `discovery-owned` wording was new working-tree wording rather than a stable
  historical design boundary
- updated the PRD, architecture note, data-model note, REST reference,
  grouped-source writeup, interaction note, runtime diagram, task list, and
  feature trackers so the active contract now says:
  - discovery publishes selectable source shapes and metadata
  - logical, capture, delivery, and worker sessions realize those choices at
    runtime
  - one canonical URI selects one fixed catalog-published source shape
- renamed grouped bind terminology from `route_prefix` and `connect_prefix` to
  `route_namespace` and `connect_namespace`
- renamed SDK doc vocabulary from `RouteScope` to `AppRoute` and replaced
  `route-scoped callbacks` phrasing with callbacks on declared routes
- updated [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md) so future work
  must keep header metadata on docs and implementation files, sweep related
  docs after major changes, update problem-doc status, and periodically archive
  stale docs

### Why

- discovery/catalog authority and runtime session ownership are different
  responsibilities and the docs should not blur them
- the blame trail showed the older repo baseline was about fixed URI meaning,
  not discovery owning runtime workers
- `*Scope` and `*prefix` names describe mechanism more than business meaning;
  `AppRoute` and `route_namespace` better match the intent-first model

### Verification

- checked `git blame` on:
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
- reviewed and aligned:
  - [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-24 – Import The Live RGBD Preset Success Into The Intent-Routing Contract

### What Changed

- updated the PRD, architecture note, data-model note, REST reference,
  interaction note, runtime diagram, README, AGENTS guidance, task list, and
  feature trackers so the repo now documents:
  - exact-member URIs that still resolve to one delivered stream
  - grouped preset URIs that may resolve to one fixed related stream bundle
  - namespaced RGBD routes such as `orbbec/color` and `orbbec/depth`
  - one grouped preset bind using `route_namespace`, for example
    `orbbec/preset/480p_30`, instead of a separate SDK-only frame-merge layer
- documented the proven grouped Orbbec preset choice
  `orbbec/preset/480p_30` alongside exact member choices such as
  `orbbec/depth/400p_30` and `orbbec/depth/480p_30`

### Why

- the sibling `insightos` repo now has a real end-to-end RGBD app success:
  one `480p_30` URI delivered color plus aligned depth, and that grouped flow
  was good enough to drive a live “capture when object < 50cm” application on
  the connected Orbbec camera
- that result weakens the earlier `insight-io` assumption that every canonical
  URI must resolve to exactly one delivered stream
- for the next full-stack repo, it is cleaner to keep routes intent-first with
  `app.route(...).expect(...)`, allow one grouped preset bind to fan out into
  related route namespaces, and remove the extra conceptual layer of a
  separate SDK-only frame-merge helper

### Verification

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
- grounded the grouped preset change on the sibling live success:
  - [rgbd_proximity_capture.cpp](/home/yixin/Coding/insightos/examples/rgbd_proximity_capture.cpp)
  - [rgbd-proximity-capture-20260324-101043-202.jpg](/home/yixin/Coding/insightos/captures/rgbd-proximity-capture-20260324-101043-202.jpg)

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
  - one canonical URI still maps to one fixed published source shape
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
- documented grouped preset routing above ordinary namespaced routes so apps
  can declare color and depth separately, then activate them together through
  one route-namespace bind without hardware-specific route setup
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
- combined color+depth consumption is easier for app authors if one grouped
  preset bind can activate named routes directly instead of turning paired
  hardware into a separate SDK-only routing primitive
- exact Orbbec aligned-depth-only behavior still needs real-device evidence, so
  the docs should frame that as an investigation rather than pretending the
  dependency shape is already settled

### Verification

- verified feature ids:
  - `docs-grouped-runtime-rule`
  - `docs-channel-path-and-grouped-preset-contract`
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
  - one canonical URI selects one fixed published source shape
  - discovery publishes exact member choices up front
  - route expectations validate compatibility instead of choosing hidden stream
    variants
- adopted the RGBD depth decision that D2C-sensitive outputs are exposed at
  discovery time as separate user choices, for example:
  - `orbbec/depth/400p_30`
  - `orbbec/depth/480p_30`
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
