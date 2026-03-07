# SmartDashboard (C++)

Lightweight C++ dashboard for FRC, inspired by WPILib SmartDashboard.

## Why this project

- Built as a community-friendly path forward as legacy SmartDashboard approaches end-of-life (2027).
- Focused scope: fast live values (`bool`, `double`, `string`), editable widgets, and saved layouts.
- Uses a direct local transport layer (`*_direct`) instead of NetworkTables for v1.

## What's in this repo

- `SmartDashboard` - Qt desktop app
- `SmartDashboard_Interface_direct` - subscriber/consumer transport layer
- `ClientInterface_direct` - publisher/producer transport layer + sample publisher
- Design + notes: `design/SmartDashboard_Design.md`, `Agent_Session_Notes.md`

## Quick start (Windows + MSVC + vcpkg)

1. Install Qt6 in vcpkg (at minimum):
   - `vcpkg install qtbase --triplet x64-windows`
2. Configure:
   - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="D:/code/vcpkg/scripts/buildsystems/vcpkg.cmake"`
3. Build:
   - `cmake --build build --config Debug`
4. Run:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`

Optional sample publisher:

- `build/ClientInterface_direct/Debug/sd_direct_publisher_sample.exe`

## Current status

- Core app + direct transport baseline is implemented.
- Runtime deployment is being stabilized for fully reproducible vcpkg-only startup.

For architecture and implementation details, see `design/SmartDashboard_Design.md`.
