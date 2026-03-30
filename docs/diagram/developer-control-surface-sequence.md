# Developer Control Surface Sequence

## Role

- role: document the current browser/developer flow, with thin `/api/dev/*`
  for operator actions and canonical `/api/apps/{id}/routes` for route
  declaration
- status: active
- version: 2
- major changes:
  - 2026-03-30 updated the checked-in browser sequence so route declaration is
    shown on canonical `/api/apps/{id}/routes` while the thin `/api/dev/*`
    facade remains the recommended path for higher-level operator actions
  - 2026-03-27 added the verified developer sequence for catalog browse,
    alias update, direct-session startup, session-backed app injection, and
    runtime inspection through the minimal REST facade

```mermaid
sequenceDiagram
    participant Browser as Browser UI / Dev Client
    participant REST as /api/dev/* + /api/* routes
    participant Catalog as Catalog Service
    participant AppSvc as App Service
    participant Sess as Session Service

    Browser->>REST: GET /api/dev/health
    Browser->>REST: GET /api/dev/catalog
    REST->>Catalog: list_devices()
    Catalog-->>Browser: devices + streams + canonical URIs

    Browser->>REST: POST /api/dev/devices/{device}/alias
    Browser->>REST: POST /api/dev/streams/{stream_id}/alias
    REST->>Catalog: persist public aliases
    Catalog-->>Browser: updated canonical URIs

    Browser->>REST: POST /api/dev/sessions {input}
    REST->>Sess: create_direct_session(...)
    Sess-->>Browser: direct session + runtime facts

    Browser->>REST: POST /api/dev/apps
    Browser->>REST: POST /api/apps/{id}/routes
    Browser->>REST: POST /api/dev/apps/{id}/sources {session_id,target}
    REST->>AppSvc: create_source(...)
    AppSvc->>Sess: attach/reuse serving runtime
    Sess-->>Browser: source row + runtime view

    Browser->>REST: GET /api/dev/runtime
    REST->>Sess: runtime_status()
    Sess-->>Browser: sessions + serving_runtimes with current aliases
```
