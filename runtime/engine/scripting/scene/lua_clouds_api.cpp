/// Lua bindings for the volumetric clouds API (coverage, density, wind, lighting).

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

/// Lua bindings for the volumetric clouds API (cloud shape, coverage, density, absorption).

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

/// uz.scene.get_clouds_layer_bottom_altitude(entity) -> number | nil
static int SceneGetCloudsLayerBottomAltitude(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_layer_bottom_altitude");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).layerBottomAltitudeKm));
    return 1;
}

/// uz.scene.get_clouds_layer_height(entity) -> number | nil
static int SceneGetCloudsLayerHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_layer_height");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).layerHeightKm));
    return 1;
}

/// uz.scene.get_clouds_coverage(entity) -> number | nil
static int SceneGetCloudsCoverage(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_coverage");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).coverage));
    return 1;
}

/// uz.scene.get_clouds_density(entity) -> number | nil
static int SceneGetCloudsDensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_density");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).density));
    return 1;
}

/// uz.scene.get_clouds_type(entity) -> number | nil
static int SceneGetCloudsType(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_type");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).cloudType));
    return 1;
}

/// uz.scene.get_clouds_wind_velocity(entity) -> x, y, z | nil
static int SceneGetCloudsWindVelocity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_wind_velocity");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& clouds = world->GetComponent<VolumetricCloudsComponent>(entity);
    lua_pushnumber(L, static_cast<double>(clouds.windVelocity.x));
    lua_pushnumber(L, static_cast<double>(clouds.windVelocity.y));
    lua_pushnumber(L, static_cast<double>(clouds.windVelocity.z));
    return 3;
}

/// uz.scene.get_clouds_detail_wind_scale(entity) -> number | nil
static int SceneGetCloudsDetailWindScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_detail_wind_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).detailWindScale));
    return 1;
}

/// uz.scene.get_clouds_extinction(entity) -> number | nil
static int SceneGetCloudsExtinction(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_extinction");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).extinction));
    return 1;
}

/// uz.scene.get_clouds_scattering_albedo(entity) -> number | nil
static int SceneGetCloudsScatteringAlbedo(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_scattering_albedo");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).scatteringAlbedo));
    return 1;
}

/// uz.scene.get_clouds_phase_g(entity) -> number | nil
static int SceneGetCloudsPhaseG(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_phase_g");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).phaseG));
    return 1;
}

/// uz.scene.get_clouds_phase_g2(entity) -> number | nil
static int SceneGetCloudsPhaseG2(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_phase_g2");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).phaseG2));
    return 1;
}

/// uz.scene.get_clouds_phase_blend(entity) -> number | nil
static int SceneGetCloudsPhaseBlend(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_phase_blend");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).phaseBlend));
    return 1;
}

/// uz.scene.get_clouds_ambient_multiplier(entity) -> number | nil
static int SceneGetCloudsAmbientMultiplier(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_ambient_multiplier");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).ambientMultiplier));
    return 1;
}

/// uz.scene.get_clouds_tracing_max_distance(entity) -> number | nil
static int SceneGetCloudsTracingMaxDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_tracing_max_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<VolumetricCloudsComponent>(entity).tracingMaxDistanceKm));
    return 1;
}

/// uz.scene.get_clouds_primary_steps(entity) -> int | nil
static int SceneGetCloudsPrimarySteps(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_primary_steps");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<VolumetricCloudsComponent>(entity).primarySteps));
    return 1;
}

/// uz.scene.get_clouds_light_steps(entity) -> int | nil
static int SceneGetCloudsLightSteps(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_light_steps");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<VolumetricCloudsComponent>(entity).lightSteps));
    return 1;
}

/// uz.scene.is_clouds_enabled(entity) -> bool
static int SceneIsCloudsEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_clouds_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<VolumetricCloudsComponent>(entity).enabled ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — tracing quality properties
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_clouds_view_sample_count_scale(entity, scale)
///
/// Sets the multiplier for primary ray-march samples during main view rendering. Default: 0.5.
static int SceneSetCloudsViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_view_sample_count_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_view_sample_count_scale: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).viewSampleCountScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_view_sample_count_scale(entity) -> number | nil
static int SceneGetCloudsViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).viewSampleCountScale));
    return 1;
}

/// uz.scene.set_clouds_reflection_view_sample_count_scale(entity, scale)
///
/// Sets the multiplier for sample count when tracing clouds in reflections. Default: 0.1.
static int SceneSetCloudsReflectionViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_reflection_view_sample_count_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_reflection_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_reflection_view_sample_count_scale: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).reflectionViewSampleCountScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_reflection_view_sample_count_scale(entity) -> number | nil
static int SceneGetCloudsReflectionViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_reflection_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).reflectionViewSampleCountScale));
    return 1;
}

/// uz.scene.set_clouds_shadow_view_sample_count_scale(entity, scale)
///
/// Sets the multiplier for sample count when tracing cloud shadows. Default: 0.5.
static int SceneSetCloudsShadowViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_shadow_view_sample_count_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_shadow_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_shadow_view_sample_count_scale: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).shadowViewSampleCountScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_shadow_view_sample_count_scale(entity) -> number | nil
static int SceneGetCloudsShadowViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_shadow_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).shadowViewSampleCountScale));
    return 1;
}

/// uz.scene.set_clouds_shadow_reflection_view_sample_count_scale(entity, scale)
///
/// Sets the multiplier for sample count when tracing cloud shadow reflections. Default: 0.1.
static int SceneSetCloudsShadowReflectionViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_shadow_reflection_view_sample_count_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_shadow_reflection_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_shadow_reflection_view_sample_count_scale: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).shadowReflectionViewSampleCountScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_shadow_reflection_view_sample_count_scale(entity) -> number | nil
static int SceneGetCloudsShadowReflectionViewSampleCountScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_shadow_reflection_view_sample_count_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).shadowReflectionViewSampleCountScale));
    return 1;
}

/// uz.scene.set_clouds_shadow_tracing_distance(entity, km)
///
/// Sets the maximum distance for shadow ray tracing in kilometers. Default: 10.0 km.
static int SceneSetCloudsShadowTracingDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_shadow_tracing_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_shadow_tracing_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_shadow_tracing_distance: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).shadowTracingDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_shadow_tracing_distance(entity) -> number | nil
static int SceneGetCloudsShadowTracingDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_shadow_tracing_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).shadowTracingDistance));
    return 1;
}

/// uz.scene.set_clouds_stop_tracing_transmittance_threshold(entity, threshold)
///
/// Sets the transmittance threshold below which ray tracing stops. Default: 0.01.
static int SceneSetCloudsStopTracingTransmittanceThreshold(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_stop_tracing_transmittance_threshold: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_stop_tracing_transmittance_threshold");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_stop_tracing_transmittance_threshold: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).stopTracingTransmittanceThreshold =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_stop_tracing_transmittance_threshold(entity) -> number | nil
static int SceneGetCloudsStopTracingTransmittanceThreshold(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_stop_tracing_transmittance_threshold");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).stopTracingTransmittanceThreshold));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — aerial perspective scattering properties
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_start_distance(entity, km)
///
/// Sets the distance from camera where Rayleigh scattering on clouds begins in km. Default: 0.0 km.
static int SceneSetCloudsAerialPerspectiveRayleighScatteringStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_start_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_aerial_perspective_rayleigh_scattering_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_start_distance: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveRayleighScatteringStartDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_aerial_perspective_rayleigh_scattering_start_distance(entity) -> number | nil
static int SceneGetCloudsAerialPerspectiveRayleighScatteringStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_aerial_perspective_rayleigh_scattering_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveRayleighScatteringStartDistance));
    return 1;
}

/// uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_fade_distance(entity, km)
///
/// Sets the distance over which Rayleigh scattering fades in on clouds in km. Default: 10.0 km.
static int SceneSetCloudsAerialPerspectiveRayleighScatteringFadeDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_fade_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_aerial_perspective_rayleigh_scattering_fade_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_fade_distance: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveRayleighScatteringFadeDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_aerial_perspective_rayleigh_scattering_fade_distance(entity) -> number | nil
static int SceneGetCloudsAerialPerspectiveRayleighScatteringFadeDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_aerial_perspective_rayleigh_scattering_fade_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveRayleighScatteringFadeDistance));
    return 1;
}

/// uz.scene.set_clouds_aerial_perspective_mie_scattering_start_distance(entity, km)
///
/// Sets the distance from camera where Mie scattering on clouds begins in km. Default: 0.0 km.
static int SceneSetCloudsAerialPerspectiveMieScatteringStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_mie_scattering_start_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_aerial_perspective_mie_scattering_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_mie_scattering_start_distance: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveMieScatteringStartDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_aerial_perspective_mie_scattering_start_distance(entity) -> number | nil
static int SceneGetCloudsAerialPerspectiveMieScatteringStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_aerial_perspective_mie_scattering_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveMieScatteringStartDistance));
    return 1;
}

/// uz.scene.set_clouds_aerial_perspective_mie_scattering_fade_distance(entity, km)
///
/// Sets the distance over which Mie scattering fades in on clouds in km. Default: 10.0 km.
static int SceneSetCloudsAerialPerspectiveMieScatteringFadeDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_mie_scattering_fade_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_aerial_perspective_mie_scattering_fade_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_aerial_perspective_mie_scattering_fade_distance: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveMieScatteringFadeDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_aerial_perspective_mie_scattering_fade_distance(entity) -> number | nil
static int SceneGetCloudsAerialPerspectiveMieScatteringFadeDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_aerial_perspective_mie_scattering_fade_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).aerialPerspectiveMieScatteringFadeDistance));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — sky light occlusion and tracing distance mode properties
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_clouds_sky_light_cloud_bottom_occlusion(entity, occlusion)
///
/// Sets the occlusion factor at the bottom of the cloud layer [0,1]. Default: 0.2.
static int SceneSetCloudsSkyLightCloudBottomOcclusion(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_sky_light_cloud_bottom_occlusion: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_sky_light_cloud_bottom_occlusion");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_sky_light_cloud_bottom_occlusion: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).skyLightCloudBottomOcclusion =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_sky_light_cloud_bottom_occlusion(entity) -> number | nil
static int SceneGetCloudsSkyLightCloudBottomOcclusion(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_sky_light_cloud_bottom_occlusion");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).skyLightCloudBottomOcclusion));
    return 1;
}

/// uz.scene.set_clouds_tracing_max_distance_mode(entity, mode)
///
/// Sets the tracing max distance mode: 0 = DistanceFromCloudLayerEntryPoint, 1 = DistanceFromPointOfView.
/// Default: 0.
static int SceneSetCloudsTracingMaxDistanceMode(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_max_distance_mode: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_tracing_max_distance_mode");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_max_distance_mode: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    const lua_Integer mode = luaL_checkinteger(L, 2);
    world->GetComponent<VolumetricCloudsComponent>(entity).tracingMaxDistanceMode =
        (mode == 1) ? TracingMaxDistanceMode::DistanceFromPointOfView
                    : TracingMaxDistanceMode::DistanceFromCloudLayerEntryPoint;
    return 0;
}

/// uz.scene.get_clouds_tracing_max_distance_mode(entity) -> int | nil
///
/// Returns 0 = DistanceFromCloudLayerEntryPoint, 1 = DistanceFromPointOfView.
static int SceneGetCloudsTracingMaxDistanceMode(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_tracing_max_distance_mode");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto mode = world->GetComponent<VolumetricCloudsComponent>(entity).tracingMaxDistanceMode;
    lua_pushinteger(L, mode == TracingMaxDistanceMode::DistanceFromPointOfView ? 1 : 0);
    return 1;
}

/// uz.scene.set_clouds_tracing_start_max_distance(entity, km)
///
/// Sets the maximum distance from the tracing origin in kilometers. Default: 350.0 km.
static int SceneSetCloudsTracingStartMaxDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_start_max_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_tracing_start_max_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_start_max_distance: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).tracingStartMaxDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_tracing_start_max_distance(entity) -> number | nil
static int SceneGetCloudsTracingStartMaxDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_tracing_start_max_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).tracingStartMaxDistance));
    return 1;
}

/// uz.scene.set_clouds_tracing_start_distance_from_camera(entity, km)
///
/// Sets the minimum distance from camera before cloud tracing begins in km. Default: 0.0 km.
static int SceneSetCloudsTracingStartDistanceFromCamera(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_start_distance_from_camera: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_clouds_tracing_start_distance_from_camera");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_clouds_tracing_start_distance_from_camera: entity {} has no VolumetricCloudsComponent", entity.id);
        return 0;
    }
    world->GetComponent<VolumetricCloudsComponent>(entity).tracingStartDistanceFromCamera =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_clouds_tracing_start_distance_from_camera(entity) -> number | nil
static int SceneGetCloudsTracingStartDistanceFromCamera(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_clouds_tracing_start_distance_from_camera");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<VolumetricCloudsComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<VolumetricCloudsComponent>(entity).tracingStartDistanceFromCamera));
    return 1;
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
    lua_pushcfunction(L, SceneGetCloudsLayerBottomAltitude);    lua_setfield(L, -2, "get_clouds_layer_bottom_altitude");
    lua_pushcfunction(L, SceneGetCloudsLayerHeight);            lua_setfield(L, -2, "get_clouds_layer_height");
    lua_pushcfunction(L, SceneGetCloudsCoverage);               lua_setfield(L, -2, "get_clouds_coverage");
    lua_pushcfunction(L, SceneGetCloudsDensity);                lua_setfield(L, -2, "get_clouds_density");
    lua_pushcfunction(L, SceneGetCloudsType);                   lua_setfield(L, -2, "get_clouds_type");
    lua_pushcfunction(L, SceneGetCloudsWindVelocity);           lua_setfield(L, -2, "get_clouds_wind_velocity");
    lua_pushcfunction(L, SceneGetCloudsDetailWindScale);        lua_setfield(L, -2, "get_clouds_detail_wind_scale");
    lua_pushcfunction(L, SceneGetCloudsExtinction);             lua_setfield(L, -2, "get_clouds_extinction");
    lua_pushcfunction(L, SceneGetCloudsScatteringAlbedo);       lua_setfield(L, -2, "get_clouds_scattering_albedo");
    lua_pushcfunction(L, SceneGetCloudsPhaseG);                 lua_setfield(L, -2, "get_clouds_phase_g");
    lua_pushcfunction(L, SceneGetCloudsPhaseG2);                lua_setfield(L, -2, "get_clouds_phase_g2");
    lua_pushcfunction(L, SceneGetCloudsPhaseBlend);             lua_setfield(L, -2, "get_clouds_phase_blend");
    lua_pushcfunction(L, SceneGetCloudsAmbientMultiplier);      lua_setfield(L, -2, "get_clouds_ambient_multiplier");
    lua_pushcfunction(L, SceneGetCloudsTracingMaxDistance);     lua_setfield(L, -2, "get_clouds_tracing_max_distance");
    lua_pushcfunction(L, SceneGetCloudsPrimarySteps);           lua_setfield(L, -2, "get_clouds_primary_steps");
    lua_pushcfunction(L, SceneGetCloudsLightSteps);             lua_setfield(L, -2, "get_clouds_light_steps");
    lua_pushcfunction(L, SceneIsCloudsEnabled);                 lua_setfield(L, -2, "is_clouds_enabled");

    // Tracing quality properties
    lua_pushcfunction(L, SceneSetCloudsViewSampleCountScale);                                  lua_setfield(L, -2, "set_clouds_view_sample_count_scale");
    lua_pushcfunction(L, SceneGetCloudsViewSampleCountScale);                                  lua_setfield(L, -2, "get_clouds_view_sample_count_scale");
    lua_pushcfunction(L, SceneSetCloudsReflectionViewSampleCountScale);                        lua_setfield(L, -2, "set_clouds_reflection_view_sample_count_scale");
    lua_pushcfunction(L, SceneGetCloudsReflectionViewSampleCountScale);                        lua_setfield(L, -2, "get_clouds_reflection_view_sample_count_scale");
    lua_pushcfunction(L, SceneSetCloudsShadowViewSampleCountScale);                            lua_setfield(L, -2, "set_clouds_shadow_view_sample_count_scale");
    lua_pushcfunction(L, SceneGetCloudsShadowViewSampleCountScale);                            lua_setfield(L, -2, "get_clouds_shadow_view_sample_count_scale");
    lua_pushcfunction(L, SceneSetCloudsShadowReflectionViewSampleCountScale);                  lua_setfield(L, -2, "set_clouds_shadow_reflection_view_sample_count_scale");
    lua_pushcfunction(L, SceneGetCloudsShadowReflectionViewSampleCountScale);                  lua_setfield(L, -2, "get_clouds_shadow_reflection_view_sample_count_scale");
    lua_pushcfunction(L, SceneSetCloudsShadowTracingDistance);                                 lua_setfield(L, -2, "set_clouds_shadow_tracing_distance");
    lua_pushcfunction(L, SceneGetCloudsShadowTracingDistance);                                 lua_setfield(L, -2, "get_clouds_shadow_tracing_distance");
    lua_pushcfunction(L, SceneSetCloudsStopTracingTransmittanceThreshold);                     lua_setfield(L, -2, "set_clouds_stop_tracing_transmittance_threshold");
    lua_pushcfunction(L, SceneGetCloudsStopTracingTransmittanceThreshold);                     lua_setfield(L, -2, "get_clouds_stop_tracing_transmittance_threshold");

    // Aerial perspective scattering properties
    lua_pushcfunction(L, SceneSetCloudsAerialPerspectiveRayleighScatteringStartDistance);      lua_setfield(L, -2, "set_clouds_aerial_perspective_rayleigh_scattering_start_distance");
    lua_pushcfunction(L, SceneGetCloudsAerialPerspectiveRayleighScatteringStartDistance);      lua_setfield(L, -2, "get_clouds_aerial_perspective_rayleigh_scattering_start_distance");
    lua_pushcfunction(L, SceneSetCloudsAerialPerspectiveRayleighScatteringFadeDistance);       lua_setfield(L, -2, "set_clouds_aerial_perspective_rayleigh_scattering_fade_distance");
    lua_pushcfunction(L, SceneGetCloudsAerialPerspectiveRayleighScatteringFadeDistance);       lua_setfield(L, -2, "get_clouds_aerial_perspective_rayleigh_scattering_fade_distance");
    lua_pushcfunction(L, SceneSetCloudsAerialPerspectiveMieScatteringStartDistance);           lua_setfield(L, -2, "set_clouds_aerial_perspective_mie_scattering_start_distance");
    lua_pushcfunction(L, SceneGetCloudsAerialPerspectiveMieScatteringStartDistance);           lua_setfield(L, -2, "get_clouds_aerial_perspective_mie_scattering_start_distance");
    lua_pushcfunction(L, SceneSetCloudsAerialPerspectiveMieScatteringFadeDistance);            lua_setfield(L, -2, "set_clouds_aerial_perspective_mie_scattering_fade_distance");
    lua_pushcfunction(L, SceneGetCloudsAerialPerspectiveMieScatteringFadeDistance);            lua_setfield(L, -2, "get_clouds_aerial_perspective_mie_scattering_fade_distance");

    // Sky light occlusion and tracing distance mode
    lua_pushcfunction(L, SceneSetCloudsSkyLightCloudBottomOcclusion);                         lua_setfield(L, -2, "set_clouds_sky_light_cloud_bottom_occlusion");
    lua_pushcfunction(L, SceneGetCloudsSkyLightCloudBottomOcclusion);                         lua_setfield(L, -2, "get_clouds_sky_light_cloud_bottom_occlusion");
    lua_pushcfunction(L, SceneSetCloudsTracingMaxDistanceMode);                                lua_setfield(L, -2, "set_clouds_tracing_max_distance_mode");
    lua_pushcfunction(L, SceneGetCloudsTracingMaxDistanceMode);                                lua_setfield(L, -2, "get_clouds_tracing_max_distance_mode");
    lua_pushcfunction(L, SceneSetCloudsTracingStartMaxDistance);                               lua_setfield(L, -2, "set_clouds_tracing_start_max_distance");
    lua_pushcfunction(L, SceneGetCloudsTracingStartMaxDistance);                               lua_setfield(L, -2, "get_clouds_tracing_start_max_distance");
    lua_pushcfunction(L, SceneSetCloudsTracingStartDistanceFromCamera);                        lua_setfield(L, -2, "set_clouds_tracing_start_distance_from_camera");
    lua_pushcfunction(L, SceneGetCloudsTracingStartDistanceFromCamera);                        lua_setfield(L, -2, "get_clouds_tracing_start_distance_from_camera");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene clouds API registered");
}

} // namespace uz
