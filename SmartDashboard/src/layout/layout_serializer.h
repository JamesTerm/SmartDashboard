#pragma once

#include <QRect>
#include <QString>
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
        QVariant progressBarTickInterval;
        QVariant progressBarShowTickMarks;
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
        QVariant doubleNumericEditable;
        QVariant boolValue;
        QVariant doubleValue;
        QVariant stringValue;
    };

    bool SaveLayout(const QWidget* canvas, const QString& filePath);
    bool LoadLayoutEntries(const QString& filePath, std::vector<WidgetLayoutEntry>& outEntries);
    QString GetDefaultLayoutPath();
}
