/// @file camera_display_widget.cpp
/// @brief Custom QWidget that displays camera frames with aspect-ratio scaling.

#include "widgets/camera_display_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace sd::widgets
{

CameraDisplayWidget::CameraDisplayWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Ian: Black background when no frame is displayed, and also
    // for letterbox/pillarbox bars around the frame.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    setMouseTracking(true);
}

void CameraDisplayWidget::SetReticleVisible(bool visible)
{
    if (m_reticleVisible != visible)
    {
        m_reticleVisible = visible;
        update();
    }
}

bool CameraDisplayWidget::IsReticleVisible() const
{
    return m_reticleVisible;
}

void CameraDisplayWidget::SetReticlePosition(const QPointF& normalizedPos)
{
    m_reticlePos = normalizedPos;
    if (m_reticleVisible)
    {
        update();
    }
}

QPointF CameraDisplayWidget::GetReticlePosition() const
{
    return m_reticlePos;
}

void CameraDisplayWidget::SetFpsDisplay(double fps)
{
    m_fps = fps;
    // Ian: Don't call update() just for FPS — it will repaint on the
    // next frame arrival anyway.  Avoids double-painting.
}

void CameraDisplayWidget::SetStatusMessage(const QString& message)
{
    m_statusMessage = message;
    update();
}

void CameraDisplayWidget::OnFrameReady(const QImage& frame)
{
    m_currentFrame = frame;
    update();
}

void CameraDisplayWidget::ClearFrame()
{
    m_currentFrame = QImage();
    m_fps = 0.0;
    update();
}

void CameraDisplayWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);

    if (m_currentFrame.isNull())
    {
        PaintStatusMessage(painter);
        return;
    }

    PaintFrame(painter);

    if (m_reticleVisible)
    {
        PaintReticle(painter);
    }

    if (m_fps > 0.0)
    {
        PaintFpsCounter(painter);
    }
}

void CameraDisplayWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_reticleVisible && event->button() == Qt::LeftButton && !m_currentFrame.isNull())
    {
        m_draggingReticle = true;
        m_reticlePos = WidgetPosToNormalized(event->pos());
        update();
    }
    else
    {
        QWidget::mousePressEvent(event);
    }
}

void CameraDisplayWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_draggingReticle && !m_currentFrame.isNull())
    {
        m_reticlePos = WidgetPosToNormalized(event->pos());
        update();
    }
    else
    {
        QWidget::mouseMoveEvent(event);
    }
}

void CameraDisplayWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_draggingReticle && event->button() == Qt::LeftButton)
    {
        m_draggingReticle = false;
    }
    else
    {
        QWidget::mouseReleaseEvent(event);
    }
}

QRect CameraDisplayWidget::ComputeFrameRect() const
{
    if (m_currentFrame.isNull())
    {
        return rect();
    }

    const QSize frameSize = m_currentFrame.size();
    const QSize widgetSize = size();

    // Ian: Scale the frame to fit the widget while preserving aspect ratio.
    // This produces letterbox (top/bottom bars) or pillarbox (left/right bars)
    // depending on whether the frame is wider or taller than the widget.
    const double frameAspect = static_cast<double>(frameSize.width()) / static_cast<double>(frameSize.height());
    const double widgetAspect = static_cast<double>(widgetSize.width()) / static_cast<double>(widgetSize.height());

    int destW = 0;
    int destH = 0;

    if (frameAspect > widgetAspect)
    {
        // Frame is wider than widget — fit to width, letterbox top/bottom.
        destW = widgetSize.width();
        destH = static_cast<int>(std::round(static_cast<double>(destW) / frameAspect));
    }
    else
    {
        // Frame is taller than widget — fit to height, pillarbox left/right.
        destH = widgetSize.height();
        destW = static_cast<int>(std::round(static_cast<double>(destH) * frameAspect));
    }

    const int x = (widgetSize.width() - destW) / 2;
    const int y = (widgetSize.height() - destH) / 2;

    return QRect(x, y, destW, destH);
}

QPointF CameraDisplayWidget::WidgetPosToNormalized(const QPoint& widgetPos) const
{
    const QRect frameRect = ComputeFrameRect();
    if (frameRect.width() <= 0 || frameRect.height() <= 0)
    {
        return {0.5, 0.5};
    }

    double nx = static_cast<double>(widgetPos.x() - frameRect.x()) / static_cast<double>(frameRect.width());
    double ny = static_cast<double>(widgetPos.y() - frameRect.y()) / static_cast<double>(frameRect.height());

    // Clamp to [0, 1].
    nx = std::clamp(nx, 0.0, 1.0);
    ny = std::clamp(ny, 0.0, 1.0);

    return {nx, ny};
}

void CameraDisplayWidget::PaintFrame(QPainter& painter)
{
    const QRect destRect = ComputeFrameRect();

    // Ian: Qt::SmoothTransformation gives bilinear filtering for decent
    // quality when scaling down.  For scaling up (zooming in), it's also
    // acceptable.  If this becomes a bottleneck, switch to Qt::FastTransformation.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(destRect, m_currentFrame);
}

void CameraDisplayWidget::PaintReticle(QPainter& painter)
{
    const QRect frameRect = ComputeFrameRect();

    // Ian: Convert normalized reticle position to widget pixel coordinates
    // within the frame display area.
    const double px = frameRect.x() + m_reticlePos.x() * frameRect.width();
    const double py = frameRect.y() + m_reticlePos.y() * frameRect.height();

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Ian: Green targeting reticle — crosshair + circle.
    // The size scales with the smaller dimension of the frame rect
    // so it looks proportional at any resolution.
    const double reticleRadius = std::min(frameRect.width(), frameRect.height()) * 0.05;
    const double crosshairLength = reticleRadius * 1.5;

    QPen pen(QColor(0, 255, 0), 2.0);
    pen.setCosmetic(true);  // Constant pixel width regardless of transform.
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Circle.
    painter.drawEllipse(QPointF(px, py), reticleRadius, reticleRadius);

    // Crosshair lines.
    painter.drawLine(QPointF(px - crosshairLength, py), QPointF(px + crosshairLength, py));
    painter.drawLine(QPointF(px, py - crosshairLength), QPointF(px, py + crosshairLength));
}

void CameraDisplayWidget::PaintStatusMessage(QPainter& painter)
{
    if (m_statusMessage.isEmpty())
    {
        // Default message when no stream is connected.
        painter.setPen(QColor(128, 128, 128));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 12));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No camera stream"));
        return;
    }

    painter.setPen(QColor(200, 200, 200));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    painter.drawText(rect(), Qt::AlignCenter, m_statusMessage);
}

void CameraDisplayWidget::PaintFpsCounter(QPainter& painter)
{
    const QString fpsText = QStringLiteral("%1 fps").arg(m_fps, 0, 'f', 1);

    painter.setPen(QColor(0, 255, 0));
    painter.setFont(QFont(QStringLiteral("Consolas"), 9));

    // Ian: Draw in the top-right corner of the widget with a small margin.
    const QRect textRect(width() - 80, 4, 76, 16);
    painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, fpsText);
}

}  // namespace sd::widgets
