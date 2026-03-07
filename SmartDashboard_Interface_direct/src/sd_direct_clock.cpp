#include "sd_direct_clock.h"

#include <chrono>

namespace sd::direct
{
    std::uint64_t GetSteadyNowUs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }
}
