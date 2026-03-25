# Intent Routing Architecture

## Role

- role: control-plane and runtime-responsibility split for `insight-io`
- status: active
- version: 5
- major changes:
  - 2026-03-25 clarified that direct sessions stay standalone until one app
    bind becomes active and that route names describe app-local input roles
  - 2026-03-25 replaced public grouped and exact bind distinctions with one
    app-local `target` surface and explicit ambiguity guardrails
  - 2026-03-25 reframed RTSP as optional publication intent rather than a peer
    to implicit local IPC attach
  - 2026-03-25 removed `/channel/...` from the active public URI grammar
- past tasks:
  - `2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

## Summary

This note documents the architecture of the DB-first route-based rebuild.

One main objective is to mask heterogeneous hardware details from users,
including LLM-assisted app builders, by making discovery publish normalized
source choices instead of forcing clients to reason about per-device
quirks.

The public app model is:

- apps declare named routes
- those route names are app-local logical input roles rather than device-global
  identifiers
- each route describes purpose and optional semantic expectations
- one derived `uri` selects one fixed catalog-published source shape
- the discovery catalog exposes exact member choices up front and may also
  expose grouped preset choices when the member bundle is fixed and proven
- grouped devices expose related source identities through catalog metadata
- discovery publishes selectable source shapes and metadata; sessions own
  runtime realization, reuse, and lifecycle
- RTSP publication intent is durable bind/session state rather than part of
  source identity
- the backend validates route expectations against resolved source metadata
- grouped-device runtime behavior remains fixed per discovered catalog entry in
  normal use
- app-source binds use one app-local `target` field; the backend resolves
  whether that target is one exact route or one grouped target root
- local SDK attach uses IPC in v1 and does not post `ipc` as a transport field;
  future remote or LAN RTSP consumption is a separate consumer path

## Top-Level Structure

```text
device discovery   enumerate exact-member and grouped-preset catalog entries, publish source-shape metadata
frontend / SDK     create apps, declare routes, bind sources, inspect status
REST API           durable app/route/source control plane + direct session APIs
durable schema     devices -> streams, apps -> app_routes -> app_sources, sessions -> session_logs
session manager    validates route expectations, expands grouped presets, and realizes catalog choices as logical sessions
runtime workers    capture, publication, RTSP, IPC attach, and reuse planning
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
  existing logical sessions through one durable surface
- `sessions` store logical session history
- `session_logs` store append-only audit events
- each route or grouped target owns at most one active binding at a time
- multiple apps may reuse the same URI across one or more sessions when the
  publication requirements are also compatible

### Runtime execution

The runtime worker graph still exists, but it does not need first-class durable
tables in v1:

```text
sessions
  worker planning
    capture reuse
    publication reuse
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

- derived URI
- source shape
- exact stream id
- source variant id
- source group id
- member kind
- channel when applicable
- delivered caps
- capture policy requirements
- supported delivery names
- grouped preset members when applicable

Examples:

- `orbbec/color/480p_30`
- `orbbec/depth/400p_30`
- `orbbec/depth/480p_30`
- `orbbec/preset/480p_30`

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
2. frontend or SDK creates one app-source bind with either:
   - `input + target` for one URI-backed bind
   - `session_id + target` for one session-backed bind
3. backend normalizes the selected source plus optional `rtsp_enabled` into
   `SessionRequest`
4. backend resolves either one concrete exact stream identity or one concrete
   preset member set from that selection
5. backend reads route expectations from `expect_json`
6. backend validates:
   - media kind
   - grouped preset member-to-route matches when the selected `target` resolves
     to one grouped target root
7. backend creates one logical session or one grouped logical session
8. SDK attaches through the existing IPC contract

Important boundary:

- declaring routes alone does not consume a matching URI or direct session
- an app starts receiving frames only when one app-source bind becomes active
- the route does not choose hidden variants
- the URI and catalog already determine the exact member or grouped preset
- route validation may reject an incompatible URI, but it does not rewrite it
- v1 app binds stay IPC-oriented; RTSP publication is additive state on the
  selected session rather than part of the selected source URI
- this is why `source_session_id` and `active_session_id` are both durable:
  one preserves the selected upstream session and the other records the serving
  session materialized for the app

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
- if that is not possible without changing what an existing URI means,
  the backend must reject the newer request
- bind-time overrides are out of scope for normal use; selecting a different
  discovered URI is the supported way to ask for different runtime behavior

## Reuse, Fan-Out, And Reroute

The runtime must preserve donor-style reuse semantics:

- identical URI plus the same publication requirements may share capture and
  serving runtime
- consumers attached to the same URI and publication intent should observe the
  same frame sequence when the shared serving path is reused
- a serving runtime may expose RTSP as an additive publication when one or more
  active consumers request it
- current scope is therefore one-way across namespaces: local `insightos://`
  selections may publish toward RTSP, but raw `rtsp://` inputs still need an
  explicit ingest/import path before they become cataloged `insightos://`
  sources

The app layer must also support:

- app-first binding: create app, then bind exact URI
- session-first binding: create direct session, then bind `session_id` to a
  route or grouped target through the same app-source surface
- direct sessions remain standalone until that later bind exists
- runtime rebind: replace one route or grouped target binding without
  destroying the durable app record

## Backend Responsibilities

### Database layer

- own the checked-in SQL schema
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
- surface grouped preset member metadata so grouped target binds can fan out
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
- preserve grouped-source relationships as internal metadata for discovery,
  inspection, and runtime orchestration
- expand grouped preset binds into matching declared routes under one grouped
  target
- expand grouped session-backed attaches into matching declared routes under one
  grouped target
- resolve compatible grouped source requests onto one backend mode when
  possible, and reject conflicting grouped requests otherwise
- do not accept bind-time policy overrides that would change the meaning of an
  existing URI
- create and stop ordinary sessions
- bind existing logical sessions to routes or grouped targets
- rebind routes at runtime
- preserve capture and publication reuse semantics

### REST server

- expose discovery and alias APIs
- expose direct logical session APIs
- expose app CRUD
- expose route CRUD
- expose app-source create, rebind, and lifecycle control for both exact-route
  and grouped binds, with `session_id` handled through the same create surface
- expose resolved exact stream identity and source-group metadata in app-source
  responses, plus grouped member bindings when a grouped preset URI is used
- expose runtime status for capture and publication reuse inspection

## Frontend Responsibilities

- persistent app list and detail pages
- route declaration UI
- URI selection from the device catalog
- direct-session launch and status inspection
- source-to-route connection or session-backed bind through the same
  app-source surface
- grouped-source inspection when devices expose multiple exact stream entries
- route rebind and bind-existing-session flows
- status and error display

The frontend remains a control-plane client. It does not own capture state.

## SDK Responsibilities

- declare routes
- describe route expectations
- bind sources with `target`
- attach existing `session_id` values to targets when needed
- attach returned stream identities through the existing IPC client
- keep local attach IPC-only in v1 even when future remote or LAN RTSP
  consumption is added as a separate path
- present one callback chain per route to applications
