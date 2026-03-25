# Intent Routing Runtime

## Role

- role: runtime/control-plane flow diagram for `insight-io`
- status: active
- version: 5
- major changes:
  - 2026-03-25 split runtime realization into capture and post-capture
    publication planning
  - 2026-03-25 replaced public grouped/exact bind selection with one
    app-local `target` surface
  - 2026-03-25 reframed RTSP as optional publication intent rather than a peer
    to implicit local IPC attach
  - 2026-03-25 removed `/channel/...` from the active public URI grammar
- past tasks:
  - `2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
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

    Sources --> Resolve[resolve uri or attached session<br/>plus optional rtsp_enabled]
    Streams --> Resolve
    Sessions --> Resolve

    Resolve --> Validate{target compatible?}
    Validate -->|no| Reject[last_error]
    Validate -->|yes| SessionMgr[create or reuse logical session]

    SessionMgr --> Sessions
    Sessions --> Logs[session_logs]
    SessionMgr --> Capture[shared capture workers]
    Capture --> Publish[post-capture publication phase<br/>profile + codec + protocol planning]
    Publish --> Runtime[publication runtime<br/>reuse + IPC/RTSP publishers]
    Runtime --> Status
    Runtime --> Attach[SDK local attach<br/>IPC only in v1]
    Runtime --> FutureRtsp[optional RTSP publication<br/>plus future remote or LAN consumers]
    Attach --> Callbacks[app callbacks]

    classDef durable fill:#eef7ef,stroke:#3d7a4a,color:#17351f;
    classDef runtime fill:#eef2ff,stroke:#4361aa,color:#16233f;
    class Devices,Streams,Apps,Routes,Sources,Sessions,Logs durable;
    class Discovery,Resolve,Validate,Reject,SessionMgr,Capture,Publish,Runtime,Attach,Callbacks,Status,FutureRtsp runtime;
```
