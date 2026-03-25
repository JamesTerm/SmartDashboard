# Agent session notes

- Keep this file short and handoff-focused.
- Move durable milestone history to `docs/project_history.md`.
- Move verbose findings, root-cause investigations, and debugging narratives to `docs/journal/<date>-<slug>.md` (descending date order, newest first).
- Once a topic is fully resolved and no longer needs to be in the foreground, move it to the journal. Do not let completed work accumulate here.

## Workflow

- **Use CRLF line endings** for all source files (`.cpp`, `.h`, `.cmake`, `.ps1`, `.py`, `.md`, `.rc`, `.gitignore`). Both repos standardized CRLF as of the Shuffleboard merge.
- Read nearby `Ian:` comments before editing and add new ones where transport, protocol, lifecycle, or ownership lessons would be expensive to rediscover.
- Never mix `SetEnvironmentVariableA` (Win32 write) with `_dupenv_s` (CRT read) for the same variable. Use `GetEnvironmentVariableA` on the read side to match the Win32 write.
- `gtest_discover_tests` calls all use `DISCOVERY_MODE PRE_TEST`.
- Solution Explorer folder grouping is in place (`USE_FOLDERS ON`).
- Debug logs only write when `--instance-tag` is passed or `SMARTDASHBOARD_INSTANCE_TAG` env var is set. Logs go to `.debug/native_link_ui_<tag>.log`.
- SmartDashboard settings are persisted in Windows Registry under `HKCU:\Software\SmartDashboard\SmartDashboardApp`.

## Build

```bash
# Configure
cmake -G "Visual Studio 17 2022" -B build -DCMAKE_TOOLCHAIN_FILE="D:/code/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows

# Build
cmake --build build --config Debug

# Test (91 tests)
cd build && ctest -C Debug --output-on-failure
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

## Completed milestones

| Feature | Branch | Status |
|---|---|---|
| Native Link TCP carrier | `feature/native-link-tcpip-carrier` | Merged to main |
| Shuffleboard NT4 transport | `feature/shuffleboard-transport` | Merged to main |
| Glass NT4 transport + Shuffleboard→NT4 rename | `feature/glass-transport` | Active |

## Glass transport (active — `feature/glass-transport`)

Glass is the next dashboard integration target. It uses the same NT4 protocol as Shuffleboard — same WebSocket transport, same MsgPack binary frames, same JSON control messages, same port 5810. Because the NT4 plugin already implements a full NT4 client, the Glass plugin may share most of that code.

### Glass installation

Glass 2026.2.2 is installed at `D:\code\Glass` (portable directory, same pattern as `D:\code\Shuffleboard`):

| File | Purpose |
|---|---|
| `glass.exe` | Glass application (native C++ binary — no JRE needed) |
| `run_glass.bat` | Launch Glass (default config from `%APPDATA%`) |
| `run_glass_local.bat` | Launch Glass pre-configured for localhost:5810 |
| `config_local\glass.json` | Pre-configured: NT4 client mode, `localhost`, port 5810 |

**How local config works:** Glass takes one CLI argument — a save directory for its JSON config files. `run_glass_local.bat` passes `config_local\` which contains a pre-seeded `glass.json`. Default `run_glass.bat` uses `%APPDATA%` and requires manual GUI configuration.

**Source:** `https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/tools/Glass/2026.2.2/Glass-2026.2.2-windowsx86-64.zip`

### Plan

1. ~~Pull Glass into `D:\code\Glass`~~ — Done
2. **Make Robot_Simulation work with Glass** — likely zero server changes since Glass speaks the same NT4 protocol on port 5810
3. **Create SmartDashboard Glass plugin** under `plugins/GlassTransport/`

### Lessons from the Shuffleboard integration to apply to Glass

**Protocol:**
- NT4 is subscription-driven. The server must NOT send `announce` messages until the client sends `subscribe`. This was the #1 silent failure with Shuffleboard.
- When a client sends binary value frames, it uses its **pubuid** (from the JSON `publish` message), NOT the server-assigned topic ID. The server must maintain a per-client `pubuid -> topicId` map.
- The server should echo values back to the sender (not skip the originating client). The server is the single source of truth.
- Binary MsgPack frames may contain multiple concatenated messages per WebSocket frame.
- Subprotocol negotiation is critical. IXWebSocket needed a patch to echo exactly ONE selected protocol (RFC 6455). The overlay port at `D:\code\Robot_Simulation\overlay-ports\ixwebsocket/` has this fix. Glass may use a different subprotocol string — check its source.

**SmartDashboard plugin side:**
- WSAStartup must be called before any IXWebSocket operations. Use `ix::initNetSystem()` in `Start()`, `ix::uninitNetSystem()` in `Stop()`, guarded by a flag. Do NOT initialize Winsock globally.
- Stock vcpkg ixwebsocket works fine for the client side (no overlay port needed).
- Topic prefix: NT4 uses `/SmartDashboard/<key>`. Glass may use a different prefix — check its NT4 topic namespace.
- `EnsurePublished` must build the correct full topic path for the publish JSON message.
- The chooser protocol (`.type`, `options`, `default`, `active`, `selected` sub-keys) is a WPILib convention. Glass should support it too, but verify.

**Simulator side (Robot_Simulation):**
- The NT4 server binds to port 5810. Glass also uses port 5810 in NT4 client mode (same as Shuffleboard) — no port changes needed. The old note about "Glass defaults to port 1735" was wrong; 1735 is the NT3 fallback port.
- `IsChooserEnabledForCurrentConnection()` in `AI_Input_Example.cpp` must include the new mode.
- `UsesLegacyTransportPath()` in `Transport.cpp` must return false for the new mode.
- A new `ConnectionMode` enum value and `IConnectionBackend` subclass are needed.
- See the `Ian:` comment on `Transport.h` for the full checklist of files to update.

**Testing & automation:**
- `tools/sdcmd.ps1` sends debug commands (including `publish`) to SmartDashboard via named pipe.
- `dsctl.ps1` (Robot_Simulation) automates DriverStation button clicks.
- Use `--instance-tag` when launching SmartDashboard for debug logging.
- Process detection: use `Get-Process -Name <name> -ErrorAction SilentlyContinue` (NOT `tasklist | findstr` which breaks in Git Bash).
- Killing processes: use PowerShell `Stop-Process` (NOT `taskkill` through Git Bash — flag mangling).

**Common pitfalls:**
- `sdcmd.ps1` parameter must NOT be named `$Pid` — it shadows PowerShell's automatic `$PID` variable.
- No value persistence by design — TestMove and chooser start at 0/"Do Nothing" each launch.
- The smoke test seeds values synthetically; real DriverStation flow requires a client to write values.

### NT4 protocol quick reference

- **Transport:** WebSocket, resource path `/nt/<clientname>`
- **Subprotocols:** `v4.1.networktables.first.wpi.edu` (preferred), `networktables.first.wpi.edu` (v4.0)
- **Control messages:** JSON text frames — each frame is a JSON array of message objects with `method` and `params`
- **Server→client:** `announce`, `unannounce`, `properties`
- **Client→server:** `subscribe`, `unsubscribe`, `publish`, `unpublish`, `setproperties`
- **Value updates:** MsgPack binary frames — `[topicID, timestamp_us, dataType, value]`
- **Data types:** boolean=0, double=1, int=2, float=3, string=4, raw=5, boolean[]=16, double[]=17, int[]=18, float[]=19, string[]=20
- **Timestamp sync:** topicID=-1, client sends local time, server responds with `[-1, serverTime, typeCode, clientTime]`
- **Full spec:** https://github.com/wpilibsuite/allwpilib/blob/main/ntcore/doc/networktables4.adoc

## Deferred work (not blocking Glass)

- Wire a UI toolbar/status-bar Connect button
- Write-ack protocol on TCP Publish (currently fire-and-forget)
- Expand smoke test published keys from ~6 + chooser to full TeleAutonV2 (~49 keys)
