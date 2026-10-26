#include "lua_vm.h"

#include "log.h"
#include "lua_sandbox.h"
#include "lua_scene_api.h"
#include "lua_material_api.h"
#include "lua_scene_io_api.h"
#include "lua_camera_api.h"
#include "lua_input_api.h"
#include "lua_renderer_api.h"
#include "lua_pathtracer_api.h"
#include "lua_cvar_api.h"
#include "lua_asset_api.h"
#include "lua_pass_inspector_api.h"
#include "lua_entity_api.h"
#include "lua_light_api.h"
#include "lua_fog_api.h"
#include "lua_skylight_api.h"
#include "lua_atmosphere_api.h"
#include "lua_clouds_api.h"
#include "lua_time_api.h"
#include "lua_math_api.h"
#include "lua_quat_api.h"
#include "lua_window_api.h"
#include "lua_vfs_api.h"
#include "lua_decal_api.h"
#include "lua_reflection_probe_api.h"
#include "lua_postprocess_api.h"
#include "lua_editor_api.h"
#include "lua_animation_api.h"

/// Lua virtual machine initialization, state management, sandbox configuration, and API registration.

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Log Category
// ─────────────────────────────────────────────────────────────────────────────

UZ_DEFINE_LOG_CATEGORY(Scripting, Info);

namespace uz {

lua_State* LuaVM::s_state = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.log / uz.log_warn / uz.log_error / uz.quit
// ─────────────────────────────────────────────────────────────────────────────

/// uz.log(msg) — log info message from Lua script.
static int LuaCFunc_Log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    UZ_LOG(Scripting, Info, "[Lua] {}", msg);
    return 0;
}

/// uz.log_warn(msg) — log warning message from Lua script.
static int LuaCFunc_LogWarn(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    UZ_LOG(Scripting, Warning, "[Lua] {}", msg);
    return 0;
}

/// uz.log_error(msg) — log error message from Lua script.
static int LuaCFunc_LogError(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    UZ_LOG(Scripting, Error, "[Lua] {}", msg);
    return 0;
}

/// uz.quit() — request engine exit.
static int LuaCFunc_Quit(lua_State* L) {
    (void)L;
    UZ_LOG(Scripting, Info, "[Lua] uz.quit() called — requesting exit");
    // Store quit flag in registry so the engine loop can check it
    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, "uz_quit_requested");
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Register `uz` global table with core API
// ─────────────────────────────────────────────────────────────────────────────

static void RegisterUzTable(lua_State* L) {
    lua_newtable(L); // uz = {}

    // uz.log, uz.log_warn, uz.log_error
    lua_pushcfunction(L, LuaCFunc_Log);
    lua_setfield(L, -2, "log");

    lua_pushcfunction(L, LuaCFunc_LogWarn);
    lua_setfield(L, -2, "log_warn");

    lua_pushcfunction(L, LuaCFunc_LogError);
    lua_setfield(L, -2, "log_error");

    // uz.quit()
    lua_pushcfunction(L, LuaCFunc_Quit);
    lua_setfield(L, -2, "quit");

    // uz.delta_time = 0, uz.total_time = 0, uz.frame_index = 0
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "delta_time");

    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "total_time");

    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "frame_index");

    lua_setglobal(L, "uz");
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool LuaVM::Init() {
    if (s_state) {
        UZ_LOG(Scripting, Warning, "LuaVM::Init() called but already initialized");
        return true;
    }

    s_state = luaL_newstate();
    if (!s_state) {
        UZ_LOG(Scripting, Error, "Failed to create Lua state");
        return false;
    }

    // Never luaL_openlibs: it opens package/debug/io/os/jit and preloads ffi,
    // each of which lets a script load native code or escape the sandbox.
    LuaSandboxOpenSafeLibs(s_state);
    RegisterUzTable(s_state);

    // Register engine sub-API tables into uz.* after the base table exists.
    LuaRegisterSceneAPI(s_state);
    LuaRegisterMaterialAPI(s_state);
    LuaRegisterSceneIoAPI(s_state);
    LuaRegisterCameraAPI(s_state);
    LuaRegisterInputAPI(s_state);
    LuaRegisterRendererAPI(s_state);
    LuaRegisterPathTracerAPI(s_state);
    LuaRegisterCvarAPI(s_state);
    LuaRegisterAssetAPI(s_state);
    LuaRegisterPassInspectorAPI(s_state);
    LuaRegisterEntityAPI(s_state);
    LuaRegisterAnimationAPI(s_state);
    LuaRegisterLightAPI(s_state);
    LuaRegisterFogAPI(s_state);
    LuaRegisterSkyLightAPI(s_state);
    LuaRegisterAtmosphereAPI(s_state);
    LuaRegisterCloudsAPI(s_state);
    LuaRegisterTimeAPI(s_state);
    LuaRegisterMathAPI(s_state);
    LuaRegisterQuatAPI(s_state);
    LuaRegisterWindowAPI(s_state);
    LuaRegisterVfsAPI(s_state);
    LuaRegisterDecalAPI(s_state);
    LuaRegisterReflectionProbeAPI(s_state);
    LuaRegisterPostProcessAPI(s_state);
    LuaRegisterEditorAPI(s_state);

    // Per-script environments read through this whitelist. Built last so the
    // uz proxy wraps the fully registered table; later uz.* additions are
    // still visible because the proxy reads the live table.
    if (!LuaSandboxBuildScriptGlobals(s_state)) {
        UZ_LOG(Scripting, Error, "LuaVM init failed — could not build the script sandbox");
        lua_close(s_state);
        s_state = nullptr;
        return false;
    }

    UZ_LOG(Scripting, Info, "LuaVM initialized (LuaJIT 2.1)");
    return true;
}

void LuaVM::Shutdown() {
    if (s_state) {
        lua_close(s_state);
        s_state = nullptr;
        UZ_LOG(Scripting, Info, "LuaVM shutdown");
    }
}

lua_State* LuaVM::GetState() {
    return s_state;
}

bool LuaVM::IsInitialized() {
    return s_state != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame globals update
// ─────────────────────────────────────────────────────────────────────────────

void LuaVM::UpdateFrameGlobals(float deltaTime, float totalTime, uint64_t frameIndex) {
    if (!s_state) return;

    lua_getglobal(s_state, "uz");
    if (!lua_istable(s_state, -1)) {
        lua_pop(s_state, 1);
        return;
    }

    lua_pushnumber(s_state, static_cast<double>(deltaTime));
    lua_setfield(s_state, -2, "delta_time");

    lua_pushnumber(s_state, static_cast<double>(totalTime));
    lua_setfield(s_state, -2, "total_time");

    lua_pushnumber(s_state, static_cast<double>(frameIndex));
    lua_setfield(s_state, -2, "frame_index");

    lua_pop(s_state, 1); // pop uz table
}

// ─────────────────────────────────────────────────────────────────────────────
// Script loading
// ─────────────────────────────────────────────────────────────────────────────

int LuaVM::LoadScript(const std::vector<uint8_t>& scriptBytes, const std::string& scriptName) {
    if (!s_state) {
        UZ_LOG(Scripting, Error, "LoadScript failed — LuaVM not initialized");
        return LUA_REFNIL;
    }

    if (scriptBytes.empty()) {
        UZ_LOG(Scripting, Error, "LoadScript failed — empty script: '{}'", scriptName);
        return LUA_REFNIL;
    }

    // Text only: LuaJIT does not verify bytecode, so a crafted binary chunk
    // corrupts memory.
    const int result = LuaSandboxLoadTextChunk(
        s_state,
        reinterpret_cast<const char*>(scriptBytes.data()),
        scriptBytes.size(),
        scriptName.c_str()
    );

    if (result != 0) {
        const char* err = lua_tostring(s_state, -1);
        UZ_LOG(Scripting, Error, "Lua compile error in '{}': {}", scriptName, err ? err : "unknown");
        lua_pop(s_state, 1);
        return LUA_REFNIL;
    }

    // Store compiled chunk in registry
    int ref = luaL_ref(s_state, LUA_REGISTRYINDEX);
    UZ_LOG(Scripting, Debug, "Script loaded: '{}' (ref={})", scriptName, ref);
    return ref;
}

void LuaVM::UnloadScript(int ref) {
    if (!s_state || ref == LUA_REFNIL || ref == LUA_NOREF) return;
    luaL_unref(s_state, LUA_REGISTRYINDEX, ref);
}

// ─────────────────────────────────────────────────────────────────────────────
// Protected call with error handling
// ─────────────────────────────────────────────────────────────────────────────

bool LuaVM::PCall(int ref, int nargs, int nresults, const char* source) {
    if (!s_state) return false;

    // Push the function from registry
    lua_rawgeti(s_state, LUA_REGISTRYINDEX, ref);

    // If there are args already on stack, the function needs to go below them
    if (nargs > 0) {
        lua_insert(s_state, -(nargs + 1));
    }

    int result = lua_pcall(s_state, nargs, nresults, 0);
    if (result != 0) {
        const char* err = lua_tostring(s_state, -1);
        UZ_LOG(Scripting, Error, "Lua runtime error in '{}': {}", source ? source : "unknown", err ? err : "unknown");
        lua_pop(s_state, 1);
        return false;
    }

    return true;
}

} // namespace uz
