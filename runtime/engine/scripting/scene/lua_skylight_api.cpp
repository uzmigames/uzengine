/// Lua bindings for the sky light API (intensity, cubemap, lower hemisphere, occlusion).

#include "lua_skylight_api.h"

#include "log.h"
#include "world.h"
#include "environment/sky_light_component.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>

static_assert(sizeof(void*) >= 8,
    "uz::Entity requires 64-bit pointers for lightuserdata encoding");

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key + entity encoding (same as lua_scene_api.cpp)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kWorldRegistryKey = "uz_world";

[[nodiscard]] static Entity UserdataToEntity(void* p) noexcept {
    uint64_t packed;
    static_assert(sizeof(packed) == sizeof(p));
    std::memcpy(&packed, &p, sizeof(packed));
    Entity e;
    e.id         = static_cast<uint32_t>(packed >> 32);
    e.generation = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
    return e;
}

[[nodiscard]] static World* GetWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<World*>(p);
}

[[nodiscard]] static Entity CheckEntity(lua_State* L, int idx, const char* funcName) {
    if (!lua_islightuserdata(L, idx)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.{}: argument {} must be an entity (lightuserdata), got {}",
            funcName, idx, lua_typename(L, lua_type(L, idx)));
        return Entity::Invalid();
    }
    return UserdataToEntity(lua_touserdata(L, idx));
}

// ─────────────────────────────────────────────────────────────────────────────
// SkyLight (15.6)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.create_sky_light — idempotent: adds SkyLightComponent if absent.
static int SceneCreateSkyLight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.create_sky_light: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "create_sky_light");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        world->AddComponent<SkyLightComponent>(entity);
    }
    return 0;
}

/// uz.scene.set_sky_light_intensity — sets SkyLightComponent::intensity.
static int SceneSetSkyLightIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_intensity: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_intensity: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).intensity =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_light_color — sets SkyLightComponent::lightColor (r, g, b).
static int SceneSetSkyLightColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_color: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_color");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_color: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    auto& sky = world->GetComponent<SkyLightComponent>(entity);
    sky.lightColor.x = static_cast<float>(luaL_checknumber(L, 2));
    sky.lightColor.y = static_cast<float>(luaL_checknumber(L, 3));
    sky.lightColor.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_sky_light_min_occlusion — sets SkyLightComponent::minOcclusion.
static int SceneSetSkyLightMinOcclusion(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_min_occlusion: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_min_occlusion");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_min_occlusion: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).minOcclusion =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_light_indirect_intensity — sets SkyLightComponent::indirectLightingIntensity.
static int SceneSetSkyLightIndirectIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_indirect_intensity: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_indirect_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_indirect_intensity: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).indirectLightingIntensity =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_light_source_type — sets SkyLightComponent::sourceType.
/// Accepts "captured" or "cubemap" string.
static int SceneSetSkyLightSourceType(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_source_type: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_source_type");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_source_type: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    const char* typeStr = luaL_checkstring(L, 2);
    auto& skyLight = world->GetComponent<SkyLightComponent>(entity);
    if (std::strcmp(typeStr, "captured") == 0) {
        skyLight.sourceType = ESkyLightSourceType::CapturedScene;
    } else if (std::strcmp(typeStr, "cubemap") == 0) {
        skyLight.sourceType = ESkyLightSourceType::SpecifiedCubemap;
    } else {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_sky_light_source_type: unknown type '{}', expected 'captured' or 'cubemap'",
            typeStr);
    }
    return 0;
}

/// uz.scene.set_sky_light_real_time_capture — sets SkyLightComponent::realTimeCapture.
static int SceneSetSkyLightRealTimeCapture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_real_time_capture: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_real_time_capture");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_real_time_capture: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).realTimeCapture = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.set_sky_light_cubemap_resolution — sets SkyLightComponent::cubemapResolution.
static int SceneSetSkyLightCubemapResolution(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_cubemap_resolution: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_cubemap_resolution");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_cubemap_resolution: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).cubemapResolution =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.set_sky_light_lower_hemisphere_black — sets SkyLightComponent::lowerHemisphereIsBlack.
static int SceneSetSkyLightLowerHemisphereBlack(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_lower_hemisphere_black: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_lower_hemisphere_black");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_lower_hemisphere_black: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).lowerHemisphereIsBlack = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.set_sky_light_lower_hemisphere_color — sets SkyLightComponent::lowerHemisphereColor (r, g, b).
static int SceneSetSkyLightLowerHemisphereColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_lower_hemisphere_color: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_lower_hemisphere_color");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_lower_hemisphere_color: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    auto& sky = world->GetComponent<SkyLightComponent>(entity);
    sky.lowerHemisphereColor.x = static_cast<float>(luaL_checknumber(L, 2));
    sky.lowerHemisphereColor.y = static_cast<float>(luaL_checknumber(L, 3));
    sky.lowerHemisphereColor.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_sky_light_occlusion_max_distance — sets SkyLightComponent::occlusionMaxDistance.
static int SceneSetSkyLightOcclusionMaxDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_occlusion_max_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_occlusion_max_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_occlusion_max_distance: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).occlusionMaxDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_light_occlusion_tint — sets SkyLightComponent::occlusionTint (r, g, b).
static int SceneSetSkyLightOcclusionTint(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_occlusion_tint: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_occlusion_tint");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_occlusion_tint: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    auto& sky = world->GetComponent<SkyLightComponent>(entity);
    sky.occlusionTint.x = static_cast<float>(luaL_checknumber(L, 2));
    sky.occlusionTint.y = static_cast<float>(luaL_checknumber(L, 3));
    sky.occlusionTint.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_sky_light_occlusion_exponent — sets SkyLightComponent::occlusionExponent.
static int SceneSetSkyLightOcclusionExponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_occlusion_exponent: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_occlusion_exponent");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_occlusion_exponent: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).occlusionExponent =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_light_enabled — sets SkyLightComponent::enabled.
static int SceneSetSkyLightEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_enabled: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_light_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_light_enabled: entity {} has no SkyLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyLightComponent>(entity).enabled = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.get_sky_light_intensity(entity) -> number | nil
static int SceneGetSkyLightIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<SkyLightComponent>(entity).intensity));
    return 1;
}

/// uz.scene.get_sky_light_color(entity) -> r, g, b | nil
static int SceneGetSkyLightColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_color");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& sky = world->GetComponent<SkyLightComponent>(entity);
    lua_pushnumber(L, static_cast<double>(sky.lightColor.x));
    lua_pushnumber(L, static_cast<double>(sky.lightColor.y));
    lua_pushnumber(L, static_cast<double>(sky.lightColor.z));
    return 3;
}

/// uz.scene.get_sky_light_min_occlusion(entity) -> number | nil
static int SceneGetSkyLightMinOcclusion(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_min_occlusion");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<SkyLightComponent>(entity).minOcclusion));
    return 1;
}

/// uz.scene.get_sky_light_indirect_intensity(entity) -> number | nil
static int SceneGetSkyLightIndirectIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_indirect_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<SkyLightComponent>(entity).indirectLightingIntensity));
    return 1;
}

/// uz.scene.get_sky_light_source_type(entity) -> "captured" | "cubemap" | nil
static int SceneGetSkyLightSourceType(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_source_type");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& sky = world->GetComponent<SkyLightComponent>(entity);
    if (sky.sourceType == ESkyLightSourceType::CapturedScene)
        lua_pushstring(L, "captured");
    else
        lua_pushstring(L, "cubemap");
    return 1;
}

/// uz.scene.is_sky_light_real_time_capture(entity) -> bool
static int SceneIsSkyLightRealTimeCapture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_sky_light_real_time_capture");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<SkyLightComponent>(entity).realTimeCapture ? 1 : 0);
    return 1;
}

/// uz.scene.get_sky_light_cubemap_resolution(entity) -> int | nil
static int SceneGetSkyLightCubemapResolution(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_cubemap_resolution");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<SkyLightComponent>(entity).cubemapResolution));
    return 1;
}

/// uz.scene.is_sky_light_lower_hemisphere_black(entity) -> bool
static int SceneIsSkyLightLowerHemisphereBlack(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_sky_light_lower_hemisphere_black");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<SkyLightComponent>(entity).lowerHemisphereIsBlack ? 1 : 0);
    return 1;
}

/// uz.scene.get_sky_light_lower_hemisphere_color(entity) -> r, g, b | nil
static int SceneGetSkyLightLowerHemisphereColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_lower_hemisphere_color");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& sky = world->GetComponent<SkyLightComponent>(entity);
    lua_pushnumber(L, static_cast<double>(sky.lowerHemisphereColor.x));
    lua_pushnumber(L, static_cast<double>(sky.lowerHemisphereColor.y));
    lua_pushnumber(L, static_cast<double>(sky.lowerHemisphereColor.z));
    return 3;
}

/// uz.scene.get_sky_light_occlusion_max_distance(entity) -> number | nil
static int SceneGetSkyLightOcclusionMaxDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_occlusion_max_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<SkyLightComponent>(entity).occlusionMaxDistance));
    return 1;
}

/// uz.scene.get_sky_light_occlusion_tint(entity) -> r, g, b | nil
static int SceneGetSkyLightOcclusionTint(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_occlusion_tint");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& sky = world->GetComponent<SkyLightComponent>(entity);
    lua_pushnumber(L, static_cast<double>(sky.occlusionTint.x));
    lua_pushnumber(L, static_cast<double>(sky.occlusionTint.y));
    lua_pushnumber(L, static_cast<double>(sky.occlusionTint.z));
    return 3;
}

/// uz.scene.get_sky_light_occlusion_exponent(entity) -> number | nil
static int SceneGetSkyLightOcclusionExponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_sky_light_occlusion_exponent");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<SkyLightComponent>(entity).occlusionExponent));
    return 1;
}

/// uz.scene.is_sky_light_enabled(entity) -> bool
static int SceneIsSkyLightEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_sky_light_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SkyLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<SkyLightComponent>(entity).enabled ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterSkyLightAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterSkyLightAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterSkyLightAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    lua_pushcfunction(L, SceneCreateSkyLight);                    lua_setfield(L, -2, "create_sky_light");
    lua_pushcfunction(L, SceneSetSkyLightIntensity);              lua_setfield(L, -2, "set_sky_light_intensity");
    lua_pushcfunction(L, SceneSetSkyLightColor);                  lua_setfield(L, -2, "set_sky_light_color");
    lua_pushcfunction(L, SceneSetSkyLightMinOcclusion);           lua_setfield(L, -2, "set_sky_light_min_occlusion");
    lua_pushcfunction(L, SceneSetSkyLightIndirectIntensity);      lua_setfield(L, -2, "set_sky_light_indirect_intensity");
    lua_pushcfunction(L, SceneSetSkyLightSourceType);             lua_setfield(L, -2, "set_sky_light_source_type");
    lua_pushcfunction(L, SceneSetSkyLightRealTimeCapture);        lua_setfield(L, -2, "set_sky_light_real_time_capture");
    lua_pushcfunction(L, SceneSetSkyLightCubemapResolution);      lua_setfield(L, -2, "set_sky_light_cubemap_resolution");
    lua_pushcfunction(L, SceneSetSkyLightLowerHemisphereBlack);   lua_setfield(L, -2, "set_sky_light_lower_hemisphere_black");
    lua_pushcfunction(L, SceneSetSkyLightLowerHemisphereColor);   lua_setfield(L, -2, "set_sky_light_lower_hemisphere_color");
    lua_pushcfunction(L, SceneSetSkyLightOcclusionMaxDistance);   lua_setfield(L, -2, "set_sky_light_occlusion_max_distance");
    lua_pushcfunction(L, SceneSetSkyLightOcclusionTint);          lua_setfield(L, -2, "set_sky_light_occlusion_tint");
    lua_pushcfunction(L, SceneSetSkyLightOcclusionExponent);      lua_setfield(L, -2, "set_sky_light_occlusion_exponent");
    lua_pushcfunction(L, SceneSetSkyLightEnabled);                lua_setfield(L, -2, "set_sky_light_enabled");
    lua_pushcfunction(L, SceneGetSkyLightIntensity);              lua_setfield(L, -2, "get_sky_light_intensity");
    lua_pushcfunction(L, SceneGetSkyLightColor);                  lua_setfield(L, -2, "get_sky_light_color");
    lua_pushcfunction(L, SceneGetSkyLightMinOcclusion);           lua_setfield(L, -2, "get_sky_light_min_occlusion");
    lua_pushcfunction(L, SceneGetSkyLightIndirectIntensity);      lua_setfield(L, -2, "get_sky_light_indirect_intensity");
    lua_pushcfunction(L, SceneGetSkyLightSourceType);             lua_setfield(L, -2, "get_sky_light_source_type");
    lua_pushcfunction(L, SceneIsSkyLightRealTimeCapture);         lua_setfield(L, -2, "is_sky_light_real_time_capture");
    lua_pushcfunction(L, SceneGetSkyLightCubemapResolution);      lua_setfield(L, -2, "get_sky_light_cubemap_resolution");
    lua_pushcfunction(L, SceneIsSkyLightLowerHemisphereBlack);    lua_setfield(L, -2, "is_sky_light_lower_hemisphere_black");
    lua_pushcfunction(L, SceneGetSkyLightLowerHemisphereColor);   lua_setfield(L, -2, "get_sky_light_lower_hemisphere_color");
    lua_pushcfunction(L, SceneGetSkyLightOcclusionMaxDistance);   lua_setfield(L, -2, "get_sky_light_occlusion_max_distance");
    lua_pushcfunction(L, SceneGetSkyLightOcclusionTint);          lua_setfield(L, -2, "get_sky_light_occlusion_tint");
    lua_pushcfunction(L, SceneGetSkyLightOcclusionExponent);      lua_setfield(L, -2, "get_sky_light_occlusion_exponent");
    lua_pushcfunction(L, SceneIsSkyLightEnabled);                 lua_setfield(L, -2, "is_sky_light_enabled");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene sky light API registered");
}

} // namespace uz
