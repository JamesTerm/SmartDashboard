#pragma once

#include <cstdint>
#include <string>

namespace sd::direct
{
    enum class ValueType : std::uint8_t
    {
        Bool = 1,
        Double = 2,
        String = 3
    };

    struct VariableValue
    {
        bool boolValue = false;
        double doubleValue = 0.0;
        std::string stringValue;
    };

    struct VariableUpdate
    {
        std::string key;
        ValueType type;
        VariableValue value;
        std::uint64_t seq;
        std::uint64_t sourceTimestampUs;
    };

    enum class ConnectionState : std::uint8_t
    {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        Stale = 3
    };
}
