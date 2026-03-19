#pragma once

#include "native_link_ipc_protocol.h"

#include <cstdint>

namespace sd::nativelink::tcp
{
    constexpr std::uint32_t kFrameMagic = 0x4E4C5443;
    constexpr std::uint16_t kFrameVersion = 1;

    enum class FrameKind : std::uint16_t
    {
        ClientHello = 1,
        ServerHello = 2,
        ServerMessage = 3,
        ClientPublish = 4,
        Heartbeat = 5
    };

    struct FrameHeader
    {
        std::uint32_t magic = kFrameMagic;
        std::uint16_t version = kFrameVersion;
        std::uint16_t kind = 0;
        std::uint32_t payloadBytes = 0;
    };

    struct HelloPayload
    {
        char channelId[64] = {};
        char clientId[64] = {};
    };

    struct ServerHelloPayload
    {
        std::uint64_t serverSessionId = 0;
    };

    struct HeartbeatPayload
    {
        std::uint64_t serverSessionId = 0;
    };

    constexpr std::uint32_t kMaxFramePayloadBytes = sizeof(ipc::SharedMessage);
}
