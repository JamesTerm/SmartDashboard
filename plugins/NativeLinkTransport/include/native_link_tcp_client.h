#pragma once

#include "native_link_carrier_client.h"

namespace sd::nativelink
{
    class NativeLinkTcpClient final : public NativeLinkCarrierClient
    {
    public:
        NativeLinkTcpClient();
        ~NativeLinkTcpClient();

        bool Start(const NativeLinkClientConfig& config, NativeLinkUpdateCallback onUpdate, NativeLinkConnectionStateCallback onConnectionState) override;
        void Stop() override;
        bool Publish(const std::string& topicPath, const TopicValue& value) override;
        bool IsConnected() const override;

    private:
        struct Impl;
        Impl* m_impl;
    };
}
