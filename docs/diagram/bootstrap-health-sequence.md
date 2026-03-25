# Bootstrap Health Sequence

## Role

- role: Mermaid sequence diagram for the first runtime-tested backend slice
- status: active
- version: 1
- major changes:
  - 2026-03-25 added the bootstrap startup and health-check interaction path
- past tasks:
  - `2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice`

```mermaid
sequenceDiagram
    participant Operator
    participant Backend as insightiod
    participant Store as SchemaStore
    participant SQLite
    participant API as RestServer

    Operator->>Backend: start with --db-path and --frontend
    Backend->>Store: initialize()
    Store->>SQLite: open database
    Store->>SQLite: apply 001_initial.sql
    Store-->>Backend: schema ready
    Backend->>API: start(host, port)
    API-->>Backend: listen
    Operator->>API: GET /api/health
    API-->>Operator: {status: ok, version, db_path, frontend_path}
```
