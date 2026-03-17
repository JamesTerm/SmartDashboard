# Project History

Curated milestone history for this repository.

- Edit this file for durable project milestones and outcomes.
- Keep lean handoff context in `Agent_Session_Notes.md`.
- Keep milestone sections in descending chronological order (newest first) so recent changes are immediately visible.
- Historical branch/status wording in older entries is time-bound; read each section as a snapshot from that date.

## 2026-03-17 - Direct numeric control recovery, layout-value cleanup, and debugging workflow note

- Stopped layout save/load from persisting live widget values:
  - layout files now keep arrangement and widget configuration only
  - old robot/session values no longer overwrite startup state when the robot is offline.
- Reworked direct simulator probe behavior so it behaves more like a real stream instead of a single burst:
  - repeated publish window with flushes
  - explicit numeric seeding path for `AutonTest` and `TestMove`
  - chooser path renamed to `Test/Auton_Selection/AutoChooser` so chooser experiments no longer collide with legacy numeric `AutonTest`.
- Tightened Direct-mode teaching/debugging workflow after regression hunting with Robot_Simulation:
  - verify the smallest observable first (`AutonTest` populate check)
  - confirm the helper/test path matches the real dashboard ownership model
  - separate chooser and numeric experiments to reduce masked failures
  - use small strategic commits so known-good checkpoints can be reused for regression testing.
- Current paired result with Robot_Simulation:
  - direct numeric populate is working again from the probe path
  - end-to-end paired motion now depends more on harness baseline semantics than on the original command-delivery bug.

## 2026-03-16 - Direct stress harness, session control, and restart hardening

- Added a more systematic Direct-mode debugging workflow for Robot_Simulation pairing:
  - `tools/smartdashboard_process.py` for deterministic launch/check/close control of SmartDashboard
  - `DirectStateProbeCli` to seed and verify chooser + `TestMove`
  - `DirectWatchCli` to passively capture Direct transport updates during smoke runs
- Hardened Direct transport session handling:
  - subscriber path now uses independent read-cursor semantics instead of a single shared consumed cursor
  - UI delivery now batches queued transport updates onto the Qt thread instead of posting one queued lambda per update
  - fixed harness tile placement for `Test/AutonTest/AutoChooser`, `TestMove`, `Timer`, and `Y_ft` to support repeatable visual verification
- Worked jointly with Robot_Simulation transport updates to improve repeated robot-restart behavior:
  - repeated single-dashboard runs are healthier
  - extra concurrent observers still expose race/session weaknesses, so multi-observer robustness remains a later transport-design task
  - `Timer` and `Y_ft` are now the recommended live paint indicators during smoke, while chooser/TestMove remain primarily setup-state indicators.

## 2026-03-15 - Direct transport control replay for simulator reconnect stability

- Added direct transport retained-control replay hook to dashboard transport interface:
  - `IDashboardTransport::ReplayRetainedControls(...)`.
- Implemented retained control replay in direct dashboard transport:
  - caches latest direct variable updates by key (`m_latestByKey`)
  - replays key control values (`AutonTest`, `Test/AutonTest`) on transport start path.
- Added startup and reconnect control republish behavior in main window:
  - publishes current operator control tile values on transport start
  - republishes remembered control values when connection transitions to `Connected`
  - includes chooser `key/selected` publish behavior.
- Added remembered control cache in main window for reconnects without fresh UI edit events.
- Validation:
  - `SmartDashboardApp` builds in Debug.
  - paired manual stress against Robot_Simulation direct mode confirms control values (`AutonTest`, `TestMove`) survive repeated simulator restarts without restarting SmartDashboard.

## 2026-03-15 - Robot simulation transport contract aligned with Shuffleboard-focused direction

- Added dedicated simulator-facing transport handoff guide:
  - `docs/robot_simulation_transport_guide.md`
- Document now captures dual-mode simulator expectation for interoperability work:
  - `Direct` mode for local deterministic integration loops
  - `Legacy NT` mode as compatibility baseline for SmartDashboard/Shuffleboard migration validation
- Compatibility stance is now explicit and reusable across sessions:
  - keep legacy NT behavior stable as the comparison oracle
  - allow Shuffleboard-oriented behavior as additive profile work that must not break legacy baseline behavior
- Added explicit guidance for chooser contract and migration key policy (`scoped preferred + legacy alias fallback`) so Robot_Simulation and dashboard sessions share the same assumptions.

## 2026-03-14 - Documentation synchronization and handoff cleanup

- Reduced `Agent_Session_Notes.md` to handoff-critical current-state context only.
- Reaffirmed documentation split:
  - `Agent_Session_Notes.md` = lean next-session handoff
  - `docs/project_history.md` = durable milestone log
- Refreshed replay documentation to match local `main` state:
  - `docs/replay_parity_roadmap.md` now marks baseline and competitive parity items as implemented
  - advanced replay section now distinguishes implemented vs. still-future items
  - dockable replay workspace note now reflects merged/finalized status instead of in-progress branch language
  - `docs/replay_user_manual.md` now matches the dockable replay workspace layout (`Replay Controls` and `Replay Timeline` panels, status-bar readouts)

## 2026-03-14 - Dockable replay workspace finalized and merged to main

- Merged `feature/replay-dockable-workspace` into local `main` after workspace validation.
- Replay workspace on `main` now includes dockable `Replay Controls`, `Replay Timeline`, and `Replay Markers` panels with persisted visibility/layout behavior.

### Implementation details from the feature branch

- Added dockable replay workspace panels for parity with Replay Markers workflow:
  - `Replay Controls` moved into its own `QDockWidget`
  - `Replay Timeline` moved into its own `QDockWidget`
  - both docks support move/float/close behavior and persist visibility preferences
- Added `View` menu controls for replay workspace composition:
  - `Replay Controls`
  - `Replay Timeline`
  - existing `Replay Markers`
- Added context-menu docking controls for consistency across replay panels:
  - `Float`
  - `Dock Left`
  - `Dock Right`
  - `Dock Bottom`
- Added bottom-dock default-layout restore behavior:
  - choosing `Dock Bottom` on controls or timeline re-docks both panels to bottom and restores side-by-side horizontal split
- Persistence and UX alignment:
  - added `QSettings` visibility keys for controls/timeline (`replay/controlsVisible`, `replay/timelineVisible`)
  - retained deterministic marker-panel visibility persistence and bookmark persistence from previous iteration work
- Validation:
  - built `SmartDashboardApp`
  - manual UX verification confirms dock/float cycling and bottom-layout restore behavior

### Follow-up refinement (same branch/session)

- Added replay workspace recovery affordance:
  - `Reset Replay Layout` action in replay controls/timeline context menus
  - restores controls + timeline to default bottom side-by-side arrangement and re-enables both panels
- Applied persistence hardening parity for controls/timeline panel visibility:
  - aligned persistence/sync behavior with replay-markers deterministic guard model to avoid context-transition clobbering
- Applied controls panel alignment polish:
  - controls row remains top-anchored when floating/stretched to reduce vertical dead-space perception
- Finalized timeline/readout simplification + docked sizing tune:
  - moved replay readouts (`t=`, `window=`) to status bar labels instead of timeline canvas text
  - retained floating timeline ergonomics while reducing docked vertical pressure
  - tuned docked replay height lock ratio to `44/86` (controls/timeline) and wired reset-layout path to same ratio
- Validation:
  - built `SmartDashboardApp`
  - manual operator verification confirmed desired docked replay layout and reset behavior

## 2026-03-13 - Replay UX persistence polish and menu/docking refinements

- Applied operator-driven replay UX polish based on manual walkthrough feedback:
  - moved telemetry feature toggle to `View` (`Enable telemetry recording/playback UI`)
  - moved replay file-open action to `File` (`Replay: Open session file...`) so `Connection` remains transport-focused
- Improved Replay Markers dock affordances and recoverability:
  - added dock context menu actions (`Float`, `Dock Right`, `Dock Left`) for deterministic dock/float transitions
  - retained close/visibility management through `View -> Replay Markers`
- Improved replay usability details:
  - marker list item interaction hardening for click-to-seek reliability
  - timeline auto-follow behavior while playing in zoomed windows (cursor past ~85% pans window forward)
- Added replay analysis persistence behaviors:
  - user bookmarks now persist across sessions (`QSettings` key `replay/userBookmarks`)
  - replay marker panel visibility preference now persists reliably across sessions (`QSettings` key `replay/markersVisible`), including startup/replay-context transitions
  - aligned timeline marker rendering source with Replay Markers panel source so marker list rows and timeline glyphs represent the same merged marker set
- Validation:
  - built `SmartDashboardApp` after each incremental UX/persistence patch
  - manual operator validation confirmed replay marker visibility preference now restores correctly between sessions

## 2026-03-13 - Line-plot axis stabilization follow-up and student notes

- Refined line-plot x-axis stabilization in `SmartDashboard/src/widgets/line_plot_widget.cpp`:
  - retained sample-anchored viewport semantics (`left=oldest retained`, `right=newest retained`)
  - added x tick-step hysteresis (`0.70x..1.60x` hold window) to reduce number-line/gridline oscillation under jittered update cadence
  - kept absolute-time tick anchoring (`floor(min/step) * step`) so grid alignment remains deterministic
- Expanded automated line-plot regression coverage in `SmartDashboard/tests/line_plot_widget_tests.cpp`:
  - strengthened jitter stability scenario by evaluating steady-state (post-buffer-fill) tick behavior
  - added burst/pause/resume anchoring test to verify x-range remains exactly oldest/newest retained sample bounds at full buffer depth
- Added student-facing concept/reference document:
  - `docs/line_plot_notes.md`
  - covers design tradeoffs, anchoring strategies, EMA smoothing rationale, hysteresis concept, and diagnostics ideas for future tuning.

### Follow-up refinement (same session)

- Replay file extension contract aligned in UI and recorder path:
  - replay open dialog now filters to `.json`
  - session recorder now emits `session_<timestamp>.json`
  - replay manual clarifies content remains newline-delimited JSON events (JSONL-style) under `.json` extension
- Replay loader compatibility expanded to accept both schemas:
  - SmartDashboard replay event-stream format (line-delimited event objects)
  - capture CLI session format (`metadata` + `signals[]` series)
  - this resolves "loads with no time" behavior when opening harness-capture JSON directly in replay mode

## 2026-03-12 - Telemetry recording/replay slice and controls refinement

- Added first telemetry recording/replay vertical slice on feature branch `feature/playback-recording-replay`.
- Added replay transport path to dashboard transport contract (`Replay` kind + playback control APIs) so replay behaves as a transport source rather than UI-special-cased data.
- Implemented session recorder in main window flow:
  - live Direct/NetworkTables sessions can write newline-delimited JSON event logs under `logs/session_<timestamp>.json`
  - records bool/double/string updates and connection-state events
  - uses a background writer thread to avoid blocking UI update paths
- Implemented replay file loading and deterministic playback behavior in transport layer:
  - play/pause/seek/rate controls (`0.25x`, `0.5x`, `1x`, `2x` in UI)
  - cursor/duration reporting for timeline binding
  - index/checkpoint-assisted state reconstruction on seek
- Added timeline widget and UI integration:
  - new `PlaybackTimelineWidget` with scrub (left-drag), zoom (wheel), and pan (right-drag)
  - mounted in status bar and synchronized with replay cursor
  - timeline now has explicit discoverability tooltip in app UI
- Added initial automated coverage for timeline behavior:
  - `PlaybackTimelineWidgetTests.ClampsCursorAndWindowToDuration`
- Refined telemetry UX after manual testing:
  - replay tile labels now use compact last-segment display like direct mode
  - added telemetry feature toggle (`Connection -> Enable telemetry recording/playback UI`) to fully hide controls/timeline when not needed
  - recording is now operator-controlled via dedicated record toggle (instead of always recording on connect)
  - record control is disabled/ghosted while replay transport is active
  - compact icon controls now use a combined play/pause button and a separate record indicator button for better timeline real estate

### Follow-up refinement (same session)

- Fixed transport-switch connection-state consistency in `MainWindow`:
  - changing transport kind now stops any active transport first, preventing stale connected-state UI when mode changes
- Updated replay-mode status/title behavior:
  - status bar now shows concise `Replay` text only (to preserve timeline width)
  - window title carries replay file context (`Replay (<filename>)` or `Replay (no file selected)`)
- Updated replay control semantics:
  - replay mode auto-starts when a persisted replay file path exists (no manual Connect required)
  - Connect/Disconnect actions are disabled in replay mode
- Added compact rewind-to-start playback control (`|◀`) for replay workflows:
  - rewinds to cursor `0` and pauses playback
- Improved non-replay visual affordances:
  - play/pause icon now has explicit disabled-state styling so ghosted state is clear
  - replay-only controls (scrub, speed, play/pause, rewind) are visibly ghosted outside replay mode

## 2026-03-12 - Standalone testing-harness capture CLI

- Added new standalone command-driven capture executable target:
  - `SmartDashboardCaptureCli` (`ClientInterface_direct/tools/smartdashboard_capture_cli.cpp`)
  - build wiring added in `ClientInterface_direct/CMakeLists.txt`
- Implemented command-line contract for automated A/B harness runs:
  - required args: `--out`, `--label`, `--duration-sec`
  - preferred args: `--start-delay-ms`, `--sample-ms`, `--overwrite`, `--append`, `--quiet`, `--verbose`, repeatable `--tag k=v`
  - operational args: `--list-signals`, `--signals`, `--stop-file`, `--run-id`
- Implemented runtime and output behavior:
  - clear usage and argument validation with non-zero error exits
  - deterministic metadata-rich JSON output (`schema_version`, `metadata`, `signals`)
  - robust file write path using temp-file + rename for overwrite mode
  - append mode support for multi-run documents in one file
  - console summary includes start/end timestamps, output path, and per-signal sample counts
- Added teaching/documentation coverage:
  - `docs/testing_harness_capture_cli.md` with argument reference and schema sample
  - `README.md` section `Testing Harness Capture CLI` with runnable commands
- Generalized sample naming for clarity in public docs/help text:
  - replaced project-specific names with `example_name_1` and `example_name_2`

### Follow-up refinement (same session)

- Added iteration-1 connection hardening for harness runs that were generating empty output files:
  - direct channel override args: `--mapping-name`, `--data-event-name`, `--heartbeat-event-name`
  - connection wait arg: `--wait-for-connected-ms` (default `2000`)
  - strict data guard arg: `--require-first-sample` (non-zero exit on empty capture)
- Added explicit diagnostics and failure modes:
  - timeout failure when subscriber never reaches `Connected` (`exit code 6`)
  - no-sample failure when strict guard is enabled (`exit code 7`)
  - verbose output includes selected channel names and final connection state
- Updated harness docs and README examples to include connection-safe usage guidance.

- Clarified capture-summary connection semantics to prevent misread healthy runs:
  - reports `Connection observed during capture`
  - reports `Connection state at capture end` (before stop)
  - reports `Post-stop connection state` separately (expected `Disconnected`)
- Added internal automated tests for capture CLI:
  - `ClientInterface_direct/tests/capture_cli_tests.cpp`
  - covers successful capture against custom channel names and connection-timeout failure behavior

## 2026-03-12 - Capture CLI iteration 2 (connection method selection)

- Added `--connect-method <direct|auto>` to `SmartDashboardCaptureCli`.
- Implemented candidate-based connection selection:
  - `direct`: use explicit/direct configured channel path only
  - `auto`: try explicit overrides first (if provided), then known default direct channel families
- Added selected-candidate diagnostics in verbose output for troubleshooting.
- Extended internal tests with auto-mode coverage against legacy-short channel naming.
- Updated README and harness docs with auto-connect usage guidance.

## 2026-03-12 - Replay parity roadmap and user manual

- Added `docs/replay_parity_roadmap.md` to define iterative replay parity goals against modern dashboard workflows.
- Added `docs/replay_user_manual.md` to provide operator instructions for replay mode, timeline controls, and troubleshooting usage patterns.
- Updated `README.md` and session notes to surface these docs for both student learning and practical test workflows.

## 2026-03-12 - Stability checkpoint and branch transition

- Merged `feature/playback-recording-replay` into local `main` after replay + capture CLI validation.
- Created annotated stability tag: `v0.9.0-replay-foundation`.
- At that time, the next iteration branch was `feature/replay-iteration-a-timeline-readability`.
- At that time, the next implementation target was Iteration A from `docs/replay_parity_roadmap.md` (timeline readability and temporal affordances).

## 2026-03-12 - Replay parity iterations A+B (timeline readability, markers, jump workflow)

- Implemented Iteration A timeline readability in `PlaybackTimelineWidget`:
  - adaptive tick marks with time labels that remain readable across broad/narrow zoom windows
  - explicit cursor timestamp readout and visible-window span readout
  - overview strip that shows full replay duration with highlighted current window span
- Added focused automated coverage for timeline readability behavior in `SmartDashboard/tests/playback_timeline_widget_tests.cpp`:
  - `TickStepAdaptsAcrossZoomSpans`
  - `TimeAndSpanLabelsUseReadableFormats`
- Implemented Iteration B marker workflow across transport + UI:
  - replay transport now extracts typed playback markers from `connection_state` and `marker` events (`connect`, `disconnect`, `stale`, generic)
  - transport contract extended with marker retrieval API (`GetPlaybackMarkers`) and shared marker model types
  - timeline renders marker glyphs in both overview and zoomed track views
  - status bar playback controls now include previous/next marker jump actions (`⏮` / `⏭`) that seek replay cursor to adjacent markers
- Validation:
  - built `SmartDashboard_tests` and `SmartDashboardApp`
  - replay timeline test suite passes after A+B changes

## 2026-03-12 - Replay parity iteration C (marker list panel + keyboard stepping)

- Added a replay marker list panel in the main window UI (`Replay Markers` dock):
  - displays marker rows with formatted timestamp, marker kind, and marker label
  - wired click/activation to seek replay cursor directly to selected marker time
  - list selection auto-follows replay cursor by selecting nearest marker at-or-before current cursor
- Added keyboard replay navigation in replay mode:
  - `Left` / `Right` arrows step cursor by `100 ms`
  - `Shift+Left` / `Shift+Right` step cursor by `1 s`
- Marker list panel visibility now follows telemetry replay context:
  - shown only when telemetry UI is enabled and replay transport is selected
- Validation:
  - built `SmartDashboardApp`
  - existing playback timeline tests continue to pass

## 2026-03-12 - Replay parity iteration D (analysis helpers first slice)

- Implemented first analysis-helper slice focused on practical replay forensics workflow:
  - added bookmark capture action in replay controls (`B+`) to create user markers at current cursor time
  - added bookmark clear action (`Bx`) to reset user-added bookmarks quickly during review loops
  - replay marker stream now includes anomaly marker classification:
    - explicit `anomaly` flags in replay events are converted to anomaly markers
    - low-voltage/brownout-style heuristics add inferred anomaly markers for relevant numeric signals
  - timeline marker model/rendering extended with dedicated anomaly kind and distinct visual color
  - replay marker dock adds visible-window summary stats (marker count, anomaly count, bookmark count, window span)
- Preserved and integrated previous Iteration B/C workflows:
  - marker jump controls, marker list click-to-seek, keyboard stepping, and auto-follow selection
- Validation:
  - built `SmartDashboardApp`
  - playback timeline regression tests pass after D-slice changes

## 2026-03-11 - Line-plot smoothing, direct stream cadence tuning, and direct-label compaction

- Improved line-plot smooth-scrolling behavior in `SmartDashboard/src/widgets/line_plot_widget.cpp`:
  - decoupled sample ingest from paint cadence by switching to timer-driven repaint (~16 ms) instead of repaint-on-every-sample
  - replaced jitter-sensitive x-window (`oldest..newest`) with a fixed-width viewport derived from `bufferSize * EMA(samplePeriod)`
  - stabilized x-axis tick spacing (including number-lines mode) using nice-step quantization to avoid frame-to-frame label breathing
- Extended line-plot test coverage in `SmartDashboard/tests/line_plot_widget_tests.cpp` and wired it into `SmartDashboard_tests` target in `SmartDashboard/CMakeLists.txt`.
- Hardened Qt widget test bootstrapping in `SmartDashboard/tests/variable_tile_tests.cpp` by reusing a single `QApplication` instance for multi-test runs.
- Added live publish-cadence tuning to direct stream test `DirectPublisherTests.StreamsSineWaveDouble`:
  - new key `Test/DoubleSine/Config/SampleRateMs` (default `16.0`)
  - updates are command-subscribed and applied on the active run
  - loop delay now uses configured sample rate instead of a hardcoded sleep step
- Added direct-mode UI label compaction in `SmartDashboard/src/app/main_window.cpp`:
  - tile title text shows the final key segment only when transport is Direct (for example `Test/DoubleSine/Config/SampleRateMs` renders as `SampleRateMs`)
  - full variable keys remain unchanged for publish/subscribe, command routing, and layout persistence identity.
- Updated manual testing notes in `docs/testing.md` to include the new `SampleRateMs` tuning key.

### Follow-up refinement (same session)

- Reworked line-plot x-axis semantics to match legacy sample-anchored expectations:
  - right boundary is always the newest sample
  - left boundary is always the oldest retained sample (or first sample before buffer fill)
  - once buffer is full (e.g. 250 samples), the sample 250-back is pinned to the left margin
- Updated sample ingest x-positioning to advance by EMA-estimated sample period per sample rather than raw wall-clock elapsed time, so pause/resume or irregular arrival gaps do not cause violent boundary shifts.
- Added/updated line-plot regression assertions in `SmartDashboard/tests/line_plot_widget_tests.cpp` to verify x-range exactly matches oldest/newest retained sample times.

## 2026-03-10 - Transport abstraction, parity contracts, and legacy NT2 integration

- Added transport-parity contract coverage for direct adapter semantics in:
  - `ClientInterface_direct/tests/transport_parity_contract_tests.cpp`
  - telemetry roundtrip (`bool`/`double`/`string`)
  - command roundtrip (`bool`/`double`/`string`)
  - retained reconnect replay behavior
- Introduced transport-agnostic dashboard boundary:
  - `SmartDashboard/src/transport/dashboard_transport.h`
  - `SmartDashboard/src/transport/dashboard_transport.cpp`
- Refactored main window to use adapter-driven transport control instead of direct-only Qt adapters.
- Added Connection menu workflow in UI:
  - `Connect` / `Disconnect`
  - transport selection (`Direct`, `NetworkTables`)
  - NT endpoint settings (team/host) persisted through `QSettings`
- Removed superseded direct-only Qt transport wrappers:
  - `direct_subscriber_adapter.*`
  - `direct_publisher_adapter.*`
- Integrated legacy NT2 compatibility path for simulator validation by optionally building a local static library from:
  - `D:/code/Robot_Simulation/Source/Libraries/SmartDashboard`
- Added CMake controls for legacy NT2 build wiring:
  - `SMARTDASHBOARD_ENABLE_LEGACY_NT2`
  - `SMARTDASHBOARD_LEGACY_NT2_DIR`
  - compile-time switch `SD_HAS_LEGACY_NT2`
- Preserved fallback behavior: if legacy NT2 sources are unavailable, NetworkTables UI selection remains but uses disconnected stub adapter.
- Updated docs and planning artifacts to align with the new iteration state and interoperability direction:
  - `design/SmartDashboard_Design.md`
  - `docs/testing.md`
  - `README.md`
  - `Agent_Session_Notes.md`

### Follow-up refinement (same session)

- Removed external build dependency on Robot_Simulation SmartDashboard sources.
- Replaced dependency-based NT path with an in-tree NT2-compatible client transport implementation in `dashboard_transport.cpp`.
- Kept Robot_Simulation as runtime interoperability test target to validate protocol compatibility on a second machine with no shared source path assumptions.
- Fixed NT connection UX consistency:
  - choosing `NT: Set host...` now disables team mode
  - choosing `NT: Set team...` enables team mode
- Added NT key normalization for legacy server namespace prefix:
  - incoming `/SmartDashboard/<key>` is normalized to `<key>` for layout/widget key matching
- Fixed NT command/write interoperability for existing keys:
  - write path now sends `FIELD_UPDATE` for known entries using server-provided entry ids
  - keeps `ENTRY_ASSIGNMENT` only for first-time keys
  - aligns client writes with legacy NT2 server expectations (resolves editable numeric writeback failures)

## 2026-03-10 - Legacy XML import parity and widget property refinements

- Added File menu action `Import Legacy XML...` to load legacy SmartDashboard `.xml` layouts directly into the current dashboard session.
- Implemented legacy XML parser and widget mapping for common classes:
  - `SimpleDial` -> `double.gauge`
  - `ProgressBar` -> `double.progress`
  - `CheckBox` -> `bool.checkbox`
  - `BooleanBox` -> `bool.led`
  - `TextBox` / `FormattedField` -> text/numeric mappings based on type
- Added import issue reporting that appears only when unsupported classes/properties are encountered.
- Extended legacy-property handling and parity:
  - progress bar `Foreground` / `Background` color import support
  - `Font Size` import support for text-oriented widgets
  - removed false-positive import warnings for now-supported progress bar colors and font size
- Refined bool checkbox presentation:
  - default hide label behavior
  - added checkbox `Show Label` property toggle
  - compact checkbox tile width when label is hidden
- Refined bool LED behavior:
  - default off-state now renders visibly (no blank appearance before first update)
- Expanded progress bar behavior and properties:
  - default hide percentage text
  - added `Show Percentage` property toggle
  - added `Foreground` and `Background` color properties with color picker dialogs
  - applied custom `QProgressBar` styling so fill/chunk renders with configured colors and visible minimum thickness
  - reduced label-to-bar vertical spacing to match legacy compact look and allow bar area to expand with tile height
- Added text font-size configurability in properties for:
  - numeric text widgets (`double.numeric`, including editable mode)
  - non-edit text widgets (`bool.text`, `string.text`, `string.multiline`)
  - editable text widget (`string.edit`)
- Extended layout persistence/load-apply for new properties:
  - `progressBarShowPercentage`
  - `progressBarForegroundColor`
  - `progressBarBackgroundColor`
  - `boolCheckboxShowLabel`
  - `textFontPointSize`

## 2026-03-10 - Progress startup fix, test hardening, and Windows icon

- Fixed `double.progress` startup/render ordering issue where initial chunk fill could be missing when value updates arrived before widget visibility settled; value display routing now uses configured `widgetType` semantics.
- Added SmartDashboard widget regression test coverage:
  - new `SmartDashboard_tests` target
  - `tests/variable_tile_tests.cpp`
  - `VariableTileTests.ProgressBarZeroCentersBeforeWidgetIsShown` guards centered-zero startup behavior.
- Hardened direct client test isolation for retained/cache edge cases:
  - `SmartDashboardClientTests.AssertiveGetPublishesDefaultAndCallbackReceivesUpdates` now uses unique per-test direct channel names
  - retained-store fallback is disabled in that test to guarantee empty-start assertive-get semantics.
- Added embedded Windows app icon resources for `SmartDashboardApp.exe`:
  - `SmartDashboard/dist/win/smartdashboard_app.ico`
  - `SmartDashboard/dist/win/app_icon.rc`

## 2026-03-10 - Layout title/save UX and load-mode options

- Added current-layout awareness to window title for JSON layouts:
  - title now includes loaded layout name without extension
  - unsaved edits are indicated with `*`.
- Refined layout save UX into explicit actions:
  - `Save / Update Layout` saves directly to the active JSON layout path when available
  - `Save Layout As...` always prompts for a destination.
- Updated close-save prompt wording to reflect current-layout semantics (`Save changes to current layout?`).
- Fixed startup false-dirty behavior by limiting layout-dirty marks from tile move/resize/property events to editable mode only.
- Added load-mode menu split without changing core load behavior:
  - `Load Layout (Merge)` keeps existing merge semantics
  - `Load Layout (Replace)` clears widgets first, then loads selected layout.

## 2026-03-10 - Runtime app/taskbar icon parity

- Extended icon integration beyond EXE resources so runtime UI surfaces use the same icon:
  - added Qt resource manifest `SmartDashboard/src/resources/resources.qrc`
  - included icon asset alias for runtime lookup (`:/app/icon.ico`)
  - set application and main window icon in startup path (`QApplication::setWindowIcon` + `window.setWindowIcon`).
- Result: titlebar/taskbar icon now aligns with embedded EXE icon on launch.

## 2026-03-09 - Gauge editing workflow and layout persistence

- Finalized line-plot axis behavior updates:
  - x-axis tick labels are now tied to fixed time values and scroll left with advancing time (no frame-to-frame relabel jitter)
  - reset graph state now preserves visible default `0..1` x/y axes so number lines and grid lines remain visible with no samples
- Added gauge-style properties for non-gauge numeric widgets:
  - `double.progress`: `Upper Limit`, `Lower Limit`, `Tick Interval`, `Show Tick Marks`
  - `double.slider`: `Upper Limit`, `Lower Limit`, `Tick Interval`, `Show Tick Marks`
- Extended slider/control behavior to respect configured slider range for both UI->command mapping and value->UI normalization.
- Added layout save/load coverage for new properties:
  - `progressBarLowerLimit`
  - `progressBarUpperLimit`
  - `sliderLowerLimit`
  - `sliderUpperLimit`
  - `sliderTickInterval`
  - `sliderShowTickMarks`
- Clarified `double.progress` capabilities: Qt `QProgressBar` does not support rendered tick marks, so progress-bar properties are now limited to upper/lower range bounds.

- Refined `double.lineplot` behavior and readability:
  - manual Y-axis bounds now clip out-of-range sample rendering
  - added optional axis rendering controls:
    - `linePlotShowNumberLines`
    - `linePlotShowGridLines`
  - improved tick generation to endpoint-inclusive, pixel-driven spacing on both axes when number lines are enabled
  - improved tick label formatting with adaptive decimal precision (tenths by default, hundredths when needed)
  - aligned grid-line positions with rendered tick positions
  - improved Y auto-range responsiveness for smaller buffer sizes
- Updated `StreamsSineWaveDouble` timing semantics so `SweepSeconds` edits apply on the active run.
- Added direct retained-table semantics to `SmartDashboardClient` for authoritative latest-value ownership in direct mode:
  - shared-memory retained store with named mutex for cross-process coordination
  - optional retained file persistence path (default under `%LOCALAPPDATA%`)
  - `TryGet/Get` now fall back to retained store when local cache misses
  - local `Put*` writes retained entries with sequence/timestamp metadata
- Added automated restart regression coverage:
  - `SmartDashboardClientTests.RetainedStoreRestoresValuesAcrossClientRestart`
  - validates cross-client restart recovery for bool/double/string values
- Added new `double.lineplot` widget type with custom Qt paint rendering and rolling sample buffer.
- Implemented line-plot behavior for telemetry visualization:
  - starts with normalized `[0..1]` style axis baseline
  - x-axis head tracks elapsed time and slides once sample buffer is full
  - y-axis auto-expands to observed min/max including negatives
- Added line-plot properties and persistence:
  - `linePlotBufferSizeSamples` (default `5000`)
  - `linePlotAutoYAxis`
  - `linePlotYLowerLimit`
  - `linePlotYUpperLimit`
- Added line-plot non-edit mode context menu action: `Reset Graph`.
- Added numeric double widget property `Editable` with direct text-entry command publishing.
- Added value persistence for bool/double/string tile values in layout JSON.
- Expanded layout save/load UX and lifecycle behavior:
  - save/load now uses file chooser dialogs (`.json`)
  - remembers last-used layout path in app settings
  - tracks dirty layout changes and prompts on close (`Yes/No/Cancel`)
  - load now applies to existing tiles and creates missing saved tiles at startup/load
  - load behavior was adjusted to merge/apply (clear remains explicit action via menu)
- Updated `StreamsSineWaveDouble` test flow for iterative UI tuning:
  - publishes config keys for amplitude min/max and sweep seconds
  - supports command-path updates while streaming
  - keeps stress/repopulate workflow stable under repeated runs

- Expanded editable tile context menu behavior (editable-only):
  - `Change to...`
  - `Properties...`
  - `Send To Back`
  - `Reset Size` (disabled when already at default)
  - `Remove`
- Added removable tile workflow by wiring tile-level remove action to `MainWindow` tile map/layout state cleanup.
- Added gauge-specific properties dialog and behavior:
  - upper/lower limit
  - tick interval
  - show tick marks toggle
- Updated gauge normalization and publish mapping to use configured lower/upper limits instead of fixed `-1..1`.
- Added layout persistence for gauge properties in serializer/load path:
  - `gaugeLowerLimit`
  - `gaugeUpperLimit`
  - `gaugeTickInterval`
  - `gaugeShowTickMarks`
- Refined editable visual affordances:
  - no always-on edit chrome; outline appears only when hovering active tile
  - kept cursor-based directional resize/move hints
  - active drag/resize outline brightens and thickens for immediate feedback
  - gauge no longer appears disabled/inverted in editable mode while still blocking value manipulation

## 2026-03-08 - Reliability, UX, tests, and docs maturation

- Added automated GoogleTest coverage for direct publisher streaming and SmartDashboard client facade behavior.
- Stabilized latest-value semantics and reconnect handling (sequence reset tracking and rollback recovery).
- Implemented bidirectional command channel support and writable controls:
  - `bool.checkbox`
  - `double.slider`
  - `string.edit`
- Added command roundtrip sample and documented manual validation loop.
- Added single-instance app guard and clear-widgets action for iterative testing.
- Expanded widget presentation options and improved layout ergonomics.
- Added editable canvas move/resize workflows with snap-to-grid and interaction mode controls.
- Enforced layout-only safety while editable is on (no value writes from controls).
- Restored runtime interaction in non-editable mode, including gauge command writes and interaction cursor hints.
- Added educational and process documentation:
  - `docs/history.md`
  - `docs/requirements.md`
  - `docs/testing.md`
  - `docs/development_workflow.md`
  - `docs/ai_development_guidelines.md`
- Clarified README structure with architecture overview, status, development approach, and roadmap.

## 2026-03-07 - Baseline architecture and app scaffold

- Established root CMake workspace with three projects:
  - `SmartDashboard`
  - `SmartDashboard_Interface_direct`
  - `ClientInterface_direct`
- Implemented direct transport foundation with shared-memory ring buffer, events, framing, and heartbeat/state handling.
- Built initial Qt dashboard shell with variable tiles, type-based widget switching, and JSON layout save/load.
- Added in-app `VariableStore` to separate latest-value model state from widget instances.

## Forward-looking considerations (documented, not implemented)

- Optional NetworkTables adapter via transport interface extension.
- Optional telemetry event bus between ingestion and UI with:
  - topic-based subscriptions
  - rate-limited delivery and coalescing
  - latest-value cache bootstrap for new subscribers
  - explicit non-UI-thread processing with safe Qt-thread handoff
- Optional telemetry recording/playback pipeline for analysis and debugging:
  - recorder subscribes to telemetry events and writes timestamp/topic/value logs
  - playback engine re-injects recorded events through the same event path as live data
  - timeline model tracks playback position, duration, and speed
  - future UI controls can bind to timeline state (play/pause/scrub) without changing widget data semantics

## Retained store follow-up checklist (short)

- Define conflict policy for retained writes when multiple producers publish the same key (source priority vs seq/timestamp precedence).
- Specify retained-schema behavior for type changes on existing keys (reject, migrate, or last-write-wins with type replacement).
- Add explicit retained GC/limits policy (max entries, eviction strategy, and diagnostics when evictions occur).
- Add transport-parity contract tests so direct/NT/TCP adapters share the same `TryGet/Get` retained semantics.
- Clarify stream fan-out roadmap: retained latest-value ownership is shared now, but ring payload consumption is still single-consumer.
