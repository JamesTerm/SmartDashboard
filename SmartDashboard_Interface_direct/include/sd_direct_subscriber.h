#pragma once

#include "sd_direct_types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace sd::direct
{
    // Called for each decoded key-value update received from the transport.
    using UpdateCallback = std::function<void(const VariableUpdate&)>;

    // Called when transport connection state changes (connecting/connected/stale/etc).
    using StateCallback = std::function<void(ConnectionState)>;

    // Configuration for a process that reads updates from the shared direct ring buffer.
    struct SubscriberConfig
    {
        // Shared-memory and event names. Must match publisher names.
        std::wstring mappingName = L"Local\\SmartDashboard.Direct.Buffer";
        std::wstring dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable";
        std::wstring heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat";

        // Poll/wait timings used by subscriber worker loop.
        // This remains event-driven; the timeout is mainly a backstop for late
        // joins and stale-state maintenance, so keep it short to avoid visible
        // latency when signals are missed or coalesced.
        std::chrono::milliseconds waitTimeout {5};
        std::chrono::milliseconds staleTimeout {1000};
        std::chrono::milliseconds heartbeatPeriod {100};
    };

    // Subscriber API used by dashboard/UI code to receive updates.
    class IDirectSubscriber
    {
    public:
        virtual ~IDirectSubscriber() = default;

        // Start background receive processing and register callbacks.
        virtual bool Start(UpdateCallback onUpdate, StateCallback onState) = 0;

        // Stop background receive processing.
        virtual void Stop() = 0;

        // Current state plus counters for debugging/telemetry.
        virtual ConnectionState GetState() const = 0;
        virtual std::uint64_t GetLastSeq() const = 0;
        virtual std::uint64_t GetDroppedCount() const = 0;
    };

    // Factory for the platform-specific direct subscriber implementation.
    std::unique_ptr<IDirectSubscriber> CreateDirectSubscriber(const SubscriberConfig& cfg);
}
