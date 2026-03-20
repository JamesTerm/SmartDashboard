#define SD_TRANSPORT_PLUGIN_EXPORTS 1

#include "native_link_carrier_client.h"
#include "transport/dashboard_transport_plugin_api.h"

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using sd::nativelink::CreateNativeLinkCarrierClient;
    using sd::nativelink::NativeLinkCarrierKind;
    using sd::nativelink::NativeLinkClientConfig;
    using sd::nativelink::TopicValue;
    using sd::nativelink::ToString;
    using sd::nativelink::TryParseCarrierKind;
    using sd::nativelink::UpdateEnvelope;
    using sd::nativelink::ValueType;

    const sd_transport_connection_field_descriptor_v1 kNativeLinkConnectionFields[] = {
        {
            SD_TRANSPORT_FIELD_CLIENT_NAME,
            "Client name",
            SD_TRANSPORT_CONNECTION_FIELD_TYPE_STRING,
            "Logical client name used for diagnostics and ownership tracking.",
            0,
            0,
            "SmartDashboardApp",
            0,
            0
        }
    };

    struct NativeLinkPluginInstance
    {
        bool running = false;
        std::string clientName = "SmartDashboardApp";
        std::string channelId = "native-link-default";
        std::string carrierName = "shm";
        std::unique_ptr<sd::nativelink::NativeLinkCarrierClient> client;
        sd_transport_callbacks_v1 callbacks {};
    };

    int GetNativeLinkBoolProperty(const char* propertyName, int defaultValue)
    {
        if (propertyName == nullptr)
        {
            return defaultValue;
        }

        const std::string name(propertyName);
        if (name == SD_TRANSPORT_PROPERTY_SUPPORTS_MULTI_CLIENT)
        {
            return 1;
        }
        if (name == SD_TRANSPORT_PROPERTY_SUPPORTS_CHOOSER)
        {
            return 1;
        }

        return defaultValue;
    }

    const sd_transport_connection_field_descriptor_v1* GetNativeLinkConnectionFields(size_t* outCount)
    {
        if (outCount != nullptr)
        {
            *outCount = sizeof(kNativeLinkConnectionFields) / sizeof(kNativeLinkConnectionFields[0]);
        }

        return kNativeLinkConnectionFields;
    }

    void PublishUpdateCallback(const NativeLinkPluginInstance& instance, const UpdateEnvelope& event)
    {
        if (instance.callbacks.on_variable_update == nullptr)
        {
            return;
        }

        sd_transport_value_v1 payload {};
        payload.type = 0;

        std::vector<const char*> stringArrayPointers;
        switch (event.value.type)
        {
            case ValueType::Bool:
                payload.type = SD_TRANSPORT_VALUE_TYPE_BOOL;
                payload.bool_value = event.value.boolValue ? 1 : 0;
                break;
            case ValueType::Double:
                payload.type = SD_TRANSPORT_VALUE_TYPE_DOUBLE;
                payload.double_value = event.value.doubleValue;
                break;
            case ValueType::String:
                payload.type = SD_TRANSPORT_VALUE_TYPE_STRING;
                payload.string_value = event.value.stringValue.c_str();
                break;
            case ValueType::StringArray:
                payload.type = SD_TRANSPORT_VALUE_TYPE_STRING_ARRAY;
                stringArrayPointers.reserve(event.value.stringArrayValue.size());
                for (const std::string& item : event.value.stringArrayValue)
                {
                    stringArrayPointers.push_back(item.c_str());
                }
                payload.string_array_items = stringArrayPointers.data();
                payload.string_array_count = stringArrayPointers.size();
                break;
        }

        instance.callbacks.on_variable_update(
            instance.callbacks.user_data,
            event.topicPath.c_str(),
            &payload,
            event.serverSequence
        );
    }

    std::string ReadPluginStringSetting(const char* jsonText, const char* key, const std::string& fallback)
    {
        if (jsonText == nullptr || key == nullptr)
        {
            return fallback;
        }

        const std::string json(jsonText);
        const std::string needle = std::string("\"") + key + "\"";
        const std::size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos)
        {
            return fallback;
        }

        const std::size_t colonPos = json.find(':', keyPos + needle.size());
        if (colonPos == std::string::npos)
        {
            return fallback;
        }

        const std::size_t firstQuote = json.find('"', colonPos + 1);
        if (firstQuote == std::string::npos)
        {
            return fallback;
        }

        const std::size_t secondQuote = json.find('"', firstQuote + 1);
        if (secondQuote == std::string::npos || secondQuote <= firstQuote + 1)
        {
            return fallback;
        }

        return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    }

    std::uint16_t ReadPluginPortSetting(const char* jsonText, const char* key, std::uint16_t fallback)
    {
        if (jsonText == nullptr || key == nullptr)
        {
            return fallback;
        }

        const std::string json(jsonText);
        const std::string needle = std::string("\"") + key + "\"";
        const std::size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos)
        {
            return fallback;
        }

        const std::size_t colonPos = json.find(':', keyPos + needle.size());
        if (colonPos == std::string::npos)
        {
            return fallback;
        }

        const std::size_t valueStart = json.find_first_of("0123456789", colonPos + 1);
        if (valueStart == std::string::npos)
        {
            return fallback;
        }

        const std::size_t valueEnd = json.find_first_not_of("0123456789", valueStart);
        const std::string token = json.substr(valueStart, valueEnd - valueStart);
        const unsigned long parsed = std::strtoul(token.c_str(), nullptr, 10);
        if (parsed > 65535UL)
        {
            return fallback;
        }
        return static_cast<std::uint16_t>(parsed);
    }

    NativeLinkCarrierKind ReadCarrierKindSetting(const char* jsonText)
    {
        NativeLinkCarrierKind kind = NativeLinkCarrierKind::Tcp;
        const std::string value = ReadPluginStringSetting(jsonText, "carrier", ToString(kind));
        if (!TryParseCarrierKind(value, kind))
        {
            kind = NativeLinkCarrierKind::Tcp;
        }
        return kind;
    }

    sd_transport_instance_v1 CreateNativeLinkInstance()
    {
        return new NativeLinkPluginInstance();
    }

    void DestroyNativeLinkInstance(sd_transport_instance_v1 instance)
    {
        delete static_cast<NativeLinkPluginInstance*>(instance);
    }

    int StartNativeLink(
        sd_transport_instance_v1 instanceHandle,
        const sd_transport_connection_config_v1* config,
        const sd_transport_callbacks_v1* callbacks)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr || callbacks == nullptr)
        {
            return 0;
        }

        instance->callbacks = *callbacks;
        instance->clientName = (config != nullptr && config->nt_client_name != nullptr && config->nt_client_name[0] != '\0')
            ? config->nt_client_name
            : "SmartDashboardApp";
        instance->channelId = ReadPluginStringSetting(
            config != nullptr ? config->plugin_settings_json : nullptr,
            "channel_id",
            "native-link-default"
        );
        const NativeLinkCarrierKind carrierKind = ReadCarrierKindSetting(
            config != nullptr ? config->plugin_settings_json : nullptr
        );
        instance->carrierName = ToString(carrierKind);

        NativeLinkClientConfig clientConfig;
        clientConfig.carrierKind = carrierKind;
        clientConfig.channelId = instance->channelId;
        clientConfig.clientId = instance->clientName;
        clientConfig.host = ReadPluginStringSetting(
            config != nullptr ? config->plugin_settings_json : nullptr,
            "host",
            "127.0.0.1"
        );
        clientConfig.port = ReadPluginPortSetting(
            config != nullptr ? config->plugin_settings_json : nullptr,
            "port",
            5810
        );

        // Ian: The semantic contract sits above carrier choice now. If the
        // operator selects a carrier we do not implement yet, fail loudly here
        // instead of silently falling back to SHM and hiding a selection bug.
        instance->client = CreateNativeLinkCarrierClient(clientConfig.carrierKind);
        if (instance->client == nullptr)
        {
            instance->running = false;
            return 0;
        }

        // Ian: SmartDashboard is no longer the authority here. The plugin should
        // only succeed when the simulator-owned Native Link server is actually
        // present, so startup failures now correctly signal "no authoritative
        // server available" instead of silently creating another private bridge.
        const bool started = instance->client->Start(
            clientConfig,
            [instance](const UpdateEnvelope& event)
            {
                PublishUpdateCallback(*instance, event);
            },
            [instance](int state)
            {
                if (instance->callbacks.on_connection_state != nullptr)
                {
                    instance->callbacks.on_connection_state(instance->callbacks.user_data, state);
                }
            }
        );

        if (!started)
        {
            instance->running = false;
            instance->client.reset();
            return 0;
        }

        // Ian: The plugin start contract should only report success once the real
        // IPC client has actually crossed into the authority-owned live session.
        // Returning success as soon as the mapping opens lets higher layers fire
        // remembered publishes into a transport that is still mid-handshake.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (instance->client->IsConnected())
            {
                instance->running = true;
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Ian: Real multi-process bring-up can be a little slower than the
        // focused unit harness because the second dashboard must cross the full
        // authority-owned snapshot/live handshake while the app is also opening
        // windows and restoring settings. Fail eventually, but do not turn that
        // transient startup work into a false "carrier broken" signal too early.
        instance->client->Stop();
        instance->client.reset();
        instance->running = false;
        return 0;
    }

    void StopNativeLink(sd_transport_instance_v1 instanceHandle)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr)
        {
            return;
        }

        if (instance->client != nullptr)
        {
            instance->client->Stop();
            instance->client.reset();
        }

        instance->running = false;
    }

    int PublishBool(sd_transport_instance_v1 instanceHandle, const char* key, int value)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr || instance->client == nullptr || key == nullptr)
        {
            return 0;
        }

        return instance->client->Publish(key, TopicValue::Bool(value != 0)) ? 1 : 0;
    }

    int PublishDouble(sd_transport_instance_v1 instanceHandle, const char* key, double value)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr || instance->client == nullptr || key == nullptr)
        {
            return 0;
        }

        return instance->client->Publish(key, TopicValue::Double(value)) ? 1 : 0;
    }

    int PublishString(sd_transport_instance_v1 instanceHandle, const char* key, const char* value)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr || instance->client == nullptr || key == nullptr || value == nullptr)
        {
            return 0;
        }

        return instance->client->Publish(key, TopicValue::String(value)) ? 1 : 0;
    }

    const sd_transport_api_v1 kNativeLinkApi = {
        &CreateNativeLinkInstance,
        &DestroyNativeLinkInstance,
        &StartNativeLink,
        &StopNativeLink,
        &PublishBool,
        &PublishDouble,
        &PublishString
    };

    const sd_transport_plugin_descriptor_v1 kNativeLinkDescriptor = {
        sizeof(sd_transport_plugin_descriptor_v1),
        SD_TRANSPORT_PLUGIN_API_VERSION_1,
        "native-link",
        "Native Link",
        "native-link",
        0u,
        &GetNativeLinkBoolProperty,
        &GetNativeLinkConnectionFields,
        &kNativeLinkApi
    };
}

extern "C" SD_TRANSPORT_PLUGIN_EXPORT const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void)
{
    return &kNativeLinkDescriptor;
}
