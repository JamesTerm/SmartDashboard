#define SD_TRANSPORT_PLUGIN_EXPORTS 1

#include "transport/dashboard_transport_plugin_api.h"

#include <gtest/gtest.h>

extern "C" const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void);

namespace
{
    TEST(NativeLinkPluginTests, DescriptorAdvertisesMultiClientCapability)
    {
        const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
        ASSERT_NE(descriptor, nullptr);
        ASSERT_NE(descriptor->plugin_id, nullptr);
        EXPECT_STREQ(descriptor->plugin_id, "native-link");
        ASSERT_NE(descriptor->display_name, nullptr);
        EXPECT_STREQ(descriptor->display_name, "Native Link");
        ASSERT_NE(descriptor->get_bool_property, nullptr);
        EXPECT_NE(descriptor->get_bool_property(SD_TRANSPORT_PROPERTY_SUPPORTS_MULTI_CLIENT, 0), 0);
        EXPECT_NE(descriptor->get_bool_property(SD_TRANSPORT_PROPERTY_SUPPORTS_CHOOSER, 0), 0);
    }

    TEST(NativeLinkPluginTests, DescriptorAdvertisesLegacyNtStyleConnectionFields)
    {
        const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
        ASSERT_NE(descriptor, nullptr);
        ASSERT_NE(descriptor->get_connection_fields, nullptr);

        size_t count = 0;
        const sd_transport_connection_field_descriptor_v1* fields = descriptor->get_connection_fields(&count);
        ASSERT_NE(fields, nullptr);
        ASSERT_EQ(count, 5u);

        EXPECT_STREQ(fields[0].field_id, SD_TRANSPORT_FIELD_USE_TEAM_NUMBER);
        EXPECT_STREQ(fields[1].field_id, SD_TRANSPORT_FIELD_TEAM_NUMBER);
        EXPECT_STREQ(fields[2].field_id, SD_TRANSPORT_FIELD_HOST);
        EXPECT_STREQ(fields[3].field_id, SD_TRANSPORT_FIELD_CLIENT_NAME);
        EXPECT_STREQ(fields[4].field_id, "auto_connect");
    }

    TEST(NativeLinkPluginTests, AutoConnectFieldDefaultIsTrue)
    {
        // Ian: ReadPluginBoolSetting falls back to `true` (reconnect on) when
        // "auto_connect" is absent from legacy INI entries. The descriptor must
        // declare the same default so the settings dialog pre-populates the
        // checkbox correctly and the two sources of truth agree.
        const sd_transport_plugin_descriptor_v1* descriptor = SdGetTransportPluginV1();
        ASSERT_NE(descriptor, nullptr);
        ASSERT_NE(descriptor->get_connection_fields, nullptr);

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

        ASSERT_NE(autoConnectField, nullptr) << "auto_connect field must be declared in kNativeLinkConnectionFields";
        EXPECT_EQ(autoConnectField->field_type, SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL);
        EXPECT_NE(autoConnectField->default_bool_value, 0)
            << "auto_connect must default to true (reconnect on) so legacy entries behave as before";
    }

}
