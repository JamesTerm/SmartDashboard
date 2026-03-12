#include "widgets/playback_timeline_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>

namespace sd::widgets
{
    PlaybackTimelineWidget::PlaybackTimelineWidget(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumHeight(36);
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

    void PlaybackTimelineWidget::paintEvent(QPaintEvent* event)
    {
        static_cast<void>(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(25, 25, 25));

        const QRect trackRect = rect().adjusted(8, 8, -8, -8);
        painter.fillRect(trackRect, QColor(55, 55, 55));

        if (m_durationUs <= 0)
        {
            return;
        }

        const std::int64_t windowSpanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        const double ratio = static_cast<double>(m_cursorUs - m_windowStartUs) / static_cast<double>(windowSpanUs);
        const int cursorX = trackRect.left() + static_cast<int>(ratio * static_cast<double>(trackRect.width()));

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
        const QRect trackRect = rect().adjusted(8, 8, -8, -8);
        const int clampedX = std::clamp(x, trackRect.left(), trackRect.right());
        const double widthPixels = static_cast<double>(std::max(1, trackRect.width()));
        const double t = static_cast<double>(clampedX - trackRect.left()) / widthPixels;

        const std::int64_t spanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        return m_windowStartUs + static_cast<std::int64_t>(t * static_cast<double>(spanUs));
    }

    int PlaybackTimelineWidget::TimeUsToPosition(std::int64_t timeUs) const
    {
        const QRect trackRect = rect().adjusted(8, 8, -8, -8);
        const std::int64_t spanUs = std::max<std::int64_t>(1, m_windowEndUs - m_windowStartUs);
        const double t = static_cast<double>(timeUs - m_windowStartUs) / static_cast<double>(spanUs);
        return trackRect.left() + static_cast<int>(t * static_cast<double>(trackRect.width()));
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
