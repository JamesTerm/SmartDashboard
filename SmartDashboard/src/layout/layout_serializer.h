#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <vector>

class QWidget;

namespace sd::layout
{
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
        QVariant boolCheckboxShowLabel;
        QVariant boolLedShowLabel;
        QVariant stringTextShowLabel;
        QVariant stringChooserMode;
        QVariant stringChooserOptions;
    };

    bool SaveLayout(const QWidget* canvas, const QString& filePath);
    bool LoadLayoutEntries(const QString& filePath, std::vector<WidgetLayoutEntry>& outEntries);
    bool LoadLegacyXmlLayoutEntries(
        const QString& filePath,
        std::vector<WidgetLayoutEntry>& outEntries,
        QStringList* outIssues = nullptr
    );
    QString GetDefaultLayoutPath();
}
