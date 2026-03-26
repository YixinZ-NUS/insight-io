# Intent Routing ER Diagram

## Role

- role: durable-schema entity-relationship diagram for `insight-io`
- status: active
- version: 6
- major changes:
  - 2026-03-26 scoped exact app-route binds to one owning app via composite
    `(app_id, route_id)` route references
  - 2026-03-26 removed redundant `app_sources.target_kind` and
    `app_sources.source_kind`, and kept bind-kind inference on existing
    foreign-key fields instead
  - 2026-03-26 removed redundant `selector_key` from the active `streams`
    schema and kept selector identity scoped to each device
  - 2026-03-25 replaced durable `delivery_name` with `rtsp_enabled` and
    publication metadata while keeping local IPC implicit
  - 2026-03-25 replaced grouped-route public bind naming with one app-local
    `target_name`
  - 2026-03-24 replaced stored `canonical_uri` with derived public `uri`
- past tasks:
  - `2026-03-26 – Apply Selector Review And Device-Scoped Stream Keying`
  - `2026-03-26 – Take Back Redundant App-Source Kind Columns`
  - `2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent`
  - `2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds`
  - `2026-03-24 – Add Mermaid ER Diagram For The Simplified Schema`

```mermaid
erDiagram
    devices ||--o{ streams : publishes
    apps ||--o{ app_routes : declares
    apps ||--o{ app_sources : owns
    app_routes ||--o{ app_sources : targets
    streams ||--o{ app_sources : selects
    streams ||--o{ sessions : realizes
    sessions ||--o{ app_sources : source_session
    sessions ||--o{ app_sources : active_session
    sessions ||--o{ session_logs : records

    devices {
        INTEGER device_id PK
        TEXT device_key UK
        TEXT public_name UK
        TEXT driver
        TEXT status
        TEXT metadata_json
        INTEGER last_seen_at_ms
        INTEGER created_at_ms
        INTEGER updated_at_ms
    }

    streams {
        INTEGER stream_id PK
        INTEGER device_id FK
        TEXT selector
        TEXT media_kind
        TEXT shape_kind
        TEXT channel
        TEXT group_key
        TEXT caps_json
        TEXT capture_policy_json
        TEXT members_json
        TEXT publications_json
        INTEGER is_present
        INTEGER created_at_ms
        INTEGER updated_at_ms
    }

    apps {
        INTEGER app_id PK
        TEXT name UK
        TEXT description
        TEXT config_json
        INTEGER created_at_ms
        INTEGER updated_at_ms
    }

    app_routes {
        INTEGER route_id PK
        INTEGER app_id FK
        TEXT route_name
        TEXT expect_json
        TEXT config_json
        INTEGER created_at_ms
        INTEGER updated_at_ms
    }

    app_sources {
        INTEGER source_id PK
        INTEGER app_id FK
        INTEGER route_id FK
        INTEGER stream_id FK
        INTEGER source_session_id FK
        INTEGER active_session_id FK
        TEXT target_name
        INTEGER rtsp_enabled
        TEXT state
        TEXT resolved_routes_json
        TEXT last_error
        INTEGER created_at_ms
        INTEGER updated_at_ms
    }

    sessions {
        INTEGER session_id PK
        INTEGER stream_id FK
        TEXT session_kind
        INTEGER rtsp_enabled
        TEXT request_json
        TEXT resolved_members_json
        TEXT state
        TEXT last_error
        INTEGER started_at_ms
        INTEGER stopped_at_ms
        INTEGER created_at_ms
        INTEGER updated_at_ms
    }

    session_logs {
        INTEGER log_id PK
        INTEGER session_id FK
        TEXT level
        TEXT event_type
        TEXT message
        TEXT payload_json
        INTEGER created_at_ms
    }
```

## Notes

- exact-route app-source rows use one app-scoped composite route reference:
  `(app_id, route_id) -> app_routes(app_id, route_id)`
- grouped app-source rows keep `route_id = NULL`, so route deletion cascades
  only exact-route bindings
