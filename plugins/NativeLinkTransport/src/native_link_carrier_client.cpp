#include "native_link_carrier_client.h"

#include "native_link_ipc_client.h"
#include "native_link_tcp_client.h"

#include <cctype>
#include <string>

namespace sd::nativelink
{
    namespace
    {
        std::string ToLowerCopy(const std::string& text)
        {
            std::string normalized(text.begin(), text.end());
            for (char& ch : normalized)
            {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return normalized;
        }

    }

    const char* ToString(NativeLinkCarrierKind kind)
    {
        switch (kind)
        {
            case NativeLinkCarrierKind::SharedMemory:
                return "shm";
            case NativeLinkCarrierKind::Tcp:
                return "tcp";
        }

        return "unknown";
    }

    bool TryParseCarrierKind(const std::string& text, NativeLinkCarrierKind& outKind)
    {
        const std::string normalized = ToLowerCopy(text);
        if (normalized == "shm" || normalized == "shared_memory" || normalized == "shared-memory")
        {
            outKind = NativeLinkCarrierKind::SharedMemory;
            return true;
        }
        if (normalized == "tcp")
        {
            outKind = NativeLinkCarrierKind::Tcp;
            return true;
        }

        return false;
    }

    std::unique_ptr<NativeLinkCarrierClient> CreateNativeLinkCarrierClient(NativeLinkCarrierKind kind)
    {
        switch (kind)
        {
            case NativeLinkCarrierKind::SharedMemory:
                return std::make_unique<NativeLinkIpcClient>();
            case NativeLinkCarrierKind::Tcp:
                // Ian: Carrier selection now decides only the transport medium.
                // The snapshot/session/lease contract is shared above this
                // factory, so adding TCP here must not fork the semantics.
                return std::make_unique<NativeLinkTcpClient>();
        }

        return nullptr;
    }
}
