#pragma once
// ---------------------------------------------------------------------------
// log_streamer.h -- Structured log streaming with severity & subscriptions.
//
// Usage (C++ side):
//   uz::LogStreamer::init(&wsServer);
//   uz::LogStreamer::emit(uz::LogLevel::Error, "RHI", "Device removed");
//
// Usage (IPC side):
//   subscribe_logs { "level": "WARN", "systems": ["RHI"] }
//   unsubscribe_logs { "id": "..." }
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uz {

// Severity levels (ordered by increasing severity).
enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5,
};

inline const char* log_level_name(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

inline LogLevel log_level_from_string(const std::string& s) {
    if (s == "TRACE") return LogLevel::Trace;
    if (s == "DEBUG") return LogLevel::Debug;
    if (s == "INFO")  return LogLevel::Info;
    if (s == "WARN")  return LogLevel::Warn;
    if (s == "ERROR") return LogLevel::Error;
    if (s == "FATAL") return LogLevel::Fatal;
    return LogLevel::Info;
}

// Broadcast callback type — sends a JSON text message to all WS clients.
using BroadcastFn = std::function<void(const std::string&)>;

struct LogSubscription {
    std::string id;
    LogLevel    min_level = LogLevel::Info;
    std::unordered_set<std::string> systems; // Empty = all systems.
};

class LogStreamer {
public:
    // Set the broadcast function (typically WsServer::broadcastText).
    static void init(BroadcastFn fn);

    // Emit a log entry. If any subscription matches, broadcasts to WS clients.
    // Also buffers the last `kMaxBuffer` entries for AI retrieval.
    static void emit(LogLevel level, const char* system, const char* message);

    // Add a subscription. Returns the subscription ID.
    static std::string subscribe(LogLevel min_level,
                                  const std::vector<std::string>& systems);

    // Remove a subscription by ID.
    static void unsubscribe(const std::string& id);

    // Clear all subscriptions.
    static void clear();

    // Get buffered error/fatal entries (for AI crash analysis).
    static std::string get_error_buffer_json();

    // Rate limiter: max messages per second per subscription.
    static constexpr int kMaxRate = 100;

    // Buffer size for recent error/fatal entries.
    static constexpr int kMaxBuffer = 200;
};

} // namespace uz
