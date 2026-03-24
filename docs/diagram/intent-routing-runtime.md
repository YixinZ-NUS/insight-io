# Intent Routing Runtime

```mermaid
flowchart LR
    Discovery[device discovery] --> Devices[devices]
    Devices --> Streams[streams]

    UI[Frontend or SDK] --> Apps[apps]
    UI --> Routes[app_routes]
    UI --> Sources[app_sources]
    UI --> Sessions[sessions]
    UI --> Status[/api/status]

    Sources --> Resolve[resolve stream or attached session]
    Streams --> Resolve
    Sessions --> Resolve

    Resolve --> Validate{route or route_grouped compatible?}
    Validate -->|no| Reject[last_error]
    Validate -->|yes| SessionMgr[create attach or reuse logical session]

    SessionMgr --> Sessions
    Sessions --> Logs[session_logs]
    SessionMgr --> Runtime[runtime workers<br/>capture + delivery + reuse]
    Runtime --> Status
    Runtime --> Attach[SDK local attach<br/>session_id + stream_name]
    Attach --> Callbacks[app callbacks]

    classDef durable fill:#eef7ef,stroke:#3d7a4a,color:#17351f;
    classDef runtime fill:#eef2ff,stroke:#4361aa,color:#16233f;
    class Devices,Streams,Apps,Routes,Sources,Sessions,Logs durable;
    class Discovery,Resolve,Validate,Reject,SessionMgr,Runtime,Attach,Callbacks,Status runtime;
```
