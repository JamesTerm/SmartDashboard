#include "app/debug_log_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QProcessEnvironment>
#include <QTextStream>

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

    void AppendTaggedDebugLine(const QString& filePrefix, const QString& tag, const QString& line)
    {
        if (tag.trimmed().isEmpty())
        {
            return;
        }

        // Ian: These tiny tagged files are for cross-process debugging, not
        // user-facing logs. We want a dead-simple append-only path that still
        // works even before the rest of the app UI/transport stack is fully up.
        QFile file(GetDebugLogPath(QString("%1_%2.log").arg(filePrefix, tag)));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            return;
        }

        QTextStream stream(&file);
        stream << line << '\n';
    }
}
