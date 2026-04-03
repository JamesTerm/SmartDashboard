#include "widgets/line_plot_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sd::widgets
{
    namespace
    {
        // Ian: Extract the leaf name from a NetworkTables key path for legend
        // labels.  E.g. "/SmartDashboard/Drive/LeftSpeed" -> "LeftSpeed".
        // Mirrors the LeafName helper in variable_tile.cpp and the logic in
        // MainWindow::BuildDisplayLabel.
        QString LeafName(const QString& key)
        {
            const QStringList segments = key.split('/', Qt::SkipEmptyParts);
            if (segments.isEmpty())
            {
                return key;
            }
            return segments.back();
        }
    }

    // Ian: ROY-G-BIV palette for multi-line plot default colors.  The first
    // color (#33b5e5) is the legacy single-series blue, kept as the primary
    // series color for visual continuity with existing single-line plots.
    const std::vector<QColor>& LinePlotWidget::DefaultColorPalette()
    {
        static const std::vector<QColor> palette =
        {
            QColor("#33b5e5"),  // Original blue (primary series)
            QColor("#e74c3c"),  // Red
            QColor("#f39c12"),  // Orange
            QColor("#f1c40f"),  // Yellow
            QColor("#2ecc71"),  // Green
            QColor("#3498db"),  // Blue (lighter)
            QColor("#8e44ad"),  // Indigo/Violet
        };
        return palette;
    }

    LinePlotWidget::LinePlotWidget(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumHeight(80);
        setAutoFillBackground(true);

        // Render-cadence decoupling: draw at a steady rate so axis updates do not
        // inherit transport jitter from irregular sample arrival times.
        m_renderTimer.setInterval(16);
        connect(&m_renderTimer, &QTimer::timeout, this, [this]()
        {
            update();
        });
    }

    // --- Backward-compatible single-series API (operates on primary series) ---

    void LinePlotWidget::AddSample(double value)
    {
        // Ian: Find or create the primary series by key, NOT by index.
        // After persistence reload, SetPlotSources may have already pushed
        // secondary series into m_series before the primary receives its
        // first sample.  Using m_series[0] in that case would write the
        // primary tile's data into the first secondary — causing one line
        // to show merged data and an X-range truncation on the real series.
        int primaryIdx = FindSeriesIndex(m_primaryKey);
        if (primaryIdx < 0)
        {
            const auto& palette = DefaultColorPalette();
            Series primary;
            primary.key = m_primaryKey;
            primary.label = LeafName(m_primaryKey);
            primary.color = palette[0];
            // Insert at front so index 0 is the primary for legacy callers
            m_series.insert(m_series.begin(), std::move(primary));
            primaryIdx = 0;
            m_nextColorIndex = 1;
        }

        AddSampleInternal(m_series[primaryIdx], value);
    }

    void LinePlotWidget::ResetGraph()
    {
        for (auto& series : m_series)
        {
            series.samples.clear();
        }
        m_hasStarted = false;
        m_hasLastSampleTime = false;
        m_estimatedSamplePeriodSeconds = 0.02;
        m_lastXTickInterval = 0.0;
        m_renderTimer.stop();
        update();
    }

    void LinePlotWidget::SetBufferSizeSamples(int samples)
    {
        if (samples < 2)
        {
            samples = 2;
        }

        m_bufferSizeSamples = samples;
        for (auto& series : m_series)
        {
            while (static_cast<int>(series.samples.size()) > m_bufferSizeSamples)
            {
                series.samples.pop_front();
            }
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

    void LinePlotWidget::SetShowLegend(bool enabled)
    {
        m_showLegend = enabled;
        update();
    }

    bool LinePlotWidget::IsShowLegend() const
    {
        return m_showLegend;
    }

    // --- Multi-series API ---

    int LinePlotWidget::AddSeries(const QString& key, const QString& label, const QColor& color)
    {
        // Don't add duplicates
        const int existing = FindSeriesIndex(key);
        if (existing >= 0)
        {
            return existing;
        }

        Series s;
        s.key = key;
        s.label = label;
        s.color = color;
        m_series.push_back(std::move(s));
        return static_cast<int>(m_series.size()) - 1;
    }

    void LinePlotWidget::RemoveSeries(const QString& key)
    {
        const int idx = FindSeriesIndex(key);
        if (idx < 0)
        {
            return;
        }

        m_series.erase(m_series.begin() + idx);
    }

    void LinePlotWidget::AddSampleToSeries(const QString& key, double value)
    {
        const int idx = FindSeriesIndex(key);
        if (idx < 0)
        {
            return;
        }

        AddSampleInternal(m_series[idx], value);
    }

    void LinePlotWidget::SetSeriesColor(const QString& key, const QColor& color)
    {
        const int idx = FindSeriesIndex(key);
        if (idx >= 0)
        {
            m_series[idx].color = color;
            update();
        }
    }

    void LinePlotWidget::SetSeriesVisible(const QString& key, bool visible)
    {
        const int idx = FindSeriesIndex(key);
        if (idx >= 0)
        {
            m_series[idx].visible = visible;
            update();
        }
    }

    bool LinePlotWidget::IsSeriesVisible(const QString& key) const
    {
        const int idx = FindSeriesIndex(key);
        if (idx >= 0)
        {
            return m_series[idx].visible;
        }
        return true;
    }

    int LinePlotWidget::GetSeriesCount() const
    {
        return static_cast<int>(m_series.size());
    }

    QColor LinePlotWidget::GetSeriesColor(const QString& key) const
    {
        const int idx = FindSeriesIndex(key);
        if (idx >= 0)
        {
            return m_series[idx].color;
        }
        return QColor();
    }

    QString LinePlotWidget::GetSeriesLabel(const QString& key) const
    {
        const int idx = FindSeriesIndex(key);
        if (idx >= 0)
        {
            return m_series[idx].label;
        }
        return {};
    }

    QStringList LinePlotWidget::GetSeriesKeys() const
    {
        QStringList keys;
        keys.reserve(static_cast<int>(m_series.size()));
        for (const auto& s : m_series)
        {
            keys.push_back(s.key);
        }
        return keys;
    }

    void LinePlotWidget::SetPrimarySeriesKey(const QString& key)
    {
        m_primaryKey = key;
    }

    // --- Testing API ---

    int LinePlotWidget::GetSampleCountForTesting() const
    {
        const int idx = FindSeriesIndex(m_primaryKey);
        if (idx < 0)
        {
            return 0;
        }
        return static_cast<int>(m_series[idx].samples.size());
    }

    double LinePlotWidget::GetEstimatedSamplePeriodSecondsForTesting() const
    {
        return m_estimatedSamplePeriodSeconds;
    }

    std::pair<double, double> LinePlotWidget::GetXRangeForTesting() const
    {
        const AxisRange range = ComputeXRange();
        return { range.min, range.max };
    }

    double LinePlotWidget::GetOldestSampleTimeForTesting() const
    {
        const int idx = FindSeriesIndex(m_primaryKey);
        if (idx < 0 || m_series[idx].samples.empty())
        {
            return 0.0;
        }
        return m_series[idx].samples.front().xSeconds;
    }

    double LinePlotWidget::GetLatestSampleTimeForTesting() const
    {
        const int idx = FindSeriesIndex(m_primaryKey);
        if (idx < 0 || m_series[idx].samples.empty())
        {
            return 0.0;
        }
        return m_series[idx].samples.back().xSeconds;
    }

    double LinePlotWidget::GetXTickIntervalForTesting(int drawWidth) const
    {
        return ComputeXTickInterval(drawWidth);
    }

    // --- Internal helpers ---

    int LinePlotWidget::FindSeriesIndex(const QString& key) const
    {
        for (int i = 0; i < static_cast<int>(m_series.size()); ++i)
        {
            if (m_series[i].key == key)
            {
                return i;
            }
        }
        return -1;
    }

    void LinePlotWidget::AddSampleInternal(Series& series, double value)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!m_hasStarted)
        {
            m_startTime = now;
            m_hasStarted = true;
            m_lastSampleTime = now;
            m_hasLastSampleTime = true;
            series.samples.push_back(SamplePoint{ 0.0, value });
        }
        else if (m_hasLastSampleTime)
        {
            const std::chrono::duration<double> delta = now - m_lastSampleTime;
            const double deltaSeconds = std::clamp(delta.count(), 0.001, 1.0);
            // Exponential moving average: smooth sample-period estimate used to
            // derive a stable visible time window from sample-count buffer size.
            m_estimatedSamplePeriodSeconds =
                (0.1 * deltaSeconds) + (0.9 * m_estimatedSamplePeriodSeconds);
            m_lastSampleTime = now;

            // Ian: Use the global X time base for all series so they align on
            // the same time axis.  The X position is computed from the shared
            // start time, not per-series.
            const std::chrono::duration<double> sinceStart = now - m_startTime;
            series.samples.push_back(SamplePoint{ sinceStart.count(), value });
        }

        if (!m_renderTimer.isActive())
        {
            m_renderTimer.start();
        }

        while (static_cast<int>(series.samples.size()) > m_bufferSizeSamples)
        {
            series.samples.pop_front();
        }
    }

    // --- Paint ---

    void LinePlotWidget::paintEvent(QPaintEvent* event)
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const bool hasMultipleSeries = (m_series.size() > 1);
        const int leftPad = m_showNumberLines ? 44 : 6;
        const int rightPad = 6;
        // Ian: Dynamic legend height — wraps to multiple rows when keys
        // exceed available width (Fix 4).  Legend is completely hidden when
        // m_showLegend is false, reclaiming the vertical space for the plot.
        const bool showLegend = hasMultipleSeries && m_showLegend;
        const int legendHeight = showLegend ? ComputeLegendHeight(rect().width() - leftPad - rightPad) : 0;

        const int topPad = 6 + legendHeight;
        const int bottomPad = m_showNumberLines ? 24 : 6;
        const QRect drawRect = rect().adjusted(leftPad, topPad, -rightPad, -bottomPad);
        if (drawRect.width() <= 2 || drawRect.height() <= 2)
        {
            return;
        }

        // Draw legend above the plot area when multi-series and legend visible
        if (showLegend)
        {
            DrawLegend(painter, QRect(leftPad, 4, drawRect.width(), legendHeight), drawRect.width());
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
        static_cast<void>(formatTick);  // suppress unused warning when numberLines off

        const double yStep = ChooseNiceTickStep(ySpan, std::max(2, drawRect.height() / 60));

        // X-axis ticks: generate at fixed time intervals, render only visible ones
        std::vector<double> xTickValues;
        const double xTickInterval = ComputeXTickInterval(drawRect.width());

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

        // Draw all series
        for (const Series& series : m_series)
        {
            // Ian: Skip hidden series (Fix 4 hide-key option)
            if (!series.visible)
            {
                continue;
            }

            if (series.samples.size() < 2)
            {
                continue;
            }

            QPainterPath path;
            bool first = true;
            for (const SamplePoint& sample : series.samples)
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

            painter.setPen(QPen(series.color, 1.5));
            painter.drawPath(path);
        }
    }

    // Ian: Multi-row legend with wrapping (Fix 4).  Returns total height used.
    // Hidden series (visible=false) are skipped.  Each row is rowHeight pixels.
    int LinePlotWidget::DrawLegend(QPainter& painter, const QRect& legendRect, int availableWidth) const
    {
        QFont legendFont = font();
        legendFont.setPointSize(7);
        painter.setFont(legendFont);

        const QFontMetrics fm(legendFont);
        const int lineLen = 14;
        const int gap = 6;
        const int textGap = 3;
        const int rowHeight = 16;
        const int maxLabelWidth = 100;

        int x = legendRect.left();
        int y = legendRect.top();
        int rowCount = 1;

        for (const Series& series : m_series)
        {
            // Ian: Skip hidden series from legend display (Fix 4 hide-key)
            if (!series.visible)
            {
                continue;
            }

            const QString label = series.label.isEmpty() ? series.key : series.label;
            const int labelWidth = std::min(maxLabelWidth, fm.horizontalAdvance(label));
            const int entryWidth = lineLen + textGap + labelWidth + gap;

            // Wrap to next row if this entry won't fit
            if (x + entryWidth > legendRect.left() + availableWidth && x > legendRect.left())
            {
                x = legendRect.left();
                y += rowHeight;
                rowCount++;
            }

            // Draw colored line segment
            painter.setPen(QPen(series.color, 2));
            const int lineY = y + rowHeight / 2;
            painter.drawLine(x, lineY, x + lineLen, lineY);
            x += lineLen + textGap;

            // Draw label
            painter.setPen(QPen(QColor("#b0b0b0"), 1));
            const QString elidedLabel = fm.elidedText(label, Qt::ElideRight, std::min(maxLabelWidth, legendRect.left() + availableWidth - x));
            painter.drawText(x, y, availableWidth, rowHeight, Qt::AlignLeft | Qt::AlignVCenter, elidedLabel);
            x += fm.horizontalAdvance(elidedLabel) + gap;
        }

        painter.setFont(font());  // restore
        return rowCount * rowHeight;
    }

    int LinePlotWidget::ComputeLegendHeight(int availableWidth) const
    {
        // Ian: Pre-compute how many rows the legend needs without painting.
        QFont legendFont = font();
        legendFont.setPointSize(7);
        const QFontMetrics fm(legendFont);
        const int lineLen = 14;
        const int gap = 6;
        const int textGap = 3;
        const int rowHeight = 16;
        const int maxLabelWidth = 100;

        int x = 0;
        int rowCount = 1;

        for (const Series& series : m_series)
        {
            if (!series.visible)
            {
                continue;
            }

            const QString label = series.label.isEmpty() ? series.key : series.label;
            const int labelWidth = std::min(maxLabelWidth, fm.horizontalAdvance(label));
            const int entryWidth = lineLen + textGap + labelWidth + gap;

            if (x + entryWidth > availableWidth && x > 0)
            {
                x = 0;
                rowCount++;
            }

            x += entryWidth;
        }

        return rowCount * rowHeight;
    }

    // --- Axis computations ---

    LinePlotWidget::AxisRange LinePlotWidget::ComputeXRange() const
    {
        // Compute the union of all series' X ranges
        double globalStart = std::numeric_limits<double>::max();
        double globalEnd = std::numeric_limits<double>::lowest();
        bool hasSamples = false;

        for (const Series& series : m_series)
        {
            if (series.samples.empty())
            {
                continue;
            }
            hasSamples = true;
            globalStart = std::min(globalStart, series.samples.front().xSeconds);
            globalEnd = std::max(globalEnd, series.samples.back().xSeconds);
        }

        if (!hasSamples)
        {
            return AxisRange{ 0.0, 1.0 };
        }

        const double end = std::max(globalEnd, globalStart + 0.001);
        return AxisRange{ globalStart, end };
    }

    double LinePlotWidget::ChooseNiceTickStep(double span, int targetTicks)
    {
        if (span <= 0.0 || targetTicks <= 0)
        {
            return 1.0;
        }

        const double rough = span / static_cast<double>(targetTicks);
        const double power = std::pow(10.0, std::floor(std::log10(rough)));
        const double normalized = rough / power;

        if (normalized <= 1.0)
        {
            return 1.0 * power;
        }
        if (normalized <= 2.0)
        {
            return 2.0 * power;
        }
        if (normalized <= 5.0)
        {
            return 5.0 * power;
        }
        return 10.0 * power;
    }

    double LinePlotWidget::ComputeXTickInterval(int drawWidth) const
    {
        const AxisRange xRange = ComputeXRange();
        const double xSpan = xRange.max - xRange.min;
        if (xSpan <= 0.0)
        {
            return 1.0;
        }

        const int targetTicks = m_showNumberLines ? std::max(4, drawWidth / 80) : std::max(2, drawWidth / 90);
        const double candidate = ChooseNiceTickStep(xSpan, targetTicks);
        if (m_lastXTickInterval <= 0.0)
        {
            m_lastXTickInterval = candidate;
            return candidate;
        }

        // Tick hysteresis: hold current spacing unless candidate moves outside a
        // tolerance window. This prevents 1/2/5 step flip-flop around thresholds.
        const double holdLow = m_lastXTickInterval * 0.70;
        const double holdHigh = m_lastXTickInterval * 1.60;
        if (candidate < holdLow || candidate > holdHigh)
        {
            m_lastXTickInterval = candidate;
        }

        return m_lastXTickInterval;
    }

    LinePlotWidget::AxisRange LinePlotWidget::ComputeYRange() const
    {
        if (!m_autoYAxis)
        {
            return AxisRange{ m_manualYLowerLimit, m_manualYUpperLimit };
        }

        bool hasSamples = false;
        for (const Series& series : m_series)
        {
            if (!series.samples.empty())
            {
                hasSamples = true;
                break;
            }
        }

        if (!hasSamples)
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
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();
        bool hasSamples = false;

        for (const Series& series : m_series)
        {
            if (series.samples.empty())
            {
                continue;
            }

            hasSamples = true;
            const size_t sampleCount = series.samples.size();
            const size_t windowCount = std::min(sampleCount, static_cast<size_t>(m_bufferSizeSamples));
            const size_t startIndex = sampleCount - windowCount;

            for (size_t i = startIndex; i < sampleCount; ++i)
            {
                const double value = series.samples[i].yValue;
                minY = std::min(minY, value);
                maxY = std::max(maxY, value);
            }
        }

        if (!hasSamples || minY > maxY)
        {
            return YExtents{ 0.0, 1.0 };
        }

        return YExtents{ minY, maxY };
    }
}
