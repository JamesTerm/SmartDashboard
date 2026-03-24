#include "sd_direct_ring.h"

#include "sd_direct_clock.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sd::direct
{
    namespace
    {
        constexpr std::size_t kHeaderSize = sizeof(wire::RingHeader);

		void DebugLog(const std::string& text)
		{
#ifdef _WIN32
			OutputDebugStringA(text.c_str());
#else
			(void)text;
#endif
		}

		bool IsPrintableKey(const std::string& key)
		{
			for (unsigned char ch : key)
			{
				if (!(std::isalnum(ch) || ch == '/' || ch == '_' || ch == '.' || ch == '-' || ch == ' '))
					return false;
			}
			return true;
		}

		std::string DescribeType(ValueType type)
		{
			switch (type)
			{
			case ValueType::Bool: return "bool";
			case ValueType::Double: return "double";
			case ValueType::String: return "string";
			case ValueType::StringArray: return "string_array";
			default: return "unknown";
			}
		}
    
        // Circular buffer (ring buffer) occupancy calculation.
        std::uint32_t RingUsed(const RingAttachResult& ring)
        {
            const std::uint32_t writeIndex = ring.header->writeIndex.load(std::memory_order_acquire);
            const std::uint32_t readIndex = ring.header->consumerReadIndex.load(std::memory_order_acquire);

            if (writeIndex >= readIndex)
            {
                return writeIndex - readIndex;
            }

            return ring.capacity - (readIndex - writeIndex);
        }

        // Circular buffer free-space calculation with one-byte guard slot
        // (classic ring-buffer full/empty disambiguation).
        std::uint32_t RingFree(const RingAttachResult& ring)
        {
            return ring.capacity - RingUsed(ring) - 1U;
        }

        // Two-segment wrap copy into a circular buffer.
        void CopyToRing(const RingAttachResult& ring, std::uint32_t index, const std::uint8_t* bytes, std::uint32_t count)
        {
            if (count == 0)
            {
                return;
            }

            const std::uint32_t firstPart = (std::min)(count, ring.capacity - index);
            std::memcpy(ring.payload + index, bytes, firstPart);

            if (count > firstPart)
            {
                std::memcpy(ring.payload, bytes + firstPart, count - firstPart);
            }
        }

        // Two-segment wrap copy out of a circular buffer.
        void CopyFromRing(const RingAttachResult& ring, std::uint32_t index, std::uint8_t* outBytes, std::uint32_t count)
        {
            if (count == 0)
            {
                return;
            }

            const std::uint32_t firstPart = (std::min)(count, ring.capacity - index);
            std::memcpy(outBytes, ring.payload + index, firstPart);

            if (count > firstPart)
            {
                std::memcpy(outBytes + firstPart, ring.payload, count - firstPart);
            }
        }

        // Message framing step: compute serialized value payload size by type.
        std::uint16_t ComputeValueLen(ValueType type, const VariableValue& value)
        {
            switch (type)
            {
                case ValueType::Bool:
                    return 1;
                case ValueType::Double:
                    return 8;
                case ValueType::String:
                    return static_cast<std::uint16_t>(std::min<std::size_t>(value.stringValue.size(), wire::kStringMax));
                case ValueType::StringArray:
                {
                    std::size_t total = 1;
                    const std::size_t count = std::min<std::size_t>(value.stringArrayValue.size(), wire::kStringArrayMaxCount);
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        total += 2;
                        total += std::min<std::size_t>(value.stringArrayValue[i].size(), wire::kStringMax);
                    }
                    return static_cast<std::uint16_t>(total);
                }
                default:
                    return 0;
            }
        }

        wire::WireValueType ToWireType(ValueType type)
        {
            switch (type)
            {
                case ValueType::Bool:
                    return wire::WireValueType::Bool;
                case ValueType::Double:
                    return wire::WireValueType::Double;
                case ValueType::String:
                    return wire::WireValueType::String;
                case ValueType::StringArray:
                    return wire::WireValueType::StringArray;
                default:
                    return wire::WireValueType::String;
            }
        }

        ValueType FromWireType(wire::WireValueType type)
        {
            switch (type)
            {
                case wire::WireValueType::Bool:
                    return ValueType::Bool;
                case wire::WireValueType::Double:
                    return ValueType::Double;
                case wire::WireValueType::String:
                    return ValueType::String;
                case wire::WireValueType::StringArray:
                    return ValueType::StringArray;
                default:
                    return ValueType::String;
            }
        }
    }

    bool AttachRing(void* mappingBase, std::size_t mappingBytes, bool initializeIfNeeded, RingAttachResult& outRing)
    {
        if (mappingBase == nullptr || mappingBytes <= kHeaderSize)
        {
            return false;
        }

        auto* header = static_cast<wire::RingHeader*>(mappingBase);
        const std::uint32_t payloadCapacity = static_cast<std::uint32_t>(mappingBytes - kHeaderSize);

        // Versioned shared-memory handshake: if layout does not match,
        // publisher-side attach can reinitialize the memory region.
        const bool needsInit =
            (header->magic != wire::kMagic) ||
            (header->version != wire::kVersion) ||
            (header->capacityBytes != payloadCapacity);

        if (needsInit)
        {
            if (!initializeIfNeeded)
            {
                return false;
            }

            std::memset(mappingBase, 0, mappingBytes);
            header->magic = wire::kMagic;
            header->version = wire::kVersion;
            header->reserved0 = 0;
            header->capacityBytes = payloadCapacity;
            header->writeIndex.store(0, std::memory_order_release);
            header->readIndex.store(0, std::memory_order_release);
            header->publishedSeq.store(0, std::memory_order_release);
            header->droppedCount.store(0, std::memory_order_release);
            const std::uint64_t nowUs = GetSteadyNowUs();
            header->lastProducerHeartbeatUs.store(nowUs, std::memory_order_release);
            header->lastConsumerHeartbeatUs.store(nowUs, std::memory_order_release);
            header->consumerInstanceId.store(0, std::memory_order_release);
            header->consumerReadIndex.store(0, std::memory_order_release);
        }

        outRing.header = header;
        outRing.payload = reinterpret_cast<std::uint8_t*>(header + 1);
        outRing.capacity = header->capacityBytes;
        return true;
    }

    bool WriteUpsert(
        RingAttachResult& ring,
        std::string_view key,
        ValueType type,
        const VariableValue& value,
        std::uint64_t seq,
        std::uint64_t timestampUs,
        std::uint64_t& outDroppedCount
    )
    {
        if (ring.header == nullptr || ring.payload == nullptr || ring.capacity == 0)
        {
            return false;
        }

        const std::uint16_t keyLen = static_cast<std::uint16_t>(std::min<std::size_t>(key.size(), wire::kKeyMax));
        const std::uint16_t valueLen = ComputeValueLen(type, value);
        const std::uint32_t totalBytes = static_cast<std::uint32_t>(sizeof(wire::MessageHeader) + keyLen + valueLen);

        // Drop policy: if frame cannot fit now, account drop and return.
        // (No blocking/backpressure in this v1 transport.)
        if (totalBytes >= ring.capacity)
        {
            outDroppedCount = ring.header->droppedCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            return false;
        }

        if (RingFree(ring) < totalBytes)
        {
            outDroppedCount = ring.header->droppedCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            return false;
        }

        // Serialize message as: header + key bytes + value bytes.
        wire::MessageHeader msg {};
        msg.messageBytes = static_cast<std::uint16_t>(totalBytes);
        msg.messageType = static_cast<std::uint8_t>(wire::MsgType::Upsert);
        msg.valueType = static_cast<std::uint8_t>(ToWireType(type));
        msg.seq = seq;
        msg.sourceTimestampUs = timestampUs;
        msg.keyLen = keyLen;
        msg.valueLen = valueLen;

        std::array<std::uint8_t, sizeof(wire::MessageHeader)> msgBytes {};
        std::memcpy(msgBytes.data(), &msg, sizeof(msg));

        const std::uint32_t writeIndex = ring.header->writeIndex.load(std::memory_order_acquire);
        std::uint32_t cursor = writeIndex;

        CopyToRing(ring, cursor, msgBytes.data(), static_cast<std::uint32_t>(msgBytes.size()));
        cursor = (cursor + static_cast<std::uint32_t>(msgBytes.size())) % ring.capacity;

        if (keyLen > 0)
        {
            CopyToRing(ring, cursor, reinterpret_cast<const std::uint8_t*>(key.data()), keyLen);
            cursor = (cursor + keyLen) % ring.capacity;
        }

        if (valueLen > 0)
        {
            switch (type)
            {
                case ValueType::Bool:
                {
                    const std::uint8_t boolByte = value.boolValue ? 1U : 0U;
                    CopyToRing(ring, cursor, &boolByte, 1);
                    cursor = (cursor + 1) % ring.capacity;
                    break;
                }
                case ValueType::Double:
                {
                    std::array<std::uint8_t, 8> dBytes {};
                    static_assert(sizeof(double) == 8);
                    std::memcpy(dBytes.data(), &value.doubleValue, 8);
                    CopyToRing(ring, cursor, dBytes.data(), 8);
                    cursor = (cursor + 8) % ring.capacity;
                    break;
                }
                case ValueType::String:
                {
                    CopyToRing(
                        ring,
                        cursor,
                        reinterpret_cast<const std::uint8_t*>(value.stringValue.data()),
                        valueLen
                    );
                    cursor = (cursor + valueLen) % ring.capacity;
                    break;
                }
                case ValueType::StringArray:
                {
                    const std::uint8_t count = static_cast<std::uint8_t>(std::min<std::size_t>(value.stringArrayValue.size(), wire::kStringArrayMaxCount));
                    CopyToRing(ring, cursor, &count, 1);
                    cursor = (cursor + 1) % ring.capacity;

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const std::uint16_t itemLen = static_cast<std::uint16_t>(std::min<std::size_t>(value.stringArrayValue[i].size(), wire::kStringMax));
                        std::array<std::uint8_t, 2> lenBytes {};
                        std::memcpy(lenBytes.data(), &itemLen, sizeof(itemLen));
                        CopyToRing(ring, cursor, lenBytes.data(), static_cast<std::uint32_t>(lenBytes.size()));
                        cursor = (cursor + static_cast<std::uint32_t>(lenBytes.size())) % ring.capacity;
                        if (itemLen > 0)
                        {
                            CopyToRing(
                                ring,
                                cursor,
                                reinterpret_cast<const std::uint8_t*>(value.stringArrayValue[i].data()),
                                itemLen
                            );
                            cursor = (cursor + itemLen) % ring.capacity;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }

        ring.header->writeIndex.store(cursor, std::memory_order_release);
        ring.header->publishedSeq.store(seq, std::memory_order_release);
        ring.header->lastProducerHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
        outDroppedCount = ring.header->droppedCount.load(std::memory_order_acquire);
        return true;
    }

    bool ReadNextUpsert(const RingAttachResult& ring, std::uint32_t& readCursor, VariableUpdate& outUpdate)
    {
        if (ring.header == nullptr || ring.payload == nullptr || ring.capacity == 0)
        {
            return false;
        }

        const std::uint32_t readIndex = readCursor;
        const std::uint32_t writeIndex = ring.header->writeIndex.load(std::memory_order_acquire);
        if (readIndex == writeIndex)
        {
            return false;
        }

        std::array<std::uint8_t, sizeof(wire::MessageHeader)> msgBytes {};
        CopyFromRing(ring, readIndex, msgBytes.data(), static_cast<std::uint32_t>(msgBytes.size()));

        wire::MessageHeader msg {};
        std::memcpy(&msg, msgBytes.data(), sizeof(msg));
        // Corruption guard: on malformed frame header, fast-forward to writer.
        // This is a resynchronization strategy to keep consumer alive.
        if (msg.messageBytes < sizeof(wire::MessageHeader))
        {
			std::ostringstream out;
			out << "[DirectRing] malformed header: messageBytes=" << msg.messageBytes
				<< " readIndex=" << readIndex << " writeIndex=" << writeIndex << "\n";
			DebugLog(out.str());
            readCursor = writeIndex;
            return false;
        }

        // Skip unsupported frame types while keeping read cursor consistent.
        if (msg.messageType != static_cast<std::uint8_t>(wire::MsgType::Upsert))
        {
			std::ostringstream out;
			out << "[DirectRing] skipping unsupported messageType=" << static_cast<int>(msg.messageType)
				<< " bytes=" << msg.messageBytes << "\n";
            DebugLog(out.str());
            const std::uint32_t nextRead = (readIndex + msg.messageBytes) % ring.capacity;
            readCursor = nextRead;
            return false;
        }

        // Framing invariant check: declared message size must match key/value lengths.
        const std::uint32_t payloadBytes = static_cast<std::uint32_t>(msg.keyLen + msg.valueLen);
        if (static_cast<std::uint32_t>(msg.messageBytes) != sizeof(wire::MessageHeader) + payloadBytes)
        {
			std::ostringstream out;
			out << "[DirectRing] payload mismatch: messageBytes=" << msg.messageBytes
				<< " keyLen=" << msg.keyLen << " valueLen=" << msg.valueLen
				<< " readIndex=" << readIndex << " writeIndex=" << writeIndex << "\n";
			DebugLog(out.str());
            readCursor = writeIndex;
            return false;
        }

        std::uint32_t cursor = (readIndex + static_cast<std::uint32_t>(sizeof(wire::MessageHeader))) % ring.capacity;

        std::string key;
        key.resize(msg.keyLen);
        if (msg.keyLen > 0)
        {
            CopyFromRing(ring, cursor, reinterpret_cast<std::uint8_t*>(key.data()), msg.keyLen);
            cursor = (cursor + msg.keyLen) % ring.capacity;
        }

        VariableValue value;
        const ValueType type = FromWireType(static_cast<wire::WireValueType>(msg.valueType));
        switch (type)
        {
            case ValueType::Bool:
            {
                std::uint8_t boolByte = 0;
                if (msg.valueLen >= 1)
                {
                    CopyFromRing(ring, cursor, &boolByte, 1);
                }
                value.boolValue = (boolByte != 0);
                cursor = (cursor + msg.valueLen) % ring.capacity;
                break;
            }
            case ValueType::Double:
            {
                std::array<std::uint8_t, 8> dBytes {};
                if (msg.valueLen >= 8)
                {
                    CopyFromRing(ring, cursor, dBytes.data(), 8);
                    std::memcpy(&value.doubleValue, dBytes.data(), 8);
                }
                cursor = (cursor + msg.valueLen) % ring.capacity;
                break;
            }
            case ValueType::String:
            {
                value.stringValue.resize(msg.valueLen);
                if (msg.valueLen > 0)
                {
                    CopyFromRing(ring, cursor, reinterpret_cast<std::uint8_t*>(value.stringValue.data()), msg.valueLen);
                }
                cursor = (cursor + msg.valueLen) % ring.capacity;
                break;
            }
            case ValueType::StringArray:
            {
                if (msg.valueLen > 0)
                {
                    std::vector<std::uint8_t> bytes(msg.valueLen);
                    CopyFromRing(ring, cursor, bytes.data(), msg.valueLen);

                    std::size_t offset = 0;
                    const std::uint8_t count = bytes[offset++];
                    value.stringArrayValue.clear();
                    value.stringArrayValue.reserve(count);
                    for (std::uint8_t i = 0; i < count && offset + 2 <= bytes.size(); ++i)
                    {
                        std::uint16_t itemLen = 0;
                        std::memcpy(&itemLen, bytes.data() + offset, sizeof(itemLen));
                        offset += 2;
                        const std::size_t boundedLen = std::min<std::size_t>(itemLen, bytes.size() - offset);
                        value.stringArrayValue.emplace_back(
                            reinterpret_cast<const char*>(bytes.data() + offset),
                            boundedLen
                        );
                        offset += boundedLen;
                    }
                }
                cursor = (cursor + msg.valueLen) % ring.capacity;
                break;
            }
            default:
                cursor = (cursor + msg.valueLen) % ring.capacity;
                break;
        }

        outUpdate.key = std::move(key);
        outUpdate.type = type;
        outUpdate.value = std::move(value);
        outUpdate.seq = msg.seq;
        outUpdate.sourceTimestampUs = msg.sourceTimestampUs;

		if (!IsPrintableKey(outUpdate.key))
		{
			std::ostringstream out;
			out << "[DirectRing] suspicious key decoded: size=" << outUpdate.key.size()
				<< " seq=" << outUpdate.seq
				<< " type=" << DescribeType(outUpdate.type)
				<< " readIndex=" << readIndex << " writeIndex=" << writeIndex << " keyBytes=";
			for (unsigned char ch : outUpdate.key)
				out << static_cast<int>(ch) << ' ';
			out << "\n";
			DebugLog(out.str());
		}

        readCursor = cursor;
        ring.header->lastConsumerHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
        return true;
    }

    bool ReadNextUpsert(RingAttachResult& ring, VariableUpdate& outUpdate)
    {
        std::uint32_t cursor = ring.header->consumerReadIndex.load(std::memory_order_acquire);
        const bool read = ReadNextUpsert(static_cast<const RingAttachResult&>(ring), cursor, outUpdate);
        if (read)
        {
            ring.header->consumerReadIndex.store(cursor, std::memory_order_release);
        }
        return read;
    }
}
