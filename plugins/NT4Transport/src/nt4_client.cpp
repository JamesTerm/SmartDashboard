#include "nt4_client.h"
#include <IXNetSystem.h>
#include <IXWebSocket.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Ian: This file implements an NT4 WebSocket client that connects to an NT4
// server (Robot_Simulation NT4Backend or a real ntcore server),
// subscribes to all topics, decodes announce + binary value frames, and
// delivers updates through callbacks. The protocol details were validated
// against the Robot_Simulation NT4Server.cpp and the official Shuffleboard
// app. Key lesson from the simulation side: announces only arrive AFTER the
// client subscribes. Do not assume topics exist on connect.

namespace sd::nt4
{

namespace
{

// Ian: kReconnectDelayMs was removed.  Reconnect logic now lives in the host
// (MainWindow) which drives Stop()+Start() cycles via a QTimer.  IXWebSocket's
// built-in automatic reconnection is always disabled.

// Ian: The subscription UID is an arbitrary client-chosen integer. The server
// uses it to track which subscriptions a client has. We only ever create one
// subscription (all topics), so a fixed value is fine.
constexpr int kSubscriptionUid = 1;

// NT4 subprotocol strings — the server selects one during the WebSocket
// handshake. We prefer v4.1 but accept v4.0.
constexpr const char* kSubprotocolV41 = "v4.1.networktables.first.wpi.edu";
constexpr const char* kSubprotocolV40 = "networktables.first.wpi.edu";

// ────────────────────────────────────────────────────────────────────────────
// Minimal JSON builder — avoids pulling in nlohmann-json for a handful of
// fixed-shape messages.
// ────────────────────────────────────────────────────────────────────────────

std::string BuildSubscribeAllJson()
{
    // Subscribe to all non-meta topics with prefix match on empty string.
    // This is the canonical NT4 "give me everything" subscribe.
    return R"([{"method":"subscribe","params":{"subuid":)"
        + std::to_string(kSubscriptionUid)
        + R"(,"topics":[""],"options":{"prefix":true}}}])";
}

/// Build a JSON publish message for the client to claim a topic for writing.
std::string BuildPublishJson(const std::string& topicName, const std::string& typeStr, int pubuid)
{
    return R"([{"method":"publish","params":{"name":")"
        + topicName
        + R"(","type":")"
        + typeStr
        + R"(","pubuid":)"
        + std::to_string(pubuid)
        + R"(}}])";
}

// ────────────────────────────────────────────────────────────────────────────
// Minimal MessagePack decoder — only what NT4 binary frames need.
// ────────────────────────────────────────────────────────────────────────────

/// @brief Read a big-endian uint16 from a byte buffer.
inline uint16_t ReadBE16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

/// @brief Read a big-endian uint32 from a byte buffer.
inline uint32_t ReadBE32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24)
         | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) <<  8)
         |  static_cast<uint32_t>(p[3]);
}

/// @brief Read a big-endian uint64 from a byte buffer.
inline uint64_t ReadBE64(const uint8_t* p)
{
    return (static_cast<uint64_t>(p[0]) << 56)
         | (static_cast<uint64_t>(p[1]) << 48)
         | (static_cast<uint64_t>(p[2]) << 40)
         | (static_cast<uint64_t>(p[3]) << 32)
         | (static_cast<uint64_t>(p[4]) << 24)
         | (static_cast<uint64_t>(p[5]) << 16)
         | (static_cast<uint64_t>(p[6]) <<  8)
         |  static_cast<uint64_t>(p[7]);
}

/// @brief Read a big-endian IEEE 754 double from a byte buffer.
inline double ReadBEDouble(const uint8_t* p)
{
    const uint64_t bits = ReadBE64(p);
    double result;
    std::memcpy(&result, &bits, sizeof(double));
    return result;
}

/// @brief Minimal MessagePack cursor for decoding NT4 binary frames.
///
/// Ian: NT4 binary frames are always 4-element arrays:
///   [topicID, timestamp_us, typeCode, value]
/// The decoder only needs to handle the types that appear in those positions.
/// We do NOT need a full msgpack library — just enough to walk the format
/// bytes and extract ints, doubles, bools, strings, and string arrays.
struct MsgPackReader
{
    const uint8_t* data;
    size_t size;
    size_t pos;

    MsgPackReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {}

    bool HasRemaining(size_t n) const { return pos + n <= size; }

    uint8_t PeekByte() const { return data[pos]; }

    uint8_t ReadByte()
    {
        return data[pos++];
    }

    /// @brief Read a MessagePack integer (any width, signed or unsigned).
    /// Returns false if the format byte is not an integer type.
    bool ReadInt(int64_t& out)
    {
        if (!HasRemaining(1)) return false;
        const uint8_t b = ReadByte();

        // positive fixint: 0x00 - 0x7F
        if (b <= 0x7F)
        {
            out = static_cast<int64_t>(b);
            return true;
        }
        // negative fixint: 0xE0 - 0xFF
        if (b >= 0xE0)
        {
            out = static_cast<int64_t>(static_cast<int8_t>(b));
            return true;
        }

        switch (b)
        {
            case 0xCC: // uint8
                if (!HasRemaining(1)) return false;
                out = static_cast<int64_t>(ReadByte());
                return true;
            case 0xCD: // uint16
                if (!HasRemaining(2)) return false;
                out = static_cast<int64_t>(ReadBE16(data + pos));
                pos += 2;
                return true;
            case 0xCE: // uint32
                if (!HasRemaining(4)) return false;
                out = static_cast<int64_t>(ReadBE32(data + pos));
                pos += 4;
                return true;
            case 0xCF: // uint64
                if (!HasRemaining(8)) return false;
                out = static_cast<int64_t>(ReadBE64(data + pos));
                pos += 8;
                return true;
            case 0xD0: // int8
                if (!HasRemaining(1)) return false;
                out = static_cast<int64_t>(static_cast<int8_t>(ReadByte()));
                return true;
            case 0xD1: // int16
                if (!HasRemaining(2)) return false;
                out = static_cast<int64_t>(static_cast<int16_t>(ReadBE16(data + pos)));
                pos += 2;
                return true;
            case 0xD2: // int32
                if (!HasRemaining(4)) return false;
                out = static_cast<int64_t>(static_cast<int32_t>(ReadBE32(data + pos)));
                pos += 4;
                return true;
            case 0xD3: // int64
                if (!HasRemaining(8)) return false;
                out = static_cast<int64_t>(ReadBE64(data + pos));
                pos += 8;
                return true;
            default:
                return false;
        }
    }

    /// @brief Read a MessagePack unsigned integer.
    bool ReadUInt(uint64_t& out)
    {
        int64_t signed_val;
        if (!ReadInt(signed_val)) return false;
        out = static_cast<uint64_t>(signed_val);
        return true;
    }

    /// @brief Read a MessagePack double (float64 or float32, or integer promoted to double).
    bool ReadDouble(double& out)
    {
        if (!HasRemaining(1)) return false;
        const uint8_t b = PeekByte();

        // float64
        if (b == 0xCB)
        {
            ReadByte();
            if (!HasRemaining(8)) return false;
            out = ReadBEDouble(data + pos);
            pos += 8;
            return true;
        }
        // float32
        if (b == 0xCA)
        {
            ReadByte();
            if (!HasRemaining(4)) return false;
            const uint32_t bits = ReadBE32(data + pos);
            pos += 4;
            float f;
            std::memcpy(&f, &bits, sizeof(float));
            out = static_cast<double>(f);
            return true;
        }

        // Integer types — promote to double
        int64_t intVal;
        if (ReadInt(intVal))
        {
            out = static_cast<double>(intVal);
            return true;
        }
        return false;
    }

    /// @brief Read a MessagePack bool.
    bool ReadBool(bool& out)
    {
        if (!HasRemaining(1)) return false;
        const uint8_t b = ReadByte();
        if (b == 0xC3) { out = true; return true; }
        if (b == 0xC2) { out = false; return true; }
        return false;
    }

    /// @brief Read a MessagePack string.
    bool ReadString(std::string& out)
    {
        if (!HasRemaining(1)) return false;
        const uint8_t b = ReadByte();
        size_t len = 0;

        // fixstr: 0xA0 - 0xBF
        if ((b & 0xE0) == 0xA0)
        {
            len = b & 0x1F;
        }
        else if (b == 0xD9) // str8
        {
            if (!HasRemaining(1)) return false;
            len = ReadByte();
        }
        else if (b == 0xDA) // str16
        {
            if (!HasRemaining(2)) return false;
            len = ReadBE16(data + pos);
            pos += 2;
        }
        else if (b == 0xDB) // str32
        {
            if (!HasRemaining(4)) return false;
            len = ReadBE32(data + pos);
            pos += 4;
        }
        else
        {
            return false;
        }

        if (!HasRemaining(len)) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return true;
    }

    /// @brief Read a MessagePack array header and return the element count.
    bool ReadArrayHeader(size_t& count)
    {
        if (!HasRemaining(1)) return false;
        const uint8_t b = ReadByte();

        // fixarray: 0x90 - 0x9F
        if ((b & 0xF0) == 0x90)
        {
            count = b & 0x0F;
            return true;
        }
        if (b == 0xDC) // array16
        {
            if (!HasRemaining(2)) return false;
            count = ReadBE16(data + pos);
            pos += 2;
            return true;
        }
        if (b == 0xDD) // array32
        {
            if (!HasRemaining(4)) return false;
            count = ReadBE32(data + pos);
            pos += 4;
            return true;
        }
        return false;
    }

    /// @brief Read a string array (MessagePack array of strings).
    bool ReadStringArray(std::vector<std::string>& out)
    {
        size_t count = 0;
        if (!ReadArrayHeader(count)) return false;
        out.clear();
        out.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            std::string s;
            if (!ReadString(s)) return false;
            out.push_back(std::move(s));
        }
        return true;
    }

    /// @brief Skip a single MessagePack element (used to skip past unknown types).
    bool SkipElement()
    {
        if (!HasRemaining(1)) return false;
        const uint8_t b = ReadByte();

        // positive fixint, negative fixint
        if (b <= 0x7F || b >= 0xE0) return true;
        // nil, false, true
        if (b == 0xC0 || b == 0xC2 || b == 0xC3) return true;
        // fixstr
        if ((b & 0xE0) == 0xA0) { size_t n = b & 0x1F; if (!HasRemaining(n)) return false; pos += n; return true; }
        // fixarray
        if ((b & 0xF0) == 0x90) { size_t n = b & 0x0F; for (size_t i = 0; i < n; ++i) { if (!SkipElement()) return false; } return true; }
        // fixmap
        if ((b & 0xF0) == 0x80) { size_t n = b & 0x0F; for (size_t i = 0; i < n * 2; ++i) { if (!SkipElement()) return false; } return true; }

        switch (b)
        {
            case 0xCC: case 0xD0: if (!HasRemaining(1)) return false; pos += 1; return true;
            case 0xCD: case 0xD1: if (!HasRemaining(2)) return false; pos += 2; return true;
            case 0xCE: case 0xD2: case 0xCA: if (!HasRemaining(4)) return false; pos += 4; return true;
            case 0xCF: case 0xD3: case 0xCB: if (!HasRemaining(8)) return false; pos += 8; return true;
            case 0xD9: { if (!HasRemaining(1)) return false; size_t n = ReadByte(); if (!HasRemaining(n)) return false; pos += n; return true; }
            case 0xDA: { if (!HasRemaining(2)) return false; size_t n = ReadBE16(data + pos); pos += 2; if (!HasRemaining(n)) return false; pos += n; return true; }
            case 0xDB: { if (!HasRemaining(4)) return false; size_t n = ReadBE32(data + pos); pos += 4; if (!HasRemaining(n)) return false; pos += n; return true; }
            case 0xDC: { if (!HasRemaining(2)) return false; size_t n = ReadBE16(data + pos); pos += 2; for (size_t i = 0; i < n; ++i) { if (!SkipElement()) return false; } return true; }
            case 0xDD: { if (!HasRemaining(4)) return false; size_t n = ReadBE32(data + pos); pos += 4; for (size_t i = 0; i < n; ++i) { if (!SkipElement()) return false; } return true; }
            default: return false;
        }
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Minimal MessagePack encoder — for building outbound binary frames
// (timestamp sync, value publishes).
// ────────────────────────────────────────────────────────────────────────────

class MsgPackWriter
{
public:
    std::vector<uint8_t>& buf;

    explicit MsgPackWriter(std::vector<uint8_t>& b) : buf(b) {}

    void WriteFixArray(uint8_t count)
    {
        buf.push_back(static_cast<uint8_t>(0x90 | (count & 0x0F)));
    }

    void WriteInt(int64_t v)
    {
        if (v >= 0 && v <= 127)
        {
            buf.push_back(static_cast<uint8_t>(v));
        }
        else if (v < 0 && v >= -32)
        {
            buf.push_back(static_cast<uint8_t>(v));
        }
        else if (v >= -128 && v <= 127)
        {
            buf.push_back(0xD0);
            buf.push_back(static_cast<uint8_t>(static_cast<int8_t>(v)));
        }
        else if (v >= -32768 && v <= 32767)
        {
            buf.push_back(0xD1);
            const auto sv = static_cast<int16_t>(v);
            buf.push_back(static_cast<uint8_t>((sv >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(sv & 0xFF));
        }
        else if (v >= INT32_MIN && v <= INT32_MAX)
        {
            buf.push_back(0xD2);
            const auto sv = static_cast<int32_t>(v);
            buf.push_back(static_cast<uint8_t>((sv >> 24) & 0xFF));
            buf.push_back(static_cast<uint8_t>((sv >> 16) & 0xFF));
            buf.push_back(static_cast<uint8_t>((sv >>  8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(sv & 0xFF));
        }
        else
        {
            buf.push_back(0xD3);
            buf.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(v & 0xFF));
        }
    }

    void WriteUInt(uint64_t v)
    {
        if (v <= 127)
        {
            buf.push_back(static_cast<uint8_t>(v));
        }
        else if (v <= 0xFF)
        {
            buf.push_back(0xCC);
            buf.push_back(static_cast<uint8_t>(v));
        }
        else if (v <= 0xFFFF)
        {
            buf.push_back(0xCD);
            buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(v & 0xFF));
        }
        else if (v <= 0xFFFFFFFF)
        {
            buf.push_back(0xCE);
            buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(v & 0xFF));
        }
        else
        {
            buf.push_back(0xCF);
            buf.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            buf.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(v & 0xFF));
        }
    }

    void WriteDouble(double v)
    {
        buf.push_back(0xCB);
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(double));
        buf.push_back(static_cast<uint8_t>((bits >> 56) & 0xFF));
        buf.push_back(static_cast<uint8_t>((bits >> 48) & 0xFF));
        buf.push_back(static_cast<uint8_t>((bits >> 40) & 0xFF));
        buf.push_back(static_cast<uint8_t>((bits >> 32) & 0xFF));
        buf.push_back(static_cast<uint8_t>((bits >> 24) & 0xFF));
        buf.push_back(static_cast<uint8_t>((bits >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((bits >>  8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(bits & 0xFF));
    }

    void WriteBool(bool v)
    {
        buf.push_back(v ? 0xC3 : 0xC2);
    }

    void WriteString(const std::string& s)
    {
        const size_t len = s.size();
        if (len <= 31)
        {
            buf.push_back(static_cast<uint8_t>(0xA0 | len));
        }
        else if (len <= 0xFF)
        {
            buf.push_back(0xD9);
            buf.push_back(static_cast<uint8_t>(len));
        }
        else if (len <= 0xFFFF)
        {
            buf.push_back(0xDA);
            buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(len & 0xFF));
        }
        else
        {
            buf.push_back(0xDB);
            buf.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
            buf.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
            buf.push_back(static_cast<uint8_t>((len >>  8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(len & 0xFF));
        }
        buf.insert(buf.end(), s.begin(), s.end());
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Minimal JSON parser — just enough to extract fields from announce messages.
//
// Ian: We intentionally avoid pulling in nlohmann-json or any heavy JSON
// library. The announce messages have a fixed, small shape and we only need
// a handful of fields. A lightweight hand-parser keeps the plugin dependency
// footprint minimal — same philosophy as the NativeLink plugin.
// ────────────────────────────────────────────────────────────────────────────

/// Extract a string value for a given key from a JSON object fragment.
/// Assumes the key appears as "key":"value" with no nested escapes.
std::string JsonExtractString(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return {};

    const size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return {};

    const size_t firstQuote = json.find('"', colonPos + 1);
    if (firstQuote == std::string::npos) return {};

    const size_t secondQuote = json.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos || secondQuote <= firstQuote + 1) return {};

    return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

/// Extract an integer value for a given key from a JSON object fragment.
/// Returns -1 if not found.
int64_t JsonExtractInt(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return -1;

    const size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return -1;

    const size_t valueStart = json.find_first_of("-0123456789", colonPos + 1);
    if (valueStart == std::string::npos) return -1;

    const size_t valueEnd = json.find_first_not_of("-0123456789", valueStart);
    const std::string token = json.substr(valueStart, valueEnd - valueStart);
    return std::strtoll(token.c_str(), nullptr, 10);
}

/// @brief NT4 type string → NT4TypeCode mapping.
NT4TypeCode TypeCodeFromString(const std::string& typeStr)
{
    if (typeStr == "boolean")   return NT4TypeCode::Boolean;
    if (typeStr == "double")    return NT4TypeCode::Double;
    if (typeStr == "int")       return NT4TypeCode::Int;
    if (typeStr == "float")     return NT4TypeCode::Float;
    if (typeStr == "string")    return NT4TypeCode::String;
    if (typeStr == "raw")       return NT4TypeCode::Raw;
    if (typeStr == "boolean[]") return NT4TypeCode::BooleanArr;
    if (typeStr == "double[]")  return NT4TypeCode::DoubleArr;
    if (typeStr == "int[]")     return NT4TypeCode::IntArr;
    if (typeStr == "float[]")   return NT4TypeCode::FloatArr;
    if (typeStr == "string[]")  return NT4TypeCode::StringArr;
    return NT4TypeCode::Raw; // unknown → raw (will be skipped in value decode)
}

/// @brief NT4TypeCode → type string (for publish messages).
const char* TypeStringFromCode(NT4TypeCode code)
{
    switch (code)
    {
        case NT4TypeCode::Boolean:    return "boolean";
        case NT4TypeCode::Double:     return "double";
        case NT4TypeCode::Int:        return "int";
        case NT4TypeCode::Float:      return "float";
        case NT4TypeCode::String:     return "string";
        case NT4TypeCode::Raw:        return "raw";
        case NT4TypeCode::BooleanArr: return "boolean[]";
        case NT4TypeCode::DoubleArr:  return "double[]";
        case NT4TypeCode::IntArr:     return "int[]";
        case NT4TypeCode::FloatArr:   return "float[]";
        case NT4TypeCode::StringArr:  return "string[]";
        default:                      return "raw";
    }
}

/// @brief Strip the /SmartDashboard/ prefix to get the short key name that
/// the dashboard display pipeline expects.
///
/// Ian: The NT4 server publishes topics under /SmartDashboard/<key>. The
/// dashboard tiles work with the short key (e.g. "Velocity" not
/// "/SmartDashboard/Velocity"). Strip the prefix here so the plugin bridge
/// doesn't need to know about NT4 path conventions. If a topic doesn't have
/// the expected prefix, pass it through unchanged — the host will create a
/// tile for it anyway.
std::string StripSmartDashboardPrefix(const std::string& topicPath)
{
    static const std::string kPrefix = "/SmartDashboard/";
    if (topicPath.compare(0, kPrefix.size(), kPrefix) == 0)
    {
        return topicPath.substr(kPrefix.size());
    }
    return topicPath;
}

/// @brief Server-assigned topic metadata stored after an announce.
struct AnnouncedTopic
{
    std::string name;       // Full NT4 path, e.g. "/SmartDashboard/Velocity"
    NT4TypeCode typeCode;
    int32_t serverId;       // Server-assigned topic ID used in binary frames
};

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
// NT4Client::Impl
// ────────────────────────────────────────────────────────────────────────────

struct NT4Client::Impl
{
    NT4ClientConfig config;
    NT4UpdateCallback onUpdate;
    NT4ConnectionStateCallback onConnectionState;

    ix::WebSocket ws;
    std::atomic<bool> connected{false};
    std::atomic<bool> stopping{false};

    // Ian: Winsock initialisation is deferred until Start() so that sessions
    // using Direct or NativeLink transport never pay the WSAStartup cost.
    // ix::initNetSystem() wraps WSAStartup on Windows and is a no-op elsewhere.
    bool netInitialized{false};

    // Ian: Topic map is keyed by server-assigned topic ID. The server sends
    // the ID in every binary value frame, so lookups here happen on the hot
    // path. An unordered_map is fine for the ~50 topics we expect.
    std::mutex topicMutex;
    std::unordered_map<int32_t, AnnouncedTopic> topicsById;

    // Monotonically increasing sequence number for host-side ordering.
    std::atomic<uint64_t> sequence{0};

    // Publisher UID counter for outbound publish claims (phase 2).
    std::atomic<int> nextPubUid{1};

    // Published topic IDs for write-back (topicPath → pubuid).
    std::mutex pubMutex;
    std::unordered_map<std::string, int> publishedTopics;

    void ReportState(ConnectionState state)
    {
        if (onConnectionState)
        {
            onConnectionState(state);
        }
    }

    void HandleTextMessage(const std::string& text)
    {
        // Ian: Each text frame is a JSON array of message objects. We scan for
        // "announce" method entries and extract the topic metadata. The JSON
        // shape is fixed and small — we don't need a full parser.
        //
        // Format: [{"method":"announce","params":{"name":"...","id":N,"type":"...","properties":{...}}}]
        //
        // We split on {"method" boundaries to handle multiple messages in one frame.

        size_t searchPos = 0;
        while (searchPos < text.size())
        {
            const size_t methodPos = text.find("\"method\"", searchPos);
            if (methodPos == std::string::npos) break;

            // Find the method value
            const std::string method = JsonExtractString(
                text.substr(methodPos, text.size() - methodPos), "method");

            if (method == "announce")
            {
                // Find the params block — scan forward from methodPos for "params"
                const size_t paramsPos = text.find("\"params\"", methodPos);
                if (paramsPos != std::string::npos)
                {
                    // Extract a generous substring covering the params object
                    const size_t blockEnd = text.find("}}", paramsPos);
                    const size_t endPos = (blockEnd != std::string::npos) ? blockEnd + 2 : text.size();
                    const std::string paramsBlock = text.substr(paramsPos, endPos - paramsPos);

                    const std::string name = JsonExtractString(paramsBlock, "name");
                    const int64_t id = JsonExtractInt(paramsBlock, "id");
                    const std::string typeStr = JsonExtractString(paramsBlock, "type");

                    if (!name.empty() && id >= 0 && !typeStr.empty())
                    {
                        AnnouncedTopic topic;
                        topic.name = name;
                        topic.serverId = static_cast<int32_t>(id);
                        topic.typeCode = TypeCodeFromString(typeStr);

                        std::lock_guard<std::mutex> lock(topicMutex);
                        topicsById[topic.serverId] = std::move(topic);
                    }
                }
            }

            // Advance past this method entry
            searchPos = methodPos + 8; // past "method"
        }
    }

    void HandleBinaryMessage(const std::string& rawData)
    {
        const auto* data = reinterpret_cast<const uint8_t*>(rawData.data());
        const size_t dataSize = rawData.size();

        // Ian: A single binary frame may contain multiple concatenated
        // MessagePack arrays. Process them in a loop until the buffer is
        // exhausted. Each array is a 4-element tuple:
        //   [topicID, timestamp_us, typeCode, value]
        MsgPackReader reader(data, dataSize);

        while (reader.pos < reader.size)
        {
            size_t arrayLen = 0;
            if (!reader.ReadArrayHeader(arrayLen) || arrayLen < 4)
            {
                break; // Malformed — stop processing this frame
            }

            // Element 0: topic ID
            int64_t topicId = 0;
            if (!reader.ReadInt(topicId))
            {
                break;
            }

            // Element 1: timestamp (microseconds) — we read but don't use it yet
            uint64_t timestamp = 0;
            if (!reader.ReadUInt(timestamp))
            {
                break;
            }

            // Element 2: type code
            int64_t typeCodeRaw = 0;
            if (!reader.ReadInt(typeCodeRaw))
            {
                break;
            }

            // Topic ID -1 = timestamp sync response. Skip it.
            if (topicId == -1)
            {
                reader.SkipElement(); // skip the value element
                // Skip any extra array elements beyond 4
                for (size_t i = 4; i < arrayLen; ++i)
                {
                    reader.SkipElement();
                }
                continue;
            }

            const auto typeCode = static_cast<NT4TypeCode>(static_cast<uint8_t>(typeCodeRaw));

            // Look up the topic by server ID
            std::string topicName;
            {
                std::lock_guard<std::mutex> lock(topicMutex);
                auto it = topicsById.find(static_cast<int32_t>(topicId));
                if (it != topicsById.end())
                {
                    topicName = it->second.name;
                }
            }

            if (topicName.empty())
            {
                // Unknown topic ID — skip the value and continue
                reader.SkipElement();
                for (size_t i = 4; i < arrayLen; ++i)
                {
                    reader.SkipElement();
                }
                continue;
            }

            // Element 3: decode the value based on the type code
            TopicUpdate update;
            update.topicPath = StripSmartDashboardPrefix(topicName);
            update.serverSequence = sequence.fetch_add(1, std::memory_order_relaxed);

            bool decoded = false;
            switch (typeCode)
            {
                case NT4TypeCode::Boolean:
                {
                    bool bv = false;
                    if (reader.ReadBool(bv))
                    {
                        update.value.type = ValueType::Bool;
                        update.value.boolValue = bv;
                        decoded = true;
                    }
                    break;
                }
                case NT4TypeCode::Double:
                case NT4TypeCode::Float:
                {
                    double dv = 0.0;
                    if (reader.ReadDouble(dv))
                    {
                        update.value.type = ValueType::Double;
                        update.value.doubleValue = dv;
                        decoded = true;
                    }
                    break;
                }
                case NT4TypeCode::Int:
                {
                    int64_t iv = 0;
                    if (reader.ReadInt(iv))
                    {
                        update.value.type = ValueType::Double;
                        update.value.doubleValue = static_cast<double>(iv);
                        decoded = true;
                    }
                    break;
                }
                case NT4TypeCode::String:
                {
                    std::string sv;
                    if (reader.ReadString(sv))
                    {
                        update.value.type = ValueType::String;
                        update.value.stringValue = std::move(sv);
                        decoded = true;
                    }
                    break;
                }
                case NT4TypeCode::StringArr:
                {
                    std::vector<std::string> arr;
                    if (reader.ReadStringArray(arr))
                    {
                        update.value.type = ValueType::StringArray;
                        update.value.stringArrayValue = std::move(arr);
                        decoded = true;
                    }
                    break;
                }
                default:
                    // Unsupported type — skip the value element
                    reader.SkipElement();
                    break;
            }

            // Skip any extra array elements beyond the 4 we expect
            for (size_t i = 4; i < arrayLen; ++i)
            {
                reader.SkipElement();
            }

            if (decoded && onUpdate)
            {
                onUpdate(update);
            }
        }
    }

    /// @brief Build a binary frame for publishing a value to the server.
    /// Uses the pubuid as the topic ID field (NT4 client publish convention).
    std::vector<uint8_t> BuildValueFrame(int pubuid, NT4TypeCode typeCode, const TopicValue& value)
    {
        std::vector<uint8_t> buf;
        buf.reserve(32);
        MsgPackWriter w(buf);

        w.WriteFixArray(4);
        w.WriteInt(pubuid); // use pubuid, not server topic ID, for client→server
        w.WriteUInt(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        ));
        w.WriteInt(static_cast<int64_t>(typeCode));

        switch (typeCode)
        {
            case NT4TypeCode::Boolean: w.WriteBool(value.boolValue); break;
            case NT4TypeCode::Double:  w.WriteDouble(value.doubleValue); break;
            case NT4TypeCode::String:  w.WriteString(value.stringValue); break;
            default: w.WriteBool(false); break; // fallback
        }

        return buf;
    }

    /// @brief Ensure we have a publish claim for a topic, return the pubuid.
    /// Ian: The host passes short keys like "TestMove" or "Test/Auton_Selection/AutoChooser/selected".
    /// The NT4 server expects the full path "/SmartDashboard/<key>" — same prefix it uses
    /// when publishing topics to us. Without this prefix the server creates a new topic
    /// outside the /SmartDashboard/ namespace and the simulator never sees the value.
    int EnsurePublished(const std::string& topicPath, NT4TypeCode typeCode)
    {
        const std::string ntPath = "/SmartDashboard/" + topicPath;
        // Check if already published
        {
            std::lock_guard<std::mutex> lock(pubMutex);
            auto it = publishedTopics.find(ntPath);
            if (it != publishedTopics.end())
            {
                return it->second;
            }
        }

        // Claim the topic with a publish message
        const int pubuid = nextPubUid.fetch_add(1, std::memory_order_relaxed);
        const std::string publishJson = BuildPublishJson(ntPath, TypeStringFromCode(typeCode), pubuid);
        ws.sendText(publishJson);

        {
            std::lock_guard<std::mutex> lock(pubMutex);
            publishedTopics[ntPath] = pubuid;
        }

        return pubuid;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// NT4Client public API
// ────────────────────────────────────────────────────────────────────────────

NT4Client::NT4Client()
    : m_impl(std::make_unique<Impl>())
{
}

NT4Client::~NT4Client()
{
    Stop();
}

bool NT4Client::Start(
    const NT4ClientConfig& config,
    NT4UpdateCallback onUpdate,
    NT4ConnectionStateCallback onConnectionState)
{
    m_impl->config = config;
    m_impl->onUpdate = std::move(onUpdate);
    m_impl->onConnectionState = std::move(onConnectionState);
    m_impl->stopping.store(false);
    m_impl->connected.store(false);
    m_impl->sequence.store(0);
    m_impl->nextPubUid.store(1);

    {
        std::lock_guard<std::mutex> lock(m_impl->topicMutex);
        m_impl->topicsById.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_impl->pubMutex);
        m_impl->publishedTopics.clear();
    }

    // Build the WebSocket URL: ws://<host>:<port>/nt/<clientname>
    const std::string url = "ws://" + config.host + ":" + std::to_string(config.port)
        + "/nt/" + config.clientName;

    m_impl->ws.setUrl(url);

    // Ian: The NT4 subprotocol negotiation is critical. Without the correct
    // Sec-WebSocket-Protocol header, ntcore rejects the connection. We request
    // v4.1 first (preferred) and v4.0 as fallback. The server will pick one.
    // This was the second major discovery on the Robot_Simulation side — the
    // IXWebSocket library needed a patch to correctly echo back a single
    // selected subprotocol.
    ix::WebSocketHttpHeaders extraHeaders;
    extraHeaders["Sec-WebSocket-Protocol"] = std::string(kSubprotocolV41) + ", " + kSubprotocolV40;
    m_impl->ws.setExtraHeaders(extraHeaders);

    m_impl->ws.disablePerMessageDeflate();
    m_impl->ws.setPingInterval(30);

    // Ian: IXWebSocket's built-in auto-reconnect is always disabled now.
    // The host (MainWindow) owns reconnect logic and drives Stop()+Start()
    // cycles via a QTimer.  The plugin makes a single connect attempt per
    // Start() call and fires Disconnected if the connection drops.
    m_impl->ws.disableAutomaticReconnection();

    // Set up the message handler
    auto* impl = m_impl.get();
    m_impl->ws.setOnMessageCallback([impl](const ix::WebSocketMessagePtr& msg)
    {
        if (impl->stopping.load()) return;

        switch (msg->type)
        {
            case ix::WebSocketMessageType::Open:
            {
                impl->connected.store(true);
                impl->ReportState(ConnectionState::Connected);

                // Ian: Send subscribe immediately on connect. This is the
                // trigger for the server to start sending announces. Without
                // this, the server has no idea what topics we want and will
                // send nothing. The subscribe-before-announce invariant is
                // the #1 NT4 protocol lesson.
                const std::string subscribeMsg = BuildSubscribeAllJson();
                impl->ws.sendText(subscribeMsg);
                break;
            }
            case ix::WebSocketMessageType::Close:
            {
                impl->connected.store(false);
                // Clear topic state on disconnect — a new server session
                // may assign different topic IDs.
                {
                    std::lock_guard<std::mutex> lock(impl->topicMutex);
                    impl->topicsById.clear();
                }
                {
                    std::lock_guard<std::mutex> lock(impl->pubMutex);
                    impl->publishedTopics.clear();
                }
                impl->ReportState(ConnectionState::Disconnected);
                break;
            }
            case ix::WebSocketMessageType::Error:
            {
                impl->connected.store(false);
                impl->ReportState(ConnectionState::Disconnected);
                break;
            }
            case ix::WebSocketMessageType::Message:
            {
                if (msg->binary)
                {
                    impl->HandleBinaryMessage(msg->str);
                }
                else
                {
                    impl->HandleTextMessage(msg->str);
                }
                break;
            }
            default:
                break;
        }
    });

    // Report Connecting before starting the WebSocket
    m_impl->ReportState(ConnectionState::Connecting);

    // Ian: start() is non-blocking — it launches the IXWebSocket background
    // thread which handles connect, read, reconnect. This matches the
    // NativeLink TCP client convention: Start() returns immediately, state
    // changes arrive via callbacks.
    // Ian: Initialise Winsock on first use. ix::initNetSystem() calls
    // WSAStartup on Windows — without this, every socket call fails with
    // WSANOTINITIALISED. Deferred to Start() so Direct/NativeLink sessions
    // never trigger it; those plugins have their own WSAStartup calls.
    if (!m_impl->netInitialized)
    {
        ix::initNetSystem();
        m_impl->netInitialized = true;
    }

    m_impl->ws.start();

    return true;
}

void NT4Client::Stop()
{
    m_impl->stopping.store(true);
    m_impl->ws.stop();
    m_impl->connected.store(false);

    if (m_impl->netInitialized)
    {
        ix::uninitNetSystem();
        m_impl->netInitialized = false;
    }
}

bool NT4Client::IsConnected() const
{
    return m_impl->connected.load();
}

bool NT4Client::PublishBool(const std::string& key, bool value)
{
    if (!m_impl->connected.load()) return false;

    const int pubuid = m_impl->EnsurePublished(key, NT4TypeCode::Boolean);

    TopicValue tv;
    tv.type = ValueType::Bool;
    tv.boolValue = value;
    auto frame = m_impl->BuildValueFrame(pubuid, NT4TypeCode::Boolean, tv);

    m_impl->ws.sendBinary(ix::IXWebSocketSendData(
        reinterpret_cast<const char*>(frame.data()), frame.size()));
    return true;
}

bool NT4Client::PublishDouble(const std::string& key, double value)
{
    if (!m_impl->connected.load()) return false;

    const int pubuid = m_impl->EnsurePublished(key, NT4TypeCode::Double);

    TopicValue tv;
    tv.type = ValueType::Double;
    tv.doubleValue = value;
    auto frame = m_impl->BuildValueFrame(pubuid, NT4TypeCode::Double, tv);

    m_impl->ws.sendBinary(ix::IXWebSocketSendData(
        reinterpret_cast<const char*>(frame.data()), frame.size()));
    return true;
}

bool NT4Client::PublishString(const std::string& key, const std::string& value)
{
    if (!m_impl->connected.load()) return false;

    const int pubuid = m_impl->EnsurePublished(key, NT4TypeCode::String);

    TopicValue tv;
    tv.type = ValueType::String;
    tv.stringValue = value;
    auto frame = m_impl->BuildValueFrame(pubuid, NT4TypeCode::String, tv);

    m_impl->ws.sendBinary(ix::IXWebSocketSendData(
        reinterpret_cast<const char*>(frame.data()), frame.size()));
    return true;
}

} // namespace sd::nt4
