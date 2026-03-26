# Past Tasks

## Role

- role: chronological change log and verification index for active repo work
- status: active
- version: 14
- major changes:
  - 2026-03-26 fixed the Orbbec duplicate-suppression fallback gap, added a
    focused aggregate-discovery regression test, verified the current host
    still lists one Orbbec plus one webcam without duplication, and swept the
    guide/report to remove the old fallback caveat
  - 2026-03-26 rechecked the task-5 worktree against live host behavior,
    corrected tracker underclaims for route-mismatch rejection, exact
    source-response identity, source stop/start declaration preservation, and
    referenced-session delete conflict, and recorded the explicit task-6 start
    order plus donor-reuse recheck
  - 2026-03-26 fixed the defect-level PR #5 review items, covering route JSON
    field naming, 64-bit path-id validation, app-delete failure propagation,
    and safer post-insert reloads with focused tests plus live REST probes
  - 2026-03-26 reviewed the three post-task-5 follow-ups, confirmed SQLite
    `FULLMUTEX`, confirmed Orbbec pipeline-profile fallback, recorded pure D2C
    capability gating as TODO, and refreshed the donor-reuse status from code
    plus live catalog output
  - 2026-03-26 recorded grouped-route delete cleanup closeout, regression
    tests, live host verification, task-list refresh, tracker correction for
    callback-dependent flows, and a grouped-route-delete sequence diagram
  - 2026-03-26 recorded the current app/route/source persistence review,
    runtime verification, doc sweep, donor-reuse writeup refresh, new
    app-route-source sequence diagram, and reproduction of the grouped
    member-route delete bug
  - 2026-03-26 recorded the checked-in direct-session REST and status slice,
    including live smoke verification, doc/task/tracker sync, and a direct
    session sequence diagram
  - 2026-03-26 recorded the app-source schema takeback, removing redundant bind
    kind columns, adding the missing canonical uniqueness indexes, and
    tightening exact-route ownership to the same app row
  - 2026-03-26 recorded the selector/schema review follow-up, including the
    reviewed V4L2 selector rename, the retained Orbbec namespace, and the
    removal of redundant stored `selector_key`
  - 2026-03-26 added the current scaffold/discovery review entry, including
    donor-reuse status and the schema-keying recommendation
  - 2026-03-26 recorded the persisted discovery catalog and alias flow
  - 2026-03-25 recorded the bootstrap backend reintroduction and the related
    docs-only contract updates

## 2026-03-26 – Fix Orbbec Duplicate Suppression Fallback And Add Discovery Regression Coverage

### What Changed

- fixed aggregate discovery in
  [discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/discovery.cpp)
  so generic V4L2 duplicate suppression now activates only after
  SDK-backed Orbbec discovery actually yields at least one usable Orbbec device
- kept the existing duplicate-suppression behavior when Orbbec discovery does
  succeed, but stopped suppressing generic V4L2 fallback when Orbbec discovery
  returns no usable devices or throws
- removed the now-unused hardcoded vendor-id export from
  [orbbec_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/orbbec_discovery.cpp)
  and moved the aggregate fallback boundary into testable discovery hooks in
  [discovery.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/discovery.hpp)
- added focused regression coverage in
  [discovery_test.cpp](/home/yixin/Coding/insight-io/backend/tests/discovery_test.cpp)
  for three cases:
  - empty Orbbec SDK discovery keeps V4L2 fallback visible
  - Orbbec SDK discovery failure keeps V4L2 fallback visible
  - usable Orbbec SDK discovery still enables V4L2 duplicate suppression
- updated the build graph in
  [backend/CMakeLists.txt](/home/yixin/Coding/insight-io/backend/CMakeLists.txt)
  to build and run the new focused `discovery_test` target
- refreshed the active docs in:
  - [README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)

### Why

- the existing duplicate-suppression rule was safe only when Orbbec SDK
  discovery worked reliably; if it returned nothing usable, the generic V4L2
  fallback path could be hidden even though the hardware was still reachable
- that made the fallback boundary too brittle for the real host setup where the
  Orbbec camera sometimes looks like a V4L2 camera as well
- task 6 depends on runtime reuse and attach behavior, so discovery needed to
  be made resilient first rather than carrying an avoidable hardware-visibility
  defect into the next slice

### Verification

```bash
cmake -S . -B build
cmake --build build -j4 --target \
  discovery_test \
  catalog_service_test \
  session_service_test \
  app_service_test \
  rest_server_test \
  insightiod

ctest --test-dir build --output-on-failure -R \
  'discovery_test|catalog_service_test|session_service_test|app_service_test|rest_server_test'

mkdir -p /tmp/insight-io-orbbec-fix-frontend
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18188 \
  --db-path /tmp/insight-io-orbbec-fix.sqlite3 \
  --frontend /tmp/insight-io-orbbec-fix-frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18188/api/devices | jq -r \
  '.devices[] | [.public_name, .driver] | @tsv'
```

## 2026-03-26 – Recheck Task-5 State, Correct Tracker Underclaims, And Detail Task-6 Start Order

### What Changed

- rechecked the current task-5 worktree against both the code and a fresh live
  run on the development host
- corrected four tracker underclaims that were already implementation-backed
  and verification-backed:
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
    now marks `reject-incompatible-route-expectation` as passing
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
    now marks `delete-referenced-session-conflicts`,
    `source-response-exposes-exact-stream-identity`, and
    `app-source-stop-and-start-preserve-declaration` as passing
- refreshed the active docs to match the recheck:
  - [README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
- made the task-6 handoff more explicit in
  [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md):
  start with serving-runtime reuse keyed by `stream_id` plus publication
  intent, then teach direct sessions and app sources to attach to that reuse
  layer before porting IPC and RTSP serving
- rechecked donor reuse against `../insightos`:
  discovery remains the only substantial checked-in donor reuse, while donor
  IPC, control-server, and `session_manager.cpp` still remain reference
  material for tasks 6 through 8 rather than active reuse in this repo

### Why

- task 5 is closed, so the next planning handoff needs the feature scoreboards
  to distinguish real runtime gaps from simple tracker drift
- several `false` tracker entries were now actively hiding already-verified
  control-plane behavior, which made the remaining scope look larger and less
  ordered than it really is
- task 6 is the first runtime slice where donor reuse matters again, so the
  start order needed to be concrete before implementation resumes

### Verification

```bash
cmake -S . -B build
cmake --build build -j4 --target \
  schema_store_test \
  catalog_service_test \
  session_service_test \
  app_service_test \
  rest_server_test \
  insightiod

ctest --test-dir build --output-on-failure

mkdir -p /tmp/insight-io-review-frontend
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18186 \
  --db-path /tmp/insight-io-review-live.sqlite3 \
  --frontend /tmp/insight-io-review-frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18186/api/health | jq
curl -s http://127.0.0.1:18186/api/devices | jq

app_id=$(curl -s -X POST http://127.0.0.1:18186/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"tracker-review-app"}' | jq -r '.app_id')
curl -s -X POST http://127.0.0.1:18186/api/apps/${app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"yolov5","expect":{"media":"video"}}'
source_json=$(curl -s -X POST http://127.0.0.1:18186/api/apps/${app_id}/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"yolov5"}')
printf '%s\n' "${source_json}" | jq
source_id=$(printf '%s' "${source_json}" | jq -r '.source_id')

curl -s -X POST http://127.0.0.1:18186/api/apps/${app_id}/sources/${source_id}/stop \
  -H 'Content-Type: application/json' -d '{}' | jq
curl -s -X POST http://127.0.0.1:18186/api/apps/${app_id}/sources/${source_id}/start \
  -H 'Content-Type: application/json' -d '{}' | jq

mismatch_app_id=$(curl -s -X POST http://127.0.0.1:18186/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"tracker-mismatch-app"}' | jq -r '.app_id')
curl -s -X POST http://127.0.0.1:18186/api/apps/${mismatch_app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}'
curl -s -i -X POST http://127.0.0.1:18186/api/apps/${mismatch_app_id}/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"orbbec/depth"}'

session_id=$(curl -s -X POST http://127.0.0.1:18186/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30"}' | jq -r '.session_id')
bind_app_id=$(curl -s -X POST http://127.0.0.1:18186/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"delete-conflict-app"}' | jq -r '.app_id')
curl -s -X POST http://127.0.0.1:18186/api/apps/${bind_app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"cam","expect":{"media":"video"}}'
curl -s -X POST http://127.0.0.1:18186/api/apps/${bind_app_id}/sources \
  -H 'Content-Type: application/json' \
  -d "{\"session_id\":${session_id},\"target\":\"cam\"}" | jq
curl -s -i -X DELETE http://127.0.0.1:18186/api/sessions/${session_id}

diff -u ../insightos/backend/src/discovery/v4l2_discovery.cpp \
  backend/src/discovery/v4l2_discovery.cpp
diff -u ../insightos/backend/src/discovery/pipewire_discovery.cpp \
  backend/src/discovery/pipewire_discovery.cpp
diff -u ../insightos/backend/src/discovery/orbbec_discovery.cpp \
  backend/src/discovery/orbbec_discovery.cpp
find ../insightos/backend -maxdepth 3 -type f \
  \( -iname '*ipc*' -o -iname '*session_manager*' -o -iname '*worker*' \) | sort
find backend/src -maxdepth 2 -type f | sort
```

## 2026-03-26 – Fix PR #5 Defect-Level Review Items

### What Changed

- fixed the five defect-level PR #5 review items in the checked-in backend:
  - [app_service.cpp](/home/yixin/Coding/insight-io/backend/src/app_service.cpp)
    now propagates `stop_session(...)` failures during `delete_app(...)`
    instead of deleting the app after silently ignoring a failed stop
  - [app_service.cpp](/home/yixin/Coding/insight-io/backend/src/app_service.cpp)
    now checks the post-insert reload path for both app creation and route
    creation instead of dereferencing an empty `std::optional`
  - [rest_server.cpp](/home/yixin/Coding/insight-io/backend/src/api/rest_server.cpp)
    now serializes route expectations under the documented `expect` field
    rather than the internal `expect_json` name
  - [rest_server.cpp](/home/yixin/Coding/insight-io/backend/src/api/rest_server.cpp)
    now parses every numeric path id through one checked 64-bit helper so
    oversized values return `400 Bad Request` instead of bubbling out as
    generic `500` failures
- added focused regression coverage in:
  - [app_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/app_service_test.cpp)
  - [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
- kept the scope on real fixes only:
  cleanup, duplicate, and not-actionable PR review items remain untouched in
  this change

### Why

- PR #5 still had five review items that could produce either incorrect HTTP
  behavior or unsafe internal failure handling
- the app-delete path needed to fail closed if runtime session stop fails
  because silently deleting the durable app while the session stop path errors
  would leave the control plane inconsistent
- the REST layer needed explicit checked parsing because large numeric path
  inputs are user-controlled and were reproducing `500` responses live

### Verification

```bash
cmake --build build -j4 --target app_service_test rest_server_test insightiod

./build/bin/app_service_test
./build/bin/rest_server_test

mkdir -p /tmp/insight-io-pr5-fix-frontend
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18187 \
  --db-path /tmp/insight-io-pr5-fix.sqlite3 \
  --frontend /tmp/insight-io-pr5-fix-frontend \
  --rtsp-host 127.0.0.1

curl -s -X POST http://127.0.0.1:18187/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"review-fix-live"}'

curl -s -X POST http://127.0.0.1:18187/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"yolov5","expect":{"media":"video"}}'

curl -s http://127.0.0.1:18187/api/apps/1/routes

curl -s -i http://127.0.0.1:18187/api/apps/9223372036854775808
curl -s -i http://127.0.0.1:18187/api/sessions/9223372036854775808
```

## 2026-03-26 – Review Post-Task-5 Follow-Ups And Refresh Donor Reuse Status

### What Changed

- reviewed the three requested post-task-5 follow-ups against the checked-in
  code, focused tests, live discovery output, and the donor repo at
  `../insightos`
- confirmed the SQLite threading review item landed in:
  [schema_store.cpp](/home/yixin/Coding/insight-io/backend/src/schema_store.cpp)
  and
  [schema_store_test.cpp](/home/yixin/Coding/insight-io/backend/tests/schema_store_test.cpp)
  by switching the shared database handle from `SQLITE_OPEN_NOMUTEX` to
  `SQLITE_OPEN_FULLMUTEX`
- confirmed the Orbbec discovery fallback review item landed in
  [orbbec_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/orbbec_discovery.cpp):
  when the raw sensor list does not populate `color` or `depth`, discovery now
  retries those profiles through `ob::Pipeline::getStreamProfileList(...)`
- recorded the third review item as not fully closed yet:
  [catalog.cpp](/home/yixin/Coding/insight-io/backend/src/catalog.cpp) no
  longer uses a literal serial allowlist, but aligned/grouped `480p_30`
  publication still falls back to the proven `sv1301s-u3` family and to the
  no-probe path
- added an explicit TODO note that a pure SDK D2C capability gate should
  replace the current hardcoded/family-gated `480p_30` publication rule later;
  for now the hardcoded `480p` path is retained as acceptable behavior
- refreshed the active docs in:
  [README.md](/home/yixin/Coding/insight-io/docs/README.md)
  and
  [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  so they now describe the current donor-reuse boundary accurately:
  - discovery remains substantial donor reuse
  - the Orbbec SDK is vendored locally in this repo
  - donor IPC, worker, and `session_manager` code are still reference material,
    not checked-in reuse in `insight-io`
- did not flip any additional feature tracker entries because the remaining
  `false` items still depend on unported IPC/runtime delivery, RTSP runtime,
  SDK callbacks, or broader end-to-end verification

### Why

- task 5 is closed, but the follow-up review items needed a precise
  code-versus-doc check before the next runtime slice starts
- the docs were starting to overstate the aligned/grouped `480p` gate as if the
  pure capability-probe version were already the active rule
- the donor reuse boundary matters for task 6 onward because discovery is
  already ported while IPC, workers, and runtime reuse are still absent from
  this repo

### Verification

```bash
cmake -S . -B build
cmake --build build -j4 --target \
  schema_store_test \
  catalog_service_test \
  app_service_test \
  rest_server_test \
  insightiod

./build/bin/schema_store_test
./build/bin/catalog_service_test
./build/bin/app_service_test
./build/bin/rest_server_test

mkdir -p /tmp/insight-io-review-frontend
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18185 \
  --db-path /tmp/insight-io-review.sqlite3 \
  --frontend /tmp/insight-io-review-frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18185/api/health | jq
curl -s http://127.0.0.1:18185/api/devices | jq

sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT device_id, device_key, public_name, driver, status \
   FROM devices ORDER BY public_name;"

sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT stream_id, device_id, selector, media_kind, shape_kind \
   FROM streams ORDER BY device_id, selector;"

diff -u ../insightos/backend/src/discovery/v4l2_discovery.cpp \
  backend/src/discovery/v4l2_discovery.cpp
diff -u ../insightos/backend/src/discovery/pipewire_discovery.cpp \
  backend/src/discovery/pipewire_discovery.cpp
diff -u ../insightos/backend/src/discovery/orbbec_discovery.cpp \
  backend/src/discovery/orbbec_discovery.cpp

find backend -maxdepth 3 \
  \( -iname '*ipc*' -o -iname '*worker*' -o -iname '*session_manager*' \) | sort
```

## 2026-03-26 – Close Grouped Route Delete Cleanup And Refresh Runtime Handoff

### What Changed

- fixed grouped member-route delete cleanup in
  [app_service.cpp](/home/yixin/Coding/insight-io/backend/src/app_service.cpp)
  so route deletion now:
  - discovers grouped `app_sources` whose `resolved_routes_json` references the
    deleted route
  - removes those grouped bind rows
  - deletes any linked app-owned grouped `sessions` row in the same
    transaction
- added regression coverage in:
  - [app_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/app_service_test.cpp)
  - [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
- swept the active docs and trackers to reflect task 5 closeout in:
  - [README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
- added one new sequence diagram for the lifecycle fix:
  - [grouped-route-delete-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/grouped-route-delete-sequence.md)
- refreshed the donor reuse writeup after checking the current repo against
  `../insightos`:
  - discovery and Orbbec SDK linkage are still properly reused
  - donor IPC, `session_manager.cpp`, and worker runtime pieces are still
    reference material only and remain the next runtime-porting targets
- corrected one tracker overclaim:
  [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  now keeps `inject-yolov5-source` at `false` because SDK callback delivery is
  still not implemented in this repository

### Why

- task 5 was blocked only by grouped member-route delete cleanup, so the
  backend needed one repo-native lifecycle fix rather than more schema changes
- the next planning handoff needed to move past persistence closeout and focus
  on serving-session reuse, IPC delivery, RTSP runtime, SDK callbacks, and the
  frontend
- the feature trackers needed to distinguish what is control-plane verified now
  from what still requires actual runtime callback delivery

### Verification

```bash
cmake -S . -B build
cmake --build build -j4 --target app_service_test rest_server_test insightiod
ctest --test-dir build --output-on-failure

./build/bin/app_service_test
./build/bin/rest_server_test

mkdir -p /tmp/frontend
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18184 \
  --db-path /tmp/insight-io-task5.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18184/api/devices

app_id=$(curl -s -X POST http://127.0.0.1:18184/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"grouped-delete-live"}' | jq -r '.app_id')
curl -s -X POST http://127.0.0.1:18184/api/apps/${app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/color","expect":{"media":"video"}}'
curl -s -X POST http://127.0.0.1:18184/api/apps/${app_id}/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}'
bind_json=$(curl -s -X POST http://127.0.0.1:18184/api/apps/${app_id}/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30","target":"orbbec"}')
session_id=$(printf '%s' "$bind_json" | jq -r '.active_session_id')

curl -i -X DELETE http://127.0.0.1:18184/api/apps/${app_id}/routes/orbbec%2Fdepth
curl -s http://127.0.0.1:18184/api/apps/${app_id}/sources
curl -i http://127.0.0.1:18184/api/sessions/${session_id}
sqlite3 /tmp/insight-io-task5.sqlite3 \
  "SELECT COUNT(*) FROM app_sources WHERE app_id = ${app_id};"
sqlite3 /tmp/insight-io-task5.sqlite3 \
  "SELECT COUNT(*) FROM sessions WHERE session_id = ${session_id};"
```

## 2026-03-26 – Review App Route Source Persistence Slice And Reproduce Grouped Route Delete Bug

### What Changed

- reviewed the current worktree implementation for durable apps, routes, and
  app sources across:
  - [app_service.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/app_service.hpp)
  - [app_service.cpp](/home/yixin/Coding/insight-io/backend/src/app_service.cpp)
  - [rest_server.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/rest_server.hpp)
  - [rest_server.cpp](/home/yixin/Coding/insight-io/backend/src/api/rest_server.cpp)
  - [001_initial.sql](/home/yixin/Coding/insight-io/backend/schema/001_initial.sql)
  - [app_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/app_service_test.cpp)
  - [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
- updated the active docs so the repo no longer claims app persistence is only
  future work:
  - [README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
- added one new current-slice sequence diagram:
  - [app-route-source-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/app-route-source-sequence.md)
- refreshed the donor reuse writeup so it now states more clearly that:
  - discovery and Orbbec SDK linkage are substantially reused from
    `../insightos`
  - IPC, workers, and session-manager runtime are still reference material only
  - the current open grouped-route delete bug is in repo-native app lifecycle
    code rather than donor code
- recorded and tracked one open lifecycle bug:
  deleting a grouped member route leaves the grouped `app_sources` row and the
  grouped app-owned `sessions` row behind with stale resolved-member metadata

### Why

- the current worktree already contains a substantial app/route/source slice,
  but the docs still described that functionality as future work
- the feature trackers needed pass-state updates only where verification had
  actually run, plus one new false entry for the reproduced grouped-route
  cleanup gap
- the user specifically asked for a reuse-status writeup against the donor repo
  and a doc sweep that keeps the guide and internal diagrams explainable

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

mkdir -p /tmp/frontend
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18190 \
  --db-path /tmp/insight-io-review.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18190/api/health
curl -s http://127.0.0.1:18190/api/devices

# App, route, and exact source verification
curl -s -X POST http://127.0.0.1:18190/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"persist-app"}'
curl -s http://127.0.0.1:18190/api/apps
curl -s -X POST http://127.0.0.1:18190/api/apps/2/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"yolov5","expect":{"media":"video"}}'
curl -s -X POST http://127.0.0.1:18190/api/apps/2/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/color","expect":{"media":"video"}}'
curl -s -X POST http://127.0.0.1:18190/api/apps/2/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}'
curl -s http://127.0.0.1:18190/api/apps/2/routes
curl -s -X POST http://127.0.0.1:18190/api/apps/2/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"yolov5"}'
curl -s -X POST http://127.0.0.1:18190/api/apps/2/sources/2/stop \
  -H 'Content-Type: application/json' -d '{}'
curl -s -X POST http://127.0.0.1:18190/api/apps/2/sources/2/start \
  -H 'Content-Type: application/json' -d '{}'
curl -s -X POST http://127.0.0.1:18190/api/apps/2/sources/2/rebind \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/1080p_30"}'

# Route expectation rejection
curl -s -X POST http://127.0.0.1:18190/api/apps/2/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","target":"orbbec/depth"}'

# Host validation on both paths
curl -s -X POST http://127.0.0.1:18190/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://not-local/web-camera/720p_30"}'
curl -s -X POST http://127.0.0.1:18190/api/apps/4/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://not-local/web-camera/720p_30","target":"cam"}'

# Session-backed bind + referenced delete conflict
curl -s -X POST http://127.0.0.1:18190/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30"}'
curl -s -X POST http://127.0.0.1:18190/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"session-bind-app"}'
curl -s -X POST http://127.0.0.1:18190/api/apps/3/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"cam","expect":{"media":"video"}}'
curl -s -X POST http://127.0.0.1:18190/api/apps/3/sources \
  -H 'Content-Type: application/json' \
  -d '{"session_id":5,"target":"cam"}'
curl -i -X DELETE http://127.0.0.1:18190/api/sessions/5

# Grouped bind verification and grouped-route delete bug reproduction
curl -s -X POST http://127.0.0.1:18190/api/apps \
  -H 'Content-Type: application/json' \
  -d '{"name":"rgbd-review"}'
curl -s -X POST http://127.0.0.1:18190/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/color","expect":{"media":"video"}}'
curl -s -X POST http://127.0.0.1:18190/api/apps/1/routes \
  -H 'Content-Type: application/json' \
  -d '{"route_name":"orbbec/depth","expect":{"media":"depth"}}'
curl -s -X POST http://127.0.0.1:18190/api/apps/1/sources \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/sv1301s-u3/orbbec/preset/480p_30","target":"orbbec"}'
curl -i -X DELETE http://127.0.0.1:18190/api/apps/1/routes/orbbec%2Fdepth
curl -s http://127.0.0.1:18190/api/apps/1/sources
sqlite3 /tmp/insight-io-review.sqlite3 \
  "SELECT source_id, app_id, route_id, stream_id, source_session_id, active_session_id, \
          target_name, state, resolved_routes_json \
   FROM app_sources;"
sqlite3 /tmp/insight-io-review.sqlite3 \
  "SELECT session_id, session_kind, stream_id, state, resolved_members_json \
   FROM sessions;"

# Restart persistence verification
./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18190 \
  --db-path /tmp/insight-io-review.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1
curl -s http://127.0.0.1:18190/api/apps
curl -s http://127.0.0.1:18190/api/apps/2/sources
curl -s http://127.0.0.1:18190/api/status
```

## 2026-03-26 – Reintroduce Direct Session REST And Status Slice

### What Changed

- added the checked-in direct-session service in:
  - [session_service.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/session_service.hpp)
  - [session_service.cpp](/home/yixin/Coding/insight-io/backend/src/session_service.cpp)
- threaded that service into the backend binary and REST surface in:
  - [backend/CMakeLists.txt](/home/yixin/Coding/insight-io/backend/CMakeLists.txt)
  - [rest_server.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/rest_server.hpp)
  - [rest_server.cpp](/home/yixin/Coding/insight-io/backend/src/api/rest_server.cpp)
  - [main.cpp](/home/yixin/Coding/insight-io/backend/src/main.cpp)
- added focused lifecycle coverage in:
  - [session_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/session_service_test.cpp)
  - [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
- updated the checked-in docs so guide, report, REST reference, task handoff,
  diagrams, and trackers all reflect the same slice:
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
  - [direct-session-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/direct-session-sequence.md)

### Why

- the repo had the direct-session code path in flight, but the surrounding docs
  and trackers still mixed older scaffold notes, stale failure claims, and
  future-work wording
- the slice is now strong enough to be treated as checked in:
  focused tests are green and the live backend can create, inspect, stop,
  restart, and delete a direct session from a real catalog entry on this host
- the next handoff needs to be app, route, and app-source persistence rather
  than another pass of ambiguity about whether direct sessions exist at all

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18186 \
  --db-path /tmp/insight-io-direct-session-smoke.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18186/api/health
curl -s http://127.0.0.1:18186/api/devices
curl -s -X POST http://127.0.0.1:18186/api/sessions \
  -H 'Content-Type: application/json' \
  -d '{"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":true}'
curl -s http://127.0.0.1:18186/api/sessions
curl -s http://127.0.0.1:18186/api/status
curl -s -X POST http://127.0.0.1:18186/api/sessions/1/stop \
  -H 'Content-Type: application/json' -d '{}'

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18186 \
  --db-path /tmp/insight-io-direct-session-smoke.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18186/api/sessions/1
curl -s -X POST http://127.0.0.1:18186/api/sessions/1/start \
  -H 'Content-Type: application/json' -d '{}'
curl -s -X POST http://127.0.0.1:18186/api/sessions/1/stop \
  -H 'Content-Type: application/json' -d '{}'
curl -i -X DELETE http://127.0.0.1:18186/api/sessions/1
```

## 2026-03-26 – Apply Selector Review And Device-Scoped Stream Keying

### What Changed

- updated the checked-in schema and catalog implementation so `streams` now
  stores:
  - `selector`
  - `UNIQUE(device_id, selector)`
  - no redundant durable `selector_key`
- updated the catalog HTTP response shape to expose only the reviewed source
  fields, which means `selector_key` is no longer returned by
  `GET /api/devices` or `GET /api/devices/{device}`
- changed V4L2 webcam selectors from `video-720p_30` style names to compact
  selectors such as `720p_30`, `1080p_30`, and `2160p_30`
- kept Orbbec selectors namespaced as `orbbec/color/...`,
  `orbbec/depth/...`, and `orbbec/preset/...` because that namespace matches
  the grouped RGBD target contract
- extended focused tests to verify:
  - `streams` no longer has a `selector_key` column
  - V4L2 catalog entries use compact selectors
  - REST device responses omit `selector_key`
  - derived URI and RTSP paths follow the renamed selectors
- updated the active docs, user guide, ER diagram, and tech report so the
  contract and implementation now agree
- documented the next slice as direct-session APIs and runtime verification in
  the task list and tech report

### Why

- the review comments were correct that plain V4L2 webcam selectors did not
  need a `video-` prefix because `media_kind` already carries that meaning
- the schema review was also correct that concatenated `selector_key` storage
  duplicated information already recoverable from the parent device identity
  and the selector itself
- the Orbbec namespace should stay because it does real contract work: it ties
  exact members and grouped presets to the shared RGBD family vocabulary used
  by grouped app targets

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18184 \
  --db-path /tmp/insight-io-selector-review.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18184/api/devices | jq
curl -s http://127.0.0.1:18184/api/devices/web-camera | jq
curl -s http://127.0.0.1:18184/api/devices/sv1301s-u3 | jq

sqlite3 /tmp/insight-io-selector-review.sqlite3 ".mode box" \
  "PRAGMA table_info(streams);"

sqlite3 /tmp/insight-io-selector-review.sqlite3 ".mode box" \
  "SELECT stream_id, device_id, selector, media_kind, shape_kind \
   FROM streams ORDER BY device_id, selector;"
```

## 2026-03-26 – Take Back Redundant App-Source Kind Columns

### What Changed

- updated the canonical SQL schema in
  [001_initial.sql](/home/yixin/Coding/insight-io/backend/schema/001_initial.sql)
  so it now:
  - removes redundant `app_sources.target_kind`
  - removes redundant `app_sources.source_kind`
  - makes `sessions.stream_id` required
  - makes `app_sources.stream_id` required
  - adds the canonical app-source uniqueness indexes on `(app_id, target_name)`
    and `(app_id, route_id) WHERE route_id IS NOT NULL`
  - makes exact-route binds app-local by adding
    `UNIQUE(app_id, route_id)` on `app_routes`
  - replaces the global `route_id -> app_routes.route_id` reference with
    `(app_id, route_id) -> app_routes(app_id, route_id)`
  - makes route deletion cascade only the exact-route `app_sources` rows that
    target that declared route
- tightened the schema regression test in
  [schema_store_test.cpp](/home/yixin/Coding/insight-io/backend/tests/schema_store_test.cpp)
  so it now verifies:
  - redundant app-source kind columns stay absent
  - `stream_id` is required on `sessions` and `app_sources`
  - the canonical app-source uniqueness indexes exist
  - exact-route ownership is enforced through the composite route foreign key
- updated the active contract docs in:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)

### Why

- `target_kind` duplicated information already encoded by whether `route_id`
  is present
- `source_kind` duplicated information already encoded by whether
  `source_session_id` is present
- leaving those kind columns in the canonical SQL would create avoidable
  divergence between the stored row shape and the real ownership model
- the docs already promised app-source uniqueness semantics that the SQL had
  not yet enforced, so the canonical schema needed the missing indexes before
  implementation work starts depending on that table
- exact-route binds should not be able to drift across apps by pointing at
  someone else's `route_id`, so route ownership also needed to become a schema
  rule instead of an application-side convention

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

sqlite3 :memory: ".read backend/schema/001_initial.sql" \
  "PRAGMA table_info(app_sources);"

sqlite3 :memory: ".read backend/schema/001_initial.sql" \
  "PRAGMA table_info(sessions);"

sqlite3 :memory: ".read backend/schema/001_initial.sql" \
  "PRAGMA index_list(app_sources);"

sqlite3 :memory: ".read backend/schema/001_initial.sql" \
  "PRAGMA foreign_key_list(app_sources);"
```

## 2026-03-26 – Review Current Scaffold, Discovery Reuse, And Schema Keying

### What Changed

- reviewed the current checked-in implementation against the active repo docs,
  the live runtime behavior, and the donor repo at `../insightos`
- updated the operator-facing guide in
  [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md) so it now
  includes a review walkthrough for:
  - build and focused tests
  - live discovery/catalog verification
  - SQLite catalog inspection
  - the current Orbbec duplicate-suppression rule
- updated the internal report in
  [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  to record:
  - the current implementation boundary
  - donor reuse status across discovery, Orbbec integration, IPC, and runtime
  - a schema recommendation to replace stored `selector_key` duplication with
    relational uniqueness on `(device_id, selector)` unless a future opaque
    `uid` is proven necessary
  - a Mermaid backlog for the next runtime slices

### Why

- the repo is still at the scaffold-plus-discovery stage, so planning the next
  slices requires a precise statement of what is already real versus what is
  still doc-only backlog
- the donor repo contains several reusable subsystems, but only some of them
  are already imported; documenting that split reduces future rework
- the current `streams.selector_key` design stores a concatenated key that is
  derivable from existing data, so it was worth recording a cleaner schema
  direction before more tables start depending on it

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18183 \
  --db-path /tmp/insight-io-review.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18183/api/health | jq
curl -s http://127.0.0.1:18183/api/devices | jq
curl -s -X POST http://127.0.0.1:18183/api/devices/web-camera/alias \
  -H 'Content-Type: application/json' \
  -d '{"public_name":"front-camera"}' | jq

sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT device_id, device_key, public_name, driver, status \
   FROM devices ORDER BY public_name;"

sqlite3 /tmp/insight-io-review.sqlite3 ".mode box" \
  "SELECT stream_id, device_id, selector, media_kind, shape_kind \
   FROM streams ORDER BY device_id, selector;"

diff -u ../insightos/backend/src/discovery/v4l2_discovery.cpp \
  backend/src/discovery/v4l2_discovery.cpp
diff -u ../insightos/backend/src/discovery/orbbec_discovery.cpp \
  backend/src/discovery/orbbec_discovery.cpp
diff -u ../insightos/backend/src/discovery/pipewire_discovery.cpp \
  backend/src/discovery/pipewire_discovery.cpp
```

## 2026-03-26 – Reintroduce Persisted Discovery Catalog And Alias Flow

### What Changed

- reintroduced the persisted discovery stack for the standalone backend:
  - shared discovery/catalog types
    [types.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/types.hpp)
    and [types.cpp](/home/yixin/Coding/insight-io/backend/src/types.cpp)
  - discovery entry points
    [discovery.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/discovery.hpp),
    [discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/discovery.cpp),
    [v4l2_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/v4l2_discovery.cpp),
    [orbbec_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/orbbec_discovery.cpp),
    and [pipewire_discovery.cpp](/home/yixin/Coding/insight-io/backend/src/discovery/pipewire_discovery.cpp)
  - persisted catalog service
    [catalog.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/catalog.hpp)
    and [catalog.cpp](/home/yixin/Coding/insight-io/backend/src/catalog.cpp)
- extended the HTTP surface so the checked-in backend now serves:
  - `GET /api/devices`
  - `GET /api/devices/{device}`
  - `POST /api/devices/{device}/alias`
- grounded the Orbbec catalog on the active docs and prior probe evidence:
  - V4L2 discovery skips Orbbec USB vendor nodes when Orbbec SDK discovery is
    active
  - the connected Orbbec serial `AY27552002M` now publishes
    `orbbec/depth/400p_30`, `orbbec/depth/480p_30`, and
    `orbbec/preset/480p_30`
  - grouped and aligned depth entries expose queryable RTSP metadata using the
    same selector path as the derived `insightos://` URI
- added focused verification coverage in
  [catalog_service_test.cpp](/home/yixin/Coding/insight-io/backend/tests/catalog_service_test.cpp)
  and updated
  [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
  to cover catalog listing, grouped Orbbec selectors, RTSP metadata, and alias
  updates
- updated the repo/operator docs:
  - [README.md](/home/yixin/Coding/insight-io/README.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  - [catalog-discovery-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/catalog-discovery-sequence.md)
- updated only verified tracker entries in:
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)

### Why

- direct sessions, app binds, reuse, and restart all depend on a stable source
  catalog first, so `devices` and `streams` had to become real persisted
  resources before the session and app layers come back
- the connected machine includes the exact hardware mix described in the docs:
  one webcam, one Orbbec device, and PipeWire sources, which made this the
  right slice to runtime-verify against real hardware rather than only donor
  assumptions
- the active grouped-source docs already document probe-backed depth behavior
  for Orbbec serial `AY27552002M`, so discovery should preserve that public
  contract even when raw SDK enumeration is incomplete in the current runtime

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18182 \
  --db-path /tmp/insight-io-catalog-verify.sqlite3 \
  --frontend /tmp/frontend \
  --rtsp-host 127.0.0.1

curl -s http://127.0.0.1:18182/api/health
curl -s http://127.0.0.1:18182/api/devices | jq
curl -s http://127.0.0.1:18182/api/devices/sv1301s-u3 | jq
curl -s -X POST http://127.0.0.1:18182/api/devices/web-camera/alias \
  -H 'Content-Type: application/json' \
  -d '{"public_name":"front-camera"}' | jq
curl -s http://127.0.0.1:18182/api/devices/front-camera | jq
```

## 2026-03-25 – Reintroduce Backend Bootstrap Build And Health Slice

### What Changed

- reintroduced a buildable standalone backend scaffold in this repository:
  - top-level [CMakeLists.txt](/home/yixin/Coding/insight-io/CMakeLists.txt)
  - backend build file [backend/CMakeLists.txt](/home/yixin/Coding/insight-io/backend/CMakeLists.txt)
  - explicit schema [001_initial.sql](/home/yixin/Coding/insight-io/backend/schema/001_initial.sql)
  - SQLite bootstrap layer
    [schema_store.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/schema_store.hpp)
    and [schema_store.cpp](/home/yixin/Coding/insight-io/backend/src/schema_store.cpp)
  - backend HTTP surface
    [rest_server.hpp](/home/yixin/Coding/insight-io/backend/include/insightio/backend/rest_server.hpp),
    [rest_server.cpp](/home/yixin/Coding/insight-io/backend/src/api/rest_server.cpp),
    and [main.cpp](/home/yixin/Coding/insight-io/backend/src/main.cpp)
- added focused verification targets:
  - [schema_store_test.cpp](/home/yixin/Coding/insight-io/backend/tests/schema_store_test.cpp)
  - [rest_server_test.cpp](/home/yixin/Coding/insight-io/backend/tests/rest_server_test.cpp)
- updated repository guidance and reporting:
  - [README.md](/home/yixin/Coding/insight-io/README.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [USER_GUIDE.md](/home/yixin/Coding/insight-io/docs/USER_GUIDE.md)
  - [TECH_REPORT.md](/home/yixin/Coding/insight-io/docs/design_doc/TECH_REPORT.md)
  - [bootstrap-health-sequence.md](/home/yixin/Coding/insight-io/docs/diagram/bootstrap-health-sequence.md)
- updated the broader runtime tracker so `runtime-build-and-test` now records
  verified pass state in
  [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

### Why

- the repository had no implementation left, so every later feature was blocked
  on first making `insight-io` buildable again
- the active docs require one explicit seven-table schema, so the bootstrap
  slice checks that in before higher-level discovery or app-routing code lands
- using a very small runtime surface keeps the new code aligned with the
  documented contract instead of dragging donor-only behavior back into the new
  repo prematurely

### Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure

./build/bin/insightiod \
  --host 127.0.0.1 \
  --port 18181 \
  --db-path /tmp/insight-io-live.sqlite3 \
  --frontend /tmp/frontend

curl -s http://127.0.0.1:18181/api/health
```

## 2026-03-25 – Minimize Source Metadata And Lock Session Delete Semantics

### What Changed

- updated the active docs hub, PRD, architecture note, data-model note, REST
  reference, task list, and feature trackers so the active contract now:
  - removes stale source-variant and source-group id fields from public source
    responses
  - keeps catalog publication metadata in `streams.publications_json` and makes
    `publications_json.rtsp.url` queryable
  - defines the RTSP publication URL as the same `/<device>/<selector>` path as
    the derived `insightos://` URI with the configured RTSP host replacing
    `localhost`
  - returns `409 Conflict` from `DELETE /api/sessions/{id}` while any
    `app_source` still references that session through `source_session_id` or
    `active_session_id`
- updated the implementation trackers so the new catalog RTSP metadata behavior
  and referenced-session delete conflict are both called out as future runtime
  checks

### Why

- the active contract had already moved away from variant/group id response
  fields, but several active docs still exposed them and made the schema read
  heavier than the current design actually is
- catalog publication metadata needed one explicit rule so RTSP addresses are
  predictable and queryable without turning RTSP into part of source identity
- session delete needed a hard contract because silently detaching or rewriting
  app-source references would create ambiguous runtime ownership and surprise
  callers

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase

### What Changed

- added
  [POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md)
  to record the recommendation that `insight-io` should keep a distinct
  runtime-only publication phase after capture for output profile, codec, and
  protocol-specific publication work
- updated
  [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  so the active architecture now explicitly separates capture workers from a
  post-capture publication phase
- updated [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md),
  [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md),
  [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md), and
  [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  so the new runtime boundary is visible in the diagram, backlog, hub, and docs
  tracker

### Why

- the current contract already separates capture-side source choice from RTSP
  publication intent, so a runtime publication phase is the clean place for
  output profile selection and codec/publication handling
- the donor material also separates source-side capture policy from
  promise-specific publication work, which supports keeping this boundary in the
  runtime rather than reintroducing durable delivery tables
- local IPC and future RTSP/LAN consumers need different publication handling,
  but that should not force duplicate capture workers or a more complex durable
  schema in v1

### Verification

- reviewed and aligned:
  - [POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/POST_CAPTURE_PUBLICATION_PHASE_WRITEUP.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)

## 2026-03-25 – Document RTSP Publication Reuse After Delivery-Name Removal

### What Changed

- added
  [RTSP_PUBLICATION_REUSE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/RTSP_PUBLICATION_REUSE_WRITEUP.md)
  to explain that the old donor-style rule
  "`ipc` versus `rtsp` implies separate delivery sessions" is no longer the
  active public contract
- updated [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md) so
  the docs hub now points readers at that writeup and summarizes the active
  additive RTSP publication rule
- updated
  [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  so the docs tracker explicitly verifies the writeup and the new reuse rule

### Why

- the active `insight-io` docs now model RTSP as optional publication state via
  `rtsp_enabled`, not as one inferred peer delivery mode alongside implicit IPC
- that means the older statement "different inferred `delivery_name` values must
  remain separate delivery sessions" no longer describes the public contract
- the donor runtime may still split publishers internally, but the API should
  only promise shared runtime plus additive RTSP publication when possible

### Verification

- reviewed and aligned:
  - [RTSP_PUBLICATION_REUSE_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/RTSP_PUBLICATION_REUSE_WRITEUP.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)

## 2026-03-25 – Clarify Direct Sessions And Multi-Device Route Declarations

### What Changed

- updated the active docs hub, PRD, architecture note, data-model note, REST
  reference, interaction note, and feature trackers so the current contract now
  says:
  - a direct session is one standalone or session-first runtime created from a
    selected URI before any app target is involved
  - declaring one compatible route does not make an app consume that direct
    session automatically
  - an app starts receiving frames only after one app-source bind becomes
    active by `input` or `session_id`
  - route names stay app-local and should describe logical input roles rather
    than discovered device aliases or one globally unique route string
  - a multi-device app that consumes two V4L2 cameras plus one Orbbec should
    declare app-local routes such as `front-camera`, `rear-camera`,
    `orbbec/color`, and `orbbec/depth`
- updated the broader user-journey tracker to add an explicit future runtime
  check that direct sessions stay idle for apps until a bind exists

### Why

- "direct session" was still easy to misread as capture-only or as implicitly
  attached to any app that had already declared matching routes
- multi-device apps needed one explicit naming rule so route declarations stay
  stable when the discovered device URIs or aliases change underneath

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-25 – Unify App Targets And Reframe RTSP As Publication Intent

### What Changed

- updated the active PRD, architecture note, data-model note, REST reference,
  grouped-source writeup, interaction note, task list, diagrams, and feature
  trackers so the current contract now says:
  - public app-source binds post one app-local `target` field instead of
    separate `route` and `route_grouped` inputs
  - grouped versus exact target resolution is hidden behind server-side target
    resolution
  - apps must reserve grouped roots:
    one app can not declare both one exact route `x` and any route below `x/`
  - local SDK guidance now uses `bind_source(...)` and `rebind(...)`
  - `/channel/...` is removed from the active v1 URI grammar
  - RTSP is modeled as optional durable publication intent through
    `rtsp_enabled` rather than as a peer to implicit local IPC attach
  - raw `rtsp://` input remains a future ingest/import path rather than a v1
    source-selection shape
- updated the durable schema docs and ER diagram so:
  - `streams.deliveries_json` becomes `streams.publications_json`
  - `app_sources.route_grouped` becomes `app_sources.target_name`
  - `app_sources.delivery_name` and `sessions.delivery_name` become
    `rtsp_enabled`
- updated the runtime diagram and tracker language to reflect additive RTSP
  publication on shared runtime instead of separate `ipc` versus `rtsp`
  delivery-intent branches

### Why

- a public split between exact and grouped bind methods leaked an internal
  distinction the backend can resolve from one posted target name
- `ipc` is not a meaningful user-facing publication choice for local SDK app
  binds; it is the fixed local attach mechanism
- RTSP publication changes durable resource state, so it belongs in the
  resource body and schema rather than in a query string such as `?rtsp=on`

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-24 – Derive URIs, Persist Delivery Intent, And Unify App Source Binds

### What Changed

- updated the active PRD, architecture note, data-model note, REST reference,
  grouped-source writeup, interaction note, task list, diagrams, and feature
  trackers so the current contract now says:
  - public `uri` values are derived source identifiers rather than durable DB
    keys
  - `delivery_name` is inferred during normalization and then persisted on
    `app_sources` and `sessions` rather than being part of stored source
    identity
  - `POST /api/apps/{id}/sources` is the single app-control surface for both
    URI-backed connects and session-backed attaches
  - grouped-session attach uses the same `route_grouped` surface as grouped
    preset URI binds
  - local SDK attach remains IPC-only in v1
  - future remote or LAN RTSP consumption remains planned as a separate path
- updated the grouped-startup wording so an app with one grouped target can
  start from one bare grouped preset URI without separately managing
  `/color` and `/depth`
- updated the ER and runtime diagrams to reflect derived `uri`, durable
  `delivery_name`, unified app-source binds, and IPC-only local attach

### Why

- tying delivery into the public URI shape made app-route binds ambiguous once
  local SDK attach was constrained to IPC
- a separate route-scoped `attach-session` endpoint was under-modeled once
  grouped-session attach and external app control were both in scope
- the split between `source_session_id` and `active_session_id` needed to be
  documented explicitly to avoid future confusion when remote or LAN RTSP
  consumption is added later

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-24 – Add Mermaid ER Diagram For The Simplified Schema

### What Changed

- added [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  with a Mermaid `erDiagram` for the active durable schema
- linked the ER diagram from
  [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  and [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)

### Why

- the simplified schema was documented textually, but it still benefited from
  one visual PK/FK map
- the docs set already had a runtime diagram; the schema now has a matching ER
  view

### Verification

- reviewed and aligned:
  - [intent-routing-er.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-er.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)

## 2026-03-24 – Simplify The Durable Data Model And Add A Docs Hub

### What Changed

- added [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md) as the
  centralized entry point for the active design set and updated
  [README.md](/home/yixin/Coding/insight-io/README.md) and
  [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md) to route readers there
  first
- renamed grouped bind terminology across the active contract from
  `route_namespace` / `connect_namespace(...)` to
  `route_grouped` / `connect_grouped(...)`
- rewrote
  [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  so the durable schema is now explicitly minimal:
  - `devices`
  - `streams`
  - `apps`
  - `app_routes`
  - `app_sources`
  - `sessions`
  - `session_logs`
- removed migration-history-table and backward-compat schema requirements from
  the active design because `insight-io` is expected to start from a fresh
  implementation
- kept per-device exact-member and grouped preset choices inside `streams`
  instead of splitting them into a separate preset table
- removed lower-level runtime tables from the active durable-schema design and
  treated capture, delivery, reuse, RTSP, IPC attach, and worker details as
  runtime-only status concerns instead
- updated the architecture note, PRD, REST reference, runtime diagram, task
  list, interaction note, and feature trackers to match the new grouped-bind
  naming and smaller schema boundary

### Why

- `route_namespace` was still describing the mechanism more than the business
  meaning; `route_grouped` is plainer
- the old data-model note was still pulling runtime internals into the durable
  relational design and was heavier than the current product contract needs
- a greenfield repo should not carry migration-history or compatibility
  scaffolding before the first real implementation even exists
- a separate preset table would duplicate the same device-scoped catalog fields
  already needed by `streams`
- the repo also lacked a single obvious entry point, which made the design set
  harder to navigate than it should be

### Verification

- reviewed and aligned:
  - [docs/README.md](/home/yixin/Coding/insight-io/docs/README.md)
  - [README.md](/home/yixin/Coding/insight-io/README.md)
  - [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
  - [fullstack-intent-routing-task-list.md](/home/yixin/Coding/insight-io/docs/tasks/fullstack-intent-routing-task-list.md)

## 2026-03-24 – Separate Catalog Publication From Runtime Ownership And Rename Route APIs

### What Changed

- reviewed `git blame` on the PRD and data-model contract and found the
  `discovery-owned` wording was new working-tree wording rather than a stable
  historical design boundary
- updated the PRD, architecture note, data-model note, REST reference,
  grouped-source writeup, interaction note, runtime diagram, task list, and
  feature trackers so the active contract now says:
  - discovery publishes selectable source shapes and metadata
  - logical, capture, delivery, and worker sessions realize those choices at
    runtime
  - one canonical URI selects one fixed catalog-published source shape
- renamed grouped bind terminology from `route_prefix` and `connect_prefix` to
  `route_namespace` and `connect_namespace`
- renamed SDK doc vocabulary from `RouteScope` to `AppRoute` and replaced
  `route-scoped callbacks` phrasing with callbacks on declared routes
- updated [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md) so future work
  must keep header metadata on docs and implementation files, sweep related
  docs after major changes, update problem-doc status, and periodically archive
  stale docs

### Why

- discovery/catalog authority and runtime session ownership are different
  responsibilities and the docs should not blur them
- the blame trail showed the older repo baseline was about fixed URI meaning,
  not discovery owning runtime workers
- `*Scope` and `*prefix` names describe mechanism more than business meaning;
  `AppRoute` and `route_namespace` better match the intent-first model

### Verification

- checked `git blame` on:
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
- reviewed and aligned:
  - [AGENTS.md](/home/yixin/Coding/insight-io/AGENTS.md)
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-24 – Import The Live RGBD Preset Success Into The Intent-Routing Contract

### What Changed

- updated the PRD, architecture note, data-model note, REST reference,
  interaction note, runtime diagram, README, AGENTS guidance, task list, and
  feature trackers so the repo now documents:
  - exact-member URIs that still resolve to one delivered stream
  - grouped preset URIs that may resolve to one fixed related stream bundle
  - namespaced RGBD routes such as `orbbec/color` and `orbbec/depth`
  - one grouped preset bind using `route_namespace`, for example
    `orbbec/preset/480p_30`, instead of a separate SDK-only frame-merge layer
- documented the proven grouped Orbbec preset choice
  `orbbec/preset/480p_30` alongside exact member choices such as
  `orbbec/depth/400p_30` and `orbbec/depth/480p_30`

### Why

- the sibling `insightos` repo now has a real end-to-end RGBD app success:
  one `480p_30` URI delivered color plus aligned depth, and that grouped flow
  was good enough to drive a live “capture when object < 50cm” application on
  the connected Orbbec camera
- that result weakens the earlier `insight-io` assumption that every canonical
  URI must resolve to exactly one delivered stream
- for the next full-stack repo, it is cleaner to keep routes intent-first with
  `app.route(...).expect(...)`, allow one grouped preset bind to fan out into
  related route namespaces, and remove the extra conceptual layer of a
  separate SDK-only frame-merge helper

### Verification

- reviewed and aligned:
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
- grounded the grouped preset change on the sibling live success:
  - [rgbd_proximity_capture.cpp](/home/yixin/Coding/insightos/examples/rgbd_proximity_capture.cpp)
  - [rgbd-proximity-capture-20260324-101043-202.jpg](/home/yixin/Coding/insightos/captures/rgbd-proximity-capture-20260324-101043-202.jpg)

## 2026-03-23 – Extend Orbbec Probe For 720p And 800p Depth Modes

### What Changed

- extended `experiments/orbbec_depth_probe/probe.cpp` to:
  - print D2C-compatible depth-profile lists for selected color modes
  - distinguish strict profile matches from fallback selection
  - probe `1280x800` native depth plus forced D2C cases
  - probe whether `1280x720` aligned depth exists through D2C
- recorded the expanded raw output on branch `orbbec-depth-480p-probe` in
  `2026-03-23-orbbec-depth-probe-extended.txt`
- updated the grouped-source writeup, PRD, architecture note, data-model note,
  REST reference, and runtime feature tracker to reflect the new device result

### Why

- the earlier real-device probe only settled the `480p` aligned-depth case
- the design still needed evidence for whether this device supports aligned
  `720p` depth or a separate aligned `800p` mode
- discovery should not publish new exact-stream variants without device proof

### Results

- the connected device exposes native `1280x800` depth profiles but no native
  `1280x720` depth profile
- `getD2CDepthProfileList` returned no compatible depth profiles for color
  `1280x720@30` in either software or hardware D2C mode
- forcing software or hardware D2C on depth-only `1280x800@30` still delivered
  `1280x800` `y16` depth frames
- no distinct aligned `720p` or aligned `800p` depth output was observed on
  the tested device
- the docs now treat `depth-480p_30` as the only proven special aligned depth
  choice on this Orbbec unit and allow, at most, a short discovery comment for
  operator context rather than new dependency-specific fields

### Verification

- verified feature id `orbbec-depth-720p-800p-sdk-probe`
- built and ran on branch `orbbec-depth-480p-probe`:

```bash
cmake -S experiments/orbbec_depth_probe -B build/orbbec_depth_probe
cmake --build build/orbbec_depth_probe -j4
ORBBEC_SDK_CONFIG=$PWD/experiments/orbbec_depth_probe/vendor/orbbec_sdk/config/OrbbecSDKConfig_v1.0.xml \
  ./build/orbbec_depth_probe/orbbec_depth_probe
```

## 2026-03-23 – Run Real Orbbec Depth-480 Probe And Redesign The Contract

### What Changed

- created the isolated branch `orbbec-depth-480p-probe`
- copied a minimal Orbbec SDK subset into `experiments/orbbec_depth_probe` and
  added a standalone probe harness plus build files
- ran the probe against the connected Orbbec device and recorded the raw output
  on branch `orbbec-depth-480p-probe` in `2026-03-23-orbbec-depth-probe.txt`
- updated the grouped-source writeup, PRD, architecture note, data-model note,
  REST reference, and runtime feature tracker to reflect the real-device result

### Why

- the previous docs still treated aligned-depth-only Orbbec behavior as an open
  question
- the current donor worker disables D2C unless both color and depth are
  user-requested, but that turns out to be stricter than the tested hardware
  requires
- the backend design now needs to encode `depth-480p_30` as a capture-policy
  mapping from native `640x400` depth plus forced D2C, not as a literal native
  `640x480` depth profile lookup

### Results

- the connected device reported color `640x480@30` profiles and native depth
  `640x400@30` profiles, but no native `640x480` depth profile
- depth-only native capture delivered `640x400` `y16` depth frames
- depth-only forced software D2C delivered `640x480` `y16` depth frames with
  zero delivered color frames
- depth-only forced hardware D2C delivered `640x480` `y16` depth frames with
  zero delivered color frames
- color+depth software and hardware D2C also delivered `640x480` `y16` depth
  frames

### Verification

- verified feature id `orbbec-depth-480-sdk-probe`
- built and ran on branch `orbbec-depth-480p-probe`:

```bash
cmake -S experiments/orbbec_depth_probe -B build/orbbec_depth_probe
cmake --build build/orbbec_depth_probe -j4
ORBBEC_SDK_CONFIG=$PWD/experiments/orbbec_depth_probe/vendor/orbbec_sdk/config/OrbbecSDKConfig_v1.0.xml \
  ./build/orbbec_depth_probe/orbbec_depth_probe
```

## 2026-03-23 – Tighten Grouped Runtime, Route Validation, And Join/Pair Docs

### What Changed

- documented the grouped runtime rule across the PRD, architecture note,
  data-model note, REST reference, and runtime diagram:
  - one canonical URI still maps to one fixed published source shape
  - related URIs from one source group must either resolve to one compatible
    grouped backend mode or reject
  - normal use remains backend-fixed per discovered catalog entry, with no
    bind-time override layer
- tightened wrong-route protection by documenting that non-debug routes should
  declare `media` expectations so obvious misroutes such as depth into a video
  detector are rejected by contract
- reevaluated channel disambiguation from usage only and kept
  `/channel/<name>` in the path instead of moving source selection into a query
  parameter
- documented grouped preset routing above ordinary namespaced routes so apps
  can declare color and depth separately, then activate them together through
  one route-namespace bind without hardware-specific route setup
- added a real-device Orbbec experiment plan for testing whether
  `depth-480p_30` can run as the only user-requested stream and how grouped
  runtime behaves underneath
- updated the feature trackers to record the new doc-grounding checks and the
  pending runtime investigations

### Why

- the earlier docs described source groups and aligned depth as catalog choices,
  but they still left grouped-runtime conflict handling too implicit
- wrong-route rejection needs a slightly stronger contract than "optional
  expectations" if common app routes are supposed to be safe by default
- from usage alone, channel choice behaves like part of stream identity rather
  than like an optional URI filter
- combined color+depth consumption is easier for app authors if one grouped
  preset bind can activate named routes directly instead of turning paired
  hardware into a separate SDK-only routing primitive
- exact Orbbec aligned-depth-only behavior still needs real-device evidence, so
  the docs should frame that as an investigation rather than pretending the
  dependency shape is already settled

### Verification

- verified feature ids:
  - `docs-grouped-runtime-rule`
  - `docs-channel-path-and-grouped-preset-contract`
- reviewed and aligned:
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [INTERACTION_CONTEXT.md](/home/yixin/Coding/insight-io/docs/features/INTERACTION_CONTEXT.md)
  - [intent-routing-runtime.md](/home/yixin/Coding/insight-io/docs/diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)

## 2026-03-23 – Convert Repo To Docs-Only Exact-Stream Design Baseline

### What Changed

- rewrote the core design docs so the project contract is now explicitly:
  - one canonical URI selects one fixed published source shape
  - discovery publishes exact member choices up front
  - route expectations validate compatibility instead of choosing hidden stream
    variants
- adopted the RGBD depth decision that D2C-sensitive outputs are exposed at
  discovery time as separate user choices, for example:
  - `orbbec/depth/400p_30`
  - `orbbec/depth/480p_30`
- adopted optional `/channel/<name>` URI disambiguation for stereo or dual-eye
  devices, while keeping discovery responsible for emitting the final full URI
- expanded the lifecycle contract in the PRD, REST reference, and feature
  trackers to cover:
  - direct sessions through REST and `insightos-open`
  - app-first routing
  - session-first attach
  - identical-URI fan-out
  - different-delivery shared-capture behavior
  - runtime rebind
  - runtime and session inspection
- reset the implementation trackers so previously scaffold-backed runtime
  features are no longer marked as passing
- converted the repository to docs-only and removed the outdated checked-in
  implementation scaffold

### Why

- the old scaffold and the current design docs had diverged enough that keeping
  both in one repo created false confidence
- the grouped-source writeup exposed a deeper contract issue:
  route expectations and backend policy were both trying to decide which stream
  the user meant
- for RGBD depth, the D2C choice materially changes the delivered output, so it
  belongs in discovery-visible user choice rather than hidden route policy
- for stereo and dual-eye devices, channel distinction needs an explicit but
  uncommon escape hatch; optional `/channel/<name>` is enough
- deleting the stale implementation makes the repository honest again and turns
  it into a clean baseline for the next implementation round

### Verification

- reviewed and aligned:
  - [fullstack-intent-routing-prd.md](/home/yixin/Coding/insight-io/docs/prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](/home/yixin/Coding/insight-io/docs/design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [GROUPED_SOURCE_SELECTION_WRITEUP.md](/home/yixin/Coding/insight-io/docs/design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  - [REST.md](/home/yixin/Coding/insight-io/docs/REST.md)
  - [runtime-and-app-user-journeys.json](/home/yixin/Coding/insight-io/docs/features/runtime-and-app-user-journeys.json)
  - [fullstack-intent-routing-e2e.json](/home/yixin/Coding/insight-io/docs/features/fullstack-intent-routing-e2e.json)
- inspected the repository tree after deletion to confirm that only docs remain

## 2026-03-23 – Write Up Grouped Source Selection Problems

### What Changed

- added the focused design note
  [GROUPED_SOURCE_SELECTION_WRITEUP.md](design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)
  to document the unresolved complexity around:
  - left/right source separation
  - native versus D2C-aligned depth
  - grouped-source pairing
  - keeping route declarations simple
- recorded why fake flat names such as `desk-rgbd-color` and a visible
  `<group>` URI path layer are both poor defaults
- documented the recommended direction:
  - keep the base URI readable
  - move left/right and aligned/native distinction into catalog source-variant
    metadata
  - keep `route.expect(...)` semantic
  - let the backend auto-resolve source variants in the common path

### Why

- the current design discussion has a real unresolved tension:
  - users should not need to configure pairing and alignment manually
  - the backend still has to distinguish variants whose caps differ
- aligned depth is not just another label; it can change the delivered caps, so
  the backend must preserve that distinction even if the app API stays simple
- writing the problem down in one focused note is better than encoding a
  premature answer directly into the PRD

### Verification

- reviewed donor grounding for grouped RGBD behavior and D2C policy:
  - [standalone-project-plan.md](/home/yixin/Coding/insightos/docs/plan/standalone-project-plan.md#L179)
  - [TECH_REPORT.md](/home/yixin/Coding/insightos/docs/design_doc/TECH_REPORT.md#L808)
  - [request_support_test.cpp](/home/yixin/Coding/insightos/backend/tests/request_support_test.cpp#L93)
  - [orbbec_discovery.cpp](/home/yixin/Coding/insightos/backend/src/discovery/orbbec_discovery.cpp#L223)
- verified the new repo now contains
  [GROUPED_SOURCE_SELECTION_WRITEUP.md](design_doc/GROUPED_SOURCE_SELECTION_WRITEUP.md)

## 2026-03-23 – Add Interaction Context And Broader User-Journey Trackers

### What Changed

- expanded the PRD in
  [fullstack-intent-routing-prd.md](prd/fullstack-intent-routing-prd.md)
  with an explicit interaction baseline section that ties `insight-io` to the
  audited donor flows instead of describing the product only in abstract terms
- added [INTERACTION_CONTEXT.md](features/INTERACTION_CONTEXT.md) to explain
  how the old operator demos and SDK integration test map onto the new
  DB-first route-based project
- added the broader tracker
  [runtime-and-app-user-journeys.json](features/runtime-and-app-user-journeys.json)
  to cover:
  - backend bootstrap and health
  - catalog and alias flows
  - direct session flows
  - RTSP and audio verification
  - persisted session restart
  - idle app registration
  - late source injection into declared routes
  - mixed video and RGBD routing
  - stream rename and delivery reuse edge cases
  - browser recovery flows
- revised the public naming direction so the new concept is a route above the
  existing callback chain instead of a full replacement for the SDK callback
  surface
- added explicit feature requirements for:
  - single-URI app launch continuity
  - multi-route CLI launch with named connections
  - explicit route declaration before startup

### Why

- the original feature tracker in this repo captured the new persistence and
  route-connection core, but it did not yet represent the full user interaction
  surface that the donor repo already demonstrates
- the donor demos and the SDK integration test are the best available source of
  truth for what real operator and developer workflows should remain possible
- writing those journeys down now gives the repo a better implementation
  backlog for the SDK and frontend phases
- keeping the app surface visually close to the current SDK lowers migration
  cost and preserves the sample-app style already used across the donor repo
- a route-above-stream model is closer to `dora-rs`, which uses named inputs
  and outputs rather than removing stream names like `frame`, `color`, or
  `depth` from app-facing code

### Verification

- opened and reviewed:
  - `/home/yixin/Coding/insightos/demo_command.md`
  - `/home/yixin/Coding/insightos/demo_command_3min.md`
  - `/home/yixin/Coding/insightos/sdk/tests/app_integration_test.cpp`
- reviewed `dora-rs/dora` interface structure through DeepWiki and confirmed
  that it models routing with named inputs and outputs
- verified the new repo now contains:
  - [fullstack-intent-routing-prd.md](prd/fullstack-intent-routing-prd.md)
  - [INTERACTION_CONTEXT.md](features/INTERACTION_CONTEXT.md)
  - [runtime-and-app-user-journeys.json](features/runtime-and-app-user-journeys.json)

## 2026-03-23 – Bootstrap Standalone `insight-io`

### What Changed

- created a new standalone repository root for the DB-first route-based
  project under `insight-io`
- added repo grounding documents:
  - [fullstack-intent-routing-prd.md](prd/fullstack-intent-routing-prd.md)
  - [INTENT_ROUTING_ARCHITECTURE.md](design_doc/INTENT_ROUTING_ARCHITECTURE.md)
  - [INTENT_ROUTING_DATA_MODEL.md](design_doc/INTENT_ROUTING_DATA_MODEL.md)
  - [intent-routing-runtime.md](diagram/intent-routing-runtime.md)
  - [fullstack-intent-routing-task-list.md](tasks/fullstack-intent-routing-task-list.md)
  - [fullstack-intent-routing-e2e.json](features/fullstack-intent-routing-e2e.json)
- added repo-level agent guidance in [AGENTS.md](../AGENTS.md) so future work
  is grounded on the new docs and uses the feature list as the verification
  scoreboard
- carried over the backend-first scaffold needed to continue implementation:
  - explicit schema in [001_initial.sql](../backend/schema/001_initial.sql)
  - durable app, route, and source persistence in the backend store
  - route-aware REST routes for app route CRUD and source connection
  - focused backend tests for persistence and route validation
- replaced copied repo docs with standalone versions of [README.md](../README.md)
  and [REST.md](REST.md) so the new repository is self-contained

### Why

- the work had to move from an incremental prototype inside `insightos` into a
  clean standalone project with its own git history
- the new repository needs to be grounded by docs first so implementation stays
  aligned with the route-based product framing
- the feature tracker has to remain the single pass/fail scoreboard for future
  implementation slices

### Verification

```bash
cmake -S . -B build
cmake --build build -j4 --target \
  device_store_test \
  rest_server_test

./build/bin/device_store_test
./build/bin/rest_server_test
```
