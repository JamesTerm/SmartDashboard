#pragma once

/// @file nt4_client.h
/// @brief NT4 WebSocket client for receiving telemetry from an NT4 server.
///
/// This client connects to an NT4 server (e.g. the Robot_Simulation
/// ShuffleboardBackend or a real ntcore server) over WebSocket on port 5810.
/// It implements the NT4 subscription-driven protocol:
///   1. Connect via WebSocket to ws://<host>:5810/nt/<clientname>
///   2. Send a subscribe message with topic patterns
///   3. Receive announce messages for matching topics
///   4. Receive binary MessagePack frames with value updates
///
/// Phase 1 is receive-only (display telemetry from the authority).
/// Phase 2 will add write-back for chooser selections.
///
/// Ian: This is an NT4 *client* — the simulator/robot is the authority/server.
/// The subscribe-before-announce invariant is fundamental to NT4. Do not send
/// or expect announces before the client has subscribed; ntcore silently drops
/// unsolicited announces. This was the #1 discovery on the Robot_Simulation
/// side and cost a full debugging session to figure out.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sd::shuffleboard
{

/// @brief NT4 numeric type codes matching the NT4 protocol spec.
///
/// Ian: These must match the server-side NT4Type enum exactly. The codes are
/// defined in the NT4 spec at wpilibsuite/allwpilib. If they drift, announces
/// will parse correctly but binary frames will decode garbage.
enum class NT4TypeCode : uint8_t
{
    Boolean    = 0,
    Double     = 1,
    Int        = 2,
    Float      = 3,
    String     = 4,
    Raw        = 5,
    BooleanArr = 16,
    DoubleArr  = 17,
    IntArr     = 18,
    FloatArr   = 19,
    StringArr  = 20
};

/// @brief Value types the plugin can deliver to the SmartDashboard host.
///
/// This mirrors the subset of NT4 types that SmartDashboard can display.
/// We collapse Int/Float into Double for the dashboard display pipeline.
enum class ValueType
{
    Bool,
    Double,
    String,
    StringArray
};

/// @brief A decoded topic value from an NT4 binary frame.
struct TopicValue
{
    ValueType type = ValueType::Double;
    bool boolValue = false;
    double doubleValue = 0.0;
    std::string stringValue;
    std::vector<std::string> stringArrayValue;
};

/// @brief A fully decoded variable update delivered to the plugin bridge.
struct TopicUpdate
{
    std::string topicPath;
    TopicValue value;
    uint64_t serverSequence = 0;
};

/// @brief Connection state reported to the host.
enum class ConnectionState
{
    Disconnected = 0,
    Connecting   = 1,
    Connected    = 2,
    Stale        = 3
};

/// @brief Callback for delivering decoded topic updates to the plugin bridge.
using NT4UpdateCallback = std::function<void(const TopicUpdate&)>;

/// @brief Callback for reporting connection state changes.
using NT4ConnectionStateCallback = std::function<void(ConnectionState)>;

/// @brief Configuration for the NT4 client.
struct NT4ClientConfig
{
    std::string host = "127.0.0.1";
    uint16_t port = 5810;
    std::string clientName = "SmartDashboard";

    // Ian: The autoConnect field has been removed.  Reconnect logic now lives
    // in the host (MainWindow) which drives Stop()+Start() cycles via a
    // QTimer.  IXWebSocket's built-in auto-reconnect is always disabled.
};

/// @brief NT4 WebSocket client that connects to an NT4 server, subscribes to
/// all topics, and delivers decoded value updates through callbacks.
///
/// Ian: This client is intentionally simple — subscribe to everything, decode
/// announces and binary frames, deliver updates. The SmartDashboard display
/// pipeline does its own filtering. Do not add topic filtering here; that
/// complexity belongs in the host or a future iteration.
class NT4Client
{
public:
    NT4Client();
    ~NT4Client();

    NT4Client(const NT4Client&) = delete;
    NT4Client& operator=(const NT4Client&) = delete;

    /// @brief Start the client. Non-blocking — connection happens on a
    /// background thread and state changes arrive via the callback.
    /// @return true always (matching NativeLink TCP client convention).
    bool Start(const NT4ClientConfig& config,
               NT4UpdateCallback onUpdate,
               NT4ConnectionStateCallback onConnectionState);

    /// @brief Stop the client and disconnect. Safe to call if not started.
    void Stop();

    /// @brief Check if currently connected to the server.
    bool IsConnected() const;

    /// @brief Publish a bool value back to the server (phase 2 — chooser write-back).
    bool PublishBool(const std::string& key, bool value);

    /// @brief Publish a double value back to the server.
    bool PublishDouble(const std::string& key, double value);

    /// @brief Publish a string value back to the server (phase 2 — chooser write-back).
    bool PublishString(const std::string& key, const std::string& value);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sd::shuffleboard
