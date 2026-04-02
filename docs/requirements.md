# SmartDashboard Requirements (Human-Authored)

This document is the human-generated source of truth for product intent.

- Edit this file when requirements or acceptance criteria change.
- Do not use this file as a chronological log; use `docs/project_history.md` for milestones.
- For roadmap, remaining work, and future planning, see `docs/roadmap.md`.

## Purpose

Build an educational, community-friendly C++ dashboard prototype for FRC that demonstrates a clear, high-performance alternative to NetworkTables-style dashboard coupling.

## Product direction

- Primary audience: FRC teams who would otherwise use `SmartDashboard` for daily robot interaction.
- Primary goal: deliver a dependable live dashboard/control workflow teams can adopt without friction.
- Secondary goal: provide built-in recording/replay as a differentiator, without turning the product into an analysis-only tool.
- Comparison baseline:
  - first: `SmartDashboard` for day-to-day team usefulness
  - second: selected `AdvantageScope`-style replay ergonomics where they clearly improve practical debugging
- Planning filter for future work:
  - prioritize features that help teams during normal robot development and match-day use
  - avoid deep analytics work unless it clearly supports real team workflows
  - prefer compatibility and migration ease over purity of internal implementation
  - evaluate outside tools for ideas, but only adopt feature directions that fit this product's own identity
  - study popular dashboards (`Glass`, `Elastic`, and similar tools) for workflow lessons, but avoid cloning another product's feature surface or branding story

## Adoption principle

- Teams should be able to use this dashboard with minimal or no robot-code changes.
- Existing `SmartDashboard`-style publishing patterns should work whenever reasonably possible.
- If the dashboard keeps a cleaner internal/native transport model, provide compatibility/translation adapters so teams are not forced to rewrite working robot code.
- Compatibility and migration smoothness are higher priority than exposing the native protocol directly to teams.

## Primary goals

- Provide reliable two-way communication for bool/double/string telemetry and commands.
- Keep the architecture simple enough for students to reason about end-to-end.
- Support dashboard workflows teams expect: view values, choose widget presentation, save/load layout.
- Make engineering trade-offs visible and teachable through docs and tests.
- Be practical for real FRC teams to adopt as a SmartDashboard-class live dashboard.
- Preserve room for replay/telemetry features that improve debugging without displacing the live-dashboard focus.

## Non-goals (current phase)

- Multi-consumer direct transport support in current ring-buffer mode.
- Production deployment packaging for every team environment.
- Trying to out-feature every dedicated telemetry-analysis tool.

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
   - Dashboard-owned controls should survive reconnects without forcing operators to restart the dashboard.

## Quality requirements

- Unit tests cover transport/client behaviors, including command and telemetry paths.
- Manual validation loop exists for dashboard-integrated checks.
- Documentation explains architecture decisions and historical context for students/mentors.

## Acceptance checklist

- [x] Automated tests exist and run for direct publisher/client workflows.
- [x] Dashboard supports two-way bool/double/string communication.
- [x] Widget type switching works by variable type.
- [x] Editable mode supports move/resize with configurable interaction mode.
- [x] Editable mode prevents control writes.
- [x] Non-editable mode restores control interactions, including gauge command writes.
- [x] Layout serialization persists geometry and widget type.
- [x] Dashboard-owned control values replay/re-publish correctly across simulator reconnects in direct mode.
- [x] NT4 interoperability connects to SmartDashboard, Glass, and Robot_Simulation backends.
- [x] SendableChooser autonomous selection is supported and survives reconnects.
- [x] Camera stream viewing with MJPEG auto-discovery and auto-reconnect.
- [x] Run Browser provides signal navigation with per-leaf visibility control and layout-persisted hidden keys.
- [x] Recording/playback with timeline scrub, seek, and speed control.
- [x] Graph/plot support covers normal SmartDashboard expectations for numeric telemetry.

**Status (2026-03-31):** Core SmartDashboard scope is feature-complete. See `docs/roadmap.md` for active work (multi-trace line plot) and intentionally unsupported features.
