/// Script system — runs per-entity Lua scripts through the on_start/on_update/on_stop lifecycle.

#include "script_system.h"

#include "script_component.h"
#include "lua_vm.h"
#include "log.h"
#include "world.h"
#include "entity.h"
#include "../asset/vfs.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Log category — declared in lua_vm.cpp, only declared here.
// ─────────────────────────────────────────────────────────────────────────────

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Static state
// ─────────────────────────────────────────────────────────────────────────────

World* ScriptSystem::s_world    = nullptr;
IVFS*  ScriptSystem::s_vfs      = nullptr;
bool   ScriptSystem::s_playMode = false; ///< Scripts are dormant in editor by default.

// ─────────────────────────────────────────────────────────────────────────────
// Entity <-> lightuserdata encoding
//
// The same encoding used in lua_scene_api.cpp.  Those helpers are file-scoped
// statics in that TU and are therefore not linkable from here.  Duplicating the
// trivial bit-pack is preferable to making the encoding part of a shared header,
// which would pull <cstring> into every consumer.
// ─────────────────────────────────────────────────────────────────────────────

static_assert(sizeof(void*) >= 8,
    "uz::Entity requires 64-bit pointers for lightuserdata encoding");

/// Packs an Entity into a void* for use as Lua lightuserdata.
///
/// High 32 bits = entity.id, low 32 bits = entity.generation.
[[nodiscard]] static void* EntityToUserdata(Entity e) noexcept {
    const uint64_t packed = (static_cast<uint64_t>(e.id) << 32)
                          | static_cast<uint64_t>(e.generation);
    void* p;
    static_assert(sizeof(packed) == sizeof(p));
    std::memcpy(&p, &packed, sizeof(p));
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a sandboxed environment table on top of the Lua stack and returns its
/// registry reference.
///
/// The environment is an empty table whose `__index` metamethod points to `_G`,
/// so scripts can read all standard globals while any globals they SET are
/// isolated to their own table.
///
/// Stack effect: zero (table is stored in registry and popped).
///
/// # Returns
/// - A valid registry ref on success.
/// - `LUA_REFNIL` if the Lua state is not available.
[[nodiscard]] static int CreateScriptEnv(lua_State* L) {
    // env = {}
    lua_newtable(L);                       // [env]

    // mt = {}; mt.__index = _G
    lua_newtable(L);                       // [env, mt]
    lua_pushvalue(L, LUA_GLOBALSINDEX);    // [env, mt, _G]  (LuaJIT Lua 5.1 API)
    lua_setfield(L, -2, "__index");        // [env, mt]   mt.__index = _G
    lua_setmetatable(L, -2);              // [env]        setmetatable(env, mt)

    const int ref = luaL_ref(L, LUA_REGISTRYINDEX); // []
    return ref;
}

/// Applies the per-script environment to the chunk function so that the chunk's
/// globals refer to `env` instead of `_G`.
///
/// Uses `lua_setfenv` (Lua 5.1 / LuaJIT).  The chunk must be on top of the
/// stack.  After this call, the stack is unchanged (chunk remains on top).
///
/// # Parameters
/// - `L`:      Active Lua state.
/// - `envRef`: Registry reference to the environment table.
///
/// # Returns
/// `true` on success, `false` if `lua_setfenv` failed (e.g. chunk is a
/// non-function value — should never happen in practice).
[[nodiscard]] static bool ApplyEnvToChunk(lua_State* L, int envRef) {
    // Push env from registry
    lua_rawgeti(L, LUA_REGISTRYINDEX, envRef);  // [chunk, env]
    // lua_setfenv pops the env from the top and applies it to the function at
    // the given index, then returns 1 on success.
    const int ok = lua_setfenv(L, -2);           // [chunk]
    if (!ok) {
        UZ_LOG(Scripting, Error, "ScriptSystem: lua_setfenv failed — chunk is not a function");
        return false;
    }
    return true;
}

/// Calls a named lifecycle hook (`on_start`, `on_update`, `on_stop`) from the
/// per-script environment.
///
/// Looks up `funcName` in the environment table.  If the field exists and is a
/// function, calls it with the provided arguments.  All arguments must already
/// be on the Lua stack ABOVE the frame set up by this function.  This function
/// manages the env table retrieval internally.
///
/// # Stack contract
/// Caller must have pushed `nargs` arguments onto the stack before calling.
/// Those arguments are consumed by the call.  The function leaves the stack
/// balanced (no net change) when it returns.
///
/// # Parameters
/// - `L`:        Active Lua state.
/// - `envRef`:   Registry reference to this script's environment table.
/// - `funcName`: Name of the Lua function to call (e.g., `"on_start"`).
/// - `nargs`:    Number of arguments already on the stack.
/// - `source`:   Human-readable path used in error messages.
///
/// # Returns
/// - `true` if the function was found and called without a Lua error.
/// - `false` if the function is missing (not an error), or if a Lua error
///   occurred (error is logged, stack is cleaned up).
[[nodiscard]] static bool CallHook(
    lua_State* L,
    int envRef,
    const char* funcName,
    int nargs,
    const char* source)
{
    // Retrieve the environment table
    lua_rawgeti(L, LUA_REGISTRYINDEX, envRef);   // [args..., env]

    // Look up the hook function in the env
    lua_getfield(L, -1, funcName);               // [args..., env, func_or_nil]
    lua_remove(L, -2);                           // [args..., func_or_nil]   remove env

    if (!lua_isfunction(L, -1)) {
        // Hook not defined — not an error, just skip.
        lua_pop(L, 1);       // pop nil/non-function
        // Also pop the arguments the caller pre-pushed.
        if (nargs > 0) lua_pop(L, nargs);
        return false;
    }

    // Move function below the pre-pushed args
    if (nargs > 0) {
        lua_insert(L, -(nargs + 1));  // [func, args...]
    }

    const int result = lua_pcall(L, nargs, 0, 0);
    if (result != 0) {
        const char* err = lua_tostring(L, -1);
        UZ_LOG(Scripting, Error,
            "ScriptSystem: Lua error in '{}::{}': {}",
            source, funcName, err ? err : "unknown");
        lua_pop(L, 1); // pop error message
        return false;
    }

    return true;
}

/// Loads a single CScript that has not yet been initialized.
///
/// Steps:
/// 1. Read bytes from VFS.
/// 2. Compile with LuaVM::LoadScript — on failure, leaves luaRef = LUA_REFNIL.
/// 3. Create per-entity env table.
/// 4. Push chunk from registry, apply env via setfenv, execute to define hooks.
/// 5. Call on_start(entity) if defined.
/// 6. Mark script.started = true.
///
/// If any step fails the error is logged.  The script is left in a partially
/// initialized state so it will NOT be retried (luaRef != LUA_REFNIL after
/// compile, or started stays false after a failed on_start).
static void LoadAndStartScript(lua_State* L, IVFS* vfs, CScript& script, Entity entity) {
    // ── 1. Read script bytes from VFS ────────────────────────────────────────
    const std::vector<uint8_t> bytes = vfs->Read(script.scriptPath);
    if (bytes.empty()) {
        UZ_LOG(Scripting, Error,
            "ScriptSystem: failed to read script from VFS: path='{}'",
            script.scriptPath);
        // Leave luaRef = LUA_REFNIL so we don't retry every frame.
        // Use a non-REFNIL sentinel via a dummy ref so the "needs loading" guard
        // (luaRef == LUA_REFNIL && !started) fails on subsequent frames.
        // We do this by setting started = true on error so Update() ignores it.
        script.started = true;
        return;
    }

    // ── 2. Compile script ────────────────────────────────────────────────────
    script.luaRef = LuaVM::LoadScript(bytes, script.scriptPath);
    if (script.luaRef == LUA_REFNIL) {
        // LuaVM already logged the error.
        script.started = true; // suppress retry
        return;
    }

    // ── 3. Create per-entity environment table ───────────────────────────────
    script.envRef = CreateScriptEnv(L);
    if (script.envRef == LUA_REFNIL) {
        UZ_LOG(Scripting, Error,
            "ScriptSystem: failed to create env for script='{}'", script.scriptPath);
        LuaVM::UnloadScript(script.luaRef);
        script.luaRef  = LUA_REFNIL;
        script.started = true;
        return;
    }

    // ── 4. Push chunk, apply env, execute to register hooks ──────────────────
    lua_rawgeti(L, LUA_REGISTRYINDEX, script.luaRef); // [chunk]

    if (!ApplyEnvToChunk(L, script.envRef)) {
        lua_pop(L, 1); // pop chunk
        luaL_unref(L, LUA_REGISTRYINDEX, script.envRef);
        LuaVM::UnloadScript(script.luaRef);
        script.envRef  = LUA_REFNIL;
        script.luaRef  = LUA_REFNIL;
        script.started = true;
        return;
    }

    // Execute the chunk to run top-level code and populate the env with hooks.
    const int execResult = lua_pcall(L, 0, 0, 0);
    if (execResult != 0) {
        const char* err = lua_tostring(L, -1);
        UZ_LOG(Scripting, Error,
            "ScriptSystem: error executing script '{}': {}",
            script.scriptPath, err ? err : "unknown");
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, script.envRef);
        LuaVM::UnloadScript(script.luaRef);
        script.envRef  = LUA_REFNIL;
        script.luaRef  = LUA_REFNIL;
        script.started = true;
        return;
    }

    UZ_LOG(Scripting, Debug,
        "ScriptSystem: script loaded and executed: path='{}' entity={}",
        script.scriptPath, entity.id);

    // ── 5. Call on_start(entity) ─────────────────────────────────────────────
    lua_pushlightuserdata(L, EntityToUserdata(entity)); // push arg
    CallHook(L, script.envRef, "on_start", 1, script.scriptPath.c_str());

    // ── 6. Mark as started regardless of on_start outcome ────────────────────
    script.started = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void ScriptSystem::Initialize(World* world, IVFS* vfs) {
    s_world = world;
    s_vfs   = vfs;
    UZ_LOG(Scripting, Info, "ScriptSystem initialized");
}

void ScriptSystem::SetPlayMode(bool enabled) {
    if (s_playMode == enabled) return;
    s_playMode = enabled;
    // Deliberately Warning-level so the state transition is always visible
    // in runtime logs (default level is Error). Play-mode entry/exit is
    // infrequent and operationally significant — the user needs to be able
    // to confirm whether scripts are running when diagnosing input issues.
    UZ_LOG(Scripting, Warning, "ScriptSystem: play mode {}", enabled ? "ON" : "OFF");
}

bool ScriptSystem::IsPlayMode() {
    return s_playMode;
}

void ScriptSystem::Update(float deltaTime) {
    // Editor-mode gate: never tick gameplay scripts while the author is
    // editing the scene. The built-in FlyCameraController provides camera
    // navigation without any scripting; ECS Camera components and scene
    // author-time data are still read (see SceneRenderer::Gather), but no
    // on_start / on_update hooks fire. Entering Play Mode flips the flag.
    if (!s_playMode) {
        return;
    }

    if (!s_world || !s_vfs) {
        UZ_LOG(Scripting, Warning,
            "ScriptSystem::Update called but system was not initialized");
        return;
    }

    if (!LuaVM::IsInitialized()) {
        UZ_LOG(Scripting, Warning,
            "ScriptSystem::Update called but LuaVM is not initialized");
        return;
    }

    lua_State* L = LuaVM::GetState();

    for (auto [enttEntity, script] : s_world->View<CScript>().each()) {
        // Reconstruct the uz::Entity handle from the EnTT entity.
        // Generation mapping mirrors lua_scene_api.cpp: Entity::generation = entt_version + 1.
        const uint32_t slot = static_cast<uint32_t>(entt::to_entity(enttEntity));
        const uint32_t gen  = static_cast<uint32_t>(
            s_world->GetRegistry().current(enttEntity)) + 1u;
        Entity entity;
        entity.id         = slot;
        entity.generation = gen;

        const bool needsLoad = (script.luaRef == LUA_REFNIL && !script.started);

        if (needsLoad) {
            // Script not yet loaded — load, exec, and call on_start.
            LoadAndStartScript(L, s_vfs, script, entity);
        } else if (script.started && script.luaRef != LUA_REFNIL) {
            // Script running — call on_update(entity, dt).
            lua_pushlightuserdata(L, EntityToUserdata(entity));
            lua_pushnumber(L, static_cast<double>(deltaTime));
            CallHook(L, script.envRef, "on_update", 2, script.scriptPath.c_str());
        }
        // else: script.started == true && script.luaRef == LUA_REFNIL
        // means the script failed to load — silently skip.
    }
}

void ScriptSystem::Shutdown() {
    if (!s_world) {
        UZ_LOG(Scripting, Info, "ScriptSystem::Shutdown — already uninitialized");
        return;
    }

    lua_State* L = LuaVM::IsInitialized() ? LuaVM::GetState() : nullptr;

    for (auto [enttEntity, script] : s_world->View<CScript>().each()) {
        if (script.luaRef == LUA_REFNIL) continue; // never loaded or load failed

        if (L) {
            // Reconstruct entity handle for on_stop argument.
            const uint32_t slot = static_cast<uint32_t>(entt::to_entity(enttEntity));
            const uint32_t gen  = static_cast<uint32_t>(
                s_world->GetRegistry().current(enttEntity)) + 1u;
            Entity entity;
            entity.id         = slot;
            entity.generation = gen;

            lua_pushlightuserdata(L, EntityToUserdata(entity));
            CallHook(L, script.envRef, "on_stop", 1, script.scriptPath.c_str());

            // Release env table from registry.
            if (script.envRef != LUA_REFNIL) {
                luaL_unref(L, LUA_REGISTRYINDEX, script.envRef);
            }
        }

        // Release compiled chunk.
        LuaVM::UnloadScript(script.luaRef);

        script.luaRef  = LUA_REFNIL;
        script.envRef  = LUA_REFNIL;
        script.started = false;
    }

    UZ_LOG(Scripting, Info, "ScriptSystem shutdown complete");

    s_world = nullptr;
    s_vfs   = nullptr;
}

} // namespace uz
