#pragma once

#include <QWidget>
#include <QTimer>
#include <QColor>
#include <QString>

#include <chrono>
#include <deque>
#include <utility>
#include <vector>

class QPaintEvent;

namespace sd::widgets
{
    // Ian: Multi-series line plot widget.  The primary series (index 0) is
    // always the tile's own key.  Additional series are added when the user
    // drags other number tiles onto this plot.  Each series has its own
    // sample buffer, color, and label.  All series share X-axis timing,
    // Y-axis range, and buffer-size settings.
    class LinePlotWidget final : public QWidget
    {
    public:
        explicit LinePlotWidget(QWidget* parent = nullptr);

        // --- Single-series backward-compatible API (operates on primary series) ---
        void AddSample(double value);
        void ResetGraph();
        void SetBufferSizeSamples(int samples);
        void SetYAxisModeAuto(bool enabled);
        void SetYAxisLimits(double lowerLimit, double upperLimit);
        void SetShowNumberLines(bool enabled);
        void SetShowGridLines(bool enabled);
        void SetShowLegend(bool enabled);
        bool IsShowLegend() const;

        // --- Multi-series API ---
        // Ian: Series are identified by a string key (the NT variable key).
        // AddSampleToSeries creates the series on first call if it doesn't
        // exist, using nextDefaultColor for its color.
        int AddSeries(const QString& key, const QString& label, const QColor& color);
        void RemoveSeries(const QString& key);
        void AddSampleToSeries(const QString& key, double value);
        void SetSeriesColor(const QString& key, const QColor& color);
        void SetSeriesVisible(const QString& key, bool visible);
        bool IsSeriesVisible(const QString& key) const;
        int GetSeriesCount() const;
        QColor GetSeriesColor(const QString& key) const;
        QString GetSeriesLabel(const QString& key) const;
        QStringList GetSeriesKeys() const;
        void SetPrimarySeriesKey(const QString& key);

        // --- Testing API ---
        int GetSampleCountForTesting() const;
        double GetEstimatedSamplePeriodSecondsForTesting() const;
        std::pair<double, double> GetXRangeForTesting() const;
        double GetOldestSampleTimeForTesting() const;
        double GetLatestSampleTimeForTesting() const;
        double GetXTickIntervalForTesting(int drawWidth) const;

        // --- Default color palette (ROY-G-BIV) ---
        static const std::vector<QColor>& DefaultColorPalette();

    protected:
        void paintEvent(QPaintEvent* event) override;

    private:
        struct AxisRange
        {
            double min = 0.0;
            double max = 1.0;
        };

        struct SamplePoint
        {
            double xSeconds = 0.0;
            double yValue = 0.0;
        };

        struct YExtents
        {
            double min = 0.0;
            double max = 1.0;
        };

        struct Series
        {
            QString key;
            QString label;
            QColor color;
            std::deque<SamplePoint> samples;
            bool visible = true;  // Ian: When false, series line and legend label are hidden (Fix 4)
        };

        int FindSeriesIndex(const QString& key) const;
        void AddSampleInternal(Series& series, double value);
        AxisRange ComputeXRange() const;
        AxisRange ComputeYRange() const;
        YExtents ComputeRecentYExtents() const;
        static double ChooseNiceTickStep(double span, int targetTicks);
        double ComputeXTickInterval(int drawWidth) const;
        // Ian: Returns the total height consumed by the legend (Fix 4).
        // Multi-row wrapping means this can exceed a single row height.
        int DrawLegend(QPainter& painter, const QRect& drawRect, int availableWidth) const;
        int ComputeLegendHeight(int availableWidth) const;

        std::vector<Series> m_series;
        QString m_primaryKey;
        QTimer m_renderTimer;
        std::chrono::steady_clock::time_point m_startTime;
        std::chrono::steady_clock::time_point m_lastSampleTime;
        bool m_hasStarted = false;
        bool m_hasLastSampleTime = false;
        int m_bufferSizeSamples = 5000;
        bool m_autoYAxis = true;
        bool m_showNumberLines = false;
        bool m_showGridLines = false;
        double m_manualYLowerLimit = 0.0;
        double m_manualYUpperLimit = 1.0;
        double m_estimatedSamplePeriodSeconds = 0.02;
        mutable double m_lastXTickInterval = 0.0;
        int m_nextColorIndex = 0;
        bool m_showLegend = true;
    };
}
