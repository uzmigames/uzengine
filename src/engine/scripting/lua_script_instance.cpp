#include "engine/scripting/lua_script_instance.h"
#include "engine/scripting/lua_vm.h"
#include "engine/core/diag.h"
#include "common/safe_string.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace uz {

LuaScriptInstance::LuaScriptInstance(LuaVM* vm) : vm_(vm) {}

LuaScriptInstance::~LuaScriptInstance() {
    if (started_) on_stop();

    // Release the environment table reference
    if (env_ref_ != -1 && vm_ && vm_->L()) {
        luaL_unref(vm_->L(), LUA_REGISTRYINDEX, env_ref_);
        env_ref_ = -1;
    }
}

bool LuaScriptInstance::load(const char* path, uint32_t nid) {
    lua_State* L = vm_->L();
    if (!L) return false;

    node_id = nid;
    str_copy(script_path_, sizeof(script_path_), path);

    // Load the script file
    if (luaL_loadfile(L, path) != 0) {
        DIAG_FMT("[LuaScript] Failed to load '%s': %s", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    // Create an isolated environment table for this script.
    // env = { __index = _G }  -- scripts can read globals (uz.*, math.*, etc.)
    lua_newtable(L);                         // env
    lua_newtable(L);                         // env meta
    lua_pushvalue(L, LUA_GLOBALSINDEX);      // _G
    lua_setfield(L, -2, "__index");          // meta.__index = _G
    lua_setmetatable(L, -2);                 // setmetatable(env, meta)

    // Store node_id in the environment so the script can read it
    lua_pushinteger(L, static_cast<lua_Integer>(nid));
    lua_setfield(L, -2, "NODE_ID");

    // Set env as the chunk's environment
    lua_pushvalue(L, -1);                    // dup env for setfenv
    lua_setfenv(L, -3);                      // setfenv(chunk, env)

    // Save env reference before running the chunk
    lua_pushvalue(L, -1);                    // dup env
    env_ref_ = luaL_ref(L, LUA_REGISTRYINDEX); // env_ref_ = ref(env)

    // Swap: stack is [chunk, env] — we need [env, chunk] for pcall below
    // Actually stack is [chunk, env]. Pop env, then pcall chunk.
    lua_pop(L, 1); // pop env, stack: [chunk]

    // Execute the chunk to define on_start/on_update/on_stop in env
    if (lua_pcall(L, 0, 0, 0) != 0) {
        DIAG_FMT("[LuaScript] Error running '%s': %s", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    // Check which lifecycle functions exist in the environment
    lua_rawgeti(L, LUA_REGISTRYINDEX, env_ref_);
    lua_getfield(L, -1, "on_start");
    has_start_ = lua_isfunction(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, -1, "on_update");
    has_update_ = lua_isfunction(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, -1, "on_stop");
    has_stop_ = lua_isfunction(L, -1);
    lua_pop(L, 1);

    lua_pop(L, 1); // pop env

    DIAG_FMT("[LuaScript] Loaded '%s' (node=%u, start=%d, update=%d, stop=%d)",
             path, nid, has_start_, has_update_, has_stop_);
    return true;
}

bool LuaScriptInstance::call_func(const char* name, int nargs) {
    lua_State* L = vm_->L();
    if (!L || env_ref_ == -1) return false;

    // Push the function from env
    lua_rawgeti(L, LUA_REGISTRYINDEX, env_ref_);
    lua_getfield(L, -1, name);
    lua_remove(L, -2); // remove env, keep function

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1 + nargs);
        return false;
    }

    // Move function before the args (args were pushed before this call)
    if (nargs > 0) {
        lua_insert(L, -(nargs + 1));
    }

    if (lua_pcall(L, nargs, 0, 0) != 0) {
        DIAG_FMT("[LuaScript] Error in %s:'%s': %s",
                 script_path_, name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

void LuaScriptInstance::on_start() {
    if (!has_start_ || started_) return;
    started_ = true;
    call_func("on_start");
}

void LuaScriptInstance::on_update(float dt) {
    if (!has_update_) return;
    lua_State* L = vm_->L();
    lua_pushnumber(L, dt);
    call_func("on_update", 1);
}

void LuaScriptInstance::on_stop() {
    if (!has_stop_ || !started_) return;
    call_func("on_stop");
    started_ = false;
}

} // namespace uz
