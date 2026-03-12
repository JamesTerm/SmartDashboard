#include "sd_direct_publisher.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    std::wstring ToWide(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());
    }

    std::string BuildUniqueChannelBase(const char* suffix)
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return std::string("Local\\SmartDashboard.CaptureCliTest.") + suffix + "." + std::to_string(now);
    }

#ifdef _WIN32
    int RunProcess(const std::filesystem::path& exePath, const std::vector<std::string>& args)
    {
        std::string cmd = "\"" + exePath.string() + "\"";
        for (const std::string& arg : args)
        {
            cmd += " \"" + arg + "\"";
        }

        STARTUPINFOA si {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi {};

        std::vector<char> commandLine(cmd.begin(), cmd.end());
        commandLine.push_back('\0');

        const BOOL ok = CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        if (!ok)
        {
            return -1;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return static_cast<int>(exitCode);
    }
#endif

}

TEST(CaptureCliTests, CapturesDataAndWritesMetadata)
{
#ifdef _WIN32
    char modulePathBuffer[MAX_PATH] = {};
    const DWORD moduleLen = GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
    ASSERT_GT(moduleLen, 0u);
    const std::filesystem::path testExePath(modulePathBuffer);
    const std::filesystem::path captureExePath = testExePath.parent_path() / "SmartDashboardCaptureCli.exe";
    ASSERT_TRUE(std::filesystem::exists(captureExePath));
#else
    const std::filesystem::path captureExePath = std::filesystem::path("ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe");
#endif

    const std::string base = BuildUniqueChannelBase("CaptureData");
    const std::string mapping = base + ".Buffer";
    const std::string dataEvent = base + ".Data";
    const std::string heartbeat = base + ".Heartbeat";

    sd::direct::PublisherConfig pubCfg;
    pubCfg.mappingName = ToWide(mapping);
    pubCfg.dataEventName = ToWide(dataEvent);
    pubCfg.heartbeatEventName = ToWide(heartbeat);
    auto publisher = sd::direct::CreateDirectPublisher(pubCfg);
    ASSERT_NE(publisher, nullptr);
    ASSERT_TRUE(publisher->Start());

    std::atomic<bool> keepRunning {true};
    std::thread producer([&]()
    {
        int i = 0;
        while (keepRunning.load())
        {
            publisher->PublishDouble("Perf/Fps", 60.0 + static_cast<double>(i % 5));
            publisher->PublishBool("Perf/WorkerEnabled", (i % 2) == 0);
            publisher->FlushNow();
            ++i;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    const std::filesystem::path outPath = std::filesystem::temp_directory_path() / "capture_cli_test_ok.json";
    std::filesystem::create_directories(outPath.parent_path());

    const int rc = RunProcess(captureExePath, {
        "--out", outPath.string(),
        "--label", "capture test",
        "--duration-sec", "0.4",
        "--overwrite",
        "--wait-for-connected-ms", "2000",
        "--require-first-sample",
        "--mapping-name", mapping,
        "--data-event-name", dataEvent,
        "--heartbeat-event-name", heartbeat
    });

    keepRunning.store(false);
    if (producer.joinable())
    {
        producer.join();
    }
    publisher->Stop();

    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(std::filesystem::exists(outPath));

    std::ifstream in(outPath, std::ios::in | std::ios::binary);
    ASSERT_TRUE(in.good());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(text.find("\"schema_version\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"captured_update_count\":"), std::string::npos);
    EXPECT_EQ(text.find("\"captured_update_count\": 0"), std::string::npos);
    EXPECT_NE(text.find("\"Perf/Fps\""), std::string::npos);
}

TEST(CaptureCliTests, FailsOnConnectionTimeout)
{
#ifdef _WIN32
    char modulePathBuffer[MAX_PATH] = {};
    const DWORD moduleLen = GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
    ASSERT_GT(moduleLen, 0u);
    const std::filesystem::path testExePath(modulePathBuffer);
    const std::filesystem::path captureExePath = testExePath.parent_path() / "SmartDashboardCaptureCli.exe";
    ASSERT_TRUE(std::filesystem::exists(captureExePath));
#else
    const std::filesystem::path captureExePath = std::filesystem::path("ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe");
#endif

    const std::filesystem::path outPath = std::filesystem::temp_directory_path() / "capture_cli_test_timeout.json";
    std::filesystem::create_directories(outPath.parent_path());

    const int rc = RunProcess(captureExePath, {
        "--out", outPath.string(),
        "--label", "capture timeout",
        "--duration-sec", "0.2",
        "--overwrite",
        "--wait-for-connected-ms", "50",
        "--require-first-sample",
        "--mapping-name", "Local\\SmartDashboard.CaptureCliTest.NoPublisher.Buffer",
        "--data-event-name", "Local\\SmartDashboard.CaptureCliTest.NoPublisher.Data",
        "--heartbeat-event-name", "Local\\SmartDashboard.CaptureCliTest.NoPublisher.Heartbeat"
    });
    EXPECT_NE(rc, 0);
}

TEST(CaptureCliTests, AutoConnectMethodCanFindLegacyShortChannel)
{
#ifdef _WIN32
    char modulePathBuffer[MAX_PATH] = {};
    const DWORD moduleLen = GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
    ASSERT_GT(moduleLen, 0u);
    const std::filesystem::path testExePath(modulePathBuffer);
    const std::filesystem::path captureExePath = testExePath.parent_path() / "SmartDashboardCaptureCli.exe";
    ASSERT_TRUE(std::filesystem::exists(captureExePath));
#else
    const std::filesystem::path captureExePath = std::filesystem::path("ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe");
#endif

    sd::direct::PublisherConfig pubCfg;
    pubCfg.mappingName = ToWide("Local\\SmartDashboard.Buffer");
    pubCfg.dataEventName = ToWide("Local\\SmartDashboard.DataAvailable");
    pubCfg.heartbeatEventName = ToWide("Local\\SmartDashboard.Heartbeat");
    auto publisher = sd::direct::CreateDirectPublisher(pubCfg);
    ASSERT_NE(publisher, nullptr);
    ASSERT_TRUE(publisher->Start());

    std::atomic<bool> keepRunning {true};
    std::thread producer([&]()
    {
        while (keepRunning.load())
        {
            publisher->PublishDouble("Perf/LegacyShort/Fps", 88.8);
            publisher->FlushNow();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    const std::filesystem::path outPath = std::filesystem::temp_directory_path() / "capture_cli_test_auto_legacy_short.json";

    const int rc = RunProcess(captureExePath, {
        "--out", outPath.string(),
        "--label", "capture auto legacy short",
        "--duration-sec", "0.3",
        "--overwrite",
        "--connect-method", "auto",
        "--wait-for-connected-ms", "2000",
        "--require-first-sample"
    });

    keepRunning.store(false);
    if (producer.joinable())
    {
        producer.join();
    }
    publisher->Stop();

    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(std::filesystem::exists(outPath));
    std::ifstream in(outPath, std::ios::in | std::ios::binary);
    ASSERT_TRUE(in.good());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"connect_method\": \"auto\""), std::string::npos);
    EXPECT_NE(text.find("\"Perf/LegacyShort/Fps\""), std::string::npos);
}
