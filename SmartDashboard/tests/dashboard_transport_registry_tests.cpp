#include "transport/dashboard_transport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <chrono>

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

}

TEST(DashboardTransportRegistryTests, NativeLinkPluginIsDiscoverableWhenPresentNextToApp)
{
    ASSERT_NE(EnsureCoreApp(), nullptr);

    const QString appDir = QCoreApplication::applicationDirPath();
    ASSERT_FALSE(appDir.trimmed().isEmpty());
    EXPECT_TRUE(QDir(appDir + "/plugins").exists());
    EXPECT_TRUE(QFileInfo::exists(appDir + "/plugins/SmartDashboardTransport_NativeLink.dll"));

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

    sd::transport::DashboardTransportRegistry registry;

    sd::transport::ConnectionConfig config;
    config.kind = sd::transport::TransportKind::Plugin;
    config.transportId = "native-link";
    config.ntClientName = "RegistrySmokeTest";

    std::unique_ptr<sd::transport::IDashboardTransport> transport = registry.CreateTransport(config);
    ASSERT_NE(transport, nullptr);

    std::vector<sd::transport::VariableUpdate> updates;
    std::vector<sd::transport::ConnectionState> states;

    const bool started = transport->Start(
        [&updates](const sd::transport::VariableUpdate& update)
        {
            updates.push_back(update);
        },
        [&states](sd::transport::ConnectionState state)
        {
            states.push_back(state);
        }
    );

    ASSERT_TRUE(started);
    EXPECT_FALSE(states.empty());
    EXPECT_EQ(states.back(), sd::transport::ConnectionState::Connected);
    EXPECT_FALSE(updates.empty());

    bool sawChooserSelected = false;
    bool sawTestMove = false;
    for (const sd::transport::VariableUpdate& update : updates)
    {
        if (update.key == "Test/Auton_Selection/AutoChooser/selected")
        {
            sawChooserSelected = true;
        }
        if (update.key == "TestMove")
        {
            sawTestMove = true;
        }
    }

    EXPECT_TRUE(sawChooserSelected);
    EXPECT_TRUE(sawTestMove);

    EXPECT_TRUE(transport->PublishString("Test/Auton_Selection/AutoChooser/selected", "Move Forward"));
    EXPECT_TRUE(transport->PublishDouble("TestMove", 3.5));

    bool sawPublishedSelection = false;
    bool sawPublishedMove = false;
    for (const sd::transport::VariableUpdate& update : updates)
    {
        if (update.key == "Test/Auton_Selection/AutoChooser/selected" && update.value.toString() == "Move Forward")
        {
            sawPublishedSelection = true;
        }
        if (update.key == "TestMove" && update.value.toDouble() == 3.5)
        {
            sawPublishedMove = true;
        }
    }

    EXPECT_TRUE(sawPublishedSelection);
    EXPECT_TRUE(sawPublishedMove);

    transport->Stop();
}
