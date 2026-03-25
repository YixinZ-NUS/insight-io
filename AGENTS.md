# insight-io Agent Notes

Before proposing or implementing changes, read these documents in order:

1. `docs/README.md`
2. `docs/prd/fullstack-intent-routing-prd.md`
3. `docs/design_doc/INTENT_ROUTING_ARCHITECTURE.md`
4. `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`
5. `docs/tasks/fullstack-intent-routing-task-list.md`
6. `docs/features/fullstack-intent-routing-e2e.json`
7. `docs/past-tasks.md`

## Working Rules

- Treat this repo as the standalone home of the DB-first target-routing
  project, not as a patch layer on the older `insightos` tree.
- Treat the current repository as docs-only unless the docs are explicitly
  updated to reintroduce implementation.
- Keep the canonical source address shape and session graph described in the
  PRD and architecture docs unless those docs are updated first.
- Prefer the simpler durable schema documented in
  `docs/design_doc/INTENT_ROUTING_DATA_MODEL.md`:
  - keep `devices`, `streams`, `apps`, `app_routes`, `app_sources`,
    `sessions`, and `session_logs`
  - treat `streams` as the single per-device preset table
  - do not add migration-history or backward-compat schema layers in v1
  - do not add lower-level runtime tables for capture, delivery, or worker
    runs unless a concrete persistence need has been proven
- Keep the contract that one canonical URI selects one fixed catalog-published
  source shape:
  - exact-member URIs still resolve to one delivered stream
  - grouped preset URIs may resolve to one fixed related stream bundle
- Keep discovery responsible for exposing exact member choices and any grouped
  preset choices, including separate depth entries when backend processing
  changes delivered caps.
- Keep discovery and runtime responsibilities separate:
  - discovery and the catalog publish selectable source shapes and metadata
  - logical, capture, delivery, and worker sessions own runtime realization,
    reuse, and lifecycle
- Use `docs/features/fullstack-intent-routing-e2e.json` as the implementation
  scoreboard and `docs/features/runtime-and-app-user-journeys.json` as the
  broader lifecycle tracker:
  - new end-to-end work must add or update feature entries
  - leave `passes` as `false` until verification has actually run
  - when a feature is implemented, record the exact verification path in
    `docs/past-tasks.md` before flipping `passes` to `true`
- When touching a doc or implementation file, keep a short header comment or
  front-matter block that states the file's role, version or revision marker,
  and major changes, with pointers to the relevant `docs/past-tasks.md`
  entries.
- After any major design or implementation change, sweep the related docs in
  the same change:
  - update investigation or problem-statement docs with their current status,
    such as `open`, `intermittent`, `resolved`, or `archived`
  - periodically archive stale or superseded docs so solved material does not
    read like current guidance
- Add durable architecture diagrams under `docs/diagram/`.
- Subagent Strategy
  - Use subgents liberally to keep main context window clean
  - Offload research, exploration, and parallel analysis to subagents
  - For complex problems, throw more compute at it via subagents
- Never mark a task complete without proving it work. Apply the emperical workflow below.

## Empirical Validation Philosophy

All media-path bug fixes and performance claims **must** be backed by empirical evidence — not just code-level reasoning.

**FFmpeg strict error detection** — when testing any audio/video pipeline that is exposed via RTSP, always use:
```bash
ffmpeg -rtsp_transport tcp -loglevel warning \
    -err_detect +crccheck+bitstream+buffer+careful \
    -i <url> -an -f null /dev/null 2>errors.log
```
This catches bitstream corruption, CRC mismatches, and buffer overreads that `-loglevel error` silently ignores.  A clean run under these flags is the bar for shipping.

**Deterministic micro-benchmarks over probabilistic E2E** — race conditions and timing bugs are often impossible to reproduce reliably in end-to-end tests (e.g., loopback TCP is too fast to trigger a torn read that manifests over a real network).  When a bug is timing-dependent:
1. Write a self-contained C++ micro-benchmark that isolates the race (control writer speed, consumer hold time, buffer depth).
2. Verify the buggy path produces measurable failures (e.g., >0% torn reads).
3. Verify the fix path produces zero failures under the same conditions.
4. Record both results in `docs/past-tasks.md` alongside the fix.

The E2E test is still valuable as a smoke test, but the micro-benchmark is what **proves** correctness.

## Dead-Code & Bloat Removal

Dead-code removal must be **linter-driven**, not intuition-driven.

1. Run cppcheck to produce the hit list.
   Only target what the linter flags — do not guess.
2. Establish an `lcov` coverage baseline before removing anything:
   ```bash
   # Build with coverage
   cmake .. -DCMAKE_CXX_FLAGS="--coverage" -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
            -DCMAKE_SHARED_LINKER_FLAGS="--coverage"
   make -j$(nproc) && ./build/test_helpers
   lcov --capture --directory build --output-file baseline.info --ignore-errors mismatch
   lcov --summary baseline.info --ignore-errors mismatch
   ```
3. For each flagged symbol: remove it, rebuild, run full test suite. Then compare `lcov` coverage
   against baseline — if coverage *drops*, the removed code was untested business logic. **Revert immediately.**
4. Re-run cppcheck after each batch to catch **cascading dead code** — removing function A may leave
   its callees B and C as newly-dead. Repeat until cppcheck is clean (except intentional public API).
5. Clean up coverage artifacts (`*.gcda`, `*.gcno`, `*.info`) and rebuild without `--coverage` when done.
   Coverage instrumentation is a temporary diagnostic, not a permanent build feature.
