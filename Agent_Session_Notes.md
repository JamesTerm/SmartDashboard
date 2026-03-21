# Agent session notes

- Keep this file short and handoff-focused.
- Move durable milestone history to `docs/project_history.md`.
- Move long-term Native Link product/rollout strategy to `docs/native_link_rollout_strategy.md`.

## Workflow note

- `apply_patch` expects workspace-relative paths with forward slashes.
- Use CRLF line endings for C++ source files in this repo.
- Read nearby `Ian:` comments before editing and add new ones where transport, ordering, lifecycle, or ownership lessons would be expensive to rediscover.

## Active Native Link context

- Current stable branch head is on `feature/native-link-tcpip-carrier`.
- Baseline comparison repo is parked at `c8a1f0e` in `D:\code\SmartDashboard_baseline` for future regression checks.
- `plugins/NativeLinkTransport/include/native_link_carrier_client.h` is the active client-side carrier boundary.
- `shm` remains the diagnostic/reference backend and must stay hot-swappable even though normal runtime Native Link now defaults to TCP at the plugin boundary.
- SmartDashboard currently has a localhost TCP client/test-server path under that boundary:
  - `plugins/NativeLinkTransport/src/native_link_tcp_client.cpp`
  - `plugins/NativeLinkTransport/src/native_link_tcp_test_server.cpp`
- Plugin runtime settings support explicit carrier choice:
  - `{"carrier":"shm","channel_id":"..."}`
  - `{"carrier":"tcp","host":"127.0.0.1","port":5810,"channel_id":"..."}`
- Debug builds now expose a manual Native Link carrier override in SmartDashboard transport settings for quick SHM vs TCP comparison.
- `SMARTDASHBOARD_BUILD_PLUGIN_NATIVE_LINK` stays `OFF` by default outside focused validation.

## SHM transport status — STABLE

- SHM transport is declared stable as of 2026-03-21.
- Live telemetry fix: `NativeLink.cpp` `Core::PublishInternal` now auto-registers unknown topics for server-originated writes instead of silently rejecting them.
- Disconnect stress: 50/50 cycles PASS with `pause_ms=400`, zero warnings.
- Verification tool: `tools/native_link_live_telemetry_verify.py` — runs headless, requires no GUI.

## Key invariants (do not break)

- `RegisterDefaultTopics` stays minimal — only topics needing special writer policies (LeaseSingleWriter, StringArray) are pre-declared there. Robot-code telemetry keys auto-register on first server write.
- `IsHarnessFocusKey` in `main_window.cpp` is an intentional narrow allowlist for the debug log only. All keys still create tiles and receive updates.
- The alive guard (`m_alive` shared_ptr) in `PluginDashboardTransport::Stop()` must be set false before calling the plugin's stop, not after. Order matters for the use-after-free protection.

## Current SmartDashboard startup/defaults state

- SmartDashboard-local direct remembered-control persistence is compile-time gated off by default.
- Startup widget hydration uses temporary UI-only defaults (bool → false, numeric → 0.0, chooser → first option, string → empty).
- Temporary defaults are not transport truth and must be replaced by the first real transport value.
- `Clear Widgets` plus runtime `Load Layout` reapplies temporary defaults correctly.

## Strategy reminders

- Native Link cleanup must preserve both boundaries:
  - carrier boundary below the semantic contract
  - adapter boundary above the semantic contract
- Product stance: `tcp` is the intended normal runtime carrier; `shm` stays as the internal diagnostic/reference backend.
- SmartDashboard-specific behavior should not leak into the Native Link core contract.

## Immediate next-session focus

1. Begin Native Link TCP carrier work — SHM is stable and can serve as the reference/comparison backend.
2. For any TCP fix, rerun `native_link_live_telemetry_verify.py` (switch carrier to `tcp`) and the SmartDashboard TCP runtime probe.
3. Add `tools/native_link_tcp_disconnect_stress.py` mirroring the SHM stress script once TCP is working end-to-end.
