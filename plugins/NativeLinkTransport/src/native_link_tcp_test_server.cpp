#include "native_link_tcp_test_server.h"

#include "native_link_tcp_protocol.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sd::nativelink::testsupport
{
    namespace
    {
        class WinsockRuntime
        {
        public:
            WinsockRuntime()
            {
                WSADATA data {};
                m_started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }

            ~WinsockRuntime()
            {
                if (m_started)
                {
                    WSACleanup();
                }
            }

            bool IsStarted() const
            {
                return m_started;
            }

        private:
            bool m_started = false;
        };

        WinsockRuntime& GetWinsockRuntime()
        {
            static WinsockRuntime runtime;
            return runtime;
        }

        std::uint64_t GetSteadyNowUs()
        {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        void CopyUtf8(char* dest, std::size_t destCount, const std::string& value)
        {
            if (dest == nullptr || destCount == 0)
            {
                return;
            }

            const std::size_t maxCopy = destCount - 1;
            const std::size_t copyCount = (std::min)(maxCopy, value.size());
            if (copyCount > 0)
            {
                memcpy(dest, value.data(), copyCount);
            }
            dest[copyCount] = '\0';
        }

        std::string ReadUtf8(const char* src, std::size_t srcCount)
        {
            if (src == nullptr || srcCount == 0)
            {
                return {};
            }

            const char* end = static_cast<const char*>(memchr(src, '\0', srcCount));
            if (end == nullptr)
            {
                return std::string(src, src + srcCount);
            }

            return std::string(src, end);
        }

        std::vector<unsigned char> SerializeValue(const TopicValue& value)
        {
            std::vector<unsigned char> bytes;
            switch (value.type)
            {
                case ValueType::Bool:
                    bytes.resize(1);
                    bytes[0] = value.boolValue ? 1 : 0;
                    break;
                case ValueType::Double:
                    bytes.resize(sizeof(double));
                    memcpy(bytes.data(), &value.doubleValue, sizeof(double));
                    break;
                case ValueType::String:
                    bytes.assign(value.stringValue.begin(), value.stringValue.end());
                    break;
                case ValueType::StringArray:
                    for (const std::string& item : value.stringArrayValue)
                    {
                        const std::uint32_t len = static_cast<std::uint32_t>(item.size());
                        const unsigned char* rawLen = reinterpret_cast<const unsigned char*>(&len);
                        bytes.insert(bytes.end(), rawLen, rawLen + sizeof(std::uint32_t));
                        bytes.insert(bytes.end(), item.begin(), item.end());
                    }
                    break;
            }
            return bytes;
        }

        bool DeserializeValue(ValueType type, const unsigned char* bytes, std::size_t byteCount, TopicValue& outValue)
        {
            outValue = TopicValue();
            outValue.type = type;

            switch (type)
            {
                case ValueType::Bool:
                    if (byteCount < 1)
                    {
                        return false;
                    }
                    outValue.boolValue = bytes[0] != 0;
                    return true;
                case ValueType::Double:
                    if (byteCount < sizeof(double))
                    {
                        return false;
                    }
                    memcpy(&outValue.doubleValue, bytes, sizeof(double));
                    return true;
                case ValueType::String:
                    outValue.stringValue.assign(reinterpret_cast<const char*>(bytes), reinterpret_cast<const char*>(bytes) + byteCount);
                    return true;
                case ValueType::StringArray:
                {
                    std::size_t offset = 0;
                    while (offset + sizeof(std::uint32_t) <= byteCount)
                    {
                        std::uint32_t len = 0;
                        memcpy(&len, bytes + offset, sizeof(std::uint32_t));
                        offset += sizeof(std::uint32_t);
                        if (offset + len > byteCount)
                        {
                            return false;
                        }
                        outValue.stringArrayValue.emplace_back(
                            reinterpret_cast<const char*>(bytes + offset),
                            reinterpret_cast<const char*>(bytes + offset + len)
                        );
                        offset += len;
                    }
                    return offset == byteCount;
                }
            }

            return false;
        }

        bool SendAll(SOCKET socketHandle, const char* bytes, int totalBytes)
        {
            int sent = 0;
            while (sent < totalBytes)
            {
                const int chunk = send(socketHandle, bytes + sent, totalBytes - sent, 0);
                if (chunk == SOCKET_ERROR || chunk == 0)
                {
                    return false;
                }
                sent += chunk;
            }
            return true;
        }

        // Ian: Plain blocking RecvAll is only used for the initial ClientHello
        // handshake, where the client is expected to respond promptly. Post-handshake
        // use RecvAllWithTimeout so that a silently-dead client (no FIN/RST) does not
        // hang its per-client thread indefinitely — important for the disconnect-stress
        // harness which kills the dashboard process without a graceful socket close.
        bool RecvAll(SOCKET socketHandle, char* bytes, int totalBytes)
        {
            int received = 0;
            while (received < totalBytes)
            {
                const int chunk = recv(socketHandle, bytes + received, totalBytes - received, 0);
                if (chunk == SOCKET_ERROR || chunk == 0)
                {
                    return false;
                }
                received += chunk;
            }
            return true;
        }

        enum class ReceiveStatus
        {
            Success,
            Timeout,
            Closed,
            Error
        };

        // Ian: kClientRecvTimeoutMs bounds how long the per-client thread can
        // block waiting for the next frame header. When the client process
        // disappears without sending a FIN (e.g. process kill during stress
        // testing), the OS will eventually report an error on the socket, but
        // Windows TCP keep-alive probes take tens of seconds by default. This
        // explicit timeout lets the per-client thread notice the dead client
        // within ~2 s and clean up the slot, making the server side reconnect-
        // safe without requiring OS-level keep-alive tuning.
        static constexpr std::uint32_t kClientRecvTimeoutMs = 2000;

        ReceiveStatus RecvAllWithTimeout(SOCKET socketHandle, char* bytes, int totalBytes)
        {
            int received = 0;
            while (received < totalBytes)
            {
                fd_set readSet {};
                FD_ZERO(&readSet);
                FD_SET(socketHandle, &readSet);

                timeval timeout {};
                timeout.tv_sec  = static_cast<long>(kClientRecvTimeoutMs / 1000);
                timeout.tv_usec = static_cast<long>((kClientRecvTimeoutMs % 1000) * 1000);

                const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
                if (ready == 0)
                {
                    return ReceiveStatus::Timeout;
                }
                if (ready == SOCKET_ERROR)
                {
                    return ReceiveStatus::Error;
                }

                const int chunk = recv(socketHandle, bytes + received, totalBytes - received, 0);
                if (chunk == 0)
                {
                    return ReceiveStatus::Closed;
                }
                if (chunk == SOCKET_ERROR)
                {
                    return ReceiveStatus::Error;
                }

                received += chunk;
            }
            return ReceiveStatus::Success;
        }

        TopicDescriptor MakeDescriptor(
            const std::string& path,
            TopicKind kind,
            ValueType type,
            WriterPolicy writerPolicy,
            bool retained,
            bool replayOnSubscribe)
        {
            TopicDescriptor descriptor;
            descriptor.topicPath = path;
            descriptor.topicKind = kind;
            descriptor.valueType = type;
            descriptor.writerPolicy = writerPolicy;
            descriptor.retentionMode = retained ? RetentionMode::LatestValue : RetentionMode::None;
            descriptor.replayOnSubscribe = replayOnSubscribe;
            return descriptor;
        }
    }

    struct NativeLinkTcpTestServer::Impl
    {
        struct ClientSlot
        {
            SOCKET socketHandle = INVALID_SOCKET;
            std::string clientId;
            std::atomic<bool> alive = false;
            std::atomic<std::uint64_t> lastHeartbeatUs = 0;
            std::thread thread;
        };

        Impl(std::string inChannelId, std::uint16_t inPort, std::string inHost)
            : channelId(std::move(inChannelId))
            , port(inPort)
            , host(std::move(inHost))
        {
        }

        ~Impl()
        {
            Stop();
        }

        std::string channelId;
        std::uint16_t port = 5810;
        std::string host = "127.0.0.1";
        mutable std::mutex mutex;
        NativeLinkCore core;
        SOCKET listenSocket = INVALID_SOCKET;
        std::thread acceptThread;
        std::thread heartbeatThread;
        std::atomic<bool> running = false;
        std::atomic<bool> stopRequested = false;
        std::vector<std::unique_ptr<ClientSlot>> clients;

        bool Start()
        {
            if (running.load())
            {
                return true;
            }

            if (!GetWinsockRuntime().IsStarted())
            {
                return false;
            }

            listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listenSocket == INVALID_SOCKET)
            {
                return false;
            }

            const BOOL reuse = 1;
            setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            if (InetPtonA(AF_INET, host.c_str(), &address.sin_addr) != 1)
            {
                Stop();
                return false;
            }

            if (bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
            {
                Stop();
                return false;
            }

            if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
            {
                Stop();
                return false;
            }

            core.BeginNewSession();
            RegisterDefaultTopicsIfNeeded();

            stopRequested.store(false);
            running.store(true);
            acceptThread = std::thread(&Impl::AcceptLoop, this);
            heartbeatThread = std::thread(&Impl::HeartbeatLoop, this);
            return true;
        }

        void Stop()
        {
            stopRequested.store(true);
            running.store(false);

            if (listenSocket != INVALID_SOCKET)
            {
                shutdown(listenSocket, SD_BOTH);
                closesocket(listenSocket);
                listenSocket = INVALID_SOCKET;
            }

            if (acceptThread.joinable())
            {
                acceptThread.join();
            }
            if (heartbeatThread.joinable())
            {
                heartbeatThread.join();
            }

            for (std::unique_ptr<ClientSlot>& client : clients)
            {
                if (client->socketHandle != INVALID_SOCKET)
                {
                    shutdown(client->socketHandle, SD_BOTH);
                    closesocket(client->socketHandle);
                    client->socketHandle = INVALID_SOCKET;
                }
                if (client->thread.joinable())
                {
                    client->thread.join();
                }
            }
            clients.clear();
        }

        void RegisterDefaultDashboardTopics()
        {
            std::lock_guard<std::mutex> lock(mutex);
            RegisterDefaultTopicsIfNeeded();
        }

        bool PublishBoolean(const std::string& topicPath, bool value)
        {
            return PublishFromServer(topicPath, TopicValue::Bool(value));
        }

        bool PublishDouble(const std::string& topicPath, double value)
        {
            return PublishFromServer(topicPath, TopicValue::Double(value));
        }

        bool PublishString(const std::string& topicPath, const std::string& value)
        {
            return PublishFromServer(topicPath, TopicValue::String(value));
        }

        bool PublishStringArray(const std::string& topicPath, const std::vector<std::string>& value)
        {
            return PublishFromServer(topicPath, TopicValue::StringArray(value));
        }

        bool TryGetLatestValue(const std::string& topicPath, TopicValue& outValue) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return core.TryGetLatestValue(topicPath, outValue);
        }

        void RestartSession()
        {
            std::lock_guard<std::mutex> lock(mutex);
            core.BeginNewSession();
            for (const std::unique_ptr<ClientSlot>& client : clients)
            {
                if (client->alive.load())
                {
                    PublishSnapshotLocked(*client);
                }
            }
        }

        std::uint64_t GetServerSessionId() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return core.GetServerSessionId();
        }

        void RegisterDefaultTopicsIfNeeded()
        {
            if (core.IsTopicRegistered("Test/Auton_Selection/AutoChooser/selected"))
            {
                return;
            }

            core.RegisterTopic(MakeDescriptor("Test/Auton_Selection/AutoChooser/options", TopicKind::State, ValueType::StringArray, WriterPolicy::ServerOnly, true, true));
            core.RegisterTopic(MakeDescriptor("Test/Auton_Selection/AutoChooser/default", TopicKind::State, ValueType::String, WriterPolicy::ServerOnly, true, true));
            core.RegisterTopic(MakeDescriptor("Test/Auton_Selection/AutoChooser/active", TopicKind::State, ValueType::String, WriterPolicy::ServerOnly, true, true));
            core.RegisterTopic(MakeDescriptor("Test/Auton_Selection/AutoChooser/selected", TopicKind::State, ValueType::String, WriterPolicy::LeaseSingleWriter, true, true));
            core.RegisterTopic(MakeDescriptor("TestMove", TopicKind::State, ValueType::Double, WriterPolicy::LeaseSingleWriter, true, true));
            core.RegisterTopic(MakeDescriptor("Timer", TopicKind::State, ValueType::Double, WriterPolicy::ServerOnly, true, true));
            core.RegisterTopic(MakeDescriptor("Y_ft", TopicKind::State, ValueType::Double, WriterPolicy::ServerOnly, true, true));

            core.PublishFromServer("Test/Auton_Selection/AutoChooser/options", TopicValue::StringArray({
                "Do Nothing", "Just Move Forward", "Just Rotate", "Move Rotate Sequence", "Box Waypoints", "Smart Waypoints"
            }));
            core.PublishFromServer("Test/Auton_Selection/AutoChooser/default", TopicValue::String("Do Nothing"));
            core.PublishFromServer("Test/Auton_Selection/AutoChooser/active", TopicValue::String("Do Nothing"));
            core.PublishFromServer("Test/Auton_Selection/AutoChooser/selected", TopicValue::String("Do Nothing"));
            core.PublishFromServer("TestMove", TopicValue::Double(0.0));
            core.PublishFromServer("Timer", TopicValue::Double(0.0));
            core.PublishFromServer("Y_ft", TopicValue::Double(0.0));
        }

        bool PublishFromServer(const std::string& topicPath, const TopicValue& value)
        {
            std::lock_guard<std::mutex> lock(mutex);
            const WriteResult result = core.PublishFromServer(topicPath, value);
            if (!result.accepted)
            {
                return false;
            }

            const NativeLinkCore::TopicRuntime* topic = core.LookupTopic(topicPath);
            TopicValue latest;
            if (topic == nullptr || !core.TryGetLatestValue(topicPath, latest))
            {
                return false;
            }

            UpdateEnvelope envelope;
            envelope.serverSessionId = core.GetServerSessionId();
            envelope.serverSequence = result.serverSequence;
            envelope.topicId = topic->topicId;
            envelope.topicPath = topicPath;
            envelope.sourceClientId = "server";
            envelope.value = latest;
            envelope.deliveryKind = topic->descriptor.topicKind == TopicKind::Command
                ? DeliveryKind::LiveCommand
                : DeliveryKind::LiveState;
            PublishEnvelopeToAllClientsLocked(envelope);
            return true;
        }

        void AcceptLoop()
        {
            while (!stopRequested.load())
            {
                SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
                if (clientSocket == INVALID_SOCKET)
                {
                    break;
                }

                auto client = std::make_unique<ClientSlot>();
                client->socketHandle = clientSocket;
                client->alive.store(true);
                client->lastHeartbeatUs.store(GetSteadyNowUs());
                client->thread = std::thread(&Impl::ClientLoop, this, client.get());

                std::lock_guard<std::mutex> lock(mutex);
                clients.push_back(std::move(client));
            }
        }

        void HeartbeatLoop()
        {
            while (!stopRequested.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> lock(mutex);
                tcp::HeartbeatPayload heartbeat {};
                heartbeat.serverSessionId = core.GetServerSessionId();
                for (const std::unique_ptr<ClientSlot>& client : clients)
                {
                    if (!client->alive.load() || client->socketHandle == INVALID_SOCKET)
                    {
                        continue;
                    }
                    SendFrame(client->socketHandle, tcp::FrameKind::Heartbeat, &heartbeat, sizeof(heartbeat));
                }
            }
        }

        void ClientLoop(ClientSlot* client)
        {
            tcp::FrameHeader header {};
            if (!RecvAll(client->socketHandle, reinterpret_cast<char*>(&header), static_cast<int>(sizeof(header))))
            {
                client->alive.store(false);
                return;
            }
            if (header.magic != tcp::kFrameMagic || header.version != tcp::kFrameVersion || header.kind != static_cast<std::uint16_t>(tcp::FrameKind::ClientHello) || header.payloadBytes != sizeof(tcp::HelloPayload))
            {
                client->alive.store(false);
                return;
            }

            tcp::HelloPayload hello {};
            if (!RecvAll(client->socketHandle, reinterpret_cast<char*>(&hello), static_cast<int>(sizeof(hello))))
            {
                client->alive.store(false);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                client->clientId = ReadUtf8(hello.clientId, sizeof(hello.clientId));

                // Ian: The TCP carrier inherits the same authority-owned session
                // contract as SHM. Finish the hello first, then immediately send
                // the current server generation and snapshot so the medium does not
                // invent a different bootstrap story.
                tcp::ServerHelloPayload serverHello {};
                serverHello.serverSessionId = core.GetServerSessionId();
                SendFrame(client->socketHandle, tcp::FrameKind::ServerHello, &serverHello, sizeof(serverHello));
                PublishSnapshotLocked(*client);
            }

            while (!stopRequested.load() && client->alive.load())
            {
                tcp::FrameHeader frameHeader {};
                const ReceiveStatus headerStatus = RecvAllWithTimeout(
                    client->socketHandle,
                    reinterpret_cast<char*>(&frameHeader),
                    static_cast<int>(sizeof(frameHeader))
                );
                if (headerStatus == ReceiveStatus::Timeout)
                {
                    // Ian: Timeout on the server-side recv means the client has
                    // gone quiet — likely a crashed/killed process.  Loop and
                    // check stopRequested; if still running this slot will
                    // eventually also fail on error or closed when the OS
                    // delivers the RST once the remote side's port is reused.
                    continue;
                }
                if (headerStatus != ReceiveStatus::Success)
                {
                    break;
                }
                if (frameHeader.magic != tcp::kFrameMagic || frameHeader.version != tcp::kFrameVersion || frameHeader.payloadBytes > tcp::kMaxFramePayloadBytes)
                {
                    break;
                }

                std::vector<unsigned char> payload(frameHeader.payloadBytes);
                if (frameHeader.payloadBytes > 0)
                {
                    const ReceiveStatus payloadStatus = RecvAllWithTimeout(
                        client->socketHandle,
                        reinterpret_cast<char*>(payload.data()),
                        static_cast<int>(payload.size())
                    );
                    if (payloadStatus != ReceiveStatus::Success)
                    {
                        break;
                    }
                }

                client->lastHeartbeatUs.store(GetSteadyNowUs());

                if (frameHeader.kind == static_cast<std::uint16_t>(tcp::FrameKind::Heartbeat))
                {
                    continue;
                }

                if (frameHeader.kind == static_cast<std::uint16_t>(tcp::FrameKind::ClientPublish)
                    && payload.size() == sizeof(ipc::SharedMessage))
                {
                    ipc::SharedMessage message {};
                    memcpy(&message, payload.data(), sizeof(message));
                    std::lock_guard<std::mutex> lock(mutex);
                    HandleClientWriteLocked(*client, message);
                    continue;
                }

                break;
            }

            client->alive.store(false);
            if (client->socketHandle != INVALID_SOCKET)
            {
                shutdown(client->socketHandle, SD_BOTH);
                closesocket(client->socketHandle);
                client->socketHandle = INVALID_SOCKET;
            }
        }

        void PublishSnapshotLocked(ClientSlot& client)
        {
            const std::vector<SnapshotEvent> snapshot = core.ConnectClient(client.clientId).snapshotEvents;
            for (const SnapshotEvent& event : snapshot)
            {
                // Ian: The TCP test harness intentionally emits the same
                // snapshot sentinels the real SHM path uses. Keeping the test
                // authority on the shared semantic contract makes carrier bugs
                // easier to distinguish from protocol/ordering bugs.
                UpdateEnvelope envelope;
                if (event.kind == SnapshotEventKind::Update && event.hasUpdate)
                {
                    envelope = event.update;
                }
                else
                {
                    envelope.serverSessionId = core.GetServerSessionId();
                    envelope.sourceClientId = "server";
                    envelope.value = TopicValue::String(std::string());
                    envelope.deliveryKind = DeliveryKind::LiveEvent;
                    switch (event.kind)
                    {
                        case SnapshotEventKind::DescriptorSnapshotBegin:
                            envelope.topicId = ipc::kSnapshotStartTopicId;
                            envelope.topicPath = "__snapshot_begin__";
                            break;
                        case SnapshotEventKind::DescriptorSnapshotEnd:
                            envelope.topicId = ipc::kSnapshotEndTopicId;
                            envelope.topicPath = "__descriptor_end__";
                            break;
                        case SnapshotEventKind::StateSnapshotBegin:
                            envelope.topicId = ipc::kSnapshotStartTopicId;
                            envelope.topicPath = "__state_begin__";
                            break;
                        case SnapshotEventKind::StateSnapshotEnd:
                            envelope.topicId = ipc::kSnapshotEndTopicId;
                            envelope.topicPath = "__state_end__";
                            break;
                        case SnapshotEventKind::LiveBegin:
                            envelope.topicId = ipc::kLiveBeginTopicId;
                            envelope.topicPath = "__live_begin__";
                            break;
                        case SnapshotEventKind::Descriptor:
                            envelope.topicPath = event.descriptor.topicPath;
                            envelope.value = TopicValue::String(event.descriptor.topicPath);
                            break;
                        default:
                            break;
                    }
                }

                SendEnvelopeLocked(client, envelope);
            }
        }

        void HandleClientWriteLocked(ClientSlot& client, const ipc::SharedMessage& message)
        {
            TopicValue value;
            if (!DeserializeValue(static_cast<ValueType>(message.valueType), message.payload, static_cast<std::size_t>(message.flags), value))
            {
                return;
            }

            if (!core.GetTopicLeaseInfo(message.topicPath).hasLeaseHolder)
            {
                core.AcquireLease(message.topicPath, client.clientId);
            }

            const WriteResult result = core.Publish(message.topicPath, value, client.clientId);
            for (const UpdateEnvelope& clientEvent : core.DrainClientEvents(client.clientId))
            {
                SendEnvelopeLocked(client, clientEvent);
            }

            if (!result.accepted)
            {
                return;
            }

            const NativeLinkCore::TopicRuntime* topic = core.LookupTopic(message.topicPath);
            TopicValue latest;
            if (topic == nullptr || !core.TryGetLatestValue(message.topicPath, latest))
            {
                return;
            }

            UpdateEnvelope live;
            live.serverSessionId = core.GetServerSessionId();
            live.serverSequence = result.serverSequence;
            live.topicId = topic->topicId;
            live.topicPath = message.topicPath;
            live.sourceClientId = client.clientId;
            live.value = latest;
            live.deliveryKind = topic->descriptor.topicKind == TopicKind::Command
                ? DeliveryKind::LiveCommand
                : DeliveryKind::LiveState;
            PublishEnvelopeToAllClientsLocked(live);
        }

        void PublishEnvelopeToAllClientsLocked(const UpdateEnvelope& envelope)
        {
            for (const std::unique_ptr<ClientSlot>& client : clients)
            {
                if (client->alive.load())
                {
                    SendEnvelopeLocked(*client, envelope);
                }
            }
        }

        void SendEnvelopeLocked(ClientSlot& client, const UpdateEnvelope& envelope)
        {
            ipc::SharedMessage message {};
            message.size = sizeof(message);
            message.topicId = static_cast<std::uint32_t>(envelope.topicId);
            message.deliveryKind = static_cast<std::uint32_t>(envelope.deliveryKind);
            message.valueType = static_cast<std::uint32_t>(envelope.value.type);
            message.serverSessionId = envelope.serverSessionId;
            message.serverSequence = envelope.serverSequence;
            CopyUtf8(message.topicPath, sizeof(message.topicPath), envelope.topicPath);
            CopyUtf8(message.sourceClientId, sizeof(message.sourceClientId), envelope.sourceClientId);

            const std::vector<unsigned char> payload = SerializeValue(envelope.value);
            const std::size_t payloadBytes = (std::min<std::size_t>)(payload.size(), sizeof(message.payload));
            if (payloadBytes > 0)
            {
                memcpy(message.payload, payload.data(), payloadBytes);
            }
            message.flags = static_cast<std::uint64_t>(payloadBytes);

            SendFrame(client.socketHandle, tcp::FrameKind::ServerMessage, &message, sizeof(message));
        }

        bool SendFrame(SOCKET socketHandle, tcp::FrameKind kind, const void* payload, std::uint32_t payloadBytes)
        {
            tcp::FrameHeader header {};
            header.kind = static_cast<std::uint16_t>(kind);
            header.payloadBytes = payloadBytes;
            if (!SendAll(socketHandle, reinterpret_cast<const char*>(&header), static_cast<int>(sizeof(header))))
            {
                return false;
            }
            if (payloadBytes > 0 && payload != nullptr)
            {
                if (!SendAll(socketHandle, reinterpret_cast<const char*>(payload), static_cast<int>(payloadBytes)))
                {
                    return false;
                }
            }
            return true;
        }
    };

    NativeLinkTcpTestServer::NativeLinkTcpTestServer(std::string channelId, std::uint16_t port, std::string host)
        : m_impl(std::make_unique<Impl>(std::move(channelId), port, std::move(host)))
    {
    }

    NativeLinkTcpTestServer::~NativeLinkTcpTestServer() = default;

    bool NativeLinkTcpTestServer::Start()
    {
        return m_impl != nullptr && m_impl->Start();
    }

    void NativeLinkTcpTestServer::Stop()
    {
        if (m_impl != nullptr)
        {
            m_impl->Stop();
        }
    }

    void NativeLinkTcpTestServer::RegisterDefaultDashboardTopics()
    {
        if (m_impl != nullptr)
        {
            m_impl->RegisterDefaultDashboardTopics();
        }
    }

    bool NativeLinkTcpTestServer::PublishBoolean(const std::string& topicPath, bool value)
    {
        return m_impl != nullptr && m_impl->PublishBoolean(topicPath, value);
    }

    bool NativeLinkTcpTestServer::PublishDouble(const std::string& topicPath, double value)
    {
        return m_impl != nullptr && m_impl->PublishDouble(topicPath, value);
    }

    bool NativeLinkTcpTestServer::PublishString(const std::string& topicPath, const std::string& value)
    {
        return m_impl != nullptr && m_impl->PublishString(topicPath, value);
    }

    bool NativeLinkTcpTestServer::PublishStringArray(const std::string& topicPath, const std::vector<std::string>& value)
    {
        return m_impl != nullptr && m_impl->PublishStringArray(topicPath, value);
    }

    bool NativeLinkTcpTestServer::TryGetLatestValue(const std::string& topicPath, TopicValue& outValue) const
    {
        return m_impl != nullptr && m_impl->TryGetLatestValue(topicPath, outValue);
    }

    void NativeLinkTcpTestServer::RestartSession()
    {
        if (m_impl != nullptr)
        {
            m_impl->RestartSession();
        }
    }

    std::uint64_t NativeLinkTcpTestServer::GetServerSessionId() const
    {
        return m_impl != nullptr ? m_impl->GetServerSessionId() : 0;
    }
}
