# insight-io Frontend

## Role

- role: repo-native browser surface for the task-9 route-builder flow
- status: active
- version: 1
- major changes:
  - 2026-03-27 added a static single-page UI for catalog browse, app create,
    route declare, source bind/rebind/start/stop, and runtime inspection

The frontend is plain HTML, CSS, and JavaScript served by `insightiod`.

## Entry Points

- `/` serves `index.html`
- `/static/*` serves the bundled assets

## Covered Flows

- inspect discovered catalog URIs and grouped presets
- create or delete persistent apps
- declare app-local routes with `expect.media`
- create source binds from one `input` URI or `session_id`
- rebind one durable source through `:rebind`
- stop and restart durable sources through `:stop` and `:start`
- inspect shared runtime status, logical sessions, and additive RTSP state

## Startup

`insightiod` auto-serves this directory when started from the repo root.

Explicit override:

```bash
./build/bin/insightiod --frontend /absolute/path/to/frontend
```
