# Agent session notes

- Edit this file for short, high-signal context that helps the next session start quickly.
- Keep this file lean; move long milestone history to `docs/project_history.md`.

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
- Added `double.lineplot` widget type with rolling sample buffer and dynamic axis behavior.
- Non-edit mode line plot now has right-click `Reset Graph` for deterministic repro/testing.
- Numeric double widget now supports properties toggle for `Editable` input mode.
- Layout save/load now uses file dialogs, tracks dirty state, and prompts on close with `Yes/No/Cancel`.
- Layout load applies entries to existing session widgets and can instantiate saved widgets immediately at startup.
- Direct client now includes a retained key-value store (shared-memory + mutex + optional file persistence) to provide authoritative direct-table semantics.
- `TryGet/Get` now fall back to retained store on cache miss; this addresses cross-run config retrieval for iterative tuning tests.
- Line plot now supports optional `Show Number Lines` and `Show Grid Lines` with persisted layout properties.
- Line plot rendering refinements: manual Y-limit clipping, endpoint-inclusive tick labels, adaptive decimal precision, and faster Y auto-range response for smaller buffers.
- `StreamsSineWaveDouble` now applies `SweepSeconds` changes on the active run (no extra run required).
- Line plot x-axis ticks now use fixed time anchors and scroll naturally with the time window; reset state keeps default `0..1` axes visible so number/grid lines do not disappear.
- Added gauge-style properties for `double.slider` (`Upper/Lower Limit`, `Tick Interval`, `Show Tick Marks`) with layout persistence and load/apply support.
- Added progress-bar range properties for `double.progress` (`Upper/Lower Limit`) with layout persistence and load/apply support.
- Slider control mapping now honors configured lower/upper bounds for both display updates and command writes.
- Follow-up: progress bar tick settings were removed because Qt `QProgressBar` cannot render true tick marks; progress bar now exposes only upper/lower limits.
- Fixed startup/load ordering issue for `double.progress`: value routing now keys off configured `widgetType` instead of widget visibility, preventing initial chunk-fill misses when tiles are not yet shown.
- Added `SmartDashboard_tests` target and `tests/variable_tile_tests.cpp` regression test (`ProgressBarZeroCentersBeforeWidgetIsShown`) to lock in centered-zero startup behavior.
- Added Windows app icon resource wiring for `SmartDashboardApp.exe` (`dist/win/smartdashboard_app.ico` + `dist/win/app_icon.rc`).
- Stabilized `SmartDashboardClientTests.AssertiveGetPublishesDefaultAndCallbackReceivesUpdates` by using unique per-test direct channels and disabling retained-store fallback in that test.

## Known constraints / active considerations

- Current direct ring transport is effectively single-consumer due to shared read cursor.
- Deployment remains vcpkg/Qt-DLL based; static Qt distribution is not a current goal.
- Event-bus decoupling (topic subscriptions + rate limiting + coalescing) is documented as future work, not implemented.
- Possible future NetworkTables adapter is a design consideration, not current implementation.
- Direct ring payload path is still single-consumer; retained store introduces shared latest-value ownership but does not yet change stream fan-out semantics.

## Next-session checklist

1. Pick one focused roadmap item from `README.md` and `docs/requirements.md`.
2. Define acceptance criteria first, then implement in a small slice.
3. Run automated tests (`docs/testing.md`) plus one targeted manual validation loop.
4. Record durable milestone details in `docs/project_history.md`.
