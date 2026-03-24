#include "sd_smartdashboard_client.h"

#include "sd_direct_clock.h"
#include "sd_direct_retained_store.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sd::direct
{
    namespace
    {
        RetainedStoreConfig BuildRetainedStoreConfig(const SmartDashboardClientConfig& config)
        {
            RetainedStoreConfig store;
            store.mappingName = config.publisher.mappingName + L".Retained";
            store.mutexName = config.publisher.mappingName + L".Retained.Mutex";
            store.maxEntries = 1024;

            if (!config.retainedStorePersistencePath.empty())
            {
                store.persistenceFilePath = config.retainedStorePersistencePath;
                return store;
            }

            const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
            if (localAppData != nullptr && localAppData[0] != L'\0')
            {
                store.persistenceFilePath = std::wstring(localAppData)
                    + L"\\SmartDashboard\\DirectStore\\retained_values.txt";
            }

            return store;
        }

        // Coalescing/latest-value cache entry used by TryGet/Get and subscriptions.
        struct CachedValue
        {
            ValueType type = ValueType::String;
            VariableValue value;
            std::uint64_t seq = 0;
        };

        struct BoolSubscription
        {
            std::uint64_t id = 0;
            BoolChangedCallback callback;
        };

        struct DoubleSubscription
        {
            std::uint64_t id = 0;
            DoubleChangedCallback callback;
        };

        struct StringSubscription
        {
            std::uint64_t id = 0;
            StringChangedCallback callback;
        };

        struct StringArraySubscription
        {
            std::uint64_t id = 0;
            StringArrayChangedCallback callback;
        };
    }

    struct SmartDashboardClient::Impl
    {
        explicit Impl(const SmartDashboardClientConfig& cfg)
            : config(cfg)
        {
            publisher = CreateDirectPublisher(config.publisher);
            if (config.enableSubscriber)
            {
                subscriber = CreateDirectSubscriber(config.subscriber);
            }
        }

        SmartDashboardClientConfig config;
        std::unique_ptr<IDirectPublisher> publisher;
        std::unique_ptr<IDirectSubscriber> subscriber;
        std::unique_ptr<IDirectSubscriber> commandSubscriber;
        DirectRetainedStore retainedStore;
        bool running = false;
        std::uint64_t localSeq = 1;

        mutable std::mutex mutex;
        std::unordered_map<std::string, CachedValue> cache;
        std::uint64_t nextSubscriptionId = 1;

        std::unordered_map<std::string, std::vector<BoolSubscription>> boolSubscribers;
        std::unordered_map<std::string, std::vector<DoubleSubscription>> doubleSubscribers;
        std::unordered_map<std::string, std::vector<StringSubscription>> stringSubscribers;
        std::unordered_map<std::string, std::vector<StringArraySubscription>> stringArraySubscribers;
        std::unordered_map<std::string, std::vector<BoolSubscription>> boolCommandSubscribers;
        std::unordered_map<std::string, std::vector<DoubleSubscription>> doubleCommandSubscribers;
        std::unordered_map<std::string, std::vector<StringSubscription>> stringCommandSubscribers;
        std::unordered_map<std::string, std::vector<StringArraySubscription>> stringArrayCommandSubscribers;

        void HandleUpdate(const VariableUpdate& update)
        {
            // Fan-out pattern: capture a stable callback snapshot under lock,
            // then invoke callbacks outside lock to avoid callback re-entrancy deadlocks.
            std::vector<BoolChangedCallback> boolCallbacks;
            std::vector<DoubleChangedCallback> doubleCallbacks;
            std::vector<StringChangedCallback> stringCallbacks;
            std::vector<StringArrayChangedCallback> stringArrayCallbacks;

            {
                std::lock_guard<std::mutex> lock(mutex);

                auto it = cache.find(update.key);
                if (it == cache.end())
                {
                    CachedValue cached;
                    cached.type = update.type;
                    cached.value = update.value;
                    cached.seq = update.seq;
                    cache.emplace(update.key, std::move(cached));
                }
                else if (update.seq == 0 || update.seq >= it->second.seq)
                {
                    // Last-write-wins by sequence for each key.
                    it->second.type = update.type;
                    it->second.value = update.value;
                    it->second.seq = update.seq;
                }

                if (update.type == ValueType::Bool)
                {
                    auto subIt = boolSubscribers.find(update.key);
                    if (subIt != boolSubscribers.end())
                    {
                        for (const BoolSubscription& sub : subIt->second)
                        {
                            boolCallbacks.push_back(sub.callback);
                        }
                    }
                }
                else if (update.type == ValueType::Double)
                {
                    auto subIt = doubleSubscribers.find(update.key);
                    if (subIt != doubleSubscribers.end())
                    {
                        for (const DoubleSubscription& sub : subIt->second)
                        {
                            doubleCallbacks.push_back(sub.callback);
                        }
                    }
                }
                else if (update.type == ValueType::String)
                {
                    auto subIt = stringSubscribers.find(update.key);
                    if (subIt != stringSubscribers.end())
                    {
                        for (const StringSubscription& sub : subIt->second)
                        {
                            stringCallbacks.push_back(sub.callback);
                        }
                    }
                }
                else if (update.type == ValueType::StringArray)
                {
                    auto subIt = stringArraySubscribers.find(update.key);
                    if (subIt != stringArraySubscribers.end())
                    {
                        for (const StringArraySubscription& sub : subIt->second)
                        {
                            stringArrayCallbacks.push_back(sub.callback);
                        }
                    }
                }
            }

            if (config.enableRetainedStore)
            {
                retainedStore.Put(update);
            }

            if (update.type == ValueType::Bool)
            {
                for (const auto& callback : boolCallbacks)
                {
                    callback(update.value.boolValue);
                }
            }
            else if (update.type == ValueType::Double)
            {
                for (const auto& callback : doubleCallbacks)
                {
                    callback(update.value.doubleValue);
                }
            }
            else if (update.type == ValueType::String)
            {
                for (const auto& callback : stringCallbacks)
                {
                    callback(update.value.stringValue);
                }
            }
            else if (update.type == ValueType::StringArray)
            {
                for (const auto& callback : stringArrayCallbacks)
                {
                    callback(update.value.stringArrayValue);
                }
            }
        }

        void HandleCommandUpdate(const VariableUpdate& update)
        {
            std::vector<BoolChangedCallback> boolCallbacks;
            std::vector<DoubleChangedCallback> doubleCallbacks;
            std::vector<StringChangedCallback> stringCallbacks;
            std::vector<StringArrayChangedCallback> stringArrayCallbacks;

            {
                std::lock_guard<std::mutex> lock(mutex);

                if (update.type == ValueType::Bool)
                {
                    auto subIt = boolCommandSubscribers.find(update.key);
                    if (subIt != boolCommandSubscribers.end())
                    {
                        for (const BoolSubscription& sub : subIt->second)
                        {
                            boolCallbacks.push_back(sub.callback);
                        }
                    }
                }
                else if (update.type == ValueType::Double)
                {
                    auto subIt = doubleCommandSubscribers.find(update.key);
                    if (subIt != doubleCommandSubscribers.end())
                    {
                        for (const DoubleSubscription& sub : subIt->second)
                        {
                            doubleCallbacks.push_back(sub.callback);
                        }
                    }
                }
                else if (update.type == ValueType::String)
                {
                    auto subIt = stringCommandSubscribers.find(update.key);
                    if (subIt != stringCommandSubscribers.end())
                    {
                        for (const StringSubscription& sub : subIt->second)
                        {
                            stringCallbacks.push_back(sub.callback);
                        }
                    }
                }
                else if (update.type == ValueType::StringArray)
                {
                    auto subIt = stringArrayCommandSubscribers.find(update.key);
                    if (subIt != stringArrayCommandSubscribers.end())
                    {
                        for (const StringArraySubscription& sub : subIt->second)
                        {
                            stringArrayCallbacks.push_back(sub.callback);
                        }
                    }
                }
            }

            if (update.type == ValueType::Bool)
            {
                for (const auto& callback : boolCallbacks)
                {
                    callback(update.value.boolValue);
                }
            }
            else if (update.type == ValueType::Double)
            {
                for (const auto& callback : doubleCallbacks)
                {
                    callback(update.value.doubleValue);
                }
            }
            else if (update.type == ValueType::String)
            {
                for (const auto& callback : stringCallbacks)
                {
                    callback(update.value.stringValue);
                }
            }
            else if (update.type == ValueType::StringArray)
            {
                for (const auto& callback : stringArrayCallbacks)
                {
                    callback(update.value.stringArrayValue);
                }
            }
        }

        bool TryReadCachedBoolean(std::string_view key, bool& outValue) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = cache.find(std::string(key));
            if (it == cache.end() || it->second.type != ValueType::Bool)
            {
                return false;
            }

            outValue = it->second.value.boolValue;
            return true;
        }

        bool TryReadCachedDouble(std::string_view key, double& outValue) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = cache.find(std::string(key));
            if (it == cache.end() || it->second.type != ValueType::Double)
            {
                return false;
            }

            outValue = it->second.value.doubleValue;
            return true;
        }

        bool TryReadCachedString(std::string_view key, std::string& outValue) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = cache.find(std::string(key));
            if (it == cache.end() || it->second.type != ValueType::String)
            {
                return false;
            }

            outValue = it->second.value.stringValue;
            return true;
        }

        bool TryReadCachedStringArray(std::string_view key, std::vector<std::string>& outValue) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = cache.find(std::string(key));
            if (it == cache.end() || it->second.type != ValueType::StringArray)
            {
                return false;
            }

            outValue = it->second.value.stringArrayValue;
            return true;
        }
    };

    SmartDashboardClient::SmartDashboardClient(const SmartDashboardClientConfig& config)
        : m_impl(std::make_unique<Impl>(config))
    {
        if (m_impl->config.enableCommandSubscriber)
        {
            m_impl->commandSubscriber = CreateDirectSubscriber(m_impl->config.commandSubscriber);
        }
    }

    SmartDashboardClient::~SmartDashboardClient()
    {
        Stop();
    }

    bool SmartDashboardClient::Start()
    {
        if (!m_impl || !m_impl->publisher)
        {
            return false;
        }

        if (m_impl->running)
        {
            return true;
        }

        if (!m_impl->publisher->Start())
        {
            return false;
        }

        if (m_impl->config.enableRetainedStore)
        {
            const RetainedStoreConfig retainedConfig = BuildRetainedStoreConfig(m_impl->config);
            if (!m_impl->retainedStore.OpenOrCreate(retainedConfig))
            {
                m_impl->publisher->Stop();
                return false;
            }
        }

        if (m_impl->subscriber)
        {
            const bool started = m_impl->subscriber->Start(
                [this](const VariableUpdate& update)
                {
                    m_impl->HandleUpdate(update);
                },
                [](ConnectionState)
                {
                }
            );

            if (!started)
            {
                m_impl->retainedStore.Close();
                m_impl->publisher->Stop();
                return false;
            }
        }

        if (m_impl->commandSubscriber)
        {
            const bool started = m_impl->commandSubscriber->Start(
                [this](const VariableUpdate& update)
                {
                    m_impl->HandleCommandUpdate(update);
                },
                [](ConnectionState)
                {
                }
            );

            if (!started)
            {
                if (m_impl->subscriber)
                {
                    m_impl->subscriber->Stop();
                }
                m_impl->retainedStore.Close();
                m_impl->publisher->Stop();
                return false;
            }
        }

        m_impl->running = true;
        return true;
    }

    void SmartDashboardClient::Stop()
    {
        if (!m_impl || !m_impl->running)
        {
            return;
        }

        if (m_impl->subscriber)
        {
            m_impl->subscriber->Stop();
        }
        if (m_impl->commandSubscriber)
        {
            m_impl->commandSubscriber->Stop();
        }
        if (m_impl->publisher)
        {
            m_impl->publisher->Stop();
        }
        m_impl->retainedStore.Close();

        m_impl->running = false;
    }

    void SmartDashboardClient::PutBoolean(std::string_view key, bool value)
    {
        if (m_impl && m_impl->publisher)
        {
            VariableUpdate localUpdate;
            localUpdate.key = std::string(key);
            localUpdate.type = ValueType::Bool;
            localUpdate.value.boolValue = value;
            localUpdate.seq = m_impl->localSeq++;
            localUpdate.sourceTimestampUs = GetSteadyNowUs();
            m_impl->HandleUpdate(localUpdate);

            m_impl->publisher->PublishBool(key, value);
        }
    }

    void SmartDashboardClient::PutDouble(std::string_view key, double value)
    {
        if (m_impl && m_impl->publisher)
        {
            VariableUpdate localUpdate;
            localUpdate.key = std::string(key);
            localUpdate.type = ValueType::Double;
            localUpdate.value.doubleValue = value;
            localUpdate.seq = m_impl->localSeq++;
            localUpdate.sourceTimestampUs = GetSteadyNowUs();
            m_impl->HandleUpdate(localUpdate);

            m_impl->publisher->PublishDouble(key, value);
        }
    }

    void SmartDashboardClient::PutString(std::string_view key, std::string_view value)
    {
        if (m_impl && m_impl->publisher)
        {
            VariableUpdate localUpdate;
            localUpdate.key = std::string(key);
            localUpdate.type = ValueType::String;
            localUpdate.value.stringValue = std::string(value);
            localUpdate.seq = m_impl->localSeq++;
            localUpdate.sourceTimestampUs = GetSteadyNowUs();
            m_impl->HandleUpdate(localUpdate);

            m_impl->publisher->PublishString(key, value);
        }
    }

    void SmartDashboardClient::PutStringArray(std::string_view key, const std::vector<std::string>& value)
    {
        if (m_impl && m_impl->publisher)
        {
            VariableUpdate localUpdate;
            localUpdate.key = std::string(key);
            localUpdate.type = ValueType::StringArray;
            localUpdate.value.stringArrayValue = value;
            localUpdate.seq = m_impl->localSeq++;
            localUpdate.sourceTimestampUs = GetSteadyNowUs();
            m_impl->HandleUpdate(localUpdate);

            m_impl->publisher->PublishStringArray(key, value);
        }
    }

    bool SmartDashboardClient::FlushNow()
    {
        if (!m_impl || !m_impl->publisher)
        {
            return false;
        }

        return m_impl->publisher->FlushNow();
    }

    bool SmartDashboardClient::TryGetBoolean(std::string_view key, bool& outValue) const
    {
        if (!m_impl)
        {
            return false;
        }
        if (m_impl->TryReadCachedBoolean(key, outValue))
        {
            return true;
        }

        if (!m_impl->config.enableRetainedStore)
        {
            return false;
        }

        VariableValue retained;
        if (!m_impl->retainedStore.TryGet(key, ValueType::Bool, retained))
        {
            return false;
        }

        outValue = retained.boolValue;
        return true;
    }

    bool SmartDashboardClient::TryGetDouble(std::string_view key, double& outValue) const
    {
        if (!m_impl)
        {
            return false;
        }
        if (m_impl->TryReadCachedDouble(key, outValue))
        {
            return true;
        }

        if (!m_impl->config.enableRetainedStore)
        {
            return false;
        }

        VariableValue retained;
        if (!m_impl->retainedStore.TryGet(key, ValueType::Double, retained))
        {
            return false;
        }

        outValue = retained.doubleValue;
        return true;
    }

    bool SmartDashboardClient::TryGetString(std::string_view key, std::string& outValue) const
    {
        if (!m_impl)
        {
            return false;
        }
        if (m_impl->TryReadCachedString(key, outValue))
        {
            return true;
        }

        if (!m_impl->config.enableRetainedStore)
        {
            return false;
        }

        VariableValue retained;
        if (!m_impl->retainedStore.TryGet(key, ValueType::String, retained))
        {
            return false;
        }

        outValue = retained.stringValue;
        return true;
    }

    bool SmartDashboardClient::TryGetStringArray(std::string_view key, std::vector<std::string>& outValue) const
    {
        if (!m_impl)
        {
            return false;
        }
        if (m_impl->TryReadCachedStringArray(key, outValue))
        {
            return true;
        }

        if (!m_impl->config.enableRetainedStore)
        {
            return false;
        }

        VariableValue retained;
        if (!m_impl->retainedStore.TryGet(key, ValueType::StringArray, retained))
        {
            return false;
        }

        outValue = retained.stringArrayValue;
        return true;
    }

    bool SmartDashboardClient::GetBoolean(std::string_view key, bool defaultValue)
    {
        // Assertive get: if value missing, publish default to prime downstream readers.
        bool value = false;
        if (TryGetBoolean(key, value))
        {
            return value;
        }

        PutBoolean(key, defaultValue);
        FlushNow();
        return defaultValue;
    }

    double SmartDashboardClient::GetDouble(std::string_view key, double defaultValue)
    {
        // Assertive get: if value missing, publish default to prime downstream readers.
        double value = 0.0;
        if (TryGetDouble(key, value))
        {
            return value;
        }

        PutDouble(key, defaultValue);
        FlushNow();
        return defaultValue;
    }

    std::string SmartDashboardClient::GetString(std::string_view key, std::string_view defaultValue)
    {
        // Assertive get: if value missing, publish default to prime downstream readers.
        std::string value;
        if (TryGetString(key, value))
        {
            return value;
        }

        PutString(key, defaultValue);
        FlushNow();
        return std::string(defaultValue);
    }

    std::vector<std::string> SmartDashboardClient::GetStringArray(std::string_view key, const std::vector<std::string>& defaultValue)
    {
        std::vector<std::string> value;
        if (TryGetStringArray(key, value))
        {
            return value;
        }

        PutStringArray(key, defaultValue);
        FlushNow();
        return defaultValue;
    }

    SubscriptionToken SmartDashboardClient::SubscribeBoolean(std::string key, BoolChangedCallback callback, bool invokeImmediately)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        bool immediateValue = false;
        bool hasImmediate = false;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->boolSubscribers[key].push_back(BoolSubscription {id, callback});

            auto it = m_impl->cache.find(key);
            if (invokeImmediately && it != m_impl->cache.end() && it->second.type == ValueType::Bool)
            {
                immediateValue = it->second.value.boolValue;
                hasImmediate = true;
            }
        }

        if (hasImmediate)
        {
            callback(immediateValue);
        }

        return SubscriptionToken {std::move(key), ValueType::Bool, id};
    }

    SubscriptionToken SmartDashboardClient::SubscribeDouble(std::string key, DoubleChangedCallback callback, bool invokeImmediately)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        double immediateValue = 0.0;
        bool hasImmediate = false;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->doubleSubscribers[key].push_back(DoubleSubscription {id, callback});

            auto it = m_impl->cache.find(key);
            if (invokeImmediately && it != m_impl->cache.end() && it->second.type == ValueType::Double)
            {
                immediateValue = it->second.value.doubleValue;
                hasImmediate = true;
            }
        }

        if (hasImmediate)
        {
            callback(immediateValue);
        }

        return SubscriptionToken {std::move(key), ValueType::Double, id};
    }

    SubscriptionToken SmartDashboardClient::SubscribeString(std::string key, StringChangedCallback callback, bool invokeImmediately)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        std::string immediateValue;
        bool hasImmediate = false;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->stringSubscribers[key].push_back(StringSubscription {id, callback});

            auto it = m_impl->cache.find(key);
            if (invokeImmediately && it != m_impl->cache.end() && it->second.type == ValueType::String)
            {
                immediateValue = it->second.value.stringValue;
                hasImmediate = true;
            }
        }

        if (hasImmediate)
        {
            callback(immediateValue);
        }

        return SubscriptionToken {std::move(key), ValueType::String, id};
    }

    SubscriptionToken SmartDashboardClient::SubscribeStringArray(std::string key, StringArrayChangedCallback callback, bool invokeImmediately)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        std::vector<std::string> immediateValue;
        bool hasImmediate = false;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->stringArraySubscribers[key].push_back(StringArraySubscription {id, callback});

            auto it = m_impl->cache.find(key);
            if (invokeImmediately && it != m_impl->cache.end() && it->second.type == ValueType::StringArray)
            {
                immediateValue = it->second.value.stringArrayValue;
                hasImmediate = true;
            }
        }

        if (hasImmediate)
        {
            callback(immediateValue);
        }

        return SubscriptionToken {std::move(key), ValueType::StringArray, id};
    }

    SubscriptionToken SmartDashboardClient::SubscribeBooleanCommand(std::string key, BoolChangedCallback callback, bool)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->boolCommandSubscribers[key].push_back(BoolSubscription {id, callback});
        }
        return SubscriptionToken {std::move(key), ValueType::Bool, id};
    }

    SubscriptionToken SmartDashboardClient::SubscribeDoubleCommand(std::string key, DoubleChangedCallback callback, bool)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->doubleCommandSubscribers[key].push_back(DoubleSubscription {id, callback});
        }
        return SubscriptionToken {std::move(key), ValueType::Double, id};
    }

    SubscriptionToken SmartDashboardClient::SubscribeStringCommand(std::string key, StringChangedCallback callback, bool)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->stringCommandSubscribers[key].push_back(StringSubscription {id, callback});
        }
        return SubscriptionToken {std::move(key), ValueType::String, id};
    }

    SubscriptionToken SmartDashboardClient::SubscribeStringArrayCommand(std::string key, StringArrayChangedCallback callback, bool)
    {
        if (!m_impl || !callback)
        {
            return {};
        }

        std::uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            id = m_impl->nextSubscriptionId++;
            m_impl->stringArrayCommandSubscribers[key].push_back(StringArraySubscription {id, callback});
        }
        return SubscriptionToken {std::move(key), ValueType::StringArray, id};
    }

    bool SmartDashboardClient::Unsubscribe(const SubscriptionToken& token)
    {
        if (!m_impl || !token)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_impl->mutex);

        if (token.type == ValueType::Bool)
        {
            auto it = m_impl->boolSubscribers.find(token.key);
            if (it != m_impl->boolSubscribers.end())
            {
                auto& vec = it->second;
                const auto before = vec.size();
                vec.erase(
                    std::remove_if(vec.begin(), vec.end(), [&token](const BoolSubscription& sub)
                    {
                        return sub.id == token.id;
                    }),
                    vec.end()
                );
                if (vec.size() != before)
                {
                    return true;
                }
            }

            auto cmdIt = m_impl->boolCommandSubscribers.find(token.key);
            if (cmdIt == m_impl->boolCommandSubscribers.end())
            {
                return false;
            }

            auto& cmdVec = cmdIt->second;
            const auto cmdBefore = cmdVec.size();
            cmdVec.erase(
                std::remove_if(cmdVec.begin(), cmdVec.end(), [&token](const BoolSubscription& sub)
                {
                    return sub.id == token.id;
                }),
                cmdVec.end()
            );
            return cmdVec.size() != cmdBefore;
        }

        if (token.type == ValueType::Double)
        {
            auto it = m_impl->doubleSubscribers.find(token.key);
            if (it != m_impl->doubleSubscribers.end())
            {
                auto& vec = it->second;
                const auto before = vec.size();
                vec.erase(
                    std::remove_if(vec.begin(), vec.end(), [&token](const DoubleSubscription& sub)
                    {
                        return sub.id == token.id;
                    }),
                    vec.end()
                );
                if (vec.size() != before)
                {
                    return true;
                }
            }

            auto cmdIt = m_impl->doubleCommandSubscribers.find(token.key);
            if (cmdIt == m_impl->doubleCommandSubscribers.end())
            {
                return false;
            }

            auto& cmdVec = cmdIt->second;
            const auto cmdBefore = cmdVec.size();
            cmdVec.erase(
                std::remove_if(cmdVec.begin(), cmdVec.end(), [&token](const DoubleSubscription& sub)
                {
                    return sub.id == token.id;
                }),
                cmdVec.end()
            );
            return cmdVec.size() != cmdBefore;
        }

        if (token.type == ValueType::StringArray)
        {
            auto it = m_impl->stringArraySubscribers.find(token.key);
            if (it != m_impl->stringArraySubscribers.end())
            {
                auto& vec = it->second;
                const auto before = vec.size();
                vec.erase(
                    std::remove_if(vec.begin(), vec.end(), [&token](const StringArraySubscription& sub)
                    {
                        return sub.id == token.id;
                    }),
                    vec.end()
                );
                if (vec.size() != before)
                {
                    return true;
                }
            }

            auto cmdIt = m_impl->stringArrayCommandSubscribers.find(token.key);
            if (cmdIt == m_impl->stringArrayCommandSubscribers.end())
            {
                return false;
            }

            auto& cmdVec = cmdIt->second;
            const auto cmdBefore = cmdVec.size();
            cmdVec.erase(
                std::remove_if(cmdVec.begin(), cmdVec.end(), [&token](const StringArraySubscription& sub)
                {
                    return sub.id == token.id;
                }),
                cmdVec.end()
            );
            return cmdVec.size() != cmdBefore;
        }

        auto it = m_impl->stringSubscribers.find(token.key);
        if (it != m_impl->stringSubscribers.end())
        {
            auto& vec = it->second;
            const auto before = vec.size();
            vec.erase(
                std::remove_if(vec.begin(), vec.end(), [&token](const StringSubscription& sub)
                {
                    return sub.id == token.id;
                }),
                vec.end()
            );
            if (vec.size() != before)
            {
                return true;
            }
        }

        auto cmdIt = m_impl->stringCommandSubscribers.find(token.key);
        if (cmdIt == m_impl->stringCommandSubscribers.end())
        {
            return false;
        }

        auto& cmdVec = cmdIt->second;
        const auto cmdBefore = cmdVec.size();
        cmdVec.erase(
            std::remove_if(cmdVec.begin(), cmdVec.end(), [&token](const StringSubscription& sub)
            {
                return sub.id == token.id;
            }),
            cmdVec.end()
        );
        return cmdVec.size() != cmdBefore;
    }
}
