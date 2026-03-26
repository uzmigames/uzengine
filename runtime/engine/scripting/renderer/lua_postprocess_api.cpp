#include "lua_postprocess_api.h"

#include "log.h"
#include "postprocess/post_process_volume.h"

/// Lua bindings for post-process volume API (color grading, bloom, tone mapping, bloom intensity).

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstring>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key constant
// ─────────────────────────────────────────────────────────────────────────────

/// Key used to store the PostProcessVolumeManager* in the Lua registry.
static constexpr const char* kPPManagerRegistryKey = "uz_postprocess_manager";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the PostProcessVolumeManager* from the Lua registry.
///
/// Returns null if `LuaSetPostProcessManager` was never called or was called
/// with null.
[[nodiscard]] static PostProcessVolumeManager* GetPPManager(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kPPManagerRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<PostProcessVolumeManager*>(p);
}

/// Resolves a string override flag name to its PostProcessOverride enum value.
///
/// Returns true and sets `out` on a successful match.
/// Returns false for unrecognized names.
[[nodiscard]] static bool OverrideFromName(const char* name, PostProcessOverride& out) {
    if (std::strcmp(name, "bloom_intensity")              == 0) { out = PostProcessOverride::BloomIntensity;                 return true; }
    if (std::strcmp(name, "bloom_threshold")              == 0) { out = PostProcessOverride::BloomThreshold;                 return true; }
    if (std::strcmp(name, "exposure_min")                 == 0) { out = PostProcessOverride::AutoExposureMinBrightness;      return true; }
    if (std::strcmp(name, "exposure_max")                 == 0) { out = PostProcessOverride::AutoExposureMaxBrightness;      return true; }
    if (std::strcmp(name, "exposure_bias")                == 0) { out = PostProcessOverride::AutoExposureBias;               return true; }
    if (std::strcmp(name, "exposure_speed_up")            == 0) { out = PostProcessOverride::AutoExposureSpeedUp;            return true; }
    if (std::strcmp(name, "exposure_speed_down")          == 0) { out = PostProcessOverride::AutoExposureSpeedDown;          return true; }
    if (std::strcmp(name, "dof_focal_distance")           == 0) { out = PostProcessOverride::DepthOfFieldFocalDistance;      return true; }
    if (std::strcmp(name, "dof_focal_region")             == 0) { out = PostProcessOverride::DepthOfFieldFocalRegion;        return true; }
    if (std::strcmp(name, "dof_near_transition")          == 0) { out = PostProcessOverride::DepthOfFieldNearTransition;     return true; }
    if (std::strcmp(name, "dof_far_transition")           == 0) { out = PostProcessOverride::DepthOfFieldFarTransition;      return true; }
    if (std::strcmp(name, "dof_fstop")                    == 0) { out = PostProcessOverride::DepthOfFieldFstop;              return true; }
    if (std::strcmp(name, "motion_blur_amount")           == 0) { out = PostProcessOverride::MotionBlurAmount;               return true; }
    if (std::strcmp(name, "motion_blur_max")              == 0) { out = PostProcessOverride::MotionBlurMax;                  return true; }
    if (std::strcmp(name, "motion_blur_target_fps")       == 0) { out = PostProcessOverride::MotionBlurTargetFPS;            return true; }
    if (std::strcmp(name, "white_temp")                   == 0) { out = PostProcessOverride::WhiteTemp;                      return true; }
    if (std::strcmp(name, "white_tint")                   == 0) { out = PostProcessOverride::WhiteTint;                      return true; }
    if (std::strcmp(name, "film_slope")                   == 0) { out = PostProcessOverride::FilmSlope;                      return true; }
    if (std::strcmp(name, "film_toe")                     == 0) { out = PostProcessOverride::FilmToe;                        return true; }
    if (std::strcmp(name, "film_shoulder")                == 0) { out = PostProcessOverride::FilmShoulder;                   return true; }
    if (std::strcmp(name, "film_black_clip")              == 0) { out = PostProcessOverride::FilmBlackClip;                  return true; }
    if (std::strcmp(name, "film_white_clip")              == 0) { out = PostProcessOverride::FilmWhiteClip;                  return true; }
    if (std::strcmp(name, "vignette_intensity")           == 0) { out = PostProcessOverride::VignetteIntensity;              return true; }
    if (std::strcmp(name, "ao_intensity")                 == 0) { out = PostProcessOverride::AmbientOcclusionIntensity;      return true; }
    if (std::strcmp(name, "ao_radius")                    == 0) { out = PostProcessOverride::AmbientOcclusionRadius;         return true; }
    if (std::strcmp(name, "ao_power")                     == 0) { out = PostProcessOverride::AmbientOcclusionPower;          return true; }
    if (std::strcmp(name, "ssr_intensity")                == 0) { out = PostProcessOverride::ScreenSpaceReflectionIntensity; return true; }
    if (std::strcmp(name, "ssr_quality")                  == 0) { out = PostProcessOverride::ScreenSpaceReflectionQuality;   return true; }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Volume management — uz.postprocess.add_volume / remove_volume / clear / count
// ─────────────────────────────────────────────────────────────────────────────

/// uz.postprocess.add_volume(table) -> id
///
/// Creates a PostProcessVolume from a Lua table and returns its unique ID.
///
/// Table fields (all optional):
/// - bounds_min_x, bounds_min_y, bounds_min_z (number, default 0)
/// - bounds_max_x, bounds_max_y, bounds_max_z (number, default 0)
/// - is_unbound (boolean, default false)
/// - priority (number, default 0)
/// - blend_radius (number, default 100)
/// - blend_weight (number, default 1)
/// - enabled (boolean, default true)
///
/// # Returns
/// Integer — unique volume ID (non-zero). Returns nil if manager is unavailable.
static int PPAddVolume(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.add_volume: PostProcessVolumeManager not set");
        lua_pushnil(L);
        return 1;
    }

    luaL_checktype(L, 1, LUA_TTABLE);

    PostProcessVolume vol;

    // bounds_min
    lua_getfield(L, 1, "bounds_min_x");
    vol.boundsMin.x = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);

    lua_getfield(L, 1, "bounds_min_y");
    vol.boundsMin.y = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);

    lua_getfield(L, 1, "bounds_min_z");
    vol.boundsMin.z = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);

    // bounds_max
    lua_getfield(L, 1, "bounds_max_x");
    vol.boundsMax.x = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);

    lua_getfield(L, 1, "bounds_max_y");
    vol.boundsMax.y = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);

    lua_getfield(L, 1, "bounds_max_z");
    vol.boundsMax.z = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);

    // is_unbound
    lua_getfield(L, 1, "is_unbound");
    vol.isUnbound = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : false;
    lua_pop(L, 1);

    // priority
    lua_getfield(L, 1, "priority");
    vol.priority = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);

    // blend_radius
    lua_getfield(L, 1, "blend_radius");
    vol.blendRadius = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 100.0f;
    lua_pop(L, 1);

    // blend_weight
    lua_getfield(L, 1, "blend_weight");
    vol.blendWeight = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 1.0f;
    lua_pop(L, 1);

    // enabled
    lua_getfield(L, 1, "enabled");
    vol.enabled = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : true;
    lua_pop(L, 1);

    const uint32_t id = mgr->AddVolume(vol);
    UZ_LOG(Scripting, Debug, "[Lua] uz.postprocess.add_volume: id={}", id);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

/// uz.postprocess.remove_volume(id)
///
/// Removes the volume with the given ID. No-op if not found.
///
/// # Arguments
/// * `id` (integer) — Volume ID returned by add_volume.
static int PPRemoveVolume(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.remove_volume: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    mgr->RemoveVolume(id);
    UZ_LOG(Scripting, Debug, "[Lua] uz.postprocess.remove_volume: id={}", id);
    return 0;
}

/// uz.postprocess.clear()
///
/// Removes all volumes from the manager.
static int PPClear(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.clear: PostProcessVolumeManager not set");
        return 0;
    }
    mgr->Clear();
    UZ_LOG(Scripting, Debug, "[Lua] uz.postprocess.clear");
    return 0;
}

/// uz.postprocess.get_volume_count() -> integer
///
/// Returns the total number of registered volumes (including disabled ones).
///
/// # Returns
/// Integer — volume count. Returns 0 if manager is unavailable.
static int PPGetVolumeCount(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(mgr->GetVolumeCount()));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Volume properties — enabled / priority / blend_weight / blend_radius
// ─────────────────────────────────────────────────────────────────────────────

/// uz.postprocess.set_volume_enabled(id, enabled)
///
/// Enables or disables a volume. Disabled volumes are skipped during blending.
///
/// # Arguments
/// * `id` (integer) — Volume ID.
/// * `enabled` (boolean) — True to enable.
static int PPSetVolumeEnabled(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_enabled: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_enabled: volume {} not found", id);
        return 0;
    }
    vol->enabled = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.postprocess.is_volume_enabled(id) -> boolean
///
/// Returns whether the volume is enabled.
///
/// # Returns
/// Boolean — true if enabled. Returns nil if volume not found.
static int PPIsVolumeEnabled(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        lua_pushnil(L);
        return 1;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, vol->enabled ? 1 : 0);
    return 1;
}

/// uz.postprocess.set_volume_priority(id, priority)
///
/// Sets the blending priority. Higher priority volumes override lower ones.
///
/// # Arguments
/// * `id` (integer) — Volume ID.
/// * `priority` (number) — Priority value.
static int PPSetVolumePriority(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_priority: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_priority: volume {} not found", id);
        return 0;
    }
    vol->priority = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.postprocess.get_volume_priority(id) -> number
///
/// Returns the volume's blending priority.
///
/// # Returns
/// Number — priority value. Returns nil if volume not found.
static int PPGetVolumePriority(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        lua_pushnil(L);
        return 1;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(vol->priority));
    return 1;
}

/// uz.postprocess.set_volume_blend_weight(id, weight)
///
/// Sets the overall contribution weight in [0, 1]. 0 = no effect, 1 = full.
///
/// # Arguments
/// * `id` (integer) — Volume ID.
/// * `weight` (number) — Blend weight in [0, 1].
static int PPSetVolumeBlendWeight(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_blend_weight: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_blend_weight: volume {} not found", id);
        return 0;
    }
    vol->blendWeight = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.postprocess.get_volume_blend_weight(id) -> number
///
/// Returns the volume's contribution weight.
///
/// # Returns
/// Number — blend weight. Returns nil if volume not found.
static int PPGetVolumeBlendWeight(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        lua_pushnil(L);
        return 1;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(vol->blendWeight));
    return 1;
}

/// uz.postprocess.set_volume_blend_radius(id, radius)
///
/// Sets the fade-in distance in centimeters as the camera approaches the AABB.
/// 0 = hard cut (on inside, off outside).
///
/// # Arguments
/// * `id` (integer) — Volume ID.
/// * `radius` (number) — Blend radius in centimeters.
static int PPSetVolumeBlendRadius(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_blend_radius: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_volume_blend_radius: volume {} not found", id);
        return 0;
    }
    vol->blendRadius = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.postprocess.get_volume_blend_radius(id) -> number
///
/// Returns the volume's blend radius in centimeters.
///
/// # Returns
/// Number — blend radius. Returns nil if volume not found.
static int PPGetVolumeBlendRadius(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        lua_pushnil(L);
        return 1;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(vol->blendRadius));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings setters — each also sets the corresponding override flag
// ─────────────────────────────────────────────────────────────────────────────

/// uz.postprocess.set_bloom_intensity(id, v)
static int PPSetBloomIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_bloom_intensity: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_bloom_intensity: volume {} not found", id); return 0; }
    vol->settings.bloomIntensity = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::BloomIntensity);
    return 0;
}

/// uz.postprocess.get_bloom_intensity(id) -> number
static int PPGetBloomIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.bloomIntensity));
    return 1;
}

/// uz.postprocess.set_bloom_threshold(id, v)
static int PPSetBloomThreshold(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_bloom_threshold: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_bloom_threshold: volume {} not found", id); return 0; }
    vol->settings.bloomThreshold = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::BloomThreshold);
    return 0;
}

/// uz.postprocess.get_bloom_threshold(id) -> number
static int PPGetBloomThreshold(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.bloomThreshold));
    return 1;
}

/// uz.postprocess.set_exposure_min(id, v)
static int PPSetExposureMin(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_min: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_min: volume {} not found", id); return 0; }
    vol->settings.autoExposureMinBrightness = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AutoExposureMinBrightness);
    return 0;
}

/// uz.postprocess.get_exposure_min(id) -> number
static int PPGetExposureMin(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.autoExposureMinBrightness));
    return 1;
}

/// uz.postprocess.set_exposure_max(id, v)
static int PPSetExposureMax(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_max: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_max: volume {} not found", id); return 0; }
    vol->settings.autoExposureMaxBrightness = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AutoExposureMaxBrightness);
    return 0;
}

/// uz.postprocess.get_exposure_max(id) -> number
static int PPGetExposureMax(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.autoExposureMaxBrightness));
    return 1;
}

/// uz.postprocess.set_exposure_bias(id, v)
static int PPSetExposureBias(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_bias: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_bias: volume {} not found", id); return 0; }
    vol->settings.autoExposureBias = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AutoExposureBias);
    return 0;
}

/// uz.postprocess.get_exposure_bias(id) -> number
static int PPGetExposureBias(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.autoExposureBias));
    return 1;
}

/// uz.postprocess.set_exposure_speed_up(id, v)
static int PPSetExposureSpeedUp(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_speed_up: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_speed_up: volume {} not found", id); return 0; }
    vol->settings.autoExposureSpeedUp = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AutoExposureSpeedUp);
    return 0;
}

/// uz.postprocess.get_exposure_speed_up(id) -> number
static int PPGetExposureSpeedUp(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.autoExposureSpeedUp));
    return 1;
}

/// uz.postprocess.set_exposure_speed_down(id, v)
static int PPSetExposureSpeedDown(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_speed_down: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_exposure_speed_down: volume {} not found", id); return 0; }
    vol->settings.autoExposureSpeedDown = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AutoExposureSpeedDown);
    return 0;
}

/// uz.postprocess.get_exposure_speed_down(id) -> number
static int PPGetExposureSpeedDown(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.autoExposureSpeedDown));
    return 1;
}

/// uz.postprocess.set_dof_focal_distance(id, v)
static int PPSetDofFocalDistance(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_focal_distance: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_focal_distance: volume {} not found", id); return 0; }
    vol->settings.depthOfFieldFocalDistance = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::DepthOfFieldFocalDistance);
    return 0;
}

/// uz.postprocess.get_dof_focal_distance(id) -> number
static int PPGetDofFocalDistance(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.depthOfFieldFocalDistance));
    return 1;
}

/// uz.postprocess.set_dof_focal_region(id, v)
static int PPSetDofFocalRegion(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_focal_region: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_focal_region: volume {} not found", id); return 0; }
    vol->settings.depthOfFieldFocalRegion = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::DepthOfFieldFocalRegion);
    return 0;
}

/// uz.postprocess.get_dof_focal_region(id) -> number
static int PPGetDofFocalRegion(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.depthOfFieldFocalRegion));
    return 1;
}

/// uz.postprocess.set_dof_near_transition(id, v)
static int PPSetDofNearTransition(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_near_transition: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_near_transition: volume {} not found", id); return 0; }
    vol->settings.depthOfFieldNearTransition = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::DepthOfFieldNearTransition);
    return 0;
}

/// uz.postprocess.get_dof_near_transition(id) -> number
static int PPGetDofNearTransition(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.depthOfFieldNearTransition));
    return 1;
}

/// uz.postprocess.set_dof_far_transition(id, v)
static int PPSetDofFarTransition(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_far_transition: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_far_transition: volume {} not found", id); return 0; }
    vol->settings.depthOfFieldFarTransition = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::DepthOfFieldFarTransition);
    return 0;
}

/// uz.postprocess.get_dof_far_transition(id) -> number
static int PPGetDofFarTransition(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.depthOfFieldFarTransition));
    return 1;
}

/// uz.postprocess.set_dof_fstop(id, v)
static int PPSetDofFstop(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_fstop: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_dof_fstop: volume {} not found", id); return 0; }
    vol->settings.depthOfFieldFstop = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::DepthOfFieldFstop);
    return 0;
}

/// uz.postprocess.get_dof_fstop(id) -> number
static int PPGetDofFstop(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.depthOfFieldFstop));
    return 1;
}

/// uz.postprocess.set_motion_blur_amount(id, v)
static int PPSetMotionBlurAmount(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_motion_blur_amount: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_motion_blur_amount: volume {} not found", id); return 0; }
    vol->settings.motionBlurAmount = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::MotionBlurAmount);
    return 0;
}

/// uz.postprocess.get_motion_blur_amount(id) -> number
static int PPGetMotionBlurAmount(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.motionBlurAmount));
    return 1;
}

/// uz.postprocess.set_motion_blur_max(id, v)
static int PPSetMotionBlurMax(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_motion_blur_max: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_motion_blur_max: volume {} not found", id); return 0; }
    vol->settings.motionBlurMax = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::MotionBlurMax);
    return 0;
}

/// uz.postprocess.get_motion_blur_max(id) -> number
static int PPGetMotionBlurMax(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.motionBlurMax));
    return 1;
}

/// uz.postprocess.set_motion_blur_target_fps(id, v)
///
/// # Arguments
/// * `v` (integer) — Target FPS for motion blur scaling.
static int PPSetMotionBlurTargetFPS(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_motion_blur_target_fps: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_motion_blur_target_fps: volume {} not found", id); return 0; }
    vol->settings.motionBlurTargetFPS = static_cast<int>(luaL_checkinteger(L, 2));
    vol->settings.SetOverride(PostProcessOverride::MotionBlurTargetFPS);
    return 0;
}

/// uz.postprocess.get_motion_blur_target_fps(id) -> integer
static int PPGetMotionBlurTargetFPS(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(vol->settings.motionBlurTargetFPS));
    return 1;
}

/// uz.postprocess.set_white_temp(id, v)
static int PPSetWhiteTemp(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_white_temp: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_white_temp: volume {} not found", id); return 0; }
    vol->settings.whiteTemp = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::WhiteTemp);
    return 0;
}

/// uz.postprocess.get_white_temp(id) -> number
static int PPGetWhiteTemp(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.whiteTemp));
    return 1;
}

/// uz.postprocess.set_white_tint(id, v)
static int PPSetWhiteTint(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_white_tint: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_white_tint: volume {} not found", id); return 0; }
    vol->settings.whiteTint = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::WhiteTint);
    return 0;
}

/// uz.postprocess.get_white_tint(id) -> number
static int PPGetWhiteTint(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.whiteTint));
    return 1;
}

/// uz.postprocess.set_film_slope(id, v)
static int PPSetFilmSlope(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_slope: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_slope: volume {} not found", id); return 0; }
    vol->settings.filmSlope = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::FilmSlope);
    return 0;
}

/// uz.postprocess.get_film_slope(id) -> number
static int PPGetFilmSlope(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.filmSlope));
    return 1;
}

/// uz.postprocess.set_film_toe(id, v)
static int PPSetFilmToe(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_toe: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_toe: volume {} not found", id); return 0; }
    vol->settings.filmToe = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::FilmToe);
    return 0;
}

/// uz.postprocess.get_film_toe(id) -> number
static int PPGetFilmToe(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.filmToe));
    return 1;
}

/// uz.postprocess.set_film_shoulder(id, v)
static int PPSetFilmShoulder(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_shoulder: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_shoulder: volume {} not found", id); return 0; }
    vol->settings.filmShoulder = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::FilmShoulder);
    return 0;
}

/// uz.postprocess.get_film_shoulder(id) -> number
static int PPGetFilmShoulder(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.filmShoulder));
    return 1;
}

/// uz.postprocess.set_film_black_clip(id, v)
static int PPSetFilmBlackClip(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_black_clip: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_black_clip: volume {} not found", id); return 0; }
    vol->settings.filmBlackClip = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::FilmBlackClip);
    return 0;
}

/// uz.postprocess.get_film_black_clip(id) -> number
static int PPGetFilmBlackClip(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.filmBlackClip));
    return 1;
}

/// uz.postprocess.set_film_white_clip(id, v)
static int PPSetFilmWhiteClip(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_white_clip: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_film_white_clip: volume {} not found", id); return 0; }
    vol->settings.filmWhiteClip = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::FilmWhiteClip);
    return 0;
}

/// uz.postprocess.get_film_white_clip(id) -> number
static int PPGetFilmWhiteClip(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.filmWhiteClip));
    return 1;
}

/// uz.postprocess.set_vignette_intensity(id, v)
static int PPSetVignetteIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_vignette_intensity: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_vignette_intensity: volume {} not found", id); return 0; }
    vol->settings.vignetteIntensity = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::VignetteIntensity);
    return 0;
}

/// uz.postprocess.get_vignette_intensity(id) -> number
static int PPGetVignetteIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.vignetteIntensity));
    return 1;
}

/// uz.postprocess.set_ao_intensity(id, v)
static int PPSetAoIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ao_intensity: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ao_intensity: volume {} not found", id); return 0; }
    vol->settings.ambientOcclusionIntensity = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AmbientOcclusionIntensity);
    return 0;
}

/// uz.postprocess.get_ao_intensity(id) -> number
static int PPGetAoIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.ambientOcclusionIntensity));
    return 1;
}

/// uz.postprocess.set_ao_radius(id, v)
static int PPSetAoRadius(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ao_radius: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ao_radius: volume {} not found", id); return 0; }
    vol->settings.ambientOcclusionRadius = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AmbientOcclusionRadius);
    return 0;
}

/// uz.postprocess.get_ao_radius(id) -> number
static int PPGetAoRadius(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.ambientOcclusionRadius));
    return 1;
}

/// uz.postprocess.set_ao_power(id, v)
static int PPSetAoPower(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ao_power: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ao_power: volume {} not found", id); return 0; }
    vol->settings.ambientOcclusionPower = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::AmbientOcclusionPower);
    return 0;
}

/// uz.postprocess.get_ao_power(id) -> number
static int PPGetAoPower(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.ambientOcclusionPower));
    return 1;
}

/// uz.postprocess.set_ssr_intensity(id, v)
static int PPSetSsrIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ssr_intensity: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ssr_intensity: volume {} not found", id); return 0; }
    vol->settings.screenSpaceReflectionIntensity = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::ScreenSpaceReflectionIntensity);
    return 0;
}

/// uz.postprocess.get_ssr_intensity(id) -> number
static int PPGetSsrIntensity(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.screenSpaceReflectionIntensity));
    return 1;
}

/// uz.postprocess.set_ssr_quality(id, v)
static int PPSetSsrQuality(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ssr_quality: PostProcessVolumeManager not set"); return 0; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) { UZ_LOG(Scripting, Warning, "uz.postprocess.set_ssr_quality: volume {} not found", id); return 0; }
    vol->settings.screenSpaceReflectionQuality = static_cast<float>(luaL_checknumber(L, 2));
    vol->settings.SetOverride(PostProcessOverride::ScreenSpaceReflectionQuality);
    return 0;
}

/// uz.postprocess.get_ssr_quality(id) -> number
static int PPGetSsrQuality(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const auto* vol = mgr->FindVolume(id);
    if (!vol) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(vol->settings.screenSpaceReflectionQuality));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Override management
// ─────────────────────────────────────────────────────────────────────────────

/// uz.postprocess.set_all_overrides(id)
///
/// Sets all 28 override flags on the volume so every field participates in blending.
///
/// # Arguments
/// * `id` (integer) — Volume ID.
static int PPSetAllOverrides(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_all_overrides: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_all_overrides: volume {} not found", id);
        return 0;
    }
    vol->settings.SetAllOverrides();
    return 0;
}

/// uz.postprocess.clear_all_overrides(id)
///
/// Clears all 28 override flags on the volume so no field participates in blending.
///
/// # Arguments
/// * `id` (integer) — Volume ID.
static int PPClearAllOverrides(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.clear_all_overrides: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.clear_all_overrides: volume {} not found", id);
        return 0;
    }
    vol->settings.ClearAllOverrides();
    return 0;
}

/// uz.postprocess.set_override(id, flag_name)
///
/// Sets a single override flag by name. Flag names match the setter function suffix:
/// "bloom_intensity", "bloom_threshold", "exposure_min", "exposure_max",
/// "exposure_bias", "exposure_speed_up", "exposure_speed_down",
/// "dof_focal_distance", "dof_focal_region", "dof_near_transition",
/// "dof_far_transition", "dof_fstop",
/// "motion_blur_amount", "motion_blur_max", "motion_blur_target_fps",
/// "white_temp", "white_tint",
/// "film_slope", "film_toe", "film_shoulder", "film_black_clip", "film_white_clip",
/// "vignette_intensity",
/// "ao_intensity", "ao_radius", "ao_power",
/// "ssr_intensity", "ssr_quality".
///
/// # Arguments
/// * `id` (integer) — Volume ID.
/// * `flag_name` (string) — Override flag name.
static int PPSetOverride(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_override: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_override: volume {} not found", id);
        return 0;
    }
    const char* name = luaL_checkstring(L, 2);
    PostProcessOverride flag{};
    if (!OverrideFromName(name, flag)) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.set_override: unknown flag name '{}'", name);
        return 0;
    }
    vol->settings.SetOverride(flag);
    return 0;
}

/// uz.postprocess.clear_override(id, flag_name)
///
/// Clears a single override flag by name. See set_override for valid flag names.
///
/// # Arguments
/// * `id` (integer) — Volume ID.
/// * `flag_name` (string) — Override flag name.
static int PPClearOverride(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.clear_override: PostProcessVolumeManager not set");
        return 0;
    }
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* vol = mgr->FindVolume(id);
    if (!vol) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.clear_override: volume {} not found", id);
        return 0;
    }
    const char* name = luaL_checkstring(L, 2);
    PostProcessOverride flag{};
    if (!OverrideFromName(name, flag)) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.clear_override: unknown flag name '{}'", name);
        return 0;
    }
    vol->settings.ClearOverride(flag);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Blending query
// ─────────────────────────────────────────────────────────────────────────────

/// uz.postprocess.compute_blended(x, y, z) -> table
///
/// Computes the final blended PostProcessSettings for a camera at world position
/// (x, y, z) in centimeters. All active volumes are blended by priority, blend
/// radius, and blend weight.
///
/// # Arguments
/// * `x` (number) — Camera world X in centimeters.
/// * `y` (number) — Camera world Y in centimeters.
/// * `z` (number) — Camera world Z in centimeters.
///
/// # Returns
/// Table with all 28 blended settings fields:
/// bloom_intensity, bloom_threshold, exposure_min, exposure_max,
/// exposure_bias, exposure_speed_up, exposure_speed_down,
/// dof_focal_distance, dof_focal_region, dof_near_transition,
/// dof_far_transition, dof_fstop,
/// motion_blur_amount, motion_blur_max, motion_blur_target_fps,
/// white_temp, white_tint,
/// film_slope, film_toe, film_shoulder, film_black_clip, film_white_clip,
/// vignette_intensity,
/// ao_intensity, ao_radius, ao_power,
/// ssr_intensity, ssr_quality.
/// Returns nil if manager is unavailable.
static int PPComputeBlended(lua_State* L) {
    auto* mgr = GetPPManager(L);
    if (!mgr) {
        UZ_LOG(Scripting, Warning, "uz.postprocess.compute_blended: PostProcessVolumeManager not set");
        lua_pushnil(L);
        return 1;
    }

    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float z = static_cast<float>(luaL_checknumber(L, 3));

    const PostProcessSettings s = mgr->ComputeBlendedSettings(Vec3{x, y, z});

    lua_newtable(L);

    // Bloom
    lua_pushnumber(L, static_cast<double>(s.bloomIntensity));
    lua_setfield(L, -2, "bloom_intensity");
    lua_pushnumber(L, static_cast<double>(s.bloomThreshold));
    lua_setfield(L, -2, "bloom_threshold");

    // Auto-Exposure
    lua_pushnumber(L, static_cast<double>(s.autoExposureMinBrightness));
    lua_setfield(L, -2, "exposure_min");
    lua_pushnumber(L, static_cast<double>(s.autoExposureMaxBrightness));
    lua_setfield(L, -2, "exposure_max");
    lua_pushnumber(L, static_cast<double>(s.autoExposureBias));
    lua_setfield(L, -2, "exposure_bias");
    lua_pushnumber(L, static_cast<double>(s.autoExposureSpeedUp));
    lua_setfield(L, -2, "exposure_speed_up");
    lua_pushnumber(L, static_cast<double>(s.autoExposureSpeedDown));
    lua_setfield(L, -2, "exposure_speed_down");

    // Depth of Field
    lua_pushnumber(L, static_cast<double>(s.depthOfFieldFocalDistance));
    lua_setfield(L, -2, "dof_focal_distance");
    lua_pushnumber(L, static_cast<double>(s.depthOfFieldFocalRegion));
    lua_setfield(L, -2, "dof_focal_region");
    lua_pushnumber(L, static_cast<double>(s.depthOfFieldNearTransition));
    lua_setfield(L, -2, "dof_near_transition");
    lua_pushnumber(L, static_cast<double>(s.depthOfFieldFarTransition));
    lua_setfield(L, -2, "dof_far_transition");
    lua_pushnumber(L, static_cast<double>(s.depthOfFieldFstop));
    lua_setfield(L, -2, "dof_fstop");

    // Motion Blur
    lua_pushnumber(L, static_cast<double>(s.motionBlurAmount));
    lua_setfield(L, -2, "motion_blur_amount");
    lua_pushnumber(L, static_cast<double>(s.motionBlurMax));
    lua_setfield(L, -2, "motion_blur_max");
    lua_pushinteger(L, static_cast<lua_Integer>(s.motionBlurTargetFPS));
    lua_setfield(L, -2, "motion_blur_target_fps");

    // Color Grading
    lua_pushnumber(L, static_cast<double>(s.whiteTemp));
    lua_setfield(L, -2, "white_temp");
    lua_pushnumber(L, static_cast<double>(s.whiteTint));
    lua_setfield(L, -2, "white_tint");
    lua_pushnumber(L, static_cast<double>(s.filmSlope));
    lua_setfield(L, -2, "film_slope");
    lua_pushnumber(L, static_cast<double>(s.filmToe));
    lua_setfield(L, -2, "film_toe");
    lua_pushnumber(L, static_cast<double>(s.filmShoulder));
    lua_setfield(L, -2, "film_shoulder");
    lua_pushnumber(L, static_cast<double>(s.filmBlackClip));
    lua_setfield(L, -2, "film_black_clip");
    lua_pushnumber(L, static_cast<double>(s.filmWhiteClip));
    lua_setfield(L, -2, "film_white_clip");

    // Vignette
    lua_pushnumber(L, static_cast<double>(s.vignetteIntensity));
    lua_setfield(L, -2, "vignette_intensity");

    // SSAO
    lua_pushnumber(L, static_cast<double>(s.ambientOcclusionIntensity));
    lua_setfield(L, -2, "ao_intensity");
    lua_pushnumber(L, static_cast<double>(s.ambientOcclusionRadius));
    lua_setfield(L, -2, "ao_radius");
    lua_pushnumber(L, static_cast<double>(s.ambientOcclusionPower));
    lua_setfield(L, -2, "ao_power");

    // SSR
    lua_pushnumber(L, static_cast<double>(s.screenSpaceReflectionIntensity));
    lua_setfield(L, -2, "ssr_intensity");
    lua_pushnumber(L, static_cast<double>(s.screenSpaceReflectionQuality));
    lua_setfield(L, -2, "ssr_quality");

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterPostProcessAPI(lua_State* L) {
    // Retrieve the existing uz table.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterPostProcessAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // ── uz.postprocess = { ... } ────────────────────────────────────────────
    lua_newtable(L);

    // Volume management
    lua_pushcfunction(L, PPAddVolume);           lua_setfield(L, -2, "add_volume");
    lua_pushcfunction(L, PPRemoveVolume);        lua_setfield(L, -2, "remove_volume");
    lua_pushcfunction(L, PPClear);               lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, PPGetVolumeCount);      lua_setfield(L, -2, "get_volume_count");

    // Volume properties
    lua_pushcfunction(L, PPSetVolumeEnabled);    lua_setfield(L, -2, "set_volume_enabled");
    lua_pushcfunction(L, PPIsVolumeEnabled);     lua_setfield(L, -2, "is_volume_enabled");
    lua_pushcfunction(L, PPSetVolumePriority);   lua_setfield(L, -2, "set_volume_priority");
    lua_pushcfunction(L, PPGetVolumePriority);   lua_setfield(L, -2, "get_volume_priority");
    lua_pushcfunction(L, PPSetVolumeBlendWeight); lua_setfield(L, -2, "set_volume_blend_weight");
    lua_pushcfunction(L, PPGetVolumeBlendWeight); lua_setfield(L, -2, "get_volume_blend_weight");
    lua_pushcfunction(L, PPSetVolumeBlendRadius); lua_setfield(L, -2, "set_volume_blend_radius");
    lua_pushcfunction(L, PPGetVolumeBlendRadius); lua_setfield(L, -2, "get_volume_blend_radius");

    // Settings — bloom
    lua_pushcfunction(L, PPSetBloomIntensity);   lua_setfield(L, -2, "set_bloom_intensity");
    lua_pushcfunction(L, PPGetBloomIntensity);   lua_setfield(L, -2, "get_bloom_intensity");
    lua_pushcfunction(L, PPSetBloomThreshold);   lua_setfield(L, -2, "set_bloom_threshold");
    lua_pushcfunction(L, PPGetBloomThreshold);   lua_setfield(L, -2, "get_bloom_threshold");

    // Settings — auto-exposure
    lua_pushcfunction(L, PPSetExposureMin);      lua_setfield(L, -2, "set_exposure_min");
    lua_pushcfunction(L, PPGetExposureMin);      lua_setfield(L, -2, "get_exposure_min");
    lua_pushcfunction(L, PPSetExposureMax);      lua_setfield(L, -2, "set_exposure_max");
    lua_pushcfunction(L, PPGetExposureMax);      lua_setfield(L, -2, "get_exposure_max");
    lua_pushcfunction(L, PPSetExposureBias);     lua_setfield(L, -2, "set_exposure_bias");
    lua_pushcfunction(L, PPGetExposureBias);     lua_setfield(L, -2, "get_exposure_bias");
    lua_pushcfunction(L, PPSetExposureSpeedUp);  lua_setfield(L, -2, "set_exposure_speed_up");
    lua_pushcfunction(L, PPGetExposureSpeedUp);  lua_setfield(L, -2, "get_exposure_speed_up");
    lua_pushcfunction(L, PPSetExposureSpeedDown); lua_setfield(L, -2, "set_exposure_speed_down");
    lua_pushcfunction(L, PPGetExposureSpeedDown); lua_setfield(L, -2, "get_exposure_speed_down");

    // Settings — depth of field
    lua_pushcfunction(L, PPSetDofFocalDistance); lua_setfield(L, -2, "set_dof_focal_distance");
    lua_pushcfunction(L, PPGetDofFocalDistance); lua_setfield(L, -2, "get_dof_focal_distance");
    lua_pushcfunction(L, PPSetDofFocalRegion);   lua_setfield(L, -2, "set_dof_focal_region");
    lua_pushcfunction(L, PPGetDofFocalRegion);   lua_setfield(L, -2, "get_dof_focal_region");
    lua_pushcfunction(L, PPSetDofNearTransition); lua_setfield(L, -2, "set_dof_near_transition");
    lua_pushcfunction(L, PPGetDofNearTransition); lua_setfield(L, -2, "get_dof_near_transition");
    lua_pushcfunction(L, PPSetDofFarTransition); lua_setfield(L, -2, "set_dof_far_transition");
    lua_pushcfunction(L, PPGetDofFarTransition); lua_setfield(L, -2, "get_dof_far_transition");
    lua_pushcfunction(L, PPSetDofFstop);         lua_setfield(L, -2, "set_dof_fstop");
    lua_pushcfunction(L, PPGetDofFstop);         lua_setfield(L, -2, "get_dof_fstop");

    // Settings — motion blur
    lua_pushcfunction(L, PPSetMotionBlurAmount); lua_setfield(L, -2, "set_motion_blur_amount");
    lua_pushcfunction(L, PPGetMotionBlurAmount); lua_setfield(L, -2, "get_motion_blur_amount");
    lua_pushcfunction(L, PPSetMotionBlurMax);    lua_setfield(L, -2, "set_motion_blur_max");
    lua_pushcfunction(L, PPGetMotionBlurMax);    lua_setfield(L, -2, "get_motion_blur_max");
    lua_pushcfunction(L, PPSetMotionBlurTargetFPS); lua_setfield(L, -2, "set_motion_blur_target_fps");
    lua_pushcfunction(L, PPGetMotionBlurTargetFPS); lua_setfield(L, -2, "get_motion_blur_target_fps");

    // Settings — color grading
    lua_pushcfunction(L, PPSetWhiteTemp);        lua_setfield(L, -2, "set_white_temp");
    lua_pushcfunction(L, PPGetWhiteTemp);        lua_setfield(L, -2, "get_white_temp");
    lua_pushcfunction(L, PPSetWhiteTint);        lua_setfield(L, -2, "set_white_tint");
    lua_pushcfunction(L, PPGetWhiteTint);        lua_setfield(L, -2, "get_white_tint");
    lua_pushcfunction(L, PPSetFilmSlope);        lua_setfield(L, -2, "set_film_slope");
    lua_pushcfunction(L, PPGetFilmSlope);        lua_setfield(L, -2, "get_film_slope");
    lua_pushcfunction(L, PPSetFilmToe);          lua_setfield(L, -2, "set_film_toe");
    lua_pushcfunction(L, PPGetFilmToe);          lua_setfield(L, -2, "get_film_toe");
    lua_pushcfunction(L, PPSetFilmShoulder);     lua_setfield(L, -2, "set_film_shoulder");
    lua_pushcfunction(L, PPGetFilmShoulder);     lua_setfield(L, -2, "get_film_shoulder");
    lua_pushcfunction(L, PPSetFilmBlackClip);    lua_setfield(L, -2, "set_film_black_clip");
    lua_pushcfunction(L, PPGetFilmBlackClip);    lua_setfield(L, -2, "get_film_black_clip");
    lua_pushcfunction(L, PPSetFilmWhiteClip);    lua_setfield(L, -2, "set_film_white_clip");
    lua_pushcfunction(L, PPGetFilmWhiteClip);    lua_setfield(L, -2, "get_film_white_clip");

    // Settings — vignette
    lua_pushcfunction(L, PPSetVignetteIntensity); lua_setfield(L, -2, "set_vignette_intensity");
    lua_pushcfunction(L, PPGetVignetteIntensity); lua_setfield(L, -2, "get_vignette_intensity");

    // Settings — SSAO
    lua_pushcfunction(L, PPSetAoIntensity);      lua_setfield(L, -2, "set_ao_intensity");
    lua_pushcfunction(L, PPGetAoIntensity);      lua_setfield(L, -2, "get_ao_intensity");
    lua_pushcfunction(L, PPSetAoRadius);         lua_setfield(L, -2, "set_ao_radius");
    lua_pushcfunction(L, PPGetAoRadius);         lua_setfield(L, -2, "get_ao_radius");
    lua_pushcfunction(L, PPSetAoPower);          lua_setfield(L, -2, "set_ao_power");
    lua_pushcfunction(L, PPGetAoPower);          lua_setfield(L, -2, "get_ao_power");

    // Settings — SSR
    lua_pushcfunction(L, PPSetSsrIntensity);     lua_setfield(L, -2, "set_ssr_intensity");
    lua_pushcfunction(L, PPGetSsrIntensity);     lua_setfield(L, -2, "get_ssr_intensity");
    lua_pushcfunction(L, PPSetSsrQuality);       lua_setfield(L, -2, "set_ssr_quality");
    lua_pushcfunction(L, PPGetSsrQuality);       lua_setfield(L, -2, "get_ssr_quality");

    // Override management
    lua_pushcfunction(L, PPSetAllOverrides);     lua_setfield(L, -2, "set_all_overrides");
    lua_pushcfunction(L, PPClearAllOverrides);   lua_setfield(L, -2, "clear_all_overrides");
    lua_pushcfunction(L, PPSetOverride);         lua_setfield(L, -2, "set_override");
    lua_pushcfunction(L, PPClearOverride);       lua_setfield(L, -2, "clear_override");

    // Blending query
    lua_pushcfunction(L, PPComputeBlended);      lua_setfield(L, -2, "compute_blended");

    lua_setfield(L, -2, "postprocess");

    // Pop the uz table.
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.postprocess API registered");
}

void LuaSetPostProcessManager(lua_State* L, PostProcessVolumeManager* mgr) {
    lua_pushlightuserdata(L, static_cast<void*>(mgr));
    lua_setfield(L, LUA_REGISTRYINDEX, kPPManagerRegistryKey);
    UZ_LOG(Scripting, Debug, "LuaSetPostProcessManager: mgr={}", static_cast<void*>(mgr));
}

} // namespace uz
