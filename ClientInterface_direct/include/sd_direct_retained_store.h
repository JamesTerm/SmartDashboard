#pragma once

#include "sd_direct_types.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace sd::direct
{
    struct RetainedStoreConfig
    {
        std::wstring mappingName;
        std::wstring mutexName;
        std::wstring persistenceFilePath;
        std::size_t maxEntries = 512;
    };

    class DirectRetainedStore
    {
    public:
        DirectRetainedStore();
        ~DirectRetainedStore();

        DirectRetainedStore(const DirectRetainedStore&) = delete;
        DirectRetainedStore& operator=(const DirectRetainedStore&) = delete;

        bool OpenOrCreate(const RetainedStoreConfig& config);
        void Close();

        bool Put(const VariableUpdate& update);
        bool TryGet(std::string_view key, ValueType type, VariableValue& outValue) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
