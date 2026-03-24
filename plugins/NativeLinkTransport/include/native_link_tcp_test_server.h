#pragma once

#include "native_link_core.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sd::nativelink::testsupport
{
    class NativeLinkTcpTestServer
    {
    public:
        NativeLinkTcpTestServer(std::string channelId, std::uint16_t port, std::string host = "127.0.0.1");
        ~NativeLinkTcpTestServer();

        bool Start();
        void Stop();

        void RegisterDefaultDashboardTopics();
        bool PublishBoolean(const std::string& topicPath, bool value);
        bool PublishDouble(const std::string& topicPath, double value);
        bool PublishString(const std::string& topicPath, const std::string& value);
        bool PublishStringArray(const std::string& topicPath, const std::vector<std::string>& value);

        bool TryGetLatestValue(const std::string& topicPath, TopicValue& outValue) const;
        void RestartSession();
        std::uint64_t GetServerSessionId() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
