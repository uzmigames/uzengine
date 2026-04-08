#include "lua_asset_api.h"

#include "log.h"
#include "scene_renderer.h"
#include "mesh.h"
#include "texture_loader.h"
#include "vfs.h"

/// Lua bindings for asset loading API (mesh, texture, material queries and properties).

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>
#include <string>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key constants
// ─────────────────────────────────────────────────────────────────────────────

/// Key used to store the SceneRenderer* in the Lua registry.
static constexpr const char* kRendererRegistryKey = "uz_scene_renderer";

/// Key used to store the IVFS* in the Lua registry.
static constexpr const char* kVfsRegistryKey = "uz_vfs";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the SceneRenderer* from the Lua registry.
///
/// Returns null if `LuaSetSceneRenderer` was never called or was called with null.
[[nodiscard]] static SceneRenderer* GetRenderer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRendererRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<SceneRenderer*>(p);
}

/// Retrieves the IVFS* from the Lua registry.
///
/// Returns null if the VFS was never registered via `LuaSetVFS`.
[[nodiscard]] static IVFS* GetVFS(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kVfsRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<IVFS*>(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.mesh.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.mesh.load(path) → mesh_id | nil
///
/// Loads a mesh from the VFS and uploads it to the GPU.  If the mesh was
/// previously loaded (same path) the cached ID is returned without re-upload.
///
/// # Arguments
/// * `path` (string) — VFS-relative mesh path, e.g. "meshes/cube.gltf".
///
/// # Returns
/// Integer mesh ID on success; nil on failure (error is logged).
///
/// # Note
/// The VFS and command list are owned by the SceneRenderer.  Because the
/// Renderer API is called from script callbacks (outside the render loop),
/// actual GPU upload is deferred: the path is stored in a pending list and
/// the mesh is loaded at the start of the next `RenderFrame()`.  For MVP
/// the asset table indices are stable once assigned — the Lua ID remains
/// valid for the renderer's lifetime.
static int MeshLoad(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.mesh.load: SceneRenderer not set");
        lua_pushnil(L);
        return 1;
    }

    // Defer the actual GPU upload to the render thread.  SceneRenderer caches
    // by path so calling this multiple times with the same path is free.
    const uint32_t id = renderer->QueueMeshLoad(path);
    if (id == UINT32_MAX) {
        UZ_LOG(Scripting, Warning, "uz.mesh.load: failed to queue mesh '{}'", path);
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

/// uz.mesh.generate_sphere(sectors, stacks) -> mesh_id | nil
///
/// Generates a UV sphere mesh and registers it for GPU upload.
/// Returns a mesh_id that can be used with uz.scene.set_mesh().
///
/// # Arguments
/// * `sectors` (integer, optional) — Longitudinal subdivisions. Default: 32.
/// * `stacks` (integer, optional) — Latitudinal subdivisions. Default: 16.
///
/// # Returns
/// Integer mesh ID on success; nil on failure.
static int MeshGenerateSphere(lua_State* L) {
    const uint32_t sectors = static_cast<uint32_t>(luaL_optinteger(L, 1, 32));
    const uint32_t stacks  = static_cast<uint32_t>(luaL_optinteger(L, 2, 16));

    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.mesh.generate_sphere: SceneRenderer not set");
        lua_pushnil(L);
        return 1;
    }

    Mesh cpuMesh = GenerateSphereMesh(sectors, stacks);
    if (!cpuMesh.IsValid()) {
        UZ_LOG(Scripting, Warning, "uz.mesh.generate_sphere: mesh generation failed");
        lua_pushnil(L);
        return 1;
    }

    // Build a cache key so identical params return the same ID.
    char key[64];
    std::snprintf(key, sizeof(key), "procedural://sphere_%u_%u", sectors, stacks);

    const uint32_t id = renderer->RegisterProceduralMesh(std::move(cpuMesh), key);
    if (id == UINT32_MAX) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

/// uz.mesh.generate_plane(uv_scale) -> mesh_id | nil
///
/// Generates a flat quad (plane) mesh lying in the XY plane (Z=0) and registers
/// it for GPU upload. Returns a mesh_id that can be used with uz.scene.set_mesh().
///
/// # Arguments
/// * `uv_scale` (number, optional) — UV coordinate scale factor. Default: 1.0.
///
/// # Returns
/// Integer mesh ID on success; nil on failure.
static int MeshGeneratePlane(lua_State* L) {
    const float uvScale = static_cast<float>(luaL_optnumber(L, 1, 1.0));

    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.mesh.generate_plane: SceneRenderer not set");
        lua_pushnil(L);
        return 1;
    }

    Mesh cpuMesh = GeneratePlaneMesh(uvScale);
    if (!cpuMesh.IsValid()) {
        UZ_LOG(Scripting, Warning, "uz.mesh.generate_plane: mesh generation failed");
        lua_pushnil(L);
        return 1;
    }

    // Build a cache key so identical params return the same ID.
    char key[64];
    std::snprintf(key, sizeof(key), "procedural://plane_%.2f", uvScale);

    const uint32_t id = renderer->RegisterProceduralMesh(std::move(cpuMesh), key);
    if (id == UINT32_MAX) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.texture.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.texture.load(path) → texture_id | nil
///
/// Loads a texture from the VFS and uploads it to the GPU.  If the texture was
/// previously loaded (same path) the cached ID is returned.
///
/// # Arguments
/// * `path` (string) — VFS-relative texture path, e.g. "textures/albedo.png".
///
/// # Returns
/// Integer texture ID on success; nil on failure (error is logged).
static int TextureLoad(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.texture.load: SceneRenderer not set");
        lua_pushnil(L);
        return 1;
    }

    const uint32_t id = renderer->QueueTextureLoad(path);
    if (id == UINT32_MAX) {
        UZ_LOG(Scripting, Warning, "uz.texture.load: failed to queue texture '{}'", path);
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.texture.load_hdr
// ─────────────────────────────────────────────────────────────────────────────

/// uz.texture.load_hdr(path) -> srv_index | nil
///
/// Loads an HDR/EXR panorama texture via VFS and returns its SRV descriptor
/// heap index. The texture is loaded synchronously (HDR panoramas are
/// typically small enough that blocking is acceptable).
///
/// The returned SRV index can be passed to `uz.renderer.set_hdri_environment()`
/// to use the loaded panorama as the scene's IBL environment.
///
/// # Arguments
/// * `path` (string) — VFS-relative path to an `.exr` or `.hdr` file.
///
/// # Returns
/// Integer SRV index on success; nil on failure (error is logged).
static int TextureLoadHDR(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.texture.load_hdr: SceneRenderer not set");
        lua_pushnil(L);
        return 1;
    }

    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        UZ_LOG(Scripting, Warning, "uz.texture.load_hdr: VFS not set");
        lua_pushnil(L);
        return 1;
    }

    const std::string pathStr(path);
    auto data = vfs->Read(pathStr);
    if (data.empty()) {
        UZ_LOG(Scripting, Warning, "uz.texture.load_hdr: failed to read '{}'", path);
        lua_pushnil(L);
        return 1;
    }

    // Determine decoder from file extension (.hdr = Radiance RGBE, .exr = OpenEXR).
    const char* formatHint = "exr";
    if (pathStr.size() > 4) {
        const std::string ext = pathStr.substr(pathStr.size() - 4);
        if (ext == ".hdr") {
            formatHint = "hdr";
        }
    }

    const TextureLoadResult result = TextureLoader::LoadHDRFromMemory(
        renderer->GetDevice(), data.data(), data.size(), formatHint, path);

    if (!result.handle.IsValid()) {
        UZ_LOG(Scripting, Warning, "uz.texture.load_hdr: failed to decode '{}'", path);
        lua_pushnil(L);
        return 1;
    }

    // Store the texture handle in the renderer's texture tracking so it is
    // destroyed during Shutdown/DestroyAssets. Without this, the 16MB RGBA16F
    // equirect texture leaks (handle dropped at end of this scope).
    renderer->TrackExternalTexture(result);

    lua_pushinteger(L, static_cast<lua_Integer>(result.srvIndex));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterAssetAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterAssetAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // ── uz.mesh = { load, generate_sphere, generate_plane } ───────────────
    lua_newtable(L);

    lua_pushcfunction(L, MeshLoad);
    lua_setfield(L, -2, "load");

    lua_pushcfunction(L, MeshGenerateSphere);
    lua_setfield(L, -2, "generate_sphere");

    lua_pushcfunction(L, MeshGeneratePlane);
    lua_setfield(L, -2, "generate_plane");

    lua_setfield(L, -2, "mesh");

    // ── uz.texture = { load, load_hdr } ──────────────────────────────────
    lua_newtable(L);

    lua_pushcfunction(L, TextureLoad);
    lua_setfield(L, -2, "load");

    lua_pushcfunction(L, TextureLoadHDR);
    lua_setfield(L, -2, "load_hdr");

    lua_setfield(L, -2, "texture");

    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.mesh / uz.texture API registered");
}

} // namespace uz
