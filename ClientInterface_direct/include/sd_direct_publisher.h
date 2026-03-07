#pragma once

#include "sd_direct_types.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
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

        virtual bool FlushNow() = 0;

        virtual std::uint64_t GetPublishedSeq() const = 0;
        virtual std::uint64_t GetDroppedCount() const = 0;
    };

    std::unique_ptr<IDirectPublisher> CreateDirectPublisher(const PublisherConfig& cfg);
}
