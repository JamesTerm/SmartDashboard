#include "native_link_carrier_client.h"
#include "native_link_ipc_client.h"
#include "native_link_ipc_test_server.h"
#include "native_link_tcp_client.h"
#include "native_link_tcp_test_server.h"

#include <Windows.h>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace sd::nativelink
{
    namespace
    {
        constexpr int kConnectionStateConnected = 2;

        std::string MakeUniqueChannel(const char* baseName)
        {
            std::ostringstream builder;
            builder << baseName << "-" << static_cast<unsigned long>(::GetCurrentProcessId()) << "-"
                    << static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count());
            return builder.str();
        }

        bool WaitForCondition(const std::function<bool(void)>& predicate, int timeoutMs)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return predicate();
        }

        // Ian: The Win32 mapping/event names are process-global. These IPC tests
        // are much more sensitive to teardown overlap than pure in-memory tests,
        // so use one mutex to serialize the handful of shared-memory cases and
        // avoid one test's late worker thread touching the next test's channel.
        std::mutex& GetIpcTestMutex()
        {
            static std::mutex mutex;
            return mutex;
        }
    }

    TEST(NativeLinkIpcClientTests, ReceivesSnapshotAndLiveUpdatesFromSharedMemoryServer)
    {
        std::lock_guard<std::mutex> suiteLock(GetIpcTestMutex());
        const std::string channelId = MakeUniqueChannel("native-link-ipc-test");
        testsupport::NativeLinkIpcTestServer server(channelId);
        ASSERT_TRUE(server.Start());
        server.RegisterDefaultDashboardTopics();

        NativeLinkIpcClient client;
        std::mutex mutex;
        std::vector<UpdateEnvelope> events;
        std::vector<int> states;

        NativeLinkIpcClient::Config config;
        config.channelId = channelId;
        config.clientId = "dashboard-a";

        ASSERT_TRUE(client.Start(
            config,
            [&mutex, &events](const UpdateEnvelope& event)
            {
                std::lock_guard<std::mutex> lock(mutex);
                events.push_back(event);
            },
            [&mutex, &states](int state)
            {
                std::lock_guard<std::mutex> lock(mutex);
                states.push_back(state);
            }
        ));

        EXPECT_TRUE(WaitForCondition([&mutex, &events]()
        {
            std::lock_guard<std::mutex> lock(mutex);
            bool sawChooser = false;
            bool sawMove = false;
            for (const UpdateEnvelope& event : events)
            {
                if (event.topicPath == "Test/Auton_Selection/AutoChooser/selected" && event.value.type == ValueType::String)
                {
                    sawChooser = true;
                }
                if (event.topicPath == "TestMove" && event.value.type == ValueType::Double)
                {
                    sawMove = true;
                }
            }
            return sawChooser && sawMove;
        }, 2000));

        EXPECT_TRUE(WaitForCondition([&mutex, &states]()
        {
                std::lock_guard<std::mutex> lock(mutex);
                for (int state : states)
                {
                    if (state == kConnectionStateConnected)
                    {
                        return true;
                    }
            }
            return false;
        }, 2000));

        ASSERT_TRUE(server.PublishDouble("Timer", 12.5));

        EXPECT_TRUE(WaitForCondition([&mutex, &events]()
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const UpdateEnvelope& event : events)
            {
                if (event.topicPath == "Timer" && event.value.type == ValueType::Double && event.value.doubleValue == 12.5)
                {
                    return true;
                }
            }
            return false;
        }, 2000));

        client.Stop();
        server.Stop();
    }

    TEST(NativeLinkIpcClientTests, DashboardPublishReachesAuthoritativeServer)
    {
        std::lock_guard<std::mutex> suiteLock(GetIpcTestMutex());
        static int s_channelCounter = 0;
        const std::string channelId = MakeUniqueChannel((std::string("native-link-ipc-write-test-") + std::to_string(++s_channelCounter)).c_str());
        testsupport::NativeLinkIpcTestServer server(channelId);
        ASSERT_TRUE(server.Start());
        server.RegisterDefaultDashboardTopics();

        NativeLinkIpcClient client;
        std::mutex mutex;
        std::vector<UpdateEnvelope> events;
        std::vector<int> states;
        NativeLinkIpcClient::Config config;
        config.channelId = channelId;
        config.clientId = "dashboard-a";

        ASSERT_TRUE(client.Start(
            config,
            [&mutex, &events](const UpdateEnvelope& event)
            {
                std::lock_guard<std::mutex> lock(mutex);
                events.push_back(event);
            },
            [&mutex, &states](int state)
            {
                std::lock_guard<std::mutex> lock(mutex);
                states.push_back(state);
            }
        ));

        ASSERT_TRUE(WaitForCondition([&mutex, &states]()
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (int state : states)
            {
                if (state == kConnectionStateConnected)
                {
                    return true;
                }
            }
            return false;
        }, 2000));

        ASSERT_TRUE(client.Publish("TestMove", TopicValue::Double(3.5)));

        TopicValue latest;
        EXPECT_TRUE(WaitForCondition([&server, &latest]()
        {
            return server.TryGetLatestValue("TestMove", latest)
                && latest.type == ValueType::Double
                && latest.doubleValue == 3.5;
        }, 2000));

        client.Stop();
        server.Stop();
    }

    TEST(NativeLinkIpcClientTests, DashboardPublishSucceedsAfterServerSessionRestart)
    {
        std::lock_guard<std::mutex> suiteLock(GetIpcTestMutex());
        const std::string channelId = MakeUniqueChannel("native-link-ipc-restart-test");
        testsupport::NativeLinkIpcTestServer server(channelId);
        ASSERT_TRUE(server.Start());
        server.RegisterDefaultDashboardTopics();

        NativeLinkIpcClient client;
        std::mutex mutex;
        std::vector<int> states;
        NativeLinkIpcClient::Config config;
        config.channelId = channelId;
        config.clientId = "dashboard-a";

        ASSERT_TRUE(client.Start(
            config,
            [](const UpdateEnvelope&)
            {
            },
            [&mutex, &states](int state)
            {
                std::lock_guard<std::mutex> lock(mutex);
                states.push_back(state);
            }
        ));

        ASSERT_TRUE(WaitForCondition([&mutex, &states]()
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (int state : states)
            {
                if (state == kConnectionStateConnected)
                {
                    return true;
                }
            }
            return false;
        }, 2000));

        server.RestartSession();

        EXPECT_TRUE(WaitForCondition([&mutex, &states]()
        {
            std::lock_guard<std::mutex> lock(mutex);
            bool sawReconnect = false;
            bool sawConnectingAfterFirstConnect = false;
            bool sawFirstConnect = false;
            for (int state : states)
            {
                if (state == kConnectionStateConnected)
                {
                    if (sawConnectingAfterFirstConnect)
                    {
                        sawReconnect = true;
                    }
                    sawFirstConnect = true;
                }
                else if (sawFirstConnect)
                {
                    sawConnectingAfterFirstConnect = true;
                }
            }
            return sawReconnect;
        }, 2000));

        ASSERT_TRUE(client.Publish("TestMove", TopicValue::Double(4.5)));

        TopicValue latest;
        EXPECT_TRUE(WaitForCondition([&server, &latest]()
        {
            return server.TryGetLatestValue("TestMove", latest)
                && latest.type == ValueType::Double
                && latest.doubleValue == 4.5;
        }, 2000));

        client.Stop();
        server.Stop();
    }

    TEST(NativeLinkIpcClientTests, CarrierParserRecognizesSharedMemoryAndTcpNames)
    {
        NativeLinkCarrierKind kind = NativeLinkCarrierKind::Tcp;
        EXPECT_TRUE(TryParseCarrierKind("shm", kind));
        EXPECT_EQ(kind, NativeLinkCarrierKind::SharedMemory);
        EXPECT_TRUE(TryParseCarrierKind("shared-memory", kind));
        EXPECT_EQ(kind, NativeLinkCarrierKind::SharedMemory);
        EXPECT_TRUE(TryParseCarrierKind("tcp", kind));
        EXPECT_EQ(kind, NativeLinkCarrierKind::Tcp);
        EXPECT_FALSE(TryParseCarrierKind("udp", kind));
    }

    TEST(NativeLinkTcpClientTests, ReceivesSnapshotAndLiveUpdatesFromTcpServer)
    {
        const std::uint16_t port = 5811;
        testsupport::NativeLinkTcpTestServer server("native-link-tcp-test", port);
        ASSERT_TRUE(server.Start());
        server.RegisterDefaultDashboardTopics();

        NativeLinkTcpClient client;
        NativeLinkClientConfig config;
        config.carrierKind = NativeLinkCarrierKind::Tcp;
        config.channelId = "native-link-tcp-test";
        config.clientId = "dashboard-a";
        config.host = "127.0.0.1";
        config.port = port;

        std::mutex mutex;
        std::vector<UpdateEnvelope> events;
        std::vector<int> states;

        ASSERT_TRUE(client.Start(
            config,
            [&mutex, &events](const UpdateEnvelope& event)
            {
                std::lock_guard<std::mutex> lock(mutex);
                events.push_back(event);
            },
            [&mutex, &states](int state)
            {
                std::lock_guard<std::mutex> lock(mutex);
                states.push_back(state);
            }
        ));

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                bool sawSelected = false;
                for (const UpdateEnvelope& event : events)
                {
                    if (event.topicPath == "TestMove")
                    {
                        sawSelected = true;
                        break;
                    }
                }
                if (client.IsConnected() && sawSelected)
                {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        EXPECT_TRUE(client.IsConnected());
        ASSERT_TRUE(server.PublishDouble("Timer", 4.5));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        bool sawTimer = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const UpdateEnvelope& event : events)
            {
                if (event.topicPath == "Timer" && event.value.type == ValueType::Double && event.value.doubleValue == 4.5)
                {
                    sawTimer = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(sawTimer);

        client.Stop();
        server.Stop();
    }

    TEST(NativeLinkTcpClientTests, DashboardPublishReachesTcpAuthoritativeServer)
    {
        const std::uint16_t port = 5812;
        testsupport::NativeLinkTcpTestServer server("native-link-tcp-publish-test", port);
        ASSERT_TRUE(server.Start());
        server.RegisterDefaultDashboardTopics();

        NativeLinkTcpClient client;
        NativeLinkClientConfig config;
        config.carrierKind = NativeLinkCarrierKind::Tcp;
        config.channelId = "native-link-tcp-publish-test";
        config.clientId = "dashboard-a";
        config.host = "127.0.0.1";
        config.port = port;

        ASSERT_TRUE(client.Start(config, [](const UpdateEnvelope&) {}, [](int) {}));

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline && !client.IsConnected())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        ASSERT_TRUE(client.IsConnected());
        ASSERT_TRUE(client.Publish("TestMove", TopicValue::Double(7.25)));

        TopicValue latest;
        const auto valueDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        bool matched = false;
        while (std::chrono::steady_clock::now() < valueDeadline)
        {
            if (server.TryGetLatestValue("TestMove", latest)
                && latest.type == ValueType::Double
                && latest.doubleValue == 7.25)
            {
                matched = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        EXPECT_TRUE(matched);

        client.Stop();
        server.Stop();
    }

    TEST(NativeLinkTcpClientTests, DashboardPublishSucceedsAfterTcpServerSessionRestart)
    {
        // Ian: TCP session restart sends a new __snapshot_begin__ over the
        // existing socket (no connection close). The client must re-enter
        // Descriptors phase, emit Connecting, then re-reach Connected after
        // __live_begin__ arrives, and finally succeed with a publish — exactly
        // the same recovery path as the SHM session restart test.
        const std::uint16_t port = 5813;
        testsupport::NativeLinkTcpTestServer server("native-link-tcp-restart-test", port);
        ASSERT_TRUE(server.Start());
        server.RegisterDefaultDashboardTopics();

        NativeLinkTcpClient client;
        NativeLinkClientConfig config;
        config.carrierKind = NativeLinkCarrierKind::Tcp;
        config.channelId = "native-link-tcp-restart-test";
        config.clientId = "dashboard-a";
        config.host = "127.0.0.1";
        config.port = port;

        std::mutex mutex;
        std::vector<int> states;

        ASSERT_TRUE(client.Start(
            config,
            [](const UpdateEnvelope&) {},
            [&mutex, &states](int state)
            {
                std::lock_guard<std::mutex> lock(mutex);
                states.push_back(state);
            }
        ));

        ASSERT_TRUE(WaitForCondition([&mutex, &states]()
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (int state : states)
            {
                if (state == kConnectionStateConnected)
                {
                    return true;
                }
            }
            return false;
        }, 3000));

        server.RestartSession();

        // Expect: Connecting (from __snapshot_begin__) → Connected (from __live_begin__)
        EXPECT_TRUE(WaitForCondition([&mutex, &states]()
        {
            std::lock_guard<std::mutex> lock(mutex);
            bool sawFirstConnect = false;
            bool sawConnectingAfterFirstConnect = false;
            bool sawReconnect = false;
            for (int state : states)
            {
                if (state == kConnectionStateConnected)
                {
                    if (sawConnectingAfterFirstConnect)
                    {
                        sawReconnect = true;
                    }
                    sawFirstConnect = true;
                }
                else if (sawFirstConnect)
                {
                    sawConnectingAfterFirstConnect = true;
                }
            }
            return sawReconnect;
        }, 3000));

        ASSERT_TRUE(client.IsConnected());
        ASSERT_TRUE(client.Publish("TestMove", TopicValue::Double(4.5)));

        TopicValue latest;
        EXPECT_TRUE(WaitForCondition([&server, &latest]()
        {
            return server.TryGetLatestValue("TestMove", latest)
                && latest.type == ValueType::Double
                && latest.doubleValue == 4.5;
        }, 2000));

        client.Stop();
        server.Stop();
    }

}
