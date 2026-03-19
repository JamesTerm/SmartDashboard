#include "native_link_ipc_test_server.h"

#include "native_link_ipc_protocol.h"

#include <Windows.h>

#include <algorithm>
#include <array>
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

namespace sd::nativelink::testsupport
{
    namespace
    {
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

    struct NativeLinkIpcTestServer::Impl
    {
        explicit Impl(std::string inChannelId)
            : channelId(std::move(inChannelId))
        {
        }

        std::string channelId;
        mutable std::mutex mutex;
        NativeLinkCore core;
        AutoHandle mapping;
        void* mappingView = nullptr;
        ipc::SharedState* shared = nullptr;
        AutoHandle clientDataEvent;
        AutoHandle heartbeatEvent;
        std::thread worker;
        std::atomic<bool> running = false;
        std::atomic<bool> stopRequested = false;
        std::array<std::uint64_t, ipc::kMaxClients> lastProcessedClientWriteSequence {};

        ~Impl()
        {
            Stop();
        }

        bool Start()
        {
            if (running.load())
            {
                return true;
            }

            const std::wstring mappingName = MakeKernelObjectName(L"Local\\NativeLink.Shared.", channelId);
            const std::wstring clientEventName = MakeKernelObjectName(L"Local\\NativeLink.ClientData.", channelId);
            const std::wstring heartbeatEventName = MakeKernelObjectName(L"Local\\NativeLink.ServerHeartbeat.", channelId);

            mapping.Reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(ipc::SharedState), mappingName.c_str()));
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
            memset(shared, 0, sizeof(ipc::SharedState));
            shared->magic = ipc::kSharedMagic;
            shared->version = ipc::kSharedVersion;
            CopyUtf8(shared->channelId, sizeof(shared->channelId), channelId);

            clientDataEvent.Reset(CreateEventW(nullptr, FALSE, FALSE, clientEventName.c_str()));
            heartbeatEvent.Reset(CreateEventW(nullptr, FALSE, FALSE, heartbeatEventName.c_str()));
            if (clientDataEvent.Get() == nullptr || heartbeatEvent.Get() == nullptr)
            {
                Stop();
                return false;
            }

            core.BeginNewSession();
            PublishSessionMetadataLocked();
            lastProcessedClientWriteSequence.fill(0);

            stopRequested.store(false);
            running.store(true);
            worker = std::thread(&Impl::RunLoop, this);
            return true;
        }

        void Stop()
        {
            stopRequested.store(true);
            if (clientDataEvent.Get() != nullptr)
            {
                SetEvent(clientDataEvent.Get());
            }
            if (worker.joinable())
            {
                worker.join();
            }
            running.store(false);
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

        void RegisterDefaultDashboardTopics()
        {
            std::lock_guard<std::mutex> lock(mutex);

            core.RegisterTopic(MakeDescriptor(
                "Test/Auton_Selection/AutoChooser/options",
                TopicKind::State,
                ValueType::StringArray,
                WriterPolicy::ServerOnly,
                true,
                true
            ));
            core.RegisterTopic(MakeDescriptor(
                "Test/Auton_Selection/AutoChooser/default",
                TopicKind::State,
                ValueType::String,
                WriterPolicy::ServerOnly,
                true,
                true
            ));
            core.RegisterTopic(MakeDescriptor(
                "Test/Auton_Selection/AutoChooser/active",
                TopicKind::State,
                ValueType::String,
                WriterPolicy::ServerOnly,
                true,
                true
            ));
            core.RegisterTopic(MakeDescriptor(
                "Test/Auton_Selection/AutoChooser/selected",
                TopicKind::State,
                ValueType::String,
                WriterPolicy::LeaseSingleWriter,
                true,
                true
            ));
            core.RegisterTopic(MakeDescriptor(
                "TestMove",
                TopicKind::State,
                ValueType::Double,
                WriterPolicy::LeaseSingleWriter,
                true,
                true
            ));
            core.RegisterTopic(MakeDescriptor(
                "Timer",
                TopicKind::State,
                ValueType::Double,
                WriterPolicy::ServerOnly,
                true,
                true
            ));
            core.RegisterTopic(MakeDescriptor(
                "Y_ft",
                TopicKind::State,
                ValueType::Double,
                WriterPolicy::ServerOnly,
                true,
                true
            ));

            PublishFromServerLocked("Test/Auton_Selection/AutoChooser/options", TopicValue::StringArray({
                "Do Nothing", "Just Move Forward", "Just Rotate", "Move Rotate Sequence", "Box Waypoints", "Smart Waypoints"
            }));
            PublishFromServerLocked("Test/Auton_Selection/AutoChooser/default", TopicValue::String("Do Nothing"));
            PublishFromServerLocked("Test/Auton_Selection/AutoChooser/active", TopicValue::String("Do Nothing"));
            PublishFromServerLocked("Test/Auton_Selection/AutoChooser/selected", TopicValue::String("Do Nothing"));
            PublishFromServerLocked("TestMove", TopicValue::Double(0.0));
            PublishFromServerLocked("Timer", TopicValue::Double(0.0));
            PublishFromServerLocked("Y_ft", TopicValue::Double(0.0));
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

        bool PublishFromServer(const std::string& topicPath, const TopicValue& value)
        {
            std::lock_guard<std::mutex> lock(mutex);
            return PublishFromServerLocked(topicPath, value);
        }

        bool PublishFromServerLocked(const std::string& topicPath, const TopicValue& value)
        {
            const WriteResult result = core.PublishFromServer(topicPath, value);
            if (!result.accepted)
            {
                return false;
            }

            const NativeLinkCore::TopicRuntime* topic = nullptr;
            TopicValue latest;
            if (!core.TryGetLatestValue(topicPath, latest))
            {
                return false;
            }

            topic = core.LookupTopic(topicPath);
            if (topic == nullptr)
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

        bool TryGetLatestValue(const std::string& topicPath, TopicValue& outValue) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return core.TryGetLatestValue(topicPath, outValue);
        }

        void RestartSession()
        {
            std::lock_guard<std::mutex> lock(mutex);
            core.BeginNewSession();
            PublishSessionMetadataLocked();
            lastProcessedClientWriteSequence.fill(0);
            for (std::uint32_t i = 0; i < ipc::kMaxClients; ++i)
            {
                shared->clients[i].lastAckedSequence.store(0, std::memory_order_release);
                shared->clients[i].serverWriteIndex.store(0, std::memory_order_release);
                shared->clients[i].clientReadIndex.store(0, std::memory_order_release);
            }
        }

        std::uint64_t GetServerSessionId() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return core.GetServerSessionId();
        }

        void RunLoop()
        {
            while (!stopRequested.load())
            {
                if (shared != nullptr)
                {
                    shared->lastServerHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
                }
                if (heartbeatEvent.Get() != nullptr)
                {
                    SetEvent(heartbeatEvent.Get());
                }

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ScanClientsLocked();
                }

                WaitForSingleObject(clientDataEvent.Get(), 25);
            }
        }

        void ScanClientsLocked()
        {
            const std::uint64_t nowUs = GetSteadyNowUs();
            for (std::uint32_t i = 0; i < ipc::kMaxClients; ++i)
            {
                ipc::SharedClientSlot& slot = shared->clients[i];
                const std::uint64_t tag = slot.clientTag.load(std::memory_order_acquire);
                if (tag == 0)
                {
                    continue;
                }

                const std::string clientId = ReadUtf8(slot.clientId, sizeof(slot.clientId));
                const std::uint64_t lastHeartbeatUs = slot.lastHeartbeatUs.load(std::memory_order_acquire);

                // Ian: Claiming a client slot is a two-phase cross-process setup:
                // the client must win the slot first, then finish filling in its
                // id/heartbeat fields. Do not treat `clientTag != 0` alone as a
                // fully live client or the server can race a half-initialized slot
                // and disconnect it before the first snapshot ever goes out.
                if (clientId.empty() || lastHeartbeatUs == 0)
                {
                    continue;
                }

                const std::uint64_t heartbeatAgeUs = nowUs - lastHeartbeatUs;
                if (heartbeatAgeUs > 5000000ULL)
                {
                    core.DisconnectClient(clientId);
                    slot.clientTag.store(0, std::memory_order_release);
                    continue;
                }

                if (slot.lastAckedSequence.load(std::memory_order_acquire) == 0)
                {
                    PublishSnapshotForClientLocked(clientId);
                    slot.lastAckedSequence.store(1, std::memory_order_release);
                    if (heartbeatEvent.Get() != nullptr)
                    {
                        SetEvent(heartbeatEvent.Get());
                    }
                }

                const std::uint64_t clientWriteSequence = slot.clientWriteSequence.load(std::memory_order_acquire);
                if (clientWriteSequence > lastProcessedClientWriteSequence[i])
                {
                    // Ian: The simulator's v1 shared slot already uses
                    // `lastAckedSequence` as a coarse snapshot-sent marker. Track
                    // client write progress separately on the server side so the
                    // first post-snapshot dashboard write is never mistaken for a
                    // snapshot bookkeeping value and dropped.
                    const ipc::SharedMessage message = slot.clientWriteMessage;
                    HandleClientWriteLocked(clientId, message);
                    lastProcessedClientWriteSequence[i] = clientWriteSequence;
                    slot.lastAckedSequence.store(clientWriteSequence, std::memory_order_release);
                    if (heartbeatEvent.Get() != nullptr)
                    {
                        SetEvent(heartbeatEvent.Get());
                    }
                }
            }
        }

        void PublishSessionMetadataLocked()
        {
            shared->serverSessionId.store(core.GetServerSessionId(), std::memory_order_release);
            shared->serverBootTimeUs.store(GetSteadyNowUs(), std::memory_order_release);
            shared->lastServerHeartbeatUs.store(GetSteadyNowUs(), std::memory_order_release);
        }

        void PublishSnapshotForClientLocked(const std::string& clientId)
        {
            const std::vector<SnapshotEvent> snapshot = core.ConnectClient(clientId).snapshotEvents;
            for (const SnapshotEvent& event : snapshot)
            {
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
                            envelope.topicId = 0;
                            envelope.topicPath = event.descriptor.topicPath;
                            envelope.value = TopicValue::String(event.descriptor.topicPath);
                            break;
                        default:
                            break;
                    }
                }

                PublishEnvelopeToSingleClientLocked(clientId, envelope);
            }
        }

        void HandleClientWriteLocked(const std::string& clientId, const ipc::SharedMessage& message)
        {
            TopicValue value;
            if (!DeserializeValue(
                static_cast<ValueType>(message.valueType),
                message.payload,
                static_cast<std::size_t>(message.flags),
                value))
            {
                return;
            }

            if (!core.GetTopicLeaseInfo(message.topicPath).hasLeaseHolder)
            {
                core.AcquireLease(message.topicPath, clientId);
            }

            const WriteResult result = core.Publish(message.topicPath, value, clientId);
            for (const UpdateEnvelope& clientEvent : core.DrainClientEvents(clientId))
            {
                PublishEnvelopeToSingleClientLocked(clientId, clientEvent);
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
            live.sourceClientId = clientId;
            live.value = latest;
            live.deliveryKind = topic->descriptor.topicKind == TopicKind::Command
                ? DeliveryKind::LiveCommand
                : DeliveryKind::LiveState;
            PublishEnvelopeToAllClientsLocked(live);
        }

        void PublishEnvelopeToAllClientsLocked(const UpdateEnvelope& envelope)
        {
            for (std::uint32_t i = 0; i < ipc::kMaxClients; ++i)
            {
                if (shared->clients[i].clientTag.load(std::memory_order_acquire) == 0)
                {
                    continue;
                }
                const std::string clientId = ReadUtf8(shared->clients[i].clientId, sizeof(shared->clients[i].clientId));
                PublishEnvelopeToSingleClientLocked(clientId, envelope);
            }
        }

        void PublishEnvelopeToSingleClientLocked(const std::string& clientId, const UpdateEnvelope& envelope)
        {
            for (std::uint32_t i = 0; i < ipc::kMaxClients; ++i)
            {
                ipc::SharedClientSlot& slot = shared->clients[i];
                if (ReadUtf8(slot.clientId, sizeof(slot.clientId)) != clientId)
                {
                    continue;
                }

                ipc::SharedMessage message;
                memset(&message, 0, sizeof(message));
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

                const std::uint32_t writeIndex = slot.serverWriteIndex.load(std::memory_order_acquire);
                slot.messages[writeIndex % ipc::kMaxMessages] = message;
                slot.serverWriteIndex.store(writeIndex + 1, std::memory_order_release);
                return;
            }
        }
    };

    NativeLinkIpcTestServer::NativeLinkIpcTestServer(std::string channelId)
        : m_impl(std::make_unique<Impl>(std::move(channelId)))
    {
    }

    NativeLinkIpcTestServer::~NativeLinkIpcTestServer() = default;

    bool NativeLinkIpcTestServer::Start()
    {
        return m_impl != nullptr && m_impl->Start();
    }

    void NativeLinkIpcTestServer::Stop()
    {
        if (m_impl != nullptr)
        {
            m_impl->Stop();
        }
    }

    void NativeLinkIpcTestServer::RegisterDefaultDashboardTopics()
    {
        if (m_impl != nullptr)
        {
            m_impl->RegisterDefaultDashboardTopics();
        }
    }

    bool NativeLinkIpcTestServer::PublishBoolean(const std::string& topicPath, bool value)
    {
        return m_impl != nullptr && m_impl->PublishBoolean(topicPath, value);
    }

    bool NativeLinkIpcTestServer::PublishDouble(const std::string& topicPath, double value)
    {
        return m_impl != nullptr && m_impl->PublishDouble(topicPath, value);
    }

    bool NativeLinkIpcTestServer::PublishString(const std::string& topicPath, const std::string& value)
    {
        return m_impl != nullptr && m_impl->PublishString(topicPath, value);
    }

    bool NativeLinkIpcTestServer::PublishStringArray(const std::string& topicPath, const std::vector<std::string>& value)
    {
        return m_impl != nullptr && m_impl->PublishStringArray(topicPath, value);
    }

    bool NativeLinkIpcTestServer::TryGetLatestValue(const std::string& topicPath, TopicValue& outValue) const
    {
        return m_impl != nullptr && m_impl->TryGetLatestValue(topicPath, outValue);
    }

    void NativeLinkIpcTestServer::RestartSession()
    {
        if (m_impl != nullptr)
        {
            m_impl->RestartSession();
        }
    }

    std::uint64_t NativeLinkIpcTestServer::GetServerSessionId() const
    {
        return m_impl != nullptr ? m_impl->GetServerSessionId() : 0;
    }
}
