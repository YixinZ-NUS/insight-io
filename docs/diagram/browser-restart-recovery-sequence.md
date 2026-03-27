# Browser Restart Recovery Sequence

## Role

- role: Mermaid sequence diagram for the repo-native browser restart-recovery flow
- status: active
- version: 1
- major changes:
  - 2026-03-27 split the restart-specific browser recovery path out of the
    broader route-builder diagram to close the Mermaid backlog item

```mermaid
sequenceDiagram
    participant Browser as Browser UI
    participant REST as REST API
    participant AppSvc as App Service
    participant Sess as Session Service

    Note over Browser,REST: Backend restart with same SQLite file
    Browser->>REST: GET /api/apps
    REST->>AppSvc: list_apps()
    REST-->>Browser: persisted apps

    Browser->>REST: GET /api/apps/{id}/sources
    REST->>AppSvc: list_sources(...)
    AppSvc-->>Browser: durable source rows in stopped state

    Browser->>REST: POST /api/apps/{id}/sources/{source_id}:start
    REST->>AppSvc: start_source(...)
    AppSvc->>Sess: create or reuse fresh runtime session
    Sess-->>REST: active session metadata
    REST-->>Browser: updated source row + active_session_id

    Browser->>REST: GET /api/status
    REST-->>Browser: fresh runtime state
```
