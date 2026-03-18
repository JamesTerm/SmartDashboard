# Testing Guide

This document explains the automated and manual validation workflows.

- Edit this file when test commands, targets, or expected behaviors change.

## Automated unit tests

The repository includes GoogleTest-based automated tests in:

- `ClientInterface_direct/tests/direct_publisher_tests.cpp`
- `ClientInterface_direct/tests/smartdashboard_client_tests.cpp`

These tests validate core behaviors such as:

- telemetry publish/subscribe for bool/double/string
- deterministic latest-value semantics
- SmartDashboard client facade (`TryGet*`, `Get*(default)`, callback subscriptions)
- command channel receive paths for bool/double/string
- transport parity contract baseline for direct adapter behavior

Additional contract test file:

- `ClientInterface_direct/tests/transport_parity_contract_tests.cpp`

Current parity coverage:

- direct telemetry roundtrip (`bool`/`double`/`string`)
- direct command roundtrip (`bool`/`double`/`string`)
- direct reconnect replay from retained store

### Build and run

1. Configure with tests enabled:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE="D:/code/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

2. Build test target:

```bash
cmake --build build --config Debug --target ClientInterface_direct_tests
```

3. Run all discovered tests:

```bash
ctest --test-dir build --build-config Debug --output-on-failure
```

### Focused test runs

Run one specific test directly:

```bash
build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble
```

Use isolated per-test transport channels if needed:

```bash
set SD_DIRECT_TEST_USE_ISOLATED_CHANNELS=1 && build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble
```

## Manual dashboard integration checks

Use these checks when validating end-to-end UX behavior.

### Live stream check

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. Run a stream test from another terminal:
   - `build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble`
3. Verify in dashboard:
   - state changes to `Connected`
   - tile updates continuously
4. Optional tuning keys exposed by the stream test:
   - `Test/DoubleSine/Config/SweepSeconds`
   - `Test/DoubleSine/Config/AmplitudeMin`
   - `Test/DoubleSine/Config/AmplitudeMax`
   - `Test/DoubleSine/Config/SampleRateMs` (loop delay in milliseconds; lower = higher sample rate)

### Command roundtrip check

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. Run:
   - `build/ClientInterface_direct/Debug/sd_command_roundtrip_sample.exe`
3. In dashboard for `Integration/Armed`:
   - `Change to...` -> `Checkbox control`
   - toggle checkbox
4. Expect sample success message.

### Robot_Simulation transport checks

Use these checks when validating paired dashboard behavior against `Robot_Simulation`.

Detailed simulator-facing transport requirements and chooser contract:

- `docs/robot_simulation_transport_guide.md`

#### Direct mode reconnect check

Use this check when validating dashboard-owned control persistence across simulator restarts.

Suggested loop:

1. Start `Robot_Simulation` in `Direct` mode.
2. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
3. In dashboard menu:
   - `Connection` -> `Use Direct transport`
4. Verify:
   - status transitions to `Connected`
   - direct scalar keys update normally
   - dashboard-owned control values such as `AutonTest`, `Test/AutonTest`, or `TestMove` can be set from the dashboard
5. Restart `Robot_Simulation` without restarting dashboard.
6. Verify after reconnect:
   - status returns to `Connected`
   - remembered control values are replayed/re-published as needed
   - chooser selections continue using `<base>/selected`

#### Legacy NT2 simulator check

Use this check when validating NetworkTables transport against the cloned legacy simulator stack.

Prerequisites:

- no external NT source dependency is required for dashboard build
- robot simulation/server should expose legacy NT2 endpoint on expected host/team settings

Suggested loop:

1. Start your robot simulation in server mode (the same mode that works with official legacy SmartDashboard).
2. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
3. In dashboard menu:
   - `Connection` -> `Use NetworkTables transport`
   - set team/host values as needed
   - `Connection` -> `Connect`
4. Verify:
   - status transitions to `Connected` when simulator is reachable
   - bool/double/string values appear/update from simulator keys
   - writable controls publish updates back to simulator when edited

## Validation expectations

Before merging non-trivial behavior changes:

- run automated tests relevant to changed areas
- run at least one dashboard integration check for user-facing interaction changes
- update docs if test workflow or expected behavior changed
