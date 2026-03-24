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
        // Last-write-wins by sequence number per key.
        // This rejects stale/out-of-order updates for an existing key.
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

    void VariableStore::ResetSequenceTracking()
    {
        // Preserve last values but clear sequence gate so a new publisher session
        // (with seq restarted near 1) can update existing keys.
        for (auto& [_, record] : m_records)
        {
            record.seq = 0;
        }
    }

    void VariableStore::Clear()
    {
        m_records.clear();
    }
}
