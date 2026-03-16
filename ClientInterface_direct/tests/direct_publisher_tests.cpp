#include "sd_direct_publisher.h"
#include "sd_direct_subscriber.h"
#include "sd_smartdashboard_client.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <numbers>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

        TestChannels MakeTestChannels()
        {
            // Teaching mode default: use the same channel names as SmartDashboardApp,
            // so students can run these tests and immediately see UI updates.
            // Set SD_DIRECT_TEST_USE_ISOLATED_CHANNELS=1 to isolate each test run.
            const char* useIsolated = std::getenv("SD_DIRECT_TEST_USE_ISOLATED_CHANNELS");
            if (useIsolated == nullptr || std::string(useIsolated) != "1")
            {
                TestChannels channels;
                channels.mappingName = L"Local\\SmartDashboard.Direct.Buffer";
                channels.dataEventName = L"Local\\SmartDashboard.Direct.DataAvailable";
                channels.heartbeatEventName = L"Local\\SmartDashboard.Direct.Heartbeat";
                return channels;
            }

            static std::atomic<std::uint64_t> counter {0};
            const std::uint64_t id = counter.fetch_add(1) + 1;
            const std::uint64_t tick = static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()
            );
            const std::wstring suffix = std::to_wstring(tick) + L"." + std::to_wstring(id);

            TestChannels channels;
            channels.mappingName = L"Local\\SmartDashboard.Direct.Test.Buffer." + suffix;
            channels.dataEventName = L"Local\\SmartDashboard.Direct.Test.Data." + suffix;
            channels.heartbeatEventName = L"Local\\SmartDashboard.Direct.Test.Heartbeat." + suffix;
            return channels;
        }

        bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
        {
            // Poll with a small sleep to avoid busy-waiting while still reacting quickly.
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

        template <typename PublishFn>
        void PublishForDuration(
            IDirectPublisher& publisher,
            std::chrono::milliseconds duration,
            std::chrono::milliseconds step,
            PublishFn&& publish
        )
        {
            // Manual publish loop used by all tests.
            // We flush each iteration so subscriber callbacks receive new data promptly.
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < duration)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start
                );
                publish(elapsed, duration);
                publisher.FlushNow();
                std::this_thread::sleep_for(step);
            }
        }

    }

    TEST(DirectPublisherTests, StreamsCreativeBoolPattern)
    {
        // Subscriber reads updates (dashboard side), publisher writes updates (client side).
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        // Disable auto flush for deterministic behavior in tests.
        pubConfig.autoFlushThread = false;

        auto subscriber = CreateDirectSubscriber(subConfig);
        auto publisher = CreateDirectPublisher(pubConfig);

        std::vector<bool> observed;
        std::mutex observedMutex;

        ASSERT_TRUE(subscriber->Start(
            [&observed, &observedMutex](const VariableUpdate& update)
            {
                // Filter to the exact key/type this test owns.
                if (update.key != "Test/Bool" || update.type != ValueType::Bool)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(observedMutex);
                observed.push_back(update.value.boolValue);
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(publisher->Start());

        PublishForDuration(*publisher, 3s, 100ms, [&publisher](std::chrono::milliseconds elapsed, std::chrono::milliseconds)
        {
            // Example bool pattern: TTFFF repeat every 5 slots.
            const auto slot = elapsed.count() / 100;
            const bool value = ((slot % 5) == 0) || ((slot % 5) == 1);
            publisher->PublishBool("Test/Bool", value);
        });

        ASSERT_TRUE(WaitUntil(
            [&observed, &observedMutex]()
            {
                std::lock_guard<std::mutex> lock(observedMutex);
                return observed.size() >= 5;
            },
            2s
        ));

        publisher->Stop();
        subscriber->Stop();

        bool sawTrue = false;
        bool sawFalse = false;
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            for (const bool value : observed)
            {
                sawTrue = sawTrue || value;
                sawFalse = sawFalse || !value;
            }
        }

        EXPECT_TRUE(sawTrue);
        EXPECT_TRUE(sawFalse);
    }

    TEST(DirectPublisherTests, StreamsSineWaveDouble)
    {
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        pubConfig.autoFlushThread = false;

        auto publisher = CreateDirectPublisher(pubConfig);

        std::vector<double> observed;
        std::mutex observedMutex;

        ASSERT_TRUE(publisher->Start());

        SmartDashboardClientConfig dashboardConfig;
        dashboardConfig.publisher = pubConfig;
        dashboardConfig.subscriber = subConfig;
        dashboardConfig.enableSubscriber = true;
        dashboardConfig.enableCommandSubscriber = true;

        SmartDashboardClient dashboard(dashboardConfig);
        ASSERT_TRUE(dashboard.Start());

        auto streamToken = dashboard.SubscribeDouble("Test/DoubleSine", [&observed, &observedMutex](double value)
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            observed.push_back(value);
        });
        ASSERT_TRUE(static_cast<bool>(streamToken));

        const double defaultAmplitudeMin = -1.0;
        const double defaultAmplitudeMax = 1.0;
        const double defaultSweepSeconds = 3.0;
        const double defaultSampleRateMs = 16.0;
        double configuredAmplitudeMin = defaultAmplitudeMin;
        double configuredAmplitudeMax = defaultAmplitudeMax;
        double configuredSweepSeconds = defaultSweepSeconds;
        double configuredSampleRateMs = defaultSampleRateMs;
        std::mutex configMutex;

        // Give subscriber cache a moment to receive any existing config values.
        WaitUntil([&dashboard]()
        {
            double ignored = 0.0;
            return dashboard.TryGetDouble("Test/DoubleSine/Config/AmplitudeMin", ignored)
                || dashboard.TryGetDouble("Test/DoubleSine/Config/AmplitudeMax", ignored)
                || dashboard.TryGetDouble("Test/DoubleSine/Config/SweepSeconds", ignored)
                || dashboard.TryGetDouble("Test/DoubleSine/Config/SampleRateMs", ignored);
        }, 150ms);

        auto readOrSeedDouble = [&dashboard](std::string_view key, double defaultValue, bool& seededDefault)
        {
            double value = 0.0;
            if (dashboard.TryGetDouble(key, value))
            {
                seededDefault = false;
                return value;
            }

            dashboard.PutDouble(key, defaultValue);
            dashboard.FlushNow();
            seededDefault = true;
            return defaultValue;
        };

        bool seededAmplitudeMin = false;
        bool seededAmplitudeMax = false;
        bool seededSweepSeconds = false;
        bool seededSampleRateMs = false;
        configuredAmplitudeMin = readOrSeedDouble("Test/DoubleSine/Config/AmplitudeMin", defaultAmplitudeMin, seededAmplitudeMin);
        configuredAmplitudeMax = readOrSeedDouble("Test/DoubleSine/Config/AmplitudeMax", defaultAmplitudeMax, seededAmplitudeMax);
        configuredSweepSeconds = readOrSeedDouble("Test/DoubleSine/Config/SweepSeconds", defaultSweepSeconds, seededSweepSeconds);
        configuredSampleRateMs = readOrSeedDouble("Test/DoubleSine/Config/SampleRateMs", defaultSampleRateMs, seededSampleRateMs);

        auto minCommandToken = dashboard.SubscribeDoubleCommand("Test/DoubleSine/Config/AmplitudeMin", [&configMutex, &configuredAmplitudeMin](double value)
        {
            std::lock_guard<std::mutex> lock(configMutex);
            configuredAmplitudeMin = value;
        });
        auto maxCommandToken = dashboard.SubscribeDoubleCommand("Test/DoubleSine/Config/AmplitudeMax", [&configMutex, &configuredAmplitudeMax](double value)
        {
            std::lock_guard<std::mutex> lock(configMutex);
            configuredAmplitudeMax = value;
        });
        auto sweepCommandToken = dashboard.SubscribeDoubleCommand("Test/DoubleSine/Config/SweepSeconds", [&configMutex, &configuredSweepSeconds](double value)
        {
            std::lock_guard<std::mutex> lock(configMutex);
            configuredSweepSeconds = value;
        });
        auto sampleRateCommandToken = dashboard.SubscribeDoubleCommand("Test/DoubleSine/Config/SampleRateMs", [&configMutex, &configuredSampleRateMs](double value)
        {
            std::lock_guard<std::mutex> lock(configMutex);
            configuredSampleRateMs = value;
        });
        ASSERT_TRUE(static_cast<bool>(minCommandToken));
        ASSERT_TRUE(static_cast<bool>(maxCommandToken));
        ASSERT_TRUE(static_cast<bool>(sweepCommandToken));
        ASSERT_TRUE(static_cast<bool>(sampleRateCommandToken));

        if (seededAmplitudeMin)
        {
            EXPECT_DOUBLE_EQ(configuredAmplitudeMin, defaultAmplitudeMin);
        }
        if (seededAmplitudeMax)
        {
            EXPECT_DOUBLE_EQ(configuredAmplitudeMax, defaultAmplitudeMax);
        }
        if (seededSweepSeconds)
        {
            EXPECT_DOUBLE_EQ(configuredSweepSeconds, defaultSweepSeconds);
        }
        if (seededSampleRateMs)
        {
            EXPECT_DOUBLE_EQ(configuredSampleRateMs, defaultSampleRateMs);
        }

        // Always publish config values at test start so a fresh dashboard session
        // sees all config keys and can auto-create their widgets.
        publisher->PublishDouble("Test/DoubleSine/Config/AmplitudeMin", configuredAmplitudeMin);
        publisher->PublishDouble("Test/DoubleSine/Config/AmplitudeMax", configuredAmplitudeMax);
        publisher->PublishDouble("Test/DoubleSine/Config/SweepSeconds", configuredSweepSeconds);
        publisher->PublishDouble("Test/DoubleSine/Config/SampleRateMs", configuredSampleRateMs);
        publisher->FlushNow();

        const auto runStart = std::chrono::steady_clock::now();
        const auto maxCapDuration = 30s;
        while (true)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - runStart);

            double minValue = 0.0;
            double maxValue = 0.0;
            double sweepSeconds = 0.0;
            double sampleRateMs = 0.0;
            {
                std::lock_guard<std::mutex> lock(configMutex);
                minValue = configuredAmplitudeMin;
                maxValue = configuredAmplitudeMax;
                sweepSeconds = std::max(0.5, configuredSweepSeconds);
                sampleRateMs = std::clamp(configuredSampleRateMs, 1.0, 1000.0);
            }

            const auto targetDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::duration<double>(sweepSeconds)
            );

            // Republish config keys during the stream so newly opened dashboards
            // still discover all config widgets.
            publisher->PublishDouble("Test/DoubleSine/Config/AmplitudeMin", minValue);
            publisher->PublishDouble("Test/DoubleSine/Config/AmplitudeMax", maxValue);
            publisher->PublishDouble("Test/DoubleSine/Config/SweepSeconds", sweepSeconds);
            publisher->PublishDouble("Test/DoubleSine/Config/SampleRateMs", sampleRateMs);

            // Sweep phase from -pi to +pi over current configured duration, then map to configured amplitude range.
            const double progress = std::clamp(
                static_cast<double>(elapsed.count()) / std::max(1.0, static_cast<double>(targetDuration.count())),
                0.0,
                1.0
            );
            const double phase = -std::numbers::pi + (2.0 * std::numbers::pi * progress);
            const double normalized = (std::sin(phase) + 1.0) * 0.5;
            const double amplitudeSpan = maxValue - minValue;
            const double sineValue = minValue + (normalized * amplitudeSpan);

            publisher->PublishDouble("Test/DoubleSine", sineValue);
            publisher->FlushNow();

            if (elapsed >= targetDuration || elapsed >= maxCapDuration)
            {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(std::lround(sampleRateMs))));
        }

        ASSERT_TRUE(WaitUntil(
            [&observed, &observedMutex]()
            {
                std::lock_guard<std::mutex> lock(observedMutex);
                return observed.size() >= 50;
            },
            2s
        ));

        publisher->Stop();
        dashboard.Stop();

        double minValue = std::numeric_limits<double>::max();
        double maxValue = std::numeric_limits<double>::lowest();
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            for (const double value : observed)
            {
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
            }
        }

        EXPECT_LE(minValue, -0.8);
        EXPECT_GE(maxValue, 0.8);
    }

    TEST(DirectPublisherTests, StreamsRotatingStatusStrings)
    {
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        pubConfig.autoFlushThread = false;

        auto subscriber = CreateDirectSubscriber(subConfig);
        auto publisher = CreateDirectPublisher(pubConfig);

        std::vector<std::string> observed;
        std::mutex observedMutex;

        ASSERT_TRUE(subscriber->Start(
            [&observed, &observedMutex](const VariableUpdate& update)
            {
                if (update.key != "Test/Status" || update.type != ValueType::String)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(observedMutex);
                observed.push_back(update.value.stringValue);
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(publisher->Start());

        const std::array<std::string, 4> statuses {
            "Booting",
            "Calibrating",
            "Auto Tracking",
            "Teleop Sprint"
        };
        std::size_t statusIndex = 0;

        PublishForDuration(*publisher, 3s, 120ms, [&publisher, &statuses, &statusIndex](std::chrono::milliseconds, std::chrono::milliseconds)
        {
            // Rotate through sample status text values.
            publisher->PublishString("Test/Status", statuses[statusIndex]);
            statusIndex = (statusIndex + 1) % statuses.size();
        });

        ASSERT_TRUE(WaitUntil(
            [&observed, &observedMutex]()
            {
                std::lock_guard<std::mutex> lock(observedMutex);
                return observed.size() >= 10;
            },
            2s
        ));

        publisher->Stop();
        subscriber->Stop();

        std::set<std::string> distinct;
        {
            std::lock_guard<std::mutex> lock(observedMutex);
            distinct.insert(observed.begin(), observed.end());
        }

        // Direct transport keeps latest values per key between flushes,
        // so under timing variability we assert a realistic minimum.
        EXPECT_GE(distinct.size(), 2U);
    }

    TEST(DirectPublisherTests, StreamsStringChooserTopics)
    {
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        pubConfig.autoFlushThread = false;

        auto subscriber = CreateDirectSubscriber(subConfig);
        auto publisher = CreateDirectPublisher(pubConfig);

        struct ChooserSnapshot
        {
            std::string type;
            std::string options;
            std::string active;
            std::string selected;
        };

        ChooserSnapshot snapshot;
        std::mutex snapshotMutex;

        ASSERT_TRUE(subscriber->Start(
            [&snapshot, &snapshotMutex](const VariableUpdate& update)
            {
                if (update.type != ValueType::String)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(snapshotMutex);
                if (update.key == "Test/AutoChooser/.type")
                {
                    snapshot.type = update.value.stringValue;
                }
                else if (update.key == "Test/AutoChooser/options")
                {
                    snapshot.options = update.value.stringValue;
                }
                else if (update.key == "Test/AutoChooser/active")
                {
                    snapshot.active = update.value.stringValue;
                }
                else if (update.key == "Test/AutoChooser/selected")
                {
                    snapshot.selected = update.value.stringValue;
                }
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(publisher->Start());

        const auto chooserObserved = [&snapshot, &snapshotMutex]()
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            return
                snapshot.type == "String Chooser"
                && snapshot.options == "DoNothing,Taxi,TwoPiece"
                && snapshot.active == "DoNothing"
                && snapshot.selected == "Taxi";
        };

        const auto publishChooserSnapshot = [&publisher]()
        {
            publisher->PublishString("Test/AutoChooser/.type", "String Chooser");
            publisher->PublishString("Test/AutoChooser/options", "DoNothing,Taxi,TwoPiece");
            publisher->PublishString("Test/AutoChooser/default", "DoNothing");
            publisher->PublishString("Test/AutoChooser/active", "DoNothing");
            publisher->PublishString("Test/AutoChooser/selected", "Taxi");
            publisher->FlushNow();
        };

        ASSERT_TRUE(WaitUntil(
            [&publishChooserSnapshot, &chooserObserved]()
            {
                publishChooserSnapshot();
                return chooserObserved();
            },
            2s
        ));

        publisher->Stop();
        subscriber->Stop();

        std::lock_guard<std::mutex> lock(snapshotMutex);
        EXPECT_EQ(snapshot.type, "String Chooser");
        EXPECT_EQ(snapshot.options, "DoNothing,Taxi,TwoPiece");
        EXPECT_EQ(snapshot.active, "DoNothing");
        EXPECT_EQ(snapshot.selected, "Taxi");
    }

    TEST(DirectPublisherTests, StreamsStringArrayChooserOptions)
    {
        const TestChannels channels = MakeTestChannels();

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        pubConfig.autoFlushThread = false;

        auto subscriber = CreateDirectSubscriber(subConfig);
        auto publisher = CreateDirectPublisher(pubConfig);

        struct ChooserSnapshot
        {
            std::string type;
            std::vector<std::string> options;
            std::string active;
            std::string selected;
        };

        ChooserSnapshot snapshot;
        std::mutex snapshotMutex;

        ASSERT_TRUE(subscriber->Start(
            [&snapshot, &snapshotMutex](const VariableUpdate& update)
            {
                std::lock_guard<std::mutex> lock(snapshotMutex);
                if (update.key == "Test/AutoChooser/.type" && update.type == ValueType::String)
                {
                    snapshot.type = update.value.stringValue;
                }
                else if (update.key == "Test/AutoChooser/options" && update.type == ValueType::StringArray)
                {
                    snapshot.options = update.value.stringArrayValue;
                }
                else if (update.key == "Test/AutoChooser/active" && update.type == ValueType::String)
                {
                    snapshot.active = update.value.stringValue;
                }
                else if (update.key == "Test/AutoChooser/selected" && update.type == ValueType::String)
                {
                    snapshot.selected = update.value.stringValue;
                }
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(publisher->Start());

        const auto chooserObserved = [&snapshot, &snapshotMutex]()
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            return snapshot.type == "String Chooser"
                && snapshot.options.size() == 3
                && snapshot.options[0] == "DoNothing"
                && snapshot.options[1] == "Taxi"
                && snapshot.options[2] == "TwoPiece"
                && snapshot.active == "DoNothing"
                && snapshot.selected == "Taxi";
        };

        const auto publishChooserSnapshot = [&publisher]()
        {
            publisher->PublishString("Test/AutoChooser/.type", "String Chooser");
            publisher->PublishStringArray("Test/AutoChooser/options", {"DoNothing", "Taxi", "TwoPiece"});
            publisher->PublishString("Test/AutoChooser/default", "DoNothing");
            publisher->PublishString("Test/AutoChooser/active", "DoNothing");
            publisher->PublishString("Test/AutoChooser/selected", "Taxi");
            publisher->FlushNow();
        };

        ASSERT_TRUE(WaitUntil(
            [&publishChooserSnapshot, &chooserObserved]()
            {
                publishChooserSnapshot();
                return chooserObserved();
            },
            2s
        ));

        publisher->Stop();
        subscriber->Stop();

        std::lock_guard<std::mutex> lock(snapshotMutex);
        EXPECT_EQ(snapshot.type, "String Chooser");
        ASSERT_EQ(snapshot.options.size(), 3u);
        EXPECT_EQ(snapshot.options[0], "DoNothing");
        EXPECT_EQ(snapshot.options[1], "Taxi");
        EXPECT_EQ(snapshot.options[2], "TwoPiece");
        EXPECT_EQ(snapshot.active, "DoNothing");
        EXPECT_EQ(snapshot.selected, "Taxi");
    }

    TEST(DirectPublisherTests, LateJoiningSubscriberReceivesRetainedCommandReplay)
    {
        const TestChannels channels = MakeTestChannels();

        PublisherConfig pubConfig;
        pubConfig.mappingName = channels.mappingName;
        pubConfig.dataEventName = channels.dataEventName;
        pubConfig.heartbeatEventName = channels.heartbeatEventName;
        pubConfig.autoFlushThread = true;

        auto publisher = CreateDirectPublisher(pubConfig);
        ASSERT_TRUE(publisher->Start());

        publisher->PublishDouble("TestMove", 3.5);
        ASSERT_TRUE(publisher->FlushNow());

        SubscriberConfig subConfig;
        subConfig.mappingName = channels.mappingName;
        subConfig.dataEventName = channels.dataEventName;
        subConfig.heartbeatEventName = channels.heartbeatEventName;

        std::atomic<double> observed {0.0};
        auto subscriber = CreateDirectSubscriber(subConfig);
        ASSERT_TRUE(subscriber->Start(
            [&observed](const VariableUpdate& update)
            {
                if (update.key == "TestMove" && update.type == ValueType::Double)
                {
                    observed.store(update.value.doubleValue);
                }
            },
            [](ConnectionState)
            {
            }
        ));

        ASSERT_TRUE(WaitUntil(
            [&observed]()
            {
                return observed.load() > 1.0;
            },
            2s
        ));
        EXPECT_DOUBLE_EQ(observed.load(), 3.5);

        subscriber->Stop();
        publisher->Stop();
    }
}
