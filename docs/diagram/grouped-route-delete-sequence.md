# Grouped Route Delete Sequence

## Role

- role: Mermaid sequence diagram for grouped bind cleanup when one member route
  is deleted
- status: active
- version: 1
- major changes:
  - 2026-03-26 added the first grouped-route-delete cleanup sequence after task
    5 closeout
- past tasks:
  - `2026-03-26 – Close Grouped Route Delete Cleanup And Refresh Runtime Handoff`

```mermaid
sequenceDiagram
    actor Client
    participant REST as REST API
    participant AppSvc as AppService
    participant SessSvc as SessionService
    participant DB as SQLite

    Client->>REST: DELETE /api/apps/{id}/routes/orbbec%2Fdepth
    REST->>AppSvc: delete_route(app_id, "orbbec/depth")
    AppSvc->>DB: Load route row
    AppSvc->>DB: Find grouped app_sources whose resolved_routes_json references route

    alt Grouped bind owns one app-created session
        AppSvc->>SessSvc: stop_session(active_session_id)
        SessSvc->>DB: UPDATE sessions state='stopped'
        AppSvc->>DB: DELETE impacted grouped app_sources
        AppSvc->>DB: DELETE linked app-owned grouped sessions
    else Grouped bind points at an existing direct session
        AppSvc->>DB: DELETE impacted grouped app_sources
        Note over AppSvc,DB: The direct session remains standalone
    end

    AppSvc->>DB: DELETE app_routes row
    DB-->>AppSvc: Commit transaction
    REST-->>Client: 204 No Content

    Client->>REST: GET /api/apps/{id}/sources
    REST-->>Client: grouped bind no longer returned

    Client->>REST: GET /api/sessions/{app_owned_session_id}
    REST-->>Client: 404 Not Found
```
