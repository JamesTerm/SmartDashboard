#include "layout/layout_serializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QWidget>

namespace sd::layout
{
    QString GetDefaultLayoutPath()
    {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        return base + "/layout.json";
    }

    bool SaveLayout(const QWidget* canvas, const QString& filePath)
    {
        if (canvas == nullptr)
        {
            return false;
        }

        // Snapshot pattern: serialize current widget metadata/geometry to JSON.
        QJsonArray widgets;
        const QObjectList children = canvas->children();
        for (QObject* child : children)
        {
            QWidget* widget = qobject_cast<QWidget*>(child);
            if (widget == nullptr)
            {
                continue;
            }

            if (widget->objectName().isEmpty())
            {
                continue;
            }

            const QRect geometry = widget->geometry();
            QJsonObject entry;
            entry["widgetId"] = widget->objectName();
            entry["variableKey"] = widget->property("variableKey").toString();
            entry["widgetType"] = widget->property("widgetType").toString();

            const QVariant gaugeLower = widget->property("gaugeLowerLimit");
            const QVariant gaugeUpper = widget->property("gaugeUpperLimit");
            const QVariant gaugeTick = widget->property("gaugeTickInterval");
            const QVariant gaugeShow = widget->property("gaugeShowTickMarks");
            if (gaugeLower.isValid())
            {
                entry["gaugeLowerLimit"] = gaugeLower.toDouble();
            }
            if (gaugeUpper.isValid())
            {
                entry["gaugeUpperLimit"] = gaugeUpper.toDouble();
            }
            if (gaugeTick.isValid())
            {
                entry["gaugeTickInterval"] = gaugeTick.toDouble();
            }
            if (gaugeShow.isValid())
            {
                entry["gaugeShowTickMarks"] = gaugeShow.toBool();
            }

            QJsonObject geo;
            geo["x"] = geometry.x();
            geo["y"] = geometry.y();
            geo["w"] = geometry.width();
            geo["h"] = geometry.height();
            entry["geometry"] = geo;
            widgets.append(entry);
        }

        QJsonObject root;
        root["version"] = 1;
        root["widgets"] = widgets;

        QFile file(filePath);
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }

        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return true;
    }

    bool LoadLayoutEntries(const QString& filePath, std::vector<WidgetLayoutEntry>& outEntries)
    {
        QFile file(filePath);
        if (!file.exists())
        {
            return false;
        }

        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
        {
            return false;
        }

        // Defensive parsing: skip incomplete entries and keep loading valid ones.
        outEntries.clear();
        const QJsonArray widgets = doc.object().value("widgets").toArray();
        for (const QJsonValue& value : widgets)
        {
            const QJsonObject entry = value.toObject();
            const QString variableKey = entry.value("variableKey").toString();
            if (variableKey.isEmpty())
            {
                continue;
            }

            const QJsonObject geo = entry.value("geometry").toObject();
            WidgetLayoutEntry layoutEntry;
            layoutEntry.variableKey = variableKey;
            layoutEntry.widgetType = entry.value("widgetType").toString();
            layoutEntry.geometry = QRect(
                geo.value("x").toInt(),
                geo.value("y").toInt(),
                geo.value("w").toInt(220),
                geo.value("h").toInt(84)
            );
            if (entry.contains("gaugeLowerLimit"))
            {
                layoutEntry.gaugeLowerLimit = entry.value("gaugeLowerLimit").toDouble();
            }
            if (entry.contains("gaugeUpperLimit"))
            {
                layoutEntry.gaugeUpperLimit = entry.value("gaugeUpperLimit").toDouble();
            }
            if (entry.contains("gaugeTickInterval"))
            {
                layoutEntry.gaugeTickInterval = entry.value("gaugeTickInterval").toDouble();
            }
            if (entry.contains("gaugeShowTickMarks"))
            {
                layoutEntry.gaugeShowTickMarks = entry.value("gaugeShowTickMarks").toBool();
            }
            outEntries.push_back(layoutEntry);
        }

        return true;
    }
}
