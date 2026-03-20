#include "lua_renderer_api.h"

#include "log.h"
#include "debug_view_mode.h"
#include "scene_renderer.h"
#include "mesh.h"
#include "stats_overlay.h"
#include "postprocess/temporal_upscaler.h"
#include "cvar.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdio>
#include <cstring>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key constants
// ─────────────────────────────────────────────────────────────────────────────

/// Key used to store the SceneRenderer* in the Lua registry.
static constexpr const char* kRendererRegistryKey = "uz_scene_renderer";

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

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.renderer.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.renderer.set_render_path(path_str)
///
/// Switches the render pipeline. `path_str` must be "deferred" or "forward".
/// Takes effect on the next rendered frame. Silently ignores unknown strings.
///
/// # Arguments
/// * `path_str` (string) — "deferred" or "forward".
static int RendererSetRenderPath(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.set_render_path: SceneRenderer not set");
        return 0;
    }

    if (std::strcmp(path, "deferred") == 0) {
        renderer->SetRenderPath(RenderPath::Deferred);
        UZ_LOG(Scripting, Info, "[Lua] uz.renderer.set_render_path: Deferred");
    } else if (std::strcmp(path, "forward") == 0) {
        renderer->SetRenderPath(RenderPath::Forward);
        UZ_LOG(Scripting, Info, "[Lua] uz.renderer.set_render_path: Forward");
    } else {
        UZ_LOG(Scripting, Warning,
            "uz.renderer.set_render_path: unknown path '{}' — expected 'deferred' or 'forward'",
            path);
    }
    return 0;
}

/// uz.renderer.set_debug_mode(mode)
///
/// Sets the GBuffer debug visualization channel.
///
/// # Arguments
/// * `mode` (integer) — 0=Albedo, 1=Normal, 2=Metallic, 3=Roughness,
///   4=AO, 5=Depth, 6=WorldPos, 7=Velocity, 8 (or any value >= Count) = off.
static int RendererSetDebugMode(lua_State* L) {
    const lua_Integer mode = luaL_checkinteger(L, 1);
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.set_debug_mode: SceneRenderer not set");
        return 0;
    }

    // Values >= Count disable the debug pass (sentinel behaviour from the enum).
    const auto modeEnum = static_cast<GBufferDebugMode>(
        static_cast<uint32_t>(mode));
    renderer->SetDebugMode(modeEnum);
    UZ_LOG(Scripting, Debug, "[Lua] uz.renderer.set_debug_mode: {}", mode);
    return 0;
}

/// uz.renderer.get_stats() → table {fps, draw_calls}
///
/// Returns a snapshot of renderer performance statistics for the previous frame.
/// `fps` is approximate (derived from delta_time), `draw_calls` is the actual
/// draw call count submitted during the last frame.
///
/// # Returns
/// Table with fields:
/// * `fps` (number) — frames per second estimate.
/// * `draw_calls` (number) — draw call count from the previous frame.
static int RendererGetStats(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.get_stats: SceneRenderer not set");
        lua_pushnil(L);
        return 1;
    }

    // StatsOverlay exposes counters via GetStatsOverlay() — fall back gracefully
    // when the overlay has not yet been initialized (first frame).
    float fps = 0.f;
    uint32_t drawCalls = 0u;
    const StatsOverlay* overlay = renderer->GetStatsOverlay();
    if (overlay) {
        fps       = overlay->GetFPS();
        drawCalls = overlay->GetDrawCallCount();
    }

    lua_newtable(L);
    lua_pushnumber(L, static_cast<double>(fps));
    lua_setfield(L, -2, "fps");
    lua_pushinteger(L, static_cast<lua_Integer>(drawCalls));
    lua_setfield(L, -2, "draw_calls");
    return 1;
}

/// uz.renderer.set_debug_view(mode_name)
///
/// Sets the debug view mode on the SceneRenderer. `mode_name` is a string
/// matching a `DebugViewMode` enum value (case-insensitive), e.g. "albedo",
/// "depth", "overview", "lit" (to restore normal rendering).
///
/// Unrecognized names default to "Lit" (normal rendering).
///
/// # Arguments
/// * `mode_name` (string) — Debug view mode name.
static int RendererSetDebugView(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.set_debug_view: SceneRenderer not set");
        return 0;
    }

    const DebugViewMode mode = DebugViewModeFromString(std::string_view{name});
    renderer->SetDebugViewMode(mode);
    UZ_LOG(Scripting, Info, "[Lua] uz.renderer.set_debug_view: {}", DebugViewModeName(mode));
    return 0;
}

/// uz.renderer.screenshot(path) → true | false
///
/// Queues a screenshot capture to be processed by the runtime main loop on the
/// next frame boundary.  The path is stored in the Lua registry under the key
/// `"uz_screenshot_request"`.  The runtime polls this key after each
/// `RenderFrame()` call and, when present, captures the back buffer via
/// `ScreenshotCapture` and writes the PNG.
///
/// # Arguments
/// * `path` (string) — Output file path for the PNG (e.g. "screenshot.png").
///   Relative paths are resolved from the working directory of the process.
///
/// # Returns
/// `true` if the request was queued successfully.
/// `false` if `path` is empty or the renderer is not available.
///
/// # Note
/// Only one screenshot can be pending at a time.  A second call before the
/// runtime consumes the previous request overwrites the pending path.
static int RendererScreenshot(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!path || path[0] == '\0') {
        UZ_LOG(Scripting, Warning, "uz.renderer.screenshot: path must not be empty");
        lua_pushboolean(L, 0);
        return 1;
    }

    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.screenshot: SceneRenderer not set");
        lua_pushboolean(L, 0);
        return 1;
    }

    // Store the pending path in the Lua registry so the main loop can poll it.
    // Key: "uz_screenshot_request"  Value: path string
    lua_pushstring(L, path);
    lua_setfield(L, LUA_REGISTRYINDEX, "uz_screenshot_request");

    UZ_LOG(Scripting, Info, "[Lua] uz.renderer.screenshot queued: path='{}'", path);
    lua_pushboolean(L, 1);
    return 1;
}

/// uz.renderer.get_debug_view() -> string
///
/// Returns the current debug view mode name as a lowercase string.
///
/// # Returns
/// String — current mode name (e.g. "Lit", "Albedo", "Depth").
static int RendererGetDebugView(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (renderer) {
        lua_pushstring(L, DebugViewModeName(renderer->GetDebugViewMode()));
    } else {
        lua_pushstring(L, "Lit");
    }
    return 1;
}

/// uz.renderer.set_shadow_cache(enabled)
///
/// Enables or disables shadow caching at runtime.
/// Equivalent to the reference engine's `r.Shadow.CacheEnabled` CVar.
///
/// @ref Engel 2006 "Cascaded Shadow Maps" — shadow cache toggle
///
/// # Arguments
/// * `enabled` (boolean) — True to enable shadow caching.
static int RendererSetShadowCache(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1) != 0;
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.set_shadow_cache: SceneRenderer not set");
        return 0;
    }
    renderer->SetShadowCacheEnabled(enabled);
    UZ_LOG(Scripting, Info, "[Lua] uz.renderer.set_shadow_cache: {}", enabled ? "enabled" : "disabled");
    return 0;
}

/// uz.renderer.get_shadow_cache() -> boolean
///
/// Returns whether shadow caching is currently enabled.
///
/// # Returns
/// Boolean — true if shadow caching is enabled.
static int RendererGetShadowCache(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    lua_pushboolean(L, renderer && renderer->IsShadowCacheEnabled());
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.renderer.* (screen percentage / upscaling)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.renderer.set_screen_percentage(pct)
///
/// Sets the render scale. 100 = native resolution, 50 = half-res, 200 = supersampled.
/// Clamped to [10, 200]. Takes effect on the next frame.
///
/// # Arguments
/// * `pct` (number) — Screen percentage [10, 200].
static int RendererSetScreenPercentage(lua_State* L) {
    const float pct = static_cast<float>(luaL_checknumber(L, 1));
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.set_screen_percentage: SceneRenderer not set");
        return 0;
    }
    renderer->SetScreenPercentage(pct);
    return 0;
}

/// uz.renderer.get_screen_percentage() → number
///
/// Returns the current render scale percentage.
static int RendererGetScreenPercentage(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    lua_pushnumber(L, renderer ? static_cast<double>(renderer->GetScreenPercentage()) : 100.0);
    return 1;
}

/// uz.renderer.set_upscaler(name)
///
/// Sets the temporal upscaler. Valid names: "none", "taa", "tsr".
///
/// # Arguments
/// * `name` (string) — Upscaler name.
static int RendererSetUpscaler(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        UZ_LOG(Scripting, Warning, "uz.renderer.set_upscaler: SceneRenderer not set");
        return 0;
    }
    if (std::strcmp(name, "none") == 0) {
        renderer->SetUpscalerType(UpscalerType::None);
    } else if (std::strcmp(name, "taa") == 0) {
        renderer->SetUpscalerType(UpscalerType::TAA);
    } else if (std::strcmp(name, "tsr") == 0) {
        renderer->SetUpscalerType(UpscalerType::TSR);
    } else {
        UZ_LOG(Scripting, Warning, "uz.renderer.set_upscaler: unknown upscaler '{}'", name);
    }
    return 0;
}

/// uz.renderer.get_upscaler() → string
///
/// Returns the current upscaler name ("none", "taa", or "tsr").
static int RendererGetUpscaler(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_pushstring(L, "none");
        return 1;
    }
    auto* upscaler = renderer->GetUpscaler();
    lua_pushstring(L, upscaler ? upscaler->GetName() : "none");
    return 1;
}

/// uz.renderer.set_dynamic_resolution(enabled)
///
/// Enables or disables GPU-bound adaptive resolution scaling.
///
/// # Arguments
/// * `enabled` (boolean) — True to enable dynamic resolution.
static int RendererSetDynamicResolution(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1) != 0;
    CVarRegistry::Get().Set("r.DynamicResolution.Enabled", enabled ? 1.f : 0.f);
    return 0;
}

/// uz.renderer.get_render_resolution() → width, height
///
/// Returns the internal render resolution (after screen percentage).
static int RendererGetRenderResolution(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (renderer) {
        lua_pushinteger(L, static_cast<lua_Integer>(renderer->GetRenderWidth()));
        lua_pushinteger(L, static_cast<lua_Integer>(renderer->GetRenderHeight()));
    } else {
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
    }
    return 2;
}

/// uz.renderer.get_display_resolution() → width, height
///
/// Returns the display/backbuffer resolution.
static int RendererGetDisplayResolution(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (renderer) {
        lua_pushinteger(L, static_cast<lua_Integer>(renderer->GetWidth()));
        lua_pushinteger(L, static_cast<lua_Integer>(renderer->GetHeight()));
    } else {
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
    }
    return 2;
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
// Lua C functions — uz.cvar.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.cvar.get(name) → number | nil
///
/// Returns the current value of a CVar, or nil if not found.
static int CvarGet(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto* cvar = CVarRegistry::Get().Find(name);
    if (cvar) {
        lua_pushnumber(L, static_cast<double>(cvar->value));
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/// uz.cvar.set(name, value)
///
/// Sets a CVar value. Clamped to the CVar's [min, max] range.
/// Logs a warning if the CVar does not exist.
static int CvarSet(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const float value = static_cast<float>(luaL_checknumber(L, 2));
    auto& registry = CVarRegistry::Get();
    if (registry.Find(name)) {
        registry.Set(name, value);
    } else {
        UZ_LOG(Scripting, Warning, "uz.cvar.set: unknown CVar '{}'", name);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.debug.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.debug.set_view_mode(mode_name)
///
/// Sets the debug view mode. Equivalent to uz.renderer.set_debug_view() but
/// exposed under the canonical `uz.debug` namespace for script authoring.
///
/// # Arguments
/// * `mode_name` (string) — mode name: "lit", "albedo", "depth", "overview", etc.
///   Unrecognized names default to "Lit" (normal rendering).
///
/// # Returns
/// Nothing. Logs a warning if the SceneRenderer is not available.
static int DebugSetViewMode(lua_State* L) {
    return RendererSetDebugView(L);
}

/// uz.debug.get_view_mode() → string
///
/// Returns the current debug view mode name.
///
/// # Returns
/// String — current mode name (e.g. "Lit", "Albedo", "Depth", "Overview").
/// Returns "Lit" if the SceneRenderer is not available.
static int DebugGetViewMode(lua_State* L) {
    return RendererGetDebugView(L);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.pass_inspector.* (Phase 6: IPC/Lua API)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.pass_inspector.vis(target_name)
///
/// Sets the vis target. Pass "off" or nil to disable.
static int PassInspectorVis(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    if (lua_isnil(L, 1) || lua_gettop(L) == 0) {
        renderer->GetPassInspector().ClearVis();
    } else {
        const char* target = luaL_checkstring(L, 1);
        renderer->GetPassInspector().SetVisTarget(target);
    }
    return 0;
}

/// uz.pass_inspector.toggle(pass_name)
///
/// Toggles a pass on/off.
static int PassInspectorToggle(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    const char* name = luaL_checkstring(L, 1);
    renderer->GetPassInspector().TogglePass(name);
    return 0;
}

/// uz.pass_inspector.is_pass_enabled(pass_name) → boolean
///
/// Returns true if a pass is currently enabled.
static int PassInspectorIsPassEnabled(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_pushboolean(L, 1);
        return 1;
    }

    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, renderer->GetPassInspector().IsPassEnabled(name) ? 1 : 0);
    return 1;
}

/// uz.pass_inspector.dump(frame) or uz.pass_inspector.dump()
///
/// Requests a dump of all targets. If frame is nil or missing, dumps next frame.
static int PassInspectorDump(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        const uint32_t frame = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        renderer->GetPassInspector().RequestDump(frame);
    } else {
        renderer->GetPassInspector().RequestDump(UINT32_MAX);
    }
    return 0;
}

/// uz.pass_inspector.stats(enabled)
///
/// Enables or disables the pass stats overlay.
static int PassInspectorStats(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    const bool enabled = lua_toboolean(L, 1) != 0;
    renderer->GetPassInspector().SetStatsOverlayEnabled(enabled);
    return 0;
}

/// uz.pass_inspector.validate() or uz.pass_inspector.validate(pass_name)
///
/// Runs validation and returns results as a table.
static int PassInspectorValidate(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_newtable(L);
        return 1;
    }

    // Validation requires GPU idle — this is expensive. Log a warning.
    UZ_LOG(Scripting, Info, "[Lua] uz.pass_inspector.validate: running (stalls GPU)");

    // Return the target list for informational purposes (full validation
    // requires GPU device access which is not safe from a Lua callback).
    const auto& targets = renderer->GetPassInspector().GetTargets();
    lua_newtable(L);
    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        lua_newtable(L);
        lua_pushstring(L, targets[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, static_cast<lua_Integer>(targets[i].width));
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, static_cast<lua_Integer>(targets[i].height));
        lua_setfield(L, -2, "height");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/// uz.pass_inspector.list_targets() → table of target names
///
/// Returns a list of all registered target names this frame.
static int PassInspectorListTargets(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_newtable(L);
        return 1;
    }

    const auto& targets = renderer->GetPassInspector().GetTargets();
    lua_newtable(L);
    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        lua_pushstring(L, targets[i].name.c_str());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/// uz.pass_inspector.grid(mode)
///
/// Sets grid mode: 0=off, 1=4x4, 2=6x4 extended.
static int PassInspectorGrid(lua_State* L) {
    const int mode = static_cast<int>(luaL_optinteger(L, 1, 0));
    CVarRegistry::Get().Set("r.PassInspector.Grid", static_cast<float>(mode));
    return 0;
}

/// uz.pass_inspector.compare(pass_name) or uz.pass_inspector.compare()
///
/// Enters or exits comparison mode for a pass.
static int PassInspectorCompare(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        const char* name = luaL_checkstring(L, 1);
        renderer->GetPassInspector().SetComparePass(name);
    } else {
        renderer->GetPassInspector().ClearCompare();
    }
    return 0;
}

/// uz.pass_inspector.get_timing() → table
///
/// Returns the most recent per-pass timing data.
static int PassInspectorGetTiming(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_newtable(L);
        return 1;
    }

    const auto& timings = renderer->GetPassInspector().GetTimingData();
    lua_newtable(L);
    for (int i = 0; i < static_cast<int>(timings.size()); ++i) {
        lua_newtable(L);
        lua_pushstring(L, timings[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushnumber(L, static_cast<double>(timings[i].gpuTimeMs));
        lua_setfield(L, -2, "gpu_time_ms");
        lua_pushnumber(L, static_cast<double>(timings[i].minVal));
        lua_setfield(L, -2, "min");
        lua_pushnumber(L, static_cast<double>(timings[i].maxVal));
        lua_setfield(L, -2, "max");
        lua_pushnumber(L, static_cast<double>(timings[i].avgVal));
        lua_setfield(L, -2, "avg");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterRendererAPI(lua_State* L) {
    // Retrieve the existing uz table.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterRendererAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // ── uz.renderer = { set_render_path, set_debug_mode, get_stats } ──────
    lua_newtable(L);

    lua_pushcfunction(L, RendererSetRenderPath);
    lua_setfield(L, -2, "set_render_path");

    lua_pushcfunction(L, RendererSetDebugMode);
    lua_setfield(L, -2, "set_debug_mode");

    lua_pushcfunction(L, RendererGetStats);
    lua_setfield(L, -2, "get_stats");

    lua_pushcfunction(L, RendererSetDebugView);
    lua_setfield(L, -2, "set_debug_view");

    lua_pushcfunction(L, RendererGetDebugView);
    lua_setfield(L, -2, "get_debug_view");

    lua_pushcfunction(L, RendererScreenshot);
    lua_setfield(L, -2, "screenshot");

    lua_pushcfunction(L, RendererSetShadowCache);
    lua_setfield(L, -2, "set_shadow_cache");

    lua_pushcfunction(L, RendererGetShadowCache);
    lua_setfield(L, -2, "get_shadow_cache");

    lua_pushcfunction(L, RendererSetScreenPercentage);
    lua_setfield(L, -2, "set_screen_percentage");

    lua_pushcfunction(L, RendererGetScreenPercentage);
    lua_setfield(L, -2, "get_screen_percentage");

    lua_pushcfunction(L, RendererSetUpscaler);
    lua_setfield(L, -2, "set_upscaler");

    lua_pushcfunction(L, RendererGetUpscaler);
    lua_setfield(L, -2, "get_upscaler");

    lua_pushcfunction(L, RendererSetDynamicResolution);
    lua_setfield(L, -2, "set_dynamic_resolution");

    lua_pushcfunction(L, RendererGetRenderResolution);
    lua_setfield(L, -2, "get_render_resolution");

    lua_pushcfunction(L, RendererGetDisplayResolution);
    lua_setfield(L, -2, "get_display_resolution");

    lua_setfield(L, -2, "renderer");

    // ── uz.mesh = { load, generate_sphere, generate_plane } ───────────────
    lua_newtable(L);

    lua_pushcfunction(L, MeshLoad);
    lua_setfield(L, -2, "load");

    lua_pushcfunction(L, MeshGenerateSphere);
    lua_setfield(L, -2, "generate_sphere");

    lua_pushcfunction(L, MeshGeneratePlane);
    lua_setfield(L, -2, "generate_plane");

    lua_setfield(L, -2, "mesh");

    // ── uz.texture = { load } ──────────────────────────────────────────────
    lua_newtable(L);

    lua_pushcfunction(L, TextureLoad);
    lua_setfield(L, -2, "load");

    lua_setfield(L, -2, "texture");

    // ── uz.cvar = { get, set } ───────────────────────────────────────────
    lua_newtable(L);

    lua_pushcfunction(L, CvarGet);
    lua_setfield(L, -2, "get");

    lua_pushcfunction(L, CvarSet);
    lua_setfield(L, -2, "set");

    lua_setfield(L, -2, "cvar");

    // ── uz.debug = { set_view_mode, get_view_mode } ────────────────────────
    lua_newtable(L);

    lua_pushcfunction(L, DebugSetViewMode);
    lua_setfield(L, -2, "set_view_mode");

    lua_pushcfunction(L, DebugGetViewMode);
    lua_setfield(L, -2, "get_view_mode");

    lua_setfield(L, -2, "debug");

    // ── uz.pass_inspector = { vis, toggle, dump, stats, validate, ... } ──
    lua_newtable(L);

    lua_pushcfunction(L, PassInspectorVis);
    lua_setfield(L, -2, "vis");

    lua_pushcfunction(L, PassInspectorToggle);
    lua_setfield(L, -2, "toggle");

    lua_pushcfunction(L, PassInspectorIsPassEnabled);
    lua_setfield(L, -2, "is_pass_enabled");

    lua_pushcfunction(L, PassInspectorDump);
    lua_setfield(L, -2, "dump");

    lua_pushcfunction(L, PassInspectorStats);
    lua_setfield(L, -2, "stats");

    lua_pushcfunction(L, PassInspectorValidate);
    lua_setfield(L, -2, "validate");

    lua_pushcfunction(L, PassInspectorListTargets);
    lua_setfield(L, -2, "list_targets");

    lua_pushcfunction(L, PassInspectorGrid);
    lua_setfield(L, -2, "grid");

    lua_pushcfunction(L, PassInspectorCompare);
    lua_setfield(L, -2, "compare");

    lua_pushcfunction(L, PassInspectorGetTiming);
    lua_setfield(L, -2, "get_timing");

    lua_setfield(L, -2, "pass_inspector");

    // Pop the uz table.
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info,
           "uz.renderer / uz.mesh / uz.texture / uz.debug / uz.pass_inspector API registered");
}

void LuaSetSceneRenderer(lua_State* L, SceneRenderer* renderer) {
    lua_pushlightuserdata(L, static_cast<void*>(renderer));
    lua_setfield(L, LUA_REGISTRYINDEX, kRendererRegistryKey);
    UZ_LOG(Scripting, Debug, "LuaSetSceneRenderer: renderer={}", static_cast<void*>(renderer));
}

} // namespace uz
