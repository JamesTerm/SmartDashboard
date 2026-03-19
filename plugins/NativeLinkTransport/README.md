# Native Link Transport Plugin

This directory is the starting point for a native generic SmartDashboard transport plugin.

Build note:

- `SMARTDASHBOARD_BUILD_PLUGIN_NATIVE_LINK` is intentionally `OFF` by default so a future merge to `main` can keep stable builds from exposing unfinished Native Link plugin behavior.
- On the feature branch, enable it explicitly when validating this work: `cmake -S . -B build -DSMARTDASHBOARD_BUILD_PLUGIN_NATIVE_LINK=ON`

The goal is not to recreate NetworkTables-style shared state. The goal is to define a transport whose semantics match SmartDashboard's own product needs: reliable reconnects, explicit state ownership, strong diagnostics, and correct multi-client behavior from day one.

## Why this exists

The current `Direct` transport taught us useful lessons, but it also exposed limits that should not become the foundation for a long-lived generic transport:

- the original ring-buffer shape was effectively single-consumer and became fragile under extra observers
- retained latest-value behavior helped restart recovery, but conflict rules and freshness rules remained too implicit
- startup/reconnect bugs were often ordering bugs, not just wire-format bugs
- command-like actions and replayable state should not be treated as the same kind of topic
- helper tools and tests must behave like the real ownership model or they can hide the real problem.

`Native Link` should keep the good parts from `Direct` while deliberately correcting the weak parts.

## Problem statement

NT-style shared mutable state is a useful compatibility model, but it falls short for a product-owned native transport when we care about:

- multiple robot-side and dashboard-side clients connected at the same time
- reliable reconnect after either side restarts
- explicit write authority for actionable topics
- clear freshness, replay, and stale-data semantics
- diagnostics that help students and teams understand why a value behaved the way it did.

What we want to avoid:

- a giant shared mutable key/value bag with no ownership model
- treating commands exactly like retained state
- silent multi-writer last-wins behavior on actionable topics
- making freshness implicit
- encoding all schema meaning into topic names.

## Core transport semantics

`Native Link` should be designed around these transport-level semantics.

### 1. Server-authoritative, multi-client from day one

- one logical server session owns topic registry, sequencing, replay rules, and conflict policy
- many producers and many consumers may connect concurrently
- reconnect and late-join behavior must be defined against the server, not against peer timing luck
- the transport contract should assume both robot-side and dashboard-side process restarts will happen repeatedly.

### 2. Explicit topic descriptors

Each topic should have a descriptor that is discoverable through introspection, not inferred only from its name.

Minimum descriptor fields:

- stable topic id/path
- value type
- schema or payload version
- writable vs read-only
- retained vs non-retained
- stale/TTL policy
- writer policy / ownership model
- optional human-readable description or category.

This lets the host and tools reason about the topic without reverse-engineering naming conventions.

### 3. State topics are different from command/event topics

The transport should model this distinction explicitly.

- `state` topics
  - replay on subscribe/connect by default
  - carry latest authoritative value
  - may be retained with freshness metadata
- `command` or `event` topics
  - do not replay by default
  - should be delivered as intentional actions, not as persistent truth
  - require stronger write/conflict rules because accidental replays are dangerous.

This separation is one of the biggest ways to avoid repeating old shared-state mistakes.

### 4. Snapshot first, then delta stream

On connect or reconnect, the expected flow should be:

1. negotiate session and capabilities
2. receive a coherent snapshot for replayable state topics
3. switch to live delta updates after the snapshot boundary
4. expose a clear generation/session boundary so the client knows which stream it is observing.

This matters because the `Direct` work showed that reconnect reliability depends heavily on ordering and session boundaries.

### 5. Per-update provenance

Every update delivered through the transport should be able to carry:

- source/client id
- server timestamp
- source timestamp when available
- sequence number
- session/generation id.

That metadata is important for ordering, debugging, stale detection, and explaining behavior in tooling.

### 6. Explicit conflict handling for writable topics

Writable topics should never rely on silent accidental multi-writer semantics.

At minimum, the transport design should define how a writable topic declares one of these policies:

- single designated writer
- server arbitrated writer lease/ownership
- explicitly shared multi-writer with deterministic resolution.

If a write is rejected, deferred, stale, or superseded, diagnostics should make that visible.

### 7. Freshness must be explicit

If retained data can become stale, the transport needs explicit policy rather than hidden assumptions.

Examples:

- TTL per topic or topic class
- retained snapshot includes age/freshness metadata
- clients can surface `stale` vs `live` distinctly
- non-retained topics simply do not pretend to have a last-known-truth value.

## Minimum contract for multi-client correctness

The transport implementation is free to choose its wire format and internal data structures, but a correct design should guarantee the following:

1. each client can join without consuming data meant for another client
2. reconnecting clients can recover authoritative replayable state without depending on another client remaining alive
3. commands/events are not accidentally re-fired because a retained-state mechanism replayed them
4. sequence/session metadata makes restarts and rollover visible
5. the server can tell clients when data is stale, rejected, conflicted, or out of policy
6. introspection can explain topic descriptors, latest provenance, and current writer status.

This is the core reason to avoid a shared single-read-cursor design.

## What belongs at transport level vs higher layers

Transport-level responsibilities:

- connection/session management
- topic descriptor registry
- snapshot + delta delivery
- provenance metadata
- freshness metadata
- writer/conflict policy enforcement
- diagnostics/introspection surfaces
- basic publish/subscribe semantics.

Higher-level widget/app responsibilities:

- how a tile renders stale or conflicted data
- dashboard-specific naming shortcuts or grouping
- operator UX for choosing among writable controls
- layout persistence
- richer domain logic composed from multiple topics.

The transport should provide the truth and policy. The app should decide how to present it.

## Reusable implementation shape

The public plugin boundary should remain a stable C ABI.

Under that boundary, it is reasonable and recommended to build a reusable C++ layer:

- C ABI at the plugin edge for long-term binary stability
- C++ implementation internally for safer structure and reuse
- optional helper classes/base types for future plugin authors
- examples that teach why the ABI is C even though the implementation is modern C++.

That split matches the current SmartDashboard plugin architecture and should be preserved.

## Direct lessons to carry forward

From the current repository history, the most important lessons to keep in view are:

- multi-observer safety must be a first-class design requirement, not a later patch
- startup and reconnect ordering need explicit tests
- retained state is useful only when ownership, freshness, and replay rules are documented
- helper tools must mimic real session ownership or they will produce misleading confidence
- diagnostics should be strong enough to explain missing data, stale data, rejected writes, and reconnect state transitions
- atomic contract tests are worth the effort because reliability here will come from many small guarantees.

## Initial validation strategy

Before building a large feature set, prioritize small correctness tests around the transport core.

Suggested first test categories:

- topic descriptor parse/validation
- snapshot then delta ordering
- reconnect generation boundaries
- stale/TTL transitions
- writer-ownership acquisition and rejection
- command topics not replaying on reconnect
- retained state topics replaying exactly once per snapshot generation
- many-reader fan-out without shared-consumer loss
- many-writer conflict cases with deterministic outcomes
- diagnostics/introspection reflecting the real transport state.

## Open design questions for the next slice

These are not old-session blockers, but they do need explicit decisions before implementation gets deep:

1. Should `Native Link` run with an in-process embedded server, an external broker process, or both?
2. What is the minimum value/schema set for v1 beyond `bool`, `double`, `string`, and string arrays?
3. What is the exact ownership model for dashboard-originated writable controls?
4. Which diagnostics surface should come first: API query, log stream, debug panel, or all three?
5. How much of descriptor/schema validation should be enforced by the transport versus treated as advisory metadata?

## Immediate next step

Use this note as the acceptance baseline for the first `Native Link` architecture slice. The next document should turn these principles into a concrete transport contract: session model, topic descriptor schema, wire/message flow, and test plan.

Concrete follow-on documents now live here:

- `plugins/NativeLinkTransport/CONTRACT.md`
- `plugins/NativeLinkTransport/TEST_PLAN.md`

Initial test-driven implementation scaffold now exists here:

- `plugins/NativeLinkTransport/include/native_link_core.h`
- `plugins/NativeLinkTransport/src/native_link_core.cpp`
- `plugins/NativeLinkTransport/tests/native_link_core_tests.cpp`
- `plugins/NativeLinkTransport/src/native_link_transport_plugin.cpp`
- `plugins/NativeLinkTransport/tests/native_link_plugin_tests.cpp`

The current tests now include an in-memory autonomous survive scenario that mirrors the earlier manual Direct stress pattern in smaller form: replayable dashboard-owned control state, live `Timer` telemetry, conditional `Y_ft` movement, repeated server restarts, and repeated dashboard reconnects.

The next planned stress slice is explicit multi-client validation: first two-dashboard fan-out and ownership tests in the in-memory harness, then later two real SmartDashboard processes once the Native Link plugin can advertise multi-client capability and the app can relax single-instance enforcement for that transport.

That later real-process validation should stay capability-gated: SmartDashboard's normal single-instance behavior remains the safe default, and only transports that explicitly advertise multi-client support should be allowed to opt into multi-instance dashboard testing.

The next semantic gap to close after multi-client fan-out is command handling: explicit ack/reject feedback and hard guarantees that command topics do not replay across reconnects.

## Carrier roadmap

The current real IPC path uses local shared memory + named events as the v1
carrier. That remains valuable, but it should not be treated as the only or
final transport medium.

Planned direction:

- keep one Native Link protocol/semantic contract above the carrier layer
- preserve the current shared-memory + named-events carrier as a diagnostic and
  local-reference backend
- add a TCP carrier as the intended long-term general-purpose backend
- make carrier selection runtime-configurable for debugging and reproduction,
  with optional compile-time guards if needed later.

Recommended carrier phases:

1. freeze the current shared-memory carrier as the reference implementation for
   local startup/restart debugging
2. extract a clearer carrier boundary so protocol/session semantics are shared
   above both backends
3. implement localhost TCP under the same snapshot/session/lease contract
4. run the same focused Native Link tests against both carriers
5. promote TCP to the default production carrier once parity is proven
6. keep shared memory available as the simpler diagnostic hot-swap path.

### Why keep the shared-memory carrier

- it is easier to reason about during ordering bugs
- it already has focused startup/restart coverage in this branch
- it gives us a simpler isolation path when a future TCP bug appears and we
  need to answer "is the bug in Native Link semantics or only in the medium?"

### Suggested carrier-selection shape

Preferred long-term shape:

- runtime plugin setting such as `{"carrier":"tcp"}` or `{"carrier":"shm"}`
- simulator-side selection mirrors the same carrier choice
- optional compile-time guards may still exist (`NATIVE_LINK_ENABLE_TCP_CARRIER`,
  `NATIVE_LINK_ENABLE_SHM_CARRIER`), but runtime choice is more useful for
  reproduction and diagnostics.

### Finish / merge expectations

Native Link should be considered ready to merge back toward `main` only when:

- the focused restart/registry/client test slice is stable
- the paired real-process shared-state probe is stable
- Robot_Simulation Native Link tests stay green
- command/event no-replay behavior is locked down
- lease/ownership behavior is deterministic and diagnosable
- the shared-memory carrier is preserved as a diagnostic backend even if TCP is
  promoted as the default carrier.

On the SmartDashboard host side, the first enabling step for later real two-process validation is now clear: startup should keep single-instance enforcement by default, but accept an explicit multi-instance testing flag only when the selected transport advertises multi-client support.

There is now also a first real Native Link plugin scaffold behind the C ABI. It currently advertises chooser + multi-client support, registers a small default topic set, accepts host publishes into the in-memory core, and is intentionally narrow: enough for SmartDashboard discovery/build validation without claiming the full transport runtime is finished.

The host-side discovery path is now also covered by a focused SmartDashboard test. That gives us a lightweight confirmation that a built `native-link` plugin can be discovered by the transport registry before we spend time on full two-process validation.

Plugin deployment is now also wired into the normal build graph so `SmartDashboardApp` and `SmartDashboard_tests` depend on the Native Link plugin target. That keeps discovery tests honest and avoids relying on ad hoc copying when we move toward real multi-process validation.

We now also have a host-side runtime smoke test that starts the plugin transport through `DashboardTransportRegistry`, confirms initial retained state arrives, and confirms host publishes round-trip into the in-memory Native Link core. That gives us a practical bridge between pure core tests and future multi-process SmartDashboard validation.

For the first real multi-process checkpoint, the repo now also has a small helper at `tools/native_link_multi_instance_smoke.py` that launches two SmartDashboard processes and verifies the second one is only allowed when the Native Link multi-instance startup path is active.

That helper now also forces SmartDashboard's persisted transport selection to `native-link` first, so the startup smoke is explicitly exercising the Native Link path rather than only proving generic process coexistence.

The next helper now exists too: `tools/native_link_shared_state_probe.py`. It uses per-instance UI debug logs to confirm that two real SmartDashboard processes both start on Native Link and both observe the same initial retained shared state.

That probe is now passing for shared startup state, so the next real threshold is stronger: proving that one dashboard-originated write becomes visible to the other dashboard through the same Native Link authority.

That SmartDashboard-owned shared-authority bridge has now been replaced by a real shared-memory + named-events IPC client on the dashboard side. The authoritative Native Link server is expected to live outside SmartDashboard, with `Robot_Simulation` as the intended first real authority.

For SmartDashboard-side validation, this repo now also contains a focused IPC harness server used only by automated tests. It exists to exercise the real client/runtime/plugin path without reintroducing dashboard-owned production authority semantics.

Current status note:

- the architecture shift to real simulator-style authority is now in place on the SmartDashboard side
- focused IPC tests and focused registry/plugin tests pass locally
- first carrier-abstraction checkpoint is now in place under that authority path:
  - `plugins/NativeLinkTransport/include/native_link_carrier_client.h` defines the shared client-side carrier boundary
  - the current SHM IPC path is preserved behind that boundary as the diagnostic/reference backend
  - plugin settings can now select `{"carrier":"shm"}` or `{"carrier":"tcp","host":"127.0.0.1","port":5810}` explicitly
- Native Link rollout strategy is now documented in `docs/native_link_rollout_strategy.md`:
  - carrier work should remain subordinate to one semantic contract
  - dashboard-specific integrations should live behind adapter boundaries, not leak into the core
  - the long-term adoption story should include both compatibility-first bridges and native-first Java/C++ SDKs
- one remaining area still needs stabilization: deterministic startup/restart ordering across the full combined Native Link test slice. Do not treat the real IPC path as fully finished until the combined suite is consistently green.
