# Exact Session Attach Sequence

## Role

- role: Mermaid sequence diagram for exact `session_id` attach on the checked-in app-source surface
- status: active
- version: 1
- major changes:
  - 2026-03-27 added the exact-session attach sequence that was previously
    listed in the Mermaid backlog and is now part of the current
    implementation notes

```mermaid
sequenceDiagram
    participant Op as Operator or SDK
    participant REST as REST API
    participant Sess as Session Service
    participant AppSvc as App Service
    participant App as Running App
    participant IPC as IPC Runtime

    Op->>REST: POST /api/sessions {input exact URI}
    REST->>Sess: create_direct_session(...)
    Sess->>IPC: realize exact serving runtime
    REST-->>Op: direct session_id

    Op->>REST: POST /api/apps
    Op->>REST: POST /api/apps/{id}/routes {route_name: camera}
    Op->>REST: POST /api/apps/{id}/sources {session_id, target: camera}
    REST->>AppSvc: create_source(...)
    AppSvc->>Sess: attach existing session_id
    Sess-->>AppSvc: validate stream + reuse serving runtime
    AppSvc-->>REST: source row + active_session metadata

    App->>REST: GET /api/apps/{id}/sources
    REST-->>App: active_session + ipc_socket_path
    App->>IPC: attach exact channel
    IPC-->>App: caps + frames on route callback
```
