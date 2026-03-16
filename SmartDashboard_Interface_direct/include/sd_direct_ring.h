#pragma once

#include "sd_direct_types.h"
#include "sd_direct_wire.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sd::direct
{
    struct RingAttachResult
    {
        wire::RingHeader* header = nullptr;
        std::uint8_t* payload = nullptr;
        std::uint32_t capacity = 0;
    };

    bool AttachRing(void* mappingBase, std::size_t mappingBytes, bool initializeIfNeeded, RingAttachResult& outRing);
    bool WriteUpsert(
        RingAttachResult& ring,
        std::string_view key,
        ValueType type,
        const VariableValue& value,
        std::uint64_t seq,
        std::uint64_t timestampUs,
        std::uint64_t& outDroppedCount
    );
    bool ReadNextUpsert(RingAttachResult& ring, VariableUpdate& outUpdate);
}
