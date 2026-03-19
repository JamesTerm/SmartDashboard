#pragma once

#include "native_link_core.h"

#include <functional>
#include <string>

namespace sd::nativelink
{
    class NativeLinkIpcClient
    {
    public:
        struct Config
        {
            std::string channelId = "native-link-default";
            std::string clientId = "SmartDashboardApp";
            std::uint32_t waitTimeoutMs = 100;
            std::uint32_t heartbeatStaleTimeoutMs = 5000;
        };

        using UpdateCallback = std::function<void(const UpdateEnvelope&)>;
        using ConnectionStateCallback = std::function<void(int)>;

        NativeLinkIpcClient();
        ~NativeLinkIpcClient();

        bool Start(const Config& config, UpdateCallback onUpdate, ConnectionStateCallback onConnectionState);
        void Stop();

        bool Publish(const std::string& topicPath, const TopicValue& value);

    private:
        struct Impl;
        Impl* m_impl;
    };
}
