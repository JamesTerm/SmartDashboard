# SmartDashboard Requirements (Human-Authored)

This document is the human-generated source of truth for product intent.

- Edit this file when requirements or acceptance criteria change.
- Do not use this file as a chronological log; use `Agent_Session_Notes.md` for that.

## Purpose

Build an educational, community-friendly C++ dashboard prototype for FRC that demonstrates a clear, high-performance alternative to NetworkTables-style dashboard coupling.

## Primary goals

- Provide reliable two-way communication for bool/double/string telemetry and commands.
- Keep the architecture simple enough for students to reason about end-to-end.
- Support dashboard workflows teams expect: view values, choose widget presentation, save/load layout.
- Make engineering trade-offs visible and teachable through docs and tests.

## Non-goals (current phase)

- Full SmartDashboard feature parity.
- Multi-consumer direct transport support in current ring-buffer mode.
- Production deployment packaging for every team environment.

## Functional requirements

1. **Telemetry ingestion**
   - Dashboard receives bool/double/string updates from direct transport.
   - Latest value per key is displayed.

2. **Widget model**
   - Widgets can be switched among type-compatible presentations.
   - Defaults:
     - bool -> LED indicator
     - double -> numeric text
     - string -> text label

3. **Bidirectional controls**
   - Writable control widgets exist for each supported type.
   - Dashboard publishes commands back to app/client via command channel.

4. **Layout editing**
   - Editable mode supports tile placement and sizing.
   - Save/load layout persists widget type, key mapping, and geometry.
   - In editable mode, value manipulation is disabled (layout-only safety).

5. **Runtime interaction rules**
   - In non-editable mode, writable widgets and interactive gauge can issue commands.
   - Read-only widgets never emit value changes.

6. **Stability and reconnect behavior**
   - Sequence reset/reconnect paths should recover and continue updates.

## Quality requirements

- Unit tests cover transport/client behaviors, including command and telemetry paths.
- Manual validation loop exists for dashboard-integrated checks.
- Documentation explains architecture decisions and historical context for students/mentors.

## Current acceptance checklist

- [x] Automated tests exist and run for direct publisher/client workflows.
- [x] Dashboard supports two-way bool/double/string communication.
- [x] Widget type switching works by variable type.
- [x] Editable mode supports move/resize with configurable interaction mode.
- [x] Editable mode prevents control writes.
- [x] Non-editable mode restores control interactions, including gauge command writes.
- [x] Layout serialization persists geometry and widget type.

## Open items

- Clarify final deployment contract and reproducibility expectations.
- Expand test coverage around UI interaction state transitions.
- Evaluate a future telemetry event bus layer that decouples ingestion from UI rendering:
  - topic-based pub/sub subscription model
  - per-subscriber rate-limited delivery with coalescing
  - central latest-value cache for immediate subscriber bootstrap
  - explicit non-UI-thread ingestion/processing with safe Qt-thread handoff
- Evaluate telemetry recording/playback architecture for post-run analysis:
  - non-blocking recorder that writes timestamp/topic/value events to disk
  - playback engine that replays recorded events through the same event bus contract
  - transport-agnostic playback controls (`play`, `pause`, `seek`, speed)
  - timeline model exposing current time, duration, and playback speed
  - UI-ready integration points for future playback controls (without coupling UI to data source type)
