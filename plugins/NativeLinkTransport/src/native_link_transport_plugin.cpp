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
            SD_TRANSPORT_FIELD_USE_TEAM_NUMBER,
            "Use team number",
            SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL,
            "Use FRC team-number resolution instead of a direct host/IP for TCP Native Link.",
            1,
            0,
            nullptr,
            0,
            1
        },
        {
            SD_TRANSPORT_FIELD_TEAM_NUMBER,
            "Team number",
            SD_TRANSPORT_CONNECTION_FIELD_TYPE_INT,
            "Used when team-number resolution is enabled.",
            0,
            0,
            nullptr,
            0,
            99999
        },
        {
            SD_TRANSPORT_FIELD_HOST,
            "Host / IP",
            SD_TRANSPORT_CONNECTION_FIELD_TYPE_STRING,
            "Used when connecting directly by host name or IP address for TCP Native Link.",
            0,
            0,
            "127.0.0.1",
            0,
            0
        },
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
        },
        {
            "auto_connect",
            "Auto-connect",
            SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL,
            "When enabled the client retries automatically after each failed connect attempt. "
            "Disable for offline/development workflows where no authority is running.",
            1,
            0,
            nullptr,
            0,
            1
        }
    };

    struct NativeLinkPluginInstance
    {
        bool running = false;
        std::string clientName = "SmartDashboardApp";
        std::string channelId = "native-link-default";
        std::string carrierName = "shm";
        std::string host = "127.0.0.1";
        int teamNumber = 0;
        bool useTeamNumber = true;
        std::unique_ptr<sd::nativelink::NativeLinkCarrierClient> client;
        sd_transport_callbacks_v1 callbacks {};
    };

    std::string ResolveTcpHost(const NativeLinkPluginInstance& instance)
    {
        if (instance.useTeamNumber && instance.teamNumber > 0)
        {
            const int hi = instance.teamNumber / 100;
            const int lo = instance.teamNumber % 100;

            // Ian: Teams already understand the Legacy NT dialog shape. Reusing
            // the same team-number host resolution here keeps Native Link's TCP
            // runtime path familiar without exposing carrier internals in the UI.
            return "10." + std::to_string(hi) + "." + std::to_string(lo) + ".2";
        }

        if (!instance.host.empty())
        {
            return instance.host;
        }

        return "127.0.0.1";
    }

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

    bool ReadPluginBoolSetting(const char* jsonText, const char* key, bool fallback)
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

        // Skip whitespace after the colon.
        const std::size_t valueStart = json.find_first_not_of(" \t\r\n", colonPos + 1);
        if (valueStart == std::string::npos)
        {
            return fallback;
        }

        // Ian: JSON booleans are lowercase "true" or "false". Accept both and
        // return the fallback for anything else so an operator who fat-fingers
        // the JSON doesn't silently disable auto-connect.
        if (json.compare(valueStart, 4, "true") == 0)
        {
            return true;
        }
        if (json.compare(valueStart, 5, "false") == 0)
        {
            return false;
        }

        return fallback;
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
        instance->host = (config != nullptr && config->nt_host != nullptr && config->nt_host[0] != '\0')
            ? config->nt_host
            : "127.0.0.1";
        instance->teamNumber = config != nullptr ? config->nt_team : 0;
        instance->useTeamNumber = config != nullptr ? (config->nt_use_team != 0) : true;
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
            ResolveTcpHost(*instance)
        );
        clientConfig.port = ReadPluginPortSetting(
            config != nullptr ? config->plugin_settings_json : nullptr,
            "port",
            5810
        );
        // Ian: auto_connect defaults true so existing registry entries that
        // predate this field get the same reconnecting behaviour as before.
        // Operators who want a one-shot connect (offline dev, no authority
        // running) set {"auto_connect":false} in plugin_settings_json; the
        // client then parks in Disconnected after the first failed attempt and
        // only redials when the user clicks Connect (which calls Stop+Start).
        clientConfig.autoConnect = ReadPluginBoolSetting(
            config != nullptr ? config->plugin_settings_json : nullptr,
            "auto_connect",
            true
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

        // Ian: Native Link must behave like a normal networked transport at the
        // UX boundary. Start the carrier and return promptly so the UI can stay
        // responsive while TCP connects/reconnects in the background, then use
        // connection-state callbacks to represent progress.
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

        instance->running = true;
        return 1;
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
        SD_TRANSPORT_PLUGIN_FLAG_USE_SHORT_DISPLAY_KEYS,
        &GetNativeLinkBoolProperty,
        &GetNativeLinkConnectionFields,
        &kNativeLinkApi
    };
}

extern "C" SD_TRANSPORT_PLUGIN_EXPORT const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void)
{
    return &kNativeLinkDescriptor;
}
