# SmartDashboard (C++) Design

- Edit this file when technical architecture or implementation design changes.

## Purpose

Build a focused C++ dashboard inspired by WPILib SmartDashboard, but intentionally trimmed to the core use case:

- Render live variables of `bool`, `double`, and `string`
- Let users choose/change widget type per variable
- Let users move/arrange widgets and save/load layout
- Use direct transport as the baseline and preserve a clean adapter boundary for optional NetworkTables support

This document defines scope, architecture, first-iteration boundaries, and implementation-ready decisions.

## Product Scope (Pruned vs original Java SmartDashboard)

### In scope

- Runtime discovery of variables by key
- Default widget assignment per type
- Edit mode for moving/resizing widgets
- Right-click context menu for:
  - Change widget type
  - Open properties
- Layout persistence (save/load)
- Connection status indicator
- Receiving data from another process through `*_direct` interface

### Out of scope (initially)

- Full SmartDashboard plugin ecosystem
- Command/subsystem integrations from WPILib
- LiveWindow/test mode parity
- Send-to-robot controls/buttons (can be added later)
- Full NetworkTables feature parity (protocol details, tools integration, and long-tail topic/property options)

## Project Structure (Solution)

Three projects for the first phase:

1. `ClientInterface_direct`
   - Producer-side adapter used by external client code
   - Publishes `bool/double/string` updates

2. `SmartDashboard_Interface_direct`
   - Consumer-side transport abstraction used by the dashboard app
   - Receives updates and exposes them as a stream/callback interface

3. `SmartDashboard`
   - Desktop UI app and dashboard logic
   - Uses `SmartDashboard_Interface_direct` for inbound data

Note: your message listed `SmartDashbaord`; this design assumes the final app project is `SmartDashboard`.

## Framework Decision: Qt vs Win32

## Decision

Choose **Qt** for this project.

This decision is now locked for v1.

## Why Qt

- Built-in dockable/interactive widgets, model-view support, context menus, property dialogs
- Easier drag/move/resize behavior than raw Win32 from scratch
- Better velocity for iterative widget development
- Strong JSON and persistence support (`QJsonDocument`, `QSettings`)
- Event loop and threading primitives align well with async transport input

## Why not raw Win32 for v1

- More boilerplate for layout, property editing, rendering controls
- Higher implementation cost for equivalent UX
- Slower iteration when we know widget system will evolve

## UI/UX Workflow (matching SmartDashboard concepts)

Behavior target based on legacy SmartDashboard usage:

1. New variable appears -> create default widget and place on canvas/grid
2. User toggles `Editable` mode
3. User drags/resizes widget
4. User right-clicks widget -> `Change to...` -> chooses compatible widget
5. User right-clicks widget -> `Properties...` -> edits widget-specific settings
6. Layout can be saved and restored on next launch

## Data Model

## Variable Identity

- Key: string path-like identifier (example: `Drive/Speed`)
- Type: `bool | double | string`
- Value: latest value
- Timestamp: optional source timestamp or receive time

## Core Structures

- `VariableRecord`
  - `key`
  - `type`
  - `value`
  - `lastUpdate`

- `WidgetBinding`
  - `widgetId`
  - `variableKey`
  - `widgetType`
  - `geometry`
  - `properties` (JSON object)

- `DashboardLayout`
  - `version`
  - `widgets[]`
  - optional global UI settings

## Widget System

## Type compatibility

- `bool` widgets:
  - Text (`True/False`)
  - LED/indicator
  - Toggle display

- `double` widgets:
  - Numeric text
  - Bar/progress
  - Dial/gauge
  - Line plot (history)

- `string` widgets:
  - Text label
  - Multiline text box (read-only)

## Defaults

- `bool` -> LED indicator
- `double` -> Numeric text
- `string` -> Text label

## Widget Registry

Central registry maps:

- widget type id -> factory
- widget type id -> supported variable types
- widget type id -> default properties schema

This enables right-click `Change to...` filtering by variable type.

## Layout Persistence

Use JSON file (human-readable, versioned).

Example:

```json
{
  "version": 1,
  "widgets": [
    {
      "widgetId": "w-1",
      "variableKey": "Drive/Speed",
      "widgetType": "double.numeric",
      "geometry": { "x": 24, "y": 32, "w": 180, "h": 64 },
      "properties": { "precision": 2 }
    }
  ]
}
```

Rules:

- Save on explicit action and on graceful shutdown
- Unknown widget types in file: skip with warning
- Missing variables at startup: keep widget placeholder until data arrives

## Direct Transport Design (`*_direct`)

Use a transport abstraction so we can swap implementation later.

Design constraint: transport libraries must be plain C++/Win32 and must not depend on Qt. Qt is UI-only in `SmartDashboard`.

## Interface contract

- `ITransportPublisher` (client side)
  - Publish upsert/value update by key/type/value

- `ITransportSubscriber` (dashboard side)
  - Start/stop listening
  - Callback/event stream for messages
  - Connection state notifications

## Recommended v1 mechanism

**Memory-mapped file + named event pair (Windows)**

- Shared ring buffer in memory-mapped file
- Producer signals event `data_available`
- Consumer wakes, drains messages
- Optional `consumer_alive`/heartbeat event for status

Why this over events only:

- Named events alone only signal occurrence, not payload transport
- Shared memory gives low-latency payload transfer
- Keeps design close to your "direct" intent

### Qt independence

- `ClientInterface_direct` and `SmartDashboard_Interface_direct` expose non-Qt APIs
- `SmartDashboard` adapts subscriber callbacks into Qt signals/slots
- This keeps sender-side integration lightweight for large existing Windows applications

## IPC Memory Layout (v1)

Use a single-producer/single-consumer ring buffer in shared memory.

- Shared header:
  - `version`
  - `capacity`
  - `writeIndex`
  - `readIndex`
  - `dropCount`
  - `lastProducerHeartbeat`
  - `lastConsumerHeartbeat`
- Ring payload:
  - sequence of framed messages (`len + bytes`)

Synchronization:

- Producer writes message bytes, then advances `writeIndex`, then signals `data_available`
- Consumer wakes, drains until `readIndex == writeIndex`
- Heartbeats are updated periodically by each side for connection/stale detection

## Message schema (v1)

Minimal binary or JSON-line envelope; recommended fields:

- `messageType` = `upsert`
- `key`
- `valueType` (`bool|double|string`)
- `value`
- `seq` (uint64)
- `timestamp` (optional)

For first implementation speed, JSON-lines in shared memory is acceptable; optimize to binary later.

Recommended for v1 implementation: compact binary envelope for predictable parse cost and low allocation.

## Delivery Strategy and Latency

Expected workload (about 10 variables every 16 ms) is very light for this design.

Decision for v1: **hybrid coalesced delivery**.

- Producer API is on-demand (`PublishX(key, value)`)
- Calls update a per-key latest-value cache (coalescing)
- A dedicated flush loop emits a batch every 16 ms (configurable)
- Optional fast path: if caller requests immediate flush, signal instantly

Why this strategy:

- Preserves the latest value semantics you described
- Avoids flooding duplicate intermediate updates
- Keeps latency bounded (typically <= 16 ms transport-side)
- Still simple to reason about and debug

Consumer-side apply strategy:

- Drain all pending transport messages when signaled
- Apply only the latest value per key to the UI model per processing tick
- UI render/update at normal desktop cadence (for example 60 Hz) to avoid unnecessary redraw churn

Timing defaults for v1:

- `flush_period_ms = 16`
- `stale_timeout_ms = 250`
- `heartbeat_period_ms = 100`

These defaults should be config values so they can be tuned without architecture changes.

## C++ Header Sketch (v1)

The following is a concrete sketch for interfaces and wire structures. Names can be adjusted during implementation, but behavior should stay consistent.

### Common Types (`SmartDashboard_Interface_direct/include/sd_direct_types.h`)

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace sd::direct
{
    enum class ValueType : std::uint8_t
    {
        Bool = 1,
        Double = 2,
        String = 3
    };

    using ValueVariant = std::variant<bool, double, std::string>;

    struct VariableUpdate
    {
        std::string key;
        ValueType type;
        ValueVariant value;
        std::uint64_t seq;
        std::uint64_t sourceTimestampUs;
    };

    enum class ConnectionState : std::uint8_t
    {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        Stale = 3
    };
}
```

### Publisher API (`ClientInterface_direct/include/sd_direct_publisher.h`)

```cpp
#pragma once

#include "sd_direct_types.h"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace sd::direct
{
    struct PublisherConfig
    {
        std::wstring mappingName = L"Local\\SmartDashboard.Direct.Buffer";
        std::wstring dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable";
        std::wstring heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat";
        std::uint32_t ringBufferBytes = 1U << 20;
        std::chrono::milliseconds flushPeriod {16};
        bool autoFlushThread = true;
    };

    class IDirectPublisher
    {
    public:
        virtual ~IDirectPublisher() = default;

        virtual bool Start() = 0;
        virtual void Stop() = 0;

        virtual void PublishBool(std::string_view key, bool value) = 0;
        virtual void PublishDouble(std::string_view key, double value) = 0;
        virtual void PublishString(std::string_view key, std::string_view value) = 0;

        // Flushes current coalesced values immediately.
        virtual bool FlushNow() = 0;

        // Useful diagnostics
        virtual std::uint64_t GetPublishedSeq() const = 0;
        virtual std::uint64_t GetDroppedCount() const = 0;
    };

    // Factory (implemented in ClientInterface_direct)
    std::unique_ptr<IDirectPublisher> CreateDirectPublisher(const PublisherConfig& cfg);
}
```

### Subscriber API (`SmartDashboard_Interface_direct/include/sd_direct_subscriber.h`)

```cpp
#pragma once

#include "sd_direct_types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace sd::direct
{
    using UpdateCallback = std::function<void(const VariableUpdate&)>;
    using StateCallback = std::function<void(ConnectionState)>;

    struct SubscriberConfig
    {
        std::wstring mappingName = L"Local\\SmartDashboard.Direct.Buffer";
        std::wstring dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable";
        std::wstring heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat";
        std::chrono::milliseconds waitTimeout {50};
        std::chrono::milliseconds staleTimeout {250};
        std::chrono::milliseconds heartbeatPeriod {100};
    };

    class IDirectSubscriber
    {
    public:
        virtual ~IDirectSubscriber() = default;

        virtual bool Start(UpdateCallback onUpdate, StateCallback onState) = 0;
        virtual void Stop() = 0;

        virtual ConnectionState GetState() const = 0;
        virtual std::uint64_t GetLastSeq() const = 0;
        virtual std::uint64_t GetDroppedCount() const = 0;
    };

    // Factory (implemented in SmartDashboard_Interface_direct)
    std::unique_ptr<IDirectSubscriber> CreateDirectSubscriber(const SubscriberConfig& cfg);
}
```

### Shared Memory Wire Structures (`*_direct/src/sd_direct_wire.h`)

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace sd::direct::wire
{
    constexpr std::uint32_t kMagic = 0x53444442; // 'SDDB'
    constexpr std::uint16_t kVersion = 1;
    constexpr std::size_t kKeyMax = 128;
    constexpr std::size_t kStringMax = 256;

    enum class MsgType : std::uint8_t
    {
        Upsert = 1
    };

    enum class WireValueType : std::uint8_t
    {
        Bool = 1,
        Double = 2,
        String = 3
    };

    struct alignas(8) RingHeader
    {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t reserved0;

        std::uint32_t capacityBytes;
        std::atomic<std::uint32_t> writeIndex;
        std::atomic<std::uint32_t> readIndex;

        std::atomic<std::uint64_t> publishedSeq;
        std::atomic<std::uint64_t> droppedCount;
        std::atomic<std::uint64_t> lastProducerHeartbeatUs;
        std::atomic<std::uint64_t> lastConsumerHeartbeatUs;
    };

    struct alignas(8) MessageHeader
    {
        std::uint16_t messageBytes; // header + payload bytes
        std::uint8_t messageType;   // MsgType
        std::uint8_t valueType;     // WireValueType
        std::uint64_t seq;
        std::uint64_t sourceTimestampUs;
        std::uint16_t keyLen;
        std::uint16_t valueLen;     // only for string
    };

    // Payload layout after MessageHeader:
    // key bytes [keyLen], then value bytes:
    // - bool: 1 byte
    // - double: 8 bytes
    // - string: [valueLen] bytes (UTF-8)
}
```

### SmartDashboard Adapter Sketch (`SmartDashboard/src/transport/dashboard_transport.h`)

```cpp
#pragma once

namespace sd::transport
{
    enum class TransportKind { Direct, NetworkTables };
    enum class ConnectionState { Disconnected, Connecting, Connected, Stale };

    struct ConnectionConfig
    {
        TransportKind kind = TransportKind::Direct;
        QString ntHost = "127.0.0.1";
        int ntTeam = 0;
        bool ntUseTeam = true;
    };

    class IDashboardTransport
    {
    public:
        virtual ~IDashboardTransport() = default;
        virtual bool Start(VariableUpdateCallback, ConnectionStateCallback) = 0;
        virtual void Stop() = 0;
        virtual bool PublishBool(const QString& key, bool value) = 0;
        virtual bool PublishDouble(const QString& key, double value) = 0;
        virtual bool PublishString(const QString& key, const QString& value) = 0;
    };
}
```

Implementation note:

- `dashboard_transport` is the Qt-facing transport boundary used by `MainWindow`.
- Direct transport implementation remains Qt-free in `*_direct` and is wrapped behind the adapter interface.
- NT transport can be added without changing widget/model flow.

## Threading Model

- Transport thread receives messages
- Converts to internal updates
- Marshals to UI thread via Qt signal/slot queued connection
- UI thread updates model and widget views

Do not let transport thread touch QWidget instances directly.

## Connection State

Expose connection state in main window title and optional indicator widget.

States:

- Disconnected
- Connecting
- Connected
- Stale (no updates for timeout interval)

## Iteration Plan

## Iteration 1 (implemented)

Goal: receiving end with usable UI for `bool/double/string`.

Deliverables:

- Qt app skeleton (`SmartDashboard`)
- `SmartDashboard_Interface_direct` subscriber skeleton
- In-app variable model + default widgets
- Editable mode + drag/move basic behavior
- Right-click `Change to...` for type-compatible widgets
- Layout save/load JSON
- Connection indicator
- Minimal test publisher in `ClientInterface_direct` to feed sample data

Non-goals for iteration 1:

- Full property editor depth for every widget
- Advanced plotting features
- Bidirectional command controls

## Iteration 2 (implemented)

- Robust property editing dialog + per-widget schemas
- Better layout ergonomics (snap/grid, align)
- History buffer tuning for plots
- Improved transport diagnostics and error handling
- Bidirectional command channel (dashboard -> app) for editable widgets

## Iteration 3 (next focus)

- Optional alternate transport(s), starting with classic NetworkTables TCP/IP client mode
- Plugin-like widget extension points
- Import/export profiles and richer preferences
- Transport-parity tests (direct + NetworkTables adapters should pass the same retained/command semantics where applicable)
- Connection UX: explicit transport selection + manual connect/disconnect flow

### Iteration 3 Definition of Done (NetworkTables transport slice)

- `Transport` selector exists in UI and supports `Direct` and `NetworkTables`
- NT mode supports explicit endpoint config (team number and/or host/IP) with connect/disconnect actions
- Dashboard connection state indicator correctly reflects NT lifecycle (`Disconnected`, `Connecting`, `Connected`, `Stale`)
- Existing widget/layout behavior remains unchanged when switching transports
- Automated parity tests pass for direct adapter baseline and NT adapter telemetry/command roundtrip (bool/double/string)
- Automated reconnect test passes for NT adapter and verifies latest-value replay after reconnect
- Localhost simulation test path is documented and runnable in CI/dev workflow
- Design and testing docs are updated with commands and expected outcomes for NT validation

## Bidirectional Support Status (Dashboard -> Application)

Bidirectional support is implemented in direct transport mode for bool/double/string writable controls.
The architecture still keeps this behavior adapter-driven so alternate transports can implement equivalent command flow.

### Design approach

Use two logical channels in shared memory:

- Telemetry channel: app -> dashboard (already in v1)
- Command channel: dashboard -> app (new)

Each channel can be its own ring buffer + event pair, or one mapping with two ring regions.

### Message types to add

- `set_value_request`
  - `requestId`, `key`, `valueType`, `value`, `timestamp`
- `set_value_ack`
  - `requestId`, `status`, optional `errorText`
- optional `set_value_reject` if app enforces permissions/ranges

### UI behavior recommendation

- Editable dashboard control changes create `set_value_request`
- UI shows pending state until ack/reject
- On reject, revert control to last telemetry value and show brief error
- Last telemetry value remains source of truth for displayed state

### Safety and correctness rules

- Mark variables as `read_only` or `read_write` in metadata
- Enforce type and range checks on both sides
- Make requests idempotent where possible
- Time out unacked requests and surface state to user

Because v1 already has transport abstraction and message framing, this extension remained additive and low-risk to the existing receive path.

## NetworkTables Adapter Direction (next)

The next transport milestone is a classic NT TCP/IP adapter while keeping the current direct path intact.

Key alignment points:

- Robot program is expected to run the NT server role in normal FRC operation.
- Dashboard runs as an NT client and connects to robot host/team-address (or explicit host override).
- NT server retains cached topic values and replays latest values to late-joining subscribers.
- Dashboard-side retained cache remains useful for adapter parity and reconnect UX, but server-retained values remain source-of-truth for NT sessions.

Initial implementation slice:

1. Add a transport adapter contract test suite that can run against each adapter implementation.
2. Implement an NT-backed subscriber/publisher adapter pair behind the existing dashboard/model boundaries.
3. Add explicit connect/disconnect UX (manual connect button + endpoint/team settings).
4. Validate with localhost simulation using a real NT server/client pair in automated tests and manual dashboard loop.

## Connection UX Direction (for transport switching)

To keep interfaces clean while enabling transport growth, the UI should treat connection setup as adapter selection + endpoint settings.

Recommended v1 of this UX:

- Transport selector: `Direct (local IPC)` | `NetworkTables (NT4 TCP/IP)`
- NT connection fields: Team Number and/or Host/IP, optional Client Name
- Explicit action buttons: Connect, Disconnect
- Status display remains adapter-agnostic (`Disconnected`, `Connecting`, `Connected`, `Stale`)
- Widget/layout behavior is unchanged when transport changes; only backend adapter wiring changes

Architecture rule:

- UI binds to one transport-agnostic controller interface; transport-specific logic remains in adapter implementations
- No widget code branches by transport type

Validation baseline for this slice:

- automated: localhost server/client roundtrip tests for bool/double/string telemetry and command writes
- automated: reconnect test verifying latest value replay after client reconnect
- manual: simulator + dashboard end-to-end check using known robot/sim keys

Current implementation note:

- A legacy NT2-compatible client transport is now implemented in-tree (inside this repository), removing build dependency on external Robot_Simulation sources.
- Robot_Simulation remains the validation target/reference server for behavioral parity checks.

## NetworkTables Source References

Primary upstream source and protocol references for NT integration work:

- `https://github.com/wpilibsuite/allwpilib/tree/main/ntcore`
- `https://raw.githubusercontent.com/wpilibsuite/allwpilib/main/ntcore/src/main/native/include/networktables/NetworkTableInstance.h`
- `https://raw.githubusercontent.com/wpilibsuite/allwpilib/main/ntcore/src/main/native/include/ntcore_cpp.h`
- `https://github.com/wpilibsuite/allwpilib/blob/main/ntcore/doc/networktables4.adoc`
- `https://docs.wpilib.org/en/stable/docs/software/networktables/networktables-networking.html`

Historical/auxiliary references:

- `https://github.com/wpilibsuite/ntcore` (archived, merged into allwpilib)
- `https://github.com/wpilibsuite/NetworkTablesClients` (language-native client experiments, currently minimal)

## Long-Term Interoperability Direction

End goal is compatibility with modern WPILib dashboard ecosystems while preserving SmartDashboard UX flexibility.

Interoperability principles:

- Treat NetworkTables topic model and data types as the shared contract for cross-dashboard compatibility
- Keep key naming conventions stable and path-like (example: `Drive/Speed`)
- Keep transport adapters thin; avoid embedding dashboard-specific assumptions into transport
- Preserve support for multiple simultaneous clients connected to the robot NT server

Implication for future iterations:

- As NT adapter matures, this dashboard can coexist with tools like Shuffleboard by reading/writing the same robot-hosted NT topics
- Direct transport remains valuable for local teaching, isolated simulation, and controlled integration tests

## Telemetry Recording and Playback Design (Iteration 4)

This section defines the next feature slice: timeline-based recording and replay for post-run diagnostics.

### Why this matters

Live dashboards answer "what is happening now," but match debugging often needs "what happened 4 seconds before failure."

Student-relevant examples:

- Brownout investigation (voltage dip + subsystem behavior at the same timestamp)
- Command/telemetry mismatch investigation after reconnects
- Driver station incident review where root cause is only visible after zooming into a narrow time window

Core teaching goal: show how to design one UI workflow that works for both real-time streams and deterministic replay.

### Scope for this iteration

In scope:

- Record timestamped bool/double/string updates to disk during live sessions
- Replay recorded sessions through the same dashboard data path used by live transport
- Add global playback controls (`play`, `pause`, `seek`, speed)
- Add a shared timeline model (`cursor`, `duration`, `visible window`, zoom, pan)
- Keep all widgets time-synchronized to one global cursor

Out of scope for first playback slice:

- Full annotation system with rich editing
- Video sync or external media timelines
- Cross-file session merge/comparison views
- Complete replacement of current transport internals

### Workflow requirements (must-have UX)

The first usable playback workflow must include:

- One global cursor shared across all widgets
- Scrub, zoom, and pan timeline interactions (Audacity-style mental model)
- Deterministic replay (same file + same cursor -> same dashboard state)
- Step and speed controls (`0.25x`, `0.5x`, `1x`, `2x` minimum)
- System markers for notable events (connect/disconnect, stream gaps)
- Fast seek behavior suitable for jumping around an entire match log

### Architecture decision: replay as a transport adapter

Decision: implement playback as an additional `IDashboardTransport` implementation.

Rationale:

- Preserves existing model/widget flow (`MainWindow` + `VariableStore` + tiles)
- Avoids transport-specific branches in widget logic
- Keeps the teaching story clean: "live and replay are different sources behind one contract"
- Supports future parity tests across `Direct`, `NetworkTables`, and `Replay`

Alternatives considered:

1. Embed replay directly in `MainWindow`
   - Pro: quick prototype
   - Con: UI and data source logic become tightly coupled, harder to test
2. Embed replay in `VariableStore`
   - Pro: centralizes state logic
   - Con: store takes on I/O and timeline orchestration concerns

Both alternatives are rejected for long-term maintainability and student readability.

### Timeline model

Define a transport-agnostic timeline state object:

- `mode`: live or replay
- `cursorUs`: current playback position in microseconds
- `durationUs`: total log duration
- `windowStartUs` / `windowEndUs`: visible region
- `playbackRate`: speed multiplier
- `isPlaying`: play/pause state

Behavior rules:

- Cursor clamps to `[0, durationUs]`
- `seek(t)` sets cursor exactly, then publishes reconstructed latest state at `t`
- Zoom changes visible window width around cursor anchor
- Pan moves visible window without changing data values until cursor changes

### Recording format and event schema

Use an append-only event stream with explicit type/value data.

Event fields (minimum):

- `timestampUs` (monotonic session-relative time)
- `key`
- `valueType` (`bool|double|string`)
- `value`
- `seq` (if available from source transport)
- `eventKind` (`data`, `connection_state`, optional `marker`)

Design notes:

- Keep v1 file format simple and inspectable (newline-delimited JSON is acceptable for first slice)
- Add a lightweight side index for fast seek (`time bucket -> file offset`)
- Store metadata header (format version, session start wall-clock, source transport kind)

### Determinism and correctness rules

Replay correctness contract:

- Replaying a file to the same cursor produces identical `VariableStore` state
- Ties at equal timestamps are resolved by stable file order
- Values are applied in recorded order within each timestamp bucket
- Seeking backwards reconstructs state from nearest index checkpoint plus forward apply

This contract is more important than visual polish in the first playback release.

### Performance and threading model

Recorder:

- Transport callback path enqueues compact event records into a lock-protected buffer
- Background writer thread flushes batched records to disk (bounded interval/size)
- UI thread is never blocked on file I/O

Replay:

- Replay worker advances cursor by elapsed wall time scaled by `playbackRate`
- Seeks use index-assisted repositioning, then emit updates through normal transport callback flow
- UI updates remain on Qt thread via queued invocation (same rule as current transports)

### Incremental implementation slices

Slice 1 (minimum vertical path):

1. Recorder writes events to a session log file
2. Replay transport reads file and supports `play/pause/seek/speed`
3. Main window can switch source to replay mode
4. Basic timeline bar supports scrub + zoom + pan

Slice 2:

- Add index/checkpoint optimization for fast random seeks
- Add system markers and marker jump actions
- Add focused UI tests for mode transitions and seek edge cases

Slice 3:

- Add user bookmarks/annotations and anomaly marker generation
- Add derived range stats for selected timeline windows

### Validation plan

Automated tests (new):

- Replay determinism test (same cursor -> same store state)
- Seek correctness tests (forward/backward/edge seeks)
- Recorder/replay roundtrip test for mixed bool/double/string streams
- Timeline speed test (`0.5x`, `1x`, `2x`) with cursor progression checks

Manual checks (student workflow):

- Record a short live run with known value transitions
- Replay, zoom into a 5-second region, scrub and verify synchronized widgets
- Inject a synthetic brownout marker case and verify fast jump + correlation behavior

### Teaching notes (for code comments and docs)

When implementing this section, comments should explicitly name the core concepts students should learn:

- append-only log
- latest-value cache
- index-assisted seek
- deterministic replay contract
- producer/consumer buffering

Comments should explain why each pattern is used and what trade-off it makes.

## Proposed Initial Folder Layout

```text
SmartDashboard/
  design/
    SmartDashboard_Design.md
  SmartDashboard/
    src/
      app/
      model/
      widgets/
      layout/
      transport/
  SmartDashboard_Interface_direct/
    include/
    src/
  ClientInterface_direct/
    include/
    src/
    samples/
```

## Risks and Mitigations

- Transport complexity early -> keep strict interface boundaries and a simple message contract
- UI churn from widget experiments -> use widget registry + typed properties
- Layout file breakage over time -> include layout `version` and migration hooks

## Open Items (to lock before coding)

Locked for implementation kickoff:

- Build system/toolchain: `CMake + MSVC`
- Qt target: `Qt 6.x` (via `vcpkg`)
- Default layout file path: `%LOCALAPPDATA%/SmartDashboard/layout.json`
- Key naming: hierarchical slash-style keys (example: `Drive/Speed`)

Remaining optional choices (can be deferred):

- Exact vcpkg baseline/manifest pin
- Minimum supported Windows version

## Implementation Readiness Checklist

- Scope trimmed to core dashboard behavior
- Framework decision made (Qt)
- Three-project boundary defined
- Transport approach selected for v1 direct mode
- Data model + layout model defined
- Iteration plan and v1 deliverables concrete
- Toolchain, Qt version, layout path, and key style locked

This document is intended to be the implementation baseline for the next session.

## Implementation Status Snapshot (2026-03-07)

This section records what is currently implemented versus the design baseline.

### Completed in code

- CMake workspace + three-project split (`SmartDashboard`, `SmartDashboard_Interface_direct`, `ClientInterface_direct`).
- Qt desktop app shell with:
  - connection status in title/status bar
  - editable mode for widget movement
  - tile widgets for `bool`, `double`, `string`
  - context menu `Change to...` options by variable type
- Layout persistence with JSON (`version`, widget geometry, `variableKey`, `widgetType`).
- `VariableStore` model layer for latest-value tracking before UI apply.
- Direct transport core (Qt-free) implemented with:
  - Win32 file mapping + named events
  - shared ring header + framed messages
  - publish/drain loops, sequence tracking, and heartbeat-based state transitions

### In progress / not yet production-stable

- Windows runtime deployment for Qt via vcpkg is partially functional but not yet fully deterministic across environments.
- Pending finalization:
  - consistent plugin initialization from Visual Studio F5 output folders
  - clean handling of optional companion DLL availability in vcpkg installs

### Packaging/deployment direction (locked)

- For vcpkg Qt builds, prefer deterministic copy-based deployment from the active triplet output (`debug/bin`, `debug/plugins` for Debug).
- Avoid project-external ad hoc DLL sources as a baseline strategy.
- Keep `qt.conf` (`Plugins=plugins`) and place plugins under `plugins/` beside the executable.

### Qt6 + vcpkg deployment guardrail

For Qt6 applications using vcpkg, copy plugins from the Qt6-scoped plugin path, not the generic legacy path:

- Debug: `<vcpkg>/installed/<triplet>/debug/Qt6/plugins`
- Release: `<vcpkg>/installed/<triplet>/Qt6/plugins`

Reason: environments that have both Qt5 and Qt6 installed can expose Qt5 plugins at the generic path (`.../debug/plugins`). Mixing Qt6 core DLLs with Qt5 `platforms/qwindows*.dll` causes startup failure with incompatible plugin ABI errors.
