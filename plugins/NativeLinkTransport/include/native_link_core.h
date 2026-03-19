#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sd::nativelink
{
    enum class TopicKind
    {
        State,
        Command,
        Event
    };

    enum class ValueType
    {
        Bool,
        Double,
        String,
        StringArray
    };

    enum class RetentionMode
    {
        None,
        LatestValue
    };

    enum class WriterPolicy
    {
        ServerOnly,
        LeaseSingleWriter
    };

    enum class DeliveryKind
    {
        SnapshotState,
        LiveState,
        LiveCommand,
        LiveCommandAck,
        LiveCommandReject,
        LiveEvent
    };

    enum class FreshnessReason
    {
        Live,
        TtlExpired
    };

    enum class SnapshotEventKind
    {
        DescriptorSnapshotBegin,
        Descriptor,
        DescriptorSnapshotEnd,
        StateSnapshotBegin,
        Update,
        StateSnapshotEnd,
        LiveBegin
    };

    enum class WriteRejectReason
    {
        None,
        UnknownTopic,
        WrongType,
        ReadOnly,
        LeaseRequired,
        LeaseNotHolder,
        PolicyViolation
    };

    struct TopicValue
    {
        ValueType type = ValueType::Bool;
        bool boolValue = false;
        double doubleValue = 0.0;
        std::string stringValue;
        std::vector<std::string> stringArrayValue;

        static TopicValue Bool(bool value);
        static TopicValue Double(double value);
        static TopicValue String(std::string value);
        static TopicValue StringArray(std::vector<std::string> value);
    };

    struct TopicDescriptor
    {
        std::string topicPath;
        TopicKind topicKind = TopicKind::State;
        ValueType valueType = ValueType::Bool;
        std::string schemaName;
        int schemaVersion = 1;
        RetentionMode retentionMode = RetentionMode::None;
        bool replayOnSubscribe = false;
        int ttlMs = 0;
        WriterPolicy writerPolicy = WriterPolicy::ServerOnly;
        std::string description;
    };

    struct UpdateEnvelope
    {
        std::uint64_t serverSessionId = 0;
        std::uint64_t serverSequence = 0;
        std::uint64_t topicId = 0;
        std::string topicPath;
        std::string sourceClientId;
        DeliveryKind deliveryKind = DeliveryKind::LiveState;
        TopicValue value;
        int ttlMs = 0;
        int ageMsAtEmit = 0;
        bool isStale = false;
        FreshnessReason freshnessReason = FreshnessReason::Live;
        WriteRejectReason rejectionReason = WriteRejectReason::None;
    };

    struct SnapshotEvent
    {
        SnapshotEventKind kind = SnapshotEventKind::DescriptorSnapshotBegin;
        bool hasDescriptor = false;
        TopicDescriptor descriptor;
        bool hasUpdate = false;
        UpdateEnvelope update;
    };

    struct RegisterTopicResult
    {
        bool ok = false;
        std::string message;
        std::uint64_t topicId = 0;
    };

    struct WriteResult
    {
        bool accepted = false;
        WriteRejectReason rejectionReason = WriteRejectReason::None;
        std::uint64_t serverSequence = 0;
    };

    struct ClientSessionView
    {
        std::vector<SnapshotEvent> snapshotEvents;
    };

    struct TopicLeaseInfo
    {
        bool hasLeaseHolder = false;
        std::string leaseHolderClientId;
    };

    class NativeLinkCore
    {
    public:
        using Clock = std::function<std::chrono::steady_clock::time_point(void)>;

        struct TopicRuntime
        {
            TopicDescriptor descriptor;
            std::uint64_t topicId = 0;
            bool hasLatestValue = false;
            TopicValue latestValue;
            std::string latestSourceClientId;
            std::chrono::steady_clock::time_point latestValueTime;
            std::string leaseHolderClientId;
        };

        struct ClientRuntime
        {
            std::string clientId;
            std::deque<UpdateEnvelope> pendingEvents;
        };

        explicit NativeLinkCore(Clock clock = {});

        RegisterTopicResult RegisterTopic(const TopicDescriptor& descriptor);
        bool AcquireLease(const std::string& topicPath, const std::string& clientId);
        bool ReleaseLease(const std::string& topicPath, const std::string& clientId);
        ClientSessionView ConnectClient(const std::string& clientId);
        bool DisconnectClient(const std::string& clientId);

        WriteResult Publish(const std::string& topicPath, const TopicValue& value, const std::string& sourceClientId);
        WriteResult PublishFromServer(const std::string& topicPath, const TopicValue& value);

        std::vector<SnapshotEvent> BuildSnapshotForClient(const std::string& clientId) const;
        std::vector<UpdateEnvelope> DrainClientEvents(const std::string& clientId);
        bool TryGetLatestValue(const std::string& topicPath, TopicValue& outValue) const;
        TopicLeaseInfo GetTopicLeaseInfo(const std::string& topicPath) const;
        bool IsTopicRegistered(const std::string& topicPath) const;

        void BeginNewSession();

        std::uint64_t GetServerSessionId() const;

        const struct TopicRuntime* LookupTopic(const std::string& topicPath) const;
        DeliveryKind GetLiveDeliveryKind(TopicKind topicKind) const;

    private:
        Clock m_clock;
        std::uint64_t m_serverSessionId = 1;
        std::uint64_t m_nextTopicId = 1;
        std::uint64_t m_nextServerSequence = 1;
        std::vector<TopicRuntime> m_topics;
        std::vector<ClientRuntime> m_clients;

        const TopicRuntime* FindTopic(const std::string& topicPath) const;
        TopicRuntime* FindTopic(const std::string& topicPath);
        const ClientRuntime* FindClient(const std::string& clientId) const;
        ClientRuntime* FindClient(const std::string& clientId);
        static RegisterTopicResult ValidateDescriptor(const TopicDescriptor& descriptor);
        WriteResult PublishInternal(const std::string& topicPath, const TopicValue& value, const std::string& sourceClientId, bool allowServerOnly);
        UpdateEnvelope BuildEnvelope(const TopicRuntime& topic, DeliveryKind deliveryKind, std::uint64_t serverSequence) const;
        DeliveryKind DetermineLiveDeliveryKind(TopicKind topicKind) const;
        void EnqueueLiveEvent(const UpdateEnvelope& envelope);
        void EnqueueEventForClient(const std::string& clientId, const UpdateEnvelope& envelope);
        std::chrono::steady_clock::time_point Now() const;
    };
}
