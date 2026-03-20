#include "transport/dashboard_transport.h"

#include "native_link_ipc_test_server.h"
#include "native_link_tcp_test_server.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <chrono>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
    QCoreApplication* EnsureCoreApp()
    {
        if (QCoreApplication::instance() != nullptr)
        {
            return QCoreApplication::instance();
        }

        static int argc = 1;
        static char appName[] = "SmartDashboardTests";
        static char* argv[] = { appName };
        static std::unique_ptr<QCoreApplication> app = std::make_unique<QCoreApplication>(argc, argv);
        return app.get();
    }

    bool WaitForCondition(const std::function<bool(void)>& predicate, int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            QCoreApplication::processEvents();
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QCoreApplication::processEvents();
        return predicate();
    }

    std::string MakeUniqueChannel(const char* baseName)
    {
        std::ostringstream builder;
        builder << baseName << "-" << static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count());
        return builder.str();
    }

}

TEST(DashboardTransportRegistryTests, NativeLinkPluginIsDiscoverableWhenPresentNextToApp)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    const QString appDir = QCoreApplication::applicationDirPath();
    ASSERT_FALSE(appDir.trimmed().isEmpty());
    EXPECT_TRUE(QFileInfo::exists(appDir + "/SmartDashboardTransport_NativeLink.dll"));

    sd::transport::DashboardTransportRegistry registry;
    const sd::transport::TransportDescriptor* descriptor = registry.FindTransport("native-link");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->displayName, QString("Native Link"));
    EXPECT_EQ(descriptor->kind, sd::transport::TransportKind::Plugin);
    EXPECT_TRUE(descriptor->GetBoolProperty(QString::fromUtf8(sd::transport::kTransportPropertySupportsMultiClient), false));
    EXPECT_TRUE(descriptor->GetBoolProperty(QString::fromUtf8(sd::transport::kTransportPropertySupportsChooser), false));
}

TEST(DashboardTransportRegistryTests, NativeLinkPluginTransportStartsAndPublishesInitialState)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    const std::string channelId = MakeUniqueChannel("native-link-registry-test");
    sd::nativelink::testsupport::NativeLinkIpcTestServer server(channelId);
    ASSERT_TRUE(server.Start());
    server.RegisterDefaultDashboardTopics();

    sd::transport::DashboardTransportRegistry registry;

    sd::transport::ConnectionConfig config;
    config.kind = sd::transport::TransportKind::Plugin;
    config.transportId = "native-link";
    config.ntClientName = "RegistrySmokeTest";
    config.pluginSettingsJson = QString::fromStdString(std::string("{\"carrier\":\"shm\",\"channel_id\":\"") + channelId + "\"}");

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    std::mutex mutex;
    std::vector<sd::transport::ConnectionState> states;
    std::vector<sd::transport::VariableUpdate> updates;

    const bool started = transport->Start(
        [&mutex, &updates](const sd::transport::VariableUpdate& update)
        {
            std::lock_guard<std::mutex> lock(mutex);
            updates.push_back(update);
        },
        [&mutex, &states](sd::transport::ConnectionState state)
        {
            std::lock_guard<std::mutex> lock(mutex);
            states.push_back(state);
        }
    );

    ASSERT_TRUE(started);
    ASSERT_TRUE(WaitForCondition([&mutex, &states, &updates]()
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool connected = false;
        bool sawRetainedState = false;
        for (sd::transport::ConnectionState state : states)
        {
            if (state == sd::transport::ConnectionState::Connected)
            {
                connected = true;
            }
        }
        for (const sd::transport::VariableUpdate& update : updates)
        {
            if (update.key == "TestMove" || update.key == "Test/Auton_Selection/AutoChooser/selected")
            {
                sawRetainedState = true;
                break;
            }
        }
        return connected && sawRetainedState;
    }, 2000));

    ASSERT_TRUE(server.PublishDouble("Timer", 12.5));

    EXPECT_TRUE(transport->PublishString("Test/Auton_Selection/AutoChooser/selected", "Move Forward"));
    EXPECT_TRUE(transport->PublishDouble("TestMove", 3.5));

    sd::nativelink::TopicValue latestSelection;
    sd::nativelink::TopicValue latestMove;
    ASSERT_TRUE(WaitForCondition([&server, &latestSelection, &latestMove]()
    {
        return server.TryGetLatestValue("Test/Auton_Selection/AutoChooser/selected", latestSelection)
            && latestSelection.type == sd::nativelink::ValueType::String
            && latestSelection.stringValue == "Move Forward"
            && server.TryGetLatestValue("TestMove", latestMove)
            && latestMove.type == sd::nativelink::ValueType::Double
            && latestMove.doubleValue == 3.5;
    }, 2000));

    transport->Stop();
    server.Stop();
}

TEST(DashboardTransportRegistryTests, NativeLinkPluginTcpTransportFailsWithoutTcpAuthority)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    const std::string channelId = MakeUniqueChannel("native-link-registry-missing-tcp-authority");
    const std::uint16_t port = 5899;

    sd::transport::DashboardTransportRegistry registry;

    sd::transport::ConnectionConfig config;
    config.kind = sd::transport::TransportKind::Plugin;
    config.transportId = "native-link";
    config.ntClientName = "RegistryMissingTcpAuthority";
    config.pluginSettingsJson = QString::fromStdString(
        std::string("{\"carrier\":\"tcp\",\"host\":\"127.0.0.1\",\"port\":")
        + std::to_string(port)
        + std::string(",\"channel_id\":\"")
        + channelId
        + "\"}"
    );

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    const bool started = transport->Start(
        [](const sd::transport::VariableUpdate&)
        {
        },
        [](sd::transport::ConnectionState)
        {
        }
    );

    EXPECT_FALSE(started);

    transport->Stop();
}

TEST(DashboardTransportRegistryTests, NativeLinkPluginTcpTransportStartsAndPublishesInitialState)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    const std::string channelId = MakeUniqueChannel("native-link-registry-tcp-test");
    const std::uint16_t port = 5813;
    sd::nativelink::testsupport::NativeLinkTcpTestServer server(channelId, port);
    ASSERT_TRUE(server.Start());
    server.RegisterDefaultDashboardTopics();

    sd::transport::DashboardTransportRegistry registry;

    sd::transport::ConnectionConfig config;
    config.kind = sd::transport::TransportKind::Plugin;
    config.transportId = "native-link";
    config.ntClientName = "RegistryTcpSmokeTest";
    config.pluginSettingsJson = QString::fromStdString(
        std::string("{\"carrier\":\"tcp\",\"host\":\"127.0.0.1\",\"port\":")
        + std::to_string(port)
        + std::string(",\"channel_id\":\"")
        + channelId
        + "\"}"
    );

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    std::mutex mutex;
    std::vector<sd::transport::ConnectionState> states;
    std::vector<sd::transport::VariableUpdate> updates;

    const bool started = transport->Start(
        [&mutex, &updates](const sd::transport::VariableUpdate& update)
        {
            std::lock_guard<std::mutex> lock(mutex);
            updates.push_back(update);
        },
        [&mutex, &states](sd::transport::ConnectionState state)
        {
            std::lock_guard<std::mutex> lock(mutex);
            states.push_back(state);
        }
    );

    ASSERT_TRUE(started);
    ASSERT_TRUE(WaitForCondition([&mutex, &states, &updates]()
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool connected = false;
        bool sawRetainedState = false;
        for (sd::transport::ConnectionState state : states)
        {
            if (state == sd::transport::ConnectionState::Connected)
            {
                connected = true;
            }
        }
        for (const sd::transport::VariableUpdate& update : updates)
        {
            if (update.key == "TestMove" || update.key == "Test/Auton_Selection/AutoChooser/selected")
            {
                sawRetainedState = true;
                break;
            }
        }
        return connected && sawRetainedState;
    }, 2000));

    EXPECT_TRUE(transport->PublishDouble("TestMove", 9.0));

    sd::nativelink::TopicValue latestMove;
    ASSERT_TRUE(WaitForCondition([&server, &latestMove]()
    {
        return server.TryGetLatestValue("TestMove", latestMove)
            && latestMove.type == sd::nativelink::ValueType::Double
            && latestMove.doubleValue == 9.0;
    }, 2000));

    transport->Stop();
    server.Stop();
}

TEST(DashboardTransportRegistryTests, NativeLinkDefaultsToTcpCarrierWhenCarrierIsOmitted)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    const std::string channelId = MakeUniqueChannel("native-link-registry-default-tcp-test");
    const std::uint16_t port = 5814;
    sd::nativelink::testsupport::NativeLinkTcpTestServer server(channelId, port);
    ASSERT_TRUE(server.Start());
    server.RegisterDefaultDashboardTopics();

    sd::transport::DashboardTransportRegistry registry;

    sd::transport::ConnectionConfig config;
    config.kind = sd::transport::TransportKind::Plugin;
    config.transportId = "native-link";
    config.ntClientName = "RegistryDefaultTcpTest";
    config.pluginSettingsJson = QString::fromStdString(
        std::string("{\"host\":\"127.0.0.1\",\"port\":")
        + std::to_string(port)
        + std::string(",\"channel_id\":\"")
        + channelId
        + "\"}"
    );

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    std::mutex mutex;
    std::vector<sd::transport::VariableUpdate> updates;

    const bool started = transport->Start(
        [&mutex, &updates](const sd::transport::VariableUpdate& update)
        {
            std::lock_guard<std::mutex> lock(mutex);
            updates.push_back(update);
        },
        [](sd::transport::ConnectionState)
        {
        }
    );

    ASSERT_TRUE(started);
    ASSERT_TRUE(WaitForCondition([&mutex, &updates]()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const sd::transport::VariableUpdate& update : updates)
        {
            if (update.key == "TestMove")
            {
                return true;
            }
        }
        return false;
    }, 2000));

    transport->Stop();
    server.Stop();
}

TEST(DashboardTransportRegistryTests, NativeLinkTcpTransportStartReturnsPromptlyWithoutAuthority)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    sd::transport::DashboardTransportRegistry registry;

    sd::transport::ConnectionConfig config;
    config.kind = sd::transport::TransportKind::Plugin;
    config.transportId = "native-link";
    config.ntClientName = "RegistryNonBlockingStart";
    config.pluginSettingsJson = QString::fromStdString(
        std::string("{\"carrier\":\"tcp\",\"host\":\"127.0.0.1\",\"port\":5898,\"channel_id\":\"native-link-no-server\"}")
    );

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    const auto startTime = std::chrono::steady_clock::now();
    const bool started = transport->Start(
        [](const sd::transport::VariableUpdate&)
        {
        },
        [](sd::transport::ConnectionState)
        {
        }
    );
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime
    ).count();

    EXPECT_TRUE(started);
    EXPECT_LT(elapsedMs, 500);

    transport->Stop();
}

TEST(DashboardTransportRegistryTests, NativeLinkTcpUsesDirectHostField)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    const std::string channelId = MakeUniqueChannel("native-link-registry-direct-host-test");
    const std::uint16_t port = 5815;
    sd::nativelink::testsupport::NativeLinkTcpTestServer server(channelId, port, "127.0.0.1");
    ASSERT_TRUE(server.Start());
    server.RegisterDefaultDashboardTopics();

    sd::transport::DashboardTransportRegistry registry;

    sd::transport::ConnectionConfig config;
    config.kind = sd::transport::TransportKind::Plugin;
    config.transportId = "native-link";
    config.ntClientName = "RegistryDirectHostTcpTest";
    config.ntUseTeam = false;
    config.ntHost = "127.0.0.1";
    config.pluginSettingsJson = QString::fromStdString(
        std::string("{\"carrier\":\"tcp\",\"port\":")
        + std::to_string(port)
        + std::string(",\"channel_id\":\"")
        + channelId
        + "\"}"
    );

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    const bool started = transport->Start(
        [](const sd::transport::VariableUpdate&)
        {
        },
        [](sd::transport::ConnectionState)
        {
        }
    );

    EXPECT_TRUE(started);

    transport->Stop();
    server.Stop();
}
