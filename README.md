# SmartDashboard (C++)

Lightweight C++ dashboard for FRC, inspired by WPILib SmartDashboard.

## Overview

- Built as a community-friendly path forward as legacy SmartDashboard approaches end-of-life (2027).
- Focused scope: fast live values (`bool`, `double`, `string`), editable widgets, and saved layouts.
- Uses native `Direct` and `Replay` transports in the core app, with optional compatibility transports loaded as plugins.
- Product direction: become a practical `Shuffleboard`-class dashboard replacement first, while keeping built-in replay/telemetry as a strong differentiator.
- Adoption principle: teams should be able to keep existing robot publishing patterns whenever possible; compatibility/translation adapters are preferred over forcing robot-code rewrites.
- Product research is selective, not imitative: we study ideas from dashboards like `Shuffleboard`, `Glass`, `Elastic`, and other popular tools, then adopt only the parts that fit this product's identity.

## Transition Strategy

- `Direct` and `Replay` stay native to the core app.
- Compatibility ecosystems are handled as optional plugins so teams can bring only the bridge they need.
- Current baseline plugin is `Legacy NT`, which exists to reduce migration friction for teams that do not want to rewrite robot code immediately.
- Long-term, this plugin architecture also creates room for a future native generic transport plugin that is designed around this project's own goals instead of inheriting every historical trade-off of NetworkTables-style shared state.

## Architecture Overview

Telemetry flows through a simple staged pipeline so data movement and UI behavior are easy to reason about.

```text
Robot/App Publisher (direct transport v1)
                |
                v
Telemetry Ingestion
                |
                v
Processing Layer
  - Sequence/reconnect handling
  - Latest-value update coalescing
  - UI-facing normalization
                |
                v
State Cache (VariableStore)
                |
                v
Qt UI Rendering (tiles/widgets)
```

Notes:
- The dashboard now routes telemetry/commands through a transport-agnostic interface in the UI layer.
- Direct transport is implemented and fully test-backed.
- Legacy NetworkTables compatibility now lives in the in-repo `Legacy NT` plugin instead of being baked into the core executable.

Future consideration (not implemented yet):
- introduce a decoupled telemetry event bus between ingestion and UI rendering
- support topic subscriptions (for example `/robot/gyro`, `/robot/battery`, `/robot/arm/angle`)
- allow per-subscriber rate limits with update coalescing (for example 20 Hz, 10 Hz, 5 Hz)
- keep a latest-value cache so new subscribers receive current state immediately
- keep ingestion/processing off the Qt UI thread and deliver UI updates via safe Qt signal/slot boundaries

## What's in this repo

- `SmartDashboard` - Qt desktop app
- `SmartDashboard_Interface_direct` - subscriber/consumer transport layer
- `ClientInterface_direct` - publisher/producer transport layer + sample publisher

## Documentation map

- `docs/requirements.md` - human-authored requirements and acceptance criteria (what/why)
- `design/SmartDashboard_Design.md` - technical architecture and implementation details (how)
- `docs/development_workflow.md` - incremental development/review/validation loop used in this repo
- `docs/testing.md` - automated test suite and manual validation commands
- `docs/ai_development_guidelines.md` - expectations for responsible AI-assisted development
- `docs/history.md` - FRC dashboard/telemetry historical context for students
- `docs/project_history.md` - curated repository milestone history
- `docs/testing_harness_capture_cli.md` - command-line telemetry capture for automated test harness runs
- `docs/replay_parity_roadmap.md` - replay/timeline parity roadmap versus modern dashboard workflows
- `docs/replay_user_manual.md` - operator-focused replay feature usage guide
- `docs/robot_simulation_transport_guide.md` - simulator-side transport contract for `Direct` vs `Legacy NT` integration and chooser compatibility
- `Agent_Session_Notes.md` - lean next-session handoff context

## Replay Training

Use `docs/replay_user_manual.md` as the single training document for replay workflows.

It includes:

- replay mode setup and control reference
- timeline usage (scrub/zoom/pan) and readability cues
- marker workflows (`⏮` / `⏭`, marker list click-to-seek)
- keyboard stepping (`Left/Right`, `Shift+Left/Right`)
- analysis helpers (`B+` bookmark add, `Bx` bookmark clear, anomaly markers)
- guided training walkthrough and incident-analysis flow

## Development Approach

Development in this repository is intentionally iterative and validation-driven:

- architecture planning and requirements capture before implementation
- incremental implementation in small, reviewable slices
- unit testing and targeted manual integration checks
- iterative refinement of UX and architecture based on feedback

AI is used as a development assistant for drafting and acceleration, not as an autonomous code generator.
Human review, validation, and acceptance criteria remain required for non-trivial changes.

See `docs/development_workflow.md` and `docs/ai_development_guidelines.md` for process details.

## Project Status

- Early prototype
- Exploring dashboard architecture trade-offs
- Exploring AI-assisted development workflows in a student-mentored setting

Current product-direction priorities:

- compatibility with existing team publishing workflows
- foundation-first readiness before broad `Legacy NT` / compatibility-plugin rollout
- strong live dashboard usefulness compared with `Shuffleboard`
- `SendableChooser`-class compatibility and other common team workflows
- reconnect-safe dashboard-owned controls during simulator/robot restarts
- improved plotting, including future multi-trace line-plot support
- replay/telemetry features that strengthen debugging without taking over the product

Current focus is architecture validation, workflow clarity, and test-backed behavior, not feature completeness.

## Quick start (Windows + MSVC + vcpkg)

1. Install Qt6 in vcpkg (at minimum):
   - `vcpkg install qtbase --triplet x64-windows`
2. Configure:
   - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="D:/code/vcpkg/scripts/buildsystems/vcpkg.cmake"`
3. Build:
   - `cmake --build build --config Debug`
4. Run:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`

## Automated tests

This repo includes an automated GoogleTest suite for direct transport and client behaviors.

1. Configure with tests enabled:
   - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE="D:/code/vcpkg/scripts/buildsystems/vcpkg.cmake"`
2. Build tests:
   - `cmake --build build --config Debug --target ClientInterface_direct_tests`
3. Run tests:
   - `ctest --test-dir build --build-config Debug --output-on-failure`

See `docs/testing.md` for test scope and focused test commands.

Optional sample publisher:

- `build/ClientInterface_direct/Debug/sd_direct_publisher_sample.exe`
- Command roundtrip sample (manual dashboard writeback check):
  - `build/ClientInterface_direct/Debug/sd_command_roundtrip_sample.exe`
- Tests now publish to the same default direct channel used by the dashboard (good for live manual testing).
- To force isolated per-test channels instead:
  - `set SD_DIRECT_TEST_USE_ISOLATED_CHANNELS=1 && build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble`

## Live test loop

Use this loop to validate end-to-end publisher/subscriber behavior:

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. In a second terminal, run one stream test (shared channel default):
   - `build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble`
3. Confirm behavior in the app:
   - connection state transitions to `Connected`
   - tiles are created/updated for the streamed key(s)
   - values continue updating during the test window
4. Re-run the same test to confirm reconnect behavior:
   - existing tiles should continue updating after publisher restart

## Manual command roundtrip check

Use this check for dashboard -> app/client command flow:

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. Run roundtrip sample:
   - `build/ClientInterface_direct/Debug/sd_command_roundtrip_sample.exe`
3. In dashboard, for key `Integration/Armed`:
   - right-click tile -> `Change to...` -> `Checkbox control`
   - check the box
4. Expected result:
   - sample exits with success (`Roundtrip command received. Success.`)

## Windows build note (vcpkg + PowerShell)

- This repo sets Visual Studio global `VcpkgXUseBuiltInApplocalDeps=true` in `CMakeLists.txt` to avoid recurring `pwsh.exe` lookup noise on machines that only have Windows PowerShell.
- If you copy this setup into other projects, you can reuse the same `CMAKE_VS_GLOBALS` setting for cleaner MSBuild output.
- Optional Qt runtime companion DLL misses (`dxcompiler.dll`, `dxil.dll`, `opengl32sw.dll`) are hidden by default to keep build output clean.
- Enable deploy diagnostics only when needed with `-DSMARTDASHBOARD_VERBOSE_QT_DEPLOY=ON`.

## Roadmap

Potential next steps:

- additional telemetry widgets and interaction patterns
- richer layout customization and editing ergonomics
- performance profiling and update-path optimization
- compatibility-plugin hardening and broader transport extensibility
- improved deployment packaging and reproducibility
- decoupled telemetry event bus with topic subscriptions and rate-limited UI delivery

For architecture and implementation details, see `design/SmartDashboard_Design.md`.
For FRC dashboard/telemetry background and architectural evolution, see `docs/history.md`.

## Testing Harness Capture CLI

For automation-first perf testing (A/B harness runs), this repo includes a standalone capture executable:

- target: `SmartDashboardCaptureCli`
- exe path (Debug): `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe`

Example commands:

- example_name_1 worker ON prefetch OFF:
  - `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe --out runs/example_name_1_worker_on_prefetch_off.json --label "example_name_1 worker ON prefetch OFF" --duration-sec 45 --tag app=example_name_1 --tag worker=on --tag prefetch=off`
- example_name_1 worker OFF prefetch OFF:
  - `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe --out runs/example_name_1_worker_off_prefetch_off.json --label "example_name_1 worker OFF prefetch OFF" --duration-sec 45 --tag app=example_name_1 --tag worker=off --tag prefetch=off`
- example_name_2 worker OFF prefetch OFF:
  - `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe --out runs/example_name_2_worker_off_prefetch_off.json --label "example_name_2 worker OFF prefetch OFF" --duration-sec 45 --tag app=example_name_2 --tag worker=off --tag prefetch=off`

Connection-safe variant (recommended for harness automation):

- `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe --out runs/example_name_1_worker_on_prefetch_off.json --label "example_name_1 worker ON prefetch OFF" --duration-sec 45 --wait-for-connected-ms 5000 --require-first-sample`

Auto-channel variant (use when channel family is uncertain):

- `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe --out runs/example_name_1_worker_on_prefetch_off.json --label "example_name_1 worker ON prefetch OFF" --duration-sec 45 --connect-method auto --wait-for-connected-ms 5000 --require-first-sample`

If your publisher uses non-default direct channel names, also pass:

- `--mapping-name <name> --data-event-name <name> --heartbeat-event-name <name>`

Console summary note:

- `Post-stop connection state` is expected to be `Disconnected` because subscriber shutdown has already happened.
- Use `Connection observed during capture` and `Connection state at capture end` to judge run health.

See `docs/testing_harness_capture_cli.md` for full argument reference and JSON schema details.
