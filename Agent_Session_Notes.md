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

- Current stable branch head is on `feature/native-link-tcpip-carrier`.
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

## Current startup/defaults state

- SmartDashboard-local direct remembered-control persistence is compile-time gated off by default.
- Startup widget hydration uses temporary UI-only defaults (bool → false, numeric → 0.0, chooser → first option, string → empty).
- Temporary defaults are not transport truth and must be replaced by the first real transport value.

## Strategy reminders

- `tcp` is the intended normal runtime carrier; `shm` stays as the internal diagnostic/reference backend.
- Native Link cleanup must preserve both boundaries: carrier boundary below the semantic contract, adapter boundary above.
- SmartDashboard-specific behavior should not leak into the Native Link core contract.

## Current status

- TCP and SHM transports are both stable and at parity.
- DS root-cause fix is complete — port 5810 now binds on a clean DS launch. See `docs/journal/2026-03-21-ds-env-root-cause.md`.
- 21/21 `NativeLinkTransport_tests` + 32/32 `SmartDashboard_tests` pass.
- No known blocking items. Potential future work:
  - Wire a UI Connect button to `Stop()`+`Start()` for manual-connect mode.
  - Write-ack protocol on TCP `Publish` (currently fire-and-forget).
  - Extend `native_link_live_telemetry_verify.py --carrier tcp` with `--cycles N`.
