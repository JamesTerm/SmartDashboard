# Agent session notes

- Keep this file short and handoff-focused.
- Move durable milestone history to `docs/project_history.md`.
- Move long-term Native Link product/rollout strategy to `docs/native_link_rollout_strategy.md`.

## Workflow note

- `apply_patch` expects workspace-relative paths with forward slashes.
- Use CRLF line endings for C++ source files in this repo.
- Read nearby `Ian:` comments before editing and add new ones where transport, ordering, lifecycle, or ownership lessons would be expensive to rediscover.

## Active Native Link context

- Branch baseline is now the pre-merge Native Link TCP checkpoint on `feature/native-link-tcpip-carrier`.
- `plugins/NativeLinkTransport/include/native_link_carrier_client.h` is the active client-side carrier boundary.
- `shm` remains the diagnostic/reference backend and must stay hot-swappable even though normal runtime Native Link now defaults to TCP at the plugin boundary.
- SmartDashboard currently has a localhost TCP client/test-server path under that boundary:
  - `plugins/NativeLinkTransport/src/native_link_tcp_client.cpp`
  - `plugins/NativeLinkTransport/src/native_link_tcp_test_server.cpp`
- Plugin runtime settings support explicit carrier choice, but normal Native Link runtime use now defaults to TCP when `carrier` is omitted:
  - `{"carrier":"shm","channel_id":"..."}`
  - `{"carrier":"tcp","host":"127.0.0.1","port":5810,"channel_id":"..."}`
- `SMARTDASHBOARD_BUILD_PLUGIN_NATIVE_LINK` stays `OFF` by default outside focused validation.

## Strategy reminders

- Native Link cleanup must preserve both boundaries:
  - carrier boundary below the semantic contract
  - adapter boundary above the semantic contract
- Long-term roadmap is compatibility-first plus native-first, not robot-code-rewrite-only.
- SmartDashboard-specific behavior should not leak into the Native Link core contract.
- Product stance: `tcp` is the intended normal runtime carrier; `shm` stays as the internal diagnostic/reference backend and should not be pushed into normal team-facing UI.

## Immediate next-session focus

1. User testing / pre-merge verification of the Native Link TCP checkpoint.
2. Keep `Robot_Simulation` as the first reference authority/example, but avoid trapping reusable authority logic inside app-specific code.
3. For any follow-up fix, rerun the SmartDashboard SHM probe, SmartDashboard TCP runtime probe, SmartDashboard Native Link `ctest` slice, and focused Robot_Simulation Native Link tests.
