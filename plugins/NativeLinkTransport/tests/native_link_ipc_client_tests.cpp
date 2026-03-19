#include "native_link_ipc_client.h"
#include "native_link_ipc_test_server.h"

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

}
