#define SD_TRANSPORT_PLUGIN_EXPORTS 1

#include "transport/dashboard_transport_plugin_api.h"

#include <gtest/gtest.h>

#include <string>

extern "C" const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void);

namespace
{

// ────────────────────────────────────────────────────────────────────────────
// Descriptor / ABI contract tests
// ────────────────────────────────────────────────────────────────────────────

TEST(ShuffleboardPluginTests, DescriptorIsNotNull)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
}

TEST(ShuffleboardPluginTests, DescriptorHasCorrectIdentity)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->plugin_id, nullptr);
    EXPECT_STREQ(descriptor->plugin_id, "shuffleboard");
    ASSERT_NE(descriptor->display_name, nullptr);
    EXPECT_STREQ(descriptor->display_name, "Shuffleboard (NT4)");
}

TEST(ShuffleboardPluginTests, DescriptorReportsCorrectApiVersion)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->plugin_api_version, SD_TRANSPORT_PLUGIN_API_VERSION_1);
    EXPECT_EQ(descriptor->struct_size, sizeof(sd_transport_plugin_descriptor_v1));
}

TEST(ShuffleboardPluginTests, DescriptorHasTransportApi)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->transport_api, nullptr);
    EXPECT_NE(descriptor->transport_api->create, nullptr);
    EXPECT_NE(descriptor->transport_api->destroy, nullptr);
    EXPECT_NE(descriptor->transport_api->start, nullptr);
    EXPECT_NE(descriptor->transport_api->stop, nullptr);
    EXPECT_NE(descriptor->transport_api->publish_bool, nullptr);
    EXPECT_NE(descriptor->transport_api->publish_double, nullptr);
    EXPECT_NE(descriptor->transport_api->publish_string, nullptr);
}

TEST(ShuffleboardPluginTests, DescriptorUsesShortDisplayKeys)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    EXPECT_NE(descriptor->flags & SD_TRANSPORT_PLUGIN_FLAG_USE_SHORT_DISPLAY_KEYS, 0u);
}

TEST(ShuffleboardPluginTests, DescriptorAdvertisesMultiClient)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->get_bool_property, nullptr);
    EXPECT_NE(descriptor->get_bool_property(SD_TRANSPORT_PROPERTY_SUPPORTS_MULTI_CLIENT, 0), 0);
}

TEST(ShuffleboardPluginTests, ChooserSupportEnabled)
{
    // Ian: Chooser support is enabled now that the publish path works end-to-end.
    // The plugin sends a JSON publish claim + binary value frame to the NT4 server,
    // which updates its retained cache. The host assembles chooser sub-keys
    // (.type, /options, /default, /active, /selected) into a chooser widget and
    // routes user selections through PublishString with key + "/selected".
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->get_bool_property, nullptr);
    EXPECT_NE(descriptor->get_bool_property(SD_TRANSPORT_PROPERTY_SUPPORTS_CHOOSER, 0), 0);
}

TEST(ShuffleboardPluginTests, UnknownPropertyReturnsDefault)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->get_bool_property, nullptr);
    EXPECT_EQ(descriptor->get_bool_property("nonexistent_property", 42), 42);
}

// ────────────────────────────────────────────────────────────────────────────
// Connection field tests
// ────────────────────────────────────────────────────────────────────────────

TEST(ShuffleboardPluginTests, ConnectionFieldsArePresent)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->get_connection_fields, nullptr);

    size_t count = 0;
    const sd_transport_connection_field_descriptor_v1* fields = descriptor->get_connection_fields(&count);
    ASSERT_NE(fields, nullptr);
    ASSERT_EQ(count, 5u);
}

TEST(ShuffleboardPluginTests, ConnectionFieldIds)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    size_t count = 0;
    const sd_transport_connection_field_descriptor_v1* fields = descriptor->get_connection_fields(&count);
    ASSERT_NE(fields, nullptr);

    EXPECT_STREQ(fields[0].field_id, SD_TRANSPORT_FIELD_HOST);
    EXPECT_STREQ(fields[1].field_id, SD_TRANSPORT_FIELD_USE_TEAM_NUMBER);
    EXPECT_STREQ(fields[2].field_id, SD_TRANSPORT_FIELD_TEAM_NUMBER);
    EXPECT_STREQ(fields[3].field_id, SD_TRANSPORT_FIELD_CLIENT_NAME);
    EXPECT_STREQ(fields[4].field_id, "auto_connect");
}

TEST(ShuffleboardPluginTests, AutoConnectFieldDefaultIsTrue)
{
    // Ian: auto_connect defaults to true for the same reason as NativeLink —
    // operators who want one-shot connect must opt in via settings JSON.
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    size_t count = 0;
    const sd_transport_connection_field_descriptor_v1* fields = descriptor->get_connection_fields(&count);
    ASSERT_NE(fields, nullptr);

    const sd_transport_connection_field_descriptor_v1* autoConnectField = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        if (fields[i].field_id != nullptr && std::string(fields[i].field_id) == "auto_connect")
        {
            autoConnectField = &fields[i];
            break;
        }
    }

    ASSERT_NE(autoConnectField, nullptr) << "auto_connect field must be declared";
    EXPECT_EQ(autoConnectField->field_type, SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL);
    EXPECT_NE(autoConnectField->default_bool_value, 0)
        << "auto_connect must default to true (reconnect on)";
}

TEST(ShuffleboardPluginTests, UseTeamNumberDefaultsOff)
{
    // Ian: Unlike NativeLink (which defaults use_team_number to true for FRC
    // team use), Shuffleboard transport defaults to false because the typical
    // use case is connecting to localhost or a known IP for the simulator.
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    size_t count = 0;
    const sd_transport_connection_field_descriptor_v1* fields = descriptor->get_connection_fields(&count);
    ASSERT_NE(fields, nullptr);

    const sd_transport_connection_field_descriptor_v1* teamField = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        if (fields[i].field_id != nullptr && std::string(fields[i].field_id) == SD_TRANSPORT_FIELD_USE_TEAM_NUMBER)
        {
            teamField = &fields[i];
            break;
        }
    }

    ASSERT_NE(teamField, nullptr);
    EXPECT_EQ(teamField->default_bool_value, 0)
        << "use_team_number should default to false for Shuffleboard transport";
}

// ────────────────────────────────────────────────────────────────────────────
// Instance lifecycle tests
// ────────────────────────────────────────────────────────────────────────────

TEST(ShuffleboardPluginTests, CreateAndDestroyInstance)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    sd_transport_instance_v1 instance = descriptor->transport_api->create();
    ASSERT_NE(instance, nullptr);

    // Destroy should not crash
    descriptor->transport_api->destroy(instance);
}

TEST(ShuffleboardPluginTests, DestroyNullIsNoOp)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    // Destroying null should be a no-op, not a crash
    descriptor->transport_api->destroy(nullptr);
}

TEST(ShuffleboardPluginTests, StartWithNullCallbacksFails)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    sd_transport_instance_v1 instance = descriptor->transport_api->create();
    ASSERT_NE(instance, nullptr);

    sd_transport_connection_config_v1 config {};
    config.nt_host = "127.0.0.1";
    config.nt_client_name = "test";

    // Start with null callbacks should fail
    EXPECT_EQ(descriptor->transport_api->start(instance, &config, nullptr), 0);

    descriptor->transport_api->destroy(instance);
}

TEST(ShuffleboardPluginTests, StopWithoutStartIsNoOp)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    sd_transport_instance_v1 instance = descriptor->transport_api->create();
    ASSERT_NE(instance, nullptr);

    // Stop without ever starting should be a no-op
    descriptor->transport_api->stop(instance);

    descriptor->transport_api->destroy(instance);
}

TEST(ShuffleboardPluginTests, PublishOnUnstartedInstanceReturnsZero)
{
    const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
    ASSERT_NE(descriptor, nullptr);

    sd_transport_instance_v1 instance = descriptor->transport_api->create();
    ASSERT_NE(instance, nullptr);

    EXPECT_EQ(descriptor->transport_api->publish_bool(instance, "key", 1), 0);
    EXPECT_EQ(descriptor->transport_api->publish_double(instance, "key", 1.0), 0);
    EXPECT_EQ(descriptor->transport_api->publish_string(instance, "key", "value"), 0);

    descriptor->transport_api->destroy(instance);
}

} // anonymous namespace
