# Discovery Runtime Boundary Sequence

## Role

- role: Mermaid sequence diagram for the catalog-versus-runtime responsibility boundary
- status: active
- version: 1
- major changes:
  - 2026-03-27 added a dedicated discovery-versus-runtime sequence to make the
    no-runtime-at-discovery boundary explicit for onboarding and review

```mermaid
sequenceDiagram
    participant Client as Browser or SDK
    participant REST as REST API
    participant Catalog as Catalog Service
    participant Store as SQLite Catalog Tables
    participant AppSvc as App Service
    participant Sess as Session Service
    participant Runtime as Capture and Publication Runtime

    Client->>REST: POST /api/devices:refresh
    REST->>Catalog: refresh()
    Catalog->>Store: upsert devices + streams
    Catalog-->>REST: catalog rows only
    Note over Catalog,Runtime: Discovery publishes selectors and metadata only. No serving runtime is started here.

    Client->>REST: GET /api/devices
    REST->>Catalog: list_devices()
    Catalog->>Store: read published selectors
    REST-->>Client: exact and grouped URIs

    Client->>REST: POST /api/sessions or /api/apps/{id}/sources
    REST->>AppSvc: validate target and expectations
    AppSvc->>Sess: create or attach logical session
    Sess->>Runtime: realize capture + IPC/RTSP runtime
    Runtime-->>REST: active session + serving runtime facts
    REST-->>Client: runtime-backed response
```
