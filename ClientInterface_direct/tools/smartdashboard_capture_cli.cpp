#include "sd_direct_subscriber.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    struct CliOptions
    {
        std::string outPath;
        std::string label;
        double durationSec = -1.0;
        int startDelayMs = 0;
        int sampleMs = 0;
        bool overwrite = false;
        bool append = false;
        bool quiet = false;
        bool verbose = false;
        bool listSignals = false;
        std::string signalsCsv;
        std::string stopFilePath;
        std::string runId;
        std::string mappingName;
        std::string dataEventName;
        std::string heartbeatEventName;
        int waitForConnectedMs = 2000;
        bool requireFirstSample = false;
        std::string connectMethod = "direct";
        std::vector<std::pair<std::string, std::string>> tags;
        std::vector<std::string> rawArgs;
    };

    using SampleValue = std::variant<bool, double, std::string>;

    struct SamplePoint
    {
        std::uint64_t tUs = 0;
        SampleValue value;
    };

    struct SignalSeries
    {
        std::string key;
        sd::direct::ValueType type = sd::direct::ValueType::String;
        std::vector<SamplePoint> samples;
        std::map<std::uint64_t, SamplePoint> bucketedSamples;
    };

    struct CaptureState
    {
        mutable std::mutex mutex;
        std::map<std::string, SignalSeries> seriesByKey;
        std::set<std::string> discoveredSignals;
        std::set<std::string> filterSignals;
        bool useFilter = false;
        int sampleMs = 0;
        Clock::time_point captureStartSteady;
        std::atomic<std::uint64_t> totalUpdates {0};
    };

    std::atomic<bool> g_stopRequested {false};

    std::string EscapeJson(const std::string& value)
    {
        std::ostringstream out;
        for (const char ch : value)
        {
            switch (ch)
            {
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20)
                    {
                        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(static_cast<unsigned char>(ch))
                            << std::dec << std::setfill(' ');
                    }
                    else
                    {
                        out << ch;
                    }
                    break;
            }
        }
        return out.str();
    }

    std::string ToIso8601Utc(std::chrono::system_clock::time_point tp)
    {
        const std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tmUtc {};
#ifdef _WIN32
        gmtime_s(&tmUtc, &t);
#else
        gmtime_r(&t, &tmUtc);
#endif
        std::ostringstream out;
        out << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }

    std::string ValueTypeToString(sd::direct::ValueType type)
    {
        switch (type)
        {
            case sd::direct::ValueType::Bool:
                return "bool";
            case sd::direct::ValueType::Double:
                return "double";
            case sd::direct::ValueType::String:
            default:
                return "string";
        }
    }

    std::string ConnectionStateToString(sd::direct::ConnectionState state)
    {
        switch (state)
        {
            case sd::direct::ConnectionState::Connected:
                return "Connected";
            case sd::direct::ConnectionState::Connecting:
                return "Connecting";
            case sd::direct::ConnectionState::Stale:
                return "Stale";
            case sd::direct::ConnectionState::Disconnected:
            default:
                return "Disconnected";
        }
    }

    std::wstring ToWideLossy(std::string_view text)
    {
        return std::wstring(text.begin(), text.end());
    }

    bool IsConnectMethodValid(const std::string& method)
    {
        return method == "direct" || method == "auto";
    }

    struct ChannelCandidate
    {
        std::string description;
        sd::direct::SubscriberConfig config;
    };

    std::vector<ChannelCandidate> BuildChannelCandidates(const CliOptions& options)
    {
        std::vector<ChannelCandidate> candidates;

        auto buildConfigFromOptions = [&]() -> sd::direct::SubscriberConfig
        {
            sd::direct::SubscriberConfig cfg;
            if (!options.mappingName.empty())
            {
                cfg.mappingName = ToWideLossy(options.mappingName);
            }
            if (!options.dataEventName.empty())
            {
                cfg.dataEventName = ToWideLossy(options.dataEventName);
            }
            if (!options.heartbeatEventName.empty())
            {
                cfg.heartbeatEventName = ToWideLossy(options.heartbeatEventName);
            }
            return cfg;
        };

        if (options.connectMethod == "direct")
        {
            ChannelCandidate candidate;
            candidate.description = "direct";
            candidate.config = buildConfigFromOptions();
            candidates.push_back(std::move(candidate));
            return candidates;
        }

        // auto mode: try explicit override first (if present), then known defaults.
        if (!options.mappingName.empty() || !options.dataEventName.empty() || !options.heartbeatEventName.empty())
        {
            ChannelCandidate explicitCandidate;
            explicitCandidate.description = "explicit-override";
            explicitCandidate.config = buildConfigFromOptions();
            candidates.push_back(std::move(explicitCandidate));
        }

        {
            ChannelCandidate defaultCandidate;
            defaultCandidate.description = "smartdashboard-default";
            defaultCandidate.config = sd::direct::SubscriberConfig {};
            candidates.push_back(std::move(defaultCandidate));
        }

        {
            ChannelCandidate legacyCandidate;
            legacyCandidate.description = "legacy-short";
            legacyCandidate.config.mappingName = L"Local\\SmartDashboard.Buffer";
            legacyCandidate.config.dataEventName = L"Local\\SmartDashboard.DataAvailable";
            legacyCandidate.config.heartbeatEventName = L"Local\\SmartDashboard.Heartbeat";
            candidates.push_back(std::move(legacyCandidate));
        }

        return candidates;
    }

    std::string GenerateRunId()
    {
        const auto now = std::chrono::system_clock::now();
        const auto sinceEpoch = now.time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count();
#ifdef _WIN32
        const unsigned long pid = GetCurrentProcessId();
#else
        const unsigned long pid = 0;
#endif
        std::ostringstream out;
        out << "run-" << ms << "-" << pid;
        return out.str();
    }

    void PrintUsage(std::ostream& os)
    {
        os << "SmartDashboardCaptureCli.exe\n"
           << "\nRequired arguments:\n"
           << "  --out <path>\n"
           << "  --label <string>\n"
           << "  --duration-sec <number>\n"
           << "\nPreferred/optional arguments:\n"
           << "  --start-delay-ms <number>   (default 0)\n"
           << "  --sample-ms <number>        (default 0 = raw updates)\n"
           << "  --overwrite\n"
           << "  --append                    (JSONL append mode)\n"
           << "  --quiet\n"
           << "  --verbose\n"
           << "  --tag <k=v>                (repeatable)\n"
           << "  --mapping-name <name>      (direct channel mapping name override)\n"
           << "  --data-event-name <name>   (direct channel data event override)\n"
           << "  --heartbeat-event-name <name> (direct channel heartbeat event override)\n"
           << "  --wait-for-connected-ms <number> (default 2000)\n"
           << "  --require-first-sample     (fail if no samples captured)\n"
           << "  --connect-method <direct|auto> (default direct)\n"
           << "  --list-signals\n"
           << "  --signals <csv>\n"
           << "  --stop-file <path>\n"
           << "  --run-id <string>\n"
           << "\nExamples:\n"
           << "  SmartDashboardCaptureCli.exe --out runs/example_name_1_worker_on_prefetch_off.json --label \"example_name_1 worker ON prefetch OFF\" --duration-sec 45 --tag app=example_name_1 --tag worker=on --tag prefetch=off\n"
           << "  SmartDashboardCaptureCli.exe --out runs/example_name_1_worker_off_prefetch_off.json --label \"example_name_1 worker OFF prefetch OFF\" --duration-sec 45 --tag app=example_name_1 --tag worker=off --tag prefetch=off\n"
           << "  SmartDashboardCaptureCli.exe --out runs/example_name_2_worker_off_prefetch_off.json --label \"example_name_2 worker OFF prefetch OFF\" --duration-sec 45 --tag app=example_name_2 --tag worker=off --tag prefetch=off\n";
    }

    bool ParseCsvSignals(const std::string& csv, std::set<std::string>& out)
    {
        std::stringstream ss(csv);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](unsigned char c) { return !std::isspace(c); }));
            token.erase(std::find_if(token.rbegin(), token.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), token.end());
            if (!token.empty())
            {
                out.insert(token);
            }
        }
        return true;
    }

    bool ParseArgs(int argc, char** argv, CliOptions& options, std::string& error)
    {
        auto parseDouble = [&](const std::string& text, const std::string& name, double& out) -> bool
        {
            try
            {
                std::size_t consumed = 0;
                const double value = std::stod(text, &consumed);
                if (consumed != text.size())
                {
                    error = "Invalid numeric value for " + name + ": " + text;
                    return false;
                }
                out = value;
                return true;
            }
            catch (const std::exception&)
            {
                error = "Invalid numeric value for " + name + ": " + text;
                return false;
            }
        };

        auto parseInt = [&](const std::string& text, const std::string& name, int& out) -> bool
        {
            try
            {
                std::size_t consumed = 0;
                const int value = std::stoi(text, &consumed);
                if (consumed != text.size())
                {
                    error = "Invalid numeric value for " + name + ": " + text;
                    return false;
                }
                out = value;
                return true;
            }
            catch (const std::exception&)
            {
                error = "Invalid numeric value for " + name + ": " + text;
                return false;
            }
        };

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            options.rawArgs.push_back(arg);

            auto needValue = [&](const std::string& name) -> std::optional<std::string>
            {
                if (i + 1 >= argc)
                {
                    error = "Missing value for " + name;
                    return std::nullopt;
                }
                ++i;
                options.rawArgs.push_back(argv[i]);
                return std::string(argv[i]);
            };

            if (arg == "--out")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.outPath = *v;
            }
            else if (arg == "--label")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.label = *v;
            }
            else if (arg == "--duration-sec")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                if (!parseDouble(*v, arg, options.durationSec))
                {
                    return false;
                }
            }
            else if (arg == "--start-delay-ms")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                if (!parseInt(*v, arg, options.startDelayMs))
                {
                    return false;
                }
            }
            else if (arg == "--sample-ms")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                if (!parseInt(*v, arg, options.sampleMs))
                {
                    return false;
                }
            }
            else if (arg == "--overwrite")
            {
                options.overwrite = true;
            }
            else if (arg == "--append")
            {
                options.append = true;
            }
            else if (arg == "--quiet")
            {
                options.quiet = true;
            }
            else if (arg == "--verbose")
            {
                options.verbose = true;
            }
            else if (arg == "--tag")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }

                const auto pos = v->find('=');
                if (pos == std::string::npos || pos == 0 || pos == v->size() - 1)
                {
                    error = "--tag expects k=v";
                    return false;
                }
                options.tags.push_back({v->substr(0, pos), v->substr(pos + 1)});
            }
            else if (arg == "--mapping-name")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.mappingName = *v;
            }
            else if (arg == "--data-event-name")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.dataEventName = *v;
            }
            else if (arg == "--heartbeat-event-name")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.heartbeatEventName = *v;
            }
            else if (arg == "--wait-for-connected-ms")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                if (!parseInt(*v, arg, options.waitForConnectedMs))
                {
                    return false;
                }
            }
            else if (arg == "--require-first-sample")
            {
                options.requireFirstSample = true;
            }
            else if (arg == "--connect-method")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.connectMethod = *v;
            }
            else if (arg == "--list-signals")
            {
                options.listSignals = true;
            }
            else if (arg == "--signals")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.signalsCsv = *v;
            }
            else if (arg == "--stop-file")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.stopFilePath = *v;
            }
            else if (arg == "--run-id")
            {
                auto v = needValue(arg);
                if (!v)
                {
                    return false;
                }
                options.runId = *v;
            }
            else if (arg == "--help" || arg == "-h")
            {
                PrintUsage(std::cout);
                std::exit(0);
            }
            else
            {
                error = "Unknown argument: " + arg;
                return false;
            }
        }

        if (options.overwrite && options.append)
        {
            error = "Cannot use --overwrite and --append together";
            return false;
        }

        if (options.durationSec < 0.0)
        {
            options.durationSec = options.listSignals ? 1.0 : -1.0;
        }

        if (options.sampleMs < 0)
        {
            error = "--sample-ms must be >= 0";
            return false;
        }

        if (options.startDelayMs < 0)
        {
            error = "--start-delay-ms must be >= 0";
            return false;
        }

        if (options.waitForConnectedMs < 0)
        {
            error = "--wait-for-connected-ms must be >= 0";
            return false;
        }

        if (!IsConnectMethodValid(options.connectMethod))
        {
            error = "--connect-method must be one of: direct, auto";
            return false;
        }

        if (!options.listSignals)
        {
            if (options.outPath.empty() || options.label.empty() || options.durationSec <= 0.0)
            {
                error = "Missing required args: --out, --label, --duration-sec";
                return false;
            }
        }

        if (options.runId.empty())
        {
            options.runId = GenerateRunId();
        }

        if (options.quiet && options.verbose)
        {
            options.verbose = false;
        }

        return true;
    }

    bool IsCaptureEnabledForSignal(const CaptureState& state, const std::string& key)
    {
        if (!state.useFilter)
        {
            return true;
        }
        return state.filterSignals.find(key) != state.filterSignals.end();
    }

    void AddUpdate(CaptureState& state, const sd::direct::VariableUpdate& update, const Clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.discoveredSignals.insert(update.key);

        if (!IsCaptureEnabledForSignal(state, update.key))
        {
            return;
        }

        SignalSeries& series = state.seriesByKey[update.key];
        series.key = update.key;
        series.type = update.type;

        SamplePoint point;
        point.tUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - state.captureStartSteady).count()
        );

        switch (update.type)
        {
            case sd::direct::ValueType::Bool:
                point.value = update.value.boolValue;
                break;
            case sd::direct::ValueType::Double:
                point.value = update.value.doubleValue;
                break;
            case sd::direct::ValueType::String:
            default:
                point.value = update.value.stringValue;
                break;
        }

        if (state.sampleMs > 0)
        {
            const std::uint64_t bucketUs = static_cast<std::uint64_t>(state.sampleMs) * 1000ULL;
            const std::uint64_t bucket = bucketUs == 0 ? 0 : point.tUs / bucketUs;
            series.bucketedSamples[bucket] = std::move(point);
        }
        else
        {
            series.samples.push_back(std::move(point));
        }

        state.totalUpdates.fetch_add(1, std::memory_order_relaxed);
    }

    std::string RenderValueJson(const SampleValue& value)
    {
        if (std::holds_alternative<bool>(value))
        {
            return std::get<bool>(value) ? "true" : "false";
        }
        if (std::holds_alternative<double>(value))
        {
            std::ostringstream out;
            out << std::setprecision(17) << std::get<double>(value);
            return out.str();
        }
        return "\"" + EscapeJson(std::get<std::string>(value)) + "\"";
    }

    std::string BuildRunJson(
        const CliOptions& options,
        const std::chrono::system_clock::time_point startWall,
        const std::chrono::system_clock::time_point endWall,
        const CaptureState& state,
        std::uint64_t capturedUpdateCount)
    {
        std::map<std::string, SignalSeries> snapshot;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            snapshot = state.seriesByKey;
        }

        for (auto& [_, series] : snapshot)
        {
            if (state.sampleMs > 0)
            {
                series.samples.clear();
                series.samples.reserve(series.bucketedSamples.size());
                for (const auto& [__, point] : series.bucketedSamples)
                {
                    series.samples.push_back(point);
                }
            }
        }

        std::ostringstream json;
        json << "{\n";
        json << "  \"schema_version\": 1,\n";
        json << "  \"metadata\": {\n";
        json << "    \"label\": \"" << EscapeJson(options.label) << "\",\n";
        json << "    \"run_id\": \"" << EscapeJson(options.runId) << "\",\n";
        json << "    \"start_time_utc\": \"" << EscapeJson(ToIso8601Utc(startWall)) << "\",\n";
        json << "    \"end_time_utc\": \"" << EscapeJson(ToIso8601Utc(endWall)) << "\",\n";
        json << "    \"duration_sec\": " << std::setprecision(6) << options.durationSec << ",\n";
        json << "    \"captured_update_count\": " << capturedUpdateCount << ",\n";
        json << "    \"args\": {\n";
        json << "      \"start_delay_ms\": " << options.startDelayMs << ",\n";
        json << "      \"sample_ms\": " << options.sampleMs << ",\n";
        json << "      \"overwrite\": " << (options.overwrite ? "true" : "false") << ",\n";
        json << "      \"append\": " << (options.append ? "true" : "false") << ",\n";
        json << "      \"mapping_name\": \"" << EscapeJson(options.mappingName) << "\",\n";
        json << "      \"data_event_name\": \"" << EscapeJson(options.dataEventName) << "\",\n";
        json << "      \"heartbeat_event_name\": \"" << EscapeJson(options.heartbeatEventName) << "\",\n";
        json << "      \"wait_for_connected_ms\": " << options.waitForConnectedMs << ",\n";
        json << "      \"require_first_sample\": " << (options.requireFirstSample ? "true" : "false") << ",\n";
        json << "      \"connect_method\": \"" << EscapeJson(options.connectMethod) << "\",\n";
        json << "      \"signals\": \"" << EscapeJson(options.signalsCsv) << "\",\n";
        json << "      \"stop_file\": \"" << EscapeJson(options.stopFilePath) << "\"\n";
        json << "    },\n";
        json << "    \"tags\": {";
        for (std::size_t i = 0; i < options.tags.size(); ++i)
        {
            if (i > 0)
            {
                json << ", ";
            }
            json << "\"" << EscapeJson(options.tags[i].first) << "\": \""
                 << EscapeJson(options.tags[i].second) << "\"";
        }
        json << "}\n";
        json << "  },\n";
        json << "  \"signals\": [\n";

        bool firstSignal = true;
        for (const auto& [_, series] : snapshot)
        {
            if (!firstSignal)
            {
                json << ",\n";
            }
            firstSignal = false;

            json << "    {\n";
            json << "      \"key\": \"" << EscapeJson(series.key) << "\",\n";
            json << "      \"type\": \"" << ValueTypeToString(series.type) << "\",\n";
            json << "      \"sample_count\": " << series.samples.size() << ",\n";
            json << "      \"samples\": [";
            for (std::size_t i = 0; i < series.samples.size(); ++i)
            {
                if (i > 0)
                {
                    json << ", ";
                }
                const SamplePoint& point = series.samples[i];
                json << "{\"t_us\": " << point.tUs << ", \"value\": " << RenderValueJson(point.value) << "}";
            }
            json << "]\n";
            json << "    }";
        }

        json << "\n  ]\n";
        json << "}\n";
        return json.str();
    }

    bool WriteOutputFile(const CliOptions& options, const std::string& jsonText, std::string& error)
    {
        namespace fs = std::filesystem;

        if (options.outPath.empty())
        {
            error = "Output path is empty";
            return false;
        }

        const fs::path outPath(options.outPath);
        const fs::path parent = outPath.parent_path();
        if (!parent.empty())
        {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (ec)
            {
                error = "Failed to create output directory: " + ec.message();
                return false;
            }
        }

        if (options.append)
        {
            std::ofstream out(options.outPath, std::ios::out | std::ios::app | std::ios::binary);
            if (!out)
            {
                error = "Failed to open output file for append";
                return false;
            }
            out << jsonText;
            out.flush();
            return static_cast<bool>(out);
        }

        if (fs::exists(outPath) && !options.overwrite)
        {
            error = "Output file exists. Use --overwrite or --append.";
            return false;
        }

        const fs::path tempPath = outPath.string() + ".tmp";
        {
            std::ofstream out(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out)
            {
                error = "Failed to open temp output file";
                return false;
            }

            out << jsonText;
            out.flush();
            if (!out)
            {
                error = "Failed while writing temp output file";
                return false;
            }
        }

        std::error_code ec;
        fs::rename(tempPath, outPath, ec);
        if (ec)
        {
            fs::remove(outPath, ec);
            ec.clear();
            fs::rename(tempPath, outPath, ec);
            if (ec)
            {
                error = "Failed to move temp file into place: " + ec.message();
                return false;
            }
        }

        return true;
    }

#ifdef _WIN32
    BOOL WINAPI CtrlHandler(DWORD signal)
    {
        if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
        {
            g_stopRequested.store(true, std::memory_order_relaxed);
            return TRUE;
        }
        return FALSE;
    }
#endif

    bool StopFileExists(const std::string& stopFilePath)
    {
        if (stopFilePath.empty())
        {
            return false;
        }
        std::error_code ec;
        return std::filesystem::exists(stopFilePath, ec) && !ec;
    }
}

int main(int argc, char** argv)
{
    CliOptions options;
    std::string parseError;
    if (!ParseArgs(argc, argv, options, parseError))
    {
        std::cerr << "Argument error: " << parseError << "\n\n";
        PrintUsage(std::cerr);
        return 2;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
#endif

    CaptureState state;
    state.captureStartSteady = Clock::now();
    state.sampleMs = options.sampleMs;
    if (!options.signalsCsv.empty())
    {
        ParseCsvSignals(options.signalsCsv, state.filterSignals);
        state.useFilter = !state.filterSignals.empty();
    }

    std::atomic<sd::direct::ConnectionState> connectionState {sd::direct::ConnectionState::Disconnected};
    std::atomic<bool> connectedObserved {false};
    std::atomic<bool> sampleObserved {false};
    std::unique_ptr<sd::direct::IDirectSubscriber> subscriber;

    const std::vector<ChannelCandidate> candidates = BuildChannelCandidates(options);
    if (candidates.empty())
    {
        std::cerr << "No connection candidates available\n";
        return 3;
    }

    const int perCandidateWaitMs = (std::max)(250, options.waitForConnectedMs / static_cast<int>(candidates.size()));
    bool subscriberStarted = false;
    std::string selectedCandidate = "";
    for (const ChannelCandidate& candidate : candidates)
    {
        subscriber = sd::direct::CreateDirectSubscriber(candidate.config);
        if (!subscriber)
        {
            continue;
        }

        const bool started = subscriber->Start(
            [&state, &sampleObserved](const sd::direct::VariableUpdate& update)
            {
                sampleObserved.store(true, std::memory_order_relaxed);
                AddUpdate(state, update, Clock::now());
            },
            [&connectionState, &connectedObserved](sd::direct::ConnectionState next)
            {
                connectionState.store(next, std::memory_order_relaxed);
                if (next == sd::direct::ConnectionState::Connected)
                {
                    connectedObserved.store(true, std::memory_order_relaxed);
                }
            }
        );

        if (!started)
        {
            subscriber.reset();
            continue;
        }

        if (options.waitForConnectedMs <= 0)
        {
            subscriberStarted = true;
            selectedCandidate = candidate.description;
            break;
        }

        const auto deadline = Clock::now() + std::chrono::milliseconds(perCandidateWaitMs);
        bool connectedSeen = false;
        while (Clock::now() < deadline)
        {
            if (connectionState.load(std::memory_order_relaxed) == sd::direct::ConnectionState::Connected)
            {
                connectedSeen = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (connectedSeen)
        {
            if (options.connectMethod == "auto" && options.requireFirstSample)
            {
                const auto sampleDeadline = Clock::now() + std::chrono::milliseconds((std::max)(250, perCandidateWaitMs / 2));
                bool sawSample = sampleObserved.load(std::memory_order_relaxed);
                while (!sawSample && Clock::now() < sampleDeadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    sawSample = sampleObserved.load(std::memory_order_relaxed);
                }

                if (!sawSample)
                {
                    subscriber->Stop();
                    subscriber.reset();
                    connectionState.store(sd::direct::ConnectionState::Disconnected, std::memory_order_relaxed);
                    continue;
                }
            }

            subscriberStarted = true;
            selectedCandidate = candidate.description;
            break;
        }

        subscriber->Stop();
        subscriber.reset();
        connectionState.store(sd::direct::ConnectionState::Disconnected, std::memory_order_relaxed);
    }

    if (!subscriberStarted || !subscriber)
    {
        std::cerr << "Connection timeout: could not reach Connected using connect method '" << options.connectMethod
                  << "' within " << options.waitForConnectedMs << " ms.\n"
                  << "Tip: pass --mapping-name/--data-event-name/--heartbeat-event-name if your publisher uses custom direct channel names.\n";
        return 6;
    }

    if (!options.quiet)
    {
        std::cout << "Capture start time (UTC): " << ToIso8601Utc(std::chrono::system_clock::now()) << "\n";
        if (options.verbose)
        {
            std::cout << "Label: " << options.label << "\n"
                      << "Run ID: " << options.runId << "\n"
                      << "Duration seconds: " << options.durationSec << "\n"
                      << "Sample ms: " << options.sampleMs << "\n"
                      << "Wait for connected ms: " << options.waitForConnectedMs << "\n"
                      << "Require first sample: " << (options.requireFirstSample ? "true" : "false") << "\n"
                      << "Mapping: " << (options.mappingName.empty() ? "<default>" : options.mappingName) << "\n"
                      << "Data event: " << (options.dataEventName.empty() ? "<default>" : options.dataEventName) << "\n"
                      << "Heartbeat event: " << (options.heartbeatEventName.empty() ? "<default>" : options.heartbeatEventName) << "\n"
                      << "Connect method: " << options.connectMethod << "\n"
                      << "Selected channel candidate: " << selectedCandidate << "\n";
        }
    }

    if (options.startDelayMs > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.startDelayMs));
    }

    const auto startWall = std::chrono::system_clock::now();
    state.captureStartSteady = Clock::now();

    const auto captureDuration = std::chrono::duration<double>(options.durationSec);
    const auto captureDeadline = state.captureStartSteady + std::chrono::duration_cast<Clock::duration>(captureDuration);

    while (!g_stopRequested.load(std::memory_order_relaxed))
    {
        if (StopFileExists(options.stopFilePath))
        {
            if (!options.quiet)
            {
                std::cout << "Stop file detected: " << options.stopFilePath << "\n";
            }
            break;
        }

        if (Clock::now() >= captureDeadline)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto endWall = std::chrono::system_clock::now();
    const sd::direct::ConnectionState stateBeforeStop = connectionState.load(std::memory_order_relaxed);
    subscriber->Stop();

    if (options.listSignals)
    {
        std::set<std::string> signals;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            signals = state.discoveredSignals;
        }

        if (signals.empty())
        {
            std::cout << "No signals observed during discovery window." << "\n";
        }
        else
        {
            for (const auto& key : signals)
            {
                std::cout << key << "\n";
            }
        }
        return 0;
    }

    const std::uint64_t totalUpdates = state.totalUpdates.load(std::memory_order_relaxed);
    if (options.requireFirstSample && totalUpdates == 0)
    {
        std::cerr << "Capture failed: no telemetry samples were captured.\n"
                  << "Tip: verify channel names and try --mapping-name/--data-event-name/--heartbeat-event-name.\n";
        return 7;
    }

    const std::string jsonText = BuildRunJson(options, startWall, endWall, state, totalUpdates);

    std::string writeError;
    if (!WriteOutputFile(options, jsonText, writeError))
    {
        std::cerr << "Write error: " << writeError << "\n";
        return 5;
    }

    if (!options.quiet)
    {
        std::cout << "Capture end time (UTC): " << ToIso8601Utc(endWall) << "\n";
        std::cout << "Output file: " << options.outPath << "\n";
        std::cout << "Connection observed during capture: "
                  << (connectedObserved.load(std::memory_order_relaxed) ? "true" : "false") << "\n";
        std::cout << "Connection state at capture end: "
                  << ConnectionStateToString(stateBeforeStop) << "\n";
        std::cout << "Post-stop connection state: "
                  << ConnectionStateToString(connectionState.load(std::memory_order_relaxed)) << "\n";

        std::map<std::string, std::size_t> counts;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            for (const auto& [key, series] : state.seriesByKey)
            {
                const std::size_t count = (options.sampleMs > 0) ? series.bucketedSamples.size() : series.samples.size();
                counts[key] = count;
            }
        }

        if (counts.empty())
        {
            std::cout << "Signal sample counts: (none captured)\n";
        }
        else
        {
            std::cout << "Signal sample counts:\n";
            for (const auto& [key, count] : counts)
            {
                std::cout << "  " << key << ": " << count << "\n";
            }
        }
    }

    return 0;
}
