/// Lua bindings for the decal VFX system (spawn, transform, fade, lifetime).

#include "decal_lua_bindings.h"

#include "engine/vfx/decals/decal_manager.h"
#include "engine/vfx/decals/decal_component.h"
#include "engine/vfx/decals/decal_asset.h"
#include "engine/logging/log.h"
#include "vector3.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Log category
// ─────────────────────────────────────────────────────────────────────────────

// VFX log category -- declared here, defined in vfx_system.cpp.
UZ_DECLARE_LOG_CATEGORY(VFX);

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time safety: void* must be at least 64 bits to encode a
// VfxDecal pointer as a Lua integer handle.
// ─────────────────────────────────────────────────────────────────────────────

static_assert(sizeof(void*) >= 8,
    "Decal Lua bindings require 64-bit pointers for handle encoding");

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key
// ─────────────────────────────────────────────────────────────────────────────

/// Registry key under which the DecalManager* lightuserdata is stored.
static constexpr const char* kDecalManagerRegistryKey = "uz_decal_manager";

// ─────────────────────────────────────────────────────────────────────────────
// VfxDecal handle encoding
//
// Same pattern as emitter handles in vfx_lua_bindings.cpp: VfxDecal*
// pointers are encoded as lua_Integer values for opaque handle storage.
// ─────────────────────────────────────────────────────────────────────────────

/// Encodes a VfxDecal* as a lua_Integer for opaque handle storage.
[[nodiscard]] static lua_Integer DecalToHandle(VfxDecal* decal) noexcept {
    uintptr_t ptr;
    std::memcpy(&ptr, &decal, sizeof(ptr));
    return static_cast<lua_Integer>(ptr);
}

/// Decodes a lua_Integer back to a VfxDecal*.
[[nodiscard]] static VfxDecal* HandleToDecal(lua_Integer handle) noexcept {
    uintptr_t ptr = static_cast<uintptr_t>(handle);
    VfxDecal* decal;
    std::memcpy(&decal, &ptr, sizeof(decal));
    return decal;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the DecalManager* stored in the Lua registry.
///
/// # Returns
/// Non-null pointer if the manager was registered. Null if
/// `RegisterDecalLuaBindings` was never called or `UnregisterDecalLuaBindings`
/// was already called.
[[nodiscard]] static DecalManager* GetDecalManager(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kDecalManagerRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<DecalManager*>(p);
}

/// Reads a Vec3 from a Lua table at stack index `idx`.
///
/// Accepts both array form `{x, y, z}` (keys 1, 2, 3) and named form
/// `{x=..., y=..., z=...}`. Array form is checked first. If neither key
/// style is present for a component, that component defaults to 0.0.
[[nodiscard]] static Vec3 ReadVec3FromTable(lua_State* L, int idx) noexcept {
    Vec3 v{0.0f, 0.0f, 0.0f};

    // Try array form first: {1, 2, 3}
    lua_rawgeti(L, idx, 1);
    lua_rawgeti(L, idx, 2);
    lua_rawgeti(L, idx, 3);

    const bool hasArrayForm = lua_isnumber(L, -3)
                           && lua_isnumber(L, -2)
                           && lua_isnumber(L, -1);

    if (hasArrayForm) {
        v.x = static_cast<float>(lua_tonumber(L, -3));
        v.y = static_cast<float>(lua_tonumber(L, -2));
        v.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 3);
        return v;
    }
    lua_pop(L, 3);

    // Fall back to named form: {x=..., y=..., z=...}
    lua_getfield(L, idx, "x");
    lua_getfield(L, idx, "y");
    lua_getfield(L, idx, "z");

    if (lua_isnumber(L, -3)) v.x = static_cast<float>(lua_tonumber(L, -3));
    if (lua_isnumber(L, -2)) v.y = static_cast<float>(lua_tonumber(L, -2));
    if (lua_isnumber(L, -1)) v.z = static_cast<float>(lua_tonumber(L, -1));

    lua_pop(L, 3);
    return v;
}

/// Parses a DecalBlendMode from a Lua string.
///
/// # Arguments
/// * `modeStr` -- One of: "opaque", "additive", "multiply", "screen".
/// * `outMode` -- Receives the parsed mode on success.
///
/// # Returns
/// `true` if the string was recognized, `false` otherwise.
[[nodiscard]] static bool ParseBlendMode(const char* modeStr,
                                         DecalBlendMode& outMode) noexcept
{
    if (std::strcmp(modeStr, "opaque") == 0) {
        outMode = DecalBlendMode::Opaque;
        return true;
    }
    if (std::strcmp(modeStr, "additive") == 0) {
        outMode = DecalBlendMode::Additive;
        return true;
    }
    if (std::strcmp(modeStr, "multiply") == 0) {
        outMode = DecalBlendMode::Multiply;
        return true;
    }
    if (std::strcmp(modeStr, "screen") == 0) {
        outMode = DecalBlendMode::Screen;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions -- uz.vfx:* (decal operations)
//
// All functions receive `self` (the uz.vfx table) as argument 1 because they
// are registered with the colon-call syntax. Arguments follow from index 2.
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:spawn_decal(material, position, normal [, lifetime]) -> handle | nil
///
/// Spawns a new decal at the given surface hit point. Creates a temporary
/// DecalAsset from the provided material path and default values, then
/// delegates to DecalManager::SpawnDecal.
///
/// # Parameters
/// - `material` (string): Path to the decal material (e.g.
///   `"materials/bullet_hole.uzmat"`).
/// - `position` (table {x,y,z}): World-space hit point on the surface.
/// - `normal`   (table {x,y,z}): Unit surface normal at the hit point.
/// - `lifetime` (number, optional): Override lifetime in seconds. Default
///   -1 (use asset default of 10s).
///
/// # Returns
/// An opaque integer handle on success, or `nil` if the manager is not
/// registered or the pool is exhausted.
static int DecalSpawn(lua_State* L) {
    // Argument 1 is self (uz.vfx table) -- skip it.
    const char* materialPath = luaL_checkstring(L, 2);

    if (!lua_istable(L, 3)) {
        return luaL_error(L,
            "uz.vfx:spawn_decal -- argument 2 (position) must be a table {x,y,z}");
    }
    if (!lua_istable(L, 4)) {
        return luaL_error(L,
            "uz.vfx:spawn_decal -- argument 3 (normal) must be a table {x,y,z}");
    }

    const Vec3 position = ReadVec3FromTable(L, 3);
    const Vec3 normal   = ReadVec3FromTable(L, 4);

    const float lifetimeOverride = static_cast<float>(
        luaL_optnumber(L, 5, -1.0));

    DecalManager* manager = GetDecalManager(L);
    if (!manager) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:spawn_decal -- DecalManager not registered; "
            "call RegisterDecalLuaBindings first");
        lua_pushnil(L);
        return 1;
    }

    // Build a temporary DecalAsset with the material path and default values.
    // DecalAsset defaults are: blendMode=Opaque, lifetime=10s, fadeDuration=2s,
    // normalIntensity=0.8, roughnessOverride=-1, sortOrder=0,
    // halfExtents={0.5, 0.5, 0.2}.
    DecalAsset tempAsset;
    tempAsset.materialPath = materialPath;

    VfxDecal* decal = manager->SpawnDecal(
        tempAsset, position, normal, lifetimeOverride);

    if (!decal) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:spawn_decal -- failed to spawn decal '{}' "
            "(pool exhausted or manager uninitialized)", materialPath);
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, DecalToHandle(decal));
    return 1;
}

/// uz.vfx:fade_out_decal(handle, duration) -> void
///
/// Begins an early fade-out of a decal over the given duration.
///
/// # Parameters
/// - `handle`   (integer): Decal handle returned by `spawn_decal`.
/// - `duration` (number): Seconds over which to fade from current alpha to zero.
///
/// # Side Effects
/// - Calls `VfxDecal::FadeOut(duration)` on the resolved decal.
/// - Logs `VFX/Warning` if the manager is not registered or the handle is invalid.
static int DecalFadeOut(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const float duration     = static_cast<float>(luaL_checknumber(L, 3));

    DecalManager* manager = GetDecalManager(L);
    if (!manager) {
        UZ_LOG(VFX, Warning, "uz.vfx:fade_out_decal -- DecalManager not registered");
        return 0;
    }

    VfxDecal* decal = HandleToDecal(handle);
    if (!decal || !decal->isActive) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:fade_out_decal -- invalid or inactive decal handle");
        return 0;
    }

    decal->FadeOut(duration);
    return 0;
}

/// uz.vfx:destroy_decal(handle) -> void
///
/// Immediately marks a decal for destruction. The pool reclaims the slot
/// on the next `DecalManager::Update()` cleanup pass.
///
/// # Parameters
/// - `handle` (integer): Decal handle returned by `spawn_decal`.
///
/// # Side Effects
/// - Calls `VfxDecal::MarkForDestruction()` on the resolved decal.
/// - Logs `VFX/Warning` if the manager is not registered or the handle is invalid.
static int DecalDestroy(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    DecalManager* manager = GetDecalManager(L);
    if (!manager) {
        UZ_LOG(VFX, Warning, "uz.vfx:destroy_decal -- DecalManager not registered");
        return 0;
    }

    VfxDecal* decal = HandleToDecal(handle);
    if (!decal || !decal->isActive) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:destroy_decal -- invalid or inactive decal handle");
        return 0;
    }

    decal->MarkForDestruction();
    return 0;
}

/// uz.vfx:set_decal_blend_mode(handle, mode_string) -> void
///
/// Changes the blend mode of an active decal.
///
/// # Parameters
/// - `handle`      (integer): Decal handle returned by `spawn_decal`.
/// - `mode_string` (string): One of `"opaque"`, `"additive"`, `"multiply"`,
///                  `"screen"`.
///
/// # Side Effects
/// - Sets `VfxDecal::blendMode` to the parsed enum value.
/// - Errors via `luaL_error` if the mode string is not recognized.
/// - Logs `VFX/Warning` if the manager is not registered or the handle is invalid.
static int DecalSetBlendMode(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char* modeStr      = luaL_checkstring(L, 3);

    DecalManager* manager = GetDecalManager(L);
    if (!manager) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_decal_blend_mode -- DecalManager not registered");
        return 0;
    }

    VfxDecal* decal = HandleToDecal(handle);
    if (!decal || !decal->isActive) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_decal_blend_mode -- invalid or inactive decal handle");
        return 0;
    }

    DecalBlendMode mode;
    if (!ParseBlendMode(modeStr, mode)) {
        return luaL_error(L,
            "uz.vfx:set_decal_blend_mode -- unrecognized blend mode '%s' "
            "(expected 'opaque', 'additive', 'multiply', or 'screen')", modeStr);
    }

    decal->blendMode = mode;
    return 0;
}

/// uz.vfx:set_decal_normal_intensity(handle, value) -> void
///
/// Sets the normal map influence strength on an active decal.
///
/// # Parameters
/// - `handle` (integer): Decal handle returned by `spawn_decal`.
/// - `value`  (number): Normal intensity in [0, 1]. 0 = no normal override,
///            1 = full override.
///
/// # Side Effects
/// - Sets `VfxDecal::normalIntensity` to the given value.
/// - Logs `VFX/Warning` if the manager is not registered or the handle is invalid.
static int DecalSetNormalIntensity(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const float value        = static_cast<float>(luaL_checknumber(L, 3));

    DecalManager* manager = GetDecalManager(L);
    if (!manager) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_decal_normal_intensity -- DecalManager not registered");
        return 0;
    }

    VfxDecal* decal = HandleToDecal(handle);
    if (!decal || !decal->isActive) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_decal_normal_intensity -- invalid or inactive decal handle");
        return 0;
    }

    decal->normalIntensity = value;
    return 0;
}

/// uz.vfx:get_decal_stats() -> table
///
/// Returns a table with decal system statistics.
///
/// # Returns
/// A Lua table with the following fields:
/// - `active_count`    (integer): Number of decals currently active.
/// - `pool_available`  (integer): Number of free slots in the decal pool.
/// - `atlas_occupancy` (number):  Average atlas occupancy ratio [0.0, 1.0].
///
/// Returns a table with all zeros if the manager is not registered.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the manager is not registered.
static int DecalGetStats(lua_State* L) {
    DecalManager* manager = GetDecalManager(L);

    lua_newtable(L);

    if (!manager) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_decal_stats -- DecalManager not registered");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "active_count");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "pool_available");
        lua_pushnumber(L, 0.0);
        lua_setfield(L, -2, "atlas_occupancy");
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(manager->GetActiveCount()));
    lua_setfield(L, -2, "active_count");

    lua_pushinteger(L, static_cast<lua_Integer>(manager->GetPoolAvailable()));
    lua_setfield(L, -2, "pool_available");

    lua_pushnumber(L, static_cast<double>(manager->GetAtlasOccupancy()));
    lua_setfield(L, -2, "atlas_occupancy");

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration / Unregistration
// ─────────────────────────────────────────────────────────────────────────────

void RegisterDecalLuaBindings(lua_State* L, DecalManager* decalManager) {
    if (!L) {
        UZ_LOG(VFX, Error, "RegisterDecalLuaBindings: lua_State is null");
        return;
    }
    if (!decalManager) {
        UZ_LOG(VFX, Error, "RegisterDecalLuaBindings: decalManager is null");
        return;
    }

    // Store DecalManager* in the registry for retrieval by C closures.
    lua_pushlightuserdata(L, static_cast<void*>(decalManager));
    lua_setfield(L, LUA_REGISTRYINDEX, kDecalManagerRegistryKey);

    // Retrieve the existing `uz` global.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(VFX, Error,
            "RegisterDecalLuaBindings: 'uz' global is not a table -- "
            "call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // Retrieve the existing `uz.vfx` sub-table.
    lua_getfield(L, -1, "vfx");
    if (!lua_istable(L, -1)) {
        UZ_LOG(VFX, Error,
            "RegisterDecalLuaBindings: 'uz.vfx' is not a table -- "
            "call RegisterVFXLuaBindings first");
        lua_pop(L, 2);  // pop nil + uz
        return;
    }

    // Add decal functions to the existing uz.vfx table.
    lua_pushcfunction(L, DecalSpawn);
    lua_setfield(L, -2, "spawn_decal");

    lua_pushcfunction(L, DecalFadeOut);
    lua_setfield(L, -2, "fade_out_decal");

    lua_pushcfunction(L, DecalDestroy);
    lua_setfield(L, -2, "destroy_decal");

    lua_pushcfunction(L, DecalSetBlendMode);
    lua_setfield(L, -2, "set_decal_blend_mode");

    lua_pushcfunction(L, DecalSetNormalIntensity);
    lua_setfield(L, -2, "set_decal_normal_intensity");

    lua_pushcfunction(L, DecalGetStats);
    lua_setfield(L, -2, "get_decal_stats");

    lua_pop(L, 2);  // pop uz.vfx + uz

    UZ_LOG(VFX, Info, "uz.vfx decal Lua API registered (6 functions)");
}

void UnregisterDecalLuaBindings(lua_State* L) {
    if (!L) {
        return;
    }

    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kDecalManagerRegistryKey);

    UZ_LOG(VFX, Info, "uz.vfx decal Lua API unregistered");
}

} // namespace uz
