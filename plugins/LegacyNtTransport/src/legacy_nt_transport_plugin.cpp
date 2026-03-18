#define SD_TRANSPORT_PLUGIN_EXPORTS 1

#include "transport/dashboard_transport_plugin_api.h"

#include <string_view>

namespace
{
    int GetLegacyNtBoolProperty(const char* propertyName, int defaultValue)
    {
        if (propertyName == nullptr)
        {
            return defaultValue;
        }

        const std::string_view name(propertyName);
        if (name == SD_TRANSPORT_PROPERTY_SUPPORTS_CHOOSER)
        {
            return 1;
        }
        if (name == SD_TRANSPORT_PROPERTY_SUPPORTS_MULTI_CLIENT)
        {
            return 0;
        }

        return defaultValue;
    }

    struct LegacyNtTransportStub
    {
        sd_transport_callbacks_v1 callbacks {};
        sd_transport_connection_config_v1 config {};
    };

    void* CreateLegacyNtTransport()
    {
        return new LegacyNtTransportStub();
    }

    void DestroyLegacyNtTransport(void* instance)
    {
        delete static_cast<LegacyNtTransportStub*>(instance);
    }

    int StartLegacyNtTransport(
        void* instance,
        const sd_transport_connection_config_v1* config,
        const sd_transport_callbacks_v1* callbacks
    )
    {
        auto* transport = static_cast<LegacyNtTransportStub*>(instance);
        if (transport == nullptr)
        {
            return 0;
        }

        if (config != nullptr)
        {
            transport->config = *config;
        }
        if (callbacks != nullptr)
        {
            transport->callbacks = *callbacks;
        }

        if (transport->callbacks.on_connection_state != nullptr)
        {
            transport->callbacks.on_connection_state(
                transport->callbacks.user_data,
                SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED
            );
        }

        return 1;
    }

    void StopLegacyNtTransport(void* instance)
    {
        auto* transport = static_cast<LegacyNtTransportStub*>(instance);
        if (transport == nullptr || transport->callbacks.on_connection_state == nullptr)
        {
            return;
        }

        transport->callbacks.on_connection_state(
            transport->callbacks.user_data,
            SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED
        );
    }

    int PublishLegacyNtBool(void* instance, const char* key, int value)
    {
        static_cast<void>(instance);
        static_cast<void>(key);
        static_cast<void>(value);
        return 0;
    }

    int PublishLegacyNtDouble(void* instance, const char* key, double value)
    {
        static_cast<void>(instance);
        static_cast<void>(key);
        static_cast<void>(value);
        return 0;
    }

    int PublishLegacyNtString(void* instance, const char* key, const char* value)
    {
        static_cast<void>(instance);
        static_cast<void>(key);
        static_cast<void>(value);
        return 0;
    }

    const sd_transport_api_v1 kLegacyNtTransportApi = {
        &CreateLegacyNtTransport,
        &DestroyLegacyNtTransport,
        &StartLegacyNtTransport,
        &StopLegacyNtTransport,
        &PublishLegacyNtBool,
        &PublishLegacyNtDouble,
        &PublishLegacyNtString
    };

    const sd_transport_plugin_descriptor_v1 kLegacyNtPluginDescriptor = {
        sizeof(sd_transport_plugin_descriptor_v1),
        SD_TRANSPORT_PLUGIN_API_VERSION_1,
        "legacy-nt",
        "Legacy NT",
        "legacy-nt",
        SD_TRANSPORT_PLUGIN_FLAG_SUPPORTS_RECORDING,
        &GetLegacyNtBoolProperty,
        &kLegacyNtTransportApi
    };
}

extern "C" SD_TRANSPORT_PLUGIN_EXPORT const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void)
{
    return &kLegacyNtPluginDescriptor;
}
