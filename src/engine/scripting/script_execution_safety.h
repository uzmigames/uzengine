#pragma once

// script_execution_safety.h -- Execution safety primitives for Lua scripting.
//
// Provides:
//   - ScriptingConfig: configurable limits (instruction budget, timeout, memory)
//   - LuaAllocator: custom allocator with per-VM memory cap
//   - Instruction hook for CPU budget enforcement
//   - Sandbox escape detection (__newindex on _G)

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct lua_State;
struct lua_Debug;

namespace uz {

// ---------------------------------------------------------------------------
// ScriptingConfig — project-level scripting limits
// ---------------------------------------------------------------------------

struct ScriptingConfig {
    int         instruction_budget = 5'000'000;  // Per on_update call
    int         timeout_ms         = 100;         // Watchdog wall-clock timeout
    int         memory_limit_mb    = 32;          // Per-VM memory cap
    std::string sandbox_globals    = "block";     // "block" or "warn"
    std::vector<std::string> allowed_globals;     // Whitelist for __newindex
};

// ---------------------------------------------------------------------------
// LuaAllocator — custom allocator with memory tracking and cap
// ---------------------------------------------------------------------------

struct LuaAllocator {
    std::atomic<size_t> used{0};
    size_t              limit = 32ULL * 1024 * 1024;  // 32 MB default

    /// Lua allocator callback (lua_Alloc signature).
    /// ud must point to a LuaAllocator instance.
    static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);

    /// Current bytes allocated.
    size_t bytes_used() const { return used.load(std::memory_order_relaxed); }
};

// ---------------------------------------------------------------------------
// Instruction hook — installed via lua_sethook(L, ..., LUA_MASKCOUNT, budget)
// ---------------------------------------------------------------------------

/// Hook callback that aborts the script when the instruction budget is exceeded.
void lua_instruction_hook(lua_State* L, lua_Debug* ar);

/// Reset the instruction hook with a fresh budget. Call before each on_update.
void lua_reset_instruction_hook(lua_State* L, int budget);

// ---------------------------------------------------------------------------
// Immediate abort hook — installed by the watchdog to force-terminate
// ---------------------------------------------------------------------------

/// Hook callback that immediately raises an error. Used by ScriptWatchdog.
void lua_immediate_abort_hook(lua_State* L, lua_Debug* ar);

// ---------------------------------------------------------------------------
// Sandbox escape detection — __newindex on _G
// ---------------------------------------------------------------------------

/// Install a metatable on _G that blocks (or warns on) new global assignments.
/// config controls "block" vs "warn" mode and the allowed_globals whitelist.
void lua_install_global_guard(lua_State* L, const ScriptingConfig* config);

} // namespace uz
