#include "app/startup_instance_gate.h"

namespace sd::app
{
    StartupOptions ParseStartupOptions(int argc, char* argv[])
    {
        StartupOptions options;

        for (int i = 1; i < argc; ++i)
        {
            if (argv[i] == nullptr)
            {
                continue;
            }

            const QString arg = QString::fromUtf8(argv[i]).trimmed();
            if (arg == "--allow-multi-instance")
            {
                // Ian: Multi-instance is intentionally not a general escape
                // hatch. It only means "consider bypassing the singleton lock
                // if the selected transport explicitly supports multi-client."
                options.allowMultiInstance = true;
                continue;
            }

            if (arg == "--instance-tag" && (i + 1) < argc && argv[i + 1] != nullptr)
            {
                options.instanceTag = QString::fromUtf8(argv[i + 1]).trimmed();
                ++i;
            }
        }

        return options;
    }

    QString DetermineStartupTransportId(const QString& persistedTransportId, int persistedKindValue)
    {
        const QString trimmed = persistedTransportId.trimmed();
        if (!trimmed.isEmpty())
        {
            return trimmed;
        }

        if (persistedKindValue == static_cast<int>(sd::transport::TransportKind::Replay))
        {
            return QStringLiteral("replay");
        }

        if (persistedKindValue == static_cast<int>(sd::transport::TransportKind::Plugin))
        {
            return QStringLiteral("legacy-nt");
        }

        return QStringLiteral("direct");
    }

    bool TransportSupportsMultiClient(const sd::transport::TransportDescriptor* descriptor)
    {
        if (descriptor == nullptr)
        {
            return false;
        }

        return descriptor->GetBoolProperty(QString::fromUtf8(sd::transport::kTransportPropertySupportsMultiClient), false);
    }

    bool ShouldBypassSingleInstance(const StartupOptions& options, const sd::transport::TransportDescriptor* descriptor)
    {
        return options.allowMultiInstance && TransportSupportsMultiClient(descriptor);
    }
}
