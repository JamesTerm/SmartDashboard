#include "transport/dashboard_transport.h"
#include "transport/dashboard_transport_plugin_api.h"

#include "sd_direct_publisher.h"
#include "sd_direct_subscriber.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QLibrary>
#include <QVariant>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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
                pubConfig.autoFlushThread = true;
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
                replayNumeric(QStringLiteral("TestMove"));
                replayNumeric(QStringLiteral("Test/TestMove"));
            }

        private:
            std::unique_ptr<sd::direct::IDirectSubscriber> m_subscriber;
            std::unique_ptr<sd::direct::IDirectPublisher> m_commandPublisher;
            VariableUpdateCallback m_onVariableUpdate;
            ConnectionStateCallback m_onConnectionState;
            std::unordered_map<std::string, sd::direct::VariableUpdate> m_latestByKey;
            std::atomic<bool> m_connectedSeen {false};
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

    namespace
    {
        ConnectionState ToConnectionState(int state)
        {
            switch (state)
            {
                case SD_TRANSPORT_CONNECTION_STATE_CONNECTING:
                    return ConnectionState::Connecting;
                case SD_TRANSPORT_CONNECTION_STATE_CONNECTED:
                    return ConnectionState::Connected;
                case SD_TRANSPORT_CONNECTION_STATE_STALE:
                    return ConnectionState::Stale;
                case SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED:
                default:
                    return ConnectionState::Disconnected;
            }
        }

        TransportDescriptor MakeDirectDescriptor()
        {
            TransportDescriptor descriptor;
            descriptor.id = "direct";
            descriptor.displayName = "Direct";
            descriptor.kind = TransportKind::Direct;
            descriptor.useShortDisplayKeys = true;
            descriptor.supportsRecording = true;
            descriptor.settingsProfileId = "direct";
            descriptor.boolProperties.insert_or_assign(QString::fromUtf8(kTransportPropertySupportsChooser), true);
            return descriptor;
        }

        TransportDescriptor MakeReplayDescriptor()
        {
            TransportDescriptor descriptor;
            descriptor.id = "replay";
            descriptor.displayName = "Replay";
            descriptor.kind = TransportKind::Replay;
            descriptor.useShortDisplayKeys = true;
            descriptor.supportsPlayback = true;
            descriptor.settingsProfileId = "replay";
            return descriptor;
        }

        std::vector<ConnectionFieldDescriptor> MakeLegacyNtConnectionFields()
        {
            std::vector<ConnectionFieldDescriptor> fields;

            ConnectionFieldDescriptor useTeamField;
            useTeamField.id = QString::fromUtf8(kTransportFieldUseTeamNumber);
            useTeamField.label = "Use team number";
            useTeamField.type = ConnectionFieldType::Bool;
            useTeamField.helpText = "Use FRC team-number resolution instead of a direct host/IP.";
            useTeamField.defaultValue = true;
            fields.push_back(useTeamField);

            ConnectionFieldDescriptor teamField;
            teamField.id = QString::fromUtf8(kTransportFieldTeamNumber);
            teamField.label = "Team number";
            teamField.type = ConnectionFieldType::Int;
            teamField.helpText = "Used when team-number resolution is enabled.";
            teamField.defaultValue = 0;
            teamField.intMinimum = 0;
            teamField.intMaximum = 99999;
            fields.push_back(teamField);

            ConnectionFieldDescriptor hostField;
            hostField.id = QString::fromUtf8(kTransportFieldHost);
            hostField.label = "Host / IP";
            hostField.type = ConnectionFieldType::String;
            hostField.helpText = "Used when connecting directly by host name or IP address.";
            hostField.defaultValue = "127.0.0.1";
            fields.push_back(hostField);

            ConnectionFieldDescriptor clientNameField;
            clientNameField.id = QString::fromUtf8(kTransportFieldClientName);
            clientNameField.label = "Client name";
            clientNameField.type = ConnectionFieldType::String;
            clientNameField.helpText = "Name reported by the dashboard client to the transport ecosystem.";
            clientNameField.defaultValue = "SmartDashboardApp";
            fields.push_back(clientNameField);

            return fields;
        }

        std::vector<ConnectionFieldDescriptor> ConvertPluginConnectionFields(
            const sd_transport_plugin_descriptor_v1* pluginDescriptor,
            const QString& settingsProfileId
        )
        {
            if (pluginDescriptor == nullptr || pluginDescriptor->get_connection_fields == nullptr)
            {
                if (settingsProfileId == "legacy-nt")
                {
                    return MakeLegacyNtConnectionFields();
                }

                return {};
            }

            size_t count = 0;
            const sd_transport_connection_field_descriptor_v1* fields = pluginDescriptor->get_connection_fields(&count);
            std::vector<ConnectionFieldDescriptor> converted;
            if (fields == nullptr || count == 0)
            {
                return converted;
            }

            converted.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                const sd_transport_connection_field_descriptor_v1& source = fields[i];
                if (source.field_id == nullptr || source.label == nullptr)
                {
                    continue;
                }

                ConnectionFieldDescriptor field;
                field.id = QString::fromUtf8(source.field_id);
                field.label = QString::fromUtf8(source.label);
                field.helpText = QString::fromUtf8(source.help_text != nullptr ? source.help_text : "");
                switch (source.field_type)
                {
                    case SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL:
                        field.type = ConnectionFieldType::Bool;
                        field.defaultValue = (source.default_bool_value != 0);
                        break;
                    case SD_TRANSPORT_CONNECTION_FIELD_TYPE_INT:
                        field.type = ConnectionFieldType::Int;
                        field.defaultValue = source.default_int_value;
                        field.intMinimum = source.int_minimum;
                        field.intMaximum = source.int_maximum;
                        break;
                    case SD_TRANSPORT_CONNECTION_FIELD_TYPE_STRING:
                    default:
                        field.type = ConnectionFieldType::String;
                        field.defaultValue = QString::fromUtf8(source.default_string_value != nullptr ? source.default_string_value : "");
                        break;
                }
                converted.push_back(field);
            }

            return converted;
        }

        bool QueryPluginBoolProperty(
            const sd_transport_plugin_descriptor_v1* pluginDescriptor,
            const char* propertyName,
            bool defaultValue
        )
        {
            if (pluginDescriptor == nullptr || pluginDescriptor->get_bool_property == nullptr || propertyName == nullptr)
            {
                return defaultValue;
            }

            return pluginDescriptor->get_bool_property(propertyName, defaultValue ? 1 : 0) != 0;
        }

        class PluginDashboardTransport final : public IDashboardTransport
        {
        public:
            PluginDashboardTransport(const sd_transport_plugin_descriptor_v1* descriptor, ConnectionConfig config)
                : m_descriptor(descriptor)
                , m_config(std::move(config))
                , m_alive(std::make_shared<std::atomic<bool>>(true))
            {
            }

            ~PluginDashboardTransport() override
            {
                Stop();
                DestroyInstance();
            }

            bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) override
            {
                m_onVariableUpdate = std::move(onVariableUpdate);
                m_onConnectionState = std::move(onConnectionState);

                if (m_descriptor == nullptr || m_descriptor->transport_api == nullptr || m_descriptor->transport_api->start == nullptr)
                {
                    return false;
                }

                if (m_instance == nullptr)
                {
                    if (m_descriptor->transport_api->create == nullptr)
                    {
                        return false;
                    }
                    m_instance = m_descriptor->transport_api->create();
                }

                if (m_instance == nullptr)
                {
                    return false;
                }

                m_transportIdUtf8 = m_config.transportId.toUtf8();
                m_pluginSettingsJsonUtf8 = m_config.pluginSettingsJson.toUtf8();
                m_ntHostUtf8 = m_config.ntHost.toUtf8();
                m_ntClientNameUtf8 = m_config.ntClientName.toUtf8();
                m_replayFilePathUtf8 = m_config.replayFilePath.toUtf8();

                sd_transport_connection_config_v1 pluginConfig {};
                pluginConfig.transport_id = m_transportIdUtf8.constData();
                pluginConfig.plugin_settings_json = m_pluginSettingsJsonUtf8.constData();
                pluginConfig.nt_host = m_ntHostUtf8.constData();
                pluginConfig.nt_team = m_config.ntTeam;
                pluginConfig.nt_use_team = m_config.ntUseTeam ? 1 : 0;
                pluginConfig.nt_client_name = m_ntClientNameUtf8.constData();
                pluginConfig.replay_file_path = m_replayFilePathUtf8.constData();

                sd_transport_callbacks_v1 callbacks {};
                callbacks.user_data = this;
                callbacks.on_variable_update = &PluginDashboardTransport::OnPluginVariableUpdate;
                callbacks.on_connection_state = &PluginDashboardTransport::OnPluginConnectionState;

                return m_descriptor->transport_api->start(m_instance, &pluginConfig, &callbacks) != 0;
            }

            void Stop() override
            {
                // Signal all pending queued callbacks that this object is going away
                // before we call into the plugin's stop(). The plugin's stop() joins
                // the worker thread and may fire onConnectionState synchronously on
                // that thread; if it posts a QueuedConnection lambda, the lambda
                // captures m_alive by value and will bail out safely instead of
                // dereferencing the already-destroyed PluginDashboardTransport.
                m_alive->store(false, std::memory_order_release);

                if (m_instance != nullptr && m_descriptor != nullptr && m_descriptor->transport_api != nullptr && m_descriptor->transport_api->stop != nullptr)
                {
                    m_descriptor->transport_api->stop(m_instance);
                }
            }

            bool PublishBool(const QString& key, bool value) override
            {
                if (m_instance == nullptr || m_descriptor == nullptr || m_descriptor->transport_api == nullptr || m_descriptor->transport_api->publish_bool == nullptr)
                {
                    return false;
                }

                const QByteArray keyUtf8 = key.toUtf8();
                return m_descriptor->transport_api->publish_bool(m_instance, keyUtf8.constData(), value ? 1 : 0) != 0;
            }

            bool PublishDouble(const QString& key, double value) override
            {
                if (m_instance == nullptr || m_descriptor == nullptr || m_descriptor->transport_api == nullptr || m_descriptor->transport_api->publish_double == nullptr)
                {
                    return false;
                }

                const QByteArray keyUtf8 = key.toUtf8();
                return m_descriptor->transport_api->publish_double(m_instance, keyUtf8.constData(), value) != 0;
            }

            bool PublishString(const QString& key, const QString& value) override
            {
                if (m_instance == nullptr || m_descriptor == nullptr || m_descriptor->transport_api == nullptr || m_descriptor->transport_api->publish_string == nullptr)
                {
                    return false;
                }

                const QByteArray keyUtf8 = key.toUtf8();
                const QByteArray valueUtf8 = value.toUtf8();
                return m_descriptor->transport_api->publish_string(m_instance, keyUtf8.constData(), valueUtf8.constData()) != 0;
            }

        private:
            static void OnPluginVariableUpdate(
                void* userData,
                const char* key,
                const sd_transport_value_v1* value,
                uint64_t seq
            )
            {
                auto* self = static_cast<PluginDashboardTransport*>(userData);
                if (self == nullptr || self->m_onVariableUpdate == nullptr || key == nullptr || value == nullptr)
                {
                    return;
                }

                // Capture alive guard by value so the lambda is safe even if
                // this PluginDashboardTransport is destroyed before it executes.
                auto alive = self->m_alive;

                VariableUpdate update;
                update.key = QString::fromUtf8(key);
                update.seq = seq;

                switch (value->type)
                {
                    case SD_TRANSPORT_VALUE_TYPE_BOOL:
                        update.valueType = static_cast<int>(sd::direct::ValueType::Bool);
                        update.value = (value->bool_value != 0);
                        break;
                    case SD_TRANSPORT_VALUE_TYPE_DOUBLE:
                        update.valueType = static_cast<int>(sd::direct::ValueType::Double);
                        update.value = value->double_value;
                        break;
                    case SD_TRANSPORT_VALUE_TYPE_STRING_ARRAY:
                    {
                        update.valueType = static_cast<int>(sd::direct::ValueType::StringArray);
                        QStringList values;
                        for (size_t i = 0; i < value->string_array_count; ++i)
                        {
                            const char* item = value->string_array_items != nullptr ? value->string_array_items[i] : nullptr;
                            values.push_back(QString::fromUtf8(item != nullptr ? item : ""));
                        }
                        update.value = values;
                        break;
                    }
                    case SD_TRANSPORT_VALUE_TYPE_STRING:
                    default:
                        update.valueType = static_cast<int>(sd::direct::ValueType::String);
                        update.value = QString::fromUtf8(value->string_value != nullptr ? value->string_value : "");
                        break;
                }

	                // Ian: Plugin callbacks may arrive on a transport-owned worker
	                // thread. Queue them onto the main thread before touching the
	                // stored callback so both dashboard instances use the same UI
	                // delivery path even after merged app changes alter startup work.
	                // Capture alive by value so the lambda is a no-op if this
	                // PluginDashboardTransport has already been destroyed.
	                QMetaObject::invokeMethod(
	                    qApp,
	                    [self, alive, update]()
	                    {
	                        if (!alive->load(std::memory_order_acquire))
	                        {
	                            return;
	                        }
	                        if (self->m_onVariableUpdate != nullptr)
	                        {
	                            self->m_onVariableUpdate(update);
	                        }
	                    },
	                    Qt::QueuedConnection
	                );
            }

            static void OnPluginConnectionState(void* userData, int state)
            {
                auto* self = static_cast<PluginDashboardTransport*>(userData);
                if (self == nullptr || self->m_onConnectionState == nullptr)
                {
                    return;
                }

                // Capture alive guard so the queued lambda cannot use self
                // after the transport has been destroyed.
                auto alive = self->m_alive;
	                const ConnectionState connectionState = ToConnectionState(state);
	                QMetaObject::invokeMethod(
	                    qApp,
	                    [self, alive, connectionState]()
	                    {
	                        if (!alive->load(std::memory_order_acquire))
	                        {
	                            return;
	                        }
	                        if (self->m_onConnectionState != nullptr)
	                        {
	                            self->m_onConnectionState(connectionState);
	                        }
	                    },
	                    Qt::QueuedConnection
	                );
            }

            void DestroyInstance()
            {
                if (m_instance != nullptr && m_descriptor != nullptr && m_descriptor->transport_api != nullptr && m_descriptor->transport_api->destroy != nullptr)
                {
                    m_descriptor->transport_api->destroy(m_instance);
                    m_instance = nullptr;
                }
            }

            const sd_transport_plugin_descriptor_v1* m_descriptor = nullptr;
            ConnectionConfig m_config;
            sd_transport_instance_v1 m_instance = nullptr;
            VariableUpdateCallback m_onVariableUpdate;
            ConnectionStateCallback m_onConnectionState;
            QByteArray m_transportIdUtf8;
            QByteArray m_pluginSettingsJsonUtf8;
            QByteArray m_ntHostUtf8;
            QByteArray m_ntClientNameUtf8;
            QByteArray m_replayFilePathUtf8;
            // Shared alive flag: set to false in Stop() before calling the plugin's
            // stop(). Queued lambdas posted by worker-thread callbacks capture this
            // by value and bail out without touching `self` once it is false.
            std::shared_ptr<std::atomic<bool>> m_alive;
        };
    }

    struct DashboardTransportRegistry::Impl
    {
        struct PluginEntry
        {
            TransportDescriptor descriptor;
            std::unique_ptr<QLibrary> library;
            const sd_transport_plugin_descriptor_v1* pluginDescriptor = nullptr;
        };

        std::vector<TransportDescriptor> descriptors;
        std::vector<PluginEntry> plugins;

        Impl()
        {
            descriptors.push_back(MakeDirectDescriptor());
            descriptors.push_back(MakeReplayDescriptor());
            LoadPlugins();
        }

        void LoadPlugins()
        {
            const QString appDirPath = QCoreApplication::applicationDirPath();
            if (appDirPath.trimmed().isEmpty())
            {
                return;
            }

            QStringList entries;
            QDir pluginDir(appDirPath + "/plugins");
            if (pluginDir.exists())
            {
                entries.append(pluginDir.entryList(QDir::Files));
            }

            // Ian: Transport plugins are our own app-side DLLs, not Qt's
            // framework plugins. Keep searching the historical app-local path
            // first, but also allow DLLs placed directly beside the app so
            // SmartDashboard can coexist with Qt's own `plugins/` directory.
            QDir appDir(appDirPath);
            const QStringList appLocalEntries = appDir.entryList(QStringList() << "SmartDashboardTransport_*.dll", QDir::Files);
            for (const QString& entry : appLocalEntries)
            {
                if (!entries.contains(entry))
                {
                    entries.push_back(entry);
                }
            }

            if (entries.isEmpty())
            {
                return;
            }

            std::set<QString> seenIds;
            for (const QString& entry : entries)
            {
                const QString candidatePath = pluginDir.exists() && pluginDir.exists(entry)
                    ? pluginDir.absoluteFilePath(entry)
                    : appDir.absoluteFilePath(entry);

                auto library = std::make_unique<QLibrary>(candidatePath);
                if (!library->load())
                {
                    continue;
                }

                auto getPlugin = reinterpret_cast<sd_get_transport_plugin_v1_fn>(library->resolve("SdGetTransportPluginV1"));
                if (getPlugin == nullptr)
                {
                    continue;
                }

                const sd_transport_plugin_descriptor_v1* pluginDescriptor = getPlugin();
                if (pluginDescriptor == nullptr)
                {
                    continue;
                }
                if (pluginDescriptor->plugin_api_version != SD_TRANSPORT_PLUGIN_API_VERSION_1)
                {
                    continue;
                }
                if (pluginDescriptor->plugin_id == nullptr || pluginDescriptor->display_name == nullptr || pluginDescriptor->transport_api == nullptr)
                {
                    continue;
                }

                const QString pluginId = QString::fromUtf8(pluginDescriptor->plugin_id).trimmed();
                if (pluginId.isEmpty() || seenIds.contains(pluginId))
                {
                    continue;
                }

                seenIds.insert(pluginId);

                PluginEntry plugin;
                plugin.pluginDescriptor = pluginDescriptor;
                plugin.library = std::move(library);
                plugin.descriptor.id = pluginId;
                plugin.descriptor.displayName = QString::fromUtf8(pluginDescriptor->display_name);
                plugin.descriptor.kind = TransportKind::Plugin;
                plugin.descriptor.useShortDisplayKeys = (pluginDescriptor->flags & SD_TRANSPORT_PLUGIN_FLAG_USE_SHORT_DISPLAY_KEYS) != 0u;
                plugin.descriptor.supportsRecording = (pluginDescriptor->flags & SD_TRANSPORT_PLUGIN_FLAG_SUPPORTS_RECORDING) != 0u;
                plugin.descriptor.supportsPlayback = false;
                plugin.descriptor.settingsProfileId = QString::fromUtf8(
                    pluginDescriptor->settings_profile_id != nullptr ? pluginDescriptor->settings_profile_id : pluginDescriptor->plugin_id
                );
                plugin.descriptor.connectionFields = ConvertPluginConnectionFields(pluginDescriptor, plugin.descriptor.settingsProfileId);
                plugin.descriptor.boolProperties.insert_or_assign(
                    QString::fromUtf8(kTransportPropertySupportsChooser),
                    QueryPluginBoolProperty(pluginDescriptor, SD_TRANSPORT_PROPERTY_SUPPORTS_CHOOSER, false)
                );
                plugin.descriptor.boolProperties.insert_or_assign(
                    QString::fromUtf8(kTransportPropertySupportsMultiClient),
                    QueryPluginBoolProperty(pluginDescriptor, SD_TRANSPORT_PROPERTY_SUPPORTS_MULTI_CLIENT, false)
                );
                descriptors.push_back(plugin.descriptor);
                plugins.push_back(std::move(plugin));
            }
        }

        const TransportDescriptor* FindDescriptor(const QString& transportId) const
        {
            for (const TransportDescriptor& descriptor : descriptors)
            {
                if (descriptor.id == transportId)
                {
                    return &descriptor;
                }
            }

            return nullptr;
        }

        const PluginEntry* FindPlugin(const QString& transportId) const
        {
            for (const PluginEntry& plugin : plugins)
            {
                if (plugin.descriptor.id == transportId)
                {
                    return &plugin;
                }
            }

            return nullptr;
        }
    };

    DashboardTransportRegistry::DashboardTransportRegistry()
        : m_impl(std::make_unique<Impl>())
    {
    }

    DashboardTransportRegistry::~DashboardTransportRegistry() = default;

    const std::vector<TransportDescriptor>& DashboardTransportRegistry::GetAvailableTransports() const
    {
        return m_impl->descriptors;
    }

    const TransportDescriptor* DashboardTransportRegistry::FindTransport(const QString& transportId) const
    {
        return m_impl->FindDescriptor(transportId);
    }

    std::unique_ptr<IDashboardTransport> DashboardTransportRegistry::CreateTransport(const ConnectionConfig& config) const
    {
        const QString transportId = config.transportId.trimmed().isEmpty() ? QStringLiteral("direct") : config.transportId.trimmed();
        const TransportDescriptor* descriptor = FindTransport(transportId);
        if (descriptor == nullptr)
        {
            return nullptr;
        }

        if (descriptor->kind == TransportKind::Replay)
        {
            return std::make_unique<ReplayDashboardTransport>(config);
        }

        if (descriptor->kind == TransportKind::Direct)
        {
            return std::make_unique<DirectDashboardTransport>();
        }

        const Impl::PluginEntry* plugin = m_impl->FindPlugin(transportId);
        if (plugin == nullptr)
        {
            return nullptr;
        }

        return std::make_unique<PluginDashboardTransport>(plugin->pluginDescriptor, config);
    }

    QString ToDisplayString(TransportKind kind)
    {
        switch (kind)
        {
            case TransportKind::Replay:
                return "Replay";
            case TransportKind::Plugin:
                return "Plugin";
            case TransportKind::Direct:
            default:
                return "Direct";
        }
    }
}
