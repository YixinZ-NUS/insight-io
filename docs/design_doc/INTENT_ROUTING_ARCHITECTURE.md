# Intent Routing Architecture

## Summary

This note documents the architecture of the DB-first route-based rebuild.

One main objective is to mask heterogeneous hardware details from users,
including LLM-assisted app builders, by making discovery publish normalized
exact stream choices instead of forcing clients to reason about per-device
quirks.

The public app model is:

- apps declare named routes
- each route describes purpose and optional semantic expectations
- one canonical URI maps to one delivered stream
- the discovery catalog exposes exact stream choices up front
- grouped devices expose related source identities through catalog metadata
- the backend validates route expectations against resolved source metadata
- grouped-device runtime behavior remains fixed per discovered catalog entry in
  normal use

## Top-Level Structure

```text
device discovery   enumerate exact stream catalog entries and aliasable devices
frontend / SDK     create apps, declare routes, connect exact URIs, inspect status
REST API           durable app/route/source control plane + direct session APIs
session manager    validates route expectations, attaches sessions, and creates ordinary sessions
session graph      logical_sessions -> session_bindings -> delivery_sessions -> capture_sessions
data plane         Unix socket + memfd/ring-buffer for local, RTSP for remote
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
apps
  app_routes
  app_sources

device catalog
  public devices
  exact stream entries
  exact canonical URIs
```

Meaning:

- `apps` are persistent application records
- `app_routes` declare intended processing uses and semantic expectations
- `app_sources` bind routes either to exact URIs or to existing logical sessions
- each route owns at most one active binding at a time
- multiple routes may reuse the same canonical URI across one or more apps

### Runtime execution

The runtime media graph is unchanged:

```text
logical_sessions
  session_bindings
    delivery_sessions
      capture_sessions
        daemon_runs
```

The new app tables do not replace the media graph. They declare durable intent
above it.

## Discovery And Exact Stream Catalog

Discovery is now responsible for surfacing exact user choices.

The catalog must publish one entry per delivered stream choice with metadata
such as:

- canonical URI
- exact stream id
- source variant id
- source group id
- member kind
- channel when applicable
- delivered caps
- capture policy requirements
- supported delivery suffixes

Examples:

- `color-480p_30`
- `depth-400p_30`
- `depth-480p_30`
- `video-720p_30/channel/left`
- `video-720p_30/channel/right`

If backend processing changes the delivered caps, discovery must split that into
separate catalog entries instead of asking the route layer to infer it later.

For RGBD depth this means:

- `depth-400p_30` is the native depth output
- `depth-480p_30` is the aligned depth output

The user chooses between them at discovery time.

## Route Resolution Model

Route resolution is an app-layer concern above stream publication:

1. frontend or SDK declares routes on an app
2. frontend or SDK connects one URI with a `route`
3. backend normalizes the URI into `SessionRequest`
4. backend resolves one concrete exact stream identity from that URI
5. backend reads route expectations from `expect_json`
6. backend validates:
   - media kind
   - channel constraints
7. backend creates one logical session
8. SDK attaches through the existing local stream attach contract

Important boundary:

- the route does not choose hidden variants
- the URI and catalog already determine the exact stream
- route validation may reject an incompatible URI, but it does not rewrite it

This keeps the data plane stable while changing the orchestration surface.

## Source Groups

Grouped devices are the important special case:

- RGBD color + depth
- stereo left + right

The recommended approach is:

- discovery publishes separate exact stream entries
- the catalog marks related entries with `source_group_id`
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
- the tested Orbbec device produced aligned `640x480` depth from a depth-only
  request, so the key remaining backend task is capture-plan encoding rather
  than proving the public one-stream contract
- the same device exposed no compatible `1280x720` D2C depth path and no
  distinct aligned `1280x800` output, so discovery should not publish those as
  aligned-depth variants on that unit
- if special-case explanation helps operators, discovery may surface a short
  human-readable comment for an entry such as `depth-480p_30`; that comment is
  informative only and does not replace the exact URI contract

## Grouped Runtime Rule

When multiple exact URIs from the same source group are active:

- the session manager must try to place them onto one compatible grouped
  backend mode
- if that is not possible without changing what an existing canonical URI means,
  the backend must reject the newer request
- bind-time overrides are out of scope for normal use; selecting a different
  discovered URI is the supported way to ask for different runtime behavior

## Reuse, Fan-Out, And Reroute

The runtime must preserve donor-style reuse semantics:

- identical canonical URIs may share capture and delivery runtime
- consumers attached to the same exact canonical URI should observe the same
  frame sequence when the shared delivery path is reused
- URIs that differ only by delivery suffix, such as `/mjpeg` versus `/rtsp`,
  may share capture while keeping distinct delivery sessions

The app layer must also support:

- app-first binding: create app, then connect exact URI
- session-first binding: create direct session, then attach `session_id` to a
  route
- runtime rebind: replace one route’s current binding without destroying the
  durable app record

## Backend Responsibilities

### Database layer

- own SQL migrations
- load/save apps, routes, and sources
- normalize runtime-only state on startup

### Discovery layer

- publish exact stream catalog entries
- expose aliasable public device names
- surface channel, source-group, caps, and capture-policy metadata
- split depth choices into separate discoverable outputs when D2C changes the
  delivered caps
- keep grouped-device behavior fixed per catalog entry in normal use until
  device-specific investigation justifies a richer public contract
- allow one discovered entry to map delivered caps to a different underlying
  native sensor profile through capture policy when the backend can prove it
- avoid synthesizing aligned-depth variants that the device-specific evidence
  does not support, such as `1280x720` on the tested Orbbec unit

### Session manager

- keep the current session graph behavior
- validate source compatibility against route expectations
- resolve exact stream metadata
- enforce channel constraints when declared
- preserve grouped-source relationships as internal metadata for discovery,
  inspection, and runtime orchestration
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
- expose app-source connection, attach, rebind, and lifecycle control
- expose resolved exact stream identity and source-group metadata in app-source
  responses
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
- connect exact URIs with `route`
- provide SDK-only `join()` / `pair()` helpers above named routes for combined
  processing
- attach existing `session_id` values to routes when needed
- attach returned stream identities through the existing low-level client
- present one callback chain per route to applications
