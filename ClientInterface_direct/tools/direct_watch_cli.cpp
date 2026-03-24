#include "sd_smartdashboard_client.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    std::string JoinStrings(const std::vector<std::string>& values)
    {
        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i > 0)
            {
                out << ", ";
            }
            out << values[i];
        }
        out << "]";
        return out.str();
    }

    void LogLine(std::ofstream& file, std::mutex& mutex, const std::string& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << line << std::endl;
        file << line << '\n';
        file.flush();
    }
}

int main(int argc, char** argv)
{
    using Clock = std::chrono::steady_clock;

    const int runMs = (argc > 1) ? std::max(1000, std::atoi(argv[1])) : 15000;
    const std::string logPath = (argc > 2) ? argv[2] : "direct_watch_log.txt";

    std::ofstream logFile(logPath, std::ios::out | std::ios::trunc);
    if (!logFile.is_open())
    {
        std::cerr << "failed to open log file: " << logPath << std::endl;
        return 1;
    }

    std::mutex logMutex;

    sd::direct::SmartDashboardClientConfig config;
    config.publisher.autoFlushThread = true;
    config.enableSubscriber = true;
    config.enableCommandSubscriber = false;

    sd::direct::SmartDashboardClient client(config);
    if (!client.Start())
    {
        std::cerr << "failed to start direct watcher" << std::endl;
        return 2;
    }

    const auto start = Clock::now();
    auto timestampMs = [&]() -> long long
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    };

    const auto logPrefix = [&]() -> std::string
    {
        std::ostringstream out;
        out << "t=" << std::setw(5) << timestampMs() << "ms ";
        return out.str();
    };

    std::vector<sd::direct::SubscriptionToken> tokens;
    tokens.push_back(client.SubscribeString("Test/Auton_Selection/AutoChooser/.type", [&](const std::string& value)
    {
        LogLine(logFile, logMutex, logPrefix() + "chooser.type=" + value);
    }));
    tokens.push_back(client.SubscribeString("Test/Auton_Selection/AutoChooser/selected", [&](const std::string& value)
    {
        LogLine(logFile, logMutex, logPrefix() + "chooser.selected=" + value);
    }));
    tokens.push_back(client.SubscribeString("Test/Auton_Selection/AutoChooser/active", [&](const std::string& value)
    {
        LogLine(logFile, logMutex, logPrefix() + "chooser.active=" + value);
    }));
    tokens.push_back(client.SubscribeStringArray("Test/Auton_Selection/AutoChooser/options", [&](const std::vector<std::string>& value)
    {
        LogLine(logFile, logMutex, logPrefix() + "chooser.options=" + JoinStrings(value));
    }));
    tokens.push_back(client.SubscribeDouble("TestMove", [&](double value)
    {
        std::ostringstream out;
        out << logPrefix() << "TestMove=" << value;
        LogLine(logFile, logMutex, out.str());
    }));
    tokens.push_back(client.SubscribeDouble("Timer", [&](double value)
    {
        std::ostringstream out;
        out << logPrefix() << "Timer=" << value;
        LogLine(logFile, logMutex, out.str());
    }));
    tokens.push_back(client.SubscribeDouble("Y_ft", [&](double value)
    {
        std::ostringstream out;
        out << logPrefix() << "Y_ft=" << value;
        LogLine(logFile, logMutex, out.str());
    }));

    LogLine(logFile, logMutex, "watch.start ms=" + std::to_string(runMs));
    std::this_thread::sleep_for(std::chrono::milliseconds(runMs));
    LogLine(logFile, logMutex, "watch.end");

    for (const auto& token : tokens)
    {
        client.Unsubscribe(token);
    }
    client.Stop();
    return 0;
}
