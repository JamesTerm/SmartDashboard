#pragma once

#include "sd_direct_types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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

    std::unique_ptr<IDirectSubscriber> CreateDirectSubscriber(const SubscriberConfig& cfg);
}
