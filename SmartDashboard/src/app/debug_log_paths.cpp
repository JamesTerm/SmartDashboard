#include "app/debug_log_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace sd::app
{
    namespace
    {
        QString ResolveDebugLogDirectory()
        {
            const QString workspaceRoot = QProcessEnvironment::systemEnvironment().value("SMARTDASHBOARD_WORKSPACE_ROOT");
            if (!workspaceRoot.isEmpty())
            {
                const QString debugDirPath = QDir(workspaceRoot).filePath(".debug");
                QDir().mkpath(debugDirPath);
                return QFileInfo(debugDirPath).absoluteFilePath();
            }

            QDir dir(QCoreApplication::applicationDirPath());
            for (int i = 0; i < 8; ++i)
            {
                if (dir.exists(".git"))
                {
                    const QString debugDirPath = dir.filePath(".debug");
                    QDir().mkpath(debugDirPath);
                    return QFileInfo(debugDirPath).absoluteFilePath();
                }

                if (!dir.cdUp())
                {
                    break;
                }
            }

            const QString debugDirPath = QDir(QCoreApplication::applicationDirPath()).filePath(".debug");
            QDir().mkpath(debugDirPath);
            return QFileInfo(debugDirPath).absoluteFilePath();
        }
    }

    QString EnsureDebugLogDirectory()
    {
        return ResolveDebugLogDirectory();
    }

    QString GetDebugLogPath(const QString& fileName)
    {
        return QDir(EnsureDebugLogDirectory()).filePath(fileName);
    }
}
