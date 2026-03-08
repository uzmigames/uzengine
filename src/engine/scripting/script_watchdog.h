#pragma once

// script_watchdog.h -- Background thread that terminates long-running Lua scripts.
//
// LuaJIT can compile hot loops to native code that bypasses the Lua instruction
// hook. The watchdog monitors wall-clock time and force-aborts scripts that
// exceed the timeout by installing an immediate-abort hook.
//
// Usage:
//   ScriptWatchdog wd;
//   wd.init(100);              // 100 ms timeout
//   {
//       ScriptWatchdogGuard g(wd, L);   // RAII start/stop
//       lua_pcall(L, ...);
//   }
//   wd.shutdown();

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

struct lua_State;

namespace uz {

class ScriptWatchdog {
public:
    ScriptWatchdog() = default;
    ~ScriptWatchdog();

    ScriptWatchdog(const ScriptWatchdog&) = delete;
    ScriptWatchdog& operator=(const ScriptWatchdog&) = delete;

    /// Start the watchdog thread. timeout_ms = wall-clock budget per script call.
    void init(int timeout_ms);

    /// Stop the watchdog thread.
    void shutdown();

    /// Mark a script call as starting. Call before lua_pcall.
    void start(lua_State* L);

    /// Mark a script call as finished. Call after lua_pcall returns.
    void stop();

    /// True if the watchdog is currently running.
    bool running() const { return thread_running_.load(std::memory_order_relaxed); }

    /// True if the watchdog fired (timed out) on the current/last guard.
    bool timed_out() const { return timed_out_.load(std::memory_order_relaxed); }

    /// Number of times the watchdog has fired since init.
    uint32_t timeout_count() const { return timeout_count_.load(std::memory_order_relaxed); }

private:
    void watchdog_loop();

    std::thread           thread_;
    std::atomic<bool>     shutdown_{false};
    std::atomic<bool>     thread_running_{false};
    std::atomic<bool>     script_active_{false};
    std::atomic<bool>     timed_out_{false};
    std::atomic<uint32_t> timeout_count_{0};
    std::atomic<lua_State*> guarded_L_{nullptr};

    using Clock = std::chrono::steady_clock;
    std::atomic<int64_t>  start_time_ns_{0};
    int64_t               timeout_ns_ = 100'000'000;  // 100ms default
};

// ---------------------------------------------------------------------------
// ScriptWatchdogGuard — RAII wrapper for start/stop
// ---------------------------------------------------------------------------

class ScriptWatchdogGuard {
public:
    ScriptWatchdogGuard(ScriptWatchdog& wd, lua_State* L) : wd_(wd) {
        wd_.start(L);
    }
    ~ScriptWatchdogGuard() {
        wd_.stop();
    }

    ScriptWatchdogGuard(const ScriptWatchdogGuard&) = delete;
    ScriptWatchdogGuard& operator=(const ScriptWatchdogGuard&) = delete;

private:
    ScriptWatchdog& wd_;
};

} // namespace uz
