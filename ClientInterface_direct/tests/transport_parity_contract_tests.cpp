#include "sd_direct_publisher.h"
#include "sd_smartdashboard_client.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
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

        TestChannels MakeUniqueChannels(const wchar_t* stem)
        {
            static std::atomic<std::uint64_t> nonce {1};
            const std::uint64_t id = nonce.fetch_add(1);
            const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            const std::wstring suffix = L"." + std::to_wstring(nowUs) + L"." + std::to_wstring(id);

            TestChannels channels;
            channels.mappingName = std::wstring(stem) + L".Buffer" + suffix;
            channels.dataEventName = std::wstring(stem) + L".DataAvailable" + suffix;
            channels.heartbeatEventName = std::wstring(stem) + L".Heartbeat" + suffix;
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

        std::wstring MakeRetainedPath()
        {
            const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
            std::filesystem::path root = (localAppData != nullptr && localAppData[0] != L'\0')
                ? std::filesystem::path(localAppData)
                : std::filesystem::temp_directory_path();

            root /= "SmartDashboard";
            root /= "TransportParityTests";

            std::error_code ec;
            std::filesystem::create_directories(root, ec);

            const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            const std::wstring fileName = L"retained." + std::to_wstring(nowUs) + L".txt";
            return (root / fileName).wstring();
        }

        void RemoveFileIfExists(const std::wstring& path)
        {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(path), ec);
        }
    }

    // Contract-style tests for transport adapters.
    // Current fixture covers the direct adapter path; NT fixture will be added later.
    TEST(TransportParityContractTests, DirectTelemetryRoundtripBoolDoubleString)
    {
        const TestChannels channels = MakeUniqueChannels(L"Local\\SmartDashboard.Contract.Telemetry");

        SmartDashboardClientConfig robotConfig;
        robotConfig.publisher.mappingName = channels.mappingName;
        robotConfig.publisher.dataEventName = channels.dataEventName;
        robotConfig.publisher.heartbeatEventName = channels.heartbeatEventName;
        robotConfig.publisher.autoFlushThread = false;
        robotConfig.enableSubscriber = false;
        robotConfig.enableRetainedStore = false;

        SmartDashboardClientConfig dashboardConfig;
        dashboardConfig.publisher.mappingName = channels.mappingName;
        dashboardConfig.publisher.dataEventName = channels.dataEventName;
        dashboardConfig.publisher.heartbeatEventName = channels.heartbeatEventName;
        dashboardConfig.publisher.autoFlushThread = false;
        dashboardConfig.subscriber.mappingName = channels.mappingName;
        dashboardConfig.subscriber.dataEventName = channels.dataEventName;
        dashboardConfig.subscriber.heartbeatEventName = channels.heartbeatEventName;
        dashboardConfig.enableSubscriber = true;
        dashboardConfig.enableRetainedStore = false;

        SmartDashboardClient robot(robotConfig);
        SmartDashboardClient dashboard(dashboardConfig);
        ASSERT_TRUE(robot.Start());
        ASSERT_TRUE(dashboard.Start());

        robot.PutBoolean("Contract/Armed", true);
        robot.PutDouble("Contract/Setpoint", 42.5);
        robot.PutString("Contract/Mode", "Auto");
        ASSERT_TRUE(robot.FlushNow());

        bool armed = false;
        double setpoint = 0.0;
        std::string mode;

        ASSERT_TRUE(WaitUntil(
            [&dashboard, &armed, &setpoint, &mode]()
            {
                bool hasAll = true;
                hasAll = hasAll && dashboard.TryGetBoolean("Contract/Armed", armed);
                hasAll = hasAll && dashboard.TryGetDouble("Contract/Setpoint", setpoint);
                hasAll = hasAll && dashboard.TryGetString("Contract/Mode", mode);
                return hasAll;
            },
            2s
        ));

        EXPECT_TRUE(armed);
        EXPECT_DOUBLE_EQ(setpoint, 42.5);
        EXPECT_EQ(mode, "Auto");

        dashboard.Stop();
        robot.Stop();
    }

    TEST(TransportParityContractTests, DirectCommandRoundtripBoolDoubleString)
    {
        const TestChannels commandChannels = MakeUniqueChannels(L"Local\\SmartDashboard.Contract.Command");

        SmartDashboardClientConfig robotConfig;
        robotConfig.publisher.autoFlushThread = false;
        robotConfig.enableSubscriber = false;
        robotConfig.enableRetainedStore = false;
        robotConfig.enableCommandSubscriber = true;
        robotConfig.commandSubscriber.mappingName = commandChannels.mappingName;
        robotConfig.commandSubscriber.dataEventName = commandChannels.dataEventName;
        robotConfig.commandSubscriber.heartbeatEventName = commandChannels.heartbeatEventName;

        SmartDashboardClient robot(robotConfig);
        ASSERT_TRUE(robot.Start());

        std::atomic<bool> boolSeen {false};
        std::atomic<bool> doubleSeen {false};
        std::atomic<bool> stringSeen {false};

        const auto boolToken = robot.SubscribeBooleanCommand("Contract/Cmd/Armed", [&boolSeen](bool value)
        {
            if (value)
            {
                boolSeen.store(true);
            }
        });

        const auto doubleToken = robot.SubscribeDoubleCommand("Contract/Cmd/Setpoint", [&doubleSeen](double value)
        {
            if (value > 9.0)
            {
                doubleSeen.store(true);
            }
        });

        std::mutex stringMutex;
        std::string lastString;
        const auto stringToken = robot.SubscribeStringCommand("Contract/Cmd/Mode", [&stringSeen, &stringMutex, &lastString](const std::string& value)
        {
            std::lock_guard<std::mutex> lock(stringMutex);
            lastString = value;
            if (value == "Manual")
            {
                stringSeen.store(true);
            }
        });

        ASSERT_TRUE(static_cast<bool>(boolToken));
        ASSERT_TRUE(static_cast<bool>(doubleToken));
        ASSERT_TRUE(static_cast<bool>(stringToken));

        PublisherConfig dashboardCommandPublisherConfig;
        dashboardCommandPublisherConfig.mappingName = commandChannels.mappingName;
        dashboardCommandPublisherConfig.dataEventName = commandChannels.dataEventName;
        dashboardCommandPublisherConfig.heartbeatEventName = commandChannels.heartbeatEventName;
        dashboardCommandPublisherConfig.autoFlushThread = false;

        auto dashboardCommandPublisher = CreateDirectPublisher(dashboardCommandPublisherConfig);
        ASSERT_TRUE(dashboardCommandPublisher->Start());
        dashboardCommandPublisher->PublishBool("Contract/Cmd/Armed", true);
        dashboardCommandPublisher->PublishDouble("Contract/Cmd/Setpoint", 9.5);
        dashboardCommandPublisher->PublishString("Contract/Cmd/Mode", "Manual");
        ASSERT_TRUE(dashboardCommandPublisher->FlushNow());

        ASSERT_TRUE(WaitUntil(
            [&boolSeen, &doubleSeen, &stringSeen]()
            {
                return boolSeen.load() && doubleSeen.load() && stringSeen.load();
            },
            2s
        ));

        std::string observedString;
        {
            std::lock_guard<std::mutex> lock(stringMutex);
            observedString = lastString;
        }
        EXPECT_EQ(observedString, "Manual");

        dashboardCommandPublisher->Stop();
        EXPECT_TRUE(robot.Unsubscribe(boolToken));
        EXPECT_TRUE(robot.Unsubscribe(doubleToken));
        EXPECT_TRUE(robot.Unsubscribe(stringToken));
        robot.Stop();
    }

    TEST(TransportParityContractTests, DirectReconnectReplaysLatestFromRetainedStore)
    {
        const std::wstring retainedPath = MakeRetainedPath();
        RemoveFileIfExists(retainedPath);

        SmartDashboardClientConfig firstConfig;
        firstConfig.enableSubscriber = false;
        firstConfig.enableRetainedStore = true;
        firstConfig.retainedStorePersistencePath = retainedPath;
        firstConfig.publisher.autoFlushThread = false;

        {
            SmartDashboardClient first(firstConfig);
            ASSERT_TRUE(first.Start());

            first.PutBoolean("Contract/Retained/Bool", true);
            first.PutDouble("Contract/Retained/Double", 12.25);
            first.PutString("Contract/Retained/String", "restored");
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

        bool restoredBool = false;
        double restoredDouble = 0.0;
        std::string restoredString;

        EXPECT_TRUE(second.TryGetBoolean("Contract/Retained/Bool", restoredBool));
        EXPECT_TRUE(second.TryGetDouble("Contract/Retained/Double", restoredDouble));
        EXPECT_TRUE(second.TryGetString("Contract/Retained/String", restoredString));
        EXPECT_TRUE(restoredBool);
        EXPECT_DOUBLE_EQ(restoredDouble, 12.25);
        EXPECT_EQ(restoredString, "restored");

        second.Stop();
        RemoveFileIfExists(retainedPath);
    }

    TEST(TransportParityContractTests, DirectRobotSurvivesStressReplaysDashboardOwnedCommandValue)
    {
        const TestChannels commandChannels = MakeUniqueChannels(L"Local\\SmartDashboard.Contract.CommandRetained");

        SmartDashboardClientConfig dashboardConfig;
        dashboardConfig.publisher.mappingName = commandChannels.mappingName;
        dashboardConfig.publisher.dataEventName = commandChannels.dataEventName;
        dashboardConfig.publisher.heartbeatEventName = commandChannels.heartbeatEventName;
        dashboardConfig.publisher.autoFlushThread = true;
        dashboardConfig.enableSubscriber = false;
        dashboardConfig.enableRetainedStore = false;

        SmartDashboardClient dashboard(dashboardConfig);
        ASSERT_TRUE(dashboard.Start());
        dashboard.PutDouble("TestMove", 3.5);
        ASSERT_TRUE(dashboard.FlushNow());

        double testMove = 0.0;
        SubscriberConfig robotSubConfig;
        robotSubConfig.mappingName = commandChannels.mappingName;
        robotSubConfig.dataEventName = commandChannels.dataEventName;
        robotSubConfig.heartbeatEventName = commandChannels.heartbeatEventName;

        auto robot = CreateDirectSubscriber(robotSubConfig);
        ASSERT_TRUE(robot->Start(
            [&testMove](const VariableUpdate& update)
            {
                if (update.key == "TestMove" && update.type == ValueType::Double)
                {
                    testMove = update.value.doubleValue;
                }
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(WaitUntil(
            [&testMove]()
            {
                return testMove > 1.0;
            },
            2s
        ));
        EXPECT_DOUBLE_EQ(testMove, 3.5);

        robot->Stop();

        auto robotRestarted = CreateDirectSubscriber(robotSubConfig);
        ASSERT_TRUE(robotRestarted->Start(
            [&testMove](const VariableUpdate& update)
            {
                if (update.key == "TestMove" && update.type == ValueType::Double)
                {
                    testMove = update.value.doubleValue;
                }
            },
            [](ConnectionState)
            {
            }
        ));

        testMove = 0.0;
        ASSERT_TRUE(WaitUntil(
            [&testMove]()
            {
                return testMove > 1.0;
            },
            2s
        ));
        EXPECT_DOUBLE_EQ(testMove, 3.5);

        robotRestarted->Stop();
        dashboard.Stop();
    }

    TEST(TransportParityContractTests, DirectDashboardSurvivesStressKeepsRobotOwnedChooserSelection)
    {
        const TestChannels telemetryChannels = MakeUniqueChannels(L"Local\\SmartDashboard.Contract.TelemetryRetained");
        const TestChannels dashboardPublisherChannels = MakeUniqueChannels(L"Local\\SmartDashboard.Contract.TelemetryRetained.DashboardPub");

        SmartDashboardClientConfig robotConfig;
        robotConfig.publisher.mappingName = telemetryChannels.mappingName;
        robotConfig.publisher.dataEventName = telemetryChannels.dataEventName;
        robotConfig.publisher.heartbeatEventName = telemetryChannels.heartbeatEventName;
        robotConfig.publisher.autoFlushThread = true;
        robotConfig.enableSubscriber = false;
        robotConfig.enableRetainedStore = false;

        SmartDashboardClient robot(robotConfig);
        ASSERT_TRUE(robot.Start());

        robot.PutString("Test/Auton_Selection/AutoChooser/.type", "String Chooser");
        robot.PutStringArray("Test/Auton_Selection/AutoChooser/options", {"Do Nothing", "Just Move Forward", "Just Rotate"});
        robot.PutString("Test/Auton_Selection/AutoChooser/default", "Do Nothing");
        robot.PutString("Test/Auton_Selection/AutoChooser/active", "Just Move Forward");
        robot.PutString("Test/Auton_Selection/AutoChooser/selected", "Just Move Forward");
        ASSERT_TRUE(robot.FlushNow());

        SmartDashboardClientConfig dashboardConfig;
        dashboardConfig.publisher.mappingName = dashboardPublisherChannels.mappingName;
        dashboardConfig.publisher.dataEventName = dashboardPublisherChannels.dataEventName;
        dashboardConfig.publisher.heartbeatEventName = dashboardPublisherChannels.heartbeatEventName;
        dashboardConfig.publisher.autoFlushThread = false;
        dashboardConfig.subscriber.mappingName = telemetryChannels.mappingName;
        dashboardConfig.subscriber.dataEventName = telemetryChannels.dataEventName;
        dashboardConfig.subscriber.heartbeatEventName = telemetryChannels.heartbeatEventName;
        dashboardConfig.enableSubscriber = true;
        dashboardConfig.enableRetainedStore = false;

        SmartDashboardClient dashboard(dashboardConfig);
        ASSERT_TRUE(dashboard.Start());

        std::string selected;
        ASSERT_TRUE(WaitUntil(
            [&dashboard, &selected]()
            {
                return dashboard.TryGetString("Test/Auton_Selection/AutoChooser/selected", selected);
            },
            2s
        ));
        EXPECT_EQ(selected, "Just Move Forward");

        dashboard.Stop();

        SmartDashboardClient dashboardRestarted(dashboardConfig);
        ASSERT_TRUE(dashboardRestarted.Start());

        selected.clear();
        ASSERT_TRUE(WaitUntil(
            [&dashboardRestarted, &selected]()
            {
                return dashboardRestarted.TryGetString("Test/Auton_Selection/AutoChooser/selected", selected);
            },
            2s
        ));
        EXPECT_EQ(selected, "Just Move Forward");

        dashboardRestarted.Stop();
        robot.Stop();
    }
}
