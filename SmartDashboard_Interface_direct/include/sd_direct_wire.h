#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace sd::direct::wire
{
    constexpr std::uint32_t kMagic = 0x53444442; // 'SDDB'
    constexpr std::uint16_t kVersion = 1;
    constexpr std::uint32_t kDefaultCapacityBytes = 1U << 20;
    constexpr std::size_t kKeyMax = 128;
    constexpr std::size_t kStringMax = 256;

    enum class MsgType : std::uint8_t
    {
        Upsert = 1
    };

    enum class WireValueType : std::uint8_t
    {
        Bool = 1,
        Double = 2,
        String = 3
    };

    struct alignas(8) RingHeader
    {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t reserved0;

        std::uint32_t capacityBytes;
        std::atomic<std::uint32_t> writeIndex;
        std::atomic<std::uint32_t> readIndex;

        std::atomic<std::uint64_t> publishedSeq;
        std::atomic<std::uint64_t> droppedCount;
        std::atomic<std::uint64_t> lastProducerHeartbeatUs;
        std::atomic<std::uint64_t> lastConsumerHeartbeatUs;
    };

    struct alignas(8) MessageHeader
    {
        std::uint16_t messageBytes;
        std::uint8_t messageType;
        std::uint8_t valueType;
        std::uint64_t seq;
        std::uint64_t sourceTimestampUs;
        std::uint16_t keyLen;
        std::uint16_t valueLen;
    };
}
