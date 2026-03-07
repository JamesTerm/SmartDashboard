#include "sd_direct_subscriber.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

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
            m_running.store(true);
            m_connectionState.store(ConnectionState::Connected);
            m_worker = std::thread(&DirectSubscriberStub::RunLoop, this);

            if (m_state)
            {
                m_state(ConnectionState::Connected);
            }

            return true;
        }

        void Stop() override
        {
            if (!m_running.exchange(false))
            {
                return;
            }

            if (m_worker.joinable())
            {
                m_worker.join();
            }

            m_running.store(false);
            m_connectionState.store(ConnectionState::Disconnected);

            if (m_state)
            {
                m_state(ConnectionState::Disconnected);
            }
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
        void EmitUpdateBool(const std::string& key, bool value)
        {
            if (!m_update)
            {
                return;
            }

            VariableUpdate update;
            update.key = key;
            update.type = ValueType::Bool;
            update.value.boolValue = value;
            update.seq = ++m_lastSeq;
            update.sourceTimestampUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()
            );
            m_update(update);
        }

        void EmitUpdateDouble(const std::string& key, double value)
        {
            if (!m_update)
            {
                return;
            }

            VariableUpdate update;
            update.key = key;
            update.type = ValueType::Double;
            update.value.doubleValue = value;
            update.seq = ++m_lastSeq;
            update.sourceTimestampUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()
            );
            m_update(update);
        }

        void EmitUpdateString(const std::string& key, const std::string& value)
        {
            if (!m_update)
            {
                return;
            }

            VariableUpdate update;
            update.key = key;
            update.type = ValueType::String;
            update.value.stringValue = value;
            update.seq = ++m_lastSeq;
            update.sourceTimestampUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()
            );
            m_update(update);
        }

        void RunLoop()
        {
            int tick = 0;
            while (m_running.load())
            {
                EmitUpdateBool("Robot/Enabled", (tick % 2) == 0);
                EmitUpdateDouble("Drive/Speed", static_cast<double>(tick % 100) * 0.25);
                EmitUpdateString("Status/Mode", (tick % 20) < 10 ? "Teleop" : "Auto");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                ++tick;
            }
        }

        SubscriberConfig m_config;
        UpdateCallback m_update;
        StateCallback m_state;
        std::atomic<bool> m_running {false};
        std::atomic<ConnectionState> m_connectionState {ConnectionState::Disconnected};
        std::atomic<std::uint64_t> m_lastSeq {0};
        std::atomic<std::uint64_t> m_droppedCount {0};
        std::thread m_worker;
    };

    std::unique_ptr<IDirectSubscriber> CreateDirectSubscriber(const SubscriberConfig& cfg)
    {
        return std::make_unique<DirectSubscriberStub>(cfg);
    }
}
