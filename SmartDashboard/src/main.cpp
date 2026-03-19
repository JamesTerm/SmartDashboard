#include "app/debug_log_paths.h"
#include "app/main_window.h"
#include "app/startup_instance_gate.h"

#include "transport/dashboard_transport.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    const sd::app::StartupOptions startupOptions = sd::app::ParseStartupOptions(argc, argv);
    const QString startupTag = startupOptions.instanceTag.trimmed();

    if (!startupTag.isEmpty())
    {
        sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, "main_enter");
    }

    if (!startupTag.isEmpty())
    {
        qputenv("SMARTDASHBOARD_INSTANCE_TAG", startupTag.toUtf8());
        sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, "instance_tag_propagated");
    }

    // Ian: The startup transport registry needs a live Qt application object
    // before plugin discovery can reliably resolve the app/plugin directory.
    // We originally checked the single-instance bypass before QApplication
    // existed, which made Native Link look non-multi-client and blocked the
    // second dashboard process even though the plugin advertised support.
    QApplication app(argc, argv);
    if (!startupTag.isEmpty())
    {
        sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, "qapplication_created");
    }

#ifdef _WIN32
    QSettings startupSettings("SmartDashboard", "SmartDashboardApp");
    const int persistedKindValue = startupSettings.value("connection/transportKind", static_cast<int>(sd::transport::TransportKind::Direct)).toInt();
    const QString startupTransportId = sd::app::DetermineStartupTransportId(
        startupSettings.value("connection/transportId").toString(),
        persistedKindValue
    );
    const sd::transport::DashboardTransportRegistry startupRegistry;
    const sd::transport::TransportDescriptor* startupDescriptor = startupRegistry.FindTransport(startupTransportId);
    if (!startupTag.isEmpty())
    {
        sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, QString("startup_transport=%1").arg(startupTransportId));
        sd::app::AppendTaggedDebugLine(
            "native_link_startup",
            startupTag,
            QString("startup_supports_multi=%1").arg(sd::app::TransportSupportsMultiClient(startupDescriptor) ? "1" : "0")
        );
    }

    // Single-instance guard using a named Win32 mutex.
    // If another instance already owns this mutex, we show a message and exit.
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\SmartDashboard.SingleInstance");
    if (instanceMutex == nullptr)
    {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (sd::app::ShouldBypassSingleInstance(startupOptions, startupDescriptor))
        {
            // Ian: We still keep the global mutex for the first instance. The
            // bypass path is only for explicitly opted-in multi-client
            // transports, so release the duplicate-instance handle and let the
            // second process continue normally.
            if (!startupTag.isEmpty())
            {
                sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, "single_instance_bypass=1");
            }
            CloseHandle(instanceMutex);
            instanceMutex = nullptr;
        }
        else
        {
            if (!startupTag.isEmpty())
            {
                sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, "single_instance_bypass=0");
            }
            MessageBoxW(
                nullptr,
                L"SmartDashboard is already running. Please use only one instance at a time.",
                L"SmartDashboard",
                MB_OK | MB_ICONINFORMATION
            );
            CloseHandle(instanceMutex);
            return 0;
        }
    }
#endif

    // Qt application bootstrap: create app object, show main window, run event loop.
    const QIcon appIcon(QStringLiteral(":/app/icon.ico"));
    if (!appIcon.isNull())
    {
        QApplication::setWindowIcon(appIcon);
    }

    MainWindow window;
    if (!startupTag.isEmpty())
    {
        sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, "main_window_created");
    }
    if (!appIcon.isNull())
    {
        window.setWindowIcon(appIcon);
    }
    window.show();
    if (!startupTag.isEmpty())
    {
        sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, "window_shown");
    }

    const int exitCode = app.exec();
    if (!startupTag.isEmpty())
    {
        sd::app::AppendTaggedDebugLine("native_link_startup", startupTag, QString("app_exit=%1").arg(exitCode));
    }

#ifdef _WIN32
    if (instanceMutex != nullptr)
    {
        CloseHandle(instanceMutex);
    }
#endif

    return exitCode;
}
