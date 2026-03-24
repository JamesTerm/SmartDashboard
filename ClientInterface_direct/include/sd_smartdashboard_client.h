#pragma once

#include "sd_direct_publisher.h"
#include "sd_direct_subscriber.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sd::direct
{
    struct SmartDashboardClientConfig
    {
        PublisherConfig publisher;
        SubscriberConfig subscriber;
        bool enableSubscriber = true;

        // Shared retained key-value table (authoritative direct-store semantics).
        // When enabled, TryGet/Get can recover values across process restarts.
        bool enableRetainedStore = true;
        // Optional file path for retained-store persistence (empty => auto path).
        std::wstring retainedStorePersistencePath;

        // Optional command channel (dashboard -> app/client) for writable controls.
        SubscriberConfig commandSubscriber {
            L"Local\\SmartDashboard.Direct.Command.Buffer",
            L"Local\\SmartDashboard.Direct.Command.DataAvailable",
            L"Local\\SmartDashboard.Direct.Command.Heartbeat"
        };
        bool enableCommandSubscriber = false;
    };

    struct SubscriptionToken
    {
        std::string key;
        ValueType type = ValueType::String;
        std::uint64_t id = 0;

        explicit operator bool() const
        {
            return id != 0;
        }
    };

    using BoolChangedCallback = std::function<void(bool)>;
    using DoubleChangedCallback = std::function<void(double)>;
    using StringChangedCallback = std::function<void(const std::string&)>;
    using StringArrayChangedCallback = std::function<void(const std::vector<std::string>&)>;

    class SmartDashboardClient final
    {
    public:
        explicit SmartDashboardClient(const SmartDashboardClientConfig& config = {});
        ~SmartDashboardClient();

        bool Start();
        void Stop();

        void PutBoolean(std::string_view key, bool value);
        void PutDouble(std::string_view key, double value);
        void PutString(std::string_view key, std::string_view value);
        void PutStringArray(std::string_view key, const std::vector<std::string>& value);
        bool FlushNow();

        bool TryGetBoolean(std::string_view key, bool& outValue) const;
        bool TryGetDouble(std::string_view key, double& outValue) const;
        bool TryGetString(std::string_view key, std::string& outValue) const;
        bool TryGetStringArray(std::string_view key, std::vector<std::string>& outValue) const;

        bool GetBoolean(std::string_view key, bool defaultValue);
        double GetDouble(std::string_view key, double defaultValue);
        std::string GetString(std::string_view key, std::string_view defaultValue);
        std::vector<std::string> GetStringArray(std::string_view key, const std::vector<std::string>& defaultValue);

        SubscriptionToken SubscribeBoolean(std::string key, BoolChangedCallback callback, bool invokeImmediately = true);
        SubscriptionToken SubscribeDouble(std::string key, DoubleChangedCallback callback, bool invokeImmediately = true);
        SubscriptionToken SubscribeString(std::string key, StringChangedCallback callback, bool invokeImmediately = true);
        SubscriptionToken SubscribeStringArray(std::string key, StringArrayChangedCallback callback, bool invokeImmediately = true);
        bool Unsubscribe(const SubscriptionToken& token);

        // Callback for writable dashboard controls coming back to app/client.
        // Returns a typed subscription token that can be removed with Unsubscribe.
        SubscriptionToken SubscribeBooleanCommand(std::string key, BoolChangedCallback callback, bool invokeImmediately = false);
        SubscriptionToken SubscribeDoubleCommand(std::string key, DoubleChangedCallback callback, bool invokeImmediately = false);
        SubscriptionToken SubscribeStringCommand(std::string key, StringChangedCallback callback, bool invokeImmediately = false);
        SubscriptionToken SubscribeStringArrayCommand(std::string key, StringArrayChangedCallback callback, bool invokeImmediately = false);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
