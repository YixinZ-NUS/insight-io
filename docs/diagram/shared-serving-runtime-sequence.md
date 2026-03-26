# Shared Serving Runtime Sequence

## Role

- role: Mermaid sequence diagram for the checked-in task-6 serving-runtime
  reuse slice
- status: active
- version: 1
- major changes:
  - 2026-03-26 added a shared exact-URI runtime sequence showing direct
    session reuse, one app-owned consumer attach, additive RTSP intent, and
    `GET /api/status` serving-runtime inspection
- past tasks:
  - `2026-03-26 – Add Serving Runtime Reuse And Runtime-Status Topology`

```mermaid
sequenceDiagram
    autonumber
    actor Operator
    participant REST as RestServer
    participant Session as SessionService
    participant App as AppService
    participant Runtime as ServingRuntimeRegistry
    participant DB as SQLite

    Operator->>REST: POST /api/sessions {input, rtsp_enabled:false}
    REST->>Session: create_direct_session(input, false)
    Session->>DB: INSERT direct session row
    Session->>Runtime: attach session_id=1 to stream:17
    Runtime-->>Session: runtime_key=stream:17, consumer_count=1
    Session-->>REST: SessionRecord + serving_runtime
    REST-->>Operator: 201 Created

    Operator->>REST: POST /api/sessions {same input, rtsp_enabled:true}
    REST->>Session: create_direct_session(input, true)
    Session->>DB: INSERT second direct session row
    Session->>Runtime: attach session_id=2 to stream:17
    Note over Runtime: effective RTSP intent becomes true because one active consumer requested it
    Runtime-->>Session: runtime_key=stream:17, consumer_count=2, shared=true
    Session-->>REST: SessionRecord + serving_runtime
    REST-->>Operator: 201 Created

    Operator->>REST: POST /api/apps/{id}/sources {same input, target:yolov5}
    REST->>App: create_source(app_id, input, target)
    App->>DB: INSERT app-owned session row + app_sources row
    App->>Session: attach_session_runtime(app session)
    Session->>Runtime: attach session_id=3 to stream:17
    Runtime-->>Session: runtime_key=stream:17, consumer_count=3, shared=true
    App-->>REST: AppSourceRecord + nested active_session.serving_runtime
    REST-->>Operator: 201 Created

    Operator->>REST: GET /api/status
    REST->>Session: runtime_status()
    Session->>DB: SELECT logical sessions
    Session->>Runtime: snapshot()
    Runtime-->>Session: serving_runtimes[stream:17] with owner, consumers, RTSP intent
    Session-->>REST: sessions + serving_runtimes
    REST-->>Operator: shared serving-runtime topology
```
