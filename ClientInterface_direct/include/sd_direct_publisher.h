#pragma once

#include "sd_direct_types.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sd::direct
{
    // Configuration for a process that publishes values into the shared direct ring buffer.
    struct PublisherConfig
    {
        // Shared-memory and event names. Publisher and subscriber must match these names.
        std::wstring mappingName = L"Local\\SmartDashboard.Direct.Buffer";
        std::wstring dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable";
        std::wstring heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat";

        // Buffer size in bytes for serialized update packets.
        std::uint32_t ringBufferBytes = 1U << 20;

        // Auto-flush period when autoFlushThread is enabled.
        std::chrono::milliseconds flushPeriod {16};

        // When true, a background thread periodically calls FlushNow().
        // When false, caller is responsible for calling FlushNow().
        bool autoFlushThread = true;
    };

    // Publisher API used by robot/client code to send key-value updates.
    class IDirectPublisher
    {
    public:
        virtual ~IDirectPublisher() = default;

        // Open transport resources and begin publishing.
        virtual bool Start() = 0;

        // Stop publishing and release transport resources.
        virtual void Stop() = 0;

        // Stage latest value for a key. Values are committed on FlushNow() or auto-flush.
        virtual void PublishBool(std::string_view key, bool value) = 0;
        virtual void PublishDouble(std::string_view key, double value) = 0;
        virtual void PublishString(std::string_view key, std::string_view value) = 0;
        virtual void PublishStringArray(std::string_view key, const std::vector<std::string>& value) = 0;

        // Commit staged values to the ring and signal subscribers.
        virtual bool FlushNow() = 0;

        // Monotonic sequence of published frames and total dropped updates.
        virtual std::uint64_t GetPublishedSeq() const = 0;
        virtual std::uint64_t GetDroppedCount() const = 0;
    };

    // Factory for the platform-specific direct publisher implementation.
    std::unique_ptr<IDirectPublisher> CreateDirectPublisher(const PublisherConfig& cfg);
}
