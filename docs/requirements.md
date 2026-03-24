# SmartDashboard Requirements (Human-Authored)

This document is the human-generated source of truth for product intent.

- Edit this file when requirements or acceptance criteria change.
- Do not use this file as a chronological log; use `docs/project_history.md` for milestones.

## Purpose

Build an educational, community-friendly C++ dashboard prototype for FRC that demonstrates a clear, high-performance alternative to NetworkTables-style dashboard coupling.

## Product direction

- Primary audience: FRC teams who would otherwise use `Shuffleboard`/`SmartDashboard` for daily robot interaction.
- Primary goal: deliver a dependable live dashboard/control workflow teams can adopt without friction.
- Secondary goal: provide built-in recording/replay as a differentiator, without turning the product into an analysis-only tool.
- Comparison baseline:
  - first: `Shuffleboard` for day-to-day team usefulness
  - second: selected `AdvantageScope`-style replay ergonomics where they clearly improve practical debugging
- Planning filter for future work:
  - prioritize features that help teams during normal robot development and match-day use
  - avoid deep analytics work unless it clearly supports real team workflows
  - prefer compatibility and migration ease over purity of internal implementation
  - evaluate outside tools for ideas, but only adopt feature directions that fit this product's own identity
  - study popular dashboards (`Shuffleboard`, `Glass`, `Elastic`, and similar tools) for workflow lessons, but avoid cloning another product's feature surface or branding story

## Adoption principle

- Teams should be able to use this dashboard with minimal or no robot-code changes.
- Existing `SmartDashboard`/`Shuffleboard`-style publishing patterns should work whenever reasonably possible.
- If the dashboard keeps a cleaner internal/native transport model, provide compatibility/translation adapters so teams are not forced to rewrite working robot code.
- Compatibility and migration smoothness are higher priority than exposing the native protocol directly to teams.

## Primary goals

- Provide reliable two-way communication for bool/double/string telemetry and commands.
- Keep the architecture simple enough for students to reason about end-to-end.
- Support dashboard workflows teams expect: view values, choose widget presentation, save/load layout.
- Make engineering trade-offs visible and teachable through docs and tests.
- Be practical for real FRC teams to adopt as a `Shuffleboard`-class live dashboard.
- Preserve room for replay/telemetry features that improve debugging without displacing the live-dashboard focus.

## Non-goals (current phase)

- Full SmartDashboard feature parity.
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

## Current acceptance checklist

- [x] Automated tests exist and run for direct publisher/client workflows.
- [x] Dashboard supports two-way bool/double/string communication.
- [x] Widget type switching works by variable type.
- [x] Editable mode supports move/resize with configurable interaction mode.
- [x] Editable mode prevents control writes.
- [x] Non-editable mode restores control interactions, including gauge command writes.
- [x] Layout serialization persists geometry and widget type.
- [ ] Dashboard-owned control values replay/re-publish correctly across simulator reconnects in direct mode.

## Open items

- Clarify final deployment contract and reproducibility expectations.
- Expand test coverage around UI interaction state transitions.
- Define the compatibility/migration contract for teams coming from `SmartDashboard`/`Shuffleboard` publishing patterns:
  - what works unchanged
  - what requires an adapter/bridge
  - what remains intentionally unsupported
- Evaluate long-term line-plot architecture direction for higher-scale telemetry UX:
  - option A: many independent lightweight line-plot widgets (legacy SmartDashboard style)
  - option B: one high-performance telemetry panel with multiple traces/axes and shared timeline controls
  - define how this choice affects buffer ownership, decimation strategy, rendering batching, and UI layout workflow
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

## Need / Want / Dream

Use these buckets to keep roadmap discussions grounded in product identity.

- `Need`
  - adoption blockers, migration essentials, and daily-driver basics
  - if these are missing, teams will hesitate to switch or will bounce off quickly
- `Want`
  - high-value improvements and differentiators that strengthen the product once the baseline is dependable
  - these should improve real workflows, but are not allowed to destabilize the foundation
- `Dream`
  - interesting future ideas, larger specialty surfaces, or ambitious analysis/polish work
  - these stay intentionally deprioritized until the product is already trusted for everyday use

### Need

1. **Compatibility first**
   - Teams can leave robot code as-is, or very nearly as-is, when adopting this dashboard.
   - Existing `SmartDashboard`/`Shuffleboard`/`NetworkTables` publishing workflows are supported directly or through a clear adapter/translation layer.
   - Acceptance: a team can connect an existing robot project with little or no code churn and see expected values/widgets.

2. **Strong live dashboard baseline**
   - Reliable live telemetry, clear connection state, practical writable controls, and stable layout workflows.
   - Acceptance: teams can use the dashboard confidently during regular robot development and testing.

3. **NetworkTables interoperability and migration smoothness**
   - NetworkTables behavior should feel solid enough that teams do not see this dashboard as a special-case tool.
   - Acceptance: common FRC keys and update patterns behave as teams expect.
   - Architecture direction: legacy ecosystem compatibility should be packaged as optional per-ecosystem transport plugins so teams can keep robot code patterns while deploying only the bridge they need.
   - Current baseline: `Legacy NT` is the first real compatibility plugin and should remain the stable comparison oracle while broader Shuffleboard-oriented additions are layered carefully on top.

4. **High-value everyday widget coverage**
   - Prioritize the widgets teams most commonly need before adding niche analysis surfaces.
   - Include graph/plot support that satisfies normal day-to-day telemetry visibility needs.
   - Include `SendableChooser`-class support as a compatibility requirement.

5. **Dependable migration and operator workflow**
   - Layout/edit/save/load behavior, reconnect handling, and operator-controlled value survival should feel trustworthy.
   - Acceptance: a normal team can accomplish everyday dashboard tasks without first asking what is missing.

### Want

1. **Replay as a differentiator**
   - Keep recording/replay and timeline analysis improving, but in service of practical team debugging rather than tool sprawl.
   - Acceptance: replay remains a clear reason to choose this dashboard over older baseline tools, not a separate product direction.

2. **Enhanced multi-trace plotting**
   - After basic graph compatibility is solid, add a stronger plot experience that can show multiple related signals together when that meaningfully improves debugging.
   - Acceptance: users can inspect several related signals in one plotting surface without losing readability.

3. **High-value operational additions**
   - Add practical features that directly support common team workflows once the foundation is stable.
   - Current likely candidates:
     - camera stream support
     - lightweight alerts/notifications
     - a few more visual control variants where they materially improve migration comfort

### Dream

1. **Deeper analytics only when justified**
   - Add advanced analysis helpers only when they clearly help normal team workflows.
   - Acceptance: each addition saves real time during incident review instead of adding novelty.

2. **Broader specialty widget surfaces**
   - Explore richer FRC-specific views only after core adoption is healthy.
   - Examples:
     - field/mechanism-style views
     - command/subsystem-oriented panels
     - other specialty semantic widgets that are useful but not foundational

3. **Major UX polish layers**
   - Consider broader visual/design-system sophistication only after the product is already trusted for reliability and workflow fit.

## Foundation before enabling NetworkTables broadly

Treat this as the readiness gate before presenting NetworkTables support as a core team-facing feature.

- **Must-have before broad NT rollout**
  - Compatibility transport architecture stays optional and ecosystem-scoped:
    - `Direct` and `Replay` remain built into the core app
    - legacy/interoperability transports are discoverable plugins loaded from `plugins/`
    - each plugin owns its own compatibility scope, reconnect semantics, and multi-client story unless the core contract explicitly guarantees more
    - plugin ABI should prefer a small versioned C interface so examples remain teachable and binary compatibility is easier to preserve across builds
    - plugin settings UI should be host-rendered from transport-declared field metadata rather than by embedding transport-specific Qt UI into the plugin boundary
  - Existing common scalar widgets feel complete for normal team use:
    - bool indicators/text/control
    - numeric text/bar/slider/dial
    - string text/edit views
  - `SendableChooser`-style autonomous selection is supported.
  - Layout/edit/save/load workflow feels dependable enough for daily use.
  - Connection/reconnect/status behavior is trustworthy and unsurprising.
  - Common `SmartDashboard`/`Shuffleboard` publishing patterns are documented as:
    - works unchanged
    - works through adapter/translation layer
    - not yet supported
  - Legacy compatibility baseline is explicit:
    - preserve a stable `legacy-smartdashboard-baseline` behavior profile for validation
    - allow `shuffleboard-additive` behaviors only when they do not break legacy baseline behavior
  - Key migration policy is explicit for operator-controlled values:
    - canonical scoped keys (for example `Test/AutonTest`) are preferred
    - legacy flat aliases (for example `AutonTest`) remain supported during migration
    - read scoped first, then alias fallback where needed

- **High-priority near-foundation items**
  - Graph/plot support that covers normal `Shuffleboard` expectations for numeric telemetry.
  - Camera stream support for teams that rely on driver/diagnostic video in dashboard workflows.
  - Visual control variants where they materially improve migration comfort (`Toggle Button`, `Toggle Switch`, voltage-view-style presentation if needed).

- **Not required to unblock NT rollout**
  - Enhanced multi-trace plotting beyond normal `Shuffleboard` graph expectations.
  - Compass widget.
  - Deep replay-analysis additions beyond the current practical workflow.
  - Broader specialty `Shuffleboard`/WPILib widgets such as `Field2d`, `Mechanism2d`, command/subsystem panels, or other advanced sendable surfaces.

- **Readiness question**
  - If a typical FRC team points an existing robot project at this dashboard, can they accomplish their everyday dashboard tasks without first asking what is missing?
  - If the honest answer is no, keep the focus on foundation work before advertising NT support broadly.

## Next feature acceptance checklist (recording/playback)

- [ ] Dashboard can record bool/double/string telemetry updates to a session file during live operation.
- [ ] Replay mode can load a recorded session and feed updates through the same model/widget flow used for live transports.
- [ ] Global playback controls exist: `play`, `pause`, `seek`, and speed selection (`0.25x`, `0.5x`, `1x`, `2x` minimum).
- [ ] All widgets stay synchronized to one shared global replay cursor.
- [ ] Timeline interactions support scrub, zoom, and pan for match-scale and sub-second analysis.
- [ ] Replay is deterministic: same recording + same cursor position produces the same displayed state.
- [ ] Replay seek performance is indexed or otherwise optimized to avoid full-file replay from time zero on typical jumps.
- [ ] Automated tests cover recorder/replay roundtrip and replay seek correctness.
