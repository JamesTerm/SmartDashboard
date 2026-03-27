#pragma once

/// @file camera_display_widget.h
/// @brief Custom QWidget that displays camera frames with aspect-ratio scaling.
///
/// Accepts QImage frames (from any CameraStreamSource) and paints them
/// scaled to fit the widget while preserving aspect ratio.  Optionally
/// draws a targeting reticle overlay on top.
///
/// Ian: The targeting reticle is a dashboard-side UI feature drawn via
/// QPainter over whatever video the widget displays.  It is completely
/// separate from the Honda backup camera-style guide lines, which are
/// a simulator-side feature drawn in OSG and baked into MJPEG frames.

#include <QImage>
#include <QPointF>
#include <QWidget>

namespace sd::widgets
{
    /// @brief Widget that displays camera frames with optional reticle overlay.
    class CameraDisplayWidget final : public QWidget
    {
        Q_OBJECT

    public:
        explicit CameraDisplayWidget(QWidget* parent = nullptr);

        /// @brief Set the reticle overlay visibility.
        void SetReticleVisible(bool visible);

        /// @brief Return true if the reticle overlay is visible.
        bool IsReticleVisible() const;

        /// @brief Set the reticle position in normalized coordinates (0..1, 0..1).
        void SetReticlePosition(const QPointF& normalizedPos);

        /// @brief Return the reticle position in normalized coordinates.
        QPointF GetReticlePosition() const;

        /// @brief Set the FPS value to display (0 to hide).
        void SetFpsDisplay(double fps);

        /// @brief Set a status message to display when no frame is available.
        void SetStatusMessage(const QString& message);

    public slots:
        /// @brief Receive a new frame from the stream source.
        void OnFrameReady(const QImage& frame);

        /// @brief Clear the current frame (e.g. on disconnect).
        void ClearFrame();

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

    private:
        /// @brief Compute the destination rectangle for the frame, preserving aspect ratio.
        QRect ComputeFrameRect() const;

        /// @brief Convert a widget pixel position to normalized frame coordinates.
        QPointF WidgetPosToNormalized(const QPoint& widgetPos) const;

        void PaintFrame(QPainter& painter);
        void PaintReticle(QPainter& painter);
        void PaintStatusMessage(QPainter& painter);
        void PaintFpsCounter(QPainter& painter);

        QImage m_currentFrame;
        QString m_statusMessage;
        double m_fps = 0.0;

        // Ian: Reticle state.  Position is in normalized (0..1, 0..1)
        // coordinates relative to the frame, so it's resolution-independent
        // and survives widget resizing.
        bool m_reticleVisible = false;
        QPointF m_reticlePos{0.5, 0.5};
        bool m_draggingReticle = false;
    };
}
