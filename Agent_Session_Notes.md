# Agent session notes

- Edit this file for short, high-signal context that helps the next session start quickly.
- Keep this file lean; move long milestone history to `docs/project_history.md`.
- Commit-note convention: when the user says "update notes", keep this file to handoff-critical context only; put durable feature/change history in `docs/project_history.md`.
- `docs/project_history.md` ordering rule: keep milestone sections in descending chronological order (newest first) so current changes are always at the top.

## Workflow note

- `apply_patch` expects workspace-relative paths (forward slashes). Avoid absolute Windows paths to prevent separator errors.
- Code style uses ANSI/Allman indentation; keep brace/indent alignment consistent with existing blocks to avoid drift.
- Use Windows CRLF line endings for C++ source files in this repo.
- Read nearby `Ian:` comments before editing a file. They mark intentional boundaries or historical lessons that should be preserved unless you deliberately mean to change behavior.

## Documentation and teaching comments rule

- Treat this codebase as both production code and a learning reference.
- Add concise, high-value comments in `.cpp` files when logic is non-trivial (timing behavior, concurrency, transport semantics, state handling, etc.).
- For advanced algorithms/patterns, include the concept name directly in comments where implemented (for example: ring buffer, round-robin, coalescing/latest-value cache, debounce, backoff).
- Keep comments practical and instructional: explain *why* a pattern is used and what trade-off it makes, not just what the line does.
- Avoid noisy comments on obvious code paths; focus comments on places likely to confuse first-time readers.

## Design docs

- Primary design document: `design/SmartDashboard_Design.md`
- Durable milestone history: `docs/project_history.md`
- Replay operator reference: `docs/replay_user_manual.md`
- Replay status/roadmap reference: `docs/replay_parity_roadmap.md`
- Robot simulation transport contract: `docs/robot_simulation_transport_guide.md`

## Quick context for next session

- Repository baseline is local `main`; `feature/replay-dockable-workspace` has been merged.
- Core dashboard architecture is stable: transport-agnostic main window (`Direct`, plugin compatibility transports, `Replay`) + `VariableStore` + Qt widget tiles.
- Editable mode is layout-only; non-editable mode restores live writable controls.
- Layout workflows are in place: file-dialog save/load, dirty tracking, startup apply, and close prompt.
- Direct transport includes retained latest-value fallback for cross-run config/state retrieval.
- Replay stack is broadly in place on `main`:
  - recording to newline-delimited JSON events under `logs/session_<timestamp>.json`
  - replay load path accepts both event-stream and capture-session JSON shapes
  - timeline scrub/zoom/pan, adaptive tick labels, cursor/window readouts, marker jumps, marker dock, keyboard stepping, bookmarks, anomaly markers, and visible-window marker summary
  - dockable `Replay Controls`, `Replay Timeline`, and `Replay Markers` panels with persisted visibility and `Reset Replay Layout`
- `docs/project_history.md` is the authoritative milestone log; keep this file to current-state handoff only.
- Compatibility direction for simulator work is now documented: keep legacy NT behavior as a stable plugin baseline and treat Shuffleboard-oriented additions as additive profiles.
- Current transport-plugin status:
  - `Legacy NT` now runs as a real optional plugin discovered from `plugins/`
  - the old built-in NT transport implementation has been removed from the core app
  - transport capabilities now support shared extensible property queries (`supports_chooser`, `supports_multi_client`)
  - transport connection settings are now described by transport field schemas and rendered by the host in a separate transport-specific settings dialog
  - selected transport identity is shown in status/title text so active backend is visible during testing.
  - manual validation on this branch confirmed the plugin path works well enough to treat it as the new compatibility baseline.
- Direct survive/restart slice is now working again with Robot_Simulation pairing:
  - dashboard startup loads remembered operator-owned control values from `QSettings`
  - remembered values are applied after layout load and again after Direct retained replay so dashboard intent wins over stale startup defaults
  - sequence tracking is cleared before retained replay so synthetic startup `seq=0` values repaint tiles after a dashboard restart
  - retained replay now includes `TestMove` / `Test/TestMove` in addition to the numeric `AutonTest` baseline
  - control edits now persist immediately when dashboard-owned values change instead of waiting for a later session write
- Chooser compatibility slice remains implemented for Direct pairing:
  - chooser metadata routing handles direct string-array `/options`
  - chooser reconnect no longer clobbers robot-owned selection on dashboard reopen
  - chooser tile churn was reduced by avoiding redundant chooser-mode resets on repeated metadata updates.
- New Direct transport/harness status:
  - SmartDashboard direct telemetry now uses independent subscriber read cursors rather than one shared consumed cursor
  - UI callback path was switched from one queued lambda per update to a batched queued drain on the Qt thread
  - process-control helper exists at `tools/smartdashboard_process.py` so sessions can deterministically launch/check/close the dashboard during transport experiments
  - `tools/survive_sequence.py` now automates dashboard-survive followed by robot-survive validation
  - focused CLIs now exist for direct probing/capture:
    - `DirectStateProbeCli` seeds and verifies chooser + `TestMove`
    - `DirectWatchCli` passively records direct updates during a run
  - a fixed harness workspace now places `Test/AutoChooser`, `TestMove`, `Timer`, and `Y_ft` in visible positions for manual paint checks
- Current practical conclusion:
  - real single-dashboard Direct survive now passes again for remembered `TestMove`, chooser survival, and robot restart handoff
  - an attempted inbound-protection tweak in `OnVariableUpdateReceived` was a regression and should stay reverted unless rethought more carefully
  - repeated robot restart stress improved after fixing publisher free-space accounting against the active consumer cursor
  - passive extra observers still expose race/session weaknesses, so transport is still not treated as truly multi-observer safe
  - the short immediate post-dashboard-restart probe window can still miss early telemetry (`Timer` / `Y_ft`), even when the later robot-survive phase passes cleanly
- Next simulator-facing goal: continue Shuffleboard-oriented compatibility work on top of the new plugin boundary, while preserving the stable `Legacy NT` baseline and the real single-dashboard Direct survive path.
- Next architecture/design goal after merge: start a new `Native Link` feature branch for a product-owned generic plugin transport.
  - intent: design a more robust multi-client-capable native transport/plugin example without inheriting all legacy NetworkTables shared-state trade-offs
  - research direction: study strengths of dashboards like `Shuffleboard`, `Glass`, `Elastic`, and similar tools for workflow lessons only, not imitation
  - likely design themes: server authority, explicit topic descriptors, state-vs-command separation, reconnect snapshot + live delta flow, and stronger freshness/ownership semantics.

## Known constraints / active considerations

- Current direct ring transport is effectively single-consumer due to shared read cursor.
- Deployment remains vcpkg/Qt-DLL based; static Qt distribution is not a current goal.
- Event-bus decoupling (topic subscriptions + rate limiting + coalescing) is documented as future work, not implemented.
- If startup false-dirty (`*`) behavior regresses, add a focused startup regression test that validates initial title/dirty state before any editable interaction.

## Next-session checklist

1. Keep `Agent_Session_Notes.md` lean; record durable milestones in `docs/project_history.md`.
2. If replay docs change, keep `docs/replay_parity_roadmap.md`, `docs/replay_user_manual.md`, and `docs/project_history.md` in sync.
3. Pick one focused roadmap item from `README.md` and `docs/requirements.md`.
4. Define acceptance criteria first, then implement in a small slice.
5. Run automated tests (`docs/testing.md`) plus one targeted manual validation loop.
6. Record durable milestone details in `docs/project_history.md`.

## Active follow-up log

- Manual paired testing status with Robot_Simulation Direct:
  - chooser operator flow works (`Just Move Forward` executes correctly)
  - dashboard reopen no longer overwrites robot-owned chooser selection
  - remembered numeric control state (`TestMove`) now survives dashboard restart again in the validated paired flow
  - real single-dashboard restart behavior is much healthier, though immediate telemetry paint after dashboard restart could still use follow-up hardening
  - extra concurrent observer/watch tooling can still perturb repeated runs, which is acceptable for now if direct mode remains effectively single-real-client
- Keep an eye on additional dashboard-owned keys that may need explicit scoped alias conventions documented for mixed legacy layouts.
- Official WPILib SmartDashboard did support `SendableChooser`; upcoming work should treat chooser support as a compatibility requirement, not a Shuffleboard-only feature.
