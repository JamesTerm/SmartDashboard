#include "transport/dashboard_transport.h"

#include "sd_direct_publisher.h"
#include "sd_direct_subscriber.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

namespace sd::transport
{
    namespace
    {
        ConnectionState ToConnectionState(sd::direct::ConnectionState state)
        {
            switch (state)
            {
                case sd::direct::ConnectionState::Connecting:
                    return ConnectionState::Connecting;
                case sd::direct::ConnectionState::Connected:
                    return ConnectionState::Connected;
                case sd::direct::ConnectionState::Stale:
                    return ConnectionState::Stale;
                case sd::direct::ConnectionState::Disconnected:
                default:
                    return ConnectionState::Disconnected;
            }
        }

        PlaybackMarkerKind ToPlaybackMarkerKindFromState(const QString& stateText)
        {
            const QString normalized = stateText.trimmed().toLower();
            if (normalized == "connected" || normalized == "connect")
            {
                return PlaybackMarkerKind::Connect;
            }
            if (normalized == "disconnected" || normalized == "disconnect")
            {
                return PlaybackMarkerKind::Disconnect;
            }
            if (normalized == "stale")
            {
                return PlaybackMarkerKind::Stale;
            }
            if (normalized.contains("anomaly") || normalized.contains("brownout") || normalized.contains("outlier"))
            {
                return PlaybackMarkerKind::Anomaly;
            }

            return PlaybackMarkerKind::Generic;
        }

        PlaybackMarkerKind ToPlaybackMarkerKindFromMarkerType(const QString& markerType)
        {
            return ToPlaybackMarkerKindFromState(markerType);
        }

        class DirectDashboardTransport final : public IDashboardTransport
        {
        public:
            DirectDashboardTransport()
            {
                sd::direct::SubscriberConfig subConfig;
                m_subscriber = sd::direct::CreateDirectSubscriber(subConfig);

                sd::direct::PublisherConfig pubConfig;
                pubConfig.mappingName = L"Local\\SmartDashboard.Direct.Command.Buffer";
                pubConfig.dataEventName = L"Local\\SmartDashboard.Direct.Command.DataAvailable";
                pubConfig.heartbeatEventName = L"Local\\SmartDashboard.Direct.Command.Heartbeat";
                pubConfig.autoFlushThread = false;
                m_commandPublisher = sd::direct::CreateDirectPublisher(pubConfig);
            }

            bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) override
            {
                m_onVariableUpdate = std::move(onVariableUpdate);
                m_onConnectionState = std::move(onConnectionState);
                m_latestByKey.clear();

                if (!m_subscriber || !m_commandPublisher)
                {
                    return false;
                }

                if (!m_commandPublisher->Start())
                {
                    return false;
                }

                const bool subscriberStarted = m_subscriber->Start(
                    [this](const sd::direct::VariableUpdate& update)
                    {
                        if (!m_onVariableUpdate)
                        {
                            return;
                        }

                        VariableUpdate converted;
                        converted.key = QString::fromStdString(update.key);
                        converted.valueType = static_cast<int>(update.type);
                        converted.seq = update.seq;

                        switch (update.type)
                        {
                            case sd::direct::ValueType::Bool:
                                converted.value = update.value.boolValue;
                                break;
                            case sd::direct::ValueType::Double:
                                converted.value = update.value.doubleValue;
                                break;
                            case sd::direct::ValueType::String:
                                converted.value = QString::fromStdString(update.value.stringValue);
                                break;
                            case sd::direct::ValueType::StringArray:
                            {
                                QStringList list;
                                for (const std::string& item : update.value.stringArrayValue)
                                {
                                    list.push_back(QString::fromStdString(item));
                                }
                                converted.value = list;
                                break;
                            }
                            default:
                                converted.value = QVariant();
                                break;
                        }

                        m_onVariableUpdate(converted);

                        m_latestByKey[update.key] = update;
                    },
                    [this](sd::direct::ConnectionState state)
                    {
                        if (state == sd::direct::ConnectionState::Connected)
                        {
                            m_connectedSeen.store(true);
                        }
                        if (m_onConnectionState)
                        {
                            m_onConnectionState(ToConnectionState(state));
                        }
                    }
                );

                if (!subscriberStarted)
                {
                    m_commandPublisher->Stop();
                    return false;
                }

                m_connectedSeen.store(false);

                return true;
            }

            void Stop() override
            {
                if (m_subscriber)
                {
                    m_subscriber->Stop();
                }

                if (m_commandPublisher)
                {
                    m_commandPublisher->Stop();
                }

                m_latestByKey.clear();
                m_connectedSeen.store(false);
            }

            bool HasSeenConnected() const
            {
                return m_connectedSeen.load();
            }

            bool PublishBool(const QString& key, bool value) override
            {
                if (!m_commandPublisher)
                {
                    return false;
                }
                m_commandPublisher->PublishBool(key.toStdString(), value);
                return m_commandPublisher->FlushNow();
            }

            bool PublishDouble(const QString& key, double value) override
            {
                if (!m_commandPublisher)
                {
                    return false;
                }
                m_commandPublisher->PublishDouble(key.toStdString(), value);
                return m_commandPublisher->FlushNow();
            }

            bool PublishString(const QString& key, const QString& value) override
            {
                if (!m_commandPublisher)
                {
                    return false;
                }
                m_commandPublisher->PublishString(key.toStdString(), value.toStdString());
                return m_commandPublisher->FlushNow();
            }

            void ReplayRetainedControls(const std::function<void(const QString& key, int valueType, const QVariant& value)>& replayFn) override
            {
                if (!replayFn)
                {
                    return;
                }

                const auto replayNumeric = [&](const QString& key)
                {
                    const std::string stdKey = key.toStdString();
                    const auto it = m_latestByKey.find(stdKey);
                    if (it == m_latestByKey.end())
                    {
                        return;
                    }

                    if (it->second.type == sd::direct::ValueType::Double)
                    {
                        replayFn(key, static_cast<int>(sd::direct::ValueType::Double), QVariant(it->second.value.doubleValue));
                    }
                    else if (it->second.type == sd::direct::ValueType::String)
                    {
                        bool ok = false;
                        const double parsed = QString::fromStdString(it->second.value.stringValue).toDouble(&ok);
                        if (ok)
                        {
                            replayFn(key, static_cast<int>(sd::direct::ValueType::Double), QVariant(parsed));
                        }
                    }
                };

                replayNumeric(QStringLiteral("AutonTest"));
                replayNumeric(QStringLiteral("Test/AutonTest"));
            }

        private:
            std::unique_ptr<sd::direct::IDirectSubscriber> m_subscriber;
            std::unique_ptr<sd::direct::IDirectPublisher> m_commandPublisher;
            VariableUpdateCallback m_onVariableUpdate;
            ConnectionStateCallback m_onConnectionState;
            std::unordered_map<std::string, sd::direct::VariableUpdate> m_latestByKey;
            std::atomic<bool> m_connectedSeen {false};
        };

        class NetworkTablesDashboardTransport final : public IDashboardTransport
        {
        public:
            explicit NetworkTablesDashboardTransport(ConnectionConfig config)
                : m_config(std::move(config))
            {
            }

            ~NetworkTablesDashboardTransport() override
            {
                Stop();
            }

            bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_onVariableUpdate = std::move(onVariableUpdate);
                m_onConnectionState = std::move(onConnectionState);

                if (m_running)
                {
                    return true;
                }

                if (!EnsureWinsockLocked())
                {
                    PublishStateUnlocked(ConnectionState::Disconnected);
                    return false;
                }

                m_running = true;
                PublishStateUnlocked(ConnectionState::Connecting);
                m_worker = std::thread([this]() { RunClientLoop(); });
                return true;
            }

            void Stop() override
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!m_running)
                    {
                        return;
                    }
                    m_running = false;
                }

                CloseSocket();

                if (m_worker.joinable())
                {
                    m_worker.join();
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                PublishStateUnlocked(ConnectionState::Disconnected);
            }

            bool PublishBool(const QString& key, bool value) override
            {
                return SendEntryAssignment(key.toStdString(), 0x00, [value](std::vector<std::uint8_t>& payload)
                {
                    payload.push_back(value ? 1u : 0u);
                });
            }

            bool PublishDouble(const QString& key, double value) override
            {
                return SendEntryAssignment(key.toStdString(), 0x01, [value](std::vector<std::uint8_t>& payload)
                {
                    const std::uint64_t raw = std::bit_cast<std::uint64_t>(value);
                    for (int shift = 56; shift >= 0; shift -= 8)
                    {
                        payload.push_back(static_cast<std::uint8_t>((raw >> shift) & 0xFFu));
                    }
                });
            }

            bool PublishString(const QString& key, const QString& value) override
            {
                return SendEntryAssignment(key.toStdString(), 0x02, [value](std::vector<std::uint8_t>& payload)
                {
                    const std::string bytes = value.toStdString();
                    const std::size_t bounded = std::min<std::size_t>(bytes.size(), 65535u);
                    WriteU16(payload, static_cast<std::uint16_t>(bounded));
                    payload.insert(payload.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bounded));
                });
            }

        private:
            struct EntryMeta
            {
                std::uint16_t id = 0;
                std::uint16_t seq = 0;
                std::uint8_t typeId = 0;
                std::string wireKey;
            };

            static std::string NormalizeIncomingKey(const std::string& wireKey)
            {
                static const std::string prefix = "/SmartDashboard/";
                if (wireKey.rfind(prefix, 0) == 0)
                {
                    return wireKey.substr(prefix.size());
                }
                return wireKey;
            }

            static std::string ToWireKey(const std::string& key)
            {
                static const std::string prefix = "/SmartDashboard/";
                if (key.rfind(prefix, 0) == 0)
                {
                    return key;
                }
                return prefix + key;
            }

            static void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value)
            {
                out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
                out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            }

            static bool ReadU16(const std::vector<std::uint8_t>& in, std::size_t& offset, std::uint16_t& out)
            {
                if (offset + 2 > in.size())
                {
                    return false;
                }
                out = static_cast<std::uint16_t>((static_cast<std::uint16_t>(in[offset]) << 8) | in[offset + 1]);
                offset += 2;
                return true;
            }

            static bool ReadExact(SOCKET socket, std::uint8_t* data, std::size_t size)
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

            static bool SendExact(SOCKET socket, const std::uint8_t* data, std::size_t size)
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

            static bool ReadU16FromSocket(SOCKET socket, std::uint16_t& out)
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

            void RunClientLoop()
            {
                while (IsRunning())
                {
                    if (!ConnectAndHandshake())
                    {
                        PublishState(ConnectionState::Disconnected);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        continue;
                    }

                    PublishState(ConnectionState::Connected);

                    while (IsRunning())
                    {
                        if (!ProcessOneMessage())
                        {
                            break;
                        }
                    }

                    CloseSocket();
                    PublishState(ConnectionState::Disconnected);
                    if (IsRunning())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                }
            }

            bool ConnectAndHandshake()
            {
                std::string host;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    host = ResolveHostLocked();
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
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_socket = socket;
                    m_keyToEntry.clear();
                    m_idToKey.clear();
                    m_nextEntryId = 30000;
                }

                std::vector<std::uint8_t> hello;
                hello.push_back(0x01);
                WriteU16(hello, 0x0200);
                return SendExact(socket, hello.data(), hello.size());
            }

            bool ProcessOneMessage()
            {
                SOCKET socket = INVALID_SOCKET;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    socket = m_socket;
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
                        return HandleEntryAssignment(socket);
                    case 0x11:
                        return HandleFieldUpdate(socket);
                    default:
                        return false;
                }
            }

            bool HandleEntryAssignment(SOCKET socket)
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
                VariableUpdate update;
                update.key = QString::fromStdString(key);
                update.seq = seq;
                update.valueType = static_cast<int>(ToDirectValueType(valueType));

                if (!ReadTypedValue(socket, valueType, update.value))
                {
                    return false;
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    EntryMeta meta;
                    meta.id = entryId;
                    meta.seq = seq;
                    meta.typeId = valueType;
                    meta.wireKey = wireKey;
                    m_keyToEntry[key] = meta;
                    m_idToKey[entryId] = key;
                }

                PublishUpdate(update);
                return true;
            }

            bool HandleFieldUpdate(SOCKET socket)
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
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto idIt = m_idToKey.find(entryId);
                    if (idIt == m_idToKey.end())
                    {
                        return false;
                    }

                    key = idIt->second;
                    const auto keyIt = m_keyToEntry.find(key);
                    if (keyIt == m_keyToEntry.end())
                    {
                        return false;
                    }

                    keyIt->second.seq = seq;
                    valueType = keyIt->second.typeId;
                }

                VariableUpdate update;
                update.key = QString::fromStdString(key);
                update.seq = seq;
                update.valueType = static_cast<int>(ToDirectValueType(valueType));

                if (!ReadTypedValue(socket, valueType, update.value))
                {
                    return false;
                }

                PublishUpdate(update);
                return true;
            }

            bool ReadTypedValue(SOCKET socket, std::uint8_t typeId, QVariant& outValue)
            {
                if (typeId == 0x00)
                {
                    std::uint8_t b = 0;
                    if (!ReadExact(socket, &b, 1))
                    {
                        return false;
                    }
                    outValue = (b != 0);
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

                    outValue = std::bit_cast<double>(bits);
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

                    outValue = QString::fromStdString(std::string(bytes.begin(), bytes.end()));
                    return true;
                }

                // NT string array (0x12): 1-byte element count followed by
                // repeated [u16 length + UTF-8 bytes] elements.
                // We expose this as QStringList QVariant so chooser option
                // metadata can be consumed without lossy string flattening.
                if (typeId == 0x12)
                {
                    std::uint8_t elementCount = 0;
                    if (!ReadExact(socket, &elementCount, 1))
                    {
                        return false;
                    }

                    QStringList values;
                    values.reserve(static_cast<int>(elementCount));
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

                        values.push_back(QString::fromStdString(std::string(bytes.begin(), bytes.end())));
                    }

                    outValue = values;
                    return true;
                }

                return false;
            }

            sd::direct::ValueType ToDirectValueType(std::uint8_t ntType) const
            {
                switch (ntType)
                {
                    case 0x00:
                        return sd::direct::ValueType::Bool;
                    case 0x01:
                        return sd::direct::ValueType::Double;
                    case 0x02:
                    default:
                        return sd::direct::ValueType::String;
                }
            }

            bool SendEntryAssignment(const std::string& key, std::uint8_t typeId, const std::function<void(std::vector<std::uint8_t>&)>& appendValue)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_running || m_socket == INVALID_SOCKET)
                {
                    return false;
                }

                EntryMeta meta;
                auto it = m_keyToEntry.find(key);
                bool isUpdate = false;
                if (it == m_keyToEntry.end())
                {
                    meta.id = m_nextEntryId++;
                    meta.seq = 0;
                    meta.typeId = typeId;
                    meta.wireKey = ToWireKey(key);
                }
                else
                {
                    meta = it->second;
                    meta.typeId = typeId;
                    if (meta.wireKey.empty())
                    {
                        meta.wireKey = ToWireKey(key);
                    }
                    isUpdate = true;
                }

                meta.seq = static_cast<std::uint16_t>(meta.seq + 1);
                m_keyToEntry[key] = meta;
                m_idToKey[meta.id] = key;

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

                return SendExact(m_socket, packet.data(), packet.size());
            }

            std::string ResolveHostLocked() const
            {
                if (m_config.ntUseTeam && m_config.ntTeam > 0)
                {
                    const int team = m_config.ntTeam;
                    const int hi = team / 100;
                    const int lo = team % 100;
                    return "10." + std::to_string(hi) + "." + std::to_string(lo) + ".2";
                }

                const QString host = m_config.ntHost.trimmed();
                if (!host.isEmpty())
                {
                    return host.toStdString();
                }

                return "127.0.0.1";
            }

            bool EnsureWinsockLocked()
            {
                if (m_winsockReady)
                {
                    return true;
                }

                WSADATA data {};
                const int result = WSAStartup(MAKEWORD(2, 2), &data);
                if (result != 0)
                {
                    return false;
                }

                m_winsockReady = true;
                return true;
            }

            bool IsRunning()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_running;
            }

            void CloseSocket()
            {
                SOCKET socket = INVALID_SOCKET;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    socket = m_socket;
                    m_socket = INVALID_SOCKET;
                }

                if (socket != INVALID_SOCKET)
                {
                    shutdown(socket, SD_BOTH);
                    closesocket(socket);
                }
            }

            void PublishState(ConnectionState state)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                PublishStateUnlocked(state);
            }

            void PublishStateUnlocked(ConnectionState state)
            {
                if (m_onConnectionState)
                {
                    m_onConnectionState(state);
                }
            }

            void PublishUpdate(const VariableUpdate& update)
            {
                VariableUpdateCallback callback;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    callback = m_onVariableUpdate;
                }

                if (callback)
                {
                    callback(update);
                }
            }

            ConnectionConfig m_config;
            std::mutex m_mutex;
            bool m_running = false;
            bool m_winsockReady = false;
            SOCKET m_socket = INVALID_SOCKET;
            std::thread m_worker;
            VariableUpdateCallback m_onVariableUpdate;
            ConnectionStateCallback m_onConnectionState;
            std::uint16_t m_nextEntryId = 30000;
            std::map<std::string, EntryMeta> m_keyToEntry;
            std::map<std::uint16_t, std::string> m_idToKey;
        };

        class ReplayDashboardTransport final : public IDashboardTransport
        {
        public:
            explicit ReplayDashboardTransport(ConnectionConfig config)
                : m_config(std::move(config))
            {
            }

            ~ReplayDashboardTransport() override
            {
                Stop();
            }

            bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) override
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_onVariableUpdate = std::move(onVariableUpdate);
                    m_onConnectionState = std::move(onConnectionState);
                    if (m_running)
                    {
                        return true;
                    }
                }

                if (!LoadReplayFile())
                {
                    PublishState(ConnectionState::Disconnected);
                    return false;
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_running = true;
                    m_playing = false;
                    m_playbackRate = 1.0;
                    m_cursorUs = 0;
                    m_nextEventIndex = 0;
                    m_lastTickUs = 0;
                }

                PublishState(ConnectionState::Connected);
                SeekPlaybackUs(0);
                m_worker = std::thread([this]() { RunPlaybackLoop(); });
                return true;
            }

            void Stop() override
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!m_running)
                    {
                        return;
                    }
                    m_running = false;
                    m_playing = false;
                }

                if (m_worker.joinable())
                {
                    m_worker.join();
                }

                PublishState(ConnectionState::Disconnected);
            }

            bool PublishBool(const QString& key, bool value) override
            {
                static_cast<void>(key);
                static_cast<void>(value);
                return false;
            }

            bool PublishDouble(const QString& key, double value) override
            {
                static_cast<void>(key);
                static_cast<void>(value);
                return false;
            }

            bool PublishString(const QString& key, const QString& value) override
            {
                static_cast<void>(key);
                static_cast<void>(value);
                return false;
            }

            bool SupportsPlayback() const override
            {
                return true;
            }

            bool SetPlaybackPlaying(bool isPlaying) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_running)
                {
                    return false;
                }

                m_playing = isPlaying;
                m_lastTickUs = NowSteadyUs();
                return true;
            }

            bool SeekPlaybackUs(std::int64_t cursorUs) override
            {
                std::vector<VariableUpdate> reconstructed;
                std::size_t targetIndex = 0;
                std::int64_t clampedCursorUs = 0;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    clampedCursorUs = std::clamp<std::int64_t>(cursorUs, 0, m_durationUs);
                    targetIndex = FindFirstEventAfterTimeLocked(clampedCursorUs);

                    std::map<std::string, VariableUpdate> latestByKey;

                    std::size_t startIndex = 0;
                    const Checkpoint* checkpoint = FindCheckpointForEventLocked(targetIndex);
                    if (checkpoint != nullptr)
                    {
                        latestByKey = checkpoint->latestByKey;
                        startIndex = checkpoint->eventIndex;
                    }

                    for (std::size_t i = startIndex; i < targetIndex; ++i)
                    {
                        const ReplayEvent& event = m_events[i];
                        if (event.kind != ReplayEventKind::Data)
                        {
                            continue;
                        }
                        latestByKey[event.update.key.toStdString()] = event.update;
                    }

                    reconstructed.reserve(latestByKey.size());
                    for (const auto& [_, update] : latestByKey)
                    {
                        reconstructed.push_back(update);
                    }

                    m_cursorUs = clampedCursorUs;
                    m_nextEventIndex = targetIndex;
                    m_lastTickUs = NowSteadyUs();
                }

                for (const VariableUpdate& update : reconstructed)
                {
                    PublishUpdate(update);
                }

                return true;
            }

            bool SetPlaybackRate(double rate) override
            {
                const double bounded = std::clamp(rate, 0.05, 8.0);
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_running)
                {
                    return false;
                }

                m_playbackRate = bounded;
                m_lastTickUs = NowSteadyUs();
                return true;
            }

            std::int64_t GetPlaybackDurationUs() const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_durationUs;
            }

            std::int64_t GetPlaybackCursorUs() const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_cursorUs;
            }

            bool IsPlaybackPlaying() const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_playing;
            }

            std::vector<PlaybackMarker> GetPlaybackMarkers() const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_markers;
            }

        private:
            enum class ReplayEventKind
            {
                Data,
                ConnectionState,
                Marker
            };

            struct ReplayEvent
            {
                ReplayEventKind kind = ReplayEventKind::Data;
                std::int64_t timestampUs = 0;
                VariableUpdate update;
                PlaybackMarker marker;
            };

            struct Checkpoint
            {
                std::size_t eventIndex = 0;
                std::map<std::string, VariableUpdate> latestByKey;
            };

            static std::int64_t NowSteadyUs()
            {
                const auto now = std::chrono::steady_clock::now();
                const auto epoch = now.time_since_epoch();
                return std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
            }

            bool LoadReplayFile()
            {
                if (m_config.replayFilePath.trimmed().isEmpty())
                {
                    return false;
                }

                QFile file(m_config.replayFilePath);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    return false;
                }

                const QByteArray raw = file.readAll();
                if (raw.trimmed().isEmpty())
                {
                    return false;
                }

                std::vector<ReplayEvent> loaded;
                m_pendingAutoMarkers.clear();
                m_lastAutoMarkerByKey.clear();
                loaded.reserve(4096);

                // First try full-document JSON formats. Capture CLI outputs a
                // single JSON object with metadata/signals, while replay logs may
                // also be stored as a single JSON event object.
                QJsonParseError fullDocError;
                const QJsonDocument fullDoc = QJsonDocument::fromJson(raw, &fullDocError);
                if (fullDocError.error == QJsonParseError::NoError && fullDoc.isObject())
                {
                    const QJsonObject root = fullDoc.object();
                    if (TryParseCaptureSession(root, loaded))
                    {
                        // parsed via capture schema
                    }
                    else
                    {
                        ReplayEvent event;
                        if (ParseReplayEvent(root, event))
                        {
                            loaded.push_back(std::move(event));
                        }
                    }
                }

                // Fallback: line-delimited JSON replay events.
                if (loaded.empty())
                {
                    const QList<QByteArray> lines = raw.split('\n');
                    for (const QByteArray& rawLine : lines)
                    {
                        const QByteArray line = rawLine.trimmed();
                        if (line.isEmpty())
                        {
                            continue;
                        }

                        QJsonParseError parseError;
                        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
                        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
                        {
                            continue;
                        }

                        ReplayEvent event;
                        if (!ParseReplayEvent(doc.object(), event))
                        {
                            continue;
                        }

                        loaded.push_back(std::move(event));
                    }
                }

                if (loaded.empty())
                {
                    return false;
                }

                std::sort(
                    loaded.begin(),
                    loaded.end(),
                    [](const ReplayEvent& lhs, const ReplayEvent& rhs)
                    {
                        return lhs.timestampUs < rhs.timestampUs;
                    }
                );

                std::lock_guard<std::mutex> lock(m_mutex);
                m_events = std::move(loaded);
                m_durationUs = std::max<std::int64_t>(0, m_events.back().timestampUs);
                m_markers.clear();
                for (const ReplayEvent& event : m_events)
                {
                    if (event.kind == ReplayEventKind::ConnectionState || event.kind == ReplayEventKind::Marker)
                    {
                        m_markers.push_back(event.marker);
                    }
                }
                for (const PlaybackMarker& marker : m_pendingAutoMarkers)
                {
                    m_markers.push_back(marker);
                }
                std::sort(
                    m_markers.begin(),
                    m_markers.end(),
                    [](const PlaybackMarker& lhs, const PlaybackMarker& rhs)
                    {
                        return lhs.timestampUs < rhs.timestampUs;
                    }
                );
                m_pendingAutoMarkers.clear();
                BuildCheckpointsLocked();
                return true;
            }

            bool TryParseCaptureSession(const QJsonObject& root, std::vector<ReplayEvent>& loaded)
            {
                if (!root.contains("signals") || !root.value("signals").isArray())
                {
                    return false;
                }

                const QJsonArray signalArray = root.value("signals").toArray();
                if (signalArray.isEmpty())
                {
                    return false;
                }

                std::unordered_map<std::string, std::uint64_t> seqByKey;
                for (const QJsonValue& signalValue : signalArray)
                {
                    if (!signalValue.isObject())
                    {
                        continue;
                    }

                    const QJsonObject signal = signalValue.toObject();
                    const QString key = signal.value("key").toString();
                    const QString type = signal.value("type").toString().trimmed().toLower();
                    const QJsonArray samples = signal.value("samples").toArray();
                    if (key.isEmpty() || samples.isEmpty())
                    {
                        continue;
                    }

                    const std::string keyUtf8 = key.toStdString();
                    for (const QJsonValue& sampleValue : samples)
                    {
                        if (!sampleValue.isObject())
                        {
                            continue;
                        }

                        const QJsonObject sample = sampleValue.toObject();
                        ReplayEvent event;
                        event.kind = ReplayEventKind::Data;
                        event.timestampUs = sample.value("t_us").toVariant().toLongLong();
                        event.update.key = key;
                        event.update.seq = ++seqByKey[keyUtf8];

                        if (type == "bool")
                        {
                            event.update.valueType = static_cast<int>(sd::direct::ValueType::Bool);
                            event.update.value = sample.value("value").toBool();
                        }
                        else if (type == "double")
                        {
                            event.update.valueType = static_cast<int>(sd::direct::ValueType::Double);
                            event.update.value = sample.value("value").toDouble();
                        }
                        else
                        {
                            event.update.valueType = static_cast<int>(sd::direct::ValueType::String);
                            event.update.value = sample.value("value").toVariant().toString();
                        }

                        loaded.push_back(std::move(event));
                    }
                }

                return !loaded.empty();
            }

            bool ParseReplayEvent(const QJsonObject& object, ReplayEvent& event)
            {
                const QString kind = object.value("eventKind").toString("data");
                event.timestampUs = object.value("timestampUs").toVariant().toLongLong();

                if (kind == "connection_state")
                {
                    event.kind = ReplayEventKind::ConnectionState;
                    const QString stateText = object.value("state").toString("Disconnected");
                    event.marker.timestampUs = event.timestampUs;
                    event.marker.kind = ToPlaybackMarkerKindFromState(stateText);
                    event.marker.label = stateText;
                    return true;
                }

                if (kind == "marker")
                {
                    event.kind = ReplayEventKind::Marker;
                    const QString markerType = object.value("markerType").toString(object.value("type").toString("marker"));
                    const QString markerLabel = object.value("label").toString(markerType);
                    event.marker.timestampUs = event.timestampUs;
                    event.marker.kind = ToPlaybackMarkerKindFromMarkerType(markerType);
                    event.marker.label = markerLabel;
                    return true;
                }

                event.kind = ReplayEventKind::Data;
                event.update.key = object.value("key").toString();
                event.update.seq = object.value("seq").toVariant().toULongLong();

                const QVariant typeVariant = object.value("valueType").toVariant();
                if (typeVariant.typeId() == QMetaType::QString)
                {
                    const QString typeText = typeVariant.toString().toLower();
                    if (typeText == "bool")
                    {
                        event.update.valueType = static_cast<int>(sd::direct::ValueType::Bool);
                    }
                    else if (typeText == "double")
                    {
                        event.update.valueType = static_cast<int>(sd::direct::ValueType::Double);
                    }
                    else
                    {
                        event.update.valueType = static_cast<int>(sd::direct::ValueType::String);
                    }
                }
                else
                {
                    event.update.valueType = typeVariant.toInt();
                }

                if (event.update.valueType == static_cast<int>(sd::direct::ValueType::Bool))
                {
                    event.update.value = object.value("value").toBool();
                }
                else if (event.update.valueType == static_cast<int>(sd::direct::ValueType::Double))
                {
                    event.update.value = object.value("value").toDouble();
                }
                else
                {
                    event.update.value = object.value("value").toString();
                }

                bool addAnomalyMarker = false;
                QString anomalyLabel;

                if (object.value("anomaly").toBool(false))
                {
                    addAnomalyMarker = true;
                    anomalyLabel = QString("Anomaly: %1").arg(event.update.key);
                }
                else if (event.update.valueType == static_cast<int>(sd::direct::ValueType::Double))
                {
                    const QString keyLower = event.update.key.toLower();
                    const bool isBrownoutSignal =
                        keyLower.contains("brownout")
                        || keyLower.contains("battery")
                        || keyLower.contains("voltage");
                    const double numericValue = event.update.value.toDouble();
                    if (isBrownoutSignal && numericValue > 0.0 && numericValue < 7.0)
                    {
                        addAnomalyMarker = true;
                        anomalyLabel = QString("Low voltage: %1 = %2V").arg(event.update.key).arg(numericValue, 0, 'f', 2);
                    }
                }

                if (addAnomalyMarker)
                {
                    const std::string key = event.update.key.toStdString();
                    constexpr std::int64_t minSpacingUs = 1000000;
                    const auto it = m_lastAutoMarkerByKey.find(key);
                    const bool isSpaced = (it == m_lastAutoMarkerByKey.end()) || ((event.timestampUs - it->second) >= minSpacingUs);
                    if (isSpaced)
                    {
                        PlaybackMarker marker;
                        marker.timestampUs = event.timestampUs;
                        marker.kind = PlaybackMarkerKind::Anomaly;
                        marker.label = anomalyLabel;
                        m_pendingAutoMarkers.push_back(std::move(marker));
                        m_lastAutoMarkerByKey[key] = event.timestampUs;
                    }
                }

                return !event.update.key.isEmpty();
            }

            void BuildCheckpointsLocked()
            {
                m_checkpoints.clear();

                std::map<std::string, VariableUpdate> latestByKey;
                latestByKey.clear();

                constexpr std::size_t checkpointStride = 1000;
                for (std::size_t i = 0; i < m_events.size(); ++i)
                {
                    const ReplayEvent& event = m_events[i];
                    if (event.kind == ReplayEventKind::Data)
                    {
                        latestByKey[event.update.key.toStdString()] = event.update;
                    }

                    if (i == 0 || (i % checkpointStride) == 0)
                    {
                        Checkpoint cp;
                        cp.eventIndex = i;
                        cp.latestByKey = latestByKey;
                        m_checkpoints.push_back(std::move(cp));
                    }
                }
            }

            const Checkpoint* FindCheckpointForEventLocked(std::size_t eventIndex) const
            {
                if (m_checkpoints.empty())
                {
                    return nullptr;
                }

                const Checkpoint* candidate = nullptr;
                for (const Checkpoint& cp : m_checkpoints)
                {
                    if (cp.eventIndex > eventIndex)
                    {
                        break;
                    }
                    candidate = &cp;
                }

                return candidate;
            }

            std::size_t FindFirstEventAfterTimeLocked(std::int64_t cursorUs) const
            {
                auto it = std::lower_bound(
                    m_events.begin(),
                    m_events.end(),
                    cursorUs,
                    [](const ReplayEvent& event, std::int64_t value)
                    {
                        return event.timestampUs < value;
                    }
                );

                return static_cast<std::size_t>(std::distance(m_events.begin(), it));
            }

            bool IsRunningLocked() const
            {
                return m_running;
            }

            void RunPlaybackLoop()
            {
                while (true)
                {
                    std::vector<VariableUpdate> updates;
                    bool shouldSleep = true;

                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (!IsRunningLocked())
                        {
                            break;
                        }

                        if (!m_playing)
                        {
                            m_lastTickUs = NowSteadyUs();
                        }
                        else
                        {
                            const std::int64_t nowUs = NowSteadyUs();
                            const std::int64_t deltaUs = std::max<std::int64_t>(0, nowUs - m_lastTickUs);
                            m_lastTickUs = nowUs;
                            const std::int64_t playbackDeltaUs = static_cast<std::int64_t>(static_cast<double>(deltaUs) * m_playbackRate);

                            const std::int64_t oldCursor = m_cursorUs;
                            m_cursorUs = std::clamp<std::int64_t>(m_cursorUs + playbackDeltaUs, 0, m_durationUs);
                            const std::size_t endIndex = FindFirstEventAfterTimeLocked(m_cursorUs);

                            while (m_nextEventIndex < endIndex && m_nextEventIndex < m_events.size())
                            {
                                const ReplayEvent& event = m_events[m_nextEventIndex];
                                if (event.kind == ReplayEventKind::Data && event.timestampUs >= oldCursor)
                                {
                                    updates.push_back(event.update);
                                }
                                ++m_nextEventIndex;
                            }

                            if (m_cursorUs >= m_durationUs)
                            {
                                m_playing = false;
                            }

                            shouldSleep = updates.empty();
                        }
                    }

                    for (const VariableUpdate& update : updates)
                    {
                        PublishUpdate(update);
                    }

                    if (shouldSleep)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    }
                }
            }

            void PublishState(ConnectionState state)
            {
                ConnectionStateCallback callback;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    callback = m_onConnectionState;
                }

                if (callback)
                {
                    callback(state);
                }
            }

            void PublishUpdate(const VariableUpdate& update)
            {
                VariableUpdateCallback callback;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    callback = m_onVariableUpdate;
                }

                if (callback)
                {
                    callback(update);
                }
            }

            ConnectionConfig m_config;
            mutable std::mutex m_mutex;
            bool m_running = false;
            bool m_playing = false;
            double m_playbackRate = 1.0;
            std::int64_t m_durationUs = 0;
            std::int64_t m_cursorUs = 0;
            std::int64_t m_lastTickUs = 0;
            std::size_t m_nextEventIndex = 0;
            std::vector<ReplayEvent> m_events;
            std::vector<PlaybackMarker> m_markers;
            std::vector<PlaybackMarker> m_pendingAutoMarkers;
            std::map<std::string, std::int64_t> m_lastAutoMarkerByKey;
            std::vector<Checkpoint> m_checkpoints;
            std::thread m_worker;
            VariableUpdateCallback m_onVariableUpdate;
            ConnectionStateCallback m_onConnectionState;
        };
    }

    QString ToDisplayString(TransportKind kind)
    {
        switch (kind)
        {
            case TransportKind::Replay:
                return "Replay";
            case TransportKind::NetworkTables:
                return "NetworkTables";
            case TransportKind::Direct:
            default:
                return "Direct";
        }
    }

    std::unique_ptr<IDashboardTransport> CreateDashboardTransport(const ConnectionConfig& config)
    {
        if (config.kind == TransportKind::Replay)
        {
            return std::make_unique<ReplayDashboardTransport>(config);
        }

        if (config.kind == TransportKind::NetworkTables)
        {
            return std::make_unique<NetworkTablesDashboardTransport>(config);
        }

        return std::make_unique<DirectDashboardTransport>();
    }
}
