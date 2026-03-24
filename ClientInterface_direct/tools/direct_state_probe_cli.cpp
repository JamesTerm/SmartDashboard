#include "sd_smartdashboard_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    bool WaitForString(sd::direct::SmartDashboardClient& client, const std::string& key, std::string& out, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (client.TryGetString(key, out))
            {
                return true;
            }
            std::this_thread::sleep_for(20ms);
        }
        return client.TryGetString(key, out);
    }

    bool WaitForDouble(sd::direct::SmartDashboardClient& client, const std::string& key, double& out, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (client.TryGetDouble(key, out))
            {
                return true;
            }
            std::this_thread::sleep_for(20ms);
        }
        return client.TryGetDouble(key, out);
    }

    bool WaitForStringArray(sd::direct::SmartDashboardClient& client, const std::string& key, std::vector<std::string>& out, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (client.TryGetStringArray(key, out))
            {
                return true;
            }
            std::this_thread::sleep_for(20ms);
        }
        return client.TryGetStringArray(key, out);
    }

    void PrintStringArray(const std::vector<std::string>& values)
    {
        std::cout << "[";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i > 0)
            {
                std::cout << ", ";
            }
            std::cout << values[i];
        }
        std::cout << "]";
    }

    void PublishSeedWindow(
        sd::direct::SmartDashboardClient& telemetryClient,
        sd::direct::SmartDashboardClient& commandClient,
        bool seedChooser,
        const std::vector<std::string>& chooserOptions,
        std::chrono::milliseconds duration)
    {
        using namespace std::chrono_literals;

        // Ian: Keep this as a short streaming window, not a single burst. The
        // dashboard/robot pairing behaved differently when the helper exited too
        // quickly, so this intentionally mimics a briefly alive owning client.
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline)
        {
            commandClient.PutDouble("TestMove", 3.5);
            if (seedChooser)
            {
                // Ian: Chooser-mode smoke tests must not also populate numeric
                // `AutonTest`, or the UI/harness can no longer tell which path is
                // actually driving auton selection.
                commandClient.PutString("Test/Auton_Selection/AutoChooser/selected", "Just Move Forward");
            }
            else
            {
                commandClient.PutDouble("AutonTest", 1.0);
            }
            commandClient.FlushNow();

            telemetryClient.PutDouble("TestMove", 3.5);
            if (seedChooser)
            {
                telemetryClient.PutString("Test/Auton_Selection/AutoChooser/.type", "String Chooser");
                telemetryClient.PutStringArray("Test/Auton_Selection/AutoChooser/options", chooserOptions);
                telemetryClient.PutString("Test/Auton_Selection/AutoChooser/default", "Do Nothing");
                telemetryClient.PutString("Test/Auton_Selection/AutoChooser/active", "Just Move Forward");
                telemetryClient.PutString("Test/Auton_Selection/AutoChooser/selected", "Just Move Forward");
            }
            else
            {
                telemetryClient.PutDouble("AutonTest", 1.0);
            }
            telemetryClient.FlushNow();

            std::this_thread::sleep_for(50ms);
        }
    }
}

int main(int argc, char** argv)
{
    using namespace std::chrono_literals;

    bool seedChooser = false;

    const std::chrono::milliseconds timeout = (argc > 1)
        ? std::chrono::milliseconds(std::max(100, std::atoi(argv[1])))
        : 4000ms;
    bool seed = false;
    std::chrono::milliseconds seedDuration = std::chrono::milliseconds(1500);
    for (int i = 2; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--seed")
        {
            seed = true;
        }
        else if (std::string(argv[i]) == "--seed-ms" && (i + 1) < argc)
        {
            seedDuration = std::chrono::milliseconds(std::max(100, std::atoi(argv[++i])));
        }
        else if (std::string(argv[i]) == "--chooser")
        {
            seedChooser = true;
        }
    }

    sd::direct::SmartDashboardClientConfig telemetryConfig;
    telemetryConfig.publisher.autoFlushThread = true;
    telemetryConfig.enableSubscriber = true;
    telemetryConfig.enableCommandSubscriber = false;

    sd::direct::SmartDashboardClient telemetryClient(telemetryConfig);
    if (!telemetryClient.Start())
    {
        std::cerr << "failed to start direct state probe" << std::endl;
        return 1;
    }

    sd::direct::SmartDashboardClientConfig commandConfig;
    commandConfig.publisher.mappingName = L"Local\\SmartDashboard.Direct.Command.Buffer";
    commandConfig.publisher.dataEventName = L"Local\\SmartDashboard.Direct.Command.DataAvailable";
    commandConfig.publisher.heartbeatEventName = L"Local\\SmartDashboard.Direct.Command.Heartbeat";
    commandConfig.publisher.autoFlushThread = true;
    commandConfig.subscriber.mappingName = commandConfig.publisher.mappingName;
    commandConfig.subscriber.dataEventName = commandConfig.publisher.dataEventName;
    commandConfig.subscriber.heartbeatEventName = commandConfig.publisher.heartbeatEventName;
    commandConfig.enableSubscriber = true;
    commandConfig.enableCommandSubscriber = false;
    commandConfig.enableRetainedStore = false;

    sd::direct::SmartDashboardClient commandClient(commandConfig);
    if (!commandClient.Start())
    {
        telemetryClient.Stop();
        std::cerr << "failed to start command state probe" << std::endl;
        return 1;
    }

    const std::vector<std::string> chooserOptions = {
        "Do Nothing",
        "Just Move Forward",
        "Just Rotate",
        "Move Rotate Sequence",
        "Box Waypoints",
        "Smart Waypoints"
    };

    if (seed)
    {
        PublishSeedWindow(telemetryClient, commandClient, seedChooser, chooserOptions, seedDuration);
    }

    double autonTest = 0.0;
    std::string chooserType;
    std::string chooserSelected;
    std::vector<std::string> observedOptions;
    double testMove = 0.0;
    double timer = 0.0;
    double yFeet = 0.0;

    const bool gotAuton = seedChooser
        ? false
        : WaitForDouble(commandClient, "AutonTest", autonTest, timeout);

    // Ian: In chooser mode, `AutonTest` staying absent is expected. Success is
    // based on chooser selection plus `TestMove`, not on the numeric fallback.
    const bool gotType = WaitForString(telemetryClient, "Test/Auton_Selection/AutoChooser/.type", chooserType, timeout);
    const bool gotSelected = WaitForString(telemetryClient, "Test/Auton_Selection/AutoChooser/selected", chooserSelected, timeout);
    const bool gotOptions = WaitForStringArray(telemetryClient, "Test/Auton_Selection/AutoChooser/options", observedOptions, timeout);
    const bool gotMove = WaitForDouble(commandClient, "TestMove", testMove, timeout);
    const bool gotTimer = WaitForDouble(telemetryClient, "Timer", timer, timeout);
    const bool gotY = WaitForDouble(telemetryClient, "Y_ft", yFeet, timeout);

    std::cout << "probe.timeout_ms=" << timeout.count() << "\n";
    std::cout << "AutonTest=" << (gotAuton ? autonTest : -1.0) << "\n";
    std::cout << "chooser.type=" << (gotType ? chooserType : "<missing>") << "\n";
    std::cout << "chooser.selected=" << (gotSelected ? chooserSelected : "<missing>") << "\n";
    std::cout << "chooser.options=";
    if (gotOptions)
    {
        PrintStringArray(observedOptions);
    }
    else
    {
        std::cout << "<missing>";
    }
    std::cout << "\n";
    std::cout << "TestMove=" << (gotMove ? testMove : -1.0) << "\n";
    std::cout << "Timer=" << (gotTimer ? timer : -1.0) << "\n";
    std::cout << "Y_ft=" << (gotY ? yFeet : -1.0) << "\n";

    const bool chooserOk = !seedChooser
        || (gotSelected && chooserSelected == "Just Move Forward");

    const bool numericOk = seedChooser
        ? true
        : (gotAuton && autonTest >= 1.0);

    const bool ok = numericOk
        && gotMove
        && testMove > 1.0
        && chooserOk;

    commandClient.Stop();
    telemetryClient.Stop();
    return ok ? 0 : 2;
}
