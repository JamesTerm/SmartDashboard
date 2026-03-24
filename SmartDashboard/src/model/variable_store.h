#pragma once

#include "widgets/variable_tile.h"

#include <QVariant>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace sd::model
{
    struct VariableRecord
    {
        std::string key;
        sd::widgets::VariableType type = sd::widgets::VariableType::String;
        QVariant value;
        std::uint64_t seq = 0;
    };

    class VariableStore final
    {
    public:
        const VariableRecord& Upsert(
            const std::string& key,
            sd::widgets::VariableType type,
            const QVariant& value,
            std::uint64_t seq
        );

        void ResetSequenceTracking();
        void Clear();

    private:
        std::unordered_map<std::string, VariableRecord> m_records;
    };
}
