#include "widgets/playback_timeline_widget.h"

#include <QMouseEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace sd::widgets
{
    namespace
    {
        constexpr int kOuterPaddingPx = 8;
        constexpr int kOverviewHeightPx = 6;
        constexpr int kGapAfterOverviewPx = 4;
        constexpr int kTrackHeightPx = 8;
        constexpr int kTickLengthPx = 5;
        constexpr int kMinTickSpacingPx = 72;

        QColor MarkerColor(TimelineMarkerKind kind)
        {
            switch (kind)
            {
                case TimelineMarkerKind::Connect:
                    return QColor(74, 201, 122);
                case TimelineMarkerKind::Disconnect:
                    return QColor(235, 94, 94);
                case TimelineMarkerKind::Stale:
                    return QColor(242, 201, 76);
                case TimelineMarkerKind::Anomaly:
                    return QColor(255, 140, 80);
                case TimelineMarkerKind::Generic:
                default:
                    return QColor(155, 155, 155);
            }
        }
    }

    // Keep all timeline math in microseconds so replay seek and widget state
    // reconstruction stay deterministic across play/pause/seek operations.
    PlaybackTimelineWidget::PlaybackTimelineWidget(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumHeight(72);
        setMouseTracking(true);
    }

    void PlaybackTimelineWidget::SetDurationUs(std::int64_t durationUs)
    {
        m_durationUs = std::max<std::int64_t>(0, durationUs);
        if (m_windowEndUs <= m_windowStartUs)
        {
            m_windowStartUs = 0;
            m_windowEndUs = m_durationUs;
        }
        ClampWindowToDuration();
        m_cursorUs = std::clamp(m_cursorUs, std::int64_t(0), m_durationUs);
        update();
    }

    void PlaybackTimelineWidget::SetCursorUs(std::int64_t cursorUs)
    {
        m_cursorUs = std::clamp(cursorUs, std::int64_t(0), m_durationUs);
        update();
    }

    void PlaybackTimelineWidget::SetWindowUs(std::int64_t windowStartUs, std::int64_t windowEndUs)
    {
        m_windowStartUs = windowStartUs;
        m_windowEndUs = windowEndUs;
        ClampWindowToDuration();
        update();
    }

    void PlaybackTimelineWidget::SetMarkers(const std::vector<TimelineMarker>& markers)
    {
        m_markers = markers;
        update();
    }

    std::int64_t PlaybackTimelineWidget::GetDurationUs() const
    {
        return m_durationUs;
    }

    std::int64_t PlaybackTimelineWidget::GetCursorUs() const
    {
        return m_cursorUs;
    }

    std::int64_t PlaybackTimelineWidget::GetWindowStartUs() const
    {
        return m_windowStartUs;
    }

    std::int64_t PlaybackTimelineWidget::GetWindowEndUs() const
    {
        return m_windowEndUs;
    }

#if defined(SMARTDASHBOARD_TESTS)
    std::int64_t PlaybackTimelineWidget::DebugComputeTickStepUs(std::int64_t spanUs) const
    {
        return ComputeTickStepUs(spanUs);
    }

    QString PlaybackTimelineWidget::DebugFormatTimeLabel(std::int64_t timeUs) const
    {
        return FormatTimeLabel(timeUs);
    }

    QString PlaybackTimelineWidget::DebugFormatSpanLabel(std::int64_t spanUs) const
    {
        return FormatSpanLabel(spanUs);
    }
#endif

    void PlaybackTimelineWidget::paintEvent(QPaintEvent* event)
    {
        static_cast<void>(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(25, 25, 25));

        const QRect overviewRect = GetOverviewRect();
        const QRect trackRect = GetTrackRect();

        painter.fillRect(overviewRect, QColor(45, 45, 45));

        if (m_durationUs > 0)
        {
            const int overviewStartX = std::clamp(TimeUsToPosition(m_windowStartUs), overviewRect.left(), overviewRect.right());
            const int overviewEndX = std::clamp(TimeUsToPosition(m_windowEndUs), overviewRect.left(), overviewRect.right());
            const int widthPx = std::max(1, overviewEndX - overviewStartX);
            painter.fillRect(QRect(overviewStartX, overviewRect.top(), widthPx, overviewRect.height()), QColor(95, 140, 200, 130));
            painter.setPen(QPen(QColor(120, 170, 235), 1));
            painter.drawRect(overviewRect.adjusted(0, 0, -1, -1));

            for (const TimelineMarker& marker : m_markers)
            {
                if (marker.timestampUs < 0 || marker.timestampUs > m_durationUs)
                {
                    continue;
                }

                const double markerRatio = static_cast<double>(marker.timestampUs) / static_cast<double>(std::max<std::int64_t>(1, m_durationUs));
                const int markerX = overviewRect.left() + static_cast<int>(markerRatio * static_cast<double>(overviewRect.width()));
                painter.fillRect(QRect(markerX - 1, overviewRect.top(), 3, overviewRect.height()), MarkerColor(marker.kind));
            }
        }

        painter.fillRect(trackRect, QColor(55, 55, 55));
        QFontMetrics metrics = painter.fontMetrics();
        const int readoutBaselineY = trackRect.bottom() + kTickLengthPx + metrics.ascent() + 2;

        if (m_durationUs <= 0)
        {
            return;
        }

        const std::int64_t windowSpanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        const std::int64_t tickStepUs = ComputeTickStepUs(windowSpanUs);

        const std::int64_t tickBaseUs = (m_windowStartUs / tickStepUs) * tickStepUs;
        int lastLabelRight = std::numeric_limits<int>::min();

        painter.setPen(QColor(105, 105, 105));
        for (std::int64_t tickUs = tickBaseUs; tickUs <= m_windowEndUs + tickStepUs; tickUs += tickStepUs)
        {
            if (tickUs < m_windowStartUs)
            {
                continue;
            }
            if (tickUs > m_windowEndUs)
            {
                break;
            }

            const int x = TimeUsToPosition(tickUs);
            painter.drawLine(x, trackRect.bottom() + 1, x, trackRect.bottom() + kTickLengthPx);

            const QString label = FormatTimeLabel(tickUs);
            const int textWidth = metrics.horizontalAdvance(label);
            const int textX = x - (textWidth / 2);
            const int textRight = textX + textWidth;
            if (textX > lastLabelRight + 6)
            {
                painter.setPen(QColor(165, 165, 165));
                painter.drawText(textX, trackRect.bottom() + kTickLengthPx + metrics.ascent() + 2, label);
                painter.setPen(QColor(105, 105, 105));
                lastLabelRight = textRight;
            }
        }

        const double ratio = static_cast<double>(m_cursorUs - m_windowStartUs) / static_cast<double>(windowSpanUs);
        const int cursorX = trackRect.left() + static_cast<int>(ratio * static_cast<double>(trackRect.width()));

        for (const TimelineMarker& marker : m_markers)
        {
            if (marker.timestampUs < m_windowStartUs || marker.timestampUs > m_windowEndUs)
            {
                continue;
            }

            const int markerX = TimeUsToPosition(marker.timestampUs);
            painter.setPen(QPen(MarkerColor(marker.kind), 1));
            painter.drawLine(markerX, trackRect.top(), markerX, trackRect.bottom());
        }

        painter.setPen(QPen(QColor(255, 190, 90), 2));
        painter.drawLine(cursorX, trackRect.top(), cursorX, trackRect.bottom());

    }

    void PlaybackTimelineWidget::mousePressEvent(QMouseEvent* event)
    {
        if (event == nullptr)
        {
            return;
        }

        if (event->button() == Qt::LeftButton)
        {
            const std::int64_t cursor = PositionToTimeUs(event->position().x());
            m_cursorUs = cursor;
            emit CursorScrubbedUs(cursor);
            update();
            return;
        }

        if (event->button() == Qt::RightButton)
        {
            m_panning = true;
            m_lastPanX = event->position().x();
        }
    }

    void PlaybackTimelineWidget::mouseMoveEvent(QMouseEvent* event)
    {
        if (event == nullptr)
        {
            return;
        }

        if (event->buttons().testFlag(Qt::LeftButton))
        {
            const std::int64_t cursor = PositionToTimeUs(event->position().x());
            m_cursorUs = cursor;
            emit CursorScrubbedUs(cursor);
            update();
            return;
        }

        if (m_panning)
        {
            // Pan translates visible time window in fixed units-per-pixel space.
            const int dx = event->position().x() - m_lastPanX;
            m_lastPanX = event->position().x();

            const std::int64_t spanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
            const double usPerPixel = static_cast<double>(spanUs) / static_cast<double>(std::max(1, width()));
            const std::int64_t deltaUs = static_cast<std::int64_t>(-dx * usPerPixel);

            m_windowStartUs += deltaUs;
            m_windowEndUs += deltaUs;
            ClampWindowToDuration();
            EmitWindowChanged();
            update();
        }
    }

    void PlaybackTimelineWidget::mouseReleaseEvent(QMouseEvent* event)
    {
        if (event != nullptr && event->button() == Qt::RightButton)
        {
            m_panning = false;
        }
    }

    void PlaybackTimelineWidget::wheelEvent(QWheelEvent* event)
    {
        if (event == nullptr)
        {
            return;
        }

        if (m_durationUs <= 0)
        {
            return;
        }

        // Zoom anchors around cursor position to preserve operator context while inspecting events.
        const std::int64_t anchorUs = PositionToTimeUs(event->position().x());
        const std::int64_t oldSpanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        const double zoomFactor = event->angleDelta().y() > 0 ? 0.8 : 1.25;

        std::int64_t newSpanUs = static_cast<std::int64_t>(static_cast<double>(oldSpanUs) * zoomFactor);
        newSpanUs = std::clamp<std::int64_t>(newSpanUs, 200000, std::max<std::int64_t>(200000, m_durationUs));

        m_windowStartUs = anchorUs - (newSpanUs / 2);
        m_windowEndUs = m_windowStartUs + newSpanUs;
        ClampWindowToDuration();
        EmitWindowChanged();
        update();
    }

    std::int64_t PlaybackTimelineWidget::PositionToTimeUs(int x) const
    {
        const QRect trackRect = GetTrackRect();
        const int clampedX = std::clamp(x, trackRect.left(), trackRect.right());
        const double widthPixels = static_cast<double>(std::max(1, trackRect.width()));
        const double t = static_cast<double>(clampedX - trackRect.left()) / widthPixels;

        const std::int64_t spanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        return m_windowStartUs + static_cast<std::int64_t>(t * static_cast<double>(spanUs));
    }

    int PlaybackTimelineWidget::TimeUsToPosition(std::int64_t timeUs) const
    {
        const QRect trackRect = GetTrackRect();
        const std::int64_t spanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        const double t = static_cast<double>(timeUs - m_windowStartUs) / static_cast<double>(spanUs);
        return trackRect.left() + static_cast<int>(t * static_cast<double>(trackRect.width()));
    }

    QRect PlaybackTimelineWidget::GetOverviewRect() const
    {
        const int left = kOuterPaddingPx;
        const int top = kOuterPaddingPx;
        const int widthPx = std::max(1, width() - (2 * kOuterPaddingPx));
        return QRect(left, top, widthPx, kOverviewHeightPx);
    }

    QRect PlaybackTimelineWidget::GetTrackRect() const
    {
        const int left = kOuterPaddingPx;
        const int top = kOuterPaddingPx + kOverviewHeightPx + kGapAfterOverviewPx;
        const int widthPx = std::max(1, width() - (2 * kOuterPaddingPx));
        return QRect(left, top, widthPx, kTrackHeightPx);
    }

    std::int64_t PlaybackTimelineWidget::ComputeTickStepUs(std::int64_t spanUs) const
    {
        if (spanUs <= 0)
        {
            return 1000000;
        }

        const int widthPx = std::max(1, GetTrackRect().width());
        const int targetTickCount = std::max(2, widthPx / kMinTickSpacingPx);
        const double targetStepUs = static_cast<double>(spanUs) / static_cast<double>(targetTickCount);
        if (targetStepUs <= 0.0)
        {
            return 1000000;
        }

        const double magnitude = std::pow(10.0, std::floor(std::log10(targetStepUs)));
        const double normalized = targetStepUs / magnitude;

        double snapped = 1.0;
        if (normalized <= 1.0)
        {
            snapped = 1.0;
        }
        else if (normalized <= 2.0)
        {
            snapped = 2.0;
        }
        else if (normalized <= 5.0)
        {
            snapped = 5.0;
        }
        else
        {
            snapped = 10.0;
        }

        const std::int64_t stepUs = static_cast<std::int64_t>(snapped * magnitude);
        return std::max<std::int64_t>(1000, stepUs);
    }

    QString PlaybackTimelineWidget::FormatTimeLabel(std::int64_t timeUs) const
    {
        const bool negative = timeUs < 0;
        const std::int64_t absUs = std::llabs(timeUs);
        const std::int64_t totalMs = absUs / 1000;
        const std::int64_t totalSeconds = totalMs / 1000;
        const std::int64_t minutes = totalSeconds / 60;
        const std::int64_t seconds = totalSeconds % 60;
        const std::int64_t ms = totalMs % 1000;

        QString label;
        if (minutes > 0)
        {
            label = QString("%1:%2.%3").arg(minutes).arg(seconds, 2, 10, QChar('0')).arg(ms, 3, 10, QChar('0'));
        }
        else
        {
            label = QString("%1.%2s").arg(seconds).arg(ms, 3, 10, QChar('0'));
        }

        if (negative)
        {
            label.prepend('-');
        }

        return label;
    }

    QString PlaybackTimelineWidget::FormatSpanLabel(std::int64_t spanUs) const
    {
        return QString("%1s").arg(static_cast<double>(std::max<std::int64_t>(0, spanUs)) / 1000000.0, 0, 'f', 3);
    }

    void PlaybackTimelineWidget::ClampWindowToDuration()
    {
        if (m_durationUs <= 0)
        {
            m_windowStartUs = 0;
            m_windowEndUs = 0;
            return;
        }

        if (m_windowEndUs <= m_windowStartUs)
        {
            m_windowStartUs = 0;
            m_windowEndUs = m_durationUs;
        }

        const std::int64_t spanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        if (spanUs >= m_durationUs)
        {
            m_windowStartUs = 0;
            m_windowEndUs = m_durationUs;
            return;
        }

        // Preserve span while clamping so zoom level remains stable during pan/seek edges.
        if (m_windowStartUs < 0)
        {
            m_windowEndUs -= m_windowStartUs;
            m_windowStartUs = 0;
        }

        if (m_windowEndUs > m_durationUs)
        {
            const std::int64_t over = m_windowEndUs - m_durationUs;
            m_windowStartUs -= over;
            m_windowEndUs = m_durationUs;
        }

        m_windowStartUs = std::clamp(m_windowStartUs, std::int64_t(0), m_durationUs);
        m_windowEndUs = std::clamp(m_windowEndUs, std::int64_t(0), m_durationUs);
    }

    void PlaybackTimelineWidget::EmitWindowChanged()
    {
        emit WindowChangedUs(m_windowStartUs, m_windowEndUs);
    }
}
