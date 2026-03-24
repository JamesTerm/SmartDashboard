# Agent session notes

- Keep this file short and handoff-focused.
- Move durable milestone history to `docs/project_history.md`.
- Move verbose findings, root-cause investigations, and debugging narratives to `docs/journal/<date>-<slug>.md` (descending date order, newest first).
- Once a topic is fully resolved and no longer needs to be in the foreground, move it to the journal. Do not let completed work accumulate here.

## Workflow note

- Use CRLF line endings for C++ source files in this repo.
- Read nearby `Ian:` comments before editing and add new ones where transport, ordering, lifecycle, or ownership lessons would be expensive to rediscover.
- Never mix `SetEnvironmentVariableA` (Win32 write) with `_dupenv_s` (CRT read) for the same variable. Use `GetEnvironmentVariableA` on the read side to match the Win32 write.

## Active Native Link context

- `feature/native-link-tcpip-carrier` is merged to `main`. Active development is on `feature/shuffleboard-transport`.
- Baseline comparison repo is parked at `c8a1f0e` in `D:\code\SmartDashboard_baseline` for future regression checks.
- `plugins/NativeLinkTransport/include/native_link_carrier_client.h` is the active client-side carrier boundary.
- `shm` remains the diagnostic/reference backend and must stay hot-swappable.
- Plugin runtime settings support explicit carrier choice:
  - `{"carrier":"shm","channel_id":"..."}`
  - `{"carrier":"tcp","host":"127.0.0.1","port":5810,"channel_id":"..."}`
- `NativeLinkTransport` is always built unconditionally.
- `gtest_discover_tests` calls all use `DISCOVERY_MODE PRE_TEST`.
- Solution Explorer folder grouping is in place (`USE_FOLDERS ON`).

## Key invariants (do not break)

- `RegisterDefaultTopics` stays minimal — robot-code telemetry keys auto-register on first server write.
- `IsHarnessFocusKey` in `main_window.cpp` is an intentional narrow allowlist for the debug log only. All keys still create tiles and receive updates.
- The alive guard (`m_alive` shared_ptr) in `PluginDashboardTransport::Stop()` must be set false before calling the plugin's stop, not after.
- TCP client `Start()` is non-blocking and always returns `true`. Failure manifests as a `Disconnected` callback. The host reconnect timer handles retries.
- `OnDisconnectTransport` must route through `OnConnectionStateChanged`, not `UpdateWindowConnectionText` directly. The full pipeline (title, menu enable states, recording event) must all fire together on any state transition — manual or transport-driven.
- **Host-level auto-reconnect:** All plugin transports make a single connect attempt per `Start()` call. The reconnect timer in `MainWindow` (`m_reconnectTimer`, 1-second single-shot) drives retries via Stop()+Start() cycles. Plugins must NOT implement their own retry loops.

## Current startup/defaults state

- SmartDashboard-local direct remembered-control persistence is compile-time gated off by default.
- Startup widget hydration uses temporary UI-only defaults (bool → false, numeric → 0.0, chooser → first option, string → empty).
- Temporary defaults are not transport truth and must be replaced by the first real transport value.

## Strategy reminders

- `tcp` is the intended normal runtime carrier; `shm` stays as the internal diagnostic/reference backend.
- Native Link cleanup must preserve both boundaries: carrier boundary below the semantic contract, adapter boundary above.
- SmartDashboard-specific behavior should not leak into the Native Link core contract.

## Current status

### Native Link (main branch)

- Branch `feature/native-link-tcpip-carrier` is **merged to main and pushed** in both repos.
- All known bugs are fixed. 74/74 tests pass on main.
- Near-term roadmap items 1-5 from `docs/native_link_rollout_strategy.md` are complete on both sides.

### Shuffleboard transport (`feature/shuffleboard-transport`)

- **Plugin structure complete:** `plugins/ShuffleboardTransport/` with NT4 client, plugin ABI bridge, CMake build, and 17 tests — all passing (91/91 total tests pass).
- **NT4 client** (`nt4_client.h` / `nt4_client.cpp`, ~700 lines) — IXWebSocket-based NT4 v4.1 client: subscribe-on-connect, binary MsgPack decoding, JSON announce parsing, topic prefix stripping, publish (write-back) support. Auto-reconnect is now disabled — the host drives retries.
- **Plugin entry** (`shuffleboard_transport_plugin.cpp`, ~400 lines) — Full `sd_transport_plugin_descriptor_v1` ABI: connection fields (host, use_team_number, team_number, client_name, auto_connect), JSON settings, team-number host resolution, update callback bridge, plugin_id `"shuffleboard"`, display_name `"Shuffleboard (NT4)"`.
- **Write-back is enabled.** `supports_chooser` returns true. The publish path (`PublishBool/Double/String`) is fully wired. Three bugs were fixed — see below.
- **Build dependency:** `ixwebsocket::ixwebsocket` (STATIC from vcpkg) + `bcrypt` (Windows system lib for mbedTLS). Stock vcpkg ixwebsocket is fine for the client side.

### Host-level auto-connect refactoring (COMPLETE)

Reconnect logic lifted out of all three plugins into `MainWindow`. Every plugin now makes a single connect attempt per `Start()` call and fires `Disconnected` on failure. The host sees `Disconnected` and schedules a 1-second single-shot retry timer if `auto_connect` is enabled and the user didn't manually click Disconnect.

**Files changed (auto-connect):**

| File | Change |
|---|---|
| `SmartDashboard/src/app/main_window.h` | Added `m_reconnectTimer`, `m_userDisconnected`, `IsAutoConnectEnabled()`, `OnReconnectTimerFired()` |
| `SmartDashboard/src/app/main_window.cpp` | Timer creation (~line 1136), reconnect logic in `OnConnectionStateChanged`, connect/disconnect handlers, settings handler, `IsAutoConnectEnabled()`, `OnReconnectTimerFired()` |
| `plugins/NativeLinkTransport/include/native_link_carrier_client.h` | Removed `bool autoConnect` from `NativeLinkClientConfig` |
| `plugins/NativeLinkTransport/src/native_link_tcp_client.cpp` | Removed `kReconnectRetryMs`, `autoConnect` atomic, outer reconnect while-loop → single-attempt `RunLoop()` |
| `plugins/NativeLinkTransport/src/native_link_transport_plugin.cpp` | Removed `clientConfig.autoConnect` reading |
| `plugins/ShuffleboardTransport/include/nt4_client.h` | Removed `bool autoConnect` from `NT4ClientConfig` |
| `plugins/ShuffleboardTransport/src/nt4_client.cpp` | Removed `kReconnectDelayMs`, always `disableAutomaticReconnection()` |
| `plugins/ShuffleboardTransport/src/shuffleboard_transport_plugin.cpp` | Removed `autoConnect` from instance struct and config |
| `plugins/LegacyNtTransport/src/legacy_nt_transport_plugin.cpp` | `RunClientLoop` → single-attempt (no outer retry loop) |

**Key design decisions:**
- The `auto_connect` connection field descriptor stays in NativeLink and Shuffleboard plugin descriptor tables so the UI checkbox continues to work. The host reads it via `MainWindow::IsAutoConnectEnabled()` from `pluginSettingsJson`.
- Legacy NT never had the `auto_connect` field — it just always auto-connected. Now the host controls that uniformly for all plugins.
- `m_userDisconnected` flag distinguishes manual Disconnect (suppress retries) from transport-initiated drops (retry if auto-connect enabled).
- SHM (IPC) client was not changed — it uses a fundamentally different model (shared memory polling with heartbeat) and doesn't do TCP-style connect/disconnect cycles.

#### Write-back bug fixes (prior session)

1. **BUG 1 — Wrong topic path prefix in `EnsurePublished`** (`nt4_client.cpp` line ~921): Built path as `"/" + key` (e.g. `/TestMove`). Server publishes under `/SmartDashboard/<key>`. Fixed to `"/SmartDashboard/" + key`.
2. **`supports_chooser` flipped to true** (`shuffleboard_transport_plugin.cpp` line ~312): Enables chooser sub-key recognition on inbound updates so the chooser UI is interactive. Does NOT gate outbound publish — that always worked.
3. Test `ChooserSupportDisabledInPhase1` renamed to `ChooserSupportEnabled` and updated to expect non-zero.

## Hand-off checkpoint commit hashes

| Repo | Branch | Commit |
|---|---|---|
| SmartDashboard | `feature/shuffleboard-transport` | (see git log — auto-connect refactoring commit) |
| Robot_Simulation | `feature/shuffleboard-transport` | (see git log — write-back fixes commit) |

## Cross-repo sync rule

- This repo and `D:\code\Robot_Simulation` share the Native Link contract, carrier implementations, and plugin boundary.
- When either repo's session notes or strategy docs change, check the other side for consistency — especially around invariants, carrier defaults, and plugin support iterations.
- The canonical long-term rollout strategy lives in `docs/native_link_rollout_strategy.md` (this repo).
- **Shuffleboard transport:** Robot_Simulation's `feature/shuffleboard-transport` branch has a working NT4 server with full bidirectional support. This repo's `feature/shuffleboard-transport` branch has the SmartDashboard NT4 client plugin with write-back and host-level auto-connect.

## Next session starting point

**Write-back + auto-connect refactoring is code-complete** on `feature/shuffleboard-transport`. Both repos build clean and SmartDashboard tests pass (91/91). Next steps:

1. **End-to-end feedback verification:** Run SmartDashboard with Shuffleboard plugin against Robot_Simulation's `DriverStation_TransportSmoke.exe --mode shuffle`. Verify that dashboard edits (chooser selection, TestMove slider) reach the simulator and produce observable effects. The smoke test reads auton selection, moves forward, and the Y-ft value can be monitored to validate the full feedback loop.
2. **Git commit** both repos on `feature/shuffleboard-transport`.

Candidate follow-on tasks:

- Wire a UI toolbar/status-bar Connect button as a more prominent surface for manual connect workflows.
- Write-ack protocol on TCP `Publish` (currently fire-and-forget).
- Extend `native_link_live_telemetry_verify.py --carrier tcp` with `--cycles N`.
- Expand published keys from smoke test (~6 + chooser) to full TeleAutonV2 (~49 keys).
