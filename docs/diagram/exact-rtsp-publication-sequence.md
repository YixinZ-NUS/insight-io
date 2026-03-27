# Exact RTSP Publication Sequence

## Role

- role: Mermaid sequence for exact-source shared-runtime RTSP publication
- status: active
- version: 1
- major changes:
  - 2026-03-27 documents task-8 exact single-channel RTSP publication layered
    on top of the shared serving runtime and local IPC attach contract
- past tasks:
  - `2026-03-27 – Complete Task-7 IPC Hardening And Task-8 Exact RTSP Publication`

```mermaid
sequenceDiagram
    participant ClientA as Direct Client A
    participant ClientB as Direct Client B
    participant REST as REST API
    participant Sessions as SessionService
    participant Runtime as Serving Runtime
    participant Worker as Capture Worker
    participant RTSP as RTSP Publisher
    participant MediaMTX as mediamtx

    ClientA->>REST: POST /api/sessions {uri, rtsp_enabled:false}
    REST->>Sessions: create direct session
    Sessions->>Runtime: attach logical consumer
    Runtime-->>Sessions: runtime state = ready
    Sessions-->>ClientA: session active, runtime ready

    ClientB->>REST: POST /api/sessions {same uri, rtsp_enabled:true}
    REST->>Sessions: create direct session
    Sessions->>Runtime: attach second consumer with RTSP intent
    Runtime->>RTSP: start exact single-channel publisher
    RTSP->>MediaMTX: ANNOUNCE / publish rtsp://host:port/device/selector
    Runtime->>Worker: start capture if idle
    Worker-->>Runtime: frames
    Runtime-->>RTSP: forward frames
    Runtime-->>Sessions: runtime state = active, shared = true, rtsp = active
    Sessions-->>ClientB: session active, rtsp_url returned

    ClientB->>REST: POST /api/sessions/{id}/stop
    REST->>Sessions: stop RTSP-requesting consumer
    Sessions->>Runtime: detach logical consumer
    Runtime->>RTSP: stop publisher
    Runtime->>Worker: stop capture if no IPC consumers remain
    Runtime-->>Sessions: runtime state = ready, shared = false
```
