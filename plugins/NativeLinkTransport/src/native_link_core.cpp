#include "native_link_core.h"

#include <algorithm>
#include <utility>

namespace sd::nativelink
{
    namespace
    {
        bool TopicValueMatches(ValueType type, const TopicValue& value)
        {
            switch (type)
            {
                case ValueType::Bool:
                    return value.type == ValueType::Bool;
                case ValueType::Double:
                    return value.type == ValueType::Double;
                case ValueType::String:
                    return value.type == ValueType::String;
                case ValueType::StringArray:
                    return value.type == ValueType::StringArray;
            }

            return false;
        }
    }

    TopicValue TopicValue::Bool(bool value)
    {
        TopicValue result;
        result.type = ValueType::Bool;
        result.boolValue = value;
        return result;
    }

    TopicValue TopicValue::Double(double value)
    {
        TopicValue result;
        result.type = ValueType::Double;
        result.doubleValue = value;
        return result;
    }

    TopicValue TopicValue::String(std::string value)
    {
        TopicValue result;
        result.type = ValueType::String;
        result.stringValue = std::move(value);
        return result;
    }

    TopicValue TopicValue::StringArray(std::vector<std::string> value)
    {
        TopicValue result;
        result.type = ValueType::StringArray;
        result.stringArrayValue = std::move(value);
        return result;
    }

    NativeLinkCore::NativeLinkCore(Clock clock)
        : m_clock(std::move(clock))
    {
    }

    RegisterTopicResult NativeLinkCore::RegisterTopic(const TopicDescriptor& descriptor)
    {
        RegisterTopicResult validation = ValidateDescriptor(descriptor);
        if (!validation.ok)
        {
            return validation;
        }

        if (FindTopic(descriptor.topicPath) != nullptr)
        {
            return { false, "topic already registered", 0 };
        }

        TopicRuntime runtime;
        runtime.descriptor = descriptor;
        runtime.topicId = m_nextTopicId++;
        runtime.latestValueTime = Now();

        m_topics.push_back(runtime);
        return { true, {}, runtime.topicId };
    }

    bool NativeLinkCore::AcquireLease(const std::string& topicPath, const std::string& clientId)
    {
        TopicRuntime* topic = FindTopic(topicPath);
        if (topic == nullptr)
        {
            return false;
        }
        if (topic->descriptor.writerPolicy != WriterPolicy::LeaseSingleWriter)
        {
            return false;
        }
        if (!topic->leaseHolderClientId.empty() && topic->leaseHolderClientId != clientId)
        {
            return false;
        }

        topic->leaseHolderClientId = clientId;
        return true;
    }

    bool NativeLinkCore::ReleaseLease(const std::string& topicPath, const std::string& clientId)
    {
        TopicRuntime* topic = FindTopic(topicPath);
        if (topic == nullptr)
        {
            return false;
        }
        if (topic->leaseHolderClientId != clientId)
        {
            return false;
        }

        topic->leaseHolderClientId.clear();
        return true;
    }

    ClientSessionView NativeLinkCore::ConnectClient(const std::string& clientId)
    {
        ClientRuntime* existing = FindClient(clientId);
        if (existing == nullptr)
        {
            ClientRuntime runtime;
            runtime.clientId = clientId;
            m_clients.push_back(runtime);
        }

        ClientSessionView view;
        view.snapshotEvents = BuildSnapshotForClient(clientId);
        return view;
    }

    bool NativeLinkCore::DisconnectClient(const std::string& clientId)
    {
        const auto it = std::find_if(m_clients.begin(), m_clients.end(), [&clientId](const ClientRuntime& client)
        {
            return client.clientId == clientId;
        });
        if (it == m_clients.end())
        {
            return false;
        }

        m_clients.erase(it);
        return true;
    }

    WriteResult NativeLinkCore::Publish(const std::string& topicPath, const TopicValue& value, const std::string& sourceClientId)
    {
        return PublishInternal(topicPath, value, sourceClientId, false);
    }

    WriteResult NativeLinkCore::PublishFromServer(const std::string& topicPath, const TopicValue& value)
    {
        return PublishInternal(topicPath, value, "server", true);
    }

    WriteResult NativeLinkCore::PublishInternal(
        const std::string& topicPath,
        const TopicValue& value,
        const std::string& sourceClientId,
        bool allowServerOnly
    )
    {
        TopicRuntime* topic = FindTopic(topicPath);
        if (topic == nullptr)
        {
            UpdateEnvelope rejectEvent;
            rejectEvent.serverSessionId = m_serverSessionId;
            rejectEvent.topicPath = topicPath;
            rejectEvent.sourceClientId = sourceClientId;
            rejectEvent.deliveryKind = DeliveryKind::LiveCommandReject;
            rejectEvent.value = value;
            rejectEvent.rejectionReason = WriteRejectReason::UnknownTopic;
            EnqueueEventForClient(sourceClientId, rejectEvent);
            return { false, WriteRejectReason::UnknownTopic, 0 };
        }

        if (!TopicValueMatches(topic->descriptor.valueType, value))
        {
            UpdateEnvelope rejectEvent = BuildEnvelope(*topic, DeliveryKind::LiveCommandReject, 0);
            rejectEvent.value = value;
            rejectEvent.rejectionReason = WriteRejectReason::WrongType;
            EnqueueEventForClient(sourceClientId, rejectEvent);
            return { false, WriteRejectReason::WrongType, 0 };
        }

        if (topic->descriptor.writerPolicy == WriterPolicy::ServerOnly && !allowServerOnly)
        {
            UpdateEnvelope rejectEvent = BuildEnvelope(*topic, DeliveryKind::LiveCommandReject, 0);
            rejectEvent.value = value;
            rejectEvent.rejectionReason = WriteRejectReason::ReadOnly;
            EnqueueEventForClient(sourceClientId, rejectEvent);
            return { false, WriteRejectReason::ReadOnly, 0 };
        }

        if (topic->descriptor.writerPolicy == WriterPolicy::LeaseSingleWriter && !allowServerOnly)
        {
            if (topic->leaseHolderClientId.empty())
            {
                UpdateEnvelope rejectEvent = BuildEnvelope(*topic, DeliveryKind::LiveCommandReject, 0);
                rejectEvent.value = value;
                rejectEvent.rejectionReason = WriteRejectReason::LeaseRequired;
                EnqueueEventForClient(sourceClientId, rejectEvent);
                return { false, WriteRejectReason::LeaseRequired, 0 };
            }
            if (topic->leaseHolderClientId != sourceClientId)
            {
                UpdateEnvelope rejectEvent = BuildEnvelope(*topic, DeliveryKind::LiveCommandReject, 0);
                rejectEvent.value = value;
                rejectEvent.rejectionReason = WriteRejectReason::LeaseNotHolder;
                EnqueueEventForClient(sourceClientId, rejectEvent);
                return { false, WriteRejectReason::LeaseNotHolder, 0 };
            }
        }

        topic->hasLatestValue = true;
        topic->latestValue = value;
        topic->latestSourceClientId = sourceClientId;
        topic->latestValueTime = Now();

        const std::uint64_t serverSequence = m_nextServerSequence++;
        const UpdateEnvelope liveEvent = BuildEnvelope(*topic, DetermineLiveDeliveryKind(topic->descriptor.topicKind), serverSequence);
        EnqueueLiveEvent(liveEvent);

        if (!allowServerOnly && topic->descriptor.topicKind == TopicKind::Command)
        {
            UpdateEnvelope ackEvent = liveEvent;
            ackEvent.deliveryKind = DeliveryKind::LiveCommandAck;
            EnqueueEventForClient(sourceClientId, ackEvent);
        }

        return { true, WriteRejectReason::None, serverSequence };
    }

    std::vector<SnapshotEvent> NativeLinkCore::BuildSnapshotForClient(const std::string& clientId) const
    {
        static_cast<void>(clientId);

        std::vector<SnapshotEvent> events;
        events.push_back({ SnapshotEventKind::DescriptorSnapshotBegin, false, {}, false, {} });

        for (const TopicRuntime& topic : m_topics)
        {
            SnapshotEvent descriptorEvent;
            descriptorEvent.kind = SnapshotEventKind::Descriptor;
            descriptorEvent.hasDescriptor = true;
            descriptorEvent.descriptor = topic.descriptor;
            events.push_back(descriptorEvent);
        }

        events.push_back({ SnapshotEventKind::DescriptorSnapshotEnd, false, {}, false, {} });
        events.push_back({ SnapshotEventKind::StateSnapshotBegin, false, {}, false, {} });

        for (const TopicRuntime& topic : m_topics)
        {
            if (!topic.hasLatestValue)
            {
                continue;
            }
            if (topic.descriptor.topicKind != TopicKind::State)
            {
                continue;
            }
            if (topic.descriptor.retentionMode != RetentionMode::LatestValue)
            {
                continue;
            }
            if (!topic.descriptor.replayOnSubscribe)
            {
                continue;
            }

            SnapshotEvent updateEvent;
            updateEvent.kind = SnapshotEventKind::Update;
            updateEvent.hasUpdate = true;
            updateEvent.update = BuildEnvelope(topic, DeliveryKind::SnapshotState, 0);
            events.push_back(updateEvent);
        }

        events.push_back({ SnapshotEventKind::StateSnapshotEnd, false, {}, false, {} });
        events.push_back({ SnapshotEventKind::LiveBegin, false, {}, false, {} });
        return events;
    }

    std::vector<UpdateEnvelope> NativeLinkCore::DrainClientEvents(const std::string& clientId)
    {
        ClientRuntime* client = FindClient(clientId);
        if (client == nullptr)
        {
            return {};
        }

        std::vector<UpdateEnvelope> events;
        while (!client->pendingEvents.empty())
        {
            events.push_back(client->pendingEvents.front());
            client->pendingEvents.pop_front();
        }
        return events;
    }

    bool NativeLinkCore::TryGetLatestValue(const std::string& topicPath, TopicValue& outValue) const
    {
        const TopicRuntime* topic = FindTopic(topicPath);
        if (topic == nullptr || !topic->hasLatestValue)
        {
            return false;
        }

        outValue = topic->latestValue;
        return true;
    }

    TopicLeaseInfo NativeLinkCore::GetTopicLeaseInfo(const std::string& topicPath) const
    {
        const TopicRuntime* topic = FindTopic(topicPath);
        if (topic == nullptr || topic->leaseHolderClientId.empty())
        {
            return {};
        }

        TopicLeaseInfo info;
        info.hasLeaseHolder = true;
        info.leaseHolderClientId = topic->leaseHolderClientId;
        return info;
    }

    void NativeLinkCore::BeginNewSession()
    {
        ++m_serverSessionId;
        m_nextServerSequence = 1;

        for (TopicRuntime& topic : m_topics)
        {
            topic.leaseHolderClientId.clear();
        }

        for (ClientRuntime& client : m_clients)
        {
            client.pendingEvents.clear();
        }
    }

    std::uint64_t NativeLinkCore::GetServerSessionId() const
    {
        return m_serverSessionId;
    }

    const NativeLinkCore::TopicRuntime* NativeLinkCore::FindTopic(const std::string& topicPath) const
    {
        const auto it = std::find_if(m_topics.begin(), m_topics.end(), [&topicPath](const TopicRuntime& topic)
        {
            return topic.descriptor.topicPath == topicPath;
        });
        return it == m_topics.end() ? nullptr : &(*it);
    }

    NativeLinkCore::TopicRuntime* NativeLinkCore::FindTopic(const std::string& topicPath)
    {
        const auto it = std::find_if(m_topics.begin(), m_topics.end(), [&topicPath](const TopicRuntime& topic)
        {
            return topic.descriptor.topicPath == topicPath;
        });
        return it == m_topics.end() ? nullptr : &(*it);
    }

    const NativeLinkCore::ClientRuntime* NativeLinkCore::FindClient(const std::string& clientId) const
    {
        const auto it = std::find_if(m_clients.begin(), m_clients.end(), [&clientId](const ClientRuntime& client)
        {
            return client.clientId == clientId;
        });
        return it == m_clients.end() ? nullptr : &(*it);
    }

    NativeLinkCore::ClientRuntime* NativeLinkCore::FindClient(const std::string& clientId)
    {
        const auto it = std::find_if(m_clients.begin(), m_clients.end(), [&clientId](const ClientRuntime& client)
        {
            return client.clientId == clientId;
        });
        return it == m_clients.end() ? nullptr : &(*it);
    }

    RegisterTopicResult NativeLinkCore::ValidateDescriptor(const TopicDescriptor& descriptor)
    {
        if (descriptor.topicPath.empty())
        {
            return { false, "topic path is required", 0 };
        }

        if (descriptor.topicKind == TopicKind::Command && descriptor.replayOnSubscribe)
        {
            return { false, "command topics must not replay on subscribe", 0 };
        }

        if (descriptor.topicKind != TopicKind::State && descriptor.retentionMode != RetentionMode::None)
        {
            return { false, "only state topics may be retained", 0 };
        }

        if (descriptor.topicKind == TopicKind::Event && descriptor.writerPolicy != WriterPolicy::ServerOnly)
        {
            return { false, "event topics must be server-only", 0 };
        }

        return { true, {}, 0 };
    }

    UpdateEnvelope NativeLinkCore::BuildEnvelope(const TopicRuntime& topic, DeliveryKind deliveryKind, std::uint64_t serverSequence) const
    {
        UpdateEnvelope envelope;
        envelope.serverSessionId = m_serverSessionId;
        envelope.serverSequence = serverSequence;
        envelope.topicId = topic.topicId;
        envelope.topicPath = topic.descriptor.topicPath;
        envelope.sourceClientId = topic.latestSourceClientId;
        envelope.deliveryKind = deliveryKind;
        envelope.value = topic.latestValue;
        envelope.ttlMs = topic.descriptor.ttlMs;

        if (topic.descriptor.ttlMs > 0)
        {
            const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(Now() - topic.latestValueTime).count();
            envelope.ageMsAtEmit = static_cast<int>(ageMs);
            if (ageMs > topic.descriptor.ttlMs)
            {
                envelope.isStale = true;
                envelope.freshnessReason = FreshnessReason::TtlExpired;
            }
        }

        return envelope;
    }

    DeliveryKind NativeLinkCore::DetermineLiveDeliveryKind(TopicKind topicKind) const
    {
        switch (topicKind)
        {
            case TopicKind::State:
                return DeliveryKind::LiveState;
            case TopicKind::Command:
                return DeliveryKind::LiveCommand;
            case TopicKind::Event:
                return DeliveryKind::LiveEvent;
        }

        return DeliveryKind::LiveState;
    }

    void NativeLinkCore::EnqueueLiveEvent(const UpdateEnvelope& envelope)
    {
        for (ClientRuntime& client : m_clients)
        {
            client.pendingEvents.push_back(envelope);
        }
    }

    void NativeLinkCore::EnqueueEventForClient(const std::string& clientId, const UpdateEnvelope& envelope)
    {
        ClientRuntime* client = FindClient(clientId);
        if (client == nullptr)
        {
            return;
        }

        client->pendingEvents.push_back(envelope);
    }

    std::chrono::steady_clock::time_point NativeLinkCore::Now() const
    {
        if (m_clock)
        {
            return m_clock();
        }

        return std::chrono::steady_clock::now();
    }
}
