# SDK Idle Bind Sequence

## Role

- role: document the checked-in task-9 SDK idle-app plus later REST bind flow
- status: active
- version: 1
- major changes:
  - 2026-03-27 added the verified sequence for route declaration, idle startup,
    later REST bind, IPC attach, and route callback delivery

```mermaid
sequenceDiagram
    participant App as SDK App
    participant REST as REST API
    participant AppSvc as App Service
    participant Sess as Session Service
    participant IPC as IPC Runtime

    App->>REST: POST /api/apps
    REST->>AppSvc: create_app(...)
    App->>REST: POST /api/apps/{id}/routes
    REST->>AppSvc: create_route(...)
    App->>REST: GET /api/apps/{id}/sources
    REST->>AppSvc: list_sources(...)
    Note over App: App is idle. No source bind exists yet.

    REST->>REST: external client posts later bind
    REST->>AppSvc: create_source(input or session_id, target)
    AppSvc->>Sess: create/reuse serving session
    Sess->>IPC: realize runtime and publish IPC channel(s)
    AppSvc-->>REST: durable source row + active_session

    App->>REST: poll GET /api/apps/{id}/sources
    REST->>AppSvc: list_sources(...)
    App->>IPC: attach using active_session_id
    IPC-->>App: memfd + eventfd
    App-->>App: deliver on_caps / on_frame / on_stop per route
```
