# Native Link Transport Contract

This document turns the initial `Native Link` design note into a concrete first transport contract.

It is still a design document, not an implementation promise, but it is specific enough to guide ABI work, plugin structure, and early reliability tests.

## First recommended deployment shape

`Native Link` requires one logical authority server per transport namespace.

Recommended first validation shape:

- `Robot_Simulation` hosts the authoritative Native Link server for integration testing
- SmartDashboard loads the `Native Link` plugin as a client of that server
- additional dashboards, probes, or passive watchers connect as separate clients
- the protocol stays server-authoritative so the same contract can later support a standalone broker if needed.

Host-integration note:

- multi-client support should be an explicit advertised transport capability
- SmartDashboard should only relax its normal single-instance guard when the selected transport advertises that capability and the operator intentionally chooses that mode.

This is the cleanest way to validate real robot-side authority while preserving the multi-client model from the start.

## Scope of v1

Native Link v1 should focus on correctness before breadth.

In scope:

- `bool`, `double`, `string`, and `string_array`
- explicit topic descriptors
- state vs command/event topic distinction
- snapshot-first then live delta flow
- per-update provenance
- freshness and TTL metadata
- explicit writer policy
- diagnostics for connection state, descriptor state, write rejection, and stale state.

Out of scope for the first implementation slice:

- arbitrary nested object schemas
- full historical replay/logging semantics inside the live transport
- lossy best-effort shortcuts that weaken correctness guarantees.

## Roles

The protocol should model roles explicitly.

- `server`
  - authoritative topic registry
  - authoritative session id and server sequence
  - enforces writer policy and TTL/freshness
- `publisher client`
  - sends state or command writes for topics it is allowed to write
- `observer client`
  - receives descriptors, snapshots, deltas, diagnostics
- `controller client`
  - observer plus permission to request ownership/lease for writable topics.

One process may act in more than one role, but the protocol should not depend on that shortcut.

## Session model

### Server session identity

Each server boot creates a new logical session:

- `server_session_id` = opaque unique identifier for the running authority instance
- `server_boot_time_us` = server clock origin or boot timestamp for diagnostics
- `server_sequence` = monotonic per-session update sequence for all authoritative deliveries.

When `server_session_id` changes, clients must treat previous ordering state as invalid and expect a fresh descriptor/state snapshot.

### Client identity

Each client connection carries:

- `client_id` = unique per process start
- `client_name` = human-readable name
- `client_role_mask` = observer/controller/publisher capabilities
- `client_instance_nonce` = optional entropy to distinguish same-name restarts.

The server assigns:

- `connection_id` = unique for this connected session.

### Connection phases

The expected connection flow is:

1. `hello`
   - protocol version
   - client identity
   - capability flags
   - requested subscription profile
   - last seen `server_session_id` and `server_sequence` when reconnecting
2. `welcome`
   - accepted protocol version
   - new `connection_id`
   - current `server_session_id`
   - server capabilities
3. `descriptor_snapshot_begin`
4. zero or more `topic_descriptor`
5. `descriptor_snapshot_end`
6. `state_snapshot_begin`
7. zero or more replayable state updates
8. `state_snapshot_end`
9. `live_begin`
10. live delta updates, command acks/rejections, diagnostics.

The boundary events matter. Clients should never have to guess whether an update belongs to a replay snapshot or to the live stream.

## Topic model

Each topic has a descriptor. Meaning should be carried primarily by the descriptor, not hidden inside the topic name.

### Required descriptor fields

- `topic_id`
  - server-assigned stable identifier for the current session
- `topic_path`
  - canonical UTF-8 path string used for display and compatibility mapping
- `topic_kind`
  - `state`, `command`, or `event`
- `value_type`
  - `bool`, `double`, `string`, `string_array`
- `schema_name`
  - optional semantic label such as `chooser.options` or `units.meters`
- `schema_version`
  - integer schema version
- `access_mode`
  - `read_only`, `server_writable`, `client_writable`, or `bidirectional_by_policy`
- `retention_mode`
  - `none` or `latest_value`
- `replay_on_subscribe`
  - non-zero only for replayable state topics
- `ttl_ms`
  - `0` means no TTL-based stale transition
- `writer_policy`
  - see writer policy section
- `owner_scope`
  - `server`, `robot`, `dashboard`, `lease_holder`, or `shared_authorized`
- `description`
  - optional human-readable help text.

### Descriptor revisioning

Descriptors themselves need revision tracking.

- `descriptor_revision`
  - increments when server-side meaning changes for a topic within the same session
- clients should treat descriptor revision change as a meaningful event and may refresh UI/config accordingly.

## Topic classes

### State topics

State topics represent current truth.

Rules:

- may be retained as latest authoritative value
- included in state snapshot when `replay_on_subscribe != 0`
- latest value includes provenance and freshness metadata
- if stale by TTL, they remain visible but marked stale
- they do not become commands just because they are writable.

Examples:

- robot pose estimate
- selected auton mode
- dashboard-owned numeric tuning value.

### Command topics

Command topics represent intentional writes that should not replay automatically.

Rules:

- never included in replay snapshot by default
- each accepted write should produce an explicit server ack or rejection
- rejections should be delivered back to the writing client with a machine-readable reason code
- server may optionally emit a related state topic update if the command changes state
- reconnect must not silently re-fire the previous command.

Examples:

- reset gyro request
- begin test sequence
- save calibration.

### Event topics

Event topics represent transient notifications from the authority side.

Rules:

- not retained
- not replayed by default
- may carry structured meaning through `schema_name`
- best used for notable occurrences, not for current truth.

Examples:

- brownout detected
- autonomous started
- subsystem fault raised.

## Update envelope

Every server-delivered update should carry enough metadata to make ordering and diagnostics explicit.

### Required fields

- `server_session_id`
- `server_sequence`
- `topic_id`
- `descriptor_revision`
- `source_client_id`
- `server_timestamp_us`
- `source_timestamp_us` when available, otherwise `0`
- `delivery_kind`
  - `snapshot_state`, `live_state`, `live_command_ack`, `live_command_reject`, `live_event`, `stale_transition`
- `value`
- `value_present`
  - useful for diagnostics or schema-specific nullability later.

### Freshness fields

- `ttl_ms`
- `age_ms_at_emit`
- `is_stale`
- `freshness_reason`
  - `live`, `ttl_expired`, `source_disconnected`, `replaced_by_new_session`, or `policy_unknown`.

## Writer policy

Native Link should treat writable topics as policy-governed resources.

### Writer policy values

- `server_only`
  - only server-side authority may write
- `single_client`
  - exactly one configured client id or role may write
- `lease_single_writer`
  - server grants an active lease to one client at a time
- `shared_deterministic`
  - multiple writers allowed, but server applies a documented deterministic resolution rule.

### Recommended default by topic kind

- `state`
  - default to `server_only` unless there is a clear operator-control case
- `command`
  - default to `lease_single_writer` for dashboard-originated actions
- `event`
  - normally `server_only`.

### Lease semantics

For operator-owned controls, `lease_single_writer` is the recommended default.

Required behavior:

- clients can request lease acquisition for a topic or topic group
- server grants lease with `lease_id`, `lease_holder_client_id`, and `lease_expiry_us`
- accepted writes must reference the active lease context
- lease can be renewed, released, or forcibly revoked by policy
- server emits lease-change diagnostics to all interested clients.

This gives multiple dashboard clients a deterministic ownership story instead of hidden last-wins behavior.

### Shared deterministic fallback

If a topic genuinely permits shared multi-writer updates, the resolution rule must be explicit.

Recommended minimum rule:

- sort by server receive order within a session
- break ties by `source_client_id`
- emit diagnostics when competing writes happen close together.

This should be rare and reserved for safe, non-actionable topics.

## Reconnect semantics

Reconnect behavior should prioritize correctness over optimism.

### Same server session

If the client reconnects and the server reports the same `server_session_id`, the server may optionally resume from the client's last acknowledged `server_sequence` if:

- the sequence window is still available
- no descriptor revisions required a full resync
- policy allows resume.

Otherwise the server falls back to full descriptor snapshot plus state snapshot.

### New server session

If the server restarted:

- client discards old sequence tracking for this transport session
- server sends descriptor snapshot and state snapshot from scratch
- any previous leases are invalid
- stale state caused by the old session should be marked superseded, not silently blended.

This directly addresses the ordering/restart lessons learned from `Direct`.

## Diagnostics and introspection

Diagnostics are a core transport feature, not optional polish.

### Required server-visible facts

The system should be able to surface:

- active `server_session_id`
- connected clients and roles
- topic descriptors and descriptor revisions
- latest state provenance per topic
- current staleness/freshness status per topic
- active writer lease holder per writable topic
- recent write rejections and reasons
- reconnect count, snapshot count, and stale transitions.

### First diagnostics surfaces to design for

Recommended order:

1. programmatic diagnostics query surface
2. structured log stream
3. SmartDashboard debug panel.

This order keeps testing and automation possible before UI work expands.

### Rejection reasons

Write rejections should be explicit and machine-readable.

Minimum rejection reasons:

- `unknown_topic`
- `wrong_type`
- `read_only`
- `lease_required`
- `lease_not_holder`
- `stale_client_session`
- `descriptor_mismatch`
- `policy_violation`
- `server_not_live`.

## Plugin boundary implications

The current plugin ABI v1 is intentionally small, but Native Link will likely need additive host/plugin surfaces beyond the present `publish_*` plus raw update callback model.

Likely next ABI needs:

- descriptor snapshot delivery callback(s)
- richer update callback metadata for provenance and freshness
- diagnostics query or diagnostics event callback
- optional write result callback for ack/reject outcomes.

These should be added as a new ABI revision rather than mutating the current v1 structs.

## Carrier boundary checkpoint

The current implementation checkpoint now treats carrier choice as an explicit
internal boundary under the Native Link semantic contract.

- protocol/session semantics remain shared above the carrier layer
- `shm` remains the current reference backend for startup/restart debugging
- `tcp` is intentionally not implemented yet, but carrier selection is already
  explicit so the future socket path does not need to reshape session semantics
- unsupported carrier selections should fail clearly instead of silently
  downgrading to another medium.

## Adapter boundary checkpoint

Long-term rollout also requires an explicit adapter boundary above the transport
contract, not only a carrier boundary below it.

- carriers are responsible for moving Native Link frames/semantics
- adapters are responsible for integrating Native Link with dashboard ecosystems
  or compatibility workflows such as SmartDashboard, Shuffleboard, Elastic, or
  a future NT bridge/service
- adapter-specific naming or widget behavior should not become part of the core
  semantic contract.

## Compatibility mapping for SmartDashboard host

The dashboard host should map Native Link semantics as follows:

- state topics become normal live variables with stale/conflict metadata available for UI presentation
- command topics become explicit actionable controls and should not be treated as retained values
- descriptor metadata drives widget defaults, writability, and optional teaching/debug text
- topic path remains the human-visible identity, but schema meaning comes from the descriptor first.

## First implementation recommendation

For the first implementation slice, prefer these defaults:

- one authoritative server inside `Robot_Simulation` for paired integration work
- one SmartDashboard plugin client implementation
- full-registry subscription instead of selective topic filters at first
- lease-based ownership for dashboard writable controls
- required descriptor snapshot on every new server session
- no command replay
- state replay only for topics explicitly marked retained and replayable.

Those defaults keep the first version small while preserving the architecture needed for reliability.
