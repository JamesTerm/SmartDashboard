#include "widgets/line_plot_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sd::widgets
{
    LinePlotWidget::LinePlotWidget(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumHeight(80);
        setAutoFillBackground(true);
    }

    void LinePlotWidget::AddSample(double value)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!m_hasStarted)
        {
            m_startTime = now;
            m_hasStarted = true;
        }

        const std::chrono::duration<double> elapsed = now - m_startTime;
        m_samples.push_back(SamplePoint{ elapsed.count(), value });

        while (static_cast<int>(m_samples.size()) > m_bufferSizeSamples)
        {
            m_samples.pop_front();
        }

        update();
    }

    void LinePlotWidget::ResetGraph()
    {
        m_samples.clear();
        m_hasStarted = false;
        update();
    }

    void LinePlotWidget::SetBufferSizeSamples(int samples)
    {
        if (samples < 2)
        {
            samples = 2;
        }

        m_bufferSizeSamples = samples;
        while (static_cast<int>(m_samples.size()) > m_bufferSizeSamples)
        {
            m_samples.pop_front();
        }

        update();
    }

    void LinePlotWidget::SetYAxisModeAuto(bool enabled)
    {
        m_autoYAxis = enabled;
        update();
    }

    void LinePlotWidget::SetYAxisLimits(double lowerLimit, double upperLimit)
    {
        m_manualYLowerLimit = lowerLimit;
        m_manualYUpperLimit = upperLimit;
        if (m_manualYUpperLimit <= m_manualYLowerLimit)
        {
            m_manualYUpperLimit = m_manualYLowerLimit + 0.001;
        }

        update();
    }

    void LinePlotWidget::SetShowNumberLines(bool enabled)
    {
        m_showNumberLines = enabled;
        update();
    }

    void LinePlotWidget::SetShowGridLines(bool enabled)
    {
        m_showGridLines = enabled;
        update();
    }

    void LinePlotWidget::paintEvent(QPaintEvent* event)
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const int leftPad = m_showNumberLines ? 44 : 6;
        const int rightPad = 6;
        const int topPad = 6;
        const int bottomPad = m_showNumberLines ? 24 : 6;
        const QRect drawRect = rect().adjusted(leftPad, topPad, -rightPad, -bottomPad);
        if (drawRect.width() <= 2 || drawRect.height() <= 2)
        {
            return;
        }

        painter.setPen(QPen(QColor("#3b3b3b"), 1));
        painter.drawRect(drawRect);

        const AxisRange xRange = ComputeXRange();
        const AxisRange yRange = ComputeYRange();
        const double xSpan = xRange.max - xRange.min;
        const double ySpan = yRange.max - yRange.min;
        if (xSpan <= 0.0 || ySpan <= 0.0)
        {
            return;
        }

        auto chooseTickStep = [](double span, int targetTicks)
        {
            if (span <= 0.0 || targetTicks <= 0)
            {
                return 1.0;
            }

            const double rough = span / static_cast<double>(targetTicks);
            const double power = std::pow(10.0, std::floor(std::log10(rough)));
            const double normalized = rough / power;

            double step = power;
            if (normalized <= 1.0)
            {
                step = 1.0 * power;
            }
            else if (normalized <= 2.0)
            {
                step = 2.0 * power;
            }
            else if (normalized <= 5.0)
            {
                step = 5.0 * power;
            }
            else
            {
                step = 10.0 * power;
            }

            return step;
        };

        auto formatTick = [](double value)
        {
            const double absValue = std::abs(value);
            if (absValue >= 100.0)
            {
                return QString::number(value, 'f', 0);
            }
            if (absValue >= 10.0)
            {
                return QString::number(value, 'f', 1);
            }
            return QString::number(value, 'f', 2);
        };

        const double yStep = chooseTickStep(ySpan, std::max(2, drawRect.height() / 60));

        // X-axis ticks: generate at fixed time intervals, render only visible ones
        std::vector<double> xTickValues;
        double xTickInterval;
        if (m_showNumberLines)
        {
            // Use pixel-driven spacing to determine tick interval
            const int targetInteriorTicks = std::max(4, drawRect.width() / 80);
            xTickInterval = xSpan / static_cast<double>(targetInteriorTicks + 1);
        }
        else
        {
            xTickInterval = chooseTickStep(xSpan, std::max(2, drawRect.width() / 90));
        }

        // Generate ticks at fixed time values that fall within visible range
        const double xStartTick = std::floor(xRange.min / xTickInterval) * xTickInterval;
        for (double xTick = xStartTick; xTick <= xRange.max + (xTickInterval * 0.5); xTick += xTickInterval)
        {
            if (xTick >= xRange.min - (xTickInterval * 0.01))
            {
                xTickValues.push_back(xTick);
            }
        }

        std::vector<double> yTickValues;
        int yTickSegments = 1;
        if (m_showNumberLines)
        {
            // Keep endpoint labels at min/max, then add interior divisions with
            // roughly consistent pixel spacing and at least 3 interior ticks.
            const int interiorTickCount = std::max(3, drawRect.height() / 28);
            yTickSegments = interiorTickCount + 1;
            for (int idx = 0; idx <= yTickSegments; ++idx)
            {
                const double ratio = static_cast<double>(idx) / static_cast<double>(yTickSegments);
                yTickValues.push_back(yRange.min + (ratio * ySpan));
            }
        }
        else
        {
            const double yStartTick = std::ceil(yRange.min / yStep) * yStep;
            for (double yTick = yStartTick; yTick <= yRange.max + (yStep * 0.5); yTick += yStep)
            {
                yTickValues.push_back(yTick);
            }
        }

        if (m_showGridLines)
        {
            painter.setPen(QPen(QColor("#2d2d2d"), 1));

            for (const double xTick : xTickValues)
            {
                const double xn = (xTick - xRange.min) / xSpan;
                if (xn < -0.001 || xn > 1.001)
                {
                    continue;
                }

                const qreal xPixel = drawRect.left() + (xn * drawRect.width());
                painter.drawLine(QPointF(xPixel, drawRect.top()), QPointF(xPixel, drawRect.bottom()));
            }

            for (const double yTick : yTickValues)
            {
                const double yn = (yTick - yRange.min) / ySpan;
                if (yn < -0.001 || yn > 1.001)
                {
                    continue;
                }

                const qreal yPixel = drawRect.bottom() - (yn * drawRect.height());
                painter.drawLine(QPointF(drawRect.left(), yPixel), QPointF(drawRect.right(), yPixel));
            }
        }

        if (m_showNumberLines)
        {
            painter.setPen(QPen(QColor("#5a5a5a"), 1));

            const double yDisplayStep = ySpan / static_cast<double>(yTickSegments);
            auto decimalsForStep = [](double step)
            {
                const double absStep = std::abs(step);
                if (absStep >= 1.0)
                {
                    return 1;
                }
                if (absStep >= 0.1)
                {
                    return 1;
                }
                if (absStep >= 0.01)
                {
                    return 2;
                }
                return 3;
            };

            const int xDecimals = decimalsForStep(xTickInterval);
            const int yDecimals = decimalsForStep(yDisplayStep);

            auto formatXTick = [xDecimals](double value)
            {
                return QString::number(value, 'f', xDecimals);
            };
            auto formatYTick = [yDecimals](double value)
            {
                return QString::number(value, 'f', yDecimals);
            };
            for (const double xTick : xTickValues)
            {
                const double xn = (xTick - xRange.min) / xSpan;
                if (xn < -0.001 || xn > 1.001)
                {
                    continue;
                }

                const qreal xPixel = drawRect.left() + (xn * drawRect.width());
                painter.drawLine(QPointF(xPixel, drawRect.bottom()), QPointF(xPixel, drawRect.bottom() + 4));
                painter.drawText(
                    QRectF(xPixel - 30.0, drawRect.bottom() + 5.0, 60.0, 16.0),
                    Qt::AlignHCenter | Qt::AlignTop,
                    formatXTick(xTick)
                );
            }

            for (const double yTick : yTickValues)
            {
                const double yn = (yTick - yRange.min) / ySpan;
                if (yn < -0.001 || yn > 1.001)
                {
                    continue;
                }

                const qreal yPixel = drawRect.bottom() - (yn * drawRect.height());
                painter.drawLine(QPointF(drawRect.left() - 4, yPixel), QPointF(drawRect.left(), yPixel));
                painter.drawText(
                    QRectF(0.0, yPixel - 8.0, drawRect.left() - 6.0, 16.0),
                    Qt::AlignRight | Qt::AlignVCenter,
                    formatYTick(yTick)
                );
            }
        }

        // Only draw line plot if we have at least 2 samples
        if (m_samples.size() >= 2)
        {
            QPainterPath path;
            bool first = true;
            for (const SamplePoint& sample : m_samples)
            {
                const double xNormalized = (sample.xSeconds - xRange.min) / xSpan;
                const double clippedY = std::clamp(sample.yValue, yRange.min, yRange.max);
                const double yNormalized = (clippedY - yRange.min) / ySpan;

                const qreal xPixel = drawRect.left() + (xNormalized * drawRect.width());
                const qreal yPixel = drawRect.bottom() - (yNormalized * drawRect.height());
                if (first)
                {
                    path.moveTo(xPixel, yPixel);
                    first = false;
                }
                else
                {
                    path.lineTo(xPixel, yPixel);
                }
            }

            painter.setPen(QPen(QColor("#33b5e5"), 1.5));
            painter.drawPath(path);
        }
    }

    LinePlotWidget::AxisRange LinePlotWidget::ComputeXRange() const
    {
        if (m_samples.empty())
        {
            return AxisRange{ 0.0, 1.0 };
        }

        const double headTime = m_samples.back().xSeconds;
        if (m_samples.size() < static_cast<size_t>(m_bufferSizeSamples))
        {
            return AxisRange{ 0.0, std::max(1.0, headTime) };
        }

        const double tailTime = m_samples.front().xSeconds;
        const double end = std::max(headTime, tailTime + 0.001);
        return AxisRange{ tailTime, end };
    }

    LinePlotWidget::AxisRange LinePlotWidget::ComputeYRange() const
    {
        if (!m_autoYAxis)
        {
            return AxisRange{ m_manualYLowerLimit, m_manualYUpperLimit };
        }

        if (m_samples.empty())
        {
            return AxisRange{ 0.0, 1.0 };
        }

        const YExtents recent = ComputeRecentYExtents();
        double minY = std::min(0.0, recent.min);
        double maxY = std::max(1.0, recent.max);

        if (maxY <= minY)
        {
            maxY = minY + 0.001;
        }

        return AxisRange{ minY, maxY };
    }

    LinePlotWidget::YExtents LinePlotWidget::ComputeRecentYExtents() const
    {
        if (m_samples.empty())
        {
            return YExtents{ 0.0, 1.0 };
        }

        const size_t sampleCount = m_samples.size();
        const size_t windowCount = std::min(sampleCount, static_cast<size_t>(m_bufferSizeSamples));
        const size_t startIndex = sampleCount - windowCount;

        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();
        for (size_t i = startIndex; i < sampleCount; ++i)
        {
            const double value = m_samples[i].yValue;
            minY = std::min(minY, value);
            maxY = std::max(maxY, value);
        }

        if (minY > maxY)
        {
            return YExtents{ 0.0, 1.0 };
        }

        return YExtents{ minY, maxY };
    }
}
