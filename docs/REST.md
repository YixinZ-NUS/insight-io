# REST API Reference

`insight-io` exposes a DB-first target-routing API. Users choose a listed
canonical URI and bind it to an app-declared target. They do not manage raw
stream names in the app-source flow.

## Current API Index

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/health` | liveness and version |
| `GET` | `/api/devices` | list public devices, presets, and canonical URIs |
| `GET` | `/api/devices/{device}` | full detail for one public device |
| `POST` | `/api/apps` | create one durable app record |
| `GET` | `/api/apps` | list durable apps |
| `GET` | `/api/apps/{id}` | inspect one app |
| `DELETE` | `/api/apps/{id}` | delete one app and its owned records |
| `POST` | `/api/apps/{id}/targets` | create one target on an app |
| `GET` | `/api/apps/{id}/targets` | list one app's targets |
| `DELETE` | `/api/apps/{id}/targets/{target}` | delete one unused target |
| `GET` | `/api/apps/{id}/sources` | list one app's sources and routed bindings |
| `POST` | `/api/apps/{id}/sources` | bind one canonical URI to one app target |
| `POST` | `/api/apps/{id}/sources/{source_id}/start` | restart one persisted app source |
| `POST` | `/api/apps/{id}/sources/{source_id}/stop` | stop one running app source |
| `GET` | `/api/sessions` | list logical sessions |
| `GET` | `/api/sessions/{id}` | inspect one logical session |
| `POST` | `/api/sessions/{id}/start` | rehydrate one persisted logical session |
| `POST` | `/api/sessions/{id}/stop` | stop one logical session |
| `DELETE` | `/api/sessions/{id}` | destroy one logical session |
| `GET` | `/api/status` | inspect shared capture and delivery state |

## App Target Contract

Create targets before binding sources:

```json
{
  "target_name": "yolov5",
  "target_kind": "video"
}
```

Supported target kinds today:

- `video`: binds `frame`, else `color`, else the first non-audio stream
- `audio`: binds `audio`
- `rgbd`: requires `color` and `depth`, and includes `ir` when available

## App Source Contract

Bind a listed canonical URI to a declared target:

```json
{
  "input": "insightos://localhost/front-camera/720p_30/mjpeg",
  "target": "yolov5"
}
```

Rules:

- `target` is required
- `input` must be a canonical URI already exposed by the catalog
- duplicate canonical URIs within one app are rejected
- incompatible URI/target-kind pairs are rejected
- app-source routing is local IPC oriented in v1; remote hosts and `rtsp`-only
  sources are rejected

## Source Response Notes

App-source responses include:

- `target`
- `target_kind`
- `bindings`

Each binding reports:

- `role`
- `stream_id`
- `stream_name`

The backend still uses the existing session graph under the routing layer. A
successful app-source bind creates one ordinary logical session and computes
role bindings above it.

## Restart Behavior

- app, target, and source records are durable
- startup normalizes persisted source runtime state back to `stopped`
- `POST /api/apps/{id}/sources/{source_id}/start` creates a fresh runtime
  session for that persisted source intent
- the same durable pattern applies to logical sessions through
  `POST /api/sessions/{id}/start`
