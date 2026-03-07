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
            outEntries.push_back(layoutEntry);
        }

        return true;
    }
}
