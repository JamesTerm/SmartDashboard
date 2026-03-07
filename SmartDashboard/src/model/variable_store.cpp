#include "model/variable_store.h"

#include <utility>

namespace sd::model
{
    const VariableRecord& VariableStore::Upsert(
        const std::string& key,
        sd::widgets::VariableType type,
        const QVariant& value,
        std::uint64_t seq
    )
    {
        auto it = m_records.find(key);
        if (it == m_records.end())
        {
            VariableRecord record;
            record.key = key;
            record.type = type;
            record.value = value;
            record.seq = seq;
            it = m_records.emplace(key, std::move(record)).first;
        }
        else
        {
            if (seq >= it->second.seq)
            {
                it->second.type = type;
                it->second.value = value;
                it->second.seq = seq;
            }
        }

        return it->second;
    }
}
