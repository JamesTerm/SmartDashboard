#include "app/main_window.h"

#include <QApplication>
#include <QIcon>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Single-instance guard using a named Win32 mutex.
    // If another instance already owns this mutex, we show a message and exit.
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\SmartDashboard.SingleInstance");
    if (instanceMutex == nullptr)
    {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(
            nullptr,
            L"SmartDashboard is already running. Please use only one instance at a time.",
            L"SmartDashboard",
            MB_OK | MB_ICONINFORMATION
        );
        CloseHandle(instanceMutex);
        return 0;
    }
#endif

    // Qt application bootstrap: create app object, show main window, run event loop.
    QApplication app(argc, argv);
    const QIcon appIcon(QStringLiteral(":/app/icon.ico"));
    if (!appIcon.isNull())
    {
        QApplication::setWindowIcon(appIcon);
    }

    MainWindow window;
    if (!appIcon.isNull())
    {
        window.setWindowIcon(appIcon);
    }
    window.show();

    const int exitCode = app.exec();

#ifdef _WIN32
    CloseHandle(instanceMutex);
#endif

    return exitCode;
}
