# Intent Routing Architecture

## Summary

This note documents the architecture of the DB-first route-based rebuild.

One main objective is to mask heterogeneous hardware details from users,
including LLM-assisted app builders, by making discovery publish normalized
source choices instead of forcing clients to reason about per-device
quirks.

The public app model is:

- apps declare named routes
- each route describes purpose and optional semantic expectations
- one canonical URI selects one fixed catalog-published source shape
- the discovery catalog exposes exact member choices up front and may also
  expose grouped preset choices when the member bundle is fixed and proven
- grouped devices expose related source identities through catalog metadata
- discovery publishes selectable source shapes and metadata; sessions own
  runtime realization, reuse, and lifecycle
- the backend validates route expectations against resolved source metadata
- grouped-device runtime behavior remains fixed per discovered catalog entry in
  normal use

## Top-Level Structure

```text
device discovery   enumerate exact-member and grouped-preset catalog entries, publish source-shape metadata
frontend / SDK     create apps, declare routes, connect URIs, inspect status
REST API           durable app/route/source control plane + direct session APIs
durable schema     devices -> streams, apps -> app_routes -> app_sources, sessions -> session_logs
session manager    validates route expectations, expands grouped presets, and realizes catalog choices as logical sessions
runtime workers    capture, delivery, RTSP, IPC attach, and reuse planning
```

## Design Boundaries

### Preserved

- donor-grounded capture workers
- donor-grounded device discovery
- local IPC transport
- RTSP publication path
- existing `SessionRequest` and runtime session graph

### Replaced

- runtime-only app registry
- stream-name-first high-level SDK contract
- inline-only DDL as the only schema definition
- session-launcher frontend framing
- backend-hidden stream-variant selection from route expectations

## Orchestration Model

### Durable control plane

```text
catalog
  devices
  streams

app intent
  apps
  app_routes
  app_sources

session history
  sessions
  session_logs
```

Meaning:

- `devices` and `streams` are the catalog cache for selectable source shapes
- `apps` are persistent application records
- `app_routes` declare intended processing uses and semantic expectations
- `app_sources` bind routes either to exact URIs, grouped preset targets, or
  existing logical sessions
- `sessions` store logical session history
- `session_logs` store append-only audit events
- each route or grouped target owns at most one active binding at a time
- multiple apps may reuse the same canonical URI across one or more sessions

### Runtime execution

The runtime worker graph still exists, but it does not need first-class durable
tables in v1:

```text
sessions
  worker planning
    capture reuse
    delivery reuse
    RTSP / IPC publishers
```

The SQL schema records intent and history. The worker graph records the current
process realization.

Discovery does not own worker sessions. It only publishes selectable catalog
entries and their metadata. The session graph, donor-grounded workers, and
runtime reuse rules realize those choices later.

## Discovery And Source Catalog

Discovery is now responsible for surfacing user choices directly.

The catalog must publish one entry per fixed published source shape with
metadata such as:

- canonical URI
- source shape
- exact stream id
- source variant id
- source group id
- member kind
- channel when applicable
- delivered caps
- capture policy requirements
- supported delivery suffixes
- grouped preset members when applicable

Examples:

- `orbbec/color/480p_30`
- `orbbec/depth/400p_30`
- `orbbec/depth/480p_30`
- `orbbec/preset/480p_30`
- `video-720p_30/channel/left`
- `video-720p_30/channel/right`

If backend processing changes the delivered caps, discovery must split that into
separate catalog entries instead of asking the route layer to infer it later.

For RGBD depth this means:

- `orbbec/depth/400p_30` is the native depth output
- `orbbec/depth/480p_30` is the aligned depth output
- `orbbec/preset/480p_30` is the fixed bundled color + aligned-depth output

The user chooses between them at discovery time.

## Route Resolution Model

Route resolution is an app-layer concern above stream publication:

1. frontend or SDK declares routes on an app
2. frontend or SDK connects one URI with either:
   - `route` for one exact-member bind, or
   - `route_grouped` for one grouped preset bind
3. backend normalizes the URI into `SessionRequest`
4. backend resolves either one concrete exact stream identity or one concrete
   preset member set from that URI
5. backend reads route expectations from `expect_json`
6. backend validates:
   - media kind
   - channel constraints
   - grouped preset member-to-route matches when `route_grouped` is used
7. backend creates one logical session or one grouped logical session
8. SDK attaches through the existing local stream attach contract

Important boundary:

- the route does not choose hidden variants
- the URI and catalog already determine the exact member or grouped preset
- route validation may reject an incompatible URI, but it does not rewrite it

This keeps the data plane stable while changing the orchestration surface.

## Source Groups

Grouped devices are the important special case:

- RGBD color + depth
- stereo left + right

The recommended approach is:

- discovery publishes separate exact member entries
- discovery may additionally publish grouped preset entries when the grouped
  member set is fixed and proven
- the catalog marks related entries with `source_group_id`
- grouped preset fan-out targets an app grouped bind such as `orbbec`, letting
  routes like `orbbec/color` and `orbbec/depth` stay ordinary intent-first
  route declarations
- dual-eye channel disambiguation should stay in the URI path with an optional
  `/channel/<channel>` suffix when needed, because it identifies the exact
  stream rather than acting like an optional query filter
- most users should copy the full discovery-generated URI rather than compose
  selectors manually

This keeps the URI readable while still letting the backend prove exact stream
identity.

Dependent-source boundary:

- grouped-source relationships remain visible through existing metadata such as
  `source_group_id`, `member_kind`, and `capture_policy`
- the public contract does not add dependency-specific discovery fields yet
- grouped preset URIs are allowed only when discovery can prove the delivered
  member set and its backend policy are fixed
- the tested Orbbec device produced aligned `640x480` depth from a depth-only
  request, so the key remaining backend task is capture-plan encoding rather
  than proving the public one-stream contract
- the sibling `insightos` live RGBD proximity-capture flow also proved that a
  single `480p_30` preset request can deliver color plus aligned depth together
  in a stable way that is useful to applications
- the same device exposed no compatible `1280x720` D2C depth path and no
  distinct aligned `1280x800` output, so discovery should not publish those as
  aligned-depth variants on that unit
- if special-case explanation helps operators, discovery may surface a short
  human-readable comment for an entry such as `orbbec/depth/480p_30` or
  `orbbec/preset/480p_30`; that comment is informative only and does not
  replace the exact URI contract

## Grouped Runtime Rule

When multiple exact-member or grouped-preset URIs from the same source group
are active:

- the session manager must try to place them onto one compatible grouped
  backend mode
- if that is not possible without changing what an existing canonical URI means,
  the backend must reject the newer request
- bind-time overrides are out of scope for normal use; selecting a different
  discovered URI is the supported way to ask for different runtime behavior

## Reuse, Fan-Out, And Reroute

The runtime must preserve donor-style reuse semantics:

- identical canonical URIs may share capture and delivery runtime
- consumers attached to the same canonical URI should observe the same
  frame sequence when the shared delivery path is reused
- URIs that differ only by delivery suffix, such as `/mjpeg` versus `/rtsp`,
  may share capture while keeping distinct delivery sessions

The app layer must also support:

- app-first binding: create app, then connect exact URI
- session-first binding: create direct session, then attach `session_id` to a
  route
- runtime rebind: replace one route or grouped target binding without
  destroying the durable app record

## Backend Responsibilities

### Database layer

- own the checked-in canonical SQL schema
- load/save devices, streams, apps, routes, sources, sessions, and logs
- normalize runtime-only state on startup
- keep the durable schema minimal
- avoid schema-history or backward-compat scaffolding in v1

### Discovery layer

- publish exact-member catalog entries
- publish grouped preset catalog entries when the member bundle is fixed and
  proven
- store those exact-member and grouped preset choices directly in `streams`
  rather than splitting presets into a second durable table
- expose aliasable public device names
- surface channel, source-group, caps, and capture-policy metadata
- split depth choices into separate discoverable outputs when D2C changes the
  delivered caps
- surface grouped preset member metadata so `route_grouped` binds can fan out
  without hidden inference
- keep grouped-device behavior fixed per catalog entry in normal use until
  device-specific investigation justifies a richer public contract
- allow one discovered entry to map delivered caps to a different underlying
  native sensor profile through capture policy when the backend can prove it
- avoid synthesizing aligned-depth variants that the device-specific evidence
  does not support, such as `1280x720` on the tested Orbbec unit

### Session manager

- keep the current session graph behavior
- validate source compatibility against route expectations
- resolve exact-member metadata or grouped preset member metadata
- enforce channel constraints when declared
- preserve grouped-source relationships as internal metadata for discovery,
  inspection, and runtime orchestration
- expand grouped preset binds into matching declared routes under one grouped
  target
- resolve compatible grouped source requests onto one backend mode when
  possible, and reject conflicting grouped requests otherwise
- do not accept bind-time policy overrides that would change the meaning of an
  existing canonical URI
- create and stop ordinary sessions
- attach existing logical sessions to routes
- rebind routes at runtime
- preserve capture and delivery reuse semantics

### REST server

- expose discovery and alias APIs
- expose direct logical session APIs
- expose app CRUD
- expose route CRUD
- expose app-source connection, attach, rebind, and lifecycle control for both
  exact-route and grouped binds
- expose resolved exact stream identity and source-group metadata in app-source
  responses, plus grouped member bindings when a grouped preset URI is used
- expose runtime status for capture and delivery reuse inspection

## Frontend Responsibilities

- persistent app list and detail pages
- route declaration UI
- URI selection from the device catalog
- direct-session launch and status inspection
- source-to-route connection
- grouped-source inspection when devices expose multiple exact stream entries
- route rebind and attach-existing-session flows
- status and error display

The frontend remains a control-plane client. It does not own capture state.

## SDK Responsibilities

- declare routes
- describe route expectations
- connect source URIs with `route` or `route_grouped`
- attach existing `session_id` values to routes when needed
- attach returned stream identities through the existing low-level client
- present one callback chain per route to applications
