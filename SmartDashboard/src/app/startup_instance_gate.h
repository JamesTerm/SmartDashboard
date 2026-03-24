#pragma once

#include "transport/dashboard_transport.h"

#include <QString>

namespace sd::app
{
    struct StartupOptions
    {
        bool allowMultiInstance = false;
        QString instanceTag;
    };

    StartupOptions ParseStartupOptions(int argc, char* argv[]);
    QString DetermineStartupTransportId(const QString& persistedTransportId, int persistedKindValue);
    bool TransportSupportsMultiClient(const sd::transport::TransportDescriptor* descriptor);
    bool ShouldBypassSingleInstance(const StartupOptions& options, const sd::transport::TransportDescriptor* descriptor);
}
