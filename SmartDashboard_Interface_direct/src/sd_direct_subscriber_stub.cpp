#include "sd_direct_subscriber.h"

#include "sd_direct_clock.h"
#include "sd_direct_ring.h"
#include "sd_direct_shared_memory.h"
#include "sd_direct_wire.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <utility>

namespace sd::direct
{
    class DirectSubscriberStub final : public IDirectSubscriber
    {
    public:
        explicit DirectSubscriberStub(const SubscriberConfig& cfg)
            : m_config(cfg)
        {
        }

        bool Start(UpdateCallback onUpdate, StateCallback onState) override
        {
            if (m_running.load())
            {
                return true;
            }

            m_update = std::move(onUpdate);
            m_state = std::move(onState);
            SetState(ConnectionState::Connecting);

            bool created = false;
            const std::size_t mappingBytes = sizeof(wire::RingHeader) + wire::kDefaultCapacityBytes;
            if (!m_region.OpenOrCreate(m_config.mappingName, mappingBytes, created))
            {
                SetState(ConnectionState::Disconnected);
                return false;
            }

            if (!AttachRing(m_region.Data(), m_region.Size(), true, m_ring))
            {
                m_region.Close();
                SetState(ConnectionState::Disconnected);
                return false;
            }

            bool eventCreated = false;
            if (!m_dataEvent.OpenOrCreateAutoReset(m_config.dataEventName, eventCreated))
            {
                m_region.Close();
                SetState(ConnectionState::Disconnected);
                return false;
            }

            if (!m_heartbeatEvent.OpenOrCreateAutoReset(m_config.heartbeatEventName, eventCreated))
            {
                m_dataEvent.Close();
                m_region.Close();
                SetState(ConnectionState::Disconnected);
                return false;
            }

            m_running.store(true);
            m_worker = std::thread(&DirectSubscriberStub::RunLoop, this);

            return true;
        }

        void Stop() override
        {
            if (!m_running.load())
            {
                return;
            }

            m_running.store(false);
            m_dataEvent.Signal();

            if (m_worker.joinable())
            {
                m_worker.join();
            }

            m_heartbeatEvent.Close();
            m_dataEvent.Close();
            m_region.Close();
            SetState(ConnectionState::Disconnected);
        }

        ConnectionState GetState() const override
        {
            return m_connectionState.load();
        }

        std::uint64_t GetLastSeq() const override
        {
            return m_lastSeq.load();
        }

        std::uint64_t GetDroppedCount() const override
        {
            return m_droppedCount.load();
        }

    private:
        void SetState(ConnectionState state)
        {
            const ConnectionState previous = m_connectionState.exchange(state);
            if (previous == state)
            {
                return;
            }

            if (m_state)
            {
                m_state(state);
            }
        }

        void RunLoop()
        {
            while (m_running.load())
            {
                const DWORD waitResult = m_dataEvent.Wait(static_cast<DWORD>(m_config.waitTimeout.count()));
                if (waitResult == WAIT_OBJECT_0)
                {
                    VariableUpdate update;
                    while (ReadNextUpsert(m_ring, update))
                    {
                        m_lastSeq.store(update.seq, std::memory_order_release);
                        if (m_update)
                        {
                            m_update(update);
                        }
                    }
                }

                if (m_ring.header != nullptr)
                {
                    const std::uint64_t nowUs = GetSteadyNowUs();
                    const std::uint64_t producerHeartbeatUs =
                        m_ring.header->lastProducerHeartbeatUs.load(std::memory_order_acquire);
                    const std::uint64_t staleLimitUs = static_cast<std::uint64_t>(m_config.staleTimeout.count()) * 1000ULL;

                    if (producerHeartbeatUs == 0)
                    {
                        SetState(ConnectionState::Connecting);
                    }
                    else if ((nowUs - producerHeartbeatUs) > staleLimitUs)
                    {
                        SetState(ConnectionState::Stale);
                    }
                    else
                    {
                        SetState(ConnectionState::Connected);
                    }

                    m_ring.header->lastConsumerHeartbeatUs.store(nowUs, std::memory_order_release);
                    m_droppedCount.store(m_ring.header->droppedCount.load(std::memory_order_acquire));
                }

                if (waitResult == WAIT_FAILED)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }

        SubscriberConfig m_config;
        UpdateCallback m_update;
        StateCallback m_state;
        std::atomic<bool> m_running {false};
        std::atomic<ConnectionState> m_connectionState {ConnectionState::Disconnected};
        std::atomic<std::uint64_t> m_lastSeq {0};
        std::atomic<std::uint64_t> m_droppedCount {0};
        SharedMemoryRegion m_region;
        NamedEvent m_dataEvent;
        NamedEvent m_heartbeatEvent;
        RingAttachResult m_ring;
        std::thread m_worker;
    };

    std::unique_ptr<IDirectSubscriber> CreateDirectSubscriber(const SubscriberConfig& cfg)
    {
        return std::make_unique<DirectSubscriberStub>(cfg);
    }
}
