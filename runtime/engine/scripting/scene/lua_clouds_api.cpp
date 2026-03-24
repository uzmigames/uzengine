#include "lua_clouds_api.h"

#include "log.h"
#include "world.h"
#include "environment/volumetric_clouds_component.h"

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
// VolumetricClouds
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.create_clouds — idempotent: adds VolumetricCloudsComponent if absent.
static int SceneCreateClouds(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.create_clouds: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "create_clouds");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        world->AddComponent<VolumetricCloudsComponent>(entity);
    }
    return 0;
}

/// uz.scene.set_clouds_layer_bottom_altitude — sets VolumetricCloudsComponent::layerBottomAltitudeKm.
static int SceneSetCloudsLayerBottomAltitude(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_layer_bottom_altitude: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_layer_bottom_altitude");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_layer_bottom_altitude: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).layerBottomAltitudeKm =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_layer_height — sets VolumetricCloudsComponent::layerHeightKm.
static int SceneSetCloudsLayerHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_layer_height: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_layer_height");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_layer_height: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).layerHeightKm =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_coverage — sets VolumetricCloudsComponent::coverage.
static int SceneSetCloudsCoverage(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_coverage: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_coverage");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_coverage: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).coverage =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_density — sets VolumetricCloudsComponent::density.
static int SceneSetCloudsDensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_density: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_density");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_density: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).density =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_type — sets VolumetricCloudsComponent::cloudType.
static int SceneSetCloudsType(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_type: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_type");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_type: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).cloudType =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_wind_velocity — sets VolumetricCloudsComponent::windVelocity (x, y, z) in km/s.
static int SceneSetCloudsWindVelocity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_wind_velocity: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_wind_velocity");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_wind_velocity: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    auto& clouds = world->GetComponent<VolumetricCloudsComponent>(entity);
    clouds.windVelocity.x = static_cast<float>(luaL_checknumber(L, 2));
    clouds.windVelocity.y = static_cast<float>(luaL_checknumber(L, 3));
    clouds.windVelocity.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_clouds_detail_wind_scale — sets VolumetricCloudsComponent::detailWindScale.
static int SceneSetCloudsDetailWindScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_detail_wind_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_detail_wind_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_detail_wind_scale: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).detailWindScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_extinction — sets VolumetricCloudsComponent::extinction (km^-1).
static int SceneSetCloudsExtinction(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_extinction: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_extinction");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_extinction: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).extinction =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_scattering_albedo — sets VolumetricCloudsComponent::scatteringAlbedo.
static int SceneSetCloudsScatteringAlbedo(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_scattering_albedo: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_scattering_albedo");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_scattering_albedo: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).scatteringAlbedo =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_phase_g — sets VolumetricCloudsComponent::phaseG.
static int SceneSetCloudsPhaseG(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_phase_g: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_phase_g");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_phase_g: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).phaseG =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_phase_g2 — sets VolumetricCloudsComponent::phaseG2.
static int SceneSetCloudsPhaseG2(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_phase_g2: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_phase_g2");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_phase_g2: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).phaseG2 =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_phase_blend — sets VolumetricCloudsComponent::phaseBlend.
static int SceneSetCloudsPhaseBlend(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_phase_blend: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_phase_blend");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_phase_blend: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).phaseBlend =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_ambient_multiplier — sets VolumetricCloudsComponent::ambientMultiplier.
static int SceneSetCloudsAmbientMultiplier(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_ambient_multiplier: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_ambient_multiplier");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_ambient_multiplier: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).ambientMultiplier =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_tracing_max_distance — sets VolumetricCloudsComponent::tracingMaxDistanceKm.
static int SceneSetCloudsTracingMaxDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_max_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_tracing_max_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_max_distance: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).tracingMaxDistanceKm =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_clouds_primary_steps — sets VolumetricCloudsComponent::primarySteps.
static int SceneSetCloudsPrimarySteps(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_primary_steps: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_primary_steps");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_primary_steps: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).primarySteps =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.set_clouds_light_steps — sets VolumetricCloudsComponent::lightSteps.
static int SceneSetCloudsLightSteps(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_light_steps: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_light_steps");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_light_steps: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).lightSteps =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.set_clouds_enabled — sets VolumetricCloudsComponent::enabled.
static int SceneSetCloudsEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_enabled: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_enabled: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).enabled = lua_toboolean(L, 2) != 0;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterCloudsAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterCloudsAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterCloudsAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    lua_pushcfunction(L, SceneCreateClouds);                    lua_setfield(L, -2, "create_clouds");
    lua_pushcfunction(L, SceneSetCloudsLayerBottomAltitude);    lua_setfield(L, -2, "set_clouds_layer_bottom_altitude");
    lua_pushcfunction(L, SceneSetCloudsLayerHeight);            lua_setfield(L, -2, "set_clouds_layer_height");
    lua_pushcfunction(L, SceneSetCloudsCoverage);               lua_setfield(L, -2, "set_clouds_coverage");
    lua_pushcfunction(L, SceneSetCloudsDensity);                lua_setfield(L, -2, "set_clouds_density");
    lua_pushcfunction(L, SceneSetCloudsType);                   lua_setfield(L, -2, "set_clouds_type");
    lua_pushcfunction(L, SceneSetCloudsWindVelocity);           lua_setfield(L, -2, "set_clouds_wind_velocity");
    lua_pushcfunction(L, SceneSetCloudsDetailWindScale);        lua_setfield(L, -2, "set_clouds_detail_wind_scale");
    lua_pushcfunction(L, SceneSetCloudsExtinction);             lua_setfield(L, -2, "set_clouds_extinction");
    lua_pushcfunction(L, SceneSetCloudsScatteringAlbedo);       lua_setfield(L, -2, "set_clouds_scattering_albedo");
    lua_pushcfunction(L, SceneSetCloudsPhaseG);                 lua_setfield(L, -2, "set_clouds_phase_g");
    lua_pushcfunction(L, SceneSetCloudsPhaseG2);                lua_setfield(L, -2, "set_clouds_phase_g2");
    lua_pushcfunction(L, SceneSetCloudsPhaseBlend);             lua_setfield(L, -2, "set_clouds_phase_blend");
    lua_pushcfunction(L, SceneSetCloudsAmbientMultiplier);      lua_setfield(L, -2, "set_clouds_ambient_multiplier");
    lua_pushcfunction(L, SceneSetCloudsTracingMaxDistance);     lua_setfield(L, -2, "set_clouds_tracing_max_distance");
    lua_pushcfunction(L, SceneSetCloudsPrimarySteps);           lua_setfield(L, -2, "set_clouds_primary_steps");
    lua_pushcfunction(L, SceneSetCloudsLightSteps);             lua_setfield(L, -2, "set_clouds_light_steps");
    lua_pushcfunction(L, SceneSetCloudsEnabled);                lua_setfield(L, -2, "set_clouds_enabled");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene clouds API registered");
}

} // namespace uz
