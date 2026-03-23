# Intent Routing Architecture

## Summary

This note documents the architecture of the DB-first route-based rebuild.

The public app model is:

- apps declare named routes
- each route describes purpose and optional semantic expectations
- one source URI connects to one route
- grouped devices expose related source identities through catalog metadata
- the backend validates route expectations against resolved source metadata

## Top-Level Structure

```text
frontend           create apps, declare routes, connect listed URIs, inspect status
REST API           durable app/route/source control plane
session manager    validates route expectations and creates ordinary sessions
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

## Orchestration Model

### Durable control plane

```text
apps
  app_routes
  app_sources -> logical_sessions -> session_bindings -> delivery_sessions
```

Meaning:

- `apps` are persistent application records
- `app_routes` declare intended processing uses and semantic expectations
- `app_sources` connect listed URIs to one route
- each source still creates at most one ordinary logical session at a time

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

## Route Resolution Model

Route resolution is an app-layer concern above stream publication:

1. frontend or SDK declares routes on an app
2. frontend or SDK connects one URI with a `route`
3. backend normalizes the URI into `SessionRequest`
4. backend resolves one concrete source identity from that URI
5. backend reads route expectations from `expect_json`
6. backend validates:
   - media kind
   - channel constraints
   - same-group constraints
   - alignment constraints
7. backend creates one logical session
8. SDK attaches through the existing local stream attach contract

This keeps the data plane stable while changing the orchestration surface.

## Source Groups

Grouped devices are the important special case:

- RGBD color + depth
- stereo left + right

The URI should stay readable. The recommended approach is:

- keep `<device>/<preset>` in the path
- use `?source=<member>` when the device/preset exposes multiple members
- publish `source_group_id` and `source_member` in the catalog and source
  records

This is better than adding a visible `<group>` path layer because:

- the device alias stays readable
- the preset stays readable
- the catalog can still group related entries visually
- grouped-source logic stays in metadata instead of leaking into every path

## Backend Responsibilities

### Database layer

- own SQL migrations
- load/save apps, routes, and sources
- normalize runtime-only state on startup

### Session manager

- keep the current session graph behavior
- validate source compatibility against route expectations
- resolve grouped-source metadata
- enforce same-group and alignment constraints when declared
- create and stop ordinary sessions

### REST server

- expose app CRUD
- expose route CRUD
- expose source connection and lifecycle control
- expose resolved source identity and source-group metadata in app-source
  responses

## Frontend Responsibilities

- persistent app list and detail pages
- route declaration UI
- URI selection from the device catalog
- source-to-route connection
- grouped-source inspection when devices expose multiple members
- status and error display

The frontend remains a control-plane client. It does not own capture state.

## SDK Responsibilities

- declare routes
- describe route expectations
- connect sources with `route`
- attach returned stream identities through the existing low-level client
- present one callback chain per route to applications
