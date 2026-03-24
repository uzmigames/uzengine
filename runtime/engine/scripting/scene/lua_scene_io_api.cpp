#include "lua_scene_io_api.h"

#include "log.h"
#include "world.h"
#include "scene_loader.h"
#include "vfs.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdio>
#include <vector>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key constants
// ─────────────────────────────────────────────────────────────────────────────

/// Key used to store the World* in the Lua registry.
static constexpr const char* kWorldRegistryKey = "uz_world";

/// Key used to store the IVFS* in the Lua registry.
static constexpr const char* kVfsRegistryKey = "uz_vfs";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the World* from the Lua registry. Returns null if not set.
[[nodiscard]] static World* GetWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<World*>(p);
}

/// Retrieves the IVFS* from the Lua registry. Returns null if not set.
[[nodiscard]] static IVFS* GetVFS(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kVfsRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<IVFS*>(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.scene scene load/save
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.load_scene(path) -> bool
///
/// Destroys all existing entities in the world and loads a new scene from the
/// given VFS-relative path (e.g. `"scenes/demo.uzscene"`).
///
/// The file is read through the VFS registered via `LuaSetVFS`. The world is
/// cleared via `World::Clear()` before parsing begins, so partial failures
/// leave the world empty rather than in a mixed state.
///
/// # Arguments
/// * `path` (string) — VFS-relative path to the `.uzscene` file.
///
/// # Returns
/// - `true` if at least one entity was successfully created.
/// - `false` if the World or VFS is not set, the file is not found, or the
///   JSON is malformed (all failures are logged at Warning/Error level).
static int SceneLoadScene(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.load_scene: World not set");
        lua_pushboolean(L, 0);
        return 1;
    }

    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        UZ_LOG(Scripting, Warning, "uz.scene.load_scene: VFS not set");
        lua_pushboolean(L, 0);
        return 1;
    }

    UZ_LOG(Scripting, Info, "uz.scene.load_scene: loading '{}'", path);

    const std::vector<uint8_t> data = vfs->Read(path);
    if (data.empty()) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.load_scene: file not found or empty — path='{}'", path);
        lua_pushboolean(L, 0);
        return 1;
    }

    world->Clear();

    const size_t count = uz::LoadScene(*world, data, path);
    if (count == 0) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.load_scene: no entities loaded from '{}'", path);
        lua_pushboolean(L, 0);
        return 1;
    }

    UZ_LOG(Scripting, Info,
        "uz.scene.load_scene: loaded {} entities from '{}'", count, path);
    lua_pushboolean(L, 1);
    return 1;
}

/// uz.scene.save_scene(path) -> bool
///
/// Serializes the current scene to a `.uzscene` JSON file at the given VFS path.
///
/// # Arguments
/// * `path` (string) — VFS-relative destination path (e.g. "scenes/my_scene.uzscene").
///
/// # Returns
/// `true` on success, `false` on failure (error is logged).
static int SceneSaveScene(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    lua_getfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    auto* world = static_cast<World*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!world) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.save_scene: World not set in Lua registry");
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_getfield(L, LUA_REGISTRYINDEX, kVfsRegistryKey);
    auto* vfs = static_cast<IVFS*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!vfs) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.save_scene: VFS not set in Lua registry");
        lua_pushboolean(L, 0);
        return 1;
    }

    const std::vector<uint8_t> data = uz::SaveScene(*world);
    if (data.empty()) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.save_scene: no entities to save — path='{}'", path);
        lua_pushboolean(L, 0);
        return 1;
    }

    const std::string resolvedPath = vfs->ResolvePath(path);
    FILE* file = std::fopen(resolvedPath.c_str(), "wb");
    if (!file) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.save_scene: failed to open '{}' for writing", resolvedPath);
        lua_pushboolean(L, 0);
        return 1;
    }
    const size_t written = std::fwrite(data.data(), 1, data.size(), file);
    std::fclose(file);

    if (written != data.size()) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.save_scene: partial write — expected {} bytes, wrote {}",
            data.size(), written);
        lua_pushboolean(L, 0);
        return 1;
    }

    UZ_LOG(Scripting, Info,
        "uz.scene.save_scene: saved {} bytes to '{}'", data.size(), resolvedPath);
    lua_pushboolean(L, 1);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterSceneIoAPI(lua_State* L) {
    // Retrieve the existing uz table.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterSceneIoAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // Retrieve the existing uz.scene sub-table.
    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterSceneIoAPI: 'uz.scene' is not a table — call LuaRegisterSceneAPI first");
        lua_pop(L, 2);
        return;
    }

    // Add scene I/O functions to uz.scene.
    lua_pushcfunction(L, SceneLoadScene);
    lua_setfield(L, -2, "load_scene");

    lua_pushcfunction(L, SceneSaveScene);
    lua_setfield(L, -2, "save_scene");

    // Pop uz.scene and uz tables.
    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene I/O API registered");
}

void LuaSetVFS(lua_State* L, IVFS* vfs) {
    lua_pushlightuserdata(L, static_cast<void*>(vfs));
    lua_setfield(L, LUA_REGISTRYINDEX, kVfsRegistryKey);
    UZ_LOG(Scripting, Debug, "LuaSetVFS: vfs={}", static_cast<void*>(vfs));
}

} // namespace uz
