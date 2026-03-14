#pragma once

#include <QWidget>
#include <QString>

#include <cstdint>
#include <vector>

namespace sd::widgets
{
    enum class TimelineMarkerKind
    {
        Connect,
        Disconnect,
        Stale,
        Anomaly,
        Generic
    };

    struct TimelineMarker
    {
        std::int64_t timestampUs = 0;
        TimelineMarkerKind kind = TimelineMarkerKind::Generic;
        QString label;
    };

    // Shared replay timeline control used by all widgets.
    // Teaching note: this exposes a single cursor/window model so playback state
    // stays transport-agnostic and synchronized across the dashboard.
    class PlaybackTimelineWidget final : public QWidget
    {
        Q_OBJECT

    public:
        explicit PlaybackTimelineWidget(QWidget* parent = nullptr);

        void SetDurationUs(std::int64_t durationUs);
        void SetCursorUs(std::int64_t cursorUs);
        void SetWindowUs(std::int64_t windowStartUs, std::int64_t windowEndUs);
        void SetMarkers(const std::vector<TimelineMarker>& markers);

        std::int64_t GetDurationUs() const;
        std::int64_t GetCursorUs() const;
        std::int64_t GetWindowStartUs() const;
        std::int64_t GetWindowEndUs() const;

#if defined(SMARTDASHBOARD_TESTS)
        std::int64_t DebugComputeTickStepUs(std::int64_t spanUs) const;
        QString DebugFormatTimeLabel(std::int64_t timeUs) const;
        QString DebugFormatSpanLabel(std::int64_t spanUs) const;
#endif

    signals:
        void CursorScrubbedUs(std::int64_t cursorUs);
        void WindowChangedUs(std::int64_t windowStartUs, std::int64_t windowEndUs);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;

    private:
        QRect GetOverviewRect() const;
        QRect GetTrackRect() const;

        // Coordinate conversion helpers between pixels and timeline microseconds.
        std::int64_t PositionToTimeUs(int x) const;
        int TimeUsToPosition(std::int64_t timeUs) const;
        std::int64_t ComputeTickStepUs(std::int64_t spanUs) const;
        QString FormatTimeLabel(std::int64_t timeUs) const;
        QString FormatSpanLabel(std::int64_t spanUs) const;

        // Clamp the visible window into [0, duration] while preserving span.
        void ClampWindowToDuration();
        void EmitWindowChanged();

        std::int64_t m_durationUs = 0;
        std::int64_t m_cursorUs = 0;
        std::int64_t m_windowStartUs = 0;
        std::int64_t m_windowEndUs = 0;
        bool m_panning = false;
        int m_lastPanX = 0;
        std::vector<TimelineMarker> m_markers;
    };
}
