#pragma once

#include <QString>
#include <QVariant>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace sd::transport
{
    enum class TransportKind
    {
        Direct,
        NetworkTables,
        Replay
    };

    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        Stale
    };

    struct ConnectionConfig
    {
        TransportKind kind = TransportKind::Direct;
        QString ntHost = "127.0.0.1";
        int ntTeam = 0;
        bool ntUseTeam = true;
        QString ntClientName = "SmartDashboardApp";
        QString replayFilePath;
    };

    struct VariableUpdate
    {
        QString key;
        int valueType = 0;
        QVariant value;
        std::uint64_t seq = 0;
    };

    enum class PlaybackMarkerKind
    {
        Connect,
        Disconnect,
        Stale,
        Anomaly,
        Generic
    };

    struct PlaybackMarker
    {
        std::int64_t timestampUs = 0;
        PlaybackMarkerKind kind = PlaybackMarkerKind::Generic;
        QString label;
    };

    using VariableUpdateCallback = std::function<void(const VariableUpdate&)>;
    using ConnectionStateCallback = std::function<void(ConnectionState)>;

    class IDashboardTransport
    {
    public:
        virtual ~IDashboardTransport() = default;

        virtual bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) = 0;
        virtual void Stop() = 0;

        virtual bool PublishBool(const QString& key, bool value) = 0;
        virtual bool PublishDouble(const QString& key, double value) = 0;
        virtual bool PublishString(const QString& key, const QString& value) = 0;

        virtual void ReplayRetainedControls(const std::function<void(const QString& key, int valueType, const QVariant& value)>& replayFn)
        {
            static_cast<void>(replayFn);
        }

        virtual bool SupportsPlayback() const
        {
            return false;
        }

        virtual bool SetPlaybackPlaying(bool isPlaying)
        {
            static_cast<void>(isPlaying);
            return false;
        }

        virtual bool SeekPlaybackUs(std::int64_t cursorUs)
        {
            static_cast<void>(cursorUs);
            return false;
        }

        virtual bool SetPlaybackRate(double rate)
        {
            static_cast<void>(rate);
            return false;
        }

        virtual std::int64_t GetPlaybackDurationUs() const
        {
            return 0;
        }

        virtual std::int64_t GetPlaybackCursorUs() const
        {
            return 0;
        }

        virtual bool IsPlaybackPlaying() const
        {
            return false;
        }

        virtual std::vector<PlaybackMarker> GetPlaybackMarkers() const
        {
            return {};
        }
    };

    QString ToDisplayString(TransportKind kind);

    std::unique_ptr<IDashboardTransport> CreateDashboardTransport(const ConnectionConfig& config);
}
