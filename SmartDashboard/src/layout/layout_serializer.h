#pragma once

#include <QRect>
#include <QString>

#include <vector>

class QWidget;

namespace sd::layout
{
    struct WidgetLayoutEntry
    {
        QString variableKey;
        QString widgetType;
        QRect geometry;
    };

    bool SaveLayout(const QWidget* canvas, const QString& filePath);
    bool LoadLayoutEntries(const QString& filePath, std::vector<WidgetLayoutEntry>& outEntries);
    QString GetDefaultLayoutPath();
}
