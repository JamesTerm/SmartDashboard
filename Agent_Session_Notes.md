# Agent session notes

## Workflow note

- `apply_patch` expects workspace-relative paths (forward slashes). Avoid absolute Windows paths to prevent separator errors.
- Code style uses ANSI/Allman indentation; keep brace/indent alignment consistent with existing blocks to avoid drift.
- Use Windows CRLF line endings for C++ source files in this repo.

## Design docs

- Primary design document: `design/SmartDashboard_Design.md`

## Progress checkpoint (2026-03-07)

- Workspace and project scaffolding are in place: root CMake + three projects (`SmartDashboard`, `SmartDashboard_Interface_direct`, `ClientInterface_direct`).
- `*_direct` transport now includes a Win32 shared-memory ring buffer, named events, message framing, and basic heartbeat/state handling.
- Dashboard UI baseline is implemented in Qt:
  - main window shell, connection status indicator, editable mode
  - variable tile widgets for bool/double/string
  - right-click `Change to...` options by type
  - layout save/load JSON with variable key + widget type + geometry
- Added a lightweight in-app variable store (`VariableStore`) to separate latest-value model state from widget instances.
- Publisher/subscriber stubs were replaced with transport-backed implementations for iterative testing.

## Runtime deployment note (current blocker)

- App code builds, but Qt runtime deployment still needs final hardening for clean vcpkg-only reproducibility.
- Current CMake behavior on Windows:
  - prefers vcpkg copy-based deployment for vcpkg Qt builds
  - keeps `qt.conf` with `Plugins=plugins`
  - copies plugin folders under `plugins/`
- Remaining issue: some optional companion DLLs (`dxcompiler.dll`, `dxil.dll`, etc.) are not present in this vcpkg install, and plugin init diagnostics are still being finalized.

## Resolved deployment pitfall (important)

- Root cause of Qt startup crash was a plugin ABI mismatch: Qt6 app DLLs with Qt5 platform plugins.
- Symptom in `QT_DEBUG_PLUGINS=1` output:
  - `Plugin uses incompatible Qt library (5.15.0) [debug]`
  - `Could not find the Qt platform plugin "windows"`
- Correct vcpkg plugin source for Qt6 is:
  - Debug: `<vcpkg>/installed/<triplet>/debug/Qt6/plugins`
  - Release: `<vcpkg>/installed/<triplet>/Qt6/plugins`
- Avoid copying from `<vcpkg>/installed/<triplet>/debug/plugins` when both Qt5 and Qt6 are installed, because this can pull Qt5 plugins into a Qt6 app.
- Deployment hygiene for new Qt projects:
  - keep `qt.conf` with `Plugins=plugins`
  - clear destination `plugins/` before copying to prevent stale leftovers
  - remove/avoid `Qt5*.dll` in Qt6 app output folders

## Next session plan

1. Run/collect `QT_DEBUG_PLUGINS=1` output on a clean deploy folder.
2. Decide final vcpkg-only deployment contract (no cross-project DLL sourcing).
3. Lock deployment script behavior and verify F5 startup from Visual Studio.
