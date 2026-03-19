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
}
