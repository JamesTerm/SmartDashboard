#pragma once

#include <QString>

namespace sd::app
{
    QString EnsureDebugLogDirectory();
    QString GetDebugLogPath(const QString& fileName);
}
