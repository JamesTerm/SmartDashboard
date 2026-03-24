#include "sd_direct_retained_store.h"

#include "sd_direct_shared_memory.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace sd::direct
{
    namespace
    {
        constexpr std::uint32_t kMagic = 0x53445254U; // SDRT
        constexpr std::uint32_t kVersion = 1;
        constexpr std::size_t kMaxKeyBytes = 191;
        constexpr std::size_t kMaxStringBytes = 511;

        struct RetainedHeader
        {
            std::uint32_t magic = kMagic;
            std::uint32_t version = kVersion;
            std::uint32_t maxEntries = 0;
            std::uint32_t reserved = 0;
        };

        struct RetainedEntry
        {
            std::uint8_t occupied = 0;
            std::uint8_t type = 0;
            std::uint16_t reserved = 0;
            std::uint64_t seq = 0;
            std::uint64_t sourceTimestampUs = 0;
            char key[kMaxKeyBytes + 1] {};
            union
            {
                std::uint8_t boolValue;
                double doubleValue;
                char stringValue[kMaxStringBytes + 1];
            } value {};
            char stringArrayValue[kMaxStringBytes + 1] {};
        };

        bool WaitForMutex(HANDLE mutexHandle)
        {
            if (mutexHandle == nullptr)
            {
                return false;
            }

            const DWORD waitResult = WaitForSingleObject(mutexHandle, INFINITE);
            return (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED);
        }

        std::string Escape(const std::string& input)
        {
            std::string output;
            for (char c : input)
            {
                if (c == '\\' || c == '|' || c == '\n' || c == '\r')
                {
                    output.push_back('\\');
                }
                output.push_back(c);
            }
            return output;
        }

        std::string Unescape(const std::string& input)
        {
            std::string output;
            bool escaped = false;
            for (char c : input)
            {
                if (escaped)
                {
                    output.push_back(c);
                    escaped = false;
                    continue;
                }
                if (c == '\\')
                {
                    escaped = true;
                    continue;
                }
                output.push_back(c);
            }
            return output;
        }

        std::vector<std::string> SplitEscaped(const std::string& input)
        {
            std::vector<std::string> parts;
            std::string current;
            bool escaped = false;
            for (char c : input)
            {
                if (escaped)
                {
                    current.push_back('\\');
                    current.push_back(c);
                    escaped = false;
                    continue;
                }

                if (c == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (c == '|')
                {
                    parts.push_back(Unescape(current));
                    current.clear();
                    continue;
                }

                current.push_back(c);
            }

            if (escaped)
            {
                current.push_back('\\');
            }
            parts.push_back(Unescape(current));
            return parts;
        }
    }

    struct DirectRetainedStore::Impl
    {
        RetainedStoreConfig config;
        SharedMemoryRegion region;
        HANDLE mutexHandle = nullptr;
        RetainedHeader* header = nullptr;
        RetainedEntry* entries = nullptr;

        ~Impl()
        {
            if (mutexHandle != nullptr)
            {
                CloseHandle(mutexHandle);
                mutexHandle = nullptr;
            }
        }

        bool PersistLocked() const
        {
            if (config.persistenceFilePath.empty() || header == nullptr || entries == nullptr)
            {
                return true;
            }

            std::error_code ec;
            const std::filesystem::path p(config.persistenceFilePath);
            std::filesystem::create_directories(p.parent_path(), ec);

            std::ofstream out(p, std::ios::trunc);
            if (!out.is_open())
            {
                return false;
            }

            for (std::uint32_t i = 0; i < header->maxEntries; ++i)
            {
                const RetainedEntry& e = entries[i];
                if (e.occupied == 0)
                {
                    continue;
                }

                out << static_cast<int>(e.type) << '|'
                    << e.seq << '|'
                    << e.sourceTimestampUs << '|'
                    << Escape(std::string(e.key)) << '|';

                if (e.type == static_cast<std::uint8_t>(ValueType::Bool))
                {
                    out << (e.value.boolValue != 0 ? "1" : "0");
                }
                else if (e.type == static_cast<std::uint8_t>(ValueType::Double))
                {
                    out.precision(std::numeric_limits<double>::max_digits10);
                    out << e.value.doubleValue;
                }
                else if (e.type == static_cast<std::uint8_t>(ValueType::StringArray))
                {
                    out << Escape(std::string(e.stringArrayValue));
                }
                else
                {
                    out << Escape(std::string(e.value.stringValue));
                }

                out << '\n';
            }

            return true;
        }

        bool LoadLocked()
        {
            if (config.persistenceFilePath.empty() || header == nullptr || entries == nullptr)
            {
                return true;
            }

            std::ifstream in(std::filesystem::path(config.persistenceFilePath));
            if (!in.is_open())
            {
                return true;
            }

            for (std::uint32_t i = 0; i < header->maxEntries; ++i)
            {
                entries[i] = RetainedEntry {};
            }

            std::string line;
            std::uint32_t idx = 0;
            while (std::getline(in, line) && idx < header->maxEntries)
            {
                const std::vector<std::string> parts = SplitEscaped(line);
                if (parts.size() != 5)
                {
                    continue;
                }

                RetainedEntry& e = entries[idx];
                e = RetainedEntry {};
                e.occupied = 1;
                e.type = static_cast<std::uint8_t>(std::stoi(parts[0]));
                e.seq = static_cast<std::uint64_t>(std::stoull(parts[1]));
                e.sourceTimestampUs = static_cast<std::uint64_t>(std::stoull(parts[2]));

                const std::string key = parts[3].substr(0, kMaxKeyBytes);
                std::memcpy(e.key, key.data(), key.size());
                e.key[key.size()] = '\0';

                if (e.type == static_cast<std::uint8_t>(ValueType::Bool))
                {
                    e.value.boolValue = (parts[4] == "1") ? 1U : 0U;
                }
                else if (e.type == static_cast<std::uint8_t>(ValueType::Double))
                {
                    e.value.doubleValue = std::stod(parts[4]);
                }
                else if (e.type == static_cast<std::uint8_t>(ValueType::StringArray))
                {
                    const std::string val = parts[4].substr(0, kMaxStringBytes);
                    std::memcpy(e.stringArrayValue, val.data(), val.size());
                    e.stringArrayValue[val.size()] = '\0';
                }
                else
                {
                    const std::string val = parts[4].substr(0, kMaxStringBytes);
                    std::memcpy(e.value.stringValue, val.data(), val.size());
                    e.value.stringValue[val.size()] = '\0';
                }

                ++idx;
            }

            return true;
        }
    };

    DirectRetainedStore::DirectRetainedStore()
        : m_impl(std::make_unique<Impl>())
    {
    }

    DirectRetainedStore::~DirectRetainedStore() = default;

    bool DirectRetainedStore::OpenOrCreate(const RetainedStoreConfig& config)
    {
        if (!m_impl)
        {
            return false;
        }

        Close();
        m_impl->config = config;
        if (m_impl->config.maxEntries < 16)
        {
            m_impl->config.maxEntries = 16;
        }

        bool created = false;
        const std::size_t bytes = sizeof(RetainedHeader) + (m_impl->config.maxEntries * sizeof(RetainedEntry));
        if (!m_impl->region.OpenOrCreate(m_impl->config.mappingName, bytes, created))
        {
            return false;
        }

        m_impl->mutexHandle = CreateMutexW(nullptr, FALSE, m_impl->config.mutexName.c_str());
        if (m_impl->mutexHandle == nullptr)
        {
            m_impl->region.Close();
            return false;
        }

        m_impl->header = static_cast<RetainedHeader*>(m_impl->region.Data());
        m_impl->entries = reinterpret_cast<RetainedEntry*>(m_impl->header + 1);

        if (!WaitForMutex(m_impl->mutexHandle))
        {
            Close();
            return false;
        }

        const bool init =
            (m_impl->header->magic != kMagic) ||
            (m_impl->header->version != kVersion) ||
            (m_impl->header->maxEntries != static_cast<std::uint32_t>(m_impl->config.maxEntries));
        if (init)
        {
            std::memset(m_impl->region.Data(), 0, bytes);
            m_impl->header->magic = kMagic;
            m_impl->header->version = kVersion;
            m_impl->header->maxEntries = static_cast<std::uint32_t>(m_impl->config.maxEntries);
        }

        bool allEmpty = true;
        for (std::uint32_t i = 0; i < m_impl->header->maxEntries; ++i)
        {
            if (m_impl->entries[i].occupied != 0)
            {
                allEmpty = false;
                break;
            }
        }
        if (allEmpty)
        {
            m_impl->LoadLocked();
        }

        ReleaseMutex(m_impl->mutexHandle);
        return true;
    }

    void DirectRetainedStore::Close()
    {
        if (!m_impl)
        {
            return;
        }

        if (m_impl->mutexHandle != nullptr)
        {
            CloseHandle(m_impl->mutexHandle);
            m_impl->mutexHandle = nullptr;
        }

        m_impl->header = nullptr;
        m_impl->entries = nullptr;
        m_impl->region.Close();
    }

    bool DirectRetainedStore::Put(const VariableUpdate& update)
    {
        if (!m_impl || m_impl->header == nullptr || m_impl->entries == nullptr)
        {
            return false;
        }

        if (!WaitForMutex(m_impl->mutexHandle))
        {
            return false;
        }

        std::int64_t found = -1;
        std::int64_t freeIdx = -1;
        std::uint64_t oldestSeq = (std::numeric_limits<std::uint64_t>::max)();
        std::int64_t oldestIdx = 0;

        for (std::uint32_t i = 0; i < m_impl->header->maxEntries; ++i)
        {
            const RetainedEntry& e = m_impl->entries[i];
            if (e.occupied == 0)
            {
                if (freeIdx < 0)
                {
                    freeIdx = static_cast<std::int64_t>(i);
                }
                continue;
            }

            if (e.seq < oldestSeq)
            {
                oldestSeq = e.seq;
                oldestIdx = static_cast<std::int64_t>(i);
            }

            if (std::string_view(e.key) == update.key)
            {
                found = static_cast<std::int64_t>(i);
                break;
            }
        }

        const std::int64_t idx = (found >= 0) ? found : ((freeIdx >= 0) ? freeIdx : oldestIdx);
        RetainedEntry& dst = m_impl->entries[static_cast<std::size_t>(idx)];
        dst = RetainedEntry {};
        dst.occupied = 1;
        dst.type = static_cast<std::uint8_t>(update.type);
        dst.seq = update.seq;
        dst.sourceTimestampUs = update.sourceTimestampUs;

        const std::string key = update.key.substr(0, kMaxKeyBytes);
        std::memcpy(dst.key, key.data(), key.size());
        dst.key[key.size()] = '\0';

        if (update.type == ValueType::Bool)
        {
            dst.value.boolValue = update.value.boolValue ? 1U : 0U;
        }
        else if (update.type == ValueType::Double)
        {
            dst.value.doubleValue = update.value.doubleValue;
        }
        else if (update.type == ValueType::StringArray)
        {
            std::string joined;
            for (std::size_t i = 0; i < update.value.stringArrayValue.size(); ++i)
            {
                if (i > 0)
                {
                    joined.push_back('\x1f');
                }
                joined += update.value.stringArrayValue[i];
            }
            const std::string val = joined.substr(0, kMaxStringBytes);
            std::memcpy(dst.stringArrayValue, val.data(), val.size());
            dst.stringArrayValue[val.size()] = '\0';
        }
        else
        {
            const std::string val = update.value.stringValue.substr(0, kMaxStringBytes);
            std::memcpy(dst.value.stringValue, val.data(), val.size());
            dst.value.stringValue[val.size()] = '\0';
        }

        const bool persisted = m_impl->PersistLocked();
        ReleaseMutex(m_impl->mutexHandle);
        return persisted;
    }

    bool DirectRetainedStore::TryGet(std::string_view key, ValueType type, VariableValue& outValue) const
    {
        if (!m_impl || m_impl->header == nullptr || m_impl->entries == nullptr)
        {
            return false;
        }

        if (!WaitForMutex(m_impl->mutexHandle))
        {
            return false;
        }

        bool found = false;
        for (std::uint32_t i = 0; i < m_impl->header->maxEntries; ++i)
        {
            const RetainedEntry& e = m_impl->entries[i];
            if (e.occupied == 0 || e.type != static_cast<std::uint8_t>(type) || std::string_view(e.key) != key)
            {
                continue;
            }

            if (type == ValueType::Bool)
            {
                outValue.boolValue = (e.value.boolValue != 0);
            }
            else if (type == ValueType::Double)
            {
                outValue.doubleValue = e.value.doubleValue;
            }
            else if (type == ValueType::StringArray)
            {
                outValue.stringArrayValue.clear();
                std::string current;
                const std::string packed = e.stringArrayValue;
                for (char ch : packed)
                {
                    if (ch == '\x1f')
                    {
                        outValue.stringArrayValue.push_back(current);
                        current.clear();
                    }
                    else
                    {
                        current.push_back(ch);
                    }
                }
                outValue.stringArrayValue.push_back(current);
            }
            else
            {
                outValue.stringValue = e.value.stringValue;
            }

            found = true;
            break;
        }

        ReleaseMutex(m_impl->mutexHandle);
        return found;
    }
}
