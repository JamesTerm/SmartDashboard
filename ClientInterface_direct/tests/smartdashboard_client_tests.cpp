#include "sd_smartdashboard_client.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <filesystem>
#include <string>
#include <thread>

namespace sd::direct
{
    namespace
    {
        using namespace std::chrono_literals;

        struct TestChannels
        {
            std::wstring mappingName;
            std::wstring dataEventName;
            std::wstring heartbeatEventName;
        };

        TestChannels MakeSharedTestChannels()
        {
            static std::atomic<std::uint64_t> testChannelNonce {1};
            const std::uint64_t nonce = testChannelNonce.fetch_add(1);
            const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            const std::wstring suffix = L"." + std::to_wstring(nowUs) + L"." + std::to_wstring(nonce);

            TestChannels channels;
            channels.mappingName = L"Local\\SmartDashboard.Direct.Buffer" + suffix;
            channels.dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable" + suffix;
            channels.heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat" + suffix;
            return channels;
        }

        bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(10ms);
            }
            return predicate();
        }

        template <typename TickFn>
        void AnimateForVisualCheck(SmartDashboardClient& client, TickFn&& onTick)
        {
            // 2-second animation window so manual verification can confirm updates
            // continue to flow for already-populated widgets.
            for (int i = 0; i < 20; ++i)
            {
                onTick(i);
                client.FlushNow();
                std::this_thread::sleep_for(100ms);
            }
        }

        std::wstring MakeRetainedPath(const wchar_t* fileName)
        {
            const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
            std::filesystem::path root = (localAppData != nullptr && localAppData[0] != L'\0')
                ? std::filesystem::path(localAppData)
                : std::filesystem::temp_directory_path();
            root /= "SmartDashboard";
            root /= "DirectStoreTests";
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
            return (root / fileName).wstring();
        }

        void RemoveRetainedFile(const std::wstring& path)
        {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(path), ec);
        }
    }

    TEST(SmartDashboardClientTests, AssertiveGetPublishesDefaultAndCallbackReceivesUpdates)
    {
        const TestChannels channels = MakeSharedTestChannels();

        SmartDashboardClientConfig config;
        config.publisher.mappingName = channels.mappingName;
        config.publisher.dataEventName = channels.dataEventName;
        config.publisher.heartbeatEventName = channels.heartbeatEventName;
        config.publisher.autoFlushThread = false;
        config.subscriber.mappingName = channels.mappingName;
        config.subscriber.dataEventName = channels.dataEventName;
        config.subscriber.heartbeatEventName = channels.heartbeatEventName;
        config.enableRetainedStore = false;

        SmartDashboardClient client(config);
        ASSERT_TRUE(client.Start());

        std::atomic<bool> lastSeen {false};
        std::atomic<int> callbackCount {0};

        const auto token = client.SubscribeBoolean(
            "Demo/Ready",
            [&lastSeen, &callbackCount](bool value)
            {
                lastSeen.store(value);
                callbackCount.fetch_add(1);
            },
            true
        );
        ASSERT_TRUE(static_cast<bool>(token));

        const bool initial = client.GetBoolean("Demo/Ready", false);
        EXPECT_FALSE(initial);

        bool passiveRead = true;
        ASSERT_TRUE(WaitUntil(
            [&client, &passiveRead]()
            {
                return client.TryGetBoolean("Demo/Ready", passiveRead);
            },
            1s
        ));
        EXPECT_FALSE(passiveRead);

        client.PutBoolean("Demo/Ready", true);
        ASSERT_TRUE(client.FlushNow());

        ASSERT_TRUE(WaitUntil(
            [&lastSeen]()
            {
                return lastSeen.load();
            },
            2s
        ));
        EXPECT_GE(callbackCount.load(), 1);

        EXPECT_TRUE(client.Unsubscribe(token));
        client.Stop();
    }

    TEST(SmartDashboardClientTests, ReceivesBoolCommandFromDashboardChannel)
    {
        SmartDashboardClientConfig config;
        config.enableCommandSubscriber = true;
        config.publisher.autoFlushThread = false;

        SmartDashboardClient client(config);
        ASSERT_TRUE(client.Start());

        client.PutString("Demo/Visual/TestName", "Bool command receive");
        client.PutBoolean("Demo/Command/Enabled", false);
        ASSERT_TRUE(client.FlushNow());

        std::atomic<bool> commandValue {false};
        std::atomic<int> commandCount {0};

        const auto token = client.SubscribeBooleanCommand(
            "Demo/Command/Enabled",
            [&commandValue, &commandCount](bool value)
            {
                commandValue.store(value);
                commandCount.fetch_add(1);
            }
        );
        ASSERT_TRUE(static_cast<bool>(token));

        PublisherConfig commandPubCfg;
        commandPubCfg.mappingName = L"Local\\SmartDashboard.Direct.Command.Buffer";
        commandPubCfg.dataEventName = L"Local\\SmartDashboard.Direct.Command.DataAvailable";
        commandPubCfg.heartbeatEventName = L"Local\\SmartDashboard.Direct.Command.Heartbeat";
        commandPubCfg.autoFlushThread = false;

        auto commandPublisher = CreateDirectPublisher(commandPubCfg);
        ASSERT_TRUE(commandPublisher->Start());
        commandPublisher->PublishBool("Demo/Command/Enabled", true);
        ASSERT_TRUE(commandPublisher->FlushNow());

        ASSERT_TRUE(WaitUntil(
            [&commandValue]()
            {
                return commandValue.load();
            },
            2s
        ));
        EXPECT_GE(commandCount.load(), 1);

        AnimateForVisualCheck(client, [&client](int i)
        {
            client.PutBoolean("Demo/Command/Enabled", (i % 2) == 0);
            client.PutString("Demo/Visual/TestName", "Bool command receive (animating)");
        });

        commandPublisher->Stop();
        EXPECT_TRUE(client.Unsubscribe(token));
        client.Stop();
    }

    TEST(SmartDashboardClientTests, ReceivesDoubleCommandFromDashboardChannel)
    {
        SmartDashboardClientConfig config;
        config.enableCommandSubscriber = true;
        config.publisher.autoFlushThread = false;

        SmartDashboardClient client(config);
        ASSERT_TRUE(client.Start());

        client.PutString("Demo/Visual/TestName", "Double command receive");
        client.PutDouble("Demo/Command/Throttle", 0.0);
        ASSERT_TRUE(client.FlushNow());

        std::atomic<double> commandValue {0.0};
        std::atomic<int> commandCount {0};

        const auto token = client.SubscribeDoubleCommand(
            "Demo/Command/Throttle",
            [&commandValue, &commandCount](double value)
            {
                commandValue.store(value);
                commandCount.fetch_add(1);
            }
        );
        ASSERT_TRUE(static_cast<bool>(token));

        PublisherConfig commandPubCfg;
        commandPubCfg.mappingName = L"Local\\SmartDashboard.Direct.Command.Buffer";
        commandPubCfg.dataEventName = L"Local\\SmartDashboard.Direct.Command.DataAvailable";
        commandPubCfg.heartbeatEventName = L"Local\\SmartDashboard.Direct.Command.Heartbeat";
        commandPubCfg.autoFlushThread = false;

        auto commandPublisher = CreateDirectPublisher(commandPubCfg);
        ASSERT_TRUE(commandPublisher->Start());
        commandPublisher->PublishDouble("Demo/Command/Throttle", 0.42);
        ASSERT_TRUE(commandPublisher->FlushNow());

        ASSERT_TRUE(WaitUntil(
            [&commandValue]()
            {
                return commandValue.load() > 0.4;
            },
            2s
        ));
        EXPECT_GE(commandCount.load(), 1);

        AnimateForVisualCheck(client, [&client](int i)
        {
            const double t = static_cast<double>(i) / 19.0;
            const double ramp = -1.0 + (2.0 * t);
            client.PutDouble("Demo/Command/Throttle", ramp);
            client.PutString("Demo/Visual/TestName", "Double command receive (animating)");
        });

        commandPublisher->Stop();
        EXPECT_TRUE(client.Unsubscribe(token));
        client.Stop();
    }

    TEST(SmartDashboardClientTests, ReceivesStringCommandFromDashboardChannel)
    {
        SmartDashboardClientConfig config;
        config.enableCommandSubscriber = true;
        config.publisher.autoFlushThread = false;

        SmartDashboardClient client(config);
        ASSERT_TRUE(client.Start());

        client.PutString("Demo/Visual/TestName", "String command receive");
        client.PutString("Demo/Command/Mode", "Waiting");
        ASSERT_TRUE(client.FlushNow());

        std::atomic<int> commandCount {0};
        std::string lastValue;
        std::mutex mutex;

        const auto token = client.SubscribeStringCommand(
            "Demo/Command/Mode",
            [&lastValue, &mutex, &commandCount](const std::string& value)
            {
                std::lock_guard<std::mutex> lock(mutex);
                lastValue = value;
                commandCount.fetch_add(1);
            }
        );
        ASSERT_TRUE(static_cast<bool>(token));

        PublisherConfig commandPubCfg;
        commandPubCfg.mappingName = L"Local\\SmartDashboard.Direct.Command.Buffer";
        commandPubCfg.dataEventName = L"Local\\SmartDashboard.Direct.Command.DataAvailable";
        commandPubCfg.heartbeatEventName = L"Local\\SmartDashboard.Direct.Command.Heartbeat";
        commandPubCfg.autoFlushThread = false;

        auto commandPublisher = CreateDirectPublisher(commandPubCfg);
        ASSERT_TRUE(commandPublisher->Start());
        commandPublisher->PublishString("Demo/Command/Mode", "Manual");
        ASSERT_TRUE(commandPublisher->FlushNow());

        ASSERT_TRUE(WaitUntil(
            [&lastValue, &mutex]()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return lastValue == "Manual";
            },
            2s
        ));
        EXPECT_GE(commandCount.load(), 1);

        AnimateForVisualCheck(client, [&client](int i)
        {
            if ((i % 3) == 0)
            {
                client.PutString("Demo/Command/Mode", "Manual");
            }
            else if ((i % 3) == 1)
            {
                client.PutString("Demo/Command/Mode", "Assist");
            }
            else
            {
                client.PutString("Demo/Command/Mode", "Auto");
            }
            client.PutString("Demo/Visual/TestName", "String command receive (animating)");
        });

        commandPublisher->Stop();
        EXPECT_TRUE(client.Unsubscribe(token));
        client.Stop();
    }

    TEST(SmartDashboardClientTests, RetainedStoreRestoresValuesAcrossClientRestart)
    {
        const std::wstring retainedPath = MakeRetainedPath(L"retained_store_restart_test.txt");
        RemoveRetainedFile(retainedPath);

        SmartDashboardClientConfig firstConfig;
        firstConfig.enableSubscriber = false;
        firstConfig.enableRetainedStore = true;
        firstConfig.retainedStorePersistencePath = retainedPath;
        firstConfig.publisher.autoFlushThread = false;

        {
            SmartDashboardClient first(firstConfig);
            ASSERT_TRUE(first.Start());

            first.PutDouble("Test/Retained/Double", 3.1415);
            first.PutBoolean("Test/Retained/Bool", true);
            first.PutString("Test/Retained/String", "persisted");
            ASSERT_TRUE(first.FlushNow());

            first.Stop();
        }

        SmartDashboardClientConfig secondConfig;
        secondConfig.enableSubscriber = false;
        secondConfig.enableRetainedStore = true;
        secondConfig.retainedStorePersistencePath = retainedPath;
        secondConfig.publisher.autoFlushThread = false;

        SmartDashboardClient second(secondConfig);
        ASSERT_TRUE(second.Start());

        double restoredDouble = 0.0;
        bool restoredBool = false;
        std::string restoredString;
        EXPECT_TRUE(second.TryGetDouble("Test/Retained/Double", restoredDouble));
        EXPECT_TRUE(second.TryGetBoolean("Test/Retained/Bool", restoredBool));
        EXPECT_TRUE(second.TryGetString("Test/Retained/String", restoredString));
        EXPECT_NEAR(restoredDouble, 3.1415, 1e-9);
        EXPECT_TRUE(restoredBool);
        EXPECT_EQ(restoredString, "persisted");

        second.Stop();
        RemoveRetainedFile(retainedPath);
    }
}
