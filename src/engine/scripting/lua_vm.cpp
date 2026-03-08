#include "engine/scripting/lua_vm.h"
#include "engine/scripting/script_execution_safety.h"
#include "engine/scripting/lua_module_loader.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"
#include "engine/renderer/scene/scene_params.h"
#include "engine/core/diag.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cstring>
#include <cmath>

namespace uz {

// ---------------------------------------------------------------------------
// Helper: retrieve LuaVM* from Lua registry
// ---------------------------------------------------------------------------

static const char* const kVMRegistryKey = "uz_lua_vm";

static LuaVM* get_vm(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kVMRegistryKey);
    auto* vm = static_cast<LuaVM*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return vm;
}

// ---------------------------------------------------------------------------
// uz.delta_time() / uz.total_time()
// ---------------------------------------------------------------------------

static int l_delta_time(lua_State* L) {
    lua_pushnumber(L, get_vm(L)->delta_time());
    return 1;
}

static int l_total_time(lua_State* L) {
    lua_pushnumber(L, get_vm(L)->total_time());
    return 1;
}

// ---------------------------------------------------------------------------
// uz.log(msg)
// ---------------------------------------------------------------------------

static int l_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    DIAG_FMT("[Lua] %s", msg);
    return 0;
}

// ---------------------------------------------------------------------------
// uz.scene.find_node(name) -> node_id or 0
// ---------------------------------------------------------------------------

static int l_scene_find_node(lua_State* L) {
    auto* vm = get_vm(L);
    Scene* scene = vm->scene();
    if (!scene) { lua_pushinteger(L, 0); return 1; }
    const char* name = luaL_checkstring(L, 1);
    NodeId id = scene_find_by_name(scene, name);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// ---------------------------------------------------------------------------
// uz.scene.get_position(node_id) -> x, y, z
// ---------------------------------------------------------------------------

static int l_scene_get_position(lua_State* L) {
    auto* vm = get_vm(L);
    Scene* scene = vm->scene();
    uint32_t nid = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!scene) { lua_pushnumber(L,0); lua_pushnumber(L,0); lua_pushnumber(L,0); return 3; }
    const SceneNode* node = scene_get_node(scene, nid);
    if (!node) { lua_pushnumber(L,0); lua_pushnumber(L,0); lua_pushnumber(L,0); return 3; }
    lua_pushnumber(L, node->local_transform.position.x);
    lua_pushnumber(L, node->local_transform.position.y);
    lua_pushnumber(L, node->local_transform.position.z);
    return 3;
}

// ---------------------------------------------------------------------------
// uz.scene.set_position(node_id, x, y, z)
// ---------------------------------------------------------------------------

static int l_scene_set_position(lua_State* L) {
    auto* vm = get_vm(L);
    Scene* scene = vm->scene();
    uint32_t nid = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!scene) return 0;
    SceneNode* node = scene_get_node(scene, nid);
    if (!node) return 0;
    node->local_transform.position.x = static_cast<float>(luaL_checknumber(L, 2));
    node->local_transform.position.y = static_cast<float>(luaL_checknumber(L, 3));
    node->local_transform.position.z = static_cast<float>(luaL_checknumber(L, 4));
    node->flags |= NODE_FLAG_TRANSFORM_DIRTY;
    return 0;
}

// ---------------------------------------------------------------------------
// uz.scene.get_rotation(node_id) -> rx, ry, rz (degrees)
// ---------------------------------------------------------------------------

static int l_scene_get_rotation(lua_State* L) {
    auto* vm = get_vm(L);
    Scene* scene = vm->scene();
    uint32_t nid = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!scene) { lua_pushnumber(L,0); lua_pushnumber(L,0); lua_pushnumber(L,0); return 3; }
    const SceneNode* node = scene_get_node(scene, nid);
    if (!node) { lua_pushnumber(L,0); lua_pushnumber(L,0); lua_pushnumber(L,0); return 3; }
    lua_pushnumber(L, node->local_transform.rotation.x);
    lua_pushnumber(L, node->local_transform.rotation.y);
    lua_pushnumber(L, node->local_transform.rotation.z);
    return 3;
}

// ---------------------------------------------------------------------------
// uz.scene.set_rotation(node_id, rx, ry, rz) (degrees)
// ---------------------------------------------------------------------------

static int l_scene_set_rotation(lua_State* L) {
    auto* vm = get_vm(L);
    Scene* scene = vm->scene();
    uint32_t nid = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!scene) return 0;
    SceneNode* node = scene_get_node(scene, nid);
    if (!node) return 0;
    node->local_transform.rotation.x = static_cast<float>(luaL_checknumber(L, 2));
    node->local_transform.rotation.y = static_cast<float>(luaL_checknumber(L, 3));
    node->local_transform.rotation.z = static_cast<float>(luaL_checknumber(L, 4));
    node->flags |= NODE_FLAG_TRANSFORM_DIRTY;
    return 0;
}

// ---------------------------------------------------------------------------
// uz.scene.set_scale(node_id, sx, sy, sz)
// ---------------------------------------------------------------------------

static int l_scene_set_scale(lua_State* L) {
    auto* vm = get_vm(L);
    Scene* scene = vm->scene();
    uint32_t nid = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!scene) return 0;
    SceneNode* node = scene_get_node(scene, nid);
    if (!node) return 0;
    node->local_transform.scale.x = static_cast<float>(luaL_checknumber(L, 2));
    node->local_transform.scale.y = static_cast<float>(luaL_checknumber(L, 3));
    node->local_transform.scale.z = static_cast<float>(luaL_checknumber(L, 4));
    node->flags |= NODE_FLAG_TRANSFORM_DIRTY;
    return 0;
}

// ---------------------------------------------------------------------------
// uz.camera.get_position() -> x, y, z
// ---------------------------------------------------------------------------

static int l_camera_get_position(lua_State* L) {
    auto* vm = get_vm(L);
    SceneParams* sp = vm->scene_params();
    if (!sp) { lua_pushnumber(L,0); lua_pushnumber(L,0); lua_pushnumber(L,0); return 3; }
    lua_pushnumber(L, sp->camPosX);
    lua_pushnumber(L, sp->camPosY);
    lua_pushnumber(L, sp->camPosZ);
    return 3;
}

// ---------------------------------------------------------------------------
// uz.camera.set_position(x, y, z)
// ---------------------------------------------------------------------------

static int l_camera_set_position(lua_State* L) {
    auto* vm = get_vm(L);
    SceneParams* sp = vm->scene_params();
    if (!sp) return 0;
    sp->camPosX = static_cast<float>(luaL_checknumber(L, 1));
    sp->camPosY = static_cast<float>(luaL_checknumber(L, 2));
    sp->camPosZ = static_cast<float>(luaL_checknumber(L, 3));
    return 0;
}

// ---------------------------------------------------------------------------
// uz.camera.set_yaw_pitch(yaw, pitch) (radians)
// ---------------------------------------------------------------------------

static int l_camera_set_yaw_pitch(lua_State* L) {
    auto* vm = get_vm(L);
    SceneParams* sp = vm->scene_params();
    if (!sp) return 0;
    sp->camYaw   = static_cast<float>(luaL_checknumber(L, 1));
    sp->camPitch = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

// ---------------------------------------------------------------------------
// uz.camera.get_yaw_pitch() -> yaw, pitch
// ---------------------------------------------------------------------------

static int l_camera_get_yaw_pitch(lua_State* L) {
    auto* vm = get_vm(L);
    SceneParams* sp = vm->scene_params();
    if (!sp) { lua_pushnumber(L,0); lua_pushnumber(L,0); return 2; }
    lua_pushnumber(L, sp->camYaw);
    lua_pushnumber(L, sp->camPitch);
    return 2;
}

// ---------------------------------------------------------------------------
// uz.camera.look_at(tx, ty, tz) — orient camera toward a target point
// ---------------------------------------------------------------------------

static int l_camera_look_at(lua_State* L) {
    auto* vm = get_vm(L);
    SceneParams* sp = vm->scene_params();
    if (!sp) return 0;
    float tx = static_cast<float>(luaL_checknumber(L, 1));
    float ty = static_cast<float>(luaL_checknumber(L, 2));
    float tz = static_cast<float>(luaL_checknumber(L, 3));

    float dx = tx - sp->camPosX;
    float dy = ty - sp->camPosY;
    float dz = tz - sp->camPosZ;
    float dxz = std::sqrt(dx * dx + dz * dz);

    sp->camYaw   = std::atan2(dx, dz);
    sp->camPitch = std::atan2(dy, dxz);
    return 0;
}

// ---------------------------------------------------------------------------
// Register all bindings
// ---------------------------------------------------------------------------

void LuaVM::register_bindings() {
    // Store VM pointer in registry
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, kVMRegistryKey);

    // uz = {}
    lua_newtable(L_);

    // uz.delta_time, uz.total_time, uz.log
    lua_pushcfunction(L_, l_delta_time);  lua_setfield(L_, -2, "delta_time");
    lua_pushcfunction(L_, l_total_time);  lua_setfield(L_, -2, "total_time");
    lua_pushcfunction(L_, l_log);         lua_setfield(L_, -2, "log");

    // uz.scene = { find_node, get_position, set_position, ... }
    lua_newtable(L_);
    lua_pushcfunction(L_, l_scene_find_node);   lua_setfield(L_, -2, "find_node");
    lua_pushcfunction(L_, l_scene_get_position); lua_setfield(L_, -2, "get_position");
    lua_pushcfunction(L_, l_scene_set_position); lua_setfield(L_, -2, "set_position");
    lua_pushcfunction(L_, l_scene_get_rotation); lua_setfield(L_, -2, "get_rotation");
    lua_pushcfunction(L_, l_scene_set_rotation); lua_setfield(L_, -2, "set_rotation");
    lua_pushcfunction(L_, l_scene_set_scale);    lua_setfield(L_, -2, "set_scale");
    lua_setfield(L_, -2, "scene");

    // uz.camera = { get_position, set_position, ... }
    lua_newtable(L_);
    lua_pushcfunction(L_, l_camera_get_position);  lua_setfield(L_, -2, "get_position");
    lua_pushcfunction(L_, l_camera_set_position);  lua_setfield(L_, -2, "set_position");
    lua_pushcfunction(L_, l_camera_set_yaw_pitch); lua_setfield(L_, -2, "set_yaw_pitch");
    lua_pushcfunction(L_, l_camera_get_yaw_pitch); lua_setfield(L_, -2, "get_yaw_pitch");
    lua_pushcfunction(L_, l_camera_look_at);       lua_setfield(L_, -2, "look_at");
    lua_setfield(L_, -2, "camera");

    // Set uz as global
    lua_setglobal(L_, "uz");
}

// ---------------------------------------------------------------------------
// Sandbox — strip dangerous globals
// ---------------------------------------------------------------------------

void LuaVM::apply_sandbox() {
    const char* strip[] = {
        "os", "io", "loadfile", "dofile", "package", "require",
        "rawget", "rawset", "rawequal", "rawlen",
        "debug", "newproxy", "collectgarbage",
        nullptr
    };
    for (int i = 0; strip[i]; ++i) {
        lua_pushnil(L_);
        lua_setglobal(L_, strip[i]);
    }
}

// ---------------------------------------------------------------------------
// Module system
// ---------------------------------------------------------------------------

void LuaVM::set_project_root(const char* root) {
    if (L_ && root) {
        lua_module_loader_install(L_, root);
    }
}

void LuaVM::invalidate_module(const char* module_name) {
    if (L_ && module_name) {
        lua_module_loader_invalidate(L_, module_name);
    }
}

// ---------------------------------------------------------------------------
// LuaVM lifecycle
// ---------------------------------------------------------------------------

LuaVM::LuaVM() = default;

LuaVM::~LuaVM() {
    shutdown();
}

bool LuaVM::init() {
    return init(ScriptingConfig{});
}

bool LuaVM::init(const ScriptingConfig& config) {
    if (L_) return true;

    config_ = config;

    // Configure memory allocator
    allocator_.limit = static_cast<size_t>(config_.memory_limit_mb) * 1024 * 1024;
    allocator_.used.store(0, std::memory_order_relaxed);

    // Create state with custom allocator
    L_ = lua_newstate(LuaAllocator::alloc, &allocator_);
    if (!L_) return false;

    luaL_openlibs(L_);
    apply_sandbox();
    register_bindings();

    // Install instruction budget hook
    lua_reset_instruction_hook(L_, config_.instruction_budget);

    // Install global guard (__newindex on _G)
    lua_install_global_guard(L_, &config_);

    DIAG_FMT("[LuaVM] Initialized (LuaJIT, budget=%d, mem=%dMB)",
             config_.instruction_budget, config_.memory_limit_mb);
    return true;
}

void LuaVM::reset_instruction_hook() {
    if (L_) {
        lua_reset_instruction_hook(L_, config_.instruction_budget);
    }
}

void LuaVM::shutdown() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
        DIAG_FMT("[LuaVM] Shutdown");
    }
}

} // namespace uz
