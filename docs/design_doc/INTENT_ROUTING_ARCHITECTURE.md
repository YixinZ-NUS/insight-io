# Intent Routing Architecture

## Summary

This note documents the architecture of the DB-first target-routing rebuild.
It does not replace the standalone plan or the technical report; it narrows
them onto the new app orchestration surface.

## Top-Level Structure

```text
frontend           create apps, declare targets, bind listed URIs, inspect status
REST API           durable app/target/source control plane
session manager    validates target contracts and creates ordinary sessions
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

The new durable app orchestration model is:

```text
apps
  app_targets
  app_sources -> logical_sessions -> session_bindings -> delivery_sessions
```

Meaning:

- `apps` are persistent application records
- `app_targets` declare intended processing uses
- `app_sources` bind listed URIs to one target
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

## Target Routing

Target routing is an app-layer concern above stream publication:

1. frontend or SDK declares targets on an app
2. frontend or SDK injects one URI with a `target`
3. backend normalizes the URI into `SessionRequest`
4. backend validates that the resolved source satisfies the target kind
5. backend returns target-to-stream role bindings
6. SDK attaches through the existing local stream attach contract

This keeps the data plane stable while changing the orchestration surface.

## Backend Responsibilities

### Database layer

- own SQL migrations
- load/save apps, targets, and sources
- normalize runtime-only state on startup

### Session manager

- keep the current session graph behavior
- validate source compatibility against target kind
- compute source role bindings
- create and stop ordinary sessions

### REST server

- expose app CRUD
- expose target CRUD
- expose source injection and lifecycle control
- expose role bindings in app-source responses

## Frontend Responsibilities

- persistent app list and detail pages
- target declaration UI
- URI selection from the device catalog
- source-to-target binding
- status and error display

The frontend remains a control-plane client. It does not own capture state.

## SDK Responsibilities

- declare targets
- inject sources with `target`
- map returned role bindings into low-level stream attaches
- present target-aware callbacks to applications

## Compatibility Direction

- low-level `Client` stays stream-based
- high-level `App` becomes route-based
- legacy `on_stream(...)` remains only as a temporary migration shim if kept at
  all
