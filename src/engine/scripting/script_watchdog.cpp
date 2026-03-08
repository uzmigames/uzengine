#include "engine/scripting/script_watchdog.h"
#include "engine/scripting/script_execution_safety.h"
#include "engine/core/diag.h"

extern "C" {
#include <lua.h>
}

namespace uz {

ScriptWatchdog::~ScriptWatchdog() {
    shutdown();
}

void ScriptWatchdog::init(int timeout_ms) {
    if (thread_running_.load(std::memory_order_relaxed)) return;

    timeout_ns_ = static_cast<int64_t>(timeout_ms) * 1'000'000LL;
    shutdown_.store(false, std::memory_order_release);
    thread_running_.store(true, std::memory_order_release);
    thread_ = std::thread(&ScriptWatchdog::watchdog_loop, this);

    DIAG_FMT("[ScriptWatchdog] Started (timeout=%d ms)", timeout_ms);
}

void ScriptWatchdog::shutdown() {
    if (!thread_running_.load(std::memory_order_relaxed)) return;

    shutdown_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    thread_running_.store(false, std::memory_order_release);
    script_active_.store(false, std::memory_order_release);

    DIAG_FMT("[ScriptWatchdog] Shutdown");
}

void ScriptWatchdog::start(lua_State* L) {
    timed_out_.store(false, std::memory_order_release);
    guarded_L_.store(L, std::memory_order_release);

    auto now = Clock::now().time_since_epoch();
    start_time_ns_.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(),
        std::memory_order_release);

    script_active_.store(true, std::memory_order_release);
}

void ScriptWatchdog::stop() {
    script_active_.store(false, std::memory_order_release);
    guarded_L_.store(nullptr, std::memory_order_release);
}

void ScriptWatchdog::watchdog_loop() {
    while (!shutdown_.load(std::memory_order_acquire)) {
        if (script_active_.load(std::memory_order_acquire)) {
            auto now = Clock::now().time_since_epoch();
            int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            int64_t start = start_time_ns_.load(std::memory_order_acquire);
            int64_t elapsed = now_ns - start;

            if (elapsed > timeout_ns_) {
                lua_State* L = guarded_L_.load(std::memory_order_acquire);
                if (L && script_active_.load(std::memory_order_acquire)) {
                    // Force the Lua VM to error on the very next instruction
                    lua_sethook(L, lua_immediate_abort_hook,
                                LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
                    timed_out_.store(true, std::memory_order_release);
                    timeout_count_.fetch_add(1, std::memory_order_relaxed);
                    script_active_.store(false, std::memory_order_release);

                    DIAG_FMT("[ScriptWatchdog] Timeout fired after %lld ms",
                             static_cast<long long>(elapsed / 1'000'000));
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace uz
