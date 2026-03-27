# IPC Idle Teardown Sequence

## Role

- role: Mermaid sequence for exact-source IPC attach, idle disconnect, and
  worker release
- status: active
- version: 1
- major changes:
  - 2026-03-27 documents task-7 idle-worker teardown after the last IPC
    consumer disconnects while the logical session remains active
- past tasks:
  - `2026-03-27 – Complete Task-7 IPC Hardening And Task-8 Exact RTSP Publication`

```mermaid
sequenceDiagram
    participant Client as IPC Probe / SDK
    participant REST as REST API
    participant Sessions as SessionService
    participant Runtime as Serving Runtime
    participant Worker as Capture Worker
    participant Ring as memfd Ring

    Client->>REST: POST /api/sessions {uri, rtsp_enabled:false}
    REST->>Sessions: create direct session
    Sessions->>Runtime: attach logical consumer
    Runtime-->>Sessions: runtime state = ready

    Client->>Runtime: connect unix control socket
    Runtime->>Worker: start capture worker
    Worker-->>Ring: write first frame with caps-change flag
    Runtime-->>Client: lease memfd + eventfd
    Client->>Ring: read frame

    Client-->>Runtime: close control socket
    Runtime->>Runtime: detach IPC consumer
    Runtime->>Worker: stop worker when no IPC or RTSP consumers remain
    Runtime->>Ring: reset counters and first-frame state
    Runtime-->>Sessions: runtime state = ready

    Client->>Runtime: reconnect later
    Runtime->>Worker: restart capture worker
    Worker-->>Ring: publish fresh first frame
```
