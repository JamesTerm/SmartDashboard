#include "native_link_core.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace sd::nativelink
{
    namespace
    {
        using namespace std::chrono_literals;

        TopicDescriptor MakeStateTopic(const std::string& path)
        {
            TopicDescriptor descriptor;
            descriptor.topicPath = path;
            descriptor.topicKind = TopicKind::State;
            descriptor.valueType = ValueType::Double;
            descriptor.retentionMode = RetentionMode::LatestValue;
            descriptor.replayOnSubscribe = true;
            descriptor.writerPolicy = WriterPolicy::LeaseSingleWriter;
            return descriptor;
        }

        TopicDescriptor MakeStringStateTopic(const std::string& path)
        {
            TopicDescriptor descriptor;
            descriptor.topicPath = path;
            descriptor.topicKind = TopicKind::State;
            descriptor.valueType = ValueType::String;
            descriptor.retentionMode = RetentionMode::LatestValue;
            descriptor.replayOnSubscribe = true;
            descriptor.writerPolicy = WriterPolicy::LeaseSingleWriter;
            return descriptor;
        }

        TopicDescriptor MakeServerTelemetryTopic(const std::string& path)
        {
            TopicDescriptor descriptor;
            descriptor.topicPath = path;
            descriptor.topicKind = TopicKind::State;
            descriptor.valueType = ValueType::Double;
            descriptor.retentionMode = RetentionMode::LatestValue;
            descriptor.replayOnSubscribe = true;
            descriptor.writerPolicy = WriterPolicy::ServerOnly;
            return descriptor;
        }

        TopicDescriptor MakeCommandTopic(const std::string& path)
        {
            TopicDescriptor descriptor;
            descriptor.topicPath = path;
            descriptor.topicKind = TopicKind::Command;
            descriptor.valueType = ValueType::Bool;
            descriptor.retentionMode = RetentionMode::None;
            descriptor.replayOnSubscribe = false;
            descriptor.writerPolicy = WriterPolicy::LeaseSingleWriter;
            return descriptor;
        }

        const SnapshotEvent* FindFirstUpdateEvent(const std::vector<SnapshotEvent>& events)
        {
            for (const SnapshotEvent& event : events)
            {
                if (event.kind == SnapshotEventKind::Update && event.hasUpdate)
                {
                    return &event;
                }
            }

            return nullptr;
        }

        const UpdateEnvelope* FindEventByTopic(const std::vector<UpdateEnvelope>& events, const std::string& topicPath)
        {
            for (const UpdateEnvelope& event : events)
            {
                if (event.topicPath == topicPath)
                {
                    return &event;
                }
            }

            return nullptr;
        }

        const UpdateEnvelope* FindLastEventByTopic(const std::vector<UpdateEnvelope>& events, const std::string& topicPath)
        {
            for (auto it = events.rbegin(); it != events.rend(); ++it)
            {
                if (it->topicPath == topicPath)
                {
                    return &(*it);
                }
            }

            return nullptr;
        }

        class AutonomousScenarioHarness
        {
        public:
            explicit AutonomousScenarioHarness(NativeLinkCore core = NativeLinkCore())
                : m_core(std::move(core))
            {
                const bool registered = RegisterTopics();
                EXPECT_TRUE(registered);
            }

            void ConnectDashboard(const std::string& clientId)
            {
                m_dashboardId = clientId;
                m_core.ConnectClient(clientId);
            }

            void ConnectWatcher(const std::string& clientId)
            {
                m_watcherId = clientId;
                m_core.ConnectClient(clientId);
            }

            bool SetAutonSelection(const std::string& selection)
            {
                return PublishDashboardValue("Test/Auton_Selection/AutoChooser/selected", TopicValue::String(selection));
            }

            bool SetMoveDistance(double distanceFeet)
            {
                return PublishDashboardValue("TestMove", TopicValue::Double(distanceFeet));
            }

            void RunAutonomous(int stepCount)
            {
                TopicValue selectedValue;
                TopicValue moveDistance;

                const bool hasSelection = m_core.TryGetLatestValue("Test/Auton_Selection/AutoChooser/selected", selectedValue);
                const bool hasMoveDistance = m_core.TryGetLatestValue("TestMove", moveDistance);

                const bool moveForward = hasSelection
                    && selectedValue.type == ValueType::String
                    && selectedValue.stringValue == "Move Forward";
                const double targetDistance = hasMoveDistance && moveDistance.type == ValueType::Double
                    ? moveDistance.doubleValue
                    : 0.0;

                for (int step = 0; step < stepCount; ++step)
                {
                    const double timerValue = static_cast<double>(stepCount - step);
                    EXPECT_TRUE(m_core.PublishFromServer("Timer", TopicValue::Double(timerValue)).accepted);

                    double yFeet = 0.0;
                    if (moveForward)
                    {
                        yFeet = targetDistance * static_cast<double>(step + 1) / static_cast<double>(stepCount);
                    }

                    EXPECT_TRUE(m_core.PublishFromServer("Y_ft", TopicValue::Double(yFeet)).accepted);
                }
            }

            std::vector<UpdateEnvelope> DrainDashboardEvents()
            {
                return m_dashboardId.empty() ? std::vector<UpdateEnvelope> {} : m_core.DrainClientEvents(m_dashboardId);
            }

            std::vector<UpdateEnvelope> DrainWatcherEvents()
            {
                return m_watcherId.empty() ? std::vector<UpdateEnvelope> {} : m_core.DrainClientEvents(m_watcherId);
            }

            void ConnectSecondDashboard(const std::string& clientId)
            {
                m_secondDashboardId = clientId;
                m_core.ConnectClient(clientId);
            }

            std::vector<UpdateEnvelope> DrainSecondDashboardEvents()
            {
                return m_secondDashboardId.empty() ? std::vector<UpdateEnvelope> {} : m_core.DrainClientEvents(m_secondDashboardId);
            }

            std::vector<SnapshotEvent> ReconnectSecondDashboard()
            {
                if (!m_secondDashboardId.empty())
                {
                    m_core.DisconnectClient(m_secondDashboardId);
                    return m_core.ConnectClient(m_secondDashboardId).snapshotEvents;
                }

                return {};
            }

            std::vector<SnapshotEvent> ReconnectDashboard()
            {
                if (!m_dashboardId.empty())
                {
                    m_core.DisconnectClient(m_dashboardId);
                    return m_core.ConnectClient(m_dashboardId).snapshotEvents;
                }

                return {};
            }

            void RestartServer()
            {
                m_core.BeginNewSession();
                if (!m_dashboardId.empty())
                {
                    m_core.ConnectClient(m_dashboardId);
                }
                if (!m_watcherId.empty())
                {
                    m_core.ConnectClient(m_watcherId);
                }
                if (!m_secondDashboardId.empty())
                {
                    m_core.ConnectClient(m_secondDashboardId);
                }
            }

            NativeLinkCore& Core()
            {
                return m_core;
            }

        private:
            bool RegisterTopics()
            {
                return m_core.RegisterTopic(MakeStringStateTopic("Test/Auton_Selection/AutoChooser/selected")).ok
                    && m_core.RegisterTopic(MakeStateTopic("TestMove")).ok
                    && m_core.RegisterTopic(MakeServerTelemetryTopic("Timer")).ok
                    && m_core.RegisterTopic(MakeServerTelemetryTopic("Y_ft")).ok;
            }

            bool PublishDashboardValue(const std::string& topicPath, const TopicValue& value)
            {
                if (m_dashboardId.empty())
                {
                    return false;
                }

                if (!m_core.AcquireLease(topicPath, m_dashboardId))
                {
                    return false;
                }

                return m_core.Publish(topicPath, value, m_dashboardId).accepted;
            }

            NativeLinkCore m_core;
            std::string m_dashboardId;
            std::string m_secondDashboardId;
            std::string m_watcherId;
        };
    }

    TEST(NativeLinkCoreTests, RejectsReplayableCommandDescriptor)
    {
        NativeLinkCore core;

        TopicDescriptor descriptor;
        descriptor.topicPath = "Robot/Reset";
        descriptor.topicKind = TopicKind::Command;
        descriptor.valueType = ValueType::Bool;
        descriptor.replayOnSubscribe = true;

        const RegisterTopicResult result = core.RegisterTopic(descriptor);
        EXPECT_FALSE(result.ok);
    }

    TEST(NativeLinkCoreTests, SnapshotOrdersDescriptorsBeforeReplayableState)
    {
        NativeLinkCore core;

        TopicDescriptor stateDescriptor = MakeStateTopic("Robot/TestMove");
        ASSERT_TRUE(core.RegisterTopic(stateDescriptor).ok);
        ASSERT_TRUE(core.AcquireLease("Robot/TestMove", "dashboard-a"));

        TopicDescriptor commandDescriptor;
        commandDescriptor.topicPath = "Robot/Reset";
        commandDescriptor.topicKind = TopicKind::Command;
        commandDescriptor.valueType = ValueType::Bool;
        commandDescriptor.writerPolicy = WriterPolicy::LeaseSingleWriter;
        ASSERT_TRUE(core.RegisterTopic(commandDescriptor).ok);
        ASSERT_TRUE(core.AcquireLease("Robot/Reset", "dashboard-a"));

        ASSERT_TRUE(core.Publish("Robot/TestMove", TopicValue::Double(3.5), "dashboard-a").accepted);
        ASSERT_TRUE(core.Publish("Robot/Reset", TopicValue::Bool(true), "dashboard-a").accepted);

        const std::vector<SnapshotEvent> snapshot = core.BuildSnapshotForClient("dashboard-b");
        ASSERT_GE(snapshot.size(), 8u);

        EXPECT_EQ(snapshot[0].kind, SnapshotEventKind::DescriptorSnapshotBegin);
        EXPECT_EQ(snapshot[1].kind, SnapshotEventKind::Descriptor);
        EXPECT_EQ(snapshot[2].kind, SnapshotEventKind::Descriptor);
        EXPECT_EQ(snapshot[3].kind, SnapshotEventKind::DescriptorSnapshotEnd);
        EXPECT_EQ(snapshot[4].kind, SnapshotEventKind::StateSnapshotBegin);
        EXPECT_EQ(snapshot[5].kind, SnapshotEventKind::Update);
        EXPECT_EQ(snapshot[6].kind, SnapshotEventKind::StateSnapshotEnd);
        EXPECT_EQ(snapshot[7].kind, SnapshotEventKind::LiveBegin);

        ASSERT_TRUE(snapshot[5].hasUpdate);
        EXPECT_EQ(snapshot[5].update.topicPath, "Robot/TestMove");
        EXPECT_EQ(snapshot[5].update.deliveryKind, DeliveryKind::SnapshotState);
        EXPECT_EQ(snapshot[5].update.value.type, ValueType::Double);
        EXPECT_DOUBLE_EQ(snapshot[5].update.value.doubleValue, 3.5);
    }

    TEST(NativeLinkCoreTests, LeaseWriterPolicyRejectsNonHolder)
    {
        NativeLinkCore core;

        TopicDescriptor descriptor = MakeStateTopic("Robot/TestMove");
        ASSERT_TRUE(core.RegisterTopic(descriptor).ok);

        const WriteResult noLeaseResult = core.Publish("Robot/TestMove", TopicValue::Double(2.0), "dashboard-a");
        EXPECT_FALSE(noLeaseResult.accepted);
        EXPECT_EQ(noLeaseResult.rejectionReason, WriteRejectReason::LeaseRequired);

        ASSERT_TRUE(core.AcquireLease("Robot/TestMove", "dashboard-a"));

        const WriteResult wrongHolderResult = core.Publish("Robot/TestMove", TopicValue::Double(2.0), "dashboard-b");
        EXPECT_FALSE(wrongHolderResult.accepted);
        EXPECT_EQ(wrongHolderResult.rejectionReason, WriteRejectReason::LeaseNotHolder);

        const WriteResult acceptedResult = core.Publish("Robot/TestMove", TopicValue::Double(2.0), "dashboard-a");
        EXPECT_TRUE(acceptedResult.accepted);
        EXPECT_GT(acceptedResult.serverSequence, 0u);
    }

    TEST(NativeLinkCoreTests, SnapshotMarksRetainedStateStaleAfterTtl)
    {
        auto now = std::chrono::steady_clock::now();
        NativeLinkCore core([&now]()
        {
            return now;
        });

        TopicDescriptor descriptor = MakeStateTopic("Robot/Battery");
        descriptor.ttlMs = 50;
        ASSERT_TRUE(core.RegisterTopic(descriptor).ok);
        ASSERT_TRUE(core.AcquireLease("Robot/Battery", "dashboard-a"));
        ASSERT_TRUE(core.Publish("Robot/Battery", TopicValue::Double(12.4), "dashboard-a").accepted);

        now += 80ms;

        const std::vector<SnapshotEvent> snapshot = core.BuildSnapshotForClient("dashboard-b");
        const SnapshotEvent* updateEvent = FindFirstUpdateEvent(snapshot);
        ASSERT_NE(updateEvent, nullptr);
        EXPECT_TRUE(updateEvent->update.isStale);
        EXPECT_EQ(updateEvent->update.freshnessReason, FreshnessReason::TtlExpired);
        EXPECT_GE(updateEvent->update.ageMsAtEmit, 80);
    }

    TEST(NativeLinkCoreTests, NewSessionResetsLeases)
    {
        NativeLinkCore core;

        TopicDescriptor descriptor = MakeStateTopic("Robot/TestMove");
        ASSERT_TRUE(core.RegisterTopic(descriptor).ok);
        ASSERT_TRUE(core.AcquireLease("Robot/TestMove", "dashboard-a"));

        const std::uint64_t beforeSession = core.GetServerSessionId();
        core.BeginNewSession();

        EXPECT_GT(core.GetServerSessionId(), beforeSession);

        const WriteResult result = core.Publish("Robot/TestMove", TopicValue::Double(1.0), "dashboard-a");
        EXPECT_FALSE(result.accepted);
        EXPECT_EQ(result.rejectionReason, WriteRejectReason::LeaseRequired);
    }

    TEST(NativeLinkCoreTests, AutonomousScenarioSingleRunReplaysControlsAndStreamsTelemetry)
    {
        AutonomousScenarioHarness harness;
        harness.ConnectDashboard("dashboard-a");
        harness.ConnectWatcher("watcher-a");

        ASSERT_TRUE(harness.SetAutonSelection("Move Forward"));
        ASSERT_TRUE(harness.SetMoveDistance(3.5));
        harness.DrainDashboardEvents();
        harness.DrainWatcherEvents();

        harness.RunAutonomous(5);

        const std::vector<UpdateEnvelope> dashboardEvents = harness.DrainDashboardEvents();
        const std::vector<UpdateEnvelope> watcherEvents = harness.DrainWatcherEvents();
        ASSERT_FALSE(dashboardEvents.empty());
        ASSERT_EQ(dashboardEvents.size(), watcherEvents.size());

        const UpdateEnvelope* timerEvent = FindEventByTopic(dashboardEvents, "Timer");
        const UpdateEnvelope* yFeetEvent = FindEventByTopic(dashboardEvents, "Y_ft");
        ASSERT_NE(timerEvent, nullptr);
        ASSERT_NE(yFeetEvent, nullptr);
        EXPECT_EQ(timerEvent->deliveryKind, DeliveryKind::LiveState);
        EXPECT_EQ(yFeetEvent->deliveryKind, DeliveryKind::LiveState);

        TopicValue latestSelection;
        TopicValue latestMove;
        TopicValue latestYFeet;
        ASSERT_TRUE(harness.Core().TryGetLatestValue("Test/Auton_Selection/AutoChooser/selected", latestSelection));
        ASSERT_TRUE(harness.Core().TryGetLatestValue("TestMove", latestMove));
        ASSERT_TRUE(harness.Core().TryGetLatestValue("Y_ft", latestYFeet));
        EXPECT_EQ(latestSelection.stringValue, "Move Forward");
        EXPECT_DOUBLE_EQ(latestMove.doubleValue, 3.5);
        EXPECT_DOUBLE_EQ(latestYFeet.doubleValue, 3.5);

        const std::vector<SnapshotEvent> reconnectSnapshot = harness.ReconnectDashboard();
        int snapshotUpdateCount = 0;
        bool sawSelected = false;
        bool sawMoveDistance = false;
        for (const SnapshotEvent& event : reconnectSnapshot)
        {
            if (event.kind == SnapshotEventKind::Update && event.hasUpdate)
            {
                ++snapshotUpdateCount;
                if (event.update.topicPath == "Test/Auton_Selection/AutoChooser/selected")
                {
                    sawSelected = true;
                    EXPECT_EQ(event.update.value.stringValue, "Move Forward");
                }
                if (event.update.topicPath == "TestMove")
                {
                    sawMoveDistance = true;
                    EXPECT_DOUBLE_EQ(event.update.value.doubleValue, 3.5);
                }
            }
        }
        EXPECT_GE(snapshotUpdateCount, 4);
        EXPECT_TRUE(sawSelected);
        EXPECT_TRUE(sawMoveDistance);
    }

    TEST(NativeLinkCoreTests, AutonomousScenarioSurvivesFiveServerRestarts)
    {
        AutonomousScenarioHarness harness;
        harness.ConnectDashboard("dashboard-a");

        for (int iteration = 0; iteration < 5; ++iteration)
        {
            ASSERT_TRUE(harness.SetAutonSelection("Move Forward"));
            ASSERT_TRUE(harness.SetMoveDistance(3.5));
            harness.DrainDashboardEvents();

            harness.RunAutonomous(4);
            const std::vector<UpdateEnvelope> beforeRestartEvents = harness.DrainDashboardEvents();
            ASSERT_NE(FindEventByTopic(beforeRestartEvents, "Timer"), nullptr);
            ASSERT_NE(FindEventByTopic(beforeRestartEvents, "Y_ft"), nullptr);

            harness.RestartServer();

            const WriteResult staleWrite = harness.Core().Publish("TestMove", TopicValue::Double(3.5), "dashboard-a");
            EXPECT_FALSE(staleWrite.accepted);
            EXPECT_EQ(staleWrite.rejectionReason, WriteRejectReason::LeaseRequired);

            ASSERT_TRUE(harness.SetAutonSelection("Move Forward"));
            ASSERT_TRUE(harness.SetMoveDistance(3.5));
            harness.DrainDashboardEvents();

            harness.RunAutonomous(4);
            const std::vector<UpdateEnvelope> afterRestartEvents = harness.DrainDashboardEvents();
            const UpdateEnvelope* finalYFeet = FindLastEventByTopic(afterRestartEvents, "Y_ft");
            ASSERT_NE(finalYFeet, nullptr);
            EXPECT_DOUBLE_EQ(finalYFeet->value.doubleValue, 3.5);
        }
    }

    TEST(NativeLinkCoreTests, AutonomousScenarioSurvivesFiveDashboardReconnects)
    {
        AutonomousScenarioHarness harness;
        harness.ConnectDashboard("dashboard-a");

        ASSERT_TRUE(harness.SetAutonSelection("Move Forward"));
        ASSERT_TRUE(harness.SetMoveDistance(3.5));
        harness.DrainDashboardEvents();

        for (int iteration = 0; iteration < 5; ++iteration)
        {
            const std::vector<SnapshotEvent> reconnectSnapshot = harness.ReconnectDashboard();
            bool sawSelected = false;
            bool sawMoveDistance = false;
            for (const SnapshotEvent& event : reconnectSnapshot)
            {
                if (event.kind != SnapshotEventKind::Update || !event.hasUpdate)
                {
                    continue;
                }

                if (event.update.topicPath == "Test/Auton_Selection/AutoChooser/selected"
                    && event.update.value.stringValue == "Move Forward")
                {
                    sawSelected = true;
                }
                if (event.update.topicPath == "TestMove"
                    && event.update.value.doubleValue == 3.5)
                {
                    sawMoveDistance = true;
                }
            }
            EXPECT_TRUE(sawSelected);
            EXPECT_TRUE(sawMoveDistance);

            harness.RunAutonomous(4);
            const std::vector<UpdateEnvelope> events = harness.DrainDashboardEvents();
            const UpdateEnvelope* timerEvent = FindLastEventByTopic(events, "Timer");
            const UpdateEnvelope* yFeetEvent = FindLastEventByTopic(events, "Y_ft");
            ASSERT_NE(timerEvent, nullptr);
            ASSERT_NE(yFeetEvent, nullptr);
            EXPECT_GT(timerEvent->serverSequence, 0u);
            EXPECT_DOUBLE_EQ(yFeetEvent->value.doubleValue, 3.5);
        }
    }

    TEST(NativeLinkCoreTests, TwoDashboardsReceiveIdenticalLiveTelemetryWithoutInterference)
    {
        AutonomousScenarioHarness harness;
        harness.ConnectDashboard("dashboard-a");
        harness.ConnectSecondDashboard("dashboard-b");
        harness.ConnectWatcher("watcher-a");

        ASSERT_TRUE(harness.SetAutonSelection("Move Forward"));
        ASSERT_TRUE(harness.SetMoveDistance(3.5));
        harness.DrainDashboardEvents();
        harness.DrainSecondDashboardEvents();
        harness.DrainWatcherEvents();

        harness.RunAutonomous(5);

        const std::vector<UpdateEnvelope> dashboardAEvents = harness.DrainDashboardEvents();
        const std::vector<UpdateEnvelope> dashboardBEvents = harness.DrainSecondDashboardEvents();
        const std::vector<UpdateEnvelope> watcherEvents = harness.DrainWatcherEvents();

        ASSERT_EQ(dashboardAEvents.size(), dashboardBEvents.size());
        ASSERT_EQ(dashboardAEvents.size(), watcherEvents.size());
        ASSERT_FALSE(dashboardAEvents.empty());

        for (std::size_t i = 0; i < dashboardAEvents.size(); ++i)
        {
            EXPECT_EQ(dashboardAEvents[i].topicPath, dashboardBEvents[i].topicPath);
            EXPECT_EQ(dashboardAEvents[i].serverSequence, dashboardBEvents[i].serverSequence);
            EXPECT_EQ(dashboardAEvents[i].topicPath, watcherEvents[i].topicPath);
            EXPECT_EQ(dashboardAEvents[i].serverSequence, watcherEvents[i].serverSequence);
        }

        const UpdateEnvelope* finalYFeetA = FindLastEventByTopic(dashboardAEvents, "Y_ft");
        const UpdateEnvelope* finalYFeetB = FindLastEventByTopic(dashboardBEvents, "Y_ft");
        ASSERT_NE(finalYFeetA, nullptr);
        ASSERT_NE(finalYFeetB, nullptr);
        EXPECT_DOUBLE_EQ(finalYFeetA->value.doubleValue, 3.5);
        EXPECT_DOUBLE_EQ(finalYFeetB->value.doubleValue, 3.5);

        const std::vector<SnapshotEvent> reconnectSnapshot = harness.ReconnectSecondDashboard();
        bool sawSelected = false;
        bool sawMoveDistance = false;
        for (const SnapshotEvent& event : reconnectSnapshot)
        {
            if (event.kind != SnapshotEventKind::Update || !event.hasUpdate)
            {
                continue;
            }

            if (event.update.topicPath == "Test/Auton_Selection/AutoChooser/selected"
                && event.update.value.stringValue == "Move Forward")
            {
                sawSelected = true;
            }
            if (event.update.topicPath == "TestMove"
                && event.update.value.doubleValue == 3.5)
            {
                sawMoveDistance = true;
            }
        }
        EXPECT_TRUE(sawSelected);
        EXPECT_TRUE(sawMoveDistance);

        harness.RunAutonomous(3);
        const std::vector<UpdateEnvelope> postReconnectAEvents = harness.DrainDashboardEvents();
        const std::vector<UpdateEnvelope> postReconnectBEvents = harness.DrainSecondDashboardEvents();
        ASSERT_EQ(postReconnectAEvents.size(), postReconnectBEvents.size());
        EXPECT_NE(FindLastEventByTopic(postReconnectAEvents, "Timer"), nullptr);
        EXPECT_NE(FindLastEventByTopic(postReconnectBEvents, "Timer"), nullptr);
    }

    TEST(NativeLinkCoreTests, TwoDashboardsLeaseContentionIsDeterministic)
    {
        AutonomousScenarioHarness harness;
        harness.ConnectDashboard("dashboard-a");
        harness.ConnectSecondDashboard("dashboard-b");

        ASSERT_TRUE(harness.SetMoveDistance(3.5));

        TopicLeaseInfo leaseInfo = harness.Core().GetTopicLeaseInfo("TestMove");
        ASSERT_TRUE(leaseInfo.hasLeaseHolder);
        EXPECT_EQ(leaseInfo.leaseHolderClientId, "dashboard-a");

        EXPECT_FALSE(harness.Core().AcquireLease("TestMove", "dashboard-b"));

        const WriteResult wrongHolderWrite = harness.Core().Publish("TestMove", TopicValue::Double(1.0), "dashboard-b");
        EXPECT_FALSE(wrongHolderWrite.accepted);
        EXPECT_EQ(wrongHolderWrite.rejectionReason, WriteRejectReason::LeaseNotHolder);

        const WriteResult ownerWrite = harness.Core().Publish("TestMove", TopicValue::Double(4.0), "dashboard-a");
        EXPECT_TRUE(ownerWrite.accepted);

        TopicValue latestMove;
        ASSERT_TRUE(harness.Core().TryGetLatestValue("TestMove", latestMove));
        EXPECT_DOUBLE_EQ(latestMove.doubleValue, 4.0);
    }

    TEST(NativeLinkCoreTests, CommandTopicsProduceAckAndDoNotReplayOnReconnect)
    {
        NativeLinkCore core;
        ASSERT_TRUE(core.RegisterTopic(MakeCommandTopic("Robot/Reset")).ok);

        core.ConnectClient("dashboard-a");
        ASSERT_TRUE(core.AcquireLease("Robot/Reset", "dashboard-a"));

        const WriteResult writeResult = core.Publish("Robot/Reset", TopicValue::Bool(true), "dashboard-a");
        ASSERT_TRUE(writeResult.accepted);

        const std::vector<UpdateEnvelope> events = core.DrainClientEvents("dashboard-a");
        const UpdateEnvelope* liveCommandEvent = FindEventByTopic(events, "Robot/Reset");
        ASSERT_NE(liveCommandEvent, nullptr);
        EXPECT_EQ(liveCommandEvent->deliveryKind, DeliveryKind::LiveCommand);

        bool sawAck = false;
        for (const UpdateEnvelope& event : events)
        {
            if (event.topicPath == "Robot/Reset" && event.deliveryKind == DeliveryKind::LiveCommandAck)
            {
                sawAck = true;
                EXPECT_EQ(event.rejectionReason, WriteRejectReason::None);
            }
        }
        EXPECT_TRUE(sawAck);

        const std::vector<SnapshotEvent> reconnectSnapshot = core.ConnectClient("dashboard-b").snapshotEvents;
        for (const SnapshotEvent& event : reconnectSnapshot)
        {
            ASSERT_FALSE(event.kind == SnapshotEventKind::Update && event.hasUpdate && event.update.topicPath == "Robot/Reset");
        }
    }

    TEST(NativeLinkCoreTests, CommandRejectionIsReportedToWritingClient)
    {
        NativeLinkCore core;
        ASSERT_TRUE(core.RegisterTopic(MakeCommandTopic("Robot/Reset")).ok);

        core.ConnectClient("dashboard-a");
        core.ConnectClient("dashboard-b");
        ASSERT_TRUE(core.AcquireLease("Robot/Reset", "dashboard-a"));

        const WriteResult rejected = core.Publish("Robot/Reset", TopicValue::Bool(true), "dashboard-b");
        EXPECT_FALSE(rejected.accepted);
        EXPECT_EQ(rejected.rejectionReason, WriteRejectReason::LeaseNotHolder);

        const std::vector<UpdateEnvelope> dashboardBEvents = core.DrainClientEvents("dashboard-b");
        bool sawReject = false;
        for (const UpdateEnvelope& event : dashboardBEvents)
        {
            if (event.topicPath == "Robot/Reset" && event.deliveryKind == DeliveryKind::LiveCommandReject)
            {
                sawReject = true;
                EXPECT_EQ(event.rejectionReason, WriteRejectReason::LeaseNotHolder);
            }
        }
        EXPECT_TRUE(sawReject);
    }
}
