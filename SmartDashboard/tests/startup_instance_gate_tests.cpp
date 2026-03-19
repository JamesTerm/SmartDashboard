#include "app/startup_instance_gate.h"

#include <gtest/gtest.h>

namespace sd::app
{
    TEST(StartupInstanceGateTests, ParseStartupOptionsRecognizesMultiInstanceFlag)
    {
        char appName[] = "SmartDashboardApp";
        char allowFlag[] = "--allow-multi-instance";
        char* argv[] = { appName, allowFlag };

        const StartupOptions options = ParseStartupOptions(2, argv);
        EXPECT_TRUE(options.allowMultiInstance);
    }

    TEST(StartupInstanceGateTests, ParseStartupOptionsRecognizesInstanceTag)
    {
        char appName[] = "SmartDashboardApp";
        char tagFlag[] = "--instance-tag";
        char tagValue[] = "dashboard-b";
        char* argv[] = { appName, tagFlag, tagValue };

        const StartupOptions options = ParseStartupOptions(3, argv);
        EXPECT_EQ(options.instanceTag, "dashboard-b");
    }

    TEST(StartupInstanceGateTests, DetermineStartupTransportIdPrefersPersistedTransportId)
    {
        EXPECT_EQ(DetermineStartupTransportId("native-link", static_cast<int>(sd::transport::TransportKind::Direct)), "native-link");
    }

    TEST(StartupInstanceGateTests, DetermineStartupTransportIdFallsBackFromPersistedKind)
    {
        EXPECT_EQ(DetermineStartupTransportId("", static_cast<int>(sd::transport::TransportKind::Replay)), "replay");
        EXPECT_EQ(DetermineStartupTransportId("", static_cast<int>(sd::transport::TransportKind::Plugin)), "legacy-nt");
        EXPECT_EQ(DetermineStartupTransportId("", static_cast<int>(sd::transport::TransportKind::Direct)), "direct");
    }

    TEST(StartupInstanceGateTests, ShouldBypassSingleInstanceOnlyWhenFlagAndCapabilityArePresent)
    {
        StartupOptions options;
        sd::transport::TransportDescriptor descriptor;
        descriptor.boolProperties.insert_or_assign(QString::fromUtf8(sd::transport::kTransportPropertySupportsMultiClient), true);

        EXPECT_FALSE(ShouldBypassSingleInstance(options, &descriptor));

        options.allowMultiInstance = true;
        EXPECT_TRUE(ShouldBypassSingleInstance(options, &descriptor));

        descriptor.boolProperties.clear();
        EXPECT_FALSE(ShouldBypassSingleInstance(options, &descriptor));
        EXPECT_FALSE(ShouldBypassSingleInstance(options, nullptr));
    }
}
