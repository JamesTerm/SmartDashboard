#include "layout/layout_serializer.h"

#include "widgets/variable_tile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QWidget>
#include <QXmlStreamReader>

#include <unordered_map>

namespace sd::layout
{
    namespace
    {
        sd::widgets::VariableType LegacyTypeToVariableType(const QString& legacyType)
        {
            if (legacyType.compare("Boolean", Qt::CaseInsensitive) == 0)
            {
                return sd::widgets::VariableType::Bool;
            }
            if (legacyType.compare("Number", Qt::CaseInsensitive) == 0)
            {
                return sd::widgets::VariableType::Double;
            }

            return sd::widgets::VariableType::String;
        }

        QString LegacyClassToWidgetType(const QString& legacyClass, sd::widgets::VariableType variableType)
        {
            if (legacyClass.endsWith("SimpleDial"))
            {
                return "double.gauge";
            }
            if (legacyClass.endsWith("ProgressBar"))
            {
                return "double.progress";
            }
            if (legacyClass.endsWith("CheckBox"))
            {
                return "bool.checkbox";
            }
            if (legacyClass.endsWith("BooleanBox"))
            {
                return "bool.led";
            }
            if (legacyClass.endsWith("TextBox") || legacyClass.endsWith("FormattedField"))
            {
                if (variableType == sd::widgets::VariableType::Double)
                {
                    return "double.numeric";
                }
                if (variableType == sd::widgets::VariableType::Bool)
                {
                    return "bool.text";
                }

                return "string.text";
            }

            if (variableType == sd::widgets::VariableType::Bool)
            {
                return "bool.text";
            }
            if (variableType == sd::widgets::VariableType::Double)
            {
                return "double.numeric";
            }

            return "string.text";
        }

        QRect LegacyDefaultGeometry(const QString& widgetType, int x, int y)
        {
            if (widgetType == "double.gauge")
            {
                return QRect(x, y, 120, 90);
            }
            if (widgetType == "double.progress")
            {
                return QRect(x, y, 120, 30);
            }
            if (widgetType == "bool.checkbox")
            {
                return QRect(x, y, 160, 30);
            }
            if (widgetType == "bool.led")
            {
                return QRect(x, y, 130, 30);
            }
            if (widgetType == "double.numeric")
            {
                return QRect(x, y, 110, 30);
            }

            return QRect(x, y, 220, 84);
        }

        bool TryParseDouble(const QString& text, double& outValue)
        {
            bool ok = false;
            const double value = text.toDouble(&ok);
            if (!ok)
            {
                return false;
            }

            outValue = value;
            return true;
        }
    }

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
            const QVariant progressBarLower = widget->property("progressBarLowerLimit");
            const QVariant progressBarUpper = widget->property("progressBarUpperLimit");
            const QVariant progressBarShowPercentage = widget->property("progressBarShowPercentage");
            const QVariant progressBarForegroundColor = widget->property("progressBarForegroundColor");
            const QVariant progressBarBackgroundColor = widget->property("progressBarBackgroundColor");
            const QVariant sliderLower = widget->property("sliderLowerLimit");
            const QVariant sliderUpper = widget->property("sliderUpperLimit");
            const QVariant sliderTick = widget->property("sliderTickInterval");
            const QVariant sliderShow = widget->property("sliderShowTickMarks");
            const QVariant linePlotBufferSize = widget->property("linePlotBufferSizeSamples");
            const QVariant linePlotAutoY = widget->property("linePlotAutoYAxis");
            const QVariant linePlotShowNumberLines = widget->property("linePlotShowNumberLines");
            const QVariant linePlotShowGridLines = widget->property("linePlotShowGridLines");
            const QVariant linePlotYLower = widget->property("linePlotYLowerLimit");
            const QVariant linePlotYUpper = widget->property("linePlotYUpperLimit");
            const QVariant textFontPointSize = widget->property("textFontPointSize");
            const QVariant doubleNumericEditable = widget->property("doubleNumericEditable");
            const QVariant boolCheckboxShowLabel = widget->property("boolCheckboxShowLabel");
            const QVariant stringChooserMode = widget->property("stringChooserMode");
            const QVariant stringChooserOptions = widget->property("stringChooserOptions");
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
            if (progressBarLower.isValid())
            {
                entry["progressBarLowerLimit"] = progressBarLower.toDouble();
            }
            if (progressBarUpper.isValid())
            {
                entry["progressBarUpperLimit"] = progressBarUpper.toDouble();
            }
            if (progressBarShowPercentage.isValid())
            {
                entry["progressBarShowPercentage"] = progressBarShowPercentage.toBool();
            }
            if (progressBarForegroundColor.isValid())
            {
                entry["progressBarForegroundColor"] = progressBarForegroundColor.toString();
            }
            if (progressBarBackgroundColor.isValid())
            {
                entry["progressBarBackgroundColor"] = progressBarBackgroundColor.toString();
            }
            if (sliderLower.isValid())
            {
                entry["sliderLowerLimit"] = sliderLower.toDouble();
            }
            if (sliderUpper.isValid())
            {
                entry["sliderUpperLimit"] = sliderUpper.toDouble();
            }
            if (sliderTick.isValid())
            {
                entry["sliderTickInterval"] = sliderTick.toDouble();
            }
            if (sliderShow.isValid())
            {
                entry["sliderShowTickMarks"] = sliderShow.toBool();
            }
            if (linePlotBufferSize.isValid())
            {
                entry["linePlotBufferSizeSamples"] = linePlotBufferSize.toInt();
            }
            if (linePlotAutoY.isValid())
            {
                entry["linePlotAutoYAxis"] = linePlotAutoY.toBool();
            }
            if (linePlotShowNumberLines.isValid())
            {
                entry["linePlotShowNumberLines"] = linePlotShowNumberLines.toBool();
            }
            if (linePlotShowGridLines.isValid())
            {
                entry["linePlotShowGridLines"] = linePlotShowGridLines.toBool();
            }
            if (linePlotYLower.isValid())
            {
                entry["linePlotYLowerLimit"] = linePlotYLower.toDouble();
            }
            if (linePlotYUpper.isValid())
            {
                entry["linePlotYUpperLimit"] = linePlotYUpper.toDouble();
            }
            if (textFontPointSize.isValid())
            {
                entry["textFontPointSize"] = textFontPointSize.toInt();
            }
            if (doubleNumericEditable.isValid())
            {
                entry["doubleNumericEditable"] = doubleNumericEditable.toBool();
            }
            if (boolCheckboxShowLabel.isValid())
            {
                entry["boolCheckboxShowLabel"] = boolCheckboxShowLabel.toBool();
            }
            if (stringChooserMode.isValid())
            {
                entry["stringChooserMode"] = stringChooserMode.toBool();
            }
            if (stringChooserOptions.isValid())
            {
                entry["stringChooserOptions"] = QJsonArray::fromStringList(stringChooserOptions.toStringList());
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

            // Ian: Layout files store widget arrangement/config only. Do not
            // restore live robot values from disk or the dashboard can overwrite
            // current session state on startup.
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
            if (entry.contains("progressBarLowerLimit"))
            {
                layoutEntry.progressBarLowerLimit = entry.value("progressBarLowerLimit").toDouble();
            }
            if (entry.contains("progressBarUpperLimit"))
            {
                layoutEntry.progressBarUpperLimit = entry.value("progressBarUpperLimit").toDouble();
            }
            if (entry.contains("progressBarShowPercentage"))
            {
                layoutEntry.progressBarShowPercentage = entry.value("progressBarShowPercentage").toBool();
            }
            if (entry.contains("progressBarForegroundColor"))
            {
                layoutEntry.progressBarForegroundColor = entry.value("progressBarForegroundColor").toString();
            }
            if (entry.contains("progressBarBackgroundColor"))
            {
                layoutEntry.progressBarBackgroundColor = entry.value("progressBarBackgroundColor").toString();
            }
            if (entry.contains("sliderLowerLimit"))
            {
                layoutEntry.sliderLowerLimit = entry.value("sliderLowerLimit").toDouble();
            }
            if (entry.contains("sliderUpperLimit"))
            {
                layoutEntry.sliderUpperLimit = entry.value("sliderUpperLimit").toDouble();
            }
            if (entry.contains("sliderTickInterval"))
            {
                layoutEntry.sliderTickInterval = entry.value("sliderTickInterval").toDouble();
            }
            if (entry.contains("sliderShowTickMarks"))
            {
                layoutEntry.sliderShowTickMarks = entry.value("sliderShowTickMarks").toBool();
            }
            if (entry.contains("linePlotBufferSizeSamples"))
            {
                layoutEntry.linePlotBufferSizeSamples = entry.value("linePlotBufferSizeSamples").toInt();
            }
            if (entry.contains("linePlotAutoYAxis"))
            {
                layoutEntry.linePlotAutoYAxis = entry.value("linePlotAutoYAxis").toBool();
            }
            if (entry.contains("linePlotShowNumberLines"))
            {
                layoutEntry.linePlotShowNumberLines = entry.value("linePlotShowNumberLines").toBool();
            }
            if (entry.contains("linePlotShowGridLines"))
            {
                layoutEntry.linePlotShowGridLines = entry.value("linePlotShowGridLines").toBool();
            }
            if (entry.contains("linePlotYLowerLimit"))
            {
                layoutEntry.linePlotYLowerLimit = entry.value("linePlotYLowerLimit").toDouble();
            }
            if (entry.contains("linePlotYUpperLimit"))
            {
                layoutEntry.linePlotYUpperLimit = entry.value("linePlotYUpperLimit").toDouble();
            }
            if (entry.contains("textFontPointSize"))
            {
                layoutEntry.textFontPointSize = entry.value("textFontPointSize").toInt();
            }
            if (entry.contains("doubleNumericEditable"))
            {
                layoutEntry.doubleNumericEditable = entry.value("doubleNumericEditable").toBool();
            }
            if (entry.contains("boolCheckboxShowLabel"))
            {
                layoutEntry.boolCheckboxShowLabel = entry.value("boolCheckboxShowLabel").toBool();
            }
            if (entry.contains("stringChooserMode"))
            {
                layoutEntry.stringChooserMode = entry.value("stringChooserMode").toBool();
            }
            if (entry.contains("stringChooserOptions"))
            {
                const QJsonArray options = entry.value("stringChooserOptions").toArray();
                QStringList optionList;
                optionList.reserve(options.size());
                for (const QJsonValue& optionValue : options)
                {
                    optionList.push_back(optionValue.toString());
                }
                layoutEntry.stringChooserOptions = optionList;
            }
            outEntries.push_back(layoutEntry);
        }

        return true;
    }

    bool LoadLegacyXmlLayoutEntries(const QString& filePath, std::vector<WidgetLayoutEntry>& outEntries, QStringList* outIssues)
    {
        QFile file(filePath);
        if (!file.exists())
        {
            return false;
        }

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return false;
        }

        QXmlStreamReader xml(&file);
        outEntries.clear();
        QSet<QString> uniqueIssues;

        while (!xml.atEnd())
        {
            xml.readNext();
            if (!xml.isStartElement() || xml.name() != "widget")
            {
                continue;
            }

            const QXmlStreamAttributes attrs = xml.attributes();
            const QString variableKey = attrs.value("field").toString().trimmed();
            const QString legacyType = attrs.value("type").toString().trimmed();
            const QString legacyClass = attrs.value("class").toString().trimmed();
            if (variableKey.isEmpty())
            {
                xml.skipCurrentElement();
                continue;
            }

            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            std::unordered_map<std::string, QString> properties;

            while (!(xml.isEndElement() && xml.name() == "widget") && !xml.atEnd())
            {
                xml.readNext();
                if (!xml.isStartElement())
                {
                    continue;
                }

                if (xml.name() == "location")
                {
                    const QXmlStreamAttributes locAttrs = xml.attributes();
                    x = locAttrs.value("x").toInt();
                    y = locAttrs.value("y").toInt();
                    xml.skipCurrentElement();
                    continue;
                }

                if (xml.name() == "width")
                {
                    width = xml.readElementText().toInt();
                    continue;
                }

                if (xml.name() == "height")
                {
                    height = xml.readElementText().toInt();
                    continue;
                }

                if (xml.name() == "property")
                {
                    const QXmlStreamAttributes propertyAttrs = xml.attributes();
                    const QString propertyName = propertyAttrs.value("name").toString().trimmed();
                    if (!propertyName.isEmpty())
                    {
                        properties[propertyName.toStdString()] = propertyAttrs.value("value").toString().trimmed();
                    }
                    xml.skipCurrentElement();
                    continue;
                }

                xml.skipCurrentElement();
            }

            const sd::widgets::VariableType variableType = LegacyTypeToVariableType(legacyType);
            const QString widgetType = LegacyClassToWidgetType(legacyClass, variableType);

            const bool isKnownClass =
                legacyClass.endsWith("SimpleDial") ||
                legacyClass.endsWith("ProgressBar") ||
                legacyClass.endsWith("CheckBox") ||
                legacyClass.endsWith("BooleanBox") ||
                legacyClass.endsWith("TextBox") ||
                legacyClass.endsWith("FormattedField");
            if (!isKnownClass)
            {
                uniqueIssues.insert(
                    QString("Widget '%1': unsupported class '%2' mapped to '%3'.")
                        .arg(variableKey)
                        .arg(legacyClass)
                        .arg(widgetType)
                );
            }

            WidgetLayoutEntry entry;
            entry.variableKey = variableKey;
            entry.widgetType = widgetType;

            const QRect defaultGeometry = LegacyDefaultGeometry(widgetType, x, y);
            entry.geometry = QRect(
                x,
                y,
                (width > 0) ? width : defaultGeometry.width(),
                (height > 0) ? height : defaultGeometry.height()
            );

            double parsed = 0.0;
            auto setIfPresent = [&properties, &parsed](const char* name, QVariant& target)
            {
                auto it = properties.find(name);
                if (it != properties.end() && TryParseDouble(it->second, parsed))
                {
                    target = parsed;
                }
            };

            setIfPresent("Lower Limit", entry.gaugeLowerLimit);
            setIfPresent("Upper Limit", entry.gaugeUpperLimit);
            setIfPresent("Tick Interval", entry.gaugeTickInterval);

            setIfPresent("Minimum", entry.progressBarLowerLimit);
            setIfPresent("Maximum", entry.progressBarUpperLimit);

            auto parseLegacyColor = [&properties](const char* name, QVariant& target)
            {
                auto it = properties.find(name);
                if (it == properties.end())
                {
                    return;
                }

                const QStringList parts = it->second.split('.');
                if (parts.size() < 3)
                {
                    return;
                }

                bool okR = false;
                bool okG = false;
                bool okB = false;
                const int r = parts.at(0).toInt(&okR);
                const int g = parts.at(1).toInt(&okG);
                const int b = parts.at(2).toInt(&okB);
                if (!(okR && okG && okB))
                {
                    return;
                }

                target = QString("#%1%2%3")
                    .arg(r, 2, 16, QLatin1Char('0'))
                    .arg(g, 2, 16, QLatin1Char('0'))
                    .arg(b, 2, 16, QLatin1Char('0'))
                    .toUpper();
            };

            if (widgetType == "double.progress")
            {
                parseLegacyColor("Foreground", entry.progressBarForegroundColor);
                parseLegacyColor("Background", entry.progressBarBackgroundColor);
            }

            if (widgetType == "double.numeric" && legacyClass.endsWith("TextBox"))
            {
                entry.doubleNumericEditable = true;
            }

            auto textFontIt = properties.find("Font Size");
            if (textFontIt != properties.end())
            {
                bool ok = false;
                const int parsedFontSize = textFontIt->second.toInt(&ok);
                if (ok && parsedFontSize > 0)
                {
                    entry.textFontPointSize = parsedFontSize;
                }
            }

            if (widgetType == "bool.checkbox")
            {
                entry.boolCheckboxShowLabel = false;
            }

            for (const auto& [name, _] : properties)
            {
                const bool supportedProperty =
                    name == "Lower Limit" ||
                    name == "Upper Limit" ||
                    name == "Tick Interval" ||
                    name == "Minimum" ||
                    name == "Maximum" ||
                    name == "Font Size" ||
                    (widgetType == "double.progress" && (name == "Foreground" || name == "Background"));
                if (!supportedProperty)
                {
                    uniqueIssues.insert(
                        QString("Widget '%1': ignored legacy property '%2'.")
                            .arg(variableKey)
                            .arg(QString::fromStdString(name))
                    );
                }
            }

            outEntries.push_back(entry);
        }

        if (outIssues != nullptr)
        {
            *outIssues = uniqueIssues.values();
            outIssues->sort();
        }

        return !xml.hasError();
    }
}
