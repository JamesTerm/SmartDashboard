# Project History

Curated milestone history for this repository.

- Edit this file for durable project milestones and outcomes.
- Keep lean handoff context in `Agent_Session_Notes.md`.

## 2026-03-09 - Gauge editing workflow and layout persistence

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
