# Intent Routing Runtime

## Role

- role: runtime/control-plane flow diagram for `insight-io`
- status: active
- version: 3
- major changes:
  - 2026-03-24 clarified delivery is inferred during normalization before it is
    stored durably
  - 2026-03-24 updated the flow to show one app-source surface for URI-backed
    and session-backed binds
  - 2026-03-24 clarified local SDK attach is IPC-only in v1 and future remote
    or LAN RTSP consumption remains separate
- past tasks:
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`

```mermaid
flowchart LR
    Discovery[device discovery] --> Devices[devices]
    Devices --> Streams[streams]

    UI[Frontend or SDK] --> Apps[apps]
    UI --> Routes[app_routes]
    UI --> Sources[app_sources]
    UI --> Sessions[sessions]
    UI --> Status[/api/status]

    Sources --> Resolve[resolve uri or attached session<br/>plus inferred delivery_name]
    Streams --> Resolve
    Sessions --> Resolve

    Resolve --> Validate{route or route_grouped compatible?}
    Validate -->|no| Reject[last_error]
    Validate -->|yes| SessionMgr[create or reuse logical session]

    SessionMgr --> Sessions
    Sessions --> Logs[session_logs]
    SessionMgr --> Runtime[runtime workers<br/>capture + delivery + reuse]
    Runtime --> Status
    Runtime --> Attach[SDK local attach<br/>IPC only in v1]
    Runtime --> FutureRtsp[future remote or LAN RTSP consumers]
    Attach --> Callbacks[app callbacks]

    classDef durable fill:#eef7ef,stroke:#3d7a4a,color:#17351f;
    classDef runtime fill:#eef2ff,stroke:#4361aa,color:#16233f;
    class Devices,Streams,Apps,Routes,Sources,Sessions,Logs durable;
    class Discovery,Resolve,Validate,Reject,SessionMgr,Runtime,Attach,Callbacks,Status,FutureRtsp runtime;
```
