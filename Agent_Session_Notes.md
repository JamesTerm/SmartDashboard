# Agent session notes

- Edit this file for short, high-signal context that helps the next session start quickly.
- Keep this file lean; move long milestone history to `docs/project_history.md`.
- Commit-note convention: when the user says "update notes", keep this file to handoff-critical context only; put durable feature/change history in `docs/project_history.md`.
- `docs/project_history.md` ordering rule: keep milestone sections in descending chronological order (newest first) so current changes are always at the top.

## Workflow note

- `apply_patch` expects workspace-relative paths (forward slashes). Avoid absolute Windows paths to prevent separator errors.
- Code style uses ANSI/Allman indentation; keep brace/indent alignment consistent with existing blocks to avoid drift.
- Use Windows CRLF line endings for C++ source files in this repo.
- Read nearby `Ian:` comments before editing a file. They mark intentional boundaries or historical lessons that should be preserved unless you deliberately mean to change behavior.

## Documentation and teaching comments rule

- Treat this codebase as both production code and a learning reference.
- Add concise, high-value comments in `.cpp` files when logic is non-trivial (timing behavior, concurrency, transport semantics, state handling, etc.).
- For advanced algorithms/patterns, include the concept name directly in comments where implemented (for example: ring buffer, round-robin, coalescing/latest-value cache, debounce, backoff).
- Keep comments practical and instructional: explain *why* a pattern is used and what trade-off it makes, not just what the line does.
- Avoid noisy comments on obvious code paths; focus comments on places likely to confuse first-time readers.

## Design docs

- Primary design document: `design/SmartDashboard_Design.md`
- Durable milestone history: `docs/project_history.md`
- Replay operator reference: `docs/replay_user_manual.md`
- Replay status/roadmap reference: `docs/replay_parity_roadmap.md`
- Robot simulation transport contract: `docs/robot_simulation_transport_guide.md`

## Quick context for next session

- Repository baseline is local `main`; `feature/replay-dockable-workspace` has been merged.
- Core dashboard architecture is stable: transport-agnostic main window (`Direct`, plugin compatibility transports, `Replay`) + `VariableStore` + Qt widget tiles.
- Editable mode is layout-only; non-editable mode restores live writable controls.
- Layout workflows are in place: file-dialog save/load, dirty tracking, startup apply, and close prompt.
- Direct transport includes retained latest-value fallback for cross-run config/state retrieval.
- Replay stack is broadly in place on `main`:
  - recording to newline-delimited JSON events under `logs/session_<timestamp>.json`
  - replay load path accepts both event-stream and capture-session JSON shapes
  - timeline scrub/zoom/pan, adaptive tick labels, cursor/window readouts, marker jumps, marker dock, keyboard stepping, bookmarks, anomaly markers, and visible-window marker summary
  - dockable `Replay Controls`, `Replay Timeline`, and `Replay Markers` panels with persisted visibility and `Reset Replay Layout`
- `docs/project_history.md` is the authoritative milestone log; keep this file to current-state handoff only.
- Compatibility direction for simulator work is now documented: keep legacy NT behavior as a stable plugin baseline and treat Shuffleboard-oriented additions as additive profiles.
- Current transport-plugin status:
  - `Legacy NT` now runs as a real optional plugin discovered from `plugins/`
  - the old built-in NT transport implementation has been removed from the core app
  - transport capabilities now support shared extensible property queries (`supports_chooser`, `supports_multi_client`)
  - transport connection settings are now described by transport field schemas and rendered by the host in a separate transport-specific settings dialog
  - selected transport identity is shown in status/title text so active backend is visible during testing.
  - manual validation on this branch confirmed the plugin path works well enough to treat it as the new compatibility baseline.
- Direct survive/restart slice is now working again with Robot_Simulation pairing:
  - dashboard startup loads remembered operator-owned control values from `QSettings`
  - remembered values are applied after layout load and again after Direct retained replay so dashboard intent wins over stale startup defaults
  - sequence tracking is cleared before retained replay so synthetic startup `seq=0` values repaint tiles after a dashboard restart
  - retained replay now includes `TestMove` / `Test/TestMove` in addition to the numeric `AutonTest` baseline
  - control edits now persist immediately when dashboard-owned values change instead of waiting for a later session write
- Chooser compatibility slice remains implemented for Direct pairing:
  - chooser metadata routing handles direct string-array `/options`
  - chooser reconnect no longer clobbers robot-owned selection on dashboard reopen
  - chooser tile churn was reduced by avoiding redundant chooser-mode resets on repeated metadata updates.
- New Direct transport/harness status:
  - SmartDashboard direct telemetry now uses independent subscriber read cursors rather than one shared consumed cursor
  - UI callback path was switched from one queued lambda per update to a batched queued drain on the Qt thread
  - process-control helper exists at `tools/smartdashboard_process.py` so sessions can deterministically launch/check/close the dashboard during transport experiments
  - `tools/survive_sequence.py` now automates dashboard-survive followed by robot-survive validation
  - focused CLIs now exist for direct probing/capture:
    - `DirectStateProbeCli` seeds and verifies chooser + `TestMove`
    - `DirectWatchCli` passively records direct updates during a run
  - a fixed harness workspace now places `Test/AutoChooser`, `TestMove`, `Timer`, and `Y_ft` in visible positions for manual paint checks
- Current practical conclusion:
  - real single-dashboard Direct survive now passes again for remembered `TestMove`, chooser survival, and robot restart handoff
  - an attempted inbound-protection tweak in `OnVariableUpdateReceived` was a regression and should stay reverted unless rethought more carefully
  - repeated robot restart stress improved after fixing publisher free-space accounting against the active consumer cursor
  - passive extra observers still expose race/session weaknesses, so transport is still not treated as truly multi-observer safe
  - the short immediate post-dashboard-restart probe window can still miss early telemetry (`Timer` / `Y_ft`), even when the later robot-survive phase passes cleanly
- Next simulator-facing goal: continue Shuffleboard-oriented compatibility work on top of the new plugin boundary, while preserving the stable `Legacy NT` baseline and the real single-dashboard Direct survive path.
- Next architecture/design goal after merge: start a new `Native Link` feature branch for a product-owned generic plugin transport.
  - intent: design a more robust multi-client-capable native transport/plugin example without inheriting all legacy NetworkTables shared-state trade-offs
  - research direction: study strengths of dashboards like `Shuffleboard`, `Glass`, `Elastic`, and similar tools for workflow lessons only, not imitation
  - likely design themes: server authority, explicit topic descriptors, state-vs-command separation, reconnect snapshot + live delta flow, and stronger freshness/ownership semantics.
  - initial design baseline now lives at `plugins/NativeLinkTransport/README.md`
  - concrete contract + test-plan docs now live at `plugins/NativeLinkTransport/CONTRACT.md` and `plugins/NativeLinkTransport/TEST_PLAN.md`
  - first documented carry-forward lessons from `Direct`: do not repeat shared-consumer assumptions, make reconnect/session ordering explicit, and treat diagnostics/introspection as core transport features
  - first test-driven implementation scaffold now exists in `plugins/NativeLinkTransport/` with a small `NativeLinkTransportCore` library and focused contract tests for descriptor validation, snapshot ordering, lease policy, TTL stale marking, and session reset behavior
  - autonomous survive stress is now partially recreated in-process in `plugins/NativeLinkTransport/tests/native_link_core_tests.cpp`: chooser selection + `TestMove` replay, `Timer` countdown telemetry, conditional `Y_ft` movement, server restart x5, and dashboard reconnect x5 all pass in the current harness
  - next intended Native Link stress slice: explicit multi-client validation (two dashboards + watcher in harness first, then later real SmartDashboard multi-process validation gated by plugin-advertised multi-client capability)
  - host integration direction for later: keep SmartDashboard single-instance by default, but allow a transport-specific bypass only when the selected plugin explicitly advertises multi-client support
  - first host-side startup gate slice is now in progress via `SmartDashboard/src/app/startup_instance_gate.*` and `SmartDashboard/src/main.cpp`: an explicit `--allow-multi-instance` flag is only honored when the selected transport advertises `supports_multi_client`
  - first Native Link plugin scaffold now exists at `plugins/NativeLinkTransport/src/native_link_transport_plugin.cpp`; it is intentionally small but already exports a real `native-link` plugin descriptor and builds/deploys next to `SmartDashboardApp`
  - SmartDashboard-side discovery coverage now exists in `SmartDashboard/tests/dashboard_transport_registry_tests.cpp`, giving a focused smoke test that the built `native-link` plugin can actually be discovered by the host registry
  - top-level build ordering now makes `SmartDashboardApp` and `SmartDashboard_tests` depend on `NativeLinkTransportPlugin`, so the discovery path no longer relies on test-time fallback copying
  - host-side runtime smoke coverage now also exists in `SmartDashboard/tests/dashboard_transport_registry_tests.cpp`: the host can start a real `native-link` transport instance, receive initial retained state, and publish values back through the plugin path
  - first real two-process startup helper now exists at `tools/native_link_multi_instance_smoke.py`; it is intended as the first SmartDashboard-vs-SmartDashboard smoke step before deeper shared-state validation
  - that helper now also forces persisted startup transport selection to `native-link`, so the two-process smoke specifically exercises the Native Link startup gate path
  - next real two-process shared-state helper now exists at `tools/native_link_shared_state_probe.py`; it uses per-instance UI debug logs to confirm both dashboards observe the same initial Native Link retained state
  - that second-dashboard UI-log gap is now fixed: the root cause was checking transport/plugin multi-client capability before `QApplication` existed, which made Native Link appear non-multi-client during the singleton decision path
  - `tools/native_link_shared_state_probe.py` is now passing repeatedly and confirms both real dashboard processes observe the same initial Native Link retained chooser + `TestMove` state
  - Native Link plugin scaffold now uses one shared in-process authority for the default channel so two real dashboard processes can also observe cross-process `TestMove` propagation during early validation, not just matching startup defaults
  - important caveat for the next session: that shared authority is still an early SmartDashboard-side scaffold for `native-link-default`, not the final external/shared transport architecture; keep it as a validation bridge, not as proof that the long-term transport design is finished
  - build-default strategy is now aligned with the future merge plan: `SMARTDASHBOARD_BUILD_PLUGIN_NATIVE_LINK` should stay `OFF` by default so `main` can absorb the architecture/test work without exposing unfinished plugin behavior; enable it explicitly on the feature branch when validating Native Link
  - future merge strategy for this line of work: when we are ready to start the separate `Robot_Simulation` branch, squash-merge the SmartDashboard Native Link work into `main` but keep the new plugin disabled in project build settings so `main` stays stable without exposing unfinished plugin behavior
  - newest Native Link slice replaced the SmartDashboard-side in-memory `native-link-default` authority scaffold with a real shared-memory + named-events client/server bridge shape in `plugins/NativeLinkTransport/`:
    - new shared carrier contract header lives at `plugins/NativeLinkTransport/include/native_link_ipc_protocol.h`
    - SmartDashboard plugin now starts a real IPC client (`native_link_ipc_client.*`) instead of creating an in-process authority in `native_link_transport_plugin.cpp`
    - focused IPC harness server for SmartDashboard-side tests lives at `plugins/NativeLinkTransport/src/native_link_ipc_test_server.cpp`; it mirrors the simulator-owned v1 carrier enough for plugin/runtime validation without pretending the dashboard owns authority
    - important `Ian:` cross-process lesson now captured in the test server: do not treat `clientTag != 0` as a fully initialized client slot before `clientId` + first heartbeat are visible, or the server can race and invalidate half-initialized clients before snapshot delivery
  - current Native Link automated status is mixed and this is the main blocker for the next session:
    - core tests still pass
    - focused isolated IPC tests and focused registry runtime smoke can pass
    - but the full combined Native Link/registry `ctest` slice is still flaky/failing in some runs because the real IPC client/server handshake around initial snapshot/live readiness and post-start publish timing is not yet deterministic enough
    - especially watch `NativeLinkIpcClientTests.DashboardPublishReachesAuthoritativeServer` and `DashboardTransportRegistryTests.NativeLinkPluginTransportStartsAndPublishesInitialState`; they expose remaining startup-order races in the SmartDashboard-side validation harness even after replacing the in-memory bridge
    - do not update docs/testing claims to say the new real IPC path is fully stable until that full-suite flake is fixed
  - latest checkpoint findings on this branch:
    - tried hardening the shared carrier by aligning the mapped atomics properly and removing the packed-layout dependency; that change is now in both repos and should stay because packed atomics were a bad foundation for further debugging
    - also split snapshot bookkeeping from write-ack intent in the carrier shape by adding `snapshotCompleteSessionId`, but the SmartDashboard client/test harness startup race is still not fully resolved
    - current remaining failure pattern is still SmartDashboard-side: the client sometimes never reports `Connected` / never observes the post-restart reconnect, and first dashboard publishes can still miss the authoritative server in `NativeLinkIpcClientTests.DashboardPublishReachesAuthoritativeServer`, `NativeLinkIpcClientTests.DashboardPublishSucceedsAfterServerSessionRestart`, and `DashboardTransportRegistryTests.NativeLinkPluginTransportStartsAndPublishesInitialState`
    - focused Robot_Simulation Native Link unit tests still pass after the protocol/layout updates (`robot_unit_tests.exe --gtest_filter=*NativeLink*`)
    - likely next debugging target: inspect the SmartDashboard IPC client worker/connection-state path around `snapshotPhase`, `MaybePublishConnected`, and repeated `SetEvent`/heartbeat handling rather than continuing to only adjust tests
  - follow-up finding after deeper tracing:
    - one real bug was present in the SmartDashboard test server: `heartbeatAgeUs = nowUs - lastHeartbeatUs` could unsigned-underflow when the client refreshed heartbeat after the server sampled `nowUs`, which made the server falsely stale-disconnect a healthy client and clear its slot
    - fixed that by clamping the server-side heartbeat age at zero when `lastHeartbeatUs > nowUs`; keep the same guard in mind for the simulator-owned authority path too
    - that underflow bug explained some earlier mid-test slot clears, but it was not the only issue; after the fix, the combined SmartDashboard IPC/registry slice still flakes/fails because the client sometimes never reaches a durable `Connected` state in the first place
    - current next target remains the SmartDashboard IPC client startup/restart handshake itself rather than more carrier changes
  - latest stabilization pass result:
    - the plugin-owned SmartDashboard transport startup path was still reporting success too early; waiting for `NativeLinkIpcClient::IsConnected()` before returning plugin `start()` success closed the host-side replay/startup race that was still breaking the registry slice
    - the restart test itself also needed to validate the documented reconnect pattern (`Connected -> reconnect transition -> Connected`) instead of assuming two bare `Connected` notifications with no intervening state
    - after those updates, the focused SmartDashboard Native Link slice is currently green and survived a 20x repeat loop: `ctest --output-on-failure -C Debug -R "NativeLinkIpcClientTests|DashboardTransportRegistryTests"`
    - Robot_Simulation Native Link unit tests still pass after the paired authority-side guard changes
  - app-level paired validation attempt from these checkpoints is still incomplete:
    - `tools/native_link_shared_state_probe.py` still fails in the current real-process path because the per-instance UI logs only reach `transport_start id=native-link` and never record the expected retained updates yet
    - that means the focused automated IPC/registry slice is green, but the two-real-dashboard + real authority smoke helper is not yet proving end-to-end retained state delivery
    - a likely next debugging angle is whether the helper is still configuring the right channel/authority assumptions for the new real IPC path, plus whether `DriverStation_TransportSmoke.exe` is actually staying alive as the expected authority during the probe window
    - one concrete cleanup from this pass: the helper was still asserting the old SmartDashboard-scaffold defaults (`Do Nothing`, `TestMove=0`) instead of the simulator-owned authority seed (`Just Move Forward`, `TestMove=3.5`); that expectation is now corrected in `tools/native_link_shared_state_probe.py`
    - even after fixing the probe expectation, the real-process helper still only records `transport_start id=native-link` in the per-instance UI logs, so the remaining gap is real retained-update delivery/observation during the app-level probe, not just a stale assertion
  - latest paired app-level probe result:
    - the missing second-dashboard retained updates were caused by the helper still launching both real SmartDashboard processes with the same persisted Native Link client identity; that let the simulator-owned authority treat them as one logical client, so the probe was not exercising true two-client fan-out
    - `tools/native_link_multi_instance_smoke.py` now rewrites the persisted Native Link client name between launches (`dashboard-a`, then `dashboard-b`) and `tools/native_link_shared_state_probe.py` now auto-starts `DriverStation_TransportSmoke.exe` when available locally
    - with those helper fixes, the paired real-process probe now passes locally: `python tools/native_link_shared_state_probe.py` reports `native_link_shared_state_probe=ok`
    - one more helper hardening pass was needed for repeatability: the probe originally treated "log file exists" as if it meant retained startup updates had already drained through the UI thread, but repeated runs showed the second dashboard can lag slightly and create a false failure even though it catches up moments later
    - `tools/native_link_shared_state_probe.py` now waits for the retained chooser marker itself instead of only waiting for non-empty log files; after that timing fix, the paired real-process probe passed 10/10 repeated runs locally
    - another real probe race showed up during broader merge-readiness validation: the helper was tearing down the temporary authority as soon as the launcher returned, before the second dashboard had always finished draining retained startup state; keeping the authority alive until after retained-marker checks fixed that flake, and the paired probe again passed 10/10 repeats locally
  - roadmap note for follow-on sessions:
    - keep the current shared-memory + named-events carrier as the simpler diagnostic/reference backend even after adding TCP
    - planned direction is one Native Link protocol above multiple carriers, with TCP becoming the intended long-term default only after parity against the current focused slice and paired real-process probe is proven
  - first carrier-abstraction checkpoint is now in place on `feature/native-link-tcpip-carrier`:
    - SmartDashboard has a new internal carrier-client boundary in `plugins/NativeLinkTransport/include/native_link_carrier_client.h`
    - the existing SHM IPC client now hangs off that boundary as the preserved reference backend instead of being the only implicit implementation path
    - plugin settings can now name `{"carrier":"shm"}` explicitly, and unsupported `tcp` selection fails fast instead of silently falling back to SHM
    - Robot_Simulation mirrors the same explicit carrier enum/config boundary in `Source/Application/DriverStation/DriverStation/NativeLink.h` so the simulator-owned authority path is ready for a later TCP carrier without changing session semantics again
  - roadmap checkpoint after discussing FRC-community rollout strategy:
    - `docs/native_link_rollout_strategy.md` now captures the intended long-term product shape: one Native Link semantic core, multiple carriers, and multiple dashboard/compatibility adapters
    - important product rule: do not force early adoption to mean robot-code rewrites; preserve a compatibility-first path alongside a richer native-first Java/C++ SDK path
    - cleanup decisions should now preserve both a carrier boundary and an adapter boundary so future SmartDashboard / Shuffleboard / Elastic / bridge work can share the same contract
    - `Robot_Simulation` should continue as the first reference authority/example, but should not be treated as the permanent home of all authority-side logic

## Known constraints / active considerations

- Current direct ring transport is effectively single-consumer due to shared read cursor.
- Deployment remains vcpkg/Qt-DLL based; static Qt distribution is not a current goal.
- Event-bus decoupling (topic subscriptions + rate limiting + coalescing) is documented as future work, not implemented.
- If startup false-dirty (`*`) behavior regresses, add a focused startup regression test that validates initial title/dirty state before any editable interaction.

## Standing conventions

- Add `Ian:` why-comments in code whenever a fix depends on non-obvious reasoning, ordering, lifecycle behavior, cross-process debugging lessons, or any context that would be expensive to rediscover in a later session.
- Prefer adding those comments at the time the code is written or immediately after the bug is understood, not as a later cleanup.
- Before ending a session or making a checkpoint commit, quickly review the changed code for any missing `Ian:` comments in the tricky paths.

## Next-session checklist

1. Keep `Agent_Session_Notes.md` lean; record durable milestones in `docs/project_history.md`.
2. If replay docs change, keep `docs/replay_parity_roadmap.md`, `docs/replay_user_manual.md`, and `docs/project_history.md` in sync.
3. Pick one focused roadmap item from `README.md` and `docs/requirements.md`.
4. Define acceptance criteria first, then implement in a small slice.
5. Run automated tests (`docs/testing.md`) plus one targeted manual validation loop.
6. Record durable milestone details in `docs/project_history.md`.

## Active follow-up log

- Manual paired testing status with Robot_Simulation Direct:
  - chooser operator flow works (`Just Move Forward` executes correctly)
  - dashboard reopen no longer overwrites robot-owned chooser selection
  - remembered numeric control state (`TestMove`) now survives dashboard restart again in the validated paired flow
  - real single-dashboard restart behavior is much healthier, though immediate telemetry paint after dashboard restart could still use follow-up hardening
  - extra concurrent observer/watch tooling can still perturb repeated runs, which is acceptable for now if direct mode remains effectively single-real-client
- Keep an eye on additional dashboard-owned keys that may need explicit scoped alias conventions documented for mixed legacy layouts.
- Official WPILib SmartDashboard did support `SendableChooser`; upcoming work should treat chooser support as a compatibility requirement, not a Shuffleboard-only feature.
