#include "sd_direct_publisher.h"

#include "sd_direct_clock.h"
#include "sd_direct_ring.h"
#include "sd_direct_shared_memory.h"
#include "sd_direct_wire.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace sd::direct
{
    class DirectPublisherStub final : public IDirectPublisher
    {
    public:
        explicit DirectPublisherStub(const PublisherConfig& cfg)
            : m_config(cfg)
        {
        }

        bool Start() override
        {
            if (m_running.load())
            {
                return true;
            }

            // Shared-memory producer endpoint setup.
            bool created = false;
            const std::size_t mappingBytes = sizeof(wire::RingHeader) + m_config.ringBufferBytes;
            if (!m_region.OpenOrCreate(m_config.mappingName, mappingBytes, created))
            {
                return false;
            }

            if (!AttachRing(m_region.Data(), m_region.Size(), true, m_ring))
            {
                m_region.Close();
                return false;
            }

            m_lastObservedConsumerInstanceId = (m_ring.header != nullptr)
                ? m_ring.header->consumerInstanceId.load(std::memory_order_acquire)
                : 0;

            bool eventCreated = false;
            if (!m_dataEvent.OpenOrCreateAutoReset(m_config.dataEventName, eventCreated))
            {
                m_region.Close();
                return false;
            }

            if (!m_heartbeatEvent.OpenOrCreateAutoReset(m_config.heartbeatEventName, eventCreated))
            {
                m_dataEvent.Close();
                m_region.Close();
                return false;
            }

            m_running.store(true);

            if (m_config.autoFlushThread)
            {
                // Periodic publisher loop (fixed-rate flush pattern).
                m_worker = std::thread(&DirectPublisherStub::RunLoop, this);
            }

            return true;
        }

        void Stop() override
        {
            if (!m_running.load())
            {
                return;
            }

            FlushNow();
            m_running.store(false);

            if (m_worker.joinable())
            {
                m_worker.join();
            }

            m_consumerWasActive = false;

            m_heartbeatEvent.Close();
            m_dataEvent.Close();
            m_region.Close();
        }

        void PublishBool(std::string_view key, bool value) override
        {
            VariableValue pendingValue;
            pendingValue.boolValue = value;
            StorePending(key, ValueType::Bool, pendingValue);
        }

        void PublishDouble(std::string_view key, double value) override
        {
            VariableValue pendingValue;
            pendingValue.doubleValue = value;
            StorePending(key, ValueType::Double, pendingValue);
        }

        void PublishString(std::string_view key, std::string_view value) override
        {
            VariableValue pendingValue;
            pendingValue.stringValue = std::string(value);
            StorePending(key, ValueType::String, pendingValue);
        }

        void PublishStringArray(std::string_view key, const std::vector<std::string>& value) override
        {
            VariableValue pendingValue;
            pendingValue.stringArrayValue = value;
            StorePending(key, ValueType::StringArray, pendingValue);
        }

        bool FlushNow() override
        {
            if (!m_running.load())
            {
                return false;
            }

            std::unordered_map<std::string, PendingValue> pending;
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);

                // Coalescing/latest-value cache pattern:
                // keep only the most recent value per key for this flush window.
                pending.swap(m_pending);
            }

            std::uint64_t dropped = m_droppedCount.load();
            bool publishedAnything = false;
            for (const auto& entry : pending)
            {
                const std::uint64_t seq = m_publishedSeq.fetch_add(1) + 1;
                const bool wrote = WriteUpsert(
                    m_ring,
                    entry.first,
                    entry.second.type,
                    entry.second.value,
                    seq,
                    GetSteadyNowUs(),
                    dropped
                );

                if (!wrote)
                {
                    // Non-blocking drop strategy if ring has no room.
                    m_droppedCount.store(dropped);
                    continue;
                }

                publishedAnything = true;
            }

            if (m_ring.header != nullptr)
            {
                m_ring.header->lastProducerHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
            }

            if (publishedAnything)
            {
                m_dataEvent.Signal();
            }

            m_heartbeatEvent.Signal();
            return true;
        }

        std::uint64_t GetPublishedSeq() const override
        {
            return m_publishedSeq.load();
        }

        std::uint64_t GetDroppedCount() const override
        {
            return m_droppedCount.load();
        }

    private:
        struct PendingValue
        {
            ValueType type = ValueType::String;
            VariableValue value;
        };

        void RunLoop()
        {
            while (m_running.load())
            {
                MaybeReplayRetainedSnapshot();
                FlushNow();

                // Fixed-interval scheduling (periodic timer loop).
                std::this_thread::sleep_for(m_config.flushPeriod);
            }
        }

        void StorePending(std::string_view key, ValueType type, const VariableValue& value)
        {
            PendingValue pending;
            pending.type = type;
            pending.value = value;

            {
                std::lock_guard<std::mutex> retainedLock(m_retainedMutex);
                m_retained[std::string(key)] = pending;
            }

            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pending[std::string(key)] = std::move(pending);
        }

        void MaybeReplayRetainedSnapshot()
        {
            if (m_ring.header == nullptr)
            {
                return;
            }

            const std::uint64_t lastConsumerHeartbeatUs = m_ring.header->lastConsumerHeartbeatUs.load(std::memory_order_acquire);
            const std::uint64_t consumerInstanceId = m_ring.header->consumerInstanceId.load(std::memory_order_acquire);
            const std::uint64_t nowUs = GetSteadyNowUs();
            const bool consumerActive =
                (lastConsumerHeartbeatUs != 0) &&
                (nowUs >= lastConsumerHeartbeatUs) &&
                ((nowUs - lastConsumerHeartbeatUs) <= 500000ULL);

            if (!consumerActive)
            {
                m_consumerWasActive = false;
                if (consumerInstanceId != 0)
                {
                    m_lastObservedConsumerInstanceId = consumerInstanceId;
                }
                return;
            }

            const bool consumerChanged =
                (consumerInstanceId != 0) &&
                (consumerInstanceId != m_lastObservedConsumerInstanceId);

            if (m_consumerWasActive && !consumerChanged)
            {
                return;
            }

            m_consumerWasActive = true;
            if (consumerInstanceId != 0)
            {
                m_lastObservedConsumerInstanceId = consumerInstanceId;
            }

            std::unordered_map<std::string, PendingValue> retainedCopy;
            {
                std::lock_guard<std::mutex> retainedLock(m_retainedMutex);
                retainedCopy = m_retained;
            }

            if (retainedCopy.empty())
            {
                return;
            }

            {
                std::lock_guard<std::mutex> pendingLock(m_pendingMutex);
                for (const auto& entry : retainedCopy)
                {
                    m_pending[entry.first] = entry.second;
                }
            }
        }

        PublisherConfig m_config;
        std::atomic<bool> m_running {false};
        std::atomic<std::uint64_t> m_publishedSeq {0};
        std::atomic<std::uint64_t> m_droppedCount {0};
        SharedMemoryRegion m_region;
        NamedEvent m_dataEvent;
        NamedEvent m_heartbeatEvent;
        RingAttachResult m_ring;
        std::thread m_worker;
        std::mutex m_pendingMutex;
        std::unordered_map<std::string, PendingValue> m_pending;
        std::mutex m_retainedMutex;
        std::unordered_map<std::string, PendingValue> m_retained;
        bool m_consumerWasActive = false;
        std::uint64_t m_lastObservedConsumerInstanceId = 0;
    };

    std::unique_ptr<IDirectPublisher> CreateDirectPublisher(const PublisherConfig& cfg)
    {
        return std::make_unique<DirectPublisherStub>(cfg);
    }
}
