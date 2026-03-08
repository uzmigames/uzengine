#pragma once

// lua_coroutine_scheduler.h -- Coroutine scheduling for Lua scripts.
//
// Manages spawned Lua coroutines with wait conditions (seconds, frames,
// predicate). Each coroutine is associated with a node_id so it can be
// cancelled when the node is disabled/destroyed.
//
// Usage:
//   LuaCoroutineScheduler scheduler;
//   scheduler.init(L);
//   // In Lua: uz.coroutine.spawn(fn), uz.coroutine.wait(sec), etc.
//   // Each frame after on_update dispatch:
//   scheduler.drain(dt);
//   // On scene stop:
//   scheduler.cancel_all();

#include <cstdint>
#include <functional>
#include <variant>
#include <vector>

struct lua_State;

namespace uz {

/// Opaque handle returned by spawn(), used for cancel().
using CoroutineHandle = uint32_t;

/// Invalid handle sentinel.
inline constexpr CoroutineHandle kInvalidCoroutineHandle = 0;

// ---------------------------------------------------------------------------
// Wait conditions
// ---------------------------------------------------------------------------

struct WaitSeconds {
    float remaining;
};

struct WaitFrames {
    int remaining;
};

struct WaitUntil {
    int lua_fn_ref;  // LUA_REGISTRYINDEX reference to predicate function
};

using WaitCondition = std::variant<WaitSeconds, WaitFrames, WaitUntil>;

// ---------------------------------------------------------------------------
// Coroutine entry (internal)
// ---------------------------------------------------------------------------

struct CoroutineEntry {
    lua_State*      thread     = nullptr;
    int             thread_ref = 0;  // LUA_REGISTRYINDEX ref to keep thread alive
    WaitCondition   condition;
    uint32_t        node_id    = 0;
    CoroutineHandle handle     = kInvalidCoroutineHandle;
    bool            dead       = false;  // marked for removal
};

// ---------------------------------------------------------------------------
// Error callback — called when a coroutine errors during resume
// ---------------------------------------------------------------------------

using CoroutineErrorCallback = std::function<void(uint32_t node_id, const char* error_msg)>;

// ---------------------------------------------------------------------------
// LuaCoroutineScheduler
// ---------------------------------------------------------------------------

class LuaCoroutineScheduler {
public:
    LuaCoroutineScheduler() = default;
    ~LuaCoroutineScheduler();

    LuaCoroutineScheduler(const LuaCoroutineScheduler&) = delete;
    LuaCoroutineScheduler& operator=(const LuaCoroutineScheduler&) = delete;

    /// Initialize with the main Lua state. Must be called before spawn/drain.
    void init(lua_State* L);

    /// Register the uz.coroutine.* Lua API on the given uz table (at stack top).
    void register_lua_api(lua_State* L);

    /// Spawn a new coroutine from a Lua thread. Returns a handle for cancel().
    CoroutineHandle spawn(lua_State* thread, int thread_ref,
                          uint32_t node_id, WaitCondition condition);

    /// Advance all wait conditions and resume ready coroutines.
    /// Call once per frame after on_update dispatch.
    void drain(float dt);

    /// Cancel a specific coroutine by handle. Does not resume it.
    bool cancel(CoroutineHandle handle);

    /// Cancel all coroutines. Called on scene stop.
    void cancel_all();

    /// Cancel all coroutines belonging to a specific node.
    void cancel_by_node(uint32_t node_id);

    /// Number of active (non-dead) coroutines.
    uint32_t active_count() const;

    /// Total number of coroutines ever spawned (for diagnostics).
    uint32_t total_spawned() const { return total_spawned_; }

    /// Set error callback for coroutine runtime errors.
    void set_error_callback(CoroutineErrorCallback cb) { error_cb_ = std::move(cb); }

    /// Shutdown — release all Lua references.
    void shutdown();

private:
    void release_entry(CoroutineEntry& entry);
    void compact();

    lua_State*                   L_             = nullptr;
    std::vector<CoroutineEntry>  entries_;
    CoroutineHandle              next_handle_   = 1;
    uint32_t                     total_spawned_ = 0;
    CoroutineErrorCallback       error_cb_;
};

} // namespace uz
