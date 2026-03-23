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

- `feature/native-link-tcpip-carrier` is merged to `main`. Active development is on `main`.
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
- TCP client `Start()` is non-blocking and always returns `true`. Failure manifests as a `Disconnected` callback. Tests that need to verify a missing authority must use `auto_connect:false` and wait for `Disconnected`.
- `OnDisconnectTransport` must route through `OnConnectionStateChanged`, not `UpdateWindowConnectionText` directly. The full pipeline (title, menu enable states, recording event) must all fire together on any state transition — manual or transport-driven.

## Current startup/defaults state

- SmartDashboard-local direct remembered-control persistence is compile-time gated off by default.
- Startup widget hydration uses temporary UI-only defaults (bool → false, numeric → 0.0, chooser → first option, string → empty).
- Temporary defaults are not transport truth and must be replaced by the first real transport value.

## Strategy reminders

- `tcp` is the intended normal runtime carrier; `shm` stays as the internal diagnostic/reference backend.
- Native Link cleanup must preserve both boundaries: carrier boundary below the semantic contract, adapter boundary above.
- SmartDashboard-specific behavior should not leak into the Native Link core contract.

## Current status

- Branch `feature/native-link-tcpip-carrier` is **merged to main and pushed** in both repos.
- All known bugs are fixed. 74/74 tests pass.
- Near-term roadmap items 1-5 from `docs/native_link_rollout_strategy.md` are complete on both sides.
- Ian confirmed the UI-freeze fix. The auto-connect fix is ready for follow-up manual verification next time the DS is available.

## Hand-off checkpoint commit hashes

| Repo | Branch | Commit |
|---|---|---|
| SmartDashboard | `main` | post-merge (see git log) |
| Robot_Simulation | `main` | post-merge (see git log) |

## Cross-repo sync rule

- This repo and `D:\code\Robot_Simulation` share the Native Link contract, carrier implementations, and plugin boundary.
- When either repo's session notes or strategy docs change, check the other side for consistency — especially around invariants, carrier defaults, and plugin support iterations.
- The canonical long-term rollout strategy lives in `docs/native_link_rollout_strategy.md` (this repo).
- **Shuffleboard transport:** Robot_Simulation's `feature/shuffleboard-transport` branch takes the lead on NT4 protocol work. This repo's `feature/shuffleboard-transport` branch is reserved for the SmartDashboard plugin side once the simulator proves the protocol works against the official Shuffleboard app.

## Next session starting point

**Shuffleboard transport work is active on the Robot_Simulation side.** This repo is not involved yet — the simulator will prove NT4 protocol compatibility with the official Shuffleboard app first, then findings inform the SmartDashboard plugin.

Candidate follow-on tasks (independent of Shuffleboard work):

- **Manual verify auto-connect checkbox** with a live DS: uncheck it in settings while connected, confirm the transport stops immediately and Connect becomes available.
- Write-ack protocol on TCP `Publish` (currently fire-and-forget).
- Extend `native_link_live_telemetry_verify.py --carrier tcp` with `--cycles N`.
- Wire a UI toolbar/status-bar Connect button as a more prominent surface for `auto_connect:false` workflows.
