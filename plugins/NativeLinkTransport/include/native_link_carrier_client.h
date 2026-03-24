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
        // Ian: The autoConnect field has been removed.  Reconnect logic now
        // lives in the host (MainWindow) which drives Stop()+Start() cycles
        // via a QTimer.  The plugin makes a single connect attempt per Start()
        // call and fires Disconnected on failure.
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
