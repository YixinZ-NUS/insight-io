# Intent Routing Runtime

```mermaid
flowchart LR
    UI[Frontend or SDK] --> Apps[apps]
    UI --> Targets[app_targets]
    UI --> Sources[app_sources]

    Sources --> Normalize[normalize_source_input]
    Normalize --> Request[SessionRequest]
    Request --> Validate{target kind compatible?}
    Validate -->|no| Reject[last_error + stopped]
    Validate -->|yes| Create[create ordinary logical session]

    Create --> Logical[logical_sessions]
    Logical --> Bindings[session_bindings]
    Bindings --> Delivery[delivery_sessions]
    Delivery --> Capture[capture_sessions]
    Capture --> Runs[daemon_runs]

    Create --> Roles[role bindings<br/>primary color depth ir audio]
    Roles --> Attach[SDK local attach<br/>session_id + stream_name]

    classDef durable fill:#eef7ef,stroke:#3d7a4a,color:#17351f;
    classDef runtime fill:#eef2ff,stroke:#4361aa,color:#16233f;
    class Apps,Targets,Sources,Logical,Bindings,Delivery,Capture,Runs durable;
    class Normalize,Request,Roles,Attach runtime;
```
