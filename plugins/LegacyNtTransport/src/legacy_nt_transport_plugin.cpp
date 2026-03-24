#define SD_TRANSPORT_PLUGIN_EXPORTS 1

#include "transport/dashboard_transport_plugin_api.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    const sd_transport_connection_field_descriptor_v1 kLegacyNtConnectionFields[] = {
        {
            SD_TRANSPORT_FIELD_USE_TEAM_NUMBER,
            "Use team number",
            SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL,
            "Use FRC team-number resolution instead of a direct host/IP.",
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
            "Used when connecting directly by host name or IP address.",
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
            "Name reported by the dashboard client to the transport ecosystem.",
            0,
            0,
            "SmartDashboardApp",
            0,
            0
        }
    };

    struct EntryMeta
    {
        std::uint16_t id = 0;
        std::uint16_t seq = 0;
        std::uint8_t typeId = 0;
        std::string wireKey;
    };

    struct LegacyNtTransportInstance
    {
        std::mutex mutex;
        bool running = false;
        bool winsockReady = false;
        SOCKET socket = INVALID_SOCKET;
        std::thread worker;
        sd_transport_callbacks_v1 callbacks {};

        std::string transportId = "legacy-nt";
        std::string pluginSettingsJson;
        std::string ntHost = "127.0.0.1";
        int ntTeam = 0;
        bool ntUseTeam = true;
        std::string ntClientName = "SmartDashboardApp";
        std::string replayFilePath;

        std::uint16_t nextEntryId = 30000;
        std::map<std::string, EntryMeta> keyToEntry;
        std::map<std::uint16_t, std::string> idToKey;
    };

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

    const sd_transport_connection_field_descriptor_v1* GetLegacyNtConnectionFields(size_t* outCount)
    {
        if (outCount != nullptr)
        {
            *outCount = sizeof(kLegacyNtConnectionFields) / sizeof(kLegacyNtConnectionFields[0]);
        }

        return kLegacyNtConnectionFields;
    }

    std::string NormalizeIncomingKey(const std::string& wireKey)
    {
        static const std::string prefix = "/SmartDashboard/";
        if (wireKey.rfind(prefix, 0) == 0)
        {
            return wireKey.substr(prefix.size());
        }

        return wireKey;
    }

    std::string ToWireKey(const std::string& key)
    {
        static const std::string prefix = "/SmartDashboard/";
        if (key.rfind(prefix, 0) == 0)
        {
            return key;
        }

        return prefix + key;
    }

    void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value)
    {
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    }

    bool ReadU16(const std::vector<std::uint8_t>& in, std::size_t& offset, std::uint16_t& out)
    {
        if (offset + 2 > in.size())
        {
            return false;
        }

        out = static_cast<std::uint16_t>((static_cast<std::uint16_t>(in[offset]) << 8) | in[offset + 1]);
        offset += 2;
        return true;
    }

    bool ReadExact(SOCKET socket, std::uint8_t* data, std::size_t size)
    {
        std::size_t total = 0;
        while (total < size)
        {
            const int count = recv(socket, reinterpret_cast<char*>(data + total), static_cast<int>(size - total), 0);
            if (count <= 0)
            {
                return false;
            }
            total += static_cast<std::size_t>(count);
        }

        return true;
    }

    bool SendExact(SOCKET socket, const std::uint8_t* data, std::size_t size)
    {
        std::size_t total = 0;
        while (total < size)
        {
            const int sent = send(socket, reinterpret_cast<const char*>(data + total), static_cast<int>(size - total), 0);
            if (sent <= 0)
            {
                return false;
            }
            total += static_cast<std::size_t>(sent);
        }

        return true;
    }

    bool ReadU16FromSocket(SOCKET socket, std::uint16_t& out)
    {
        std::array<std::uint8_t, 2> raw {};
        if (!ReadExact(socket, raw.data(), raw.size()))
        {
            return false;
        }

        std::size_t offset = 0;
        const std::vector<std::uint8_t> temp(raw.begin(), raw.end());
        return ReadU16(temp, offset, out);
    }

    bool EnsureWinsockLocked(LegacyNtTransportInstance& instance)
    {
        if (instance.winsockReady)
        {
            return true;
        }

        WSADATA data {};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0)
        {
            return false;
        }

        instance.winsockReady = true;
        return true;
    }

    bool IsRunning(LegacyNtTransportInstance& instance)
    {
        std::lock_guard<std::mutex> lock(instance.mutex);
        return instance.running;
    }

    void PublishStateUnlocked(LegacyNtTransportInstance& instance, int state)
    {
        if (instance.callbacks.on_connection_state != nullptr)
        {
            instance.callbacks.on_connection_state(instance.callbacks.user_data, state);
        }
    }

    void PublishState(LegacyNtTransportInstance& instance, int state)
    {
        std::lock_guard<std::mutex> lock(instance.mutex);
        PublishStateUnlocked(instance, state);
    }

    void PublishBoolUpdate(LegacyNtTransportInstance& instance, const std::string& key, std::uint64_t seq, bool value)
    {
        if (instance.callbacks.on_variable_update == nullptr)
        {
            return;
        }

        sd_transport_value_v1 payload {};
        payload.type = SD_TRANSPORT_VALUE_TYPE_BOOL;
        payload.bool_value = value ? 1 : 0;
        instance.callbacks.on_variable_update(instance.callbacks.user_data, key.c_str(), &payload, seq);
    }

    void PublishDoubleUpdate(LegacyNtTransportInstance& instance, const std::string& key, std::uint64_t seq, double value)
    {
        if (instance.callbacks.on_variable_update == nullptr)
        {
            return;
        }

        sd_transport_value_v1 payload {};
        payload.type = SD_TRANSPORT_VALUE_TYPE_DOUBLE;
        payload.double_value = value;
        instance.callbacks.on_variable_update(instance.callbacks.user_data, key.c_str(), &payload, seq);
    }

    void PublishStringUpdate(LegacyNtTransportInstance& instance, const std::string& key, std::uint64_t seq, const std::string& value)
    {
        if (instance.callbacks.on_variable_update == nullptr)
        {
            return;
        }

        sd_transport_value_v1 payload {};
        payload.type = SD_TRANSPORT_VALUE_TYPE_STRING;
        payload.string_value = value.c_str();
        instance.callbacks.on_variable_update(instance.callbacks.user_data, key.c_str(), &payload, seq);
    }

    void PublishStringArrayUpdate(LegacyNtTransportInstance& instance, const std::string& key, std::uint64_t seq, const std::vector<std::string>& values)
    {
        if (instance.callbacks.on_variable_update == nullptr)
        {
            return;
        }

        std::vector<const char*> pointers;
        pointers.reserve(values.size());
        for (const std::string& value : values)
        {
            pointers.push_back(value.c_str());
        }

        sd_transport_value_v1 payload {};
        payload.type = SD_TRANSPORT_VALUE_TYPE_STRING_ARRAY;
        payload.string_array_items = pointers.data();
        payload.string_array_count = pointers.size();
        instance.callbacks.on_variable_update(instance.callbacks.user_data, key.c_str(), &payload, seq);
    }

    bool ReadTypedValueAndPublish(LegacyNtTransportInstance& instance, SOCKET socket, const std::string& key, std::uint64_t seq, std::uint8_t typeId)
    {
        if (typeId == 0x00)
        {
            std::uint8_t b = 0;
            if (!ReadExact(socket, &b, 1))
            {
                return false;
            }
            PublishBoolUpdate(instance, key, seq, b != 0);
            return true;
        }

        if (typeId == 0x01)
        {
            std::array<std::uint8_t, 8> raw {};
            if (!ReadExact(socket, raw.data(), raw.size()))
            {
                return false;
            }

            std::uint64_t bits = 0;
            for (const std::uint8_t byte : raw)
            {
                bits = (bits << 8) | byte;
            }

            PublishDoubleUpdate(instance, key, seq, std::bit_cast<double>(bits));
            return true;
        }

        if (typeId == 0x02)
        {
            std::uint16_t len = 0;
            if (!ReadU16FromSocket(socket, len))
            {
                return false;
            }

            std::vector<std::uint8_t> bytes(len);
            if (len > 0 && !ReadExact(socket, bytes.data(), bytes.size()))
            {
                return false;
            }

            PublishStringUpdate(instance, key, seq, std::string(bytes.begin(), bytes.end()));
            return true;
        }

        if (typeId == 0x12)
        {
            std::uint8_t elementCount = 0;
            if (!ReadExact(socket, &elementCount, 1))
            {
                return false;
            }

            std::vector<std::string> values;
            values.reserve(elementCount);
            for (std::uint8_t i = 0; i < elementCount; ++i)
            {
                std::uint16_t len = 0;
                if (!ReadU16FromSocket(socket, len))
                {
                    return false;
                }

                std::vector<std::uint8_t> bytes(len);
                if (len > 0 && !ReadExact(socket, bytes.data(), bytes.size()))
                {
                    return false;
                }

                values.emplace_back(bytes.begin(), bytes.end());
            }

            PublishStringArrayUpdate(instance, key, seq, values);
            return true;
        }

        return false;
    }

    std::string ResolveHostLocked(const LegacyNtTransportInstance& instance)
    {
        if (instance.ntUseTeam && instance.ntTeam > 0)
        {
            const int team = instance.ntTeam;
            const int hi = team / 100;
            const int lo = team % 100;
            return "10." + std::to_string(hi) + "." + std::to_string(lo) + ".2";
        }

        if (!instance.ntHost.empty())
        {
            return instance.ntHost;
        }

        return "127.0.0.1";
    }

    void CloseSocket(LegacyNtTransportInstance& instance)
    {
        SOCKET socket = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(instance.mutex);
            socket = instance.socket;
            instance.socket = INVALID_SOCKET;
        }

        if (socket != INVALID_SOCKET)
        {
            shutdown(socket, SD_BOTH);
            closesocket(socket);
        }
    }

    bool ConnectAndHandshake(LegacyNtTransportInstance& instance)
    {
        std::string host;
        {
            std::lock_guard<std::mutex> lock(instance.mutex);
            host = ResolveHostLocked(instance);
        }

        addrinfo hints {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), "1735", &hints, &result) != 0 || result == nullptr)
        {
            return false;
        }

        SOCKET socket = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (socket == INVALID_SOCKET)
        {
            freeaddrinfo(result);
            return false;
        }

        const int connected = ::connect(socket, result->ai_addr, static_cast<int>(result->ai_addrlen));
        freeaddrinfo(result);
        if (connected != 0)
        {
            closesocket(socket);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(instance.mutex);
            instance.socket = socket;
            instance.keyToEntry.clear();
            instance.idToKey.clear();
            instance.nextEntryId = 30000;
        }

        std::vector<std::uint8_t> hello;
        hello.push_back(0x01);
        WriteU16(hello, 0x0200);
        return SendExact(socket, hello.data(), hello.size());
    }

    bool HandleEntryAssignment(LegacyNtTransportInstance& instance, SOCKET socket)
    {
        std::uint16_t keyLen = 0;
        if (!ReadU16FromSocket(socket, keyLen))
        {
            return false;
        }

        std::vector<std::uint8_t> keyBytes(keyLen);
        if (keyLen > 0 && !ReadExact(socket, keyBytes.data(), keyBytes.size()))
        {
            return false;
        }

        std::uint8_t valueType = 0;
        if (!ReadExact(socket, &valueType, 1))
        {
            return false;
        }

        std::uint16_t entryId = 0;
        std::uint16_t seq = 0;
        if (!ReadU16FromSocket(socket, entryId) || !ReadU16FromSocket(socket, seq))
        {
            return false;
        }

        const std::string wireKey(keyBytes.begin(), keyBytes.end());
        const std::string key = NormalizeIncomingKey(wireKey);
        if (!ReadTypedValueAndPublish(instance, socket, key, seq, valueType))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(instance.mutex);
        EntryMeta meta;
        meta.id = entryId;
        meta.seq = seq;
        meta.typeId = valueType;
        meta.wireKey = wireKey;
        instance.keyToEntry[key] = meta;
        instance.idToKey[entryId] = key;
        return true;
    }

    bool HandleFieldUpdate(LegacyNtTransportInstance& instance, SOCKET socket)
    {
        std::uint16_t entryId = 0;
        std::uint16_t seq = 0;
        if (!ReadU16FromSocket(socket, entryId) || !ReadU16FromSocket(socket, seq))
        {
            return false;
        }

        std::uint8_t valueType = 0;
        std::string key;
        {
            std::lock_guard<std::mutex> lock(instance.mutex);
            const auto idIt = instance.idToKey.find(entryId);
            if (idIt == instance.idToKey.end())
            {
                return false;
            }

            key = idIt->second;
            const auto keyIt = instance.keyToEntry.find(key);
            if (keyIt == instance.keyToEntry.end())
            {
                return false;
            }

            keyIt->second.seq = seq;
            valueType = keyIt->second.typeId;
        }

        return ReadTypedValueAndPublish(instance, socket, key, seq, valueType);
    }

    bool ProcessOneMessage(LegacyNtTransportInstance& instance)
    {
        SOCKET socket = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(instance.mutex);
            socket = instance.socket;
        }

        if (socket == INVALID_SOCKET)
        {
            return false;
        }

        std::uint8_t type = 0;
        if (!ReadExact(socket, &type, 1))
        {
            return false;
        }

        switch (type)
        {
            case 0x00:
                return true;
            case 0x03:
                return true;
            case 0x02:
            {
                std::array<std::uint8_t, 2> protocol {};
                return ReadExact(socket, protocol.data(), protocol.size());
            }
            case 0x10:
                return HandleEntryAssignment(instance, socket);
            case 0x11:
                return HandleFieldUpdate(instance, socket);
            default:
                return false;
        }
    }

    void RunClientLoop(LegacyNtTransportInstance& instance)
    {
        // Ian: Single-attempt connection.  The host (MainWindow) now owns the
        // reconnect timer and drives retries via Stop()+Start() cycles.  This
        // method connects once, runs the message loop until the connection
        // drops or Stop() sets running=false, fires Disconnected, and exits.
        //
        // Previous behavior: an outer `while (IsRunning(instance))` loop with
        // sleep between retries.  That logic is now in
        // MainWindow::OnReconnectTimerFired().

        if (!ConnectAndHandshake(instance))
        {
            PublishState(instance, SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED);
            return;
        }

        PublishState(instance, SD_TRANSPORT_CONNECTION_STATE_CONNECTED);

        while (IsRunning(instance))
        {
            if (!ProcessOneMessage(instance))
            {
                break;
            }
        }

        CloseSocket(instance);
        PublishState(instance, SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED);
    }

    bool SendEntryAssignment(
        LegacyNtTransportInstance& instance,
        const char* key,
        std::uint8_t typeId,
        const std::function<void(std::vector<std::uint8_t>&)>& appendValue
    )
    {
        if (key == nullptr)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(instance.mutex);
        if (!instance.running || instance.socket == INVALID_SOCKET)
        {
            return false;
        }

        const std::string keyString(key);

        EntryMeta meta;
        auto it = instance.keyToEntry.find(keyString);
        bool isUpdate = false;
        if (it == instance.keyToEntry.end())
        {
            meta.id = instance.nextEntryId++;
            meta.seq = 0;
            meta.typeId = typeId;
            meta.wireKey = ToWireKey(keyString);
        }
        else
        {
            meta = it->second;
            meta.typeId = typeId;
            if (meta.wireKey.empty())
            {
                meta.wireKey = ToWireKey(keyString);
            }
            isUpdate = true;
        }

        meta.seq = static_cast<std::uint16_t>(meta.seq + 1);
        instance.keyToEntry[keyString] = meta;
        instance.idToKey[meta.id] = keyString;

        std::vector<std::uint8_t> packet;
        if (isUpdate)
        {
            packet.push_back(0x11);
            WriteU16(packet, meta.id);
            WriteU16(packet, meta.seq);
        }
        else
        {
            packet.push_back(0x10);
            const std::size_t bounded = std::min<std::size_t>(meta.wireKey.size(), 65535u);
            WriteU16(packet, static_cast<std::uint16_t>(bounded));
            packet.insert(packet.end(), meta.wireKey.begin(), meta.wireKey.begin() + static_cast<std::ptrdiff_t>(bounded));
            packet.push_back(typeId);
            WriteU16(packet, meta.id);
            WriteU16(packet, meta.seq);
        }
        appendValue(packet);

        return SendExact(instance.socket, packet.data(), packet.size());
    }

    void* CreateLegacyNtTransport()
    {
        return new LegacyNtTransportInstance();
    }

    void DestroyLegacyNtTransport(void* rawInstance)
    {
        delete static_cast<LegacyNtTransportInstance*>(rawInstance);
    }

    int StartLegacyNtTransport(
        void* rawInstance,
        const sd_transport_connection_config_v1* config,
        const sd_transport_callbacks_v1* callbacks
    )
    {
        auto* instance = static_cast<LegacyNtTransportInstance*>(rawInstance);
        if (instance == nullptr)
        {
            return 0;
        }

        std::lock_guard<std::mutex> lock(instance->mutex);
        instance->callbacks = callbacks != nullptr ? *callbacks : sd_transport_callbacks_v1 {};

        if (instance->running)
        {
            return 1;
        }

        if (config != nullptr)
        {
            instance->transportId = config->transport_id != nullptr ? config->transport_id : "legacy-nt";
            instance->pluginSettingsJson = config->plugin_settings_json != nullptr ? config->plugin_settings_json : "";
            instance->ntHost = config->nt_host != nullptr ? config->nt_host : "127.0.0.1";
            instance->ntTeam = config->nt_team;
            instance->ntUseTeam = config->nt_use_team != 0;
            instance->ntClientName = config->nt_client_name != nullptr ? config->nt_client_name : "SmartDashboardApp";
            instance->replayFilePath = config->replay_file_path != nullptr ? config->replay_file_path : "";
        }

        if (!EnsureWinsockLocked(*instance))
        {
            PublishStateUnlocked(*instance, SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED);
            return 0;
        }

        instance->running = true;
        PublishStateUnlocked(*instance, SD_TRANSPORT_CONNECTION_STATE_CONNECTING);
        instance->worker = std::thread([instance]() { RunClientLoop(*instance); });
        return 1;
    }

    void StopLegacyNtTransport(void* rawInstance)
    {
        auto* instance = static_cast<LegacyNtTransportInstance*>(rawInstance);
        if (instance == nullptr)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(instance->mutex);
            if (!instance->running)
            {
                return;
            }
            instance->running = false;
        }

        CloseSocket(*instance);

        if (instance->worker.joinable())
        {
            instance->worker.join();
        }

        std::lock_guard<std::mutex> lock(instance->mutex);
        PublishStateUnlocked(*instance, SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED);
    }

    int PublishLegacyNtBool(void* rawInstance, const char* key, int value)
    {
        auto* instance = static_cast<LegacyNtTransportInstance*>(rawInstance);
        if (instance == nullptr)
        {
            return 0;
        }

        return SendEntryAssignment(*instance, key, 0x00, [value](std::vector<std::uint8_t>& payload)
        {
            payload.push_back(value != 0 ? 1u : 0u);
        }) ? 1 : 0;
    }

    int PublishLegacyNtDouble(void* rawInstance, const char* key, double value)
    {
        auto* instance = static_cast<LegacyNtTransportInstance*>(rawInstance);
        if (instance == nullptr)
        {
            return 0;
        }

        return SendEntryAssignment(*instance, key, 0x01, [value](std::vector<std::uint8_t>& payload)
        {
            const std::uint64_t raw = std::bit_cast<std::uint64_t>(value);
            for (int shift = 56; shift >= 0; shift -= 8)
            {
                payload.push_back(static_cast<std::uint8_t>((raw >> shift) & 0xFFu));
            }
        }) ? 1 : 0;
    }

    int PublishLegacyNtString(void* rawInstance, const char* key, const char* value)
    {
        auto* instance = static_cast<LegacyNtTransportInstance*>(rawInstance);
        if (instance == nullptr)
        {
            return 0;
        }

        return SendEntryAssignment(*instance, key, 0x02, [value](std::vector<std::uint8_t>& payload)
        {
            const std::string bytes = value != nullptr ? value : "";
            const std::size_t bounded = std::min<std::size_t>(bytes.size(), 65535u);
            WriteU16(payload, static_cast<std::uint16_t>(bounded));
            payload.insert(payload.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bounded));
        }) ? 1 : 0;
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
        &GetLegacyNtConnectionFields,
        &kLegacyNtTransportApi
    };
}

extern "C" SD_TRANSPORT_PLUGIN_EXPORT const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void)
{
    return &kLegacyNtPluginDescriptor;
}
