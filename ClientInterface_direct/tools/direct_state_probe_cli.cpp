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
}

int main(int argc, char** argv)
{
    using namespace std::chrono_literals;

    const std::chrono::milliseconds timeout = (argc > 1)
        ? std::chrono::milliseconds(std::max(100, std::atoi(argv[1])))
        : 4000ms;

    sd::direct::SmartDashboardClientConfig config;
    config.publisher.autoFlushThread = true;
    config.enableSubscriber = true;
    config.enableCommandSubscriber = true;

    sd::direct::SmartDashboardClient client(config);
    if (!client.Start())
    {
        std::cerr << "failed to start direct state probe" << std::endl;
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

    client.PutString("Test/AutoChooser/.type", "String Chooser");
    client.PutStringArray("Test/AutoChooser/options", chooserOptions);
    client.PutString("Test/AutoChooser/default", "Do Nothing");
    client.PutString("Test/AutoChooser/active", "Just Move Forward");
    client.PutString("Test/AutoChooser/selected", "Just Move Forward");
    client.PutDouble("TestMove", 3.5);
    client.FlushNow();

    std::string chooserType;
    std::string chooserSelected;
    std::vector<std::string> observedOptions;
    double testMove = 0.0;

    const bool gotType = WaitForString(client, "Test/AutoChooser/.type", chooserType, timeout);
    const bool gotSelected = WaitForString(client, "Test/AutoChooser/selected", chooserSelected, timeout);
    const bool gotOptions = WaitForStringArray(client, "Test/AutoChooser/options", observedOptions, timeout);
    const bool gotMove = WaitForDouble(client, "TestMove", testMove, timeout);

    std::cout << "probe.timeout_ms=" << timeout.count() << "\n";
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

    const bool ok = gotType
        && chooserType == "String Chooser"
        && gotSelected
        && chooserSelected == "Just Move Forward"
        && gotOptions
        && observedOptions == chooserOptions
        && gotMove
        && testMove > 1.0;

    client.Stop();
    return ok ? 0 : 2;
}
