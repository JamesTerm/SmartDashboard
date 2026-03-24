#include "native_link_tcp_client.h"

#include "native_link_ipc_protocol.h"
#include "native_link_tcp_protocol.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sd::nativelink
{
    namespace
    {
        constexpr int kConnectionStateDisconnected = 0;
        constexpr int kConnectionStateConnecting = 1;
        constexpr int kConnectionStateConnected = 2;
        constexpr int kConnectionStateStale = 3;

        enum class ReceiveStatus
        {
            Success,
            Timeout,
            Closed,
            Error
        };

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

        UpdateEnvelope MessageToEnvelope(const ipc::SharedMessage& message)
        {
            UpdateEnvelope event;
            event.serverSessionId = message.serverSessionId;
            event.serverSequence = message.serverSequence;
            event.topicId = message.topicId;
            event.topicPath = ReadUtf8(message.topicPath, sizeof(message.topicPath));
            event.sourceClientId = ReadUtf8(message.sourceClientId, sizeof(message.sourceClientId));
            event.deliveryKind = static_cast<DeliveryKind>(message.deliveryKind);
            event.rejectionReason = WriteRejectReason::None;
            DeserializeValue(
                static_cast<ValueType>(message.valueType),
                message.payload,
                static_cast<std::size_t>(message.flags),
                event.value
            );
            return event;
        }

        bool IsSentinelTopic(std::uint32_t topicId)
        {
            return topicId == ipc::kSnapshotStartTopicId
                || topicId == ipc::kSnapshotEndTopicId
                || topicId == ipc::kLiveBeginTopicId;
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

        ReceiveStatus RecvAllWithTimeout(SOCKET socketHandle, char* bytes, int totalBytes, std::uint32_t timeoutMs)
        {
            int received = 0;
            while (received < totalBytes)
            {
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(socketHandle, &readSet);

                timeval timeout {};
                timeout.tv_sec = static_cast<long>(timeoutMs / 1000);
                timeout.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);

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
    }

    // Ian: kConnectTimeoutMs bounds the blocking connect() so the UI thread
    // (or worker thread) can't stall for the OS default (~20 s on Windows)
    // when the authority is not yet up. Using select() on a non-blocking
    // socket gives us a timeout without spawning an extra thread.
    constexpr std::uint32_t kConnectTimeoutMs = 3000;

    struct NativeLinkTcpClient::Impl
    {
        enum class SnapshotPhase
        {
            Descriptors,
            StateSnapshot,
            Live
        };

        NativeLinkClientConfig config;
        NativeLinkUpdateCallback onUpdate;
        NativeLinkConnectionStateCallback onConnectionState;
        SOCKET socketHandle = INVALID_SOCKET;
        std::thread worker;
        std::thread heartbeatWorker;
        std::atomic<bool> running = false;
        std::atomic<bool> stopRequested = false;
        std::atomic<bool> connected = false;
        std::atomic<std::uint64_t> serverSessionId = 0;
        std::atomic<std::uint64_t> lastHeartbeatUs = 0;
        std::mutex sendMutex;
        SnapshotPhase snapshotPhase = SnapshotPhase::Descriptors;

        ~Impl()
        {
            Stop();
        }

        bool Start(const NativeLinkClientConfig& inConfig, NativeLinkUpdateCallback updateCallback, NativeLinkConnectionStateCallback connectionCallback)
        {
            Stop();

            if (!GetWinsockRuntime().IsStarted())
            {
                return false;
            }

            config = inConfig;
            onUpdate = std::move(updateCallback);
            onConnectionState = std::move(connectionCallback);
            snapshotPhase = SnapshotPhase::Descriptors;
            lastHeartbeatUs.store(0, std::memory_order_release);
            serverSessionId.store(0, std::memory_order_release);

            running.store(true, std::memory_order_release);
            stopRequested.store(false, std::memory_order_release);
            worker = std::thread(&Impl::RunLoop, this);
            heartbeatWorker = std::thread(&Impl::HeartbeatLoop, this);
            return true;
        }

        void Stop()
        {
            stopRequested.store(true, std::memory_order_release);
            running.store(false, std::memory_order_release);
            ShutdownSocket();

            if (worker.joinable())
            {
                worker.join();
            }
            if (heartbeatWorker.joinable())
            {
                heartbeatWorker.join();
            }

            if (connected.exchange(false) && onConnectionState)
            {
                onConnectionState(kConnectionStateDisconnected);
            }

            CloseSocket();
            onUpdate = {};
            onConnectionState = {};
            snapshotPhase = SnapshotPhase::Descriptors;
            lastHeartbeatUs.store(0, std::memory_order_release);
            serverSessionId.store(0, std::memory_order_release);
        }

        bool Publish(const std::string& topicPath, const TopicValue& value)
        {
            if (!connected.load(std::memory_order_acquire) || socketHandle == INVALID_SOCKET)
            {
                return false;
            }

            ipc::SharedMessage message {};
            message.size = sizeof(message);
            message.valueType = static_cast<std::uint32_t>(value.type);
            CopyUtf8(message.topicPath, sizeof(message.topicPath), topicPath);
            CopyUtf8(message.sourceClientId, sizeof(message.sourceClientId), config.clientId);

            const std::vector<unsigned char> payload = SerializeValue(value);
            const std::size_t payloadBytes = (std::min<std::size_t>)(payload.size(), sizeof(message.payload));
            if (payloadBytes > 0)
            {
                memcpy(message.payload, payload.data(), payloadBytes);
            }
            message.flags = static_cast<std::uint64_t>(payloadBytes);

            return SendFrame(tcp::FrameKind::ClientPublish, &message, sizeof(message));
        }

        bool IsConnected() const
        {
            return connected.load(std::memory_order_acquire);
        }

        // Ian: TryConnect creates a fresh non-blocking socket, drives it to
        // connected state via select() with kConnectTimeoutMs, then sends the
        // ClientHello handshake. Returns true only when the handshake send
        // succeeds. Caller (RunLoop) retains ownership of the socket via
        // socketHandle; on failure the socket is closed before returning.
        // Non-blocking connect lets Stop() interrupt via ShutdownSocket()
        // (which calls shutdown(SD_BOTH)) even before the connect completes —
        // shutdown on a not-yet-connected socket returns SOCKET_ERROR which
        // is harmless but the worker will also see stopRequested == true and
        // exit the outer loop without retrying.
        bool TryConnect()
        {
            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET)
            {
                return false;
            }

            // Switch to non-blocking so connect() returns immediately.
            u_long nonBlocking = 1;
            if (ioctlsocket(sock, FIONBIO, &nonBlocking) != 0)
            {
                closesocket(sock);
                return false;
            }

            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_port = htons(config.port);
            if (InetPtonA(AF_INET, config.host.c_str(), &address.sin_addr) != 1)
            {
                closesocket(sock);
                return false;
            }

            // Non-blocking connect will return SOCKET_ERROR with WSAEWOULDBLOCK;
            // that is the expected path when the OS hasn't completed the 3-way
            // handshake yet.
            const int connectResult = connect(sock, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            if (connectResult == SOCKET_ERROR)
            {
                const int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
                {
                    closesocket(sock);
                    return false;
                }
            }

            // Wait for writability (connect completion) or error.
            fd_set writeSet {};
            fd_set errorSet {};
            FD_ZERO(&writeSet);
            FD_ZERO(&errorSet);
            FD_SET(sock, &writeSet);
            FD_SET(sock, &errorSet);
            timeval tv {};
            tv.tv_sec  = static_cast<long>(kConnectTimeoutMs / 1000);
            tv.tv_usec = static_cast<long>((kConnectTimeoutMs % 1000) * 1000);

            const int ready = select(0, nullptr, &writeSet, &errorSet, &tv);
            if (ready <= 0 || FD_ISSET(sock, &errorSet) || !FD_ISSET(sock, &writeSet))
            {
                closesocket(sock);
                return false;
            }

            // Verify getsockopt SO_ERROR (Windows may write error into writeset).
            int sockErr = 0;
            int sockErrLen = sizeof(sockErr);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&sockErr), &sockErrLen) != 0
                || sockErr != 0)
            {
                closesocket(sock);
                return false;
            }

            // Switch back to blocking for the recv loop; RecvAllWithTimeout
            // uses select() internally, so it still honours its own timeout.
            u_long blocking = 0;
            if (ioctlsocket(sock, FIONBIO, &blocking) != 0)
            {
                closesocket(sock);
                return false;
            }

            // Publish the socket into the shared field under sendMutex before
            // sending the hello, so that HeartbeatLoop can use it immediately
            // and ShutdownSocket() can interrupt us on Stop().
            {
                std::lock_guard<std::mutex> lock(sendMutex);
                socketHandle = sock;
            }

            tcp::HelloPayload hello {};
            CopyUtf8(hello.channelId, sizeof(hello.channelId), config.channelId);
            CopyUtf8(hello.clientId, sizeof(hello.clientId), config.clientId);
            if (!SendFrame(tcp::FrameKind::ClientHello, &hello, sizeof(hello)))
            {
                CloseSocket();
                return false;
            }

            return true;
        }

        void RunLoop()
        {
            // Ian: Single-attempt connection.  The host (MainWindow) now owns
            // the reconnect timer and drives retries via Stop()+Start() cycles.
            // This method connects once, runs the receive loop until the
            // connection drops or stopRequested is set, fires Disconnected, and
            // exits.  The host sees the Disconnected callback and schedules the
            // next attempt if auto-connect is enabled.
            //
            // Previous behavior: an outer `while(!stopRequested)` loop with
            // `autoConnect` checks and `kReconnectRetryMs` sleep between retries.
            // That logic is now in MainWindow::OnReconnectTimerFired().

            snapshotPhase = SnapshotPhase::Descriptors;
            lastHeartbeatUs.store(0, std::memory_order_release);
            serverSessionId.store(0, std::memory_order_release);

            if (onConnectionState)
            {
                onConnectionState(kConnectionStateConnecting);
            }

            if (!TryConnect())
            {
                if (!stopRequested.load(std::memory_order_acquire) && onConnectionState)
                {
                    onConnectionState(kConnectionStateDisconnected);
                }
                running.store(false, std::memory_order_release);
                return;
            }

            // Receive loop — runs until the connection dies or Stop() is called.
            RunRecvLoop();

            // Connection dropped. Clear connected state.
            if (connected.exchange(false) && onConnectionState)
            {
                onConnectionState(kConnectionStateDisconnected);
            }

            CloseSocket();
            running.store(false, std::memory_order_release);
        }

        void RunRecvLoop()
        {
            while (!stopRequested.load(std::memory_order_acquire))
            {
                tcp::FrameHeader header {};
                const ReceiveStatus headerStatus = RecvAllWithTimeout(
                    socketHandle,
                    reinterpret_cast<char*>(&header),
                    static_cast<int>(sizeof(header)),
                    config.waitTimeoutMs
                );

                if (headerStatus == ReceiveStatus::Timeout)
                {
                    MaybeReportStale();
                    continue;
                }
                if (headerStatus != ReceiveStatus::Success)
                {
                    break;
                }

                if (header.magic != tcp::kFrameMagic
                    || header.version != tcp::kFrameVersion
                    || header.payloadBytes > tcp::kMaxFramePayloadBytes)
                {
                    break;
                }

                std::vector<unsigned char> payload(header.payloadBytes);
                if (header.payloadBytes > 0)
                {
                    const ReceiveStatus payloadStatus = RecvAllWithTimeout(
                        socketHandle,
                        reinterpret_cast<char*>(payload.data()),
                        static_cast<int>(payload.size()),
                        config.waitTimeoutMs
                    );
                    if (payloadStatus != ReceiveStatus::Success)
                    {
                        break;
                    }
                }

                if (!HandleFrame(static_cast<tcp::FrameKind>(header.kind), payload))
                {
                    break;
                }
            }
        }

        void HeartbeatLoop()
        {
            while (!stopRequested.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.waitTimeoutMs));
                if (stopRequested.load(std::memory_order_acquire) || socketHandle == INVALID_SOCKET)
                {
                    // Ian: During a reconnect window socketHandle is INVALID_SOCKET;
                    // skip the send rather than erroring. The next successful dial
                    // will install a new socket and heartbeats will resume.
                    continue;
                }

                tcp::HeartbeatPayload heartbeat {};
                heartbeat.serverSessionId = serverSessionId.load(std::memory_order_acquire);
                // Ian: If SendFrame fails here the server-side socket is already
                // dead; the recv loop in RunRecvLoop() will detect the same
                // condition and break out of the inner loop, which drives the
                // outer reconnect cycle. Do not treat a heartbeat send failure
                // as a reason to exit the heartbeat thread — just continue so
                // the thread stays alive for the next connection.
                SendFrame(tcp::FrameKind::Heartbeat, &heartbeat, sizeof(heartbeat));
            }
        }

        bool HandleFrame(tcp::FrameKind kind, const std::vector<unsigned char>& payload)
        {
            switch (kind)
            {
                case tcp::FrameKind::ServerHello:
                {
                    if (payload.size() != sizeof(tcp::ServerHelloPayload))
                    {
                        return false;
                    }
                    tcp::ServerHelloPayload hello {};
                    memcpy(&hello, payload.data(), sizeof(hello));
                    serverSessionId.store(hello.serverSessionId, std::memory_order_release);
                    lastHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
                    return true;
                }
                case tcp::FrameKind::ServerMessage:
                {
                    if (payload.size() != sizeof(ipc::SharedMessage))
                    {
                        return false;
                    }
                    ipc::SharedMessage message {};
                    memcpy(&message, payload.data(), sizeof(message));
                    serverSessionId.store(message.serverSessionId, std::memory_order_release);
                    lastHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
                    return HandleMessage(message);
                }
                case tcp::FrameKind::Heartbeat:
                {
                    if (payload.size() != sizeof(tcp::HeartbeatPayload))
                    {
                        return false;
                    }
                    tcp::HeartbeatPayload heartbeat {};
                    memcpy(&heartbeat, payload.data(), sizeof(heartbeat));
                    serverSessionId.store(heartbeat.serverSessionId, std::memory_order_release);
                    lastHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
                    return true;
                }
                default:
                    return false;
            }
        }

        bool HandleMessage(const ipc::SharedMessage& message)
        {
            const std::string topicPath = ReadUtf8(message.topicPath, sizeof(message.topicPath));

            if (topicPath == "__snapshot_begin__")
            {
                // Ian: Re-enter descriptor mode whenever the authority starts a
                // fresh snapshot generation. TCP reconnects must rebuild state
                // from the same explicit boundaries as SHM instead of inferring
                // readiness from socket timing alone.
                snapshotPhase = SnapshotPhase::Descriptors;
                if (connected.exchange(false) && onConnectionState)
                {
                    onConnectionState(kConnectionStateConnecting);
                }
                return true;
            }
            if (topicPath == "__descriptor_end__")
            {
                return true;
            }
            if (topicPath == "__state_begin__")
            {
                snapshotPhase = SnapshotPhase::StateSnapshot;
                return true;
            }
            if (topicPath == "__state_end__")
            {
                return true;
            }
            if (topicPath == "__live_begin__")
            {
                snapshotPhase = SnapshotPhase::Live;
                // Ian: Publish `Connected` only after the authority says the
                // live boundary is open. That keeps TCP aligned with the Native
                // Link contract of descriptor snapshot -> state snapshot -> live
                // delta, rather than treating socket connect as semantic ready.
                if (!connected.exchange(true) && onConnectionState)
                {
                    onConnectionState(kConnectionStateConnected);
                }
                return true;
            }

            const UpdateEnvelope event = MessageToEnvelope(message);
            if (snapshotPhase != SnapshotPhase::Descriptors && !IsSentinelTopic(message.topicId) && onUpdate)
            {
                onUpdate(event);
            }
            return true;
        }

        void MaybeReportStale()
        {
            const std::uint64_t heartbeatUs = lastHeartbeatUs.load(std::memory_order_acquire);
            if (heartbeatUs == 0)
            {
                return;
            }

            const std::uint64_t nowUs = GetSteadyNowUs();
            if (nowUs >= heartbeatUs
                && (nowUs - heartbeatUs) > static_cast<std::uint64_t>(config.heartbeatStaleTimeoutMs) * 1000ULL)
            {
                snapshotPhase = SnapshotPhase::Descriptors;
                if (connected.exchange(false) && onConnectionState)
                {
                    onConnectionState(kConnectionStateStale);
                }
            }
        }

        bool SendFrame(tcp::FrameKind kind, const void* payload, std::uint32_t payloadBytes)
        {
            std::lock_guard<std::mutex> lock(sendMutex);
            if (socketHandle == INVALID_SOCKET)
            {
                return false;
            }

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

        void ShutdownSocket()
        {
            std::lock_guard<std::mutex> lock(sendMutex);
            if (socketHandle != INVALID_SOCKET)
            {
                shutdown(socketHandle, SD_BOTH);
            }
        }

        void CloseSocket()
        {
            std::lock_guard<std::mutex> lock(sendMutex);
            if (socketHandle != INVALID_SOCKET)
            {
                closesocket(socketHandle);
                socketHandle = INVALID_SOCKET;
            }
        }
    };

    NativeLinkTcpClient::NativeLinkTcpClient()
        : m_impl(new Impl())
    {
    }

    NativeLinkTcpClient::~NativeLinkTcpClient()
    {
        delete m_impl;
    }

    bool NativeLinkTcpClient::Start(const NativeLinkClientConfig& config, NativeLinkUpdateCallback onUpdate, NativeLinkConnectionStateCallback onConnectionState)
    {
        return m_impl != nullptr && m_impl->Start(config, std::move(onUpdate), std::move(onConnectionState));
    }

    void NativeLinkTcpClient::Stop()
    {
        if (m_impl != nullptr)
        {
            m_impl->Stop();
        }
    }

    bool NativeLinkTcpClient::Publish(const std::string& topicPath, const TopicValue& value)
    {
        return m_impl != nullptr && m_impl->Publish(topicPath, value);
    }

    bool NativeLinkTcpClient::IsConnected() const
    {
        return m_impl != nullptr && m_impl->IsConnected();
    }
}
