# App Route Source Sequence

## Role

- role: Mermaid sequence diagram for the current durable app/route/source REST slice
- status: active
- version: 1
- major changes:
  - 2026-03-26 added the first sequence for app create, route declaration,
    exact and session-backed source bind, grouped target resolution, and
    source stop/start/rebind lifecycle
- past tasks:
  - `2026-03-26 – Review App Route Source Persistence Slice And Reproduce Grouped Route Delete Bug`

```mermaid
sequenceDiagram
    actor Client
    participant REST as REST API
    participant AppSvc as AppService
    participant SessSvc as SessionService
    participant DB as SQLite

    Client->>REST: POST /api/apps {name}
    REST->>AppSvc: create_app(...)
    AppSvc->>DB: INSERT apps
    DB-->>AppSvc: app_id
    REST-->>Client: 201 app

    Client->>REST: POST /api/apps/{id}/routes {route_name, expect}
    REST->>AppSvc: create_route(...)
    AppSvc->>DB: INSERT app_routes
    DB-->>AppSvc: route_id
    REST-->>Client: 201 route

    alt URI-backed bind
        Client->>REST: POST /api/apps/{id}/sources {input, target}
        REST->>AppSvc: create_source(...)
        AppSvc->>DB: Resolve target + stream row
        AppSvc->>DB: INSERT sessions(session_kind='app')
        AppSvc->>DB: INSERT app_sources(active_session_id=app session)
        REST-->>Client: 201 source + active_session
    else Session-backed bind
        Client->>REST: POST /api/apps/{id}/sources {session_id, target}
        REST->>AppSvc: create_source(...)
        AppSvc->>SessSvc: get_session(session_id)
        SessSvc-->>AppSvc: direct session
        AppSvc->>DB: Validate exact route or grouped members
        AppSvc->>DB: INSERT app_sources(source_session_id, active_session_id)
        REST-->>Client: 201 source + source_session
    end

    alt Grouped target bind
        AppSvc->>DB: Load routes under target root
        AppSvc->>DB: Persist resolved_members_json
    end

    Client->>REST: POST /api/apps/{id}/sources/{source_id}/stop
    REST->>AppSvc: stop_source(...)
    AppSvc->>SessSvc: stop_session(app-owned only)
    AppSvc->>DB: UPDATE app_sources state='stopped'
    REST-->>Client: 200 source

    Client->>REST: POST /api/apps/{id}/sources/{source_id}/start
    REST->>AppSvc: start_source(...)
    AppSvc->>DB: INSERT replacement app session or reactivate source_session_id
    AppSvc->>DB: UPDATE app_sources active_session_id + state
    REST-->>Client: 200 source

    Client->>REST: POST /api/apps/{id}/sources/{source_id}/rebind {input|session_id}
    REST->>AppSvc: rebind_source(...)
    AppSvc->>DB: Resolve replacement source
    AppSvc->>DB: INSERT replacement app session when URI-backed
    AppSvc->>DB: UPDATE app_sources stream/session pointers
    AppSvc->>SessSvc: stop replaced app-owned session
    REST-->>Client: 200 source
```
