# Browser Route Builder Sequence

## Role

- role: document the repo-native browser flow for app, route, and source management
- status: active
- version: 1
- major changes:
  - 2026-03-27 added the verified browser sequence for catalog browse, app
    create, grouped or exact bind, source restart, and restart recovery

```mermaid
sequenceDiagram
    participant Browser as Browser UI
    participant REST as REST API
    participant Catalog as Catalog Service
    participant AppSvc as App Service
    participant Sess as Session Service

    Browser->>REST: GET /
    REST-->>Browser: index.html + /static/*

    Browser->>REST: GET /api/health
    Browser->>REST: GET /api/devices
    REST->>Catalog: list_devices()
    Catalog-->>Browser: exact + grouped URIs

    Browser->>REST: POST /api/apps
    REST->>AppSvc: create_app(...)
    Browser->>REST: POST /api/apps/{id}/routes
    REST->>AppSvc: create_route(...)

    Browser->>REST: POST /api/apps/{id}/sources
    REST->>AppSvc: create_source(...)
    AppSvc->>Sess: create/reuse runtime session
    Sess-->>REST: active session metadata
    REST-->>Browser: source row + active_session_id

    Browser->>REST: POST /api/apps/{id}/sources/{source_id}:stop
    Browser->>REST: POST /api/apps/{id}/sources/{source_id}:start

    Note over Browser,REST: Backend restart with same SQLite file
    Browser->>REST: GET /api/apps
    Browser->>REST: GET /api/apps/{id}/sources
    REST->>AppSvc: reload durable rows
    AppSvc-->>Browser: persisted source rows in stopped state
    Browser->>REST: POST /api/apps/{id}/sources/{source_id}:start
```
