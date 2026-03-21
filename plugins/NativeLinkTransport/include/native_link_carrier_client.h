#pragma once

#include "native_link_core.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace sd::nativelink
{
    enum class NativeLinkCarrierKind
    {
        SharedMemory,
        Tcp
    };

    const char* ToString(NativeLinkCarrierKind kind);
    bool TryParseCarrierKind(const std::string& text, NativeLinkCarrierKind& outKind);

    struct NativeLinkClientConfig
    {
        NativeLinkCarrierKind carrierKind = NativeLinkCarrierKind::SharedMemory;
        std::string channelId = "native-link-default";
        std::string clientId = "SmartDashboardApp";
        std::string host = "127.0.0.1";
        std::uint16_t port = 5810;
        std::uint32_t waitTimeoutMs = 100;
        std::uint32_t heartbeatStaleTimeoutMs = 5000;
        // Ian: When false the TCP client attempts exactly one connect on Start()
        // then parks in Disconnected on failure instead of retrying.  The user
        // must invoke Stop()/Start() (via the UI Connect button) to redial.
        // When true (default) the client pulses Connecting→Disconnected until
        // a server appears, which is the right behaviour for robot-code workflows
        // where the authority may restart at any time.
        bool autoConnect = true;
    };

    using NativeLinkUpdateCallback = std::function<void(const UpdateEnvelope&)>;
    using NativeLinkConnectionStateCallback = std::function<void(int)>;

    class NativeLinkCarrierClient
    {
    public:
        virtual ~NativeLinkCarrierClient() = default;

        virtual bool Start(
            const NativeLinkClientConfig& config,
            NativeLinkUpdateCallback onUpdate,
            NativeLinkConnectionStateCallback onConnectionState) = 0;
        virtual void Stop() = 0;
        virtual bool Publish(const std::string& topicPath, const TopicValue& value) = 0;
        virtual bool IsConnected() const = 0;
    };

    std::unique_ptr<NativeLinkCarrierClient> CreateNativeLinkCarrierClient(NativeLinkCarrierKind kind);
}
