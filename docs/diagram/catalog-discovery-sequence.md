# Catalog Discovery Sequence

## Role

- role: Mermaid sequence diagram for persisted discovery refresh and catalog reads
- status: active
- version: 1
- major changes:
  - 2026-03-26 added the discovery refresh, catalog list, and alias update flow
- past tasks:
  - `2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow`

```mermaid
sequenceDiagram
    participant Operator
    participant Backend as insightiod
    participant Discovery as discovery backends
    participant Catalog as CatalogService
    participant SQLite
    participant API as RestServer

    Backend->>Catalog: initialize()
    Catalog->>Discovery: discover_all()
    Discovery-->>Catalog: V4L2 + Orbbec + PipeWire devices
    Catalog->>SQLite: upsert devices and streams
    Catalog-->>Backend: in-memory catalog ready

    Operator->>API: GET /api/devices
    API->>Catalog: list_devices()
    Catalog-->>API: derived URIs + publications_json
    API-->>Operator: persisted catalog response

    Operator->>API: POST /api/devices/{device}/alias
    API->>Catalog: set_alias()
    Catalog->>SQLite: update devices.public_name
    Catalog->>SQLite: reload devices and streams
    Catalog-->>API: updated derived URIs + RTSP paths
    API-->>Operator: updated device response
```
