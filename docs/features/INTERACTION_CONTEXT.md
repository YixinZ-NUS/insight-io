# Interaction Context

This note explains what `insight-io` should feel like from a user and operator
point of view. It is grounded on the audited workflows from the donor project
and restates them in the new DB-first target-routing shape.

## Source Material

The interaction baseline comes from:

- `/home/yixin/Coding/insightos/demo_command.md`
- `/home/yixin/Coding/insightos/demo_command_3min.md`
- `/home/yixin/Coding/insightos/sdk/tests/app_integration_test.cpp`

Those files cover almost all currently exercised user-visible behavior:

- build and launch
- health and catalog inspection
- device aliasing
- direct session creation through REST and `insightos-open`
- shared capture and delivery reuse
- RTSP verification
- backend restart and persisted session recovery
- idle app startup
- later source injection into a running app
- mixed-source attach
- stream rename edge cases while a delivery is already live

## What Stays The Same In `insight-io`

`insight-io` keeps the same lower-level runtime model:

- canonical URIs still look like
  `insightos://<host>/<device>/<preset>[/<delivery>]`
- devices still come from discovery and catalog listing
- device aliases still create the human-usable names that appear in the URI
- logical, capture, and delivery sessions still exist and are still inspectable
- local attach still happens through the existing IPC contract
- RTSP and audio delivery flows still exist for operator-facing and debugging
  scenarios
- restart still preserves durable intent while runtime state is rebuilt

## What Changes In `insight-io`

The application layer is no longer only device-stream-name-first.

In the donor repo, the app workflow looks like:

1. Start an idle app such as `idle_mixed_monitor`
2. Fetch its `app_id`
3. Inject a source with only `input`
4. Let the app match callbacks using `on_stream("frame")`,
   `on_stream("color")`, `on_stream("depth")`, or device selectors

In `insight-io`, the same workflow becomes:

1. Start or create an app record
2. Declare routes such as `yolov5`, `yolov8`, `rgbd-view`,
   `orbbec-pointcloud`
3. Fetch or inspect the app and its declared routes
4. Inject a source with `input` plus `route`
5. Let the backend resolve bindings from the chosen URI into stream roles for
   that route
6. Let the app keep consuming familiar stream-role callbacks such as `frame`,
   `color`, and `depth` under that route scope

Example:

```json
{
  "input": "insightos://localhost/front-camera/720p_30/mjpeg",
  "route": "yolov5"
}
```

Recommended SDK shape:

```cpp
app.route("rgbd-view")
    .on_stream("color")
    .on_caps([](const insightos::Caps& caps) { /* unchanged */ })
    .on_frame([](const insightos::Frame& frame) { /* unchanged */ });
```

That keeps the current app look largely intact while still allowing multiple
independent processing routes in one app.

## Main User Journeys

### 1. Operator Bootstraps The Runtime

This remains close to `demo_command.md`:

1. Build the project
2. Start RTSP infrastructure if needed
3. Start the backend
4. Verify `/api/health`
5. Inspect `/api/devices`
6. Apply device aliases to produce stable canonical URIs

The target-routing project depends on this flow because target binding still
starts from listed canonical URIs.

### 2. Operator Uses Direct Session Flows

This also remains a first-class part of the product:

1. Create direct sessions through `POST /api/sessions` or `insightos-open`
2. Inspect session metadata
3. Verify RTSP or audio output
4. Stop sessions
5. Restart the backend
6. Rehydrate persisted sessions explicitly

These flows matter because `insight-io` is not only an app builder. It still
needs an inspectable media runtime beneath the app layer.

### 3. Developer Runs An Idle App And Injects Sources Later

This is the main app-developer flow carried forward from
`app_integration_test.cpp` and `demo_command_3min.md`:

1. Start an app with no initial source
2. Retrieve its `app_id`
3. Declare its routes
4. Inject one or more listed URIs into those routes
5. Observe route-scoped stream callbacks in the app
6. Stop, restart, or replace routed sources without destroying the app record

The difference is that source routing is explicit in the data model instead of
being reconstructed from a runtime-only registry.

### 4. Developer Builds Mixed-Source Apps

`insight-io` should support the same kinds of mixed-source apps seen in the
donor repo, but with clearer contracts:

- multiple independent video targets in one app
- one video route plus one RGBD route in the same app
- explicit rejection when a source cannot satisfy a route kind
- restart-safe app records that keep declared intent after the backend exits

### 5. Operator Audits Rename And Reuse Edge Cases

The donor demos show an important edge case:

- renaming a stream alias does not rename a running delivery in place
- a newly reused delivery still exposes the already-live public stream name
- the renamed stream appears only after the old delivery is stopped and a fresh
  delivery is created

`insight-io` should keep this behavior documented because target routing sits
above it and does not erase the underlying session and publication semantics.

## Why The Feature Trackers Need To Be Broader

The original `fullstack-intent-routing-e2e.json` file mostly tracks the new
data model and target-binding core. That is necessary but incomplete.

To represent the actual product, the repo also needs feature coverage for:

- bootstrap and health flows
- catalog and alias flows
- direct session and restart flows
- idle app discovery flows
- multi-route source injection flows
- target-binding visibility in API responses
- rename and reuse edge cases
- future browser builder flows

The additional tracker in this directory fills that gap.
