# Task 7 Runtime Hardening Plan

## Role

- role: focused execution plan for closing the remaining task-7 IPC runtime
  hardening work on the development host
- status: resolved
- version: 3
- major changes:
  - 2026-03-27 followed up on the live Orbbec wrinkle by rechecking the donor
    daemon and the current backend on the same host, restoring donor-style
    depth-family format mapping in Orbbec discovery plus the 480p catalog
    probe, and confirming the current host again publishes exact depth
    selectors plus grouped `orbbec/preset/480p_30`
  - 2026-03-27 closed the task-7 runtime hardening slice by verifying exact
    webcam, PipeWire, and SDK-backed Orbbec color IPC attach on the
    development host, fixing idle IPC teardown so workers release devices when
    the last local consumer disconnects, and recording the current host's
    verified Orbbec runtime boundary
  - 2026-03-26 added a task-scoped plan for honest Orbbec fallback behavior,
    empirical capture-to-IPC verification, teardown validation, and
    linter-driven cleanup before task 8 expands the runtime surface
- past tasks:
  - `2026-03-27 – Restore Live Orbbec Depth And Grouped Catalog Publication`
  - `2026-03-27 – Complete Task-7 IPC Hardening And Task-8 Exact RTSP Publication`
  - `2026-03-26 – Add Serving Runtime Reuse And Runtime-Status Topology`

## Plan

This plan is now resolved for the checked-in task-7 scope.

Observed closeout on the development host:

- exact IPC attach is live-verified for the webcam, PipeWire audio, and the
  SDK-backed Orbbec color path
- the serving runtime now returns to `ready` and resets IPC counters after the
  last local consumer disconnects, so idle sessions release capture devices
- repeated daemon restarts no longer reproduced the earlier disappearing
  Orbbec case during this verification pass
- the current backend now republishes one SDK-backed `sv1301s-u3` device with
  exact depth selectors including `orbbec/depth/400p_30` and
  `orbbec/depth/480p_30` plus grouped `orbbec/preset/480p_30`
- donor-style raw discovery also sees `ir` on this host, but the checked-in
  public catalog intentionally stays within the documented v1 color/depth
  exact-member and grouped-preset contract
- the V4L2 fallback node `/dev/video2` still was not directly usable through
  `v4l2-ctl` on this host during this pass, so the fallback path remains
  unproven here rather than generally claimed

## Scope

- In:
  - honest discovery and catalog behavior when the Orbbec SDK path is unusable
    on the current host
  - empirical runtime verification for exact webcam, PipeWire audio, and the
    Orbbec UVC fallback path
  - device-release checks after session stop and delete
  - linter-driven cleanup for task-7 runtime files
  - docs, trackers, and verification notes for the hardened task-7 slice
- Out:
  - active RTSP publication runtime
  - SDK callback delivery
  - frontend work

## Action Items

- [x] Confirm the failed SDK-backed Orbbec path is hidden from discovery when
      runtime-start probing proves it cannot publish frames on this host.
- [x] Keep the usable UVC fallback visible by hardening V4L2 discovery against
      the short release delay left by the failed Orbbec SDK probe.
- [x] Re-run real-hardware direct-session attach checks for webcam, PipeWire,
      and the proven SDK-backed Orbbec color path using the unix IPC probe.
- [x] Prove teardown by cycling create, attach, stop, delete, and rebind while
      checking status plus post-release webcam `v4l2-ctl` capture.
- [ ] Run `cppcheck` first, then follow the repo’s `lcov` workflow for any
      linter-flagged dead-code removals before keeping those changes.
- [x] Record the exact verification commands and outcomes in
      `docs/past-tasks.md`, then update the feature trackers and user-facing
      docs to match the re-verified grouped RGBD publication on this host.

## Open Questions

- Raw donor-style discovery sees `ir` on this host, but should the public
  catalog keep omitting `orbbec/ir/...` until the v1 contract and trackers
  explicitly adopt IR as a first-class source family?
- Should grouped RGBD discovery stay host-dependent until a device is verified
  live, even if static SDK capability queries suggest it might exist?
