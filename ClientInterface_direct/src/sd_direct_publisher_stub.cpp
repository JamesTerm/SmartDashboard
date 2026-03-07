#include "sd_direct_publisher.h"

#include <atomic>
#include <unordered_map>

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
            m_running.store(true);
            return true;
        }

        void Stop() override
        {
            m_running.store(false);
        }

        void PublishBool(std::string_view key, bool value) override
        {
            m_boolValues[std::string(key)] = value;
        }

        void PublishDouble(std::string_view key, double value) override
        {
            m_doubleValues[std::string(key)] = value;
        }

        void PublishString(std::string_view key, std::string_view value) override
        {
            m_stringValues[std::string(key)] = std::string(value);
        }

        bool FlushNow() override
        {
            if (!m_running.load())
            {
                return false;
            }

            ++m_publishedSeq;
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
        PublisherConfig m_config;
        std::atomic<bool> m_running {false};
        std::atomic<std::uint64_t> m_publishedSeq {0};
        std::atomic<std::uint64_t> m_droppedCount {0};
        std::unordered_map<std::string, bool> m_boolValues;
        std::unordered_map<std::string, double> m_doubleValues;
        std::unordered_map<std::string, std::string> m_stringValues;
    };

    std::unique_ptr<IDirectPublisher> CreateDirectPublisher(const PublisherConfig& cfg)
    {
        return std::make_unique<DirectPublisherStub>(cfg);
    }
}
