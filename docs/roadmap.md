# SmartDashboard Roadmap

Actionable future work for this repository.

- Items move here from `requirements.md` (evaluation/planning) or `Agent_Session_Notes.md` (in-progress).
- When an item is completed, move it to `docs/project_history.md` and remove it from this file.
- Cross-repo items that involve Robot_Simulation are marked with **(cross-repo)**.

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
   - Existing `SmartDashboard`/`NetworkTables` publishing workflows are supported directly or through a clear adapter/translation layer.
   - Acceptance: a team can connect an existing robot project with little or no code churn and see expected values/widgets.

2. **Strong live dashboard baseline**
   - Reliable live telemetry, clear connection state, practical writable controls, and stable layout workflows.
   - Acceptance: teams can use the dashboard confidently during regular robot development and testing.

3. **NetworkTables interoperability and migration smoothness**
   - NetworkTables behavior should feel solid enough that teams do not see this dashboard as a special-case tool.
   - Acceptance: common FRC keys and update patterns behave as teams expect.
   - Architecture direction: legacy ecosystem compatibility should be packaged as optional per-ecosystem transport plugins so teams can keep robot code patterns while deploying only the bridge they need.
   - Current baseline: `Legacy NT` is the first real compatibility plugin and should remain the stable comparison oracle while additional NT-based features are layered carefully on top.

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
      - ~~camera stream support~~ (complete — all phases closed, MJPEG viewer dock with reticle overlay, auto-discovery, auto-reconnect)
     - lightweight alerts/notifications
     - a few more visual control variants where they materially improve migration comfort

### Dream

1. **Deeper analytics only when justified**
   - Add advanced analysis helpers only when they clearly help normal team workflows.
   - Acceptance: each addition saves real time during incident review instead of adding novelty.

2. **Dedicated high-performance telemetry panel**
   - A single panel widget with multiple traces/axes, shared timeline controls, and high-scale rendering — distinct from the per-tile multi-trace line plot.
   - Evaluate how this affects buffer ownership, decimation strategy, rendering batching, and UI layout workflow.
   - Only justified once the per-tile multi-trace line plot is proven in practice and teams need more.

3. **Broader specialty widget surfaces**
   - Explore richer FRC-specific views only after core adoption is healthy.
   - Examples:
     - field/mechanism-style views (`Field2d`, `Mechanism2d`)
     - other specialty semantic widgets that are useful but not foundational

4. **Manipulator arm visualization enhancements** **(cross-repo)**
   - Phase 1 (done): side-view line-strip rendering ported from Curivator (OSG_Viewer DLL, same pattern as SwerveRobot_UI).
   - Future ideas to revisit:
     - **3D cylinders/boxes** — replace line-strip with `osg::ShapeDrawable` cylinders for arm segments, giving depth and visual weight without a full 3D model.
     - **Top-down reach shadow** — project the arm's horizontal extent as a semi-transparent arc/line in the main top-down view, showing how far the arm extends from the chassis.
     - **Side-view inset panel** — render the arm side view into a separate orthographic camera viewport (picture-in-picture), so it doesn't overlap the main field view.
     - **SmartDashboard Mechanism2d widget** — publish the arm chain as a Mechanism2d NT structure so the SmartDashboard (or official one) can render it natively without the OSG viewer.
     - **Color-coded joint stress** — blend joint segment colors based on torque/stall proximity (red = near stall, green = comfortable).
     - **IK goal overlay** — when Phase 2 commands are active, draw a ghost/wireframe of the target pose alongside the current pose to visualize tracking error.

5. **Major UX polish layers**
   - Consider broader visual/design-system sophistication only after the product is already trusted for reliability and workflow fit.

---

## Foundation before enabling NetworkTables broadly

Treat this as the readiness gate before presenting NetworkTables support as a core team-facing feature. **Status: COMPLETE** — all must-have items are checked. Command/Subsystem and LiveWindow were evaluated and intentionally excluded (see "Intentionally unsupported features" below).

### Must-have before broad NT rollout

- [x] Compatibility transport architecture stays optional and ecosystem-scoped (plugin ABI, host-rendered settings UI)
- [x] `SendableChooser`-style autonomous selection is supported
- [x] Layout/edit/save/load workflow feels dependable enough for daily use
- [x] Connection/reconnect/status behavior is trustworthy and unsurprising
- [x] Existing common scalar widgets feel complete for normal team use (bool indicators/text/control, numeric text/bar/slider/dial, string text/edit views)
- [ ] Common `SmartDashboard` publishing patterns are documented as: works unchanged / works through adapter / not yet supported
- [ ] Legacy compatibility baseline is explicit (preserve `legacy-smartdashboard-baseline` behavior profile for validation; allow additive behaviors only when they do not break legacy baseline)
- [ ] Key migration policy is explicit for operator-controlled values (canonical scoped keys preferred, legacy flat aliases remain supported during migration)
- [x] Dashboard-owned control values replay/re-publish correctly across simulator reconnects in direct mode

### High-priority near-foundation items

- [x] Graph/plot support that covers normal SmartDashboard expectations for numeric telemetry
- [x] Camera stream support for teams that rely on driver/diagnostic video in dashboard workflows (MJPEG MVP complete)
- [ ] Visual control variants where they materially improve migration comfort (`Toggle Button`, `Toggle Switch`, voltage-view-style presentation if needed)

### Not required to unblock NT rollout

- ~~Enhanced multi-trace plotting beyond normal SmartDashboard graph expectations~~ (active — see "Multi-trace line plot" section below)
- Deep replay-analysis additions beyond the current practical workflow
- Broader specialty WPILib widgets such as `Field2d`, `Mechanism2d`, or other advanced sendable surfaces
- Features intentionally excluded (compass, command/subsystem, LiveWindow) — see "Intentionally unsupported features" below

### Readiness question

If a typical FRC team points an existing robot project at this dashboard, can they accomplish their everyday dashboard tasks without first asking what is missing? If the honest answer is no, keep the focus on foundation work before advertising NT support broadly.

---

## Done: Abstract camera discovery from transport

Camera auto-discovery currently piggy-backs on the transport variable stream — `MainWindow` forwards every key update to `CameraPublisherDiscovery`, which filters for `/CameraPublisher/` keys. This only works when the active transport happens to deliver those keys as variable updates (NT4 does; Direct and Native Link do not).

Camera discovery is not part of the transport contract and should not be. A Direct-connected session should still be able to discover and connect to camera streams. The fix is to extract camera discovery into its own abstracted service that works independently of which transport plugin is active.

- [x] Define an `ICameraDiscoverySource` interface (or equivalent) that camera discovery providers implement
- [x] Move `CameraPublisherDiscovery` behind that interface as one concrete provider (NT4-style `/CameraPublisher/` key monitoring)
- [x] Wire `CameraViewerDock` to consume the abstract interface instead of being fed by `MainWindow` variable forwarding
- [x] Camera discovery works regardless of active transport plugin (Direct, Native Link, NT4, or none)

---

## Intentionally unsupported features

These SmartDashboard-classic features have been evaluated and intentionally excluded. The existing feature set covers the same workflows through simpler, more composable mechanisms.

### Command/Subsystem status display

SmartDashboard's `putData(CommandScheduler)`, `putData(subsystem)`, and `putData("name", command)` create dedicated Scheduler, Subsystem, and Command widgets with start/cancel buttons and "required by" displays.

**Why excluded**: These Sendable-based widgets add significant protocol complexity (nested sub-key trees, `.type` dispatch for `"Scheduler"`, `"Subsystem"`, `"Command v2"` types, two-way lifecycle state management) for a feature that serves only command-based teams. The same debugging information — which command is running, which subsystem is active — can be published as simple string/bool keys that the existing widget set already handles. The tradeoff favors simplicity and reliability over protocol-level Sendable interop.

### Test Mode / LiveWindow

SmartDashboard's Test Mode displays sensors and actuators grouped by subsystem under `/LiveWindow/` keys, with interactive sliders for motor output and PID parameter tuning.

**Why excluded**: The Run Browser's tile visibility system (hide/show via checkboxes, `hiddenKeys` in layout files) already provides the mechanism-grouping and view-switching workflows that LiveWindow addresses. Instead of a dedicated mode triggered by robot state, users can:
- Create a layout with mechanism/actuator tiles visible and drive/autonomous tiles hidden (the "LiveWindow" view)
- Create a layout with drive/autonomous tiles visible and mechanism internals hidden (the "driver" view)
- Switch between layouts via File > Load Layout — no special protocol support needed

This is demonstrated by the Curivator layout pair in `Robot_Simulation/Design/`: `Curivatorlayout.json` hides 26 `Manipulator/*` keys for a focused driver view, while `Curivatorlayout_all.json` shows all signals including manipulator internals.

PID tuning specifically can be accomplished by publishing P/I/D as writable double keys — the existing editable numeric and slider widgets provide the same interactive control.

### Compass widget

SmartDashboard includes a `Gyro`-type compass widget for heading display.

**Why excluded**: Qt has no native compass/circular gauge widget. The existing `double.gauge` widget (half-arc dial with configurable limits, e.g. -180 to 180) provides heading visualization that covers the practical need. Implementing a dedicated circular compass would require custom QPainter rendering for marginal visual improvement over the gauge.

### Field2d / Mechanism2d

These are Shuffleboard/Glass-specific visualization widgets, not part of the original SmartDashboard feature set. Out of scope per the SmartDashboard-only product direction (see 2026-03-29 project history entry).

---

## Active: Multi-trace line plot

Extend the existing `LinePlotWidget` to support multiple named traces in a single tile. The current widget accepts a single `AddSample(double)` call and draws one trace. Teams commonly need to overlay related signals (e.g. setpoint vs. actual, left vs. right motor output) to compare behavior at a glance.

Architecture direction: option A (many independent lightweight line-plot widgets, extended to multi-trace). A dedicated high-performance telemetry panel (option B) is a separate Dream-tier feature for later.

- [ ] `LinePlotWidget` accepts multiple named traces, each with its own color and sample buffer
- [ ] Trace assignment UI — operator can add/remove traces and assign each to a variable key
- [ ] Per-trace color selection (auto-assigned defaults, user-overridable)
- [ ] Shared x-axis and configurable y-axis (auto-range across all visible traces, or per-trace manual limits)
- [ ] Legend or trace labels visible on the plot for readability
- [ ] Existing single-trace `double.lineplot` behavior is preserved as the default (one trace, no regression)
- [ ] Properties dialog updated for multi-trace configuration and persistence through layout save/load

---

## Open evaluation items

These are architectural directions that need discussion and decisions before implementation begins.

- Define the compatibility/migration contract for teams coming from `SmartDashboard` publishing patterns:
  - what works unchanged
  - what requires an adapter/bridge
  - what remains intentionally unsupported
- Evaluate a future telemetry event bus layer that decouples ingestion from UI rendering:
  - topic-based pub/sub subscription model
  - per-subscriber rate-limited delivery with coalescing
  - central latest-value cache for immediate subscriber bootstrap
  - explicit non-UI-thread ingestion/processing with safe Qt-thread handoff
- Clarify final deployment contract and reproducibility expectations
- Expand test coverage around UI interaction state transitions

---

## Deferred work

Lower-priority items parked for future consideration.

- Wire a UI toolbar/status-bar Connect button
- Write-ack protocol on TCP Publish (currently fire-and-forget)
- Expand smoke test published keys from ~6 + chooser to full TeleAutonV2 (~49 keys)
- Dedicated recorder-to-replay roundtrip test and replay seek correctness test (timeline widget and transport parity tests exist, but no end-to-end record → file → replay-load → seek → verify-state coverage)
- NT4 announce properties ABI forwarding (the NT4 client already parses `properties` from server announce messages; remaining work is wiring `on_topic_announce` through the C ABI and host — deprioritized since Command/Subsystem widgets are intentionally excluded)
