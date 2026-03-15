# Robot Simulation Transport Guide

This guide is the handoff document for the separate `Robot_Simulation` session.

Goal: make `Robot_Simulation` a reliable interoperability target for SmartDashboard transport work while preserving legacy behavior as a known-good baseline.

## Scope and intent

- `Robot_Simulation` should support two selectable transport modes:
  - Direct transport (native local shared-memory/event path)
  - Legacy NetworkTables transport (NT2-compatible behavior used for SmartDashboard/Shuffleboard interoperability)
- Keep legacy NT behavior stable so old SmartDashboard remains a comparison oracle when debugging regressions.
- Treat Shuffleboard-facing behavior as additive on top of legacy compatibility, not a replacement for it.

## Required connection modes

Implement explicit runtime mode selection in `Robot_Simulation`:

- `Direct` mode
  - publishes bool/double/string keys over direct channels
  - used for fast local integration and deterministic dashboard development
- `Legacy NT` mode
  - publishes over legacy NT2-compatible endpoint that current SmartDashboard NT adapter expects
  - this is the compatibility baseline for migration confidence

Optional later extension:

- `NT profile` setting under legacy NT mode:
  - `legacy-smartdashboard-baseline` (strict old behavior)
  - `shuffleboard-additive` (extra keys/widgets while preserving baseline semantics)

## Data contract Robot_Simulation should publish

### Scalar baseline (must-have)

Publish representative bool/double/string telemetry and writable command keys that mirror real robot workflows.

- bool example: `Integration/Armed`
- double example: `Integration/Speed`
- string example: `Integration/Status`

These validate the foundation transport and widget paths first.

### SendableChooser baseline (must-have for this slice)

Publish chooser metadata/value topics at one chooser base key.

Base key example: `Test/AutoChooser`

Required topics:

- `<base>/.type` string = `String Chooser`
- `<base>/options`
  - preferred on NT path: string array
  - acceptable temporary fallback: comma-separated string
- `<base>/default` string
- `<base>/active` string
- `<base>/selected` string

Selection writeback expectation:

- dashboard publishes operator choice to `<base>/selected`
- simulator should observe and apply selection changes

## Backward-compatibility strategy

Use a dual-profile mindset:

- Legacy profile is the contract anchor.
- Shuffleboard-oriented additions are permitted only if they do not break legacy profile behavior.

Practical rule:

- if a behavior differs, keep legacy profile deterministic and explicitly gate additive behavior behind profile/mode toggles.

## Test and validation references

### Unit/integration tests already in this repo

- chooser publisher contract:
  - `ClientInterface_direct/tests/direct_publisher_tests.cpp`
  - test: `DirectPublisherTests.StreamsStringChooserTopics`
- chooser widget behavior:
  - `SmartDashboard/tests/tile_control_widget_tests.cpp`
  - `SmartDashboard/tests/variable_tile_tests.cpp`

### Dashboard-side protocol handling references

- NT value decode (including string-array support for chooser options):
  - `SmartDashboard/src/transport/dashboard_transport.cpp`
- chooser topic mapping and UI routing:
  - `SmartDashboard/src/app/main_window.cpp`

### Manual validation loop

1. Start `Robot_Simulation` in selected mode (`Direct` or `Legacy NT`).
2. Start dashboard: `build/SmartDashboard/Debug/SmartDashboardApp.exe`.
3. Select matching transport in dashboard (`Connection` menu).
4. Verify:
   - connection transitions to `Connected`
   - scalar keys update and writable commands roundtrip
   - chooser appears as one dropdown tile (no extra metadata tiles)
   - changing dropdown writes to `<base>/selected` and simulator reacts

## Protocol notes for implementers

- This dashboard currently expects NT2-style wire behavior in its NT adapter path.
- Incoming NT keys with `/SmartDashboard/` prefix are normalized to layout keys by stripping that prefix.
- For chooser options, NT string-array payloads are now supported and preferred over flattened strings.

If deeper protocol details are needed in the separate session, use:

- NT3 spec reference (entry types and message framing):
  - https://raw.githubusercontent.com/wpilibsuite/allwpilib/main/ntcore/doc/networktables3.adoc

## Suggested first tasks in Robot_Simulation session

1. Add transport mode switch (`Direct` vs `Legacy NT`) with clear startup logging of active mode.
2. Implement shared scalar key publish set in both modes.
3. Implement chooser publish set in both modes (with NT string-array options on NT path).
4. Add a simulator-side assertion/log when `<base>/selected` changes.
5. Run manual loop against dashboard for both modes and record results.
