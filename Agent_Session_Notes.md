# Agent session notes

- Keep this file short and handoff-focused.
- For remaining work, deferred items, and future planning: see `docs/roadmap.md`.
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

# Test (247 tests, 2 disabled, 1 pre-existing failure)
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
- **NT4Client::Start() is backward-compatible:** The 4th `onAnnounce` callback parameter defaults to `nullptr`. Existing callers that pass 3 arguments continue to work unchanged.

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

## Remaining work

See `docs/roadmap.md` for all remaining work, deferred items, and future planning (Need/Want/Dream).
