# SmartDashboard (C++) Design

## Purpose

Build a focused C++ dashboard inspired by WPILib SmartDashboard, but intentionally trimmed to the core use case:

- Render live variables of `bool`, `double`, and `string`
- Let users choose/change widget type per variable
- Let users move/arrange widgets and save/load layout
- Use a direct transport layer (not NetworkTables)

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
- NetworkTables protocol support

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

### SmartDashboard Adapter Sketch (`SmartDashboard/src/transport/direct_subscriber_adapter.h`)

```cpp
#pragma once

#include "sd_direct_subscriber.h"

#include <QObject>

class DirectSubscriberAdapter final : public QObject
{
    Q_OBJECT

public:
    explicit DirectSubscriberAdapter(QObject* parent = nullptr);
    ~DirectSubscriberAdapter() override;

    bool Start();
    void Stop();

signals:
    void VariableUpdateReceived(const QString& key, int valueType, const QVariant& value, quint64 seq);
    void ConnectionStateChanged(int state);

private:
    std::unique_ptr<sd::direct::IDirectSubscriber> m_subscriber;
};
```

Implementation note:

- `DirectSubscriberAdapter` is the only Qt-aware transport layer class.
- All `*_direct` core headers and source remain Qt-free.

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

## Iteration 1 (this upcoming implementation session)

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

## Iteration 2

- Robust property editing dialog + per-widget schemas
- Better layout ergonomics (snap/grid, align)
- History buffer tuning for plots
- Improved transport diagnostics and error handling
- Begin bidirectional command channel (dashboard -> app) for editable widgets

## Iteration 3

- Optional alternate transport(s)
- Plugin-like widget extension points
- Import/export profiles and richer preferences
- Full bidirectional parity for user-editable controls

## Future Bidirectional Support (Dashboard -> Application)

Adding bidirectional support later is moderate effort, not a rewrite, because v1 already separates transport from UI and uses framed messages.

Expected effort after v1 is stable:

- Basic writable controls (checkbox, numeric text submit): about 2-4 focused days
- Polished UX (validation/errors/ack indicators/retry): about 1-2 additional weeks

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

Because v1 already has transport abstraction and message framing, this extension is additive and low-risk to existing receive path.

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
