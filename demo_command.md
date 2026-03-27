# Demo Commands

## Role

- role: exact bash transcript for the task-10 developer-surface follow-up plus
  the checked-in runtime verification flow
- status: active
- revision: 2026-03-27 task10-dev-surface-followup
- major changes:
  - 2026-03-27 added thin `/api/dev/*` alternatives for catalog browse,
    direct-session create, app/route/source control, alias updates, and the
    live runtime-alias coherence check while keeping the original `/api/*`
    transcript intact
  - 2026-03-27 added the PipeWire audio example verification commands,
    including direct stereo startup, idle mono late bind, and the current
    selector query that shows why `audio/mono` and `audio/stereo` stay
    distinct exact selectors
  - 2026-03-27 added an explicit CLI-versus-REST equivalence summary for the
    checked-in example apps
  - 2026-03-27 captured the bare verification command list and observed outputs
    for the current task-9 SDK, browser, and example-app slice
- past tasks:
  - `2026-03-27 – Revalidate Task-10 Developer Surface, Correct Overclaim, And Add Dev Demo Alternatives`
  - `2026-03-27 – Add PipeWire Audio Example And Verify Mono/Stereo Selectors`
  - `2026-03-27 – Complete Task-9 SDK, Browser Flows, And Runtime Verification`
  - `2026-03-27 – Simplify Example Startup Paths And Close Mermaid Backlog`

## Build

```bash
cmake --build build -j4
```

```text
"""
[ 37%] Built target insightio_backend_support
[ 45%] Built target discovery_test
[ 45%] Built target schema_store_test
[ 50%] Built target insightiod
[ 54%] Built target catalog_service_test
[ 62%] Built target session_service_test
[ 62%] Built target app_service_test
[ 66%] Built target ipc_runtime_test
[ 70%] Built target rest_server_test
[ 75%] Built target insightos_sdk
[ 79%] Built target insightio_ipc_probe
[ 87%] Built target pipewire_audio_monitor
[ 87%] Built target v4l2_latency_monitor
[ 93%] Built target mixed_device_consumer
[ 95%] Built target app_sdk_test
[100%] Built target orbbec_depth_overlay
"""
```

```bash
ctest --test-dir build --output-on-failure
```

```text
"""
Internal ctest changing into directory: /home/yixin/Coding/insight-io/build
Test project /home/yixin/Coding/insight-io/build
1/8 Test #1: schema_store_test ................   Passed    0.05 sec
2/8 Test #2: catalog_service_test .............   Passed    2.22 sec
3/8 Test #3: discovery_test ...................   Passed    0.01 sec
4/8 Test #4: session_service_test .............   Passed    4.32 sec
5/8 Test #5: rest_server_test .................   Passed    4.91 sec
6/8 Test #6: app_service_test .................   Passed    5.17 sec
7/8 Test #7: ipc_runtime_test .................   Passed    1.89 sec
8/8 Test #8: app_sdk_test .....................   Passed    9.17 sec

100% tests passed, 0 tests failed out of 8

Total Test time (real) =  27.75 sec
"""
```

## CLI And REST Equivalence

- `./build/bin/pipewire_audio_monitor ... insightos://localhost/.../audio/stereo`
  is equivalent to starting `pipewire_audio_monitor` without a startup bind and
  later posting one source bind with `target = audio` and the same exact audio
  URI; `audio/mono` follows the same rule
- `./build/bin/v4l2_latency_monitor ... insightos://localhost/web-camera/720p_30`
  is equivalent to starting `v4l2_latency_monitor` without a startup bind and
  later posting one source bind with `target = camera` and the same URI
- `./build/bin/orbbec_depth_overlay ... insightos://localhost/sv1301s-u3/orbbec/preset/480p_30`
  is equivalent to starting `orbbec_depth_overlay` without a startup bind and
  later posting one source bind with `target = orbbec` and the same grouped
  preset URI
- `./build/bin/mixed_device_consumer ... camera=... orbbec=...`
  is equivalent to starting `mixed_device_consumer` without startup binds and
  later posting two source binds: one for `camera`, then one for `orbbec`

## Thin Developer REST Alternatives

These commands mirror the same control flow through the thinner
developer-facing surface. They do not replace the canonical `/api/*`
transcript below; they are the friendlier alternative for day-to-day internal
use.

```bash
curl -s http://127.0.0.1:18291/api/dev/health | jq
curl -s http://127.0.0.1:18291/api/dev/catalog | jq '.devices[] | {name, streams: [.streams[] | {stream_id, name, selector, uri}]}'
curl -s http://127.0.0.1:18291/api/dev/uris | jq '.uris[] | {stream_id, device, name, selector, uri}'
```

```bash
session_id=$(curl -s -X POST http://127.0.0.1:18291/api/dev/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30"}' | jq -r '.session_id')

app_id=$(curl -s -X POST http://127.0.0.1:18291/api/dev/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"dev-runner"}' | jq -r '.app_id')

curl -s -X POST http://127.0.0.1:18291/api/dev/apps/${app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"name":"camera","media":"video"}' | jq

curl -s -X POST http://127.0.0.1:18291/api/dev/apps/${app_id}/sources \
  -H 'Content-Type: application/json' \
  -d "{\"session_id\":${session_id},\"target\":\"camera\"}" | jq

curl -s http://127.0.0.1:18291/api/dev/runtime | jq
```

## Live Alias-Coherence Follow-Up

The task-10 follow-up check for live alias rename while a runtime is already
active can be rerun with:

```bash
curl -s -X POST http://127.0.0.1:18291/api/dev/devices/web-camera/alias \
  -H 'Content-Type: application/json' \
  -d '{"name":"front-camera-dev"}' | jq

curl -s -X POST http://127.0.0.1:18291/api/dev/streams/29/alias \
  -H 'Content-Type: application/json' \
  -d '{"name":"main-preview"}' | jq

curl -s http://127.0.0.1:18291/api/dev/uris | jq '.uris[] | select(.stream_id == 29)'
curl -s http://127.0.0.1:18291/api/dev/sessions | jq '.sessions[] | select(.stream_id == 29)'
curl -s http://127.0.0.1:18291/api/dev/runtime | jq '.serving_runtimes[] | select(.runtime_key == "stream:29")'
```

Expected result:

- `selector` stays `720p_30`
- the canonical URI updates to
  `insightos://localhost/front-camera-dev/main-preview`
- both the session view and the serving-runtime view report the updated alias
  rather than the stale pre-rename URI

## Daemon

```bash
./build/bin/insightiod --host 127.0.0.1 --port 18291 --db-path /tmp/insight-io-examples-18291.sqlite3 --rtsp-host 127.0.0.1 --rtsp-port 18591
```

```text
"""
insightiod 0.1.0
REST API listening on 127.0.0.1:18291
Device store: /tmp/insight-io-examples-18291.sqlite3
IPC attach socket: /tmp/insight-io-ipc-56513-3490146721213240067-0.sock
V4L2 worker 'stream:29' started: /dev/video0 1280x720 fps=30 buffers=4
V4L2 worker 'stream:29' started: /dev/video0 1280x720 fps=30 buffers=4
Orbbec worker 'stream:21' enabling color: 640x480@30fps format=mjpeg
Orbbec worker 'stream:21' enabling depth: 640x400@30fps format=y16
Orbbec worker 'stream:21': D2C hardware enabled (10 compatible depth profiles)
Orbbec worker 'stream:21' active (2 streams): orbbec://AY27552002M
Orbbec worker 'stream:21' depth first frame: 640x480 format=y16 bytes=614400
Orbbec worker 'stream:21' color first frame: 640x480 format=mjpeg bytes=19560
Orbbec worker 'stream:21' stopping
Orbbec worker 'stream:21' cleanup complete
Orbbec worker 'stream:22' enabling color: 1280x720@30fps format=mjpeg
Orbbec worker 'stream:22' enabling depth: 1280x800@30fps format=y16
Orbbec worker 'stream:22' active (2 streams): orbbec://AY27552002M
Orbbec worker 'stream:22' depth first frame: 1280x800 format=y16 bytes=2048000
Orbbec worker 'stream:22' color first frame: 1280x720 format=mjpeg bytes=44696
Orbbec worker 'stream:22' stopping
Orbbec worker 'stream:22' cleanup complete
V4L2 worker 'stream:29' started: /dev/video0 1280x720 fps=30 buffers=4
Orbbec worker 'stream:21' enabling color: 640x480@30fps format=mjpeg
Orbbec worker 'stream:21' enabling depth: 640x400@30fps format=y16
Orbbec worker 'stream:21': D2C hardware enabled (10 compatible depth profiles)
Orbbec worker 'stream:21' active (2 streams): orbbec://AY27552002M
Orbbec worker 'stream:21' depth first frame: 640x480 format=y16 bytes=614400
Orbbec worker 'stream:21' color first frame: 640x480 format=mjpeg bytes=21048
Orbbec worker 'stream:21' stopping
Orbbec worker 'stream:21' cleanup complete
^C
"""
```

```bash
curl -s http://127.0.0.1:18291/api/devices
```

## Webcam Late Bind

```bash
./build/bin/v4l2_latency_monitor --backend-host=127.0.0.1 --backend-port=18291 --max-frames=10
```

```bash
curl -s http://127.0.0.1:18291/api/apps
```

```text
"""
{
  "apps": [
    {
      "app_id": 1,
      "created_at_ms": 1774592926766,
      "name": "v4l2-latency-monitor",
      "updated_at_ms": 1774592926766
    }
  ]
}
"""
```

```bash
curl -s -X POST http://127.0.0.1:18291/api/apps/1/sources -H 'Content-Type: application/json' -d '{"target":"camera","input":"insightos://localhost/web-camera/720p_30"}'
```

```text
"""
{
  "active_session": {
    "capture_policy_json": {
      "device_uri": "v4l2:/dev/video0",
      "driver": "v4l2",
      "selected_caps": {
        "format": "mjpeg",
        "fps": 30,
        "height": 720,
        "named": "mjpeg_1280x720_30",
        "width": 1280
      },
      "stream_id": "image",
      "stream_name": "frame"
    },
    "created_at_ms": 1774592938266,
    "delivered_caps_json": {
      "format": "mjpeg",
      "fps": 30,
      "height": 720,
      "named": "mjpeg_1280x720_30",
      "width": 1280
    },
    "request_json": {
      "app_id": 1,
      "input": "insightos://localhost/web-camera/720p_30",
      "rtsp_enabled": false,
      "target": "camera"
    },
    "resolved_exact_stream_id": 29,
    "resolved_source": {
      "capture_policy_json": {
        "device_uri": "v4l2:/dev/video0",
        "driver": "v4l2",
        "selected_caps": {
          "format": "mjpeg",
          "fps": 30,
          "height": 720,
          "named": "mjpeg_1280x720_30",
          "width": 1280
        },
        "stream_id": "image",
        "stream_name": "frame"
      },
      "delivered_caps_json": {
        "format": "mjpeg",
        "fps": 30,
        "height": 720,
        "named": "mjpeg_1280x720_30",
        "width": 1280
      },
      "device_key": "dev_9558a6c24ec3337b524810ac08adc1a5",
      "media_kind": "video",
      "public_name": "web-camera",
      "publications_json": {
        "rtsp": {
          "profile": "default",
          "url": "rtsp://127.0.0.1:18591/web-camera/720p_30"
        }
      },
      "selector": "720p_30",
      "shape_kind": "exact",
      "stream_id": 29,
      "uri": "insightos://localhost/web-camera/720p_30"
    },
    "rtsp_enabled": false,
    "serving_runtime": {
      "consumer_count": 1,
      "consumer_session_ids": [
        7
      ],
      "ipc_channels": [
        {
          "attached_consumer_count": 0,
          "channel_id": "stream:29:image",
          "delivered_caps_json": {
            "format": "mjpeg",
            "fps": 30,
            "height": 720,
            "named": "mjpeg_1280x720_30",
            "width": 1280
          },
          "frames_published": 0,
          "media_kind": "video",
          "route_name": "image",
          "selector": "720p_30",
          "stream_name": "image"
        }
      ],
      "ipc_socket_path": "/tmp/insight-io-ipc-56513-3490146721213240067-0.sock",
      "owner_session_id": 7,
      "rtsp_enabled": false,
      "runtime_key": "stream:29",
      "shared": false,
      "state": "ready"
    },
    "session_id": 7,
    "session_kind": "app",
    "started_at_ms": 1774592938266,
    "state": "active",
    "updated_at_ms": 1774592938266
  },
  "active_session_id": 7,
  "capture_policy_json": {
    "device_uri": "v4l2:/dev/video0",
    "driver": "v4l2",
    "selected_caps": {
      "format": "mjpeg",
      "fps": 30,
      "height": 720,
      "named": "mjpeg_1280x720_30",
      "width": 1280
    },
    "stream_id": "image",
    "stream_name": "frame"
  },
  "created_at_ms": 1774592938266,
  "delivered_caps_json": {
    "format": "mjpeg",
    "fps": 30,
    "height": 720,
    "named": "mjpeg_1280x720_30",
    "width": 1280
  },
  "resolved_exact_stream_id": 29,
  "rtsp_enabled": false,
  "source_id": 1,
  "state": "active",
  "target": "camera",
  "target_resource_name": "apps/1/routes/camera",
  "updated_at_ms": 1774592938266,
  "uri": "insightos://localhost/web-camera/720p_30"
}
"""
```

```text
"""
camera caps: format=mjpeg size=1280x720 fps=30
camera caps: format=mjpeg size=1280x720 fps=30
frame=1 pts_ms=124.614 dts_ms=124.614 avg_pts_ms=124.614 avg_dts_ms=124.614 min_pts_ms=124.614 max_pts_ms=124.614 recv_wall_ms=1774592939325
"""
```

## Webcam Startup URI

```bash
./build/bin/v4l2_latency_monitor --backend-host=127.0.0.1 --backend-port=18291 --max-frames=5 insightos://localhost/web-camera/720p_30
```

```text
"""
camera caps: format=mjpeg size=1280x720 fps=30
camera caps: format=mjpeg size=1280x720 fps=30
frame=1 pts_ms=90.1021 dts_ms=90.1021 avg_pts_ms=90.1021 avg_dts_ms=90.1021 min_pts_ms=90.1021 max_pts_ms=90.1021 recv_wall_ms=1774592949849
"""
```

## Orbbec 480 Preset

```bash
./build/bin/orbbec_depth_overlay --backend-host=127.0.0.1 --backend-port=18291 --max-pairs=2 --output=/tmp/insight-io-overlay-480-runtime.png
```

```bash
curl -s http://127.0.0.1:18291/api/apps
```

```text
"""
{
  "apps": [
    {
      "app_id": 1,
      "created_at_ms": 1774592959248,
      "name": "orbbec-depth-overlay",
      "updated_at_ms": 1774592959248
    }
  ]
}
"""
```

```bash
curl -s -X POST http://127.0.0.1:18291/api/apps/1/sources -H 'Content-Type: application/json' -d '{"target":"orbbec","input":"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30"}'
```

```text
"""
"orbbec"
"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30"
{
  "format": "mjpeg",
  "fps": 30,
  "height": 480,
  "named": "mjpeg_640x480_30",
  "width": 640
}
"""
```

```text
"""
color caps: format=mjpeg size=640x480 fps=30
depth caps: format=y16 size=640x480 fps=30
depth caps: format=y16 size=640x480 fps=30
color caps: format=mjpeg size=640x480 fps=30
rendered_pairs=1 output=/tmp/insight-io-overlay-480-runtime.png
"""
```

```bash
file /tmp/insight-io-overlay-480-runtime.png
```

```text
"""
/tmp/insight-io-overlay-480-runtime.png: PNG image data, 640 x 480, 8-bit/color RGB, non-interlaced
"""
```

## Orbbec 720 Preset

```bash
./build/bin/orbbec_depth_overlay --app-name=orbbec-depth-overlay-720 --backend-host=127.0.0.1 --backend-port=18291 --max-pairs=2 --output=/tmp/insight-io-overlay-720-runtime.png
```

```bash
curl -s http://127.0.0.1:18291/api/apps
```

```text
"""
{
  "apps": [
    {
      "app_id": 1,
      "created_at_ms": 1774592972249,
      "name": "orbbec-depth-overlay-720",
      "updated_at_ms": 1774592972249
    }
  ]
}
"""
```

```bash
curl -s -X POST http://127.0.0.1:18291/api/apps/1/sources -H 'Content-Type: application/json' -d '{"target":"orbbec","input":"insightos://localhost/sv1301s-u3/orbbec/preset/720p_30"}'
```

```text
"""
"orbbec"
"insightos://localhost/sv1301s-u3/orbbec/preset/720p_30"
"""
```

```text
"""
color caps: format=mjpeg size=1280x720 fps=30
depth caps: format=y16 size=1280x800 fps=30
depth caps: format=y16 size=1280x800 fps=30
color caps: format=mjpeg size=1280x720 fps=30
rendered_pairs=1 output=/tmp/insight-io-overlay-720-runtime.png
"""
```

```bash
file /tmp/insight-io-overlay-720-runtime.png
```

```text
"""
/tmp/insight-io-overlay-720-runtime.png: PNG image data, 1280 x 720, 8-bit/color RGB, non-interlaced
"""
```

## Mixed Webcam Plus Orbbec

```bash
./build/bin/mixed_device_consumer --backend-host=127.0.0.1 --backend-port=18291 --max-frames=30
```

```bash
curl -s http://127.0.0.1:18291/api/apps
```

```text
"""
{
  "apps": [
    {
      "app_id": 1,
      "created_at_ms": 1774592984336,
      "name": "mixed-device-consumer",
      "updated_at_ms": 1774592984336
    }
  ]
}
"""
```

```bash
curl -s -X POST http://127.0.0.1:18291/api/apps/1/sources -H 'Content-Type: application/json' -d '{"target":"camera","input":"insightos://localhost/web-camera/720p_30"}'
```

```text
"""
"camera"
"insightos://localhost/web-camera/720p_30"
"""
```

```bash
curl -s -X POST http://127.0.0.1:18291/api/apps/1/sources -H 'Content-Type: application/json' -d '{"target":"orbbec","input":"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30"}'
```

```text
"""
"orbbec"
"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30"
"""
```

```text
"""
camera first frame format=mjpeg size=1280x720
camera=1 color=0 depth=0
orbbec depth first frame format=y16 size=640x480
orbbec color first frame format=mjpeg size=640x480
camera=22 color=1 depth=7
"""
```

## PipeWire Audio

```bash
curl -s http://127.0.0.1:18294/api/health
```

```text
"""
{
  "catalog_device_count": 4,
  "db_path": "/tmp/insight-io-audio-18294.sqlite3",
  "frontend_path": "/home/yixin/Coding/insight-io/frontend",
  "ipc_socket_path": "/tmp/insight-io-ipc-129958-6872020206810212583-0.sock",
  "session_count": 0,
  "status": "ok",
  "version": "0.1.0"
}
"""
```

```bash
curl -s http://127.0.0.1:18294/api/devices
```

```bash
./build/bin/pipewire_audio_monitor --backend-host=127.0.0.1 --backend-port=18294 --max-frames=6 --report-every=3 insightos://localhost/web-camera-mono/audio/stereo
```

```text
"""
audio caps: selector=audio/stereo format=s16le sample_rate=48000 channels=2
audio caps: selector=audio/stereo format=s16le sample_rate=48000 channels=2
frame=1 selector=audio/stereo format=s16le sample_rate=48000 channels=2 samples=2048 rms=0 peak=0 max_peak=0 recv_wall_ms=1774596378136 total_samples=2048
frame=3 selector=audio/stereo format=s16le sample_rate=48000 channels=2 samples=2048 rms=0.00109385 peak=0.00180054 max_peak=0.373413 recv_wall_ms=1774596378136 total_samples=6144
frame=6 selector=audio/stereo format=s16le sample_rate=48000 channels=2 samples=2048 rms=0.000470563 peak=0.00119019 max_peak=0.373413 recv_wall_ms=1774596378136 total_samples=12288
"""
```

```bash
./build/bin/pipewire_audio_monitor --app-name=pipewire-audio-rest --backend-host=127.0.0.1 --backend-port=18294 --max-frames=5 --report-every=1
```

```bash
curl -s http://127.0.0.1:18294/api/apps
```

```text
"""
{
  "apps": [
    {
      "app_id": 1,
      "created_at_ms": 1774596389562,
      "name": "pipewire-audio-rest",
      "updated_at_ms": 1774596389562
    }
  ]
}
"""
```

```bash
curl -s -X POST http://127.0.0.1:18294/api/apps/1/sources -H 'Content-Type: application/json' -d '{"target":"audio","input":"insightos://localhost/web-camera-mono/audio/mono"}'
```

```text
"""
{
  "active_session": {
    "capture_policy_json": {
      "device_uri": "pw:60",
      "driver": "pipewire",
      "selected_caps": {
        "channels": 1,
        "format": "s16le",
        "named": "s16le_48000x1",
        "sample_rate": 48000
      },
      "stream_id": "audio",
      "stream_name": "audio"
    },
    "created_at_ms": 1774596396229,
    "delivered_caps_json": {
      "channels": 1,
      "format": "s16le",
      "named": "s16le_48000x1",
      "sample_rate": 48000
    },
    "request_json": {
      "app_id": 1,
      "input": "insightos://localhost/web-camera-mono/audio/mono",
      "rtsp_enabled": false,
      "target": "audio"
    },
    "resolved_exact_stream_id": 32,
    "resolved_source": {
      "capture_policy_json": {
        "device_uri": "pw:60",
        "driver": "pipewire",
        "selected_caps": {
          "channels": 1,
          "format": "s16le",
          "named": "s16le_48000x1",
          "sample_rate": 48000
        },
        "stream_id": "audio",
        "stream_name": "audio"
      },
      "delivered_caps_json": {
        "channels": 1,
        "format": "s16le",
        "named": "s16le_48000x1",
        "sample_rate": 48000
      },
      "device_key": "dev_9a7e8bfbbed8ea7e8319e7c5b593e24c",
      "media_kind": "audio",
      "public_name": "web-camera-mono",
      "publications_json": {
        "rtsp": {
          "profile": "default",
          "url": "rtsp://127.0.0.1:18594/web-camera-mono/audio/mono"
        }
      },
      "selector": "audio/mono",
      "shape_kind": "exact",
      "stream_id": 32,
      "uri": "insightos://localhost/web-camera-mono/audio/mono"
    },
    "rtsp_enabled": false,
    "serving_runtime": {
      "consumer_count": 1,
      "consumer_session_ids": [
        2
      ],
      "ipc_channels": [
        {
          "attached_consumer_count": 0,
          "channel_id": "stream:32:audio",
          "delivered_caps_json": {
            "channels": 1,
            "format": "s16le",
            "named": "s16le_48000x1",
            "sample_rate": 48000
          },
          "frames_published": 0,
          "media_kind": "audio",
          "route_name": "audio",
          "selector": "audio/mono",
          "stream_name": "audio"
        }
      ],
      "ipc_socket_path": "/tmp/insight-io-ipc-129958-6872020206810212583-0.sock",
      "owner_session_id": 2,
      "rtsp_enabled": false,
      "runtime_key": "stream:32",
      "shared": false,
      "state": "ready"
    },
    "session_id": 2,
    "session_kind": "app",
    "started_at_ms": 1774596396229,
    "state": "active",
    "updated_at_ms": 1774596396229
  },
  "active_session_id": 2,
  "capture_policy_json": {
    "device_uri": "pw:60",
    "driver": "pipewire",
    "selected_caps": {
      "channels": 1,
      "format": "s16le",
      "named": "s16le_48000x1",
      "sample_rate": 48000
    },
    "stream_id": "audio",
    "stream_name": "audio"
  },
  "created_at_ms": 1774596396229,
  "delivered_caps_json": {
    "channels": 1,
    "format": "s16le",
    "named": "s16le_48000x1",
    "sample_rate": 48000
  },
  "resolved_exact_stream_id": 32,
  "rtsp_enabled": false,
  "source_id": 1,
  "state": "active",
  "target": "audio",
  "target_resource_name": "apps/1/routes/audio",
  "updated_at_ms": 1774596396229,
  "uri": "insightos://localhost/web-camera-mono/audio/mono"
}
"""
```

```text
"""
audio caps: selector=audio/mono format=s16le sample_rate=48000 channels=1
audio caps: selector=audio/mono format=s16le sample_rate=48000 channels=1
frame=1 selector=audio/mono format=s16le sample_rate=48000 channels=1 samples=1024 rms=0 peak=0 max_peak=0 recv_wall_ms=1774596396661 total_samples=1024
frame=2 selector=audio/mono format=s16le sample_rate=48000 channels=1 samples=1024 rms=0.038407 peak=0.339905 max_peak=0.339905 recv_wall_ms=1774596396661 total_samples=2048
frame=3 selector=audio/mono format=s16le sample_rate=48000 channels=1 samples=1024 rms=0.00107979 peak=0.00219727 max_peak=0.339905 recv_wall_ms=1774596396661 total_samples=3072
frame=4 selector=audio/mono format=s16le sample_rate=48000 channels=1 samples=1024 rms=0.000719793 peak=0.00149536 max_peak=0.339905 recv_wall_ms=1774596396661 total_samples=4096
frame=5 selector=audio/mono format=s16le sample_rate=48000 channels=1 samples=1024 rms=0.000587613 peak=0.00131226 max_peak=0.339905 recv_wall_ms=1774596396661 total_samples=5120
frame=6 selector=audio/mono format=s16le sample_rate=48000 channels=1 samples=1024 rms=0.000603064 peak=0.00158691 max_peak=0.339905 recv_wall_ms=1774596396661 total_samples=6144
"""
```
