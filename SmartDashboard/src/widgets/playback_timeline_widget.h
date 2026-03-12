#pragma once

#include <QWidget>

#include <cstdint>

namespace sd::widgets
{
    class PlaybackTimelineWidget final : public QWidget
    {
        Q_OBJECT

    public:
        explicit PlaybackTimelineWidget(QWidget* parent = nullptr);

        void SetDurationUs(std::int64_t durationUs);
        void SetCursorUs(std::int64_t cursorUs);
        void SetWindowUs(std::int64_t windowStartUs, std::int64_t windowEndUs);

        std::int64_t GetDurationUs() const;
        std::int64_t GetCursorUs() const;
        std::int64_t GetWindowStartUs() const;
        std::int64_t GetWindowEndUs() const;

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
        std::int64_t PositionToTimeUs(int x) const;
        int TimeUsToPosition(std::int64_t timeUs) const;
        void ClampWindowToDuration();
        void EmitWindowChanged();

        std::int64_t m_durationUs = 0;
        std::int64_t m_cursorUs = 0;
        std::int64_t m_windowStartUs = 0;
        std::int64_t m_windowEndUs = 0;
        bool m_panning = false;
        int m_lastPanX = 0;
    };
}
