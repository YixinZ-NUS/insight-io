# Task 8 RTSP Publication Plan

## Role

- role: focused execution plan for the task-8 RTSP runtime and post-capture
  publication phase
- status: resolved
- version: 2
- major changes:
  - 2026-03-27 closed the first task-8 slice by adding exact single-channel
    RTSP publication on the shared serving runtime, exposing runtime
    publication facts through session/status views, vendoring mediamtx into the
    repo, and validating publication with strict FFmpeg checks on a non-default
    daemon RTSP port
  - 2026-03-26 added a task-scoped plan for layering runtime-only RTSP
    publication on top of the existing shared serving-runtime and IPC worker
    graph
- past tasks:
  - `2026-03-27 – Complete Task-7 IPC Hardening And Task-8 Exact RTSP Publication`
  - `2026-03-25 – Define A Runtime-Only Post-Capture Publication Phase`
  - `2026-03-26 – Add Serving Runtime Reuse And Runtime-Status Topology`

## Plan

This plan is now resolved for the first exact-source task-8 slice.

Delivered closeout:

- exact single-channel serving runtimes can now add RTSP publication on top of
  the same worker stream that backs local IPC attach
- session responses plus `GET /api/status` now expose runtime RTSP publication
  facts including URL, state, profile, promised format, actual format, and
  frame counters
- the daemon now accepts a dedicated `--rtsp-port` argument so publication URLs
  are not tied to `8554`
- the repo now vendors `third_party/mediamtx` for local runtime validation

## Scope

- In:
  - runtime-only publication planning for exact-source serving runtimes
  - additive RTSP enablement on top of task-6 shared runtime reuse
  - runtime/status exposure for RTSP publication state and output details
  - live verification with a reachable RTSP server on the development host
  - docs, diagrams, and tracker updates for the first task-8 slice
- Out:
  - durable delivery or publication tables
  - SDK callback delivery
  - frontend controls for RTSP publication
  - grouped multi-track RTSP publication beyond what is explicitly proven

## Action Items

- [x] Lift the donor RTSP input-format planning into `insight-io` runtime form
      without importing donor delivery-table or session-id semantics.
- [x] Add one runtime-only publication object above each shared serving runtime
      that can describe `transport`, `publication_profile`,
      `promised_format`, `actual_format`, URL, state, and last error.
- [x] Start RTSP publication additively when one active consumer requests
      `rtsp_enabled = true`, while preserving the existing IPC attach path for
      the same runtime.
- [x] Stop RTSP publication cleanly when the last RTSP-requiring consumer
      detaches, without breaking non-RTSP consumers that still share the
      runtime.
- [x] Extend REST/status read models so sessions, app sources, and
      `GET /api/status` expose runtime publication facts instead of only the
      durable intent flag.
- [x] Add focused tests for publication planning and reuse state, then run live
      verification against the development hardware with an RTSP server and the
      repo’s strict FFmpeg validation workflow.

## Open Questions

- Should the next RTSP cut support audio publication beyond the already-derived
  catalog addresses, or keep audio runtime publication out until there is a
  concrete consumer need?
- Should grouped sessions publish one member URL per stream in v1, or remain
  IPC-only until a multi-track RTSP contract is documented explicitly?
