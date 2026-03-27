# Demo Commands

## Role

- role: exact bash transcript for the task-9 runtime verification flow
- status: active
- revision: 2026-03-27 task9-runtime-verification
- major changes:
  - 2026-03-27 added an explicit CLI-versus-REST equivalence summary for the
    checked-in example apps
  - 2026-03-27 captured the bare verification command list and observed outputs
    for the current task-9 SDK, browser, and example-app slice
- past tasks:
  - `2026-03-27 – Complete Task-9 SDK, Browser Flows, And Runtime Verification`
  - `2026-03-27 – Simplify Example Startup Paths And Close Mermaid Backlog`

## Build

```bash
cmake --build build -j4
```

```text
"""
[ 39%] Built target insightio_backend_support
[ 47%] Built target schema_store_test
[ 47%] Built target catalog_service_test
[ 52%] Built target insightiod
[ 56%] Built target discovery_test
[ 73%] Built target ipc_runtime_test
[ 73%] Built target session_service_test
[ 73%] Built target rest_server_test
[ 73%] Built target app_service_test
[ 78%] Built target insightio_ipc_probe
[ 82%] Built target insightos_sdk
[ 93%] Building CXX object examples/CMakeFiles/v4l2_latency_monitor.dir/v4l2_latency_monitor.cpp.o
[ 93%] Building CXX object examples/CMakeFiles/orbbec_depth_overlay.dir/orbbec_depth_overlay.cpp.o
[ 93%] Building CXX object examples/CMakeFiles/mixed_device_consumer.dir/mixed_device_consumer.cpp.o
[ 93%] Built target app_sdk_test
[ 95%] Linking CXX executable ../bin/mixed_device_consumer
[ 95%] Built target mixed_device_consumer
[ 97%] Linking CXX executable ../bin/v4l2_latency_monitor
[100%] Linking CXX executable ../bin/orbbec_depth_overlay
[100%] Built target v4l2_latency_monitor
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
1/8 Test #1: schema_store_test ................   Passed    0.06 sec
2/8 Test #2: catalog_service_test .............   Passed    2.65 sec
3/8 Test #3: discovery_test ...................   Passed    0.01 sec
4/8 Test #4: session_service_test .............   Passed    4.30 sec
5/8 Test #5: rest_server_test .................   Passed    4.93 sec
6/8 Test #6: app_service_test .................   Passed    5.07 sec
7/8 Test #7: ipc_runtime_test .................   Passed    1.87 sec
8/8 Test #8: app_sdk_test .....................   Passed    8.65 sec

100% tests passed, 0 tests failed out of 8

Total Test time (real) =  27.55 sec
"""
```

## CLI And REST Equivalence

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
