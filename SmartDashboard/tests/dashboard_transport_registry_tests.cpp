#include "transport/dashboard_transport.h"

#include "native_link_ipc_test_server.h"

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
    config.pluginSettingsJson = QString::fromStdString(std::string("{\"channel_id\":\"") + channelId + "\"}");

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    const bool started = transport->Start(
        [](const sd::transport::VariableUpdate&) {},
        [](sd::transport::ConnectionState) {}
    );

    ASSERT_TRUE(started);
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
