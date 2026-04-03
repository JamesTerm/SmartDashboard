#pragma once

#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <vector>

class QWidget;

namespace sd::layout
{
    // Ian: Describes one additional data source in a multi-line plot.
    // The primary series is always the tile's own variableKey; these
    // are the extras merged via drag-and-drop.
    struct LinePlotSourceEntry
    {
        QString key;
        QString color;  // hex RGB, e.g. "#e74c3c"
        QString originalWidgetType;  // widget type before absorption (Fix 5)
        bool visible = true;  // Ian: legend/series visibility (Fix 4)
    };

    struct WidgetLayoutEntry
    {
        QString variableKey;
        QString widgetType;
        QRect geometry;
        QVariant gaugeLowerLimit;
        QVariant gaugeUpperLimit;
        QVariant gaugeTickInterval;
        QVariant gaugeShowTickMarks;
        QVariant progressBarLowerLimit;
        QVariant progressBarUpperLimit;
        QVariant progressBarShowPercentage;
        QVariant progressBarForegroundColor;
        QVariant progressBarBackgroundColor;
        QVariant sliderLowerLimit;
        QVariant sliderUpperLimit;
        QVariant sliderTickInterval;
        QVariant sliderShowTickMarks;
        QVariant linePlotBufferSizeSamples;
        QVariant linePlotAutoYAxis;
        QVariant linePlotShowNumberLines;
        QVariant linePlotShowGridLines;
        QVariant linePlotYLowerLimit;
        QVariant linePlotYUpperLimit;
        QVariant textFontPointSize;
        QVariant doubleNumericEditable;
        QVariant showLabel;
        QVariant stringChooserMode;
        QVariant stringChooserOptions;
        // Ian: Additional data sources for multi-line plots.
        // Empty for single-series line plots and all other widget types.
        std::vector<LinePlotSourceEntry> linePlotSources;
        // Ian: When true the line plot tile accepts drag-and-drop merges.
        // Persisted so the user doesn't have to re-enable after reload.
        bool multiLinePlotMode = false;
    };

    // Ian: hiddenKeys is a top-level JSON array in the layout file that captures
    // which signal keys were hidden at save time.  This lets different layout
    // files act as different dashboard "views" without needing a tab system.
    // Older layout files without the field load with an empty set (show all).
    bool SaveLayout(const QWidget* canvas, const QString& filePath, const QSet<QString>& hiddenKeys = {});
    bool LoadLayoutEntries(const QString& filePath, std::vector<WidgetLayoutEntry>& outEntries, QSet<QString>* outHiddenKeys = nullptr);
    bool LoadLegacyXmlLayoutEntries(
        const QString& filePath,
        std::vector<WidgetLayoutEntry>& outEntries,
        QStringList* outIssues = nullptr
    );
    QString GetDefaultLayoutPath();
}
