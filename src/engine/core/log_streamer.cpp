// ---------------------------------------------------------------------------
// log_streamer.cpp -- Implementation of structured log streaming.
// ---------------------------------------------------------------------------
#include "engine/core/log_streamer.h"
#include "engine/core/diag.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <sstream>

namespace {

// Global state (all guarded by s_mutex).
std::mutex                     s_mutex;
uz::BroadcastFn                s_broadcast;
std::vector<uz::LogSubscription> s_subscriptions;
uint32_t                       s_next_id = 1;

// Error/Fatal ring buffer for AI retrieval.
struct BufferedEntry {
    uz::LogLevel    level;
    std::string     system;
    std::string     message;
    int64_t         timestamp_ms;
};
std::deque<BufferedEntry>      s_error_buffer;

// Per-subscription rate limiter state.
struct RateState {
    int64_t window_start_ms = 0;
    int     count           = 0;
    int     dropped         = 0;
};
std::unordered_map<std::string, RateState> s_rate_state;

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// Escape a string for JSON output.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

} // anonymous namespace

namespace uz {

void LogStreamer::init(BroadcastFn fn) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_broadcast = std::move(fn);
}

void LogStreamer::emit(LogLevel level, const char* system, const char* message) {
    std::lock_guard<std::mutex> lock(s_mutex);

    const int64_t ts = now_ms();
    const std::string sys = system ? system : "";
    const std::string msg = message ? message : "";

    // Buffer errors/fatals for AI retrieval.
    if (level >= LogLevel::Error) {
        s_error_buffer.push_back({level, sys, msg, ts});
        while (static_cast<int>(s_error_buffer.size()) > kMaxBuffer) {
            s_error_buffer.pop_front();
        }
    }

    if (!s_broadcast) return;

    // Check each subscription.
    for (auto& sub : s_subscriptions) {
        // Level filter.
        if (level < sub.min_level) continue;

        // System filter (empty = all systems).
        if (!sub.systems.empty() && sub.systems.find(sys) == sub.systems.end()) {
            continue;
        }

        // Rate limiter.
        auto& rs = s_rate_state[sub.id];
        if (ts - rs.window_start_ms >= 1000) {
            // New window — send overflow notification if needed.
            if (rs.dropped > 0) {
                char overflow[256];
                std::snprintf(overflow, sizeof(overflow),
                    R"({"event":"log_overflow","data":{"subscription_id":"%s","dropped_count":%d}})",
                    sub.id.c_str(), rs.dropped);
                s_broadcast(overflow);
            }
            rs.window_start_ms = ts;
            rs.count = 0;
            rs.dropped = 0;
        }
        if (rs.count >= kMaxRate) {
            rs.dropped++;
            continue;
        }
        rs.count++;

        // Broadcast the log event as JSON.
        char buf[2048];
        std::snprintf(buf, sizeof(buf),
            R"({"event":"engine.log","data":{"level":"%s","system":"%s","message":"%s","timestamp_ms":%lld,"subscription_id":"%s"}})",
            log_level_name(level),
            json_escape(sys).c_str(),
            json_escape(msg).c_str(),
            static_cast<long long>(ts),
            sub.id.c_str());
        s_broadcast(buf);
    }
}

std::string LogStreamer::subscribe(LogLevel min_level,
                                   const std::vector<std::string>& systems) {
    std::lock_guard<std::mutex> lock(s_mutex);
    LogSubscription sub;
    sub.id = "logsub_" + std::to_string(s_next_id++);
    sub.min_level = min_level;
    for (const auto& s : systems) {
        sub.systems.insert(s);
    }
    s_subscriptions.push_back(std::move(sub));
    return s_subscriptions.back().id;
}

void LogStreamer::unsubscribe(const std::string& id) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_subscriptions.erase(
        std::remove_if(s_subscriptions.begin(), s_subscriptions.end(),
                       [&](const LogSubscription& s) { return s.id == id; }),
        s_subscriptions.end());
    s_rate_state.erase(id);
}

void LogStreamer::clear() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_subscriptions.clear();
    s_rate_state.clear();
}

std::string LogStreamer::get_error_buffer_json() {
    std::lock_guard<std::mutex> lock(s_mutex);
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& e : s_error_buffer) {
        if (!first) oss << ",";
        first = false;
        oss << R"({"level":")" << log_level_name(e.level)
            << R"(","system":")" << json_escape(e.system)
            << R"(","message":")" << json_escape(e.message)
            << R"(","timestamp_ms":)" << e.timestamp_ms << "}";
    }
    oss << "]";
    return oss.str();
}

} // namespace uz
