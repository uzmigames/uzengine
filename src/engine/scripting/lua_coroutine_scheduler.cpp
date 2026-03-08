#include "engine/scripting/lua_coroutine_scheduler.h"
#include "engine/core/diag.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>

namespace uz {

// ---------------------------------------------------------------------------
// Wait type tags — yielded as first argument to distinguish conditions
// ---------------------------------------------------------------------------

static constexpr int kWaitTagSeconds = 1;
static constexpr int kWaitTagFrames  = 2;
static constexpr int kWaitTagUntil   = 3;

// ---------------------------------------------------------------------------
// Registry keys
// ---------------------------------------------------------------------------

static const char* const kSchedulerRegistryKey = "uz_coroutine_scheduler";
static const char* const kCurrentNodeIdKey     = "uz_current_node_id";

static LuaCoroutineScheduler* get_scheduler(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kSchedulerRegistryKey);
    auto* sched = static_cast<LuaCoroutineScheduler*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return sched;
}

// ---------------------------------------------------------------------------
// Helper: read wait condition from thread stack after LUA_YIELD
//
// The wait functions yield: tag [, value]
//   WaitSeconds: tag=1, value=float
//   WaitFrames:  tag=2, value=int
//   WaitUntil:   tag=3, value=fn_ref (stored in registry by wait_until before yield)
// ---------------------------------------------------------------------------

static WaitCondition read_wait_condition(lua_State* main_L, lua_State* thread) {
    int n = lua_gettop(thread);
    if (n == 0) {
        return WaitFrames{1};  // default: wait 1 frame
    }

    int tag = static_cast<int>(lua_tointeger(thread, 1));

    switch (tag) {
    case kWaitTagSeconds: {
        float secs = (n >= 2) ? static_cast<float>(lua_tonumber(thread, 2)) : 0.0f;
        lua_settop(thread, 0);
        return WaitSeconds{secs};
    }
    case kWaitTagFrames: {
        int frames = (n >= 2) ? static_cast<int>(lua_tointeger(thread, 2)) : 1;
        lua_settop(thread, 0);
        return WaitFrames{frames};
    }
    case kWaitTagUntil: {
        // fn_ref was stored in the registry by wait_until before yielding.
        // The ref index is passed as second yield arg.
        int fn_ref = (n >= 2) ? static_cast<int>(lua_tointeger(thread, 2)) : LUA_NOREF;
        lua_settop(thread, 0);
        return WaitUntil{fn_ref};
    }
    default:
        lua_settop(thread, 0);
        return WaitFrames{1};
    }
}

// ---------------------------------------------------------------------------
// Lua API: uz.coroutine.wait(seconds)
// ---------------------------------------------------------------------------

static int l_coroutine_wait(lua_State* L) {
    float seconds = static_cast<float>(luaL_checknumber(L, 1));
    if (seconds < 0.0f) seconds = 0.0f;

    lua_pushinteger(L, kWaitTagSeconds);
    lua_pushnumber(L, seconds);
    return lua_yield(L, 2);
}

// ---------------------------------------------------------------------------
// Lua API: uz.coroutine.wait_frames(n)
// ---------------------------------------------------------------------------

static int l_coroutine_wait_frames(lua_State* L) {
    int frames = static_cast<int>(luaL_checkinteger(L, 1));
    if (frames < 0) frames = 0;

    lua_pushinteger(L, kWaitTagFrames);
    lua_pushinteger(L, frames);
    return lua_yield(L, 2);
}

// ---------------------------------------------------------------------------
// Lua API: uz.coroutine.wait_until(fn)
// ---------------------------------------------------------------------------

static int l_coroutine_wait_until(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    // Store the predicate in the main state's registry so it survives across yields.
    // We need to get main state for luaL_ref — but the function is on this thread's stack.
    // Move it to main state, ref it, then yield with the ref index.
    lua_State* main_L = L;

    // In LuaJIT, luaL_ref works fine from a coroutine thread.
    lua_pushvalue(L, 1);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushinteger(L, kWaitTagUntil);
    lua_pushinteger(L, fn_ref);
    return lua_yield(L, 2);
}

// ---------------------------------------------------------------------------
// Lua API: uz.coroutine.spawn(fn) -> handle
// ---------------------------------------------------------------------------

static int l_coroutine_spawn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    auto* sched = get_scheduler(L);
    if (!sched) {
        return luaL_error(L, "uz.coroutine.spawn: scheduler not initialized");
    }

    uint32_t node_id = 0;
    lua_getfield(L, LUA_REGISTRYINDEX, kCurrentNodeIdKey);
    if (!lua_isnil(L, -1)) {
        node_id = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);

    // Create a new Lua thread
    lua_State* thread = lua_newthread(L);
    int thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Copy the function to the thread stack
    lua_pushvalue(L, 1);
    lua_xmove(L, thread, 1);

    // Resume — runs until first yield or completion
    int status = lua_resume(thread, 0);

    if (status == LUA_YIELD) {
        WaitCondition cond = read_wait_condition(L, thread);
        CoroutineHandle h = sched->spawn(thread, thread_ref, node_id, cond);
        lua_pushinteger(L, static_cast<lua_Integer>(h));
        return 1;
    } else if (status == 0) {
        // Completed immediately
        luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
        lua_pushinteger(L, 0);
        return 1;
    } else {
        // Error
        const char* err = lua_tostring(thread, -1);
        DIAG_FMT("[LuaCoroutine] spawn error (node %u): %s",
                 node_id, err ? err : "unknown");
        luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
        lua_pushinteger(L, 0);
        return 1;
    }
}

// ---------------------------------------------------------------------------
// Lua API: uz.coroutine.cancel(handle) -> bool
// ---------------------------------------------------------------------------

static int l_coroutine_cancel(lua_State* L) {
    auto handle = static_cast<CoroutineHandle>(luaL_checkinteger(L, 1));

    auto* sched = get_scheduler(L);
    if (!sched) {
        return luaL_error(L, "uz.coroutine.cancel: scheduler not initialized");
    }

    bool ok = sched->cancel(handle);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// LuaCoroutineScheduler lifecycle
// ---------------------------------------------------------------------------

LuaCoroutineScheduler::~LuaCoroutineScheduler() {
    shutdown();
}

void LuaCoroutineScheduler::init(lua_State* L) {
    L_ = L;
    if (L_) {
        lua_pushlightuserdata(L_, this);
        lua_setfield(L_, LUA_REGISTRYINDEX, kSchedulerRegistryKey);
    }
}

void LuaCoroutineScheduler::register_lua_api(lua_State* L) {
    // Expects the "uz" table at the top of the stack.
    lua_newtable(L);

    lua_pushcfunction(L, l_coroutine_wait);
    lua_setfield(L, -2, "wait");

    lua_pushcfunction(L, l_coroutine_wait_frames);
    lua_setfield(L, -2, "wait_frames");

    lua_pushcfunction(L, l_coroutine_wait_until);
    lua_setfield(L, -2, "wait_until");

    lua_pushcfunction(L, l_coroutine_spawn);
    lua_setfield(L, -2, "spawn");

    lua_pushcfunction(L, l_coroutine_cancel);
    lua_setfield(L, -2, "cancel");

    lua_setfield(L, -2, "coroutine");
}

void LuaCoroutineScheduler::shutdown() {
    if (L_) {
        cancel_all();
        lua_pushnil(L_);
        lua_setfield(L_, LUA_REGISTRYINDEX, kSchedulerRegistryKey);
    }
    L_ = nullptr;
    entries_.clear();
    next_handle_ = 1;
    total_spawned_ = 0;
}

// ---------------------------------------------------------------------------
// spawn
// ---------------------------------------------------------------------------

CoroutineHandle LuaCoroutineScheduler::spawn(lua_State* thread, int thread_ref,
                                              uint32_t node_id,
                                              WaitCondition condition) {
    CoroutineHandle h = next_handle_++;
    entries_.push_back(CoroutineEntry{
        thread,
        thread_ref,
        condition,
        node_id,
        h,
        false
    });
    ++total_spawned_;
    return h;
}

// ---------------------------------------------------------------------------
// drain
// ---------------------------------------------------------------------------

void LuaCoroutineScheduler::drain(float dt) {
    if (!L_) return;

    // Process by index — new spawns during resume get appended and will be
    // processed next frame (n is captured before the loop).
    size_t n = entries_.size();
    for (size_t i = 0; i < n; ++i) {
        auto& entry = entries_[i];
        if (entry.dead) continue;

        bool ready = false;

        if (auto* ws = std::get_if<WaitSeconds>(&entry.condition)) {
            ws->remaining -= dt;
            if (ws->remaining <= 0.0f) ready = true;
        } else if (auto* wf = std::get_if<WaitFrames>(&entry.condition)) {
            wf->remaining -= 1;
            if (wf->remaining <= 0) ready = true;
        } else if (auto* wu = std::get_if<WaitUntil>(&entry.condition)) {
            // Evaluate predicate via pcall on the MAIN state
            lua_rawgeti(L_, LUA_REGISTRYINDEX, wu->lua_fn_ref);
            if (lua_pcall(L_, 0, 1, 0) == 0) {
                ready = lua_toboolean(L_, -1) != 0;
                lua_pop(L_, 1);
            } else {
                const char* err = lua_tostring(L_, -1);
                DIAG_FMT("[LuaCoroutine] wait_until predicate error (node %u): %s",
                         entry.node_id, err ? err : "unknown");
                if (error_cb_) {
                    error_cb_(entry.node_id, err ? err : "unknown");
                }
                lua_pop(L_, 1);
                release_entry(entry);
                entry.dead = true;
                continue;
            }
        }

        if (!ready) continue;

        // Resume the coroutine
        int status = lua_resume(entry.thread, 0);

        if (status == LUA_YIELD) {
            // Re-yielded — read new wait condition
            // Release old WaitUntil ref if we're transitioning away from it
            if (auto* old_wu = std::get_if<WaitUntil>(&entry.condition)) {
                luaL_unref(L_, LUA_REGISTRYINDEX, old_wu->lua_fn_ref);
            }
            entry.condition = read_wait_condition(L_, entry.thread);
        } else if (status == 0) {
            // Finished normally
            release_entry(entry);
            entry.dead = true;
        } else {
            // Runtime error in coroutine
            const char* err = lua_tostring(entry.thread, -1);
            DIAG_FMT("[LuaCoroutine] runtime error (node %u): %s",
                     entry.node_id, err ? err : "unknown");
            if (error_cb_) {
                error_cb_(entry.node_id, err ? err : "unknown");
            }
            release_entry(entry);
            entry.dead = true;
        }
    }

    compact();
}

// ---------------------------------------------------------------------------
// cancel / cancel_all / cancel_by_node
// ---------------------------------------------------------------------------

bool LuaCoroutineScheduler::cancel(CoroutineHandle handle) {
    for (auto& entry : entries_) {
        if (!entry.dead && entry.handle == handle) {
            release_entry(entry);
            entry.dead = true;
            compact();
            return true;
        }
    }
    return false;
}

void LuaCoroutineScheduler::cancel_all() {
    for (auto& entry : entries_) {
        if (!entry.dead) {
            release_entry(entry);
            entry.dead = true;
        }
    }
    entries_.clear();
}

void LuaCoroutineScheduler::cancel_by_node(uint32_t node_id) {
    for (auto& entry : entries_) {
        if (!entry.dead && entry.node_id == node_id) {
            release_entry(entry);
            entry.dead = true;
        }
    }
    compact();
}

// ---------------------------------------------------------------------------
// active_count
// ---------------------------------------------------------------------------

uint32_t LuaCoroutineScheduler::active_count() const {
    uint32_t count = 0;
    for (const auto& e : entries_) {
        if (!e.dead) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void LuaCoroutineScheduler::release_entry(CoroutineEntry& entry) {
    if (!L_) return;

    // Release WaitUntil function reference
    if (auto* wu = std::get_if<WaitUntil>(&entry.condition)) {
        if (wu->lua_fn_ref != LUA_NOREF && wu->lua_fn_ref != LUA_REFNIL) {
            luaL_unref(L_, LUA_REGISTRYINDEX, wu->lua_fn_ref);
        }
    }

    // Release thread reference
    if (entry.thread_ref != 0) {
        luaL_unref(L_, LUA_REGISTRYINDEX, entry.thread_ref);
        entry.thread_ref = 0;
    }
    entry.thread = nullptr;
}

void LuaCoroutineScheduler::compact() {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [](const CoroutineEntry& e) { return e.dead; }),
        entries_.end()
    );
}

} // namespace uz
