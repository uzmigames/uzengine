#include "lua_environment_api.h"

#include "log.h"
#include "world.h"
#include "environment/height_fog_component.h"
#include "environment/sky_light_component.h"
#include "environment/sky_atmosphere_component.h"

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
// HeightFog (15.4)
// ─────────────────────────────────────────────────────────────────────────────

static int SceneSetFogDensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_density: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_density");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_density: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).density =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int SceneSetFogColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_color: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_color");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_color: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    auto& fog = world->GetComponent<HeightFogComponent>(entity);
    fog.color.x = static_cast<float>(luaL_checknumber(L, 2));
    fog.color.y = static_cast<float>(luaL_checknumber(L, 3));
    fog.color.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

static int SceneSetFogHeightFalloff(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_height_falloff: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_height_falloff");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_height_falloff: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).heightFalloff =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int SceneSetFogBaseHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_base_height: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_base_height");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_base_height: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).baseHeight =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int SceneSetFogMaxOpacity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_max_opacity: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_max_opacity");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_max_opacity: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).maxOpacity =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int SceneSetFogEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_enabled: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_enabled: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).enabled = lua_toboolean(L, 2) != 0;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// SkyLight (15.6)
// ─────────────────────────────────────────────────────────────────────────────

// uz.scene.create_sky_light — idempotent: adds SkyLightComponent if absent.
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

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmosphere artistic multipliers (15.5)
// ─────────────────────────────────────────────────────────────────────────────

static int SceneSetSkyRayleighScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_rayleigh_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_rayleigh_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_rayleigh_scale: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).rayleighScatteringScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int SceneSetSkyMieScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_mie_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_scale: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).mieScatteringScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int SceneSetSkyMultiScattering(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_multi_scattering: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_multi_scattering");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_multi_scattering: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).multiScatteringFactor =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterEnvironmentAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterEnvironmentAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterEnvironmentAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    // HeightFog
    lua_pushcfunction(L, SceneSetFogDensity);      lua_setfield(L, -2, "set_fog_density");
    lua_pushcfunction(L, SceneSetFogColor);        lua_setfield(L, -2, "set_fog_color");
    lua_pushcfunction(L, SceneSetFogHeightFalloff);lua_setfield(L, -2, "set_fog_height_falloff");
    lua_pushcfunction(L, SceneSetFogBaseHeight);   lua_setfield(L, -2, "set_fog_base_height");
    lua_pushcfunction(L, SceneSetFogMaxOpacity);   lua_setfield(L, -2, "set_fog_max_opacity");
    lua_pushcfunction(L, SceneSetFogEnabled);      lua_setfield(L, -2, "set_fog_enabled");

    // SkyLight
    lua_pushcfunction(L, SceneCreateSkyLight);          lua_setfield(L, -2, "create_sky_light");
    lua_pushcfunction(L, SceneSetSkyLightIntensity);    lua_setfield(L, -2, "set_sky_light_intensity");
    lua_pushcfunction(L, SceneSetSkyLightColor);        lua_setfield(L, -2, "set_sky_light_color");
    lua_pushcfunction(L, SceneSetSkyLightMinOcclusion); lua_setfield(L, -2, "set_sky_light_min_occlusion");

    // SkyAtmosphere
    lua_pushcfunction(L, SceneSetSkyRayleighScale);  lua_setfield(L, -2, "set_sky_rayleigh_scale");
    lua_pushcfunction(L, SceneSetSkyMieScale);       lua_setfield(L, -2, "set_sky_mie_scale");
    lua_pushcfunction(L, SceneSetSkyMultiScattering);lua_setfield(L, -2, "set_sky_multi_scattering");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene environment API registered");
}

} // namespace uz
