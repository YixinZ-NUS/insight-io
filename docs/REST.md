# REST API Reference

`insight-io` exposes a DB-first route-based API. Users choose a listed URI and
connect it to an app-declared route. Route declarations are purpose-first. They
may include semantic expectations, but they do not use raw runtime stream names
as the primary contract.

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
| `POST` | `/api/apps/{id}/routes` | create one route on an app |
| `GET` | `/api/apps/{id}/routes` | list one app's routes |
| `DELETE` | `/api/apps/{id}/routes/{route}` | delete one unused route |
| `GET` | `/api/apps/{id}/sources` | list one app's sources |
| `POST` | `/api/apps/{id}/sources` | connect one canonical URI to one app route |
| `POST` | `/api/apps/{id}/sources/{source_id}/start` | restart one persisted app source |
| `POST` | `/api/apps/{id}/sources/{source_id}/stop` | stop one running app source |
| `GET` | `/api/sessions` | list logical sessions |
| `GET` | `/api/sessions/{id}` | inspect one logical session |
| `POST` | `/api/sessions/{id}/start` | rehydrate one persisted logical session |
| `POST` | `/api/sessions/{id}/stop` | stop one logical session |
| `DELETE` | `/api/sessions/{id}` | destroy one logical session |
| `GET` | `/api/status` | inspect shared capture and delivery state |

## App Route Contract

Create routes before connecting sources:

```json
{
  "route_name": "yolov5",
  "expect": {
    "media": "video"
  }
}
```

Semantic expectation keys may include:

- `media`
- `channel`
- `same_group_as`
- `alignment_required`

Example:

```json
{
  "route_name": "scene-depth",
  "expect": {
    "media": "depth",
    "same_group_as": "scene-color",
    "alignment_required": true
  }
}
```

## App Source Contract

Connect a listed canonical URI to a declared route:

```json
{
  "input": "insightos://localhost/front-camera/720p_30/mjpeg",
  "route": "yolov5"
}
```

For grouped devices, the URI may select a member source while keeping the base
device alias readable:

```json
{
  "input": "insightos://localhost/desk-rgbd/480p_30?source=depth",
  "route": "scene-depth"
}
```

Rules:

- `route` is required
- `input` must be a canonical URI already exposed by the catalog
- if the URI includes `source=...`, the selector must resolve to a valid listed
  source member
- route expectations are checked against resolved source metadata
- duplicate canonical URIs within one app are rejected
- app-source routing is local IPC oriented in v1; remote hosts and `rtsp`-only
  sources are rejected

## Source Response Notes

App-source responses include:

- `route`
- `resolved_source_id`
- `resolved_source_member`
- `resolved_source_group_id` when present

The backend still uses the existing session graph under the routing layer. A
successful app-source connect creates one ordinary logical session and records
which resolved source was connected to the route.

## Restart Behavior

- app, route, and source records are durable
- startup normalizes persisted source runtime state back to `stopped`
- `POST /api/apps/{id}/sources/{source_id}/start` creates a fresh runtime
  session for that persisted source intent
- the same durable pattern applies to logical sessions through
  `POST /api/sessions/{id}/start`
