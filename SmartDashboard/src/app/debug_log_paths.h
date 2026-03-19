#pragma once

#include <QString>

namespace sd::app
{
    QString EnsureDebugLogDirectory();
    QString GetDebugLogPath(const QString& fileName);
    void AppendTaggedDebugLine(const QString& filePrefix, const QString& tag, const QString& line);
}
