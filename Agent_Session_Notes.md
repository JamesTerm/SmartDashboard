# Agent session notes

- Edit this file for short, high-signal context that helps the next session start quickly.
- Keep this file lean; move long milestone history to `docs/project_history.md`.
- Commit-note convention: when the user says "update notes", keep this file to handoff-critical context only; put durable feature/change history in `docs/project_history.md`.

## Workflow note

- `apply_patch` expects workspace-relative paths (forward slashes). Avoid absolute Windows paths to prevent separator errors.
- Code style uses ANSI/Allman indentation; keep brace/indent alignment consistent with existing blocks to avoid drift.
- Use Windows CRLF line endings for C++ source files in this repo.

## Documentation and teaching comments rule

- Treat this codebase as both production code and a learning reference.
- Add concise, high-value comments in `.cpp` files when logic is non-trivial (timing behavior, concurrency, transport semantics, state handling, etc.).
- For advanced algorithms/patterns, include the concept name directly in comments where implemented (for example: ring buffer, round-robin, coalescing/latest-value cache, debounce, backoff).
- Keep comments practical and instructional: explain *why* a pattern is used and what trade-off it makes, not just what the line does.
- Avoid noisy comments on obvious code paths; focus comments on places likely to confuse first-time readers.

## Design docs

- Primary design document: `design/SmartDashboard_Design.md`

## Quick context for next session

- Current architecture: direct transport (`*_direct`) + `VariableStore` + Qt widget tiles.
- Two-way bool/double/string command/telemetry path is implemented and unit tested.
- Editable mode supports move/resize workflows and intentionally blocks value writes.
- Non-editable mode restores writable controls (including interactive gauge command writes).
- Layout save/load now uses file dialogs, tracks dirty state, and prompts on close with `Yes/No/Cancel`.
- Layout load applies entries to existing session widgets and can instantiate saved widgets immediately at startup.
- Direct client now includes a retained key-value store (shared-memory + mutex + optional file persistence) to provide authoritative direct-table semantics.
- `TryGet/Get` now fall back to retained store on cache miss; this addresses cross-run config retrieval for iterative tuning tests.
- Progress-bar startup behavior is stabilized by routing value updates via configured `widgetType` (not transient visibility state).
- `SmartDashboard_tests` target exists with `tests/variable_tile_tests.cpp` regression coverage for centered-zero progress-bar startup behavior.
- App icon is now wired for Windows builds via `dist/win/app_icon.rc` and `dist/win/smartdashboard_app.ico`.
- Runtime Qt icon is also set via `src/resources/resources.qrc` + `QApplication::setWindowIcon`, so titlebar/taskbar icon paths match the EXE icon.
- `AssertiveGetPublishesDefaultAndCallbackReceivesUpdates` test isolation uses unique per-test direct channels and disables retained fallback for that case.
- Line plot x-axis model refined to sample-anchored behavior: right edge is newest sample, left edge is oldest retained sample (up to configured buffer size), so the 250th sample back stays pinned to the left margin once buffer is full.
- Line plot ingest now advances x-position by EMA-estimated sample period per sample (instead of raw wall-clock gaps), preserving smooth left/right anchoring through pause/resume and irregular transport timing.
- Number-line/gridline stability work remains centered on x-axis spacing and anchoring; rendering path itself is stable.
- Added `SmartDashboard/tests/line_plot_widget_tests.cpp` stress-oriented regression coverage for varying buffer/rate scenarios; `SmartDashboard_tests` now includes both line-plot and variable-tile tests.
- `DirectPublisherTests.StreamsSineWaveDouble` now exposes live-tunable `Test/DoubleSine/Config/SampleRateMs` (default 16 ms) so publish cadence can be adjusted without editing code.
- Direct transport UI label compaction: tile title text now shows only the last key segment in Direct mode (for example `.../Config/SampleRateMs` -> `SampleRateMs`) while preserving full underlying keys for publish/subscribe and layout identity.
- Telemetry recording/playback vertical slice is now implemented on branch `feature/playback-recording-replay`:
  - recorder writes live Direct/NT bool/double/string events to `logs/session_<timestamp>.jsonl`
  - replay transport can load session files and drive existing widget/model flow with play/pause/seek/speed
  - timeline scrub/zoom/pan control exists in status bar and is wired to replay cursor
- Telemetry UI controls were refined for operator workflow:
  - menu toggle to enable/disable telemetry UI entirely (`Connection -> Enable telemetry recording/playback UI`)
  - compact transport controls now use icon-style play/pause and record indicators
  - record control is disabled/ghosted in replay transport
  - replay label compaction now matches direct mode (shows last key segment only)
- Replay workflow/connection semantics were further refined:
  - switching transport kind now tears down active transport first so stale "Connected" state does not persist across mode changes
  - replay mode status bar now shows only `Replay` (filename kept in title bar)
  - replay mode window title shows selected replay filename (or `no file selected`)
  - replay mode auto-starts when a persisted replay file path exists; Connect/Disconnect actions are disabled in replay mode
- Telemetry controls got additional UX polish:
  - playback controls/scrub are ghosted when not in replay mode
  - added rewind-to-start control (`|◀`) that pauses playback and seeks to t=0
  - play/pause icon now has explicit disabled-state styling so ghosting is visually obvious
- Added standalone testing-harness capture CLI target `SmartDashboardCaptureCli`:
  - source: `ClientInterface_direct/tools/smartdashboard_capture_cli.cpp`
  - build target wired in `ClientInterface_direct/CMakeLists.txt`
  - exe path (Debug): `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe`
  - supports required args (`--out`, `--label`, `--duration-sec`) and preferred ops (`--start-delay-ms`, `--sample-ms`, `--overwrite`, `--append`, `--quiet`, `--verbose`, repeatable `--tag`)
  - supports orchestration args (`--list-signals`, `--signals`, `--stop-file`, `--run-id`)
  - writes stable metadata + signal-series JSON schema with robust temp-file replace on overwrite mode
- Capture CLI iteration-1 connection hardening added for empty-log troubleshooting:
  - direct channel override args: `--mapping-name`, `--data-event-name`, `--heartbeat-event-name`
  - startup gating arg: `--wait-for-connected-ms` (default `2000`)
  - strict non-empty guard: `--require-first-sample` (fails non-zero on empty capture)
  - verbose output now includes connection diagnostics and selected channel names
  - summary now distinguishes `Connection state at capture end` vs `Post-stop connection state` to avoid false confusion when post-stop is `Disconnected`
- Iteration-2 started on capture connection method selection:
  - new arg `--connect-method <direct|auto>`
  - `auto` tries explicit overrides first (if set), then known default direct channel families
  - this is intended to reduce empty-run risk when publisher channel family is uncertain
  - external validation note: healthy captures may end with `Connection state at capture end: Stale` and `Post-stop connection state: Disconnected`; this is acceptable when `Connection observed during capture: true` and sample counts are non-zero
- Added internal automated coverage for capture CLI behavior:
  - `ClientInterface_direct/tests/capture_cli_tests.cpp`
  - validates successful capture on custom direct channels and timeout failure path
- New teaching/user docs for harness usage:
  - `docs/testing_harness_capture_cli.md`
  - README section `Testing Harness Capture CLI`
  - examples intentionally use generic placeholders (`example_name_1`, `example_name_2`) to avoid project-specific confusion
- Added replay planning + user-facing docs:
  - `docs/replay_parity_roadmap.md` (iterative parity checklist and acceptance path)
  - `docs/replay_user_manual.md` (operator workflow guide for replay controls and timeline interactions)

## Known constraints / active considerations

- Current direct ring transport is effectively single-consumer due to shared read cursor.
- Deployment remains vcpkg/Qt-DLL based; static Qt distribution is not a current goal.
- Event-bus decoupling (topic subscriptions + rate limiting + coalescing) is documented as future work, not implemented.
- Main window now uses a transport-agnostic adapter interface (`dashboard_transport`) with Connection menu actions (Connect/Disconnect, Direct vs NetworkTables selection, NT host/team settings persisted in QSettings).
- NetworkTables menu path now uses an in-tree NT2-compatible client implementation (socket + message framing/handshake), so no external source dependency is required for build.
- Robot_Simulation is used as interoperability validation target, not as a build-time library dependency.
- NT UX tweak: setting explicit host now automatically disables `NT: Use team number`; setting team auto-enables team mode.
- NT key normalization: incoming keys with `/SmartDashboard/` prefix are normalized to layout keys by stripping that prefix on ingest.
- NT write-path fix: existing keys now publish as `FIELD_UPDATE` (0x11) using server-assigned entry id/seq, while first-time keys publish as `ENTRY_ASSIGNMENT` (0x10) under `/SmartDashboard/` wire namespace.
- Direct ring payload path is still single-consumer; retained store introduces shared latest-value ownership but does not yet change stream fan-out semantics.
- If startup false-dirty (`*`) behavior regresses, add a focused startup regression test that validates initial title/dirty state before any editable interaction.

## Next-session checklist

1. Pick one focused roadmap item from `README.md` and `docs/requirements.md`.
2. Define acceptance criteria first, then implement in a small slice.
3. Run automated tests (`docs/testing.md`) plus one targeted manual validation loop.
4. Record durable milestone details in `docs/project_history.md`.
