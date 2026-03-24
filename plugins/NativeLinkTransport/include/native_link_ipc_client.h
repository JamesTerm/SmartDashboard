#pragma once

#include "native_link_carrier_client.h"

namespace sd::nativelink
{
    class NativeLinkIpcClient final : public NativeLinkCarrierClient
    {
    public:
        using Config = NativeLinkClientConfig;
        using UpdateCallback = NativeLinkUpdateCallback;
        using ConnectionStateCallback = NativeLinkConnectionStateCallback;

        NativeLinkIpcClient();
        ~NativeLinkIpcClient();

        bool Start(const Config& config, UpdateCallback onUpdate, ConnectionStateCallback onConnectionState) override;
        void Stop() override;

        bool Publish(const std::string& topicPath, const TopicValue& value) override;
        bool IsConnected() const override;

    private:
        struct Impl;
        Impl* m_impl;
    };
}
