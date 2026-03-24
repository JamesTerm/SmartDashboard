#define SD_TRANSPORT_PLUGIN_EXPORTS 1

#include "nt4_client.h"
#include "transport/dashboard_transport_plugin_api.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

// Ian: This is the ABI bridge between the SmartDashboard host and the NT4
// WebSocket client. It follows the exact same pattern as the NativeLink
// plugin: static descriptor + C function table + opaque instance. The plugin
// connects to an NT4 server (Robot_Simulation ShuffleboardBackend or a real
// ntcore server running on a robot/coprocessor) and feeds received topic
// values into the dashboard display pipeline. Phase 1 is receive-only;
// phase 2 adds write-back for chooser selections via NT4 publish.

namespace
{

using sd::shuffleboard::ConnectionState;
using sd::shuffleboard::NT4Client;
using sd::shuffleboard::NT4ClientConfig;
using sd::shuffleboard::TopicUpdate;
using sd::shuffleboard::ValueType;

// ────────────────────────────────────────────────────────────────────────────
// Connection fields — host-rendered settings schema
// ────────────────────────────────────────────────────────────────────────────

const sd_transport_connection_field_descriptor_v1 kShuffleboardConnectionFields[] = {
    {
        SD_TRANSPORT_FIELD_HOST,
        "Host / IP",
        SD_TRANSPORT_CONNECTION_FIELD_TYPE_STRING,
        "Address of the NT4 server (robot, simulator, or coprocessor).",
        0,
        0,
        "127.0.0.1",
        0,
        0
    },
    {
        SD_TRANSPORT_FIELD_USE_TEAM_NUMBER,
        "Use team number",
        SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL,
        "Use FRC team-number resolution instead of a direct host/IP.",
        0,  // default off — most Shuffleboard use is against localhost or a known IP
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
        SD_TRANSPORT_FIELD_CLIENT_NAME,
        "Client name",
        SD_TRANSPORT_CONNECTION_FIELD_TYPE_STRING,
        "Client identity sent to the NT4 server during WebSocket handshake.",
        0,
        0,
        "SmartDashboard",
        0,
        0
    },
    {
        "auto_connect",
        "Auto-connect",
        SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL,
        "When enabled the client automatically reconnects after disconnection. "
        "Disable for development workflows where no NT4 server is running.",
        1,
        0,
        nullptr,
        0,
        1
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Plugin instance
// ────────────────────────────────────────────────────────────────────────────

struct ShuffleboardPluginInstance
{
    bool running = false;
    std::string clientName = "SmartDashboard";
    std::string host = "127.0.0.1";
    uint16_t port = 5810;
    int teamNumber = 0;
    bool useTeamNumber = false;
    std::unique_ptr<NT4Client> client;
    sd_transport_callbacks_v1 callbacks {};
};

// ────────────────────────────────────────────────────────────────────────────
// JSON settings helpers (reused from NativeLink plugin pattern)
// ────────────────────────────────────────────────────────────────────────────

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

    const std::size_t valueStart = json.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos)
    {
        return fallback;
    }

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

// ────────────────────────────────────────────────────────────────────────────
// Host resolution
// ────────────────────────────────────────────────────────────────────────────

std::string ResolveHost(const ShuffleboardPluginInstance& instance)
{
    if (instance.useTeamNumber && instance.teamNumber > 0)
    {
        // Ian: Standard FRC team-number → roboRIO IP resolution.
        // Team 1234 → 10.12.34.2. Same as NativeLink and Legacy NT.
        const int hi = instance.teamNumber / 100;
        const int lo = instance.teamNumber % 100;
        return "10." + std::to_string(hi) + "." + std::to_string(lo) + ".2";
    }

    if (!instance.host.empty())
    {
        return instance.host;
    }

    return "127.0.0.1";
}

// ────────────────────────────────────────────────────────────────────────────
// ABI callback bridge
// ────────────────────────────────────────────────────────────────────────────

void PublishUpdateCallback(const ShuffleboardPluginInstance& instance, const TopicUpdate& update)
{
    if (instance.callbacks.on_variable_update == nullptr)
    {
        return;
    }

    sd_transport_value_v1 payload {};
    payload.type = 0;

    std::vector<const char*> stringArrayPointers;
    switch (update.value.type)
    {
        case ValueType::Bool:
            payload.type = SD_TRANSPORT_VALUE_TYPE_BOOL;
            payload.bool_value = update.value.boolValue ? 1 : 0;
            break;
        case ValueType::Double:
            payload.type = SD_TRANSPORT_VALUE_TYPE_DOUBLE;
            payload.double_value = update.value.doubleValue;
            break;
        case ValueType::String:
            payload.type = SD_TRANSPORT_VALUE_TYPE_STRING;
            payload.string_value = update.value.stringValue.c_str();
            break;
        case ValueType::StringArray:
            payload.type = SD_TRANSPORT_VALUE_TYPE_STRING_ARRAY;
            stringArrayPointers.reserve(update.value.stringArrayValue.size());
            for (const std::string& item : update.value.stringArrayValue)
            {
                stringArrayPointers.push_back(item.c_str());
            }
            payload.string_array_items = stringArrayPointers.data();
            payload.string_array_count = stringArrayPointers.size();
            break;
    }

    instance.callbacks.on_variable_update(
        instance.callbacks.user_data,
        update.topicPath.c_str(),
        &payload,
        update.serverSequence
    );
}

// ────────────────────────────────────────────────────────────────────────────
// Plugin property queries
// ────────────────────────────────────────────────────────────────────────────

int GetShuffleboardBoolProperty(const char* propertyName, int defaultValue)
{
    if (propertyName == nullptr)
    {
        return defaultValue;
    }

    const std::string name(propertyName);

    // Ian: Chooser support requires write-back to the NT4 server. The publish
    // path in NT4Client::PublishString sends a JSON publish claim followed by
    // a binary value frame. The server creates the topic and updates the
    // retained cache. Returning true here tells the host to assemble inbound
    // chooser sub-keys (.type, /options, /default, /active, /selected) into a
    // chooser widget, and to route user selections through PublishString with
    // the key + "/selected" convention.
    if (name == SD_TRANSPORT_PROPERTY_SUPPORTS_CHOOSER)
    {
        return 1;
    }

    // NT4 servers support multiple clients natively.
    if (name == SD_TRANSPORT_PROPERTY_SUPPORTS_MULTI_CLIENT)
    {
        return 1;
    }

    return defaultValue;
}

const sd_transport_connection_field_descriptor_v1* GetShuffleboardConnectionFields(size_t* outCount)
{
    if (outCount != nullptr)
    {
        *outCount = sizeof(kShuffleboardConnectionFields) / sizeof(kShuffleboardConnectionFields[0]);
    }

    return kShuffleboardConnectionFields;
}

// ────────────────────────────────────────────────────────────────────────────
// Plugin API function table
// ────────────────────────────────────────────────────────────────────────────

sd_transport_instance_v1 CreateShuffleboardInstance()
{
    return new ShuffleboardPluginInstance();
}

void DestroyShuffleboardInstance(sd_transport_instance_v1 instance)
{
    delete static_cast<ShuffleboardPluginInstance*>(instance);
}

int StartShuffleboard(
    sd_transport_instance_v1 instanceHandle,
    const sd_transport_connection_config_v1* config,
    const sd_transport_callbacks_v1* callbacks)
{
    auto* instance = static_cast<ShuffleboardPluginInstance*>(instanceHandle);
    if (instance == nullptr || callbacks == nullptr)
    {
        return 0;
    }

    instance->callbacks = *callbacks;
    instance->clientName = (config != nullptr && config->nt_client_name != nullptr && config->nt_client_name[0] != '\0')
        ? config->nt_client_name
        : "SmartDashboard";
    instance->host = (config != nullptr && config->nt_host != nullptr && config->nt_host[0] != '\0')
        ? config->nt_host
        : "127.0.0.1";
    instance->teamNumber = config != nullptr ? config->nt_team : 0;
    instance->useTeamNumber = config != nullptr ? (config->nt_use_team != 0) : false;

    const char* settingsJson = (config != nullptr) ? config->plugin_settings_json : nullptr;

    instance->port = ReadPluginPortSetting(settingsJson, "port", 5810);
    // Ian: auto_connect is no longer read by the plugin.  The host
    // (MainWindow) now owns reconnect logic via a QTimer that drives
    // Stop()+Start() cycles.  The auto_connect setting still lives in
    // plugin_settings_json and is read by MainWindow::IsAutoConnectEnabled()
    // so the UI checkbox continues to work.

    // Allow host/port override from plugin_settings_json
    const std::string settingsHost = ReadPluginStringSetting(settingsJson, "host", "");
    if (!settingsHost.empty())
    {
        instance->host = settingsHost;
    }

    NT4ClientConfig clientConfig;
    clientConfig.host = ResolveHost(*instance);
    clientConfig.port = instance->port;
    clientConfig.clientName = instance->clientName;

    instance->client = std::make_unique<NT4Client>();

    // Ian: Start is non-blocking — it launches the WebSocket background thread
    // and returns immediately. Connection state changes arrive via callbacks,
    // matching the NativeLink convention. The UI stays responsive while the
    // WebSocket connects/reconnects in the background.
    const bool started = instance->client->Start(
        clientConfig,
        [instance](const TopicUpdate& update)
        {
            PublishUpdateCallback(*instance, update);
        },
        [instance](ConnectionState state)
        {
            if (instance->callbacks.on_connection_state != nullptr)
            {
                instance->callbacks.on_connection_state(
                    instance->callbacks.user_data,
                    static_cast<int>(state)
                );
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

void StopShuffleboard(sd_transport_instance_v1 instanceHandle)
{
    auto* instance = static_cast<ShuffleboardPluginInstance*>(instanceHandle);
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

int ShuffleboardPublishBool(sd_transport_instance_v1 instanceHandle, const char* key, int value)
{
    auto* instance = static_cast<ShuffleboardPluginInstance*>(instanceHandle);
    if (instance == nullptr || instance->client == nullptr || key == nullptr)
    {
        return 0;
    }

    return instance->client->PublishBool(key, value != 0) ? 1 : 0;
}

int ShuffleboardPublishDouble(sd_transport_instance_v1 instanceHandle, const char* key, double value)
{
    auto* instance = static_cast<ShuffleboardPluginInstance*>(instanceHandle);
    if (instance == nullptr || instance->client == nullptr || key == nullptr)
    {
        return 0;
    }

    return instance->client->PublishDouble(key, value) ? 1 : 0;
}

int ShuffleboardPublishString(sd_transport_instance_v1 instanceHandle, const char* key, const char* value)
{
    auto* instance = static_cast<ShuffleboardPluginInstance*>(instanceHandle);
    if (instance == nullptr || instance->client == nullptr || key == nullptr || value == nullptr)
    {
        return 0;
    }

    return instance->client->PublishString(key, value) ? 1 : 0;
}

// ────────────────────────────────────────────────────────────────────────────
// Static descriptor and API table
// ────────────────────────────────────────────────────────────────────────────

const sd_transport_api_v1 kShuffleboardApi = {
    &CreateShuffleboardInstance,
    &DestroyShuffleboardInstance,
    &StartShuffleboard,
    &StopShuffleboard,
    &ShuffleboardPublishBool,
    &ShuffleboardPublishDouble,
    &ShuffleboardPublishString
};

const sd_transport_plugin_descriptor_v1 kShuffleboardDescriptor = {
    sizeof(sd_transport_plugin_descriptor_v1),
    SD_TRANSPORT_PLUGIN_API_VERSION_1,
    "shuffleboard",                                          // plugin_id
    "Shuffleboard (NT4)",                                    // display_name
    "shuffleboard",                                          // settings_profile_id
    SD_TRANSPORT_PLUGIN_FLAG_USE_SHORT_DISPLAY_KEYS,         // flags
    &GetShuffleboardBoolProperty,
    &GetShuffleboardConnectionFields,
    &kShuffleboardApi
};

} // anonymous namespace

extern "C" SD_TRANSPORT_PLUGIN_EXPORT const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void)
{
    return &kShuffleboardDescriptor;
}
