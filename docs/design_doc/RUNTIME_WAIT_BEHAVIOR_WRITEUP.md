# Runtime Wait Behavior Writeup

## Role

- role: document the current fixed-sleep and bounded-wait behavior in the
  runtime so a later performance and hardening pass can optimize it without
  losing the current startup-safety intent
- status: active
- version: 1
- major changes:
  - 2026-03-27 documented the current startup-grace waits in session startup
    and RTSP publication, recorded the live behavior observed on the
    development host, and outlined an empirical optimization plan for replacing
    fixed sleeps with more explicit readiness checks
- past tasks:
  - `2026-03-27 – Document Runtime Wait And Startup Sleep Behavior`

## Summary

The current runtime still uses a small number of fixed sleeps and polling waits
around worker startup and RTSP publisher startup.

Those waits are not currently proven functional defects.

On the 2026-03-27 review pass:

- exact RTSP publication on `web-camera/720p_30` became active on a fresh
  runtime
- strict FFmpeg validation against the published RTSP URL produced no warnings
- exact IPC attach on the same runtime returned a real frame immediately

So the current waits should be treated as:

- acceptable startup-grace behavior for the present slice
- cleanup and performance-optimization targets
- candidates for stronger readiness signaling later

They should not be treated as justification for changing the public contract.

## Scope

This note focuses on the waits that directly affect serving-runtime startup and
attach latency:

- RTSP publisher startup in
  [rtsp_publisher.cpp](/home/yixin/Coding/insight-io/backend/src/publication/rtsp_publisher.cpp)
- worker-start grace windows in
  [session_service.cpp](/home/yixin/Coding/insight-io/backend/src/session_service.cpp)

It does not try to redesign:

- device-discovery retry/backoff behavior
- daemon shutdown sleeps
- worker-specific capture cadence or frame pacing

## Current Wait Sites

### 1. RTSP publisher start grace

Current site:

- [rtsp_publisher.cpp](/home/yixin/Coding/insight-io/backend/src/publication/rtsp_publisher.cpp#L286)

Current behavior:

- after `ffmpeg` is forked and both stdin/stderr pipes are made non-blocking,
  the publisher sleeps for `250ms`
- after that grace period, it calls `poll_process_locked()`
- if the child already exited, startup fails with the captured publisher error

What this protects today:

- immediate crash detection for bad `ffmpeg` startup
- a short grace window before the runtime treats the publisher as unusable

What it does not prove:

- that `ffmpeg` is fully connected to the RTSP server
- that a downstream reader can already consume frames

### 2. Session RTSP-start worker grace

Current site:

- [session_service.cpp](/home/yixin/Coding/insight-io/backend/src/session_service.cpp#L992)

Current behavior:

- if RTSP publication needs the worker to start, the runtime sleeps for `200ms`
- it then checks whether any IPC channel has published frames
- it only declares failure when the worker is already not running and no frames
  were published

What this protects today:

- immediate detection of a worker that dies almost instantly before first frame
- a small startup grace window before reporting
  `capture worker exited before publishing frames`

### 3. IPC attach worker grace

Current site:

- [session_service.cpp](/home/yixin/Coding/insight-io/backend/src/session_service.cpp#L1050)

Current behavior:

- if IPC attach has to start the worker from `ready`, the runtime sleeps for
  `200ms`
- it then checks the same emitted-frame signal
- it only errors when the worker has already exited and no frames were emitted

What this protects today:

- early attach failure classification for workers that die before first frame
- first-frame startup slack before the attach path reports
  `runtime_start_failed`

## Current Runtime Semantics

The current waits are not pure blind delays.

They are short grace windows followed by a concrete failure check:

- RTSP publisher path:
  `sleep -> poll child process -> fail if already exited`
- worker-start paths:
  `sleep -> inspect worker liveness plus published-frame counters -> fail only
  if the worker already died before emitting anything`

That distinction matters for future optimization:

- the present behavior is more conservative than "sleep and assume success"
- but it is still less precise than an explicit readiness signal

## Live Behavior Observed On 2026-03-27

The following fresh host run was used while re-checking PR #7 review items:

```bash
./third_party/mediamtx/mediamtx /tmp/mediamtx-pr7.yml

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18284 \
  --db-path /tmp/pr7-review.sqlite3 \
  --frontend /tmp/pr7-review-frontend \
  --rtsp-host 127.0.0.1 \
  --rtsp-port 18584

curl -s -X POST http://127.0.0.1:18284/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":true}'

ffmpeg -rtsp_transport tcp -loglevel warning \
  -err_detect +crccheck+bitstream+buffer+careful \
  -i rtsp://127.0.0.1:18584/web-camera/720p_30 \
  -an -f null /dev/null 2>errors.log

socket_path=$(curl -s http://127.0.0.1:18284/api/health | jq -r '.ipc_socket_path')
./build/bin/insightio_ipc_probe "${socket_path}" 1
```

Observed results:

- the runtime reached
  `rtsp_publication.state = active`
- `frames_forwarded` advanced continuously
- strict FFmpeg validation produced no warnings in `errors.log`
- IPC attach returned a real `mjpeg` frame immediately on the same runtime
- the session/status surfaces reported one active serving runtime with live
  frame counters

This means the current waits are not known-bad on the development host.

## Why Optimization Still Matters

Even though the current behavior is working, the fixed waits still have real
cost and risk:

- they impose a minimum startup delay even when the worker or publisher became
  ready sooner
- they are load-sensitive because a slow machine may need longer than the
  current grace window
- they make time-to-first-frame harder to reason about precisely
- they encode readiness indirectly through elapsed time rather than an explicit
  state transition

So they are reasonable optimization targets for:

- lower first-attach latency
- lower RTSP publication latency
- cleaner startup/error reporting
- less flake risk on slower hosts

## Recommended Optimization Direction

### Preferred replacement for worker-start sleeps

Replace the fixed `200ms` grace windows with a bounded wait loop tied to one
real readiness condition:

- first frame published on any expected channel
- or explicit worker-exit detection
- or timeout with structured startup error

Good shapes:

- condition-variable or promise-style first-frame signal from the runtime
- bounded polling loop on `frames_published` with a short interval and timeout

Preferred result:

- success as soon as the first frame is available
- fast failure if the worker dies immediately
- explicit timeout if the worker stays alive but never produces frames

### Preferred replacement for RTSP publisher sleep

Replace the fixed `250ms` grace window with one bounded readiness probe tied to
publisher state.

Possible shapes:

- parse `ffmpeg` stderr until a known-ready or known-failed condition appears
- poll the child plus one explicit write-readiness or handshake state
- add a publisher-local timeout loop instead of one fixed sleep

Preferred result:

- fail immediately when `ffmpeg` exits with a clear startup error
- stop waiting as soon as the publisher is genuinely usable
- preserve explicit error capture without assuming one host-specific startup
  delay

## Measurement Plan For The Perf Pass

Any future optimization should be empirical.

Minimum measurements:

1. Worker startup latency:
   request start or IPC attach, then measure time until first published frame.
2. RTSP publication startup latency:
   request `rtsp_enabled`, then measure time until strict FFmpeg can consume
   the stream.
3. False-failure rate:
   on repeated runs under host load, count how often startup is reported as
   failed even though a longer wait would have succeeded.
4. Idle-cost comparison:
   compare fixed-sleep startup time against readiness-based startup time.

Recommended host conditions:

- idle machine
- loaded machine
- repeated cold daemon starts
- repeated worker stop/start or attach/detach cycles

## Micro-Benchmark Recommendation

If startup races become hard to reproduce reliably end to end, use the repo's
empirical rule:

- isolate the wait path in a deterministic micro-benchmark
- control producer startup delay explicitly
- show the old threshold-based approach failing under one controlled delay
- show the new readiness-based approach succeeding under the same conditions

This is especially useful if a later change introduces:

- first-frame signaling
- publisher handshake state
- timeout policy changes

## Acceptance Criteria For A Future Optimization

A future wait-optimization change should not be considered complete until it
shows all of the following:

- exact IPC attach still succeeds on live hardware
- exact RTSP publication still passes strict FFmpeg validation
- startup error reporting remains explicit and not silent
- first-frame or first-consume latency is measurably improved or at least not
  regressed
- no increase in flaky startup failures under repeated runs

## Non-Goals

This note does not recommend:

- keeping devices open idly just to avoid startup wait
- widening the public API contract with transport-specific wait fields
- changing the current `insightos://` source identity model
- adding durable worker-state tables for readiness tracking in v1
