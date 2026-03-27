# Agent session notes

- Keep this file short and handoff-focused.
- Move durable milestone history to `docs/project_history.md`.
- Move verbose findings, root-cause investigations, and debugging narratives to `docs/journal/<date>-<slug>.md` (descending date order, newest first).
- Once a topic is fully resolved and no longer needs to be in the foreground, move it to the journal. Do not let completed work accumulate here.

## Workflow

- **Use CRLF line endings** for all source files (`.cpp`, `.h`, `.cmake`, `.ps1`, `.py`, `.md`, `.rc`, `.gitignore`). Both repos standardized CRLF as of the NT4 transport merge.
- Read nearby `Ian:` comments before editing and add new ones where transport, protocol, lifecycle, or ownership lessons would be expensive to rediscover.
- Never mix `SetEnvironmentVariableA` (Win32 write) with `_dupenv_s` (CRT read) for the same variable. Use `GetEnvironmentVariableA` on the read side to match the Win32 write.
- `gtest_discover_tests` calls all use `DISCOVERY_MODE PRE_TEST`.
- `vcpkg.json` manifest in repo root auto-installs all C++ dependencies (qtbase, ixwebsocket) during CMake configure. No manual `vcpkg install` needed — just pass `-DCMAKE_TOOLCHAIN_FILE=...` and the manifest handles the rest.
- Solution Explorer folder grouping is in place (`USE_FOLDERS ON`).
- Debug logs only write when `--instance-tag` is passed or `SMARTDASHBOARD_INSTANCE_TAG` env var is set. Logs go to `.debug/native_link_ui_<tag>.log`.
- SmartDashboard settings are persisted in Windows Registry under `HKCU:\Software\SmartDashboard\SmartDashboardApp`.

## Build

```bash
# Configure (vcpkg.json manifest auto-installs Qt6, ixwebsocket, etc.)
cmake -G "Visual Studio 17 2022" -B build -DCMAKE_TOOLCHAIN_FILE="<your-vcpkg-root>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows

# Build
cmake --build build --config Debug

# Test (197 tests, 2 disabled)
ctest -C Debug --output-on-failure
```

## Key invariants (do not break)

- `RegisterDefaultTopics` stays minimal — robot-code telemetry keys auto-register on first server write.
- `IsHarnessFocusKey` in `main_window.cpp` is an intentional narrow allowlist for the debug log only. All keys still create tiles and receive updates.
- The alive guard (`m_alive` shared_ptr) in `PluginDashboardTransport::Stop()` must be set false before calling the plugin's stop, not after.
- TCP client `Start()` is non-blocking and always returns `true`. Failure manifests as a `Disconnected` callback. The host reconnect timer handles retries.
- `OnDisconnectTransport` must route through `OnConnectionStateChanged`, not `UpdateWindowConnectionText` directly. The full pipeline (title, menu enable states, recording event) must all fire together on any state transition.
- **Host-level auto-reconnect:** All plugin transports make a single connect attempt per `Start()` call. The reconnect timer in `MainWindow` (`m_reconnectTimer`, 1-second single-shot) drives retries via Stop()+Start() cycles. Plugins must NOT implement their own retry loops.
- **WSAStartup is deferred:** Winsock is initialized only when a WebSocket-based transport actually connects (via `ix::initNetSystem()`). Direct/NativeLink sessions never trigger it.

## Transport architecture

### Plugin system

Plugins live under `plugins/<Name>Transport/` and implement the C ABI in `dashboard_transport_plugin_api.h`. See the `Ian:` comment at the top of that file for the checklist of adding a new transport.

Current plugins:
| Plugin | ID | Protocol | Status |
|---|---|---|---|
| LegacyNtTransport | `legacy-nt` | NetworkTables v2 TCP | Stable |
| NativeLinkTransport | `native-link` | Custom TCP/SHM | Stable |
| NT4Transport | `nt4` | NT4 WebSocket | Stable, merged |

### Auto-connect

- The `auto_connect` connection field descriptor stays in each plugin's descriptor table so the UI checkbox works. The host reads it via `MainWindow::IsAutoConnectEnabled()` from `pluginSettingsJson`.
- `m_userDisconnected` flag distinguishes manual Disconnect (suppress retries) from transport-initiated drops (retry if auto-connect enabled).

### Write-back

- `PublishBool/Double/String` in the plugin ABI is fully wired for NT4. The `EnsurePublished` path in `nt4_client.cpp` uses the `/SmartDashboard/` prefix.
- `supports_chooser` returns true. This property controls whether inbound updates are assembled into chooser widgets — it does NOT gate outbound publish.
- `RememberControlValueIfAllowed` only works for Direct transport (`CurrentTransportUsesRememberedControlValues()` returns true only for `TransportKind::Direct`).

## Cross-repo sync

- This repo and `D:\code\Robot_Simulation` share the Native Link contract and the NT4 protocol layer.
- Canonical rollout strategy: `docs/native_link_rollout_strategy.md` (this repo).
- When either repo's session notes change, check the other side for consistency.

## Complete: Run Browser dock (`feature/run-browser-dock`)

Dockable tree panel for browsing signal key hierarchies. The Run Browser is an **optional navigational filter** — it never blocks tile creation or visibility by default.

### Two-mode design

| | Reading mode (Replay) | Streaming / Layout-mirror mode (Live) |
|---|---|---|
| Transport kinds | `TransportKind::Replay` | Direct, NativeLink, NT4, LegacyNT |
| Tree population | Up front from JSON parse (`AddRunFromFile`) | Driven by layout tile lifecycle (`OnTileAdded` / `OnTileRemoved`) |
| Default visibility | **Off** — user opts in via checkboxes | **On** — everything visible, user opts out |
| Top-level node | Named run from file label/metadata | Transport-labeled root node (`kNodeKindRun`) |
| Persistence | Checked keys (`runBrowser/checkedKeys`) | Hidden keys (`runBrowser/hiddenKeys`) |

MainWindow drives mode selection:
- **Reading mode:** `ClearAllRuns()` → `AddRunFromFile(path)` → groups start unchecked → user opts in
- **Streaming mode:** `ClearDiscoveredKeys()` → `SetStreamingRootLabel(name)` → `OnTileAdded()` per tile → groups start checked → user opts out

### Architecture (streaming mode)

- MainWindow emits `TileAdded`, `TileRemoved`, `TilesCleared` signals from tile lifecycle points (`GetOrCreateTile`, `OnRemoveWidgetRequested`, `OnClearWidgets`)
- Dock connects to these signals — tree is a 1:1 mirror of the layout's tile collection
- `SetStreamingRootLabel()` initializes streaming mode (always fully reinitializes — clears model, creates fresh root)
- `ClearDiscoveredKeys()` stays in streaming mode (clears tree, recreates root) — only `ClearAllRuns()` exits streaming mode
- Reading mode is fully immune to layout operations — all three signal handlers guard on `!m_streamingMode`
- No Clear button — dock content is driven entirely by layout lifecycle and transport state

### Files

| File | What |
|---|---|
| `src/widgets/run_browser_dock.h` | `RunBrowserDock` QDockWidget, structs, persistence API |
| `src/widgets/run_browser_dock.cpp` | JSON parse, tree model, checkbox propagation, layout-mirror, persistence |
| `src/app/main_window.h` | `TileAdded`, `TileRemoved`, `TilesCleared` signals |
| `src/app/main_window.cpp` | Integration — dock lifecycle, visibility filtering, persistence, signal wiring |
| `tests/run_browser_dock_tests.cpp` | 104 GTest tests |
| `CMakeLists.txt` | Sources in app + test targets |

### Known limitations

- `SignalActivated` / `RunActivated` signals have no downstream consumer (future: comparison plots)
- `runIndex` is a vector index — unstable across operations
- No `RemoveRun(int)` API — only bulk clear
- No duplicate-file detection
- No `qWarning()` on parse failures
- Slash-only keys produce no tree nodes

## Completed milestones

| Feature | Branch | Status |
|---|---|---|
| Run Browser dock | `feature/run-browser-dock` | Complete, pending merge to main |
| Native Link TCP carrier | `feature/native-link-tcpip-carrier` | Merged to main |
| NT4 transport (originally "Shuffleboard") | `feature/shuffleboard-transport` | Merged to main |
| Glass verification + Shuffleboard→NT4 rename | `feature/glass-transport` | Merged to main |

Glass support details and NT4 protocol reference moved to `docs/project_history.md`.

## In progress: Camera viewer dock (`feature/camera-widget`)

MJPEG camera stream viewer as a dockable panel.  Full design in
`docs/camera_widget_design.md`.

### Research completed

- Analyzed 2014 BroncBotz Dashboard video pipeline (FrameGrabber, Preview,
  ProcessingVision, ProcAmp, Controls, FrameWork library).
- Documented complete NT4 CameraPublisher key schema and stream URL format
  (`/CameraPublisher/{Name}/streams` -> `mjpg:http://...`, base port 1181).
- Confirmed Glass has no built-in camera viewer.
- Confirmed SmartDashboard has zero existing camera/video code.

### Architecture summary

- **MjpegStreamSource**: `QNetworkAccessManager` HTTP client that parses
  `multipart/x-mixed-replace` boundaries and decodes JPEG frames via
  `QImage::loadFromData()`.  No new dependencies.
- **CameraDisplayWidget**: Custom `QWidget::paintEvent()` with aspect-ratio
  scaling and fighter-jet style targeting reticle overlay (dashboard-side,
  QPainter, click-drag positionable).
- **CameraViewerDock**: `QDockWidget` following `RunBrowserDock` pattern --
  toolbar with camera selector combo, URL field, connect/disconnect, reticle
  toggle.  View menu "Camera" checkbox, starts hidden.
- **CameraPublisherDiscovery**: Watches NT4 `/CameraPublisher/` keys to
  auto-populate the camera selector.
- **CameraStreamSource**: Abstract interface so display widget accepts frames
  from any backend (MJPEG, future Robot_Simulation, test pattern).

Ian: Two separate overlay concepts exist and must not be conflated:
1. **Targeting reticle** (SmartDashboard): Dashboard-side QPainter overlay
   drawn on top of the video widget.  Fighter-jet crosshair + circle.
2. **Backup camera guide lines** (Robot_Simulation): Simulator-side OSG
   overlay drawn in 3D and baked into MJPEG frames.  Honda-style curved
   path lines driven by velocity/angular velocity.
These serve different purposes and live in different codebases.

### Implementation phases

1. ~~MJPEG stream reader + display widget + dock + CameraPublisher discovery + MainWindow wiring (MVP)~~ **COMPLETE**
2. Targeting reticle overlay (dashboard-side, crosshair + circle)
3. ~~Robot Simulation MJPEG server (Robot_Simulation repo)~~ **IN PROGRESS** — code written, pending build/test
4. Backup camera guide lines (Robot_Simulation repo, OSG-side)

Ian: Phase 3 implementation in Robot_Simulation (`feature/camera-widget` branch):
- MjpegServer subclasses ix::SocketServer (not ix::HttpServer — that's request-response only)
- SimCameraSource generates 320x240@15fps synthetic frames via stb_image_write JPEG encoding
- NT4Backend publishes /CameraPublisher/SimCamera/streams for auto-discovery
- Stream URL: mjpg:http://127.0.0.1:1181/?action=stream

Ian: Phase 3/4 design note — the simulator's MJPEG server should support two
source modes: (a) OSG framebuffer readback (vector graphics on black, the
default) and (b) real USB camera feed with OSG guide-line overlay composited
on top.  When a USB camera is available, the simulator can grab frames from
it, draw the backup-camera guide lines over the live video, encode to JPEG,
and serve via MJPEG.  When no camera is available, fall back to the existing
pure-OSG render.  This keeps the MJPEG server API identical either way —
the dashboard doesn't care whether frames come from a real camera or a
synthetic render.  Test with a machine that has a USB camera connected.

### Current status

Phase 1 (MVP) is **complete and building clean** with all 137 existing tests passing.

**All source files created and integrated:**
- `camera_stream_source.h`, `mjpeg_stream_source.h/.cpp`, `camera_publisher_discovery.h/.cpp`
- `camera_display_widget.h/.cpp`, `camera_viewer_dock.h/.cpp`
- `main_window.h` modified (forward decls, 3 member variables)
- `main_window.cpp` modified (7 integration points: includes, View menu action, dock+discovery creation, variable update routing, StopTransport camera stop, disconnect camera clear)
- `CMakeLists.txt` modified (new sources in both app and test targets)

**Build fixes applied during integration:**
- `camera_viewer_dock.h`: Changed forward declaration of `CameraStreamSource` to full `#include "camera/camera_stream_source.h"` — the header uses `CameraStreamSource::State` enum in a slot signature, which requires the full type definition
- `mjpeg_stream_source.cpp`: Fixed Most Vexing Parse — `QNetworkRequest request(QUrl(url))` was parsed as a function declaration; changed to brace-init `QNetworkRequest request{QUrl(url)}`
- `CMakeLists.txt` (test target): Added `camera_stream_source.h` to sources so MOC generates the QObject meta-object for the abstract base class (Q_OBJECT signals need MOC even in header-only classes)

**Next steps:**
- Unit tests for `MjpegStreamSource` (boundary parsing, frame decode, error handling)
- Unit tests for `CameraDisplayWidget` (aspect ratio, reticle positioning)
- Manual testing with a real MJPEG stream or test server

### Files

| File | What |
|---|---|
| `src/camera/camera_stream_source.h` | Abstract frame source interface |
| `src/camera/mjpeg_stream_source.h/.cpp` | MJPEG HTTP stream reader |
| `src/camera/camera_publisher_discovery.h/.cpp` | NT4 CameraPublisher key watcher |
| `src/widgets/camera_viewer_dock.h/.cpp` | Dock widget container |
| `src/widgets/camera_display_widget.h/.cpp` | Custom paint widget + HUD overlay |
| `docs/camera_widget_design.md` | Full design document |

### 2014 reference codebase (read-only, at `D:\Stuff\BroncBotz\Code\BroncBotz_DashBoard\Source`)

Key files consulted during research:

| File | What |
|---|---|
| `Dashboard/Dashboard.cpp` | Main app, video pipeline orchestration, ini parsing |
| `FFMpeg121125/FrameGrabber.h` | FrameGrabber facade with FFMpeg/HTTP/TestPattern backends |
| `FrameWork/Preview.h/.cpp` | DirectDraw 7 multi-buffer renderer |
| `FrameWork/Bitmap.h` | Templatized bitmap containers |
| `ProcessingVision/ProcessingVision.h/.cpp` | Vision processing plugin interface |
| `ProcessingVision/NI_VisionProcessing.cpp` | NI Vision particle analysis |
| `Controls/Controls.cpp/.h` | Controls DLL plugin (file controls, procamp controls) |
| `ProcAmp/procamp_matrix.h` | 4x4 color correction matrix math |

## Deferred work

- Wire a UI toolbar/status-bar Connect button
- Write-ack protocol on TCP Publish (currently fire-and-forget)
- Expand smoke test published keys from ~6 + chooser to full TeleAutonV2 (~49 keys)
