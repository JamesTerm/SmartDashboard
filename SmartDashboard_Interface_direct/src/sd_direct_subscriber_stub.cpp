#include "sd_direct_subscriber.h"

#include "sd_direct_clock.h"
#include "sd_direct_ring.h"
#include "sd_direct_shared_memory.h"
#include "sd_direct_wire.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

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
            m_instanceId = NextInstanceId();
            m_lastProducerHeartbeatUs = 0;
            m_lastHeartbeatChangeUs = 0;
            m_staleLogged = false;
            SetState(ConnectionState::Connecting);

            // Shared-memory consumer endpoint setup.
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

            m_readCursor = (m_ring.header != nullptr)
                ? m_ring.header->writeIndex.load(std::memory_order_acquire)
                : 0;

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

            // Dedicated receive worker thread.
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
            m_lastProducerHeartbeatUs = 0;
            m_lastHeartbeatChangeUs = 0;
            m_staleLogged = false;
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
		void DebugLog(const std::string& text) const
		{
#ifdef _WIN32
			OutputDebugStringA(text.c_str());
#else
			(void)text;
#endif
		}

        void SetState(ConnectionState state)
        {
            const ConnectionState previous = m_connectionState.exchange(state);
            if (previous == state)
            {
                return;
            }

			std::ostringstream out;
			out << "[DirectSubscriber] state " << static_cast<int>(previous)
				<< " -> " << static_cast<int>(state)
				<< " lastSeq=" << m_lastSeq.load(std::memory_order_acquire)
				<< " dropped=" << m_droppedCount.load(std::memory_order_acquire)
				<< " instance=" << m_instanceId
				<< "\n";
			DebugLog(out.str());

            if (m_state)
            {
                m_state(state);
            }
        }

        void RunLoop()
        {
            while (m_running.load())
            {
                // Event-driven wait loop (reacts to publisher data event).
                const DWORD waitResult = m_dataEvent.Wait(static_cast<DWORD>(m_config.waitTimeout.count()));
                if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_TIMEOUT)
                {
                    VariableUpdate update;

                    // Drain pattern: consume all currently queued ring messages.
                    // We do this on timeout as well so late-joining subscribers can
                    // recover already-buffered frames even if they missed the original signal.
                    while (ReadNextUpsert(static_cast<const RingAttachResult&>(m_ring), m_readCursor, update))
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

                    if (producerHeartbeatUs != m_lastProducerHeartbeatUs)
                    {
                        m_lastProducerHeartbeatUs = producerHeartbeatUs;
                        m_lastHeartbeatChangeUs = nowUs;
                        m_staleLogged = false;
                    }

                    // Heartbeat timeout -> stale-state detection algorithm.
                    if (producerHeartbeatUs == 0)
                    {
                        SetState(ConnectionState::Connecting);
                    }
                    else if ((nowUs - producerHeartbeatUs) > staleLimitUs)
                    {
                        const std::uint64_t heartbeatQuietUs = (m_lastHeartbeatChangeUs == 0)
                            ? (nowUs - producerHeartbeatUs)
                            : (nowUs - m_lastHeartbeatChangeUs);
                        if (heartbeatQuietUs > staleLimitUs)
                        {
                            if (!m_staleLogged)
                            {
							std::ostringstream out;
							out << "[DirectSubscriber] stale producerHeartbeatUs=" << producerHeartbeatUs
								<< " nowUs=" << nowUs
								<< " deltaUs=" << (nowUs - producerHeartbeatUs)
								<< " staleLimitUs=" << staleLimitUs
								<< " quietUs=" << heartbeatQuietUs
								<< " instance=" << m_instanceId
								<< "\n";
							DebugLog(out.str());
                                m_staleLogged = true;
                            }
                            SetState(ConnectionState::Stale);
                        }
                        else
                        {
                            SetState(ConnectionState::Connected);
                        }
                    }
                    else
                    {
                        m_staleLogged = false;
                        SetState(ConnectionState::Connected);
                    }

                    const std::uint64_t previousInstanceId =
                        m_ring.header->consumerInstanceId.exchange(m_instanceId, std::memory_order_acq_rel);
                    if (previousInstanceId != m_instanceId)
                    {
                        std::ostringstream out;
					out << "[DirectSubscriber] consumer instance claim old=" << previousInstanceId
						<< " new=" << m_instanceId
						<< " keepReadCursor=" << m_readCursor
						<< "\n";
					DebugLog(out.str());
                    }

                    m_ring.header->lastConsumerHeartbeatUs.store(nowUs, std::memory_order_release);
                    m_droppedCount.store(m_ring.header->droppedCount.load(std::memory_order_acquire));
                }

                if (waitResult == WAIT_FAILED)
                {
                    // Backoff to avoid tight retry loop on transient wait failures.
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }

        static std::uint64_t NextInstanceId()
        {
            static std::atomic<std::uint64_t> nextId {1};
            const std::uint64_t localId = nextId.fetch_add(1, std::memory_order_relaxed);
            const std::uint64_t nowUs = GetSteadyNowUs();

#ifdef _WIN32
            const std::uint64_t pidPart = static_cast<std::uint64_t>(::GetCurrentProcessId()) << 32;
            return pidPart ^ nowUs ^ localId;
#else
            return nowUs ^ (localId << 32);
#endif
        }

        SubscriberConfig m_config;
        UpdateCallback m_update;
        StateCallback m_state;
        std::atomic<bool> m_running {false};
        std::atomic<ConnectionState> m_connectionState {ConnectionState::Disconnected};
        std::atomic<std::uint64_t> m_lastSeq {0};
        std::atomic<std::uint64_t> m_droppedCount {0};
        std::uint64_t m_instanceId = 0;
        std::uint64_t m_lastProducerHeartbeatUs = 0;
        std::uint64_t m_lastHeartbeatChangeUs = 0;
        bool m_staleLogged = false;
        std::uint32_t m_readCursor = 0;
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
