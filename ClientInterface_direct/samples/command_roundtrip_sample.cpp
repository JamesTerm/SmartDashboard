#include "sd_smartdashboard_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
    using namespace std::chrono_literals;

#ifdef _WIN32
    // Detect whether SmartDashboard is already running via the single-instance mutex.
    // This is a non-owning check; we close the handle immediately.
    HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\SmartDashboard.SingleInstance");
    if (existing == nullptr)
    {
        std::cout
            << "SmartDashboard does not appear to be running."
            << " Launch SmartDashboardApp first for manual roundtrip testing.\n";
    }
    else
    {
        CloseHandle(existing);
    }
#endif

    sd::direct::SmartDashboardClientConfig config;
    config.publisher.autoFlushThread = false;
    config.enableCommandSubscriber = true;

    sd::direct::SmartDashboardClient client(config);
    if (!client.Start())
    {
        std::cerr << "Failed to start SmartDashboardClient." << std::endl;
        return 1;
    }

    // Subscribe to command channel for the bool key that will be edited in the dashboard.
    std::atomic<bool> armedFromDashboard {false};
    const auto commandToken = client.SubscribeBooleanCommand(
        "Integration/Armed",
        [&armedFromDashboard](bool value)
        {
            armedFromDashboard.store(value);
        }
    );

    client.PutString("Integration/Instructions", "Use checkbox control for Integration/Armed and check it.");
    client.PutBoolean("Integration/Armed", false);
    client.FlushNow();

    // Countdown stream for visual verification while waiting for roundtrip command.
    const auto total = 10s;
    const auto start = std::chrono::steady_clock::now();
    while ((std::chrono::steady_clock::now() - start) < total)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        );

        const double t = std::clamp(
            static_cast<double>(elapsed.count()) / static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(total).count()),
            0.0,
            1.0
        );

        // Normalization mapping 0..1 -> 1..-1 so existing double widgets (range -1..1) are meaningful.
        const double countdownNormalized = 1.0 - (2.0 * t);
        client.PutDouble("Integration/Countdown", countdownNormalized);
        client.FlushNow();

        if (armedFromDashboard.load())
        {
            break;
        }

        std::this_thread::sleep_for(100ms);
    }

    const bool success = armedFromDashboard.load();
    client.Unsubscribe(commandToken);
    client.Stop();

    if (!success)
    {
        std::cerr << "Timed out waiting for dashboard command on Integration/Armed." << std::endl;
        return 2;
    }

    std::cout << "Roundtrip command received. Success." << std::endl;
    return 0;
}
