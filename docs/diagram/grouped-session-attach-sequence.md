# Grouped Session Attach Sequence

## Role

- role: Mermaid sequence diagram for grouped `session_id` attach through one grouped target root
- status: active
- version: 1
- major changes:
  - 2026-03-27 added the grouped-session attach sequence that was previously
    listed in the Mermaid backlog and is now part of the checked-in task-9
    explanation set

```mermaid
sequenceDiagram
    participant Op as Operator or SDK
    participant REST as REST API
    participant Sess as Session Service
    participant AppSvc as App Service
    participant App as Running App
    participant IPC as IPC Runtime

    Op->>REST: POST /api/sessions {input grouped preset URI}
    REST->>Sess: create_direct_session(...)
    Sess->>IPC: realize grouped serving runtime
    REST-->>Op: grouped session_id

    Op->>REST: POST /api/apps
    Op->>REST: POST /api/apps/{id}/routes {route_name: orbbec/color}
    Op->>REST: POST /api/apps/{id}/routes {route_name: orbbec/depth}
    Op->>REST: POST /api/apps/{id}/sources {session_id, target: orbbec}
    REST->>AppSvc: create_source(...)
    AppSvc->>Sess: attach existing grouped session_id
    Sess-->>AppSvc: validate resolved_members_json
    AppSvc-->>REST: grouped source row + active_session metadata

    App->>REST: GET /api/apps/{id}/sources
    REST-->>App: grouped active_session + ipc_channels
    App->>IPC: attach color and depth channels
    IPC-->>App: route callbacks on orbbec/color and orbbec/depth
```
