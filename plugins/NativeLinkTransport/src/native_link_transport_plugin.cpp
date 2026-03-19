#define SD_TRANSPORT_PLUGIN_EXPORTS 1

#include "native_link_core.h"
#include "transport/dashboard_transport_plugin_api.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace
{
    using sd::nativelink::DeliveryKind;
    using sd::nativelink::NativeLinkCore;
    using sd::nativelink::TopicDescriptor;
    using sd::nativelink::TopicKind;
    using sd::nativelink::TopicValue;
    using sd::nativelink::ValueType;
    using sd::nativelink::WriterPolicy;

    const sd_transport_connection_field_descriptor_v1 kNativeLinkConnectionFields[] = {
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
        }
    };

    struct SharedNativeLinkServer
    {
        std::mutex mutex;
        NativeLinkCore core;
        std::unordered_map<std::string, bool> initializedTopicsByChannel;
    };

    struct NativeLinkPluginInstance
    {
        bool running = false;
        std::string clientName = "SmartDashboardApp";
        std::string channelId = "native-link-default";
        std::string runtimeClientKey;
        sd_transport_callbacks_v1 callbacks {};
    };

    SharedNativeLinkServer& GetSharedServer()
    {
        static SharedNativeLinkServer server;
        return server;
    }

    std::string MakeClientKey(const std::string& channelId, const std::string& clientName)
    {
        return channelId + "::" + clientName;
    }

    std::string MakeRuntimeClientKey(const NativeLinkPluginInstance& instance)
    {
        std::ostringstream builder;
        builder << MakeClientKey(instance.channelId, instance.clientName) << "#" << static_cast<const void*>(&instance);
        return builder.str();
    }

    std::unordered_map<std::string, NativeLinkPluginInstance*>& GetLiveInstances()
    {
        static std::unordered_map<std::string, NativeLinkPluginInstance*> instances;
        return instances;
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

    TopicDescriptor MakeDescriptor(
        const std::string& path,
        TopicKind kind,
        ValueType type,
        WriterPolicy writerPolicy,
        bool retained,
        bool replayOnSubscribe
    )
    {
        TopicDescriptor descriptor;
        descriptor.topicPath = path;
        descriptor.topicKind = kind;
        descriptor.valueType = type;
        descriptor.writerPolicy = writerPolicy;
        descriptor.retentionMode = retained ? sd::nativelink::RetentionMode::LatestValue : sd::nativelink::RetentionMode::None;
        descriptor.replayOnSubscribe = replayOnSubscribe;
        return descriptor;
    }

    void PublishUpdateCallback(const NativeLinkPluginInstance& instance, const sd::nativelink::UpdateEnvelope& event)
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

    void DrainAndPublishEventsLocked(const NativeLinkPluginInstance& instance, SharedNativeLinkServer& server)
    {
        const std::vector<sd::nativelink::UpdateEnvelope> events = server.core.DrainClientEvents(instance.runtimeClientKey);
        for (const sd::nativelink::UpdateEnvelope& event : events)
        {
            if (event.deliveryKind == DeliveryKind::LiveCommandAck || event.deliveryKind == DeliveryKind::LiveCommandReject)
            {
                continue;
            }

            PublishUpdateCallback(instance, event);
        }
    }

    std::vector<NativeLinkPluginInstance*> GetRunningInstancesForChannelLocked(
        const std::string& channelId
    )
    {
        std::vector<NativeLinkPluginInstance*> result;

        for (const auto& [_, instance] : GetLiveInstances())
        {
            if (instance != nullptr && instance->running && instance->channelId == channelId)
            {
                result.push_back(instance);
            }
        }

        return result;
    }

    void PublishSnapshotLocked(const NativeLinkPluginInstance& instance, SharedNativeLinkServer& server)
    {
        const sd::nativelink::ClientSessionView session = server.core.ConnectClient(instance.runtimeClientKey);
        for (const sd::nativelink::SnapshotEvent& event : session.snapshotEvents)
        {
            if (event.kind == sd::nativelink::SnapshotEventKind::Update && event.hasUpdate)
            {
                PublishUpdateCallback(instance, event.update);
            }
        }
    }

    void EnsureDefaultTopicsLocked(SharedNativeLinkServer& server, const NativeLinkPluginInstance& instance)
    {
        if (server.initializedTopicsByChannel[instance.channelId])
        {
            return;
        }

        // Ian: The point of the real two-dashboard probe is shared authority,
        // not two isolated per-process caches. Keep one in-process server per
        // channel so both SmartDashboard processes are looking at the same
        // retained state and live writes during this early validation phase.
        server.core.RegisterTopic(MakeDescriptor(
            "Test/Auton_Selection/AutoChooser/selected",
            TopicKind::State,
            ValueType::String,
            WriterPolicy::LeaseSingleWriter,
            true,
            true
        ));
        server.core.RegisterTopic(MakeDescriptor(
            "TestMove",
            TopicKind::State,
            ValueType::Double,
            WriterPolicy::LeaseSingleWriter,
            true,
            true
        ));
        server.core.RegisterTopic(MakeDescriptor(
            "Timer",
            TopicKind::State,
            ValueType::Double,
            WriterPolicy::ServerOnly,
            true,
            true
        ));
        server.core.RegisterTopic(MakeDescriptor(
            "Y_ft",
            TopicKind::State,
            ValueType::Double,
            WriterPolicy::ServerOnly,
            true,
            true
        ));

        const std::string clientKey = instance.runtimeClientKey;
        server.core.AcquireLease("Test/Auton_Selection/AutoChooser/selected", clientKey);
        server.core.Publish("Test/Auton_Selection/AutoChooser/selected", TopicValue::String("Do Nothing"), clientKey);
        server.core.AcquireLease("TestMove", clientKey);
        server.core.Publish("TestMove", TopicValue::Double(0.0), clientKey);
        server.core.PublishFromServer("Timer", TopicValue::Double(15.0));
        server.core.PublishFromServer("Y_ft", TopicValue::Double(0.0));

        server.initializedTopicsByChannel[instance.channelId] = true;
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
        const sd_transport_callbacks_v1* callbacks
    )
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
        instance->channelId = "native-link-default";
        instance->runtimeClientKey = MakeRuntimeClientKey(*instance);
        instance->running = true;

        SharedNativeLinkServer& server = GetSharedServer();
        std::lock_guard<std::mutex> lock(server.mutex);

        EnsureDefaultTopicsLocked(server, *instance);
        GetLiveInstances()[instance->runtimeClientKey] = instance;

        PublishSnapshotLocked(*instance, server);

        if (instance->callbacks.on_connection_state != nullptr)
        {
            instance->callbacks.on_connection_state(instance->callbacks.user_data, SD_TRANSPORT_CONNECTION_STATE_CONNECTED);
        }

        return 1;
    }

    void StopNativeLink(sd_transport_instance_v1 instanceHandle)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr)
        {
            return;
        }

        SharedNativeLinkServer& server = GetSharedServer();
        std::lock_guard<std::mutex> lock(server.mutex);

        if (instance->running && instance->callbacks.on_connection_state != nullptr)
        {
            instance->callbacks.on_connection_state(instance->callbacks.user_data, SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED);
        }

        GetLiveInstances().erase(instance->runtimeClientKey);
        server.core.DisconnectClient(instance->runtimeClientKey);
        instance->running = false;
    }

    int PublishBool(sd_transport_instance_v1 instanceHandle, const char* key, int value)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr || key == nullptr)
        {
            return 0;
        }

        SharedNativeLinkServer& server = GetSharedServer();
        std::lock_guard<std::mutex> lock(server.mutex);
        const std::string clientKey = instance->runtimeClientKey;
        server.core.AcquireLease(key, clientKey);
        const sd::nativelink::WriteResult result = server.core.Publish(key, TopicValue::Bool(value != 0), clientKey);

        for (NativeLinkPluginInstance* liveInstance : GetRunningInstancesForChannelLocked(instance->channelId))
        {
            if (liveInstance == nullptr)
            {
                continue;
            }

            DrainAndPublishEventsLocked(*liveInstance, server);
        }
        return result.accepted ? 1 : 0;
    }

    int PublishDouble(sd_transport_instance_v1 instanceHandle, const char* key, double value)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr || key == nullptr)
        {
            return 0;
        }

        SharedNativeLinkServer& server = GetSharedServer();
        std::lock_guard<std::mutex> lock(server.mutex);
        const std::string clientKey = instance->runtimeClientKey;
        server.core.AcquireLease(key, clientKey);
        const sd::nativelink::WriteResult result = server.core.Publish(key, TopicValue::Double(value), clientKey);

        // Ian: A real shared-authority write must fan out to every connected
        // dashboard client, not just the writer. That is the entire point of
        // this next validation step before we involve Robot_Simulation.
        for (NativeLinkPluginInstance* liveInstance : GetRunningInstancesForChannelLocked(instance->channelId))
        {
            if (liveInstance == nullptr)
            {
                continue;
            }

            DrainAndPublishEventsLocked(*liveInstance, server);
        }

        return result.accepted ? 1 : 0;
    }

    int PublishString(sd_transport_instance_v1 instanceHandle, const char* key, const char* value)
    {
        auto* instance = static_cast<NativeLinkPluginInstance*>(instanceHandle);
        if (instance == nullptr || key == nullptr || value == nullptr)
        {
            return 0;
        }

        SharedNativeLinkServer& server = GetSharedServer();
        std::lock_guard<std::mutex> lock(server.mutex);
        const std::string clientKey = instance->runtimeClientKey;
        server.core.AcquireLease(key, clientKey);
        const sd::nativelink::WriteResult result = server.core.Publish(key, TopicValue::String(value), clientKey);

        for (NativeLinkPluginInstance* liveInstance : GetRunningInstancesForChannelLocked(instance->channelId))
        {
            if (liveInstance == nullptr)
            {
                continue;
            }

            DrainAndPublishEventsLocked(*liveInstance, server);
        }

        return result.accepted ? 1 : 0;
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
        0u,
        &GetNativeLinkBoolProperty,
        &GetNativeLinkConnectionFields,
        &kNativeLinkApi
    };
}

extern "C" SD_TRANSPORT_PLUGIN_EXPORT const sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void)
{
    return &kNativeLinkDescriptor;
}
