#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sd::direct
{
    // Value type discriminator carried with each update.
    enum class ValueType : std::uint8_t
    {
        Bool = 1,
        Double = 2,
        String = 3,
        StringArray = 4
    };

    // Generic value container used in callbacks.
    struct VariableValue
    {
        bool boolValue = false;
        double doubleValue = 0.0;
        std::string stringValue;
        std::vector<std::string> stringArrayValue;
    };

    // One logical update produced by a publisher and consumed by subscribers.
    struct VariableUpdate
    {
        // Topic/key name (for example "Drive/Speed").
        std::string key;
        ValueType type;
        VariableValue value;

        // Monotonic transport sequence number (resets when publisher restarts).
        std::uint64_t seq;

        // Publisher source timestamp in microseconds.
        std::uint64_t sourceTimestampUs;
    };

    // Subscriber connection state to publisher heartbeat/data flow.
    enum class ConnectionState : std::uint8_t
    {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        Stale = 3
    };
}
