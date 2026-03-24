# Persistence Debugging Notes

This note captures the places we had to inspect while debugging SmartDashboard startup values that looked like persistence bugs, especially around `TestMove`, chooser selection, and Native Link startup.

## The important distinction

There are two different ways a value can show up on startup:

- SmartDashboard-local persistence
  - dashboard-owned remembered values restored from `QSettings`
  - now compile-time gated off by default; older notes here describe the original `Direct`-only model
- transport-retained state
  - authority-owned retained/live values replayed by the selected transport
  - applies even when local persistence is empty.

The most important lesson from this pass was that a startup value can look like a persistence bug even when the real source is retained transport state.

## Places to check when a value keeps coming back

### 1. `QSettings` / registry state

First check whether SmartDashboard actually persisted the value locally.

- key path:
  - `HKCU\Software\SmartDashboard\SmartDashboardApp\directRememberedControls`
- fields per entry:
  - `key`
  - `valueType`
  - `value`

What we found:

- old stale values really were present there during the first bug pass
- automated tests also wrote `TestMove=3.5` into the real user hive until we fixed test isolation.

### 2. Startup transport selection

If the app starts on `direct`, it can legitimately replay retained command/control state from the direct transport path.

Check:

- `connection/transportId`
- `connection/transportKind`

Relevant code:

- `SmartDashboard/src/app/main_window.cpp`
- `SmartDashboard/src/app/startup_instance_gate.cpp`

### 3. Main window remembered-control load/save/apply paths

These are the SmartDashboard-local persistence entry points.

Relevant code:

- `MainWindow::LoadRememberedControlValues()`
- `MainWindow::SaveRememberedControlValues()`
- `MainWindow::ApplyRememberedControlValuesToTiles()`
- `MainWindow::RememberControlValueIfAllowed()`

Relevant file:

- `SmartDashboard/src/app/main_window.cpp`

Rules we locked in:

- remembered controls were scoped to `Direct`, but are now disabled by default behind `SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS`
- telemetry updates must not create remembered values
- only explicit local control edits may create or update remembered values when that feature is enabled.

### 4. Control edit handlers

These are the intended persistence writers for operator-owned controls.

Relevant code:

- `MainWindow::OnControlBoolEdited()`
- `MainWindow::OnControlDoubleEdited()`
- `MainWindow::OnControlStringEdited()`

If a value shows up in remembered settings without going through a local widget edit, that is a bug.

### 5. Telemetry update path

This was the subtle bug source.

Relevant code:

- `MainWindow::OnVariableUpdateReceived()`

What we found:

- incoming transport updates for operator widgets could still flow into remembered-control state on the `Direct` path
- that made authority-owned retained values look like local SmartDashboard persistence.

Fix:

- incoming telemetry still updates tiles
- incoming telemetry may still be published back to the command path in `Direct` where required by the legacy behavior
- but telemetry no longer creates or refreshes remembered control entries.

### 6. Reconnect/startup cache refresh path

Relevant code:

- `MainWindow::StartTransport()`

What we found:

- startup/reconnect code was refreshing remembered values from current tiles
- if not constrained, that can silently convert transport-retained startup state into remembered persistence.

Fix:

- only refresh entries that already existed in `m_rememberedControlValues`
- do not invent new remembered entries from startup tile state.

### 7. Layout loading

Relevant files:

- `SmartDashboard/src/layout/layout_serializer.cpp`
- `Robot_Simulation/Design/Swervelayout.json`

What we verified:

- layout files persist widget configuration and geometry
- they do not persist live `TestMove`, `Timer`, or chooser-selected values.

The layout can still create a tile that visually exists on startup, which is why we added the tile placeholder/no-data behavior.

### 8. Tile/widget default presentation

Relevant files:

- `SmartDashboard/src/widgets/variable_tile.cpp`
- `SmartDashboard/src/widgets/tile_control_widget.cpp`

What we found:

- pre-existing layout tiles could look alive before a real authority update arrived
- slider/chooser widgets especially made this easy to misread as persistence.

Fix evolution:

- tiles gained an explicit `No data` placeholder state until a real value arrives
- current stable behavior now seeds temporary UI-only defaults for selected widget classes so startup/layout reload is usable without reviving persistence.

### 9. Direct transport retained replay

Relevant code:

- `DirectDashboardTransport::ReplayRetainedControls()`
- `m_latestByKey` update cache in the same class

Relevant file:

- `SmartDashboard/src/transport/dashboard_transport.cpp`

What we found:

- `Direct` intentionally replays retained control values like `TestMove`
- this replay is transport state, not SmartDashboard persistence.

So if `TestMove` still comes back after clearing `directRememberedControls`, inspect this path next.

### 10. Direct shared-memory publisher retained snapshot behavior

Relevant file:

- `ClientInterface_direct/src/sd_direct_publisher_stub.cpp`

What we found:

- the direct publisher keeps a retained map and replays it when a consumer appears/reappears
- this is another place where a value can survive without touching SmartDashboard settings.

### 11. Authority-side default seeds

Relevant files:

- `Robot_Simulation/Source/Application/DriverStation/DriverStation/NativeLinkAuthorityHelpers.cpp`
- `SmartDashboard/plugins/NativeLinkTransport/src/native_link_tcp_test_server.cpp`
- `Robot_Simulation/Source/Application/DriverStation/DriverStation/TransportSmoke.cpp`

What we found:

- some reference/test authorities intentionally seed defaults like `TestMove=0.0`
- some smoke/probe flows intentionally seed `TestMove=3.5`
- those values are valid retained authority truth during those scenarios and should not be confused with dashboard persistence.

### 12. Probe and test pollution

Relevant files:

- `SmartDashboard/tests/main_window_persistence_tests.cpp`
- `SmartDashboard/tests/dashboard_transport_registry_tests.cpp`
- `SmartDashboard/tools/native_link_tcp_runtime_probe.py`

What we found:

- tests/probes can write realistic retained values (`TestMove=3.5`) on purpose
- tests originally polluted the user's real `QSettings` hive.

Fix:

- persistence tests now snapshot and restore the user's settings around each run.

## Fast triage checklist next time

When a startup value looks sticky:

1. Clear/query `directRememberedControls` first.
2. Check selected startup transport (`direct`, `native-link`, etc.).
3. Confirm whether the tile is showing `No data` before any update.
4. Inspect `OnVariableUpdateReceived()` for accidental telemetry-to-persistence writes.
5. Inspect `StartTransport()` for startup cache refresh that may be reclassifying retained values.
6. Inspect `ReplayRetainedControls()` and the underlying transport retained cache.
7. Inspect authority/test-server seeds before concluding the dashboard persisted anything.

## Regression coverage added during this pass

Relevant tests:

- `MainWindowPersistenceTests.NativeLinkTelemetryUpdatesDoNotPopulateRememberedControls`
- `MainWindowPersistenceTests.DirectTelemetryUpdatesDoNotCreateRememberedControls`
- `MainWindowPersistenceTests.RememberedControlsStayDisabledByDefaultEvenOnDirect`
- `MainWindowPersistenceTests.ClearWidgetsThenReloadLayoutReappliesTemporaryDefaults`
- `VariableTileTests.TemporaryDefaultYieldsToFirstLiveValue`
- `VariableTileTests.EmptyTemporaryStringDefaultSuppressesNoDataPlaceholder`
- `TileControlWidgetTests.DoubleSliderEmitsEditedValueWhenInteractive`

Those tests should be the first place to extend if another startup/persistence bug appears.
