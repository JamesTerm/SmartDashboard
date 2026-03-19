#include "native_link_ipc_client.h"

#include "native_link_ipc_protocol.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
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

        class AutoHandle
        {
        public:
            AutoHandle() = default;
            ~AutoHandle()
            {
                Reset();
            }

            AutoHandle(const AutoHandle&) = delete;
            AutoHandle& operator=(const AutoHandle&) = delete;

            void Reset(HANDLE newHandle = nullptr)
            {
                if (m_handle != nullptr)
                {
                    CloseHandle(m_handle);
                }
                m_handle = newHandle;
            }

            HANDLE Get() const
            {
                return m_handle;
            }

        private:
            HANDLE m_handle = nullptr;
        };

        std::uint64_t GetSteadyNowUs()
        {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        std::wstring Utf8ToWide(const std::string& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
            if (required <= 0)
            {
                return std::wstring(value.begin(), value.end());
            }

            std::wstring wide(required, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &wide[0], required);
            return wide;
        }

        std::wstring MakeKernelObjectName(const wchar_t* prefix, const std::string& channelId)
        {
            std::wstring name = prefix;
            const std::wstring wideChannel = Utf8ToWide(channelId);
            for (wchar_t ch : wideChannel)
            {
                const bool safe =
                    (ch >= L'0' && ch <= L'9')
                    || (ch >= L'a' && ch <= L'z')
                    || (ch >= L'A' && ch <= L'Z')
                    || ch == L'-'
                    || ch == L'_';
                name += safe ? ch : L'_';
            }
            return name;
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

        bool IsClientSlotReady(const ipc::SharedClientSlot& slot)
        {
            return slot.clientTag.load(std::memory_order_acquire) != 0
                && slot.lastHeartbeatUs.load(std::memory_order_acquire) != 0
                && slot.clientId[0] != '\0';
        }

        bool HasServerFinishedSnapshotForClient(const ipc::SharedClientSlot& slot)
        {
            return slot.snapshotCompleteSessionId.load(std::memory_order_acquire) != 0;
        }
    }

    struct NativeLinkIpcClient::Impl
    {
        Config config;
        UpdateCallback onUpdate;
        ConnectionStateCallback onConnectionState;
        AutoHandle mapping;
        void* mappingView = nullptr;
        ipc::SharedState* shared = nullptr;
        AutoHandle clientDataEvent;
        AutoHandle heartbeatEvent;
        std::uint32_t slotIndex = ipc::kMaxClients;
        std::uint64_t clientTag = 0;
        std::thread worker;
        std::atomic<bool> running = false;
        std::atomic<bool> stopRequested = false;
        std::atomic<bool> connected = false;
        bool serverAlive = false;
        std::mutex publishMutex;
        std::uint64_t lastSubmittedWriteSequence = 0;
        enum class SnapshotPhase
        {
            Descriptors,
            StateSnapshot,
            Live
        };
        SnapshotPhase snapshotPhase = SnapshotPhase::Descriptors;

        ~Impl()
        {
            Stop();
        }

        bool Start(const Config& inConfig, UpdateCallback updateCallback, ConnectionStateCallback connectionCallback)
        {
            Stop();

            config = inConfig;
            onUpdate = std::move(updateCallback);
            onConnectionState = std::move(connectionCallback);

            const std::wstring mappingName = MakeKernelObjectName(L"Local\\NativeLink.Shared.", config.channelId);
            const std::wstring clientEventName = MakeKernelObjectName(L"Local\\NativeLink.ClientData.", config.channelId);
            const std::wstring heartbeatEventName = MakeKernelObjectName(L"Local\\NativeLink.ServerHeartbeat.", config.channelId);

            mapping.Reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName.c_str()));
            if (mapping.Get() == nullptr)
            {
                return false;
            }

            mappingView = MapViewOfFile(mapping.Get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ipc::SharedState));
            if (mappingView == nullptr)
            {
                mapping.Reset();
                return false;
            }

            shared = static_cast<ipc::SharedState*>(mappingView);
            if (shared->magic != ipc::kSharedMagic || shared->version != ipc::kSharedVersion)
            {
                CleanupMappingOnly();
                return false;
            }

            clientDataEvent.Reset(OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, clientEventName.c_str()));
            heartbeatEvent.Reset(OpenEventW(SYNCHRONIZE, FALSE, heartbeatEventName.c_str()));
            if (clientDataEvent.Get() == nullptr || heartbeatEvent.Get() == nullptr)
            {
                CleanupMappingOnly();
                return false;
            }

            clientTag = GetSteadyNowUs() ^ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
            for (std::uint32_t i = 0; i < ipc::kMaxClients; ++i)
            {
                ipc::SharedClientSlot& slot = shared->clients[i];
                std::uint64_t expected = 0;
                if (slot.clientTag.compare_exchange_strong(expected, clientTag, std::memory_order_acq_rel))
                {
                    slotIndex = i;
                    slot.snapshotCompleteSessionId.store(0, std::memory_order_release);
                    slot.lastAckedSequence.store(0, std::memory_order_release);
                    slot.serverWriteIndex.store(0, std::memory_order_release);
                    slot.clientReadIndex.store(0, std::memory_order_release);
                    slot.clientWriteSequence.store(0, std::memory_order_release);
                    memset(&slot.clientWriteMessage, 0, sizeof(slot.clientWriteMessage));

                    // Ian: Claiming the slot is not the same as advertising a live
                    // client. Finish resetting the per-client cursors/message slot
                    // first, then publish `clientId` and heartbeat last so the
                    // server never snapshots against half-old, half-new slot state.
                    CopyUtf8(slot.clientId, sizeof(slot.clientId), config.clientId);
                    slot.lastHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);

                    shared->clientCount.fetch_add(1, std::memory_order_acq_rel);
                    lastSubmittedWriteSequence = 0;
                    snapshotPhase = SnapshotPhase::Descriptors;
                    running.store(true);
                    stopRequested.store(false);
                    worker = std::thread(&Impl::RunLoop, this);
                    return true;
                }
            }

            CleanupMappingOnly();
            return false;
        }

        void Stop()
        {
            stopRequested.store(true);
            if (worker.joinable())
            {
                worker.join();
            }

            if (connected.exchange(false) && onConnectionState)
            {
                onConnectionState(kConnectionStateDisconnected);
            }

            if (shared != nullptr && slotIndex < ipc::kMaxClients)
            {
                ipc::SharedClientSlot& slot = shared->clients[slotIndex];
                memset(slot.clientId, 0, sizeof(slot.clientId));
                slot.clientTag.store(0, std::memory_order_release);
                shared->clientCount.fetch_sub(1, std::memory_order_acq_rel);
            }

            slotIndex = ipc::kMaxClients;
            clientTag = 0;
            lastSubmittedWriteSequence = 0;
            running.store(false);
            CleanupMappingOnly();
            onUpdate = {};
            onConnectionState = {};
        }

        bool Publish(const std::string& topicPath, const TopicValue& value)
        {
            std::lock_guard<std::mutex> lock(publishMutex);
            if (shared == nullptr || slotIndex >= ipc::kMaxClients)
            {
                return false;
            }

            ipc::SharedClientSlot& slot = shared->clients[slotIndex];
            if (!IsClientSlotReady(slot) || !connected.load(std::memory_order_acquire))
            {
                return false;
            }

            if (lastSubmittedWriteSequence != 0)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    if (slot.lastAckedSequence.load(std::memory_order_acquire) >= lastSubmittedWriteSequence)
                    {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                if (slot.lastAckedSequence.load(std::memory_order_acquire) < lastSubmittedWriteSequence)
                {
                    return false;
                }
            }

            ipc::SharedMessage& message = slot.clientWriteMessage;
            memset(&message, 0, sizeof(message));
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

            const std::uint64_t baselineSequence = (std::max)(
                slot.clientWriteSequence.load(std::memory_order_acquire),
                slot.lastAckedSequence.load(std::memory_order_acquire)
            );
            const std::uint64_t writeSequence = baselineSequence + 1;
            slot.clientWriteSequence.store(writeSequence, std::memory_order_release);
            lastSubmittedWriteSequence = writeSequence;
            slot.lastHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);

            bool signaled = false;
            const auto signalDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            while (std::chrono::steady_clock::now() < signalDeadline)
            {
                signaled = SetEvent(clientDataEvent.Get()) != FALSE || signaled;
                if (slot.lastAckedSequence.load(std::memory_order_acquire) >= writeSequence)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            return signaled;
        }

        void RunLoop()
        {
            if (onConnectionState)
            {
                onConnectionState(kConnectionStateConnecting);
            }

            while (!stopRequested.load())
            {
                if (shared == nullptr || slotIndex >= ipc::kMaxClients)
                {
                    break;
                }

                const std::uint64_t lastHeartbeatUs = shared->lastServerHeartbeatUs.load(std::memory_order_acquire);
                const std::uint64_t nowUs = GetSteadyNowUs();
                const bool serverLooksAlive = lastHeartbeatUs != 0
                    && nowUs >= lastHeartbeatUs
                    && (nowUs - lastHeartbeatUs) <= static_cast<std::uint64_t>(config.heartbeatStaleTimeoutMs) * 1000ULL;

                if (!serverLooksAlive)
                {
                    serverAlive = false;
                    snapshotPhase = SnapshotPhase::Descriptors;
                    lastSubmittedWriteSequence = 0;
                    if (connected.exchange(false) && onConnectionState)
                    {
                        onConnectionState(kConnectionStateStale);
                    }
                }
                else
                {
                    serverAlive = true;
                }

                DrainMessages();

                ipc::SharedClientSlot& slot = shared->clients[slotIndex];
                MaybePublishConnected(slot);
                slot.lastHeartbeatUs.store(nowUs, std::memory_order_release);
                WaitForSingleObject(heartbeatEvent.Get(), config.waitTimeoutMs);
            }
        }

        void MaybePublishConnected(const ipc::SharedClientSlot& slot)
        {
            if (!serverAlive || snapshotPhase != SnapshotPhase::Live)
            {
                return;
            }

            const std::uint64_t currentServerSessionId = shared != nullptr
                ? shared->serverSessionId.load(std::memory_order_acquire)
                : 0;
            if (currentServerSessionId == 0
                || slot.snapshotCompleteSessionId.load(std::memory_order_acquire) != currentServerSessionId)
            {
                return;
            }

            // Ian: `__live_begin__` only means the authority queued the live
            // boundary into this slot. Do not surface `Connected` until the
            // server-side snapshot bookkeeping has also advanced, or immediate
            // dashboard publishes can beat the final handshake step and become a
            // flaky startup race instead of a deterministic first live write.
            if (!connected.exchange(true) && onConnectionState)
            {
                onConnectionState(kConnectionStateConnected);
            }
        }

        void DrainMessages()
        {
            ipc::SharedClientSlot& slot = shared->clients[slotIndex];
            std::uint32_t readIndex = slot.clientReadIndex.load(std::memory_order_acquire);
            const std::uint32_t writeIndex = slot.serverWriteIndex.load(std::memory_order_acquire);
            while (readIndex < writeIndex)
            {
                const ipc::SharedMessage& message = slot.messages[readIndex % ipc::kMaxMessages];
                const std::string topicPath = ReadUtf8(message.topicPath, sizeof(message.topicPath));

                if (topicPath == "__snapshot_begin__")
                {
                    snapshotPhase = SnapshotPhase::Descriptors;
                    lastSubmittedWriteSequence = 0;
                    if (connected.exchange(false) && onConnectionState)
                    {
                        onConnectionState(kConnectionStateConnecting);
                    }
                    ++readIndex;
                    continue;
                }
                if (topicPath == "__descriptor_end__")
                {
                    ++readIndex;
                    continue;
                }
                if (topicPath == "__state_begin__")
                {
                    snapshotPhase = SnapshotPhase::StateSnapshot;
                    ++readIndex;
                    continue;
                }
                if (topicPath == "__state_end__")
                {
                    ++readIndex;
                    continue;
                }
                if (topicPath == "__live_begin__")
                {
                    snapshotPhase = SnapshotPhase::Live;
                    ++readIndex;
                    continue;
                }

                const UpdateEnvelope event = MessageToEnvelope(message);

                // Ian: The server sends descriptor metadata before state snapshot.
                // ABI v1 cannot surface descriptors yet, so suppress only that
                // phase while still forwarding retained state snapshot updates and
                // later live deltas in the documented snapshot-then-live order.
                if (snapshotPhase != SnapshotPhase::Descriptors && !IsSentinelTopic(message.topicId) && onUpdate)
                {
                    onUpdate(event);
                }

                ++readIndex;
            }
            slot.clientReadIndex.store(readIndex, std::memory_order_release);
            MaybePublishConnected(slot);
        }

        void CleanupMappingOnly()
        {
            heartbeatEvent.Reset();
            clientDataEvent.Reset();
            if (mappingView != nullptr)
            {
                UnmapViewOfFile(mappingView);
                mappingView = nullptr;
            }
            shared = nullptr;
            mapping.Reset();
        }
    };

    NativeLinkIpcClient::NativeLinkIpcClient()
        : m_impl(new Impl())
    {
    }

    NativeLinkIpcClient::~NativeLinkIpcClient()
    {
        delete m_impl;
    }

    bool NativeLinkIpcClient::Start(const Config& config, UpdateCallback onUpdate, ConnectionStateCallback onConnectionState)
    {
        return m_impl != nullptr && m_impl->Start(config, std::move(onUpdate), std::move(onConnectionState));
    }

    void NativeLinkIpcClient::Stop()
    {
        if (m_impl != nullptr)
        {
            m_impl->Stop();
        }
    }

    bool NativeLinkIpcClient::Publish(const std::string& topicPath, const TopicValue& value)
    {
        return m_impl != nullptr && m_impl->Publish(topicPath, value);
    }
}
