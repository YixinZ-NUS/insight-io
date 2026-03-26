# Direct Session Sequence

## Role

- role: Mermaid sequence diagram for the checked-in direct-session REST slice
- status: active
- version: 1
- major changes:
  - 2026-03-26 added a create, status, restart, and delete sequence for the
    direct-session slice now served by `insightiod`
- past tasks:
  - `2026-03-26 – Reintroduce Direct Session REST And Status Slice`

```mermaid
sequenceDiagram
    autonumber
    actor Operator
    participant REST as RestServer
    participant Session as SessionService
    participant DB as SQLite

    Operator->>REST: POST /api/sessions {input, rtsp_enabled}
    REST->>Session: create_direct_session(input, rtsp_enabled)
    Session->>DB: SELECT streams + devices by public_name and selector
    DB-->>Session: resolved source row
    Session->>DB: INSERT sessions(... state='active' ...)
    Session->>DB: INSERT session_logs(create)
    Session-->>REST: SessionRecord(active)
    REST-->>Operator: 201 Created + resolved source metadata

    Operator->>REST: GET /api/sessions and GET /api/status
    REST->>Session: list_sessions() / runtime_status()
    Session->>DB: SELECT sessions joined to streams/devices
    DB-->>Session: hydrated session rows
    Session-->>REST: session list + counters
    REST-->>Operator: persisted session view

    Operator->>REST: POST /api/sessions/{id}/stop
    REST->>Session: stop_session(id)
    Session->>DB: UPDATE sessions state='stopped', stopped_at_ms=now
    Session->>DB: INSERT session_logs(stop)
    Session-->>REST: SessionRecord(stopped)
    REST-->>Operator: 200 OK

    Note over Session,DB: On backend restart, initialize() normalizes any leftover active rows to stopped.

    Operator->>REST: POST /api/sessions/{id}/start
    REST->>Session: start_session(id)
    Session->>DB: UPDATE sessions state='active', started_at_ms=now
    Session->>DB: INSERT session_logs(start)
    Session-->>REST: SessionRecord(active)
    REST-->>Operator: 200 OK

    Operator->>REST: DELETE /api/sessions/{id}
    REST->>Session: delete_session(id)
    Session->>DB: Check app_sources references
    alt referenced by app_sources
        DB-->>Session: source_session_id/active_session_id match found
        Session-->>REST: conflict
        REST-->>Operator: 409 Conflict
    else unreferenced
        Session->>DB: DELETE FROM sessions WHERE session_id = ?
        Session-->>REST: deleted
        REST-->>Operator: 204 No Content
    end
```
