# Native Link Initial Test Plan

This test plan focuses on transport correctness before widget breadth.

## Goals

- prove multi-client correctness early
- prove reconnect behavior under repeated process restarts
- prove commands do not replay accidentally
- prove ownership/conflict handling is deterministic
- prove diagnostics reflect actual transport behavior.

## Unit-level contract tests

### Descriptor model

- descriptor with valid required fields is accepted
- descriptor missing required field is rejected
- descriptor revision change is observable
- topic kind and retention policy combinations are validated
- invalid writable policy for event topic is rejected.

### Snapshot and ordering

- client receives descriptor snapshot before state snapshot
- client receives `state_snapshot_end` before `live_begin`
- live delta after snapshot advances monotonic `server_sequence`
- reconnect to new server session resets sequence expectations cleanly.

### Freshness and stale state

- retained state topic transitions to stale after TTL expiry
- stale transition preserves latest value but marks freshness state
- non-retained topic does not pretend to have replayable last value
- source disconnect can mark topic stale with explicit reason.

### Writer policy

- `server_only` topic rejects client write
- `single_client` topic accepts only configured owner
- `lease_single_writer` topic requires active lease
- revoked lease causes subsequent write rejection
- `shared_deterministic` topic resolves competing writes predictably.

### Command vs state behavior

- command topic write receives ack or reject
- command topic does not replay on reconnect
- retained state topic replays once during state snapshot
- replayed state topic does not double-apply after `live_begin` boundary.
- command rejection should be visible to the writing client with a machine-readable reason.

### Diagnostics

- write rejection emits explicit reason code
- diagnostics report active session id and connected clients
- diagnostics reflect current lease holder for writable topic
- diagnostics reflect stale topic status and freshness reason.

## Multi-client integration tests

### Many observers

- one robot authority, two dashboards, one passive watcher
- all observers receive descriptor snapshot and retained state snapshot
- one observer disconnecting does not perturb others
- no observer consumes another observer's updates.

Recommended sequencing:

- first prove the same behavior in the in-memory Native Link harness
- then validate the real SmartDashboard IPC client against an authority-style shared-memory/event server
- then validate two real SmartDashboard processes once the plugin path can explicitly advertise multi-client support and bypass single-instance enforcement for that transport.

### Many writers

- two dashboards contend for one leased writable topic
- only active lease holder writes successfully
- lease handoff is observable and deterministic
- rejected dashboard receives explicit rejection reason.

### Host multi-instance gating

- transports that do not advertise multi-client support keep normal single-instance behavior
- transports that do advertise multi-client support may opt into a controlled multi-process dashboard validation path
- two real SmartDashboard processes should be treated as a later confirmation layer after the in-memory harness passes.

### Restart reliability

- dashboard restart while server stays alive
- server restart while dashboards stay alive
- repeated alternating restarts over many iterations
- state snapshot after restart restores replayable state without replaying commands.

### Autonomous survive scenario

- dashboard sets replayable control state for auton selection and `TestMove`
- server publishes countdown `Timer` telemetry during a simulated autonomous run
- server publishes `Y_ft` progress only when the selected auton says to move forward
- single-run scenario confirms control replay plus live telemetry progression
- server-restart loop x5 confirms lease reset is explicit and control state can be re-established cleanly
- dashboard-reconnect loop x5 confirms retained state snapshots restore operator intent before telemetry continues.

## Robot_Simulation paired validation direction

Recommended first integration target:

- add a Native Link server mode to `D:\code\Robot_Simulation`
- keep `Legacy NT` as comparison oracle during validation
- use SmartDashboard Native Link plugin as one client and optional probes/watchers as extra clients.

Current SmartDashboard-side note:

- the old SmartDashboard-owned in-memory authority bridge is no longer the target path
- SmartDashboard tests should now cover the real shared-memory + named-events client path directly
- until the full combined Native Link `ctest` slice is stable, treat startup/restart ordering as the primary bug-hunting area, not the finished state.

### Suggested paired baseline topics

- state:
  - `Integration/Armed`
  - `Integration/Speed`
  - `Integration/Status`
  - `TestMove`
- command:
  - `Integration/Reset`
- chooser-style state set:
  - `Test/Auton_Selection/AutoChooser/options`
  - `Test/Auton_Selection/AutoChooser/default`
  - `Test/Auton_Selection/AutoChooser/active`
  - `Test/Auton_Selection/AutoChooser/selected`

### Paired validation loops

1. single dashboard plus simulator authority
2. dashboard restart survive loop
3. simulator restart survive loop
4. two dashboards connected at once
5. two dashboards competing for a leased writable control
6. passive watcher added during stress loop.

## Carrier parity plan

Native Link should eventually validate the same semantics against more than one
carrier.

Target carriers:

- `shm`: local shared memory + named events
- `tcp`: localhost TCP as the intended long-term general-purpose backend

Parity expectations:

- the same snapshot/session/lease semantics must pass against both carriers
- focused restart/registry/client tests should be runnable per carrier
- paired real-process shared-state smoke should remain available per carrier
- any failure that reproduces on one carrier but not the other should be called
  out as either a semantic bug or a carrier-specific bug.

Diagnostic policy:

- keep `shm` as the simpler hot-swappable diagnostic carrier even after `tcp`
  becomes the default
- use `shm` as the reference path when isolating new ordering/reconnect bugs in
  `tcp`.

Current checkpoint additions:

- validate carrier-name parsing independently from transport startup
- validate that explicit `tcp` selection fails cleanly until the TCP carrier is
  implemented, rather than silently reusing the SHM backend.

## Acceptance signal for first implementation slice

Do not treat Native Link v1 as ready until these all pass:

- repeated restart loops are stable
- many observers do not interfere with each other
- commands never replay on reconnect
- writable-topic conflicts are visible and deterministic
- diagnostics explain every observed failure mode in testing.
