/// Lua bindings for the height fog API (density, falloff, color, inscattering).

#include "lua_fog_api.h"

#include "log.h"
#include "world.h"
#include "environment/height_fog_component.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>

/// Lua bindings for the height fog API (density, height falloff, inscatter color).

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

/// uz.scene.create_fog — idempotent: adds HeightFogComponent if absent.
static int SceneCreateFog(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.create_fog: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "create_fog");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        world->AddComponent<HeightFogComponent>(entity);
    }
    return 0;
}

/// uz.scene.set_fog_density — sets HeightFogComponent::density.
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

/// uz.scene.set_fog_color — sets HeightFogComponent::color (r, g, b).
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

/// uz.scene.set_fog_height_falloff — sets HeightFogComponent::heightFalloff.
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

/// uz.scene.set_fog_base_height — sets HeightFogComponent::baseHeight.
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

/// uz.scene.set_fog_max_opacity — sets HeightFogComponent::maxOpacity.
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

/// uz.scene.set_fog_enabled — sets HeightFogComponent::enabled.
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

/// uz.scene.set_fog_start_distance — sets HeightFogComponent::startDistance.
static int SceneSetFogStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_start_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_start_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).startDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_fog_cutoff_distance — sets HeightFogComponent::fogCutoffDistance.
static int SceneSetFogCutoffDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_cutoff_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_cutoff_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_cutoff_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).fogCutoffDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_fog_second_density — sets HeightFogComponent::secondFogDensity.
static int SceneSetFogSecondDensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_second_density: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_second_density");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_second_density: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).secondFogDensity =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_fog_second_height_falloff — sets HeightFogComponent::secondFogHeightFalloff.
static int SceneSetFogSecondHeightFalloff(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_second_height_falloff: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_second_height_falloff");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_second_height_falloff: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).secondFogHeightFalloff =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_fog_second_base_height — sets HeightFogComponent::secondFogBaseHeight.
static int SceneSetFogSecondBaseHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_second_base_height: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_second_base_height");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_second_base_height: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).secondFogBaseHeight =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_fog_directional_inscattering_color — sets HeightFogComponent::directionalInscatteringColor (r, g, b).
static int SceneSetFogDirectionalInscatteringColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_directional_inscattering_color: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_directional_inscattering_color");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_directional_inscattering_color: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    auto& fog = world->GetComponent<HeightFogComponent>(entity);
    fog.directionalInscatteringColor.x = static_cast<float>(luaL_checknumber(L, 2));
    fog.directionalInscatteringColor.y = static_cast<float>(luaL_checknumber(L, 3));
    fog.directionalInscatteringColor.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_fog_directional_inscattering_exponent — sets HeightFogComponent::directionalInscatteringExponent.
static int SceneSetFogDirectionalInscatteringExponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_directional_inscattering_exponent: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_directional_inscattering_exponent");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_directional_inscattering_exponent: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).directionalInscatteringExponent =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_fog_directional_inscattering_start_distance — sets HeightFogComponent::directionalInscatteringStartDistance.
static int SceneSetFogDirectionalInscatteringStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_directional_inscattering_start_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_directional_inscattering_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_directional_inscattering_start_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).directionalInscatteringStartDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_fog_volumetric — sets HeightFogComponent::enableVolumetricFog.
static int SceneSetFogVolumetric(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).enableVolumetricFog = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.get_fog_density(entity) -> number | nil
static int SceneGetFogDensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_density");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).density));
    return 1;
}

/// uz.scene.get_fog_color(entity) -> r, g, b | nil
static int SceneGetFogColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_color");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& fog = world->GetComponent<HeightFogComponent>(entity);
    lua_pushnumber(L, static_cast<double>(fog.color.x));
    lua_pushnumber(L, static_cast<double>(fog.color.y));
    lua_pushnumber(L, static_cast<double>(fog.color.z));
    return 3;
}

/// uz.scene.get_fog_height_falloff(entity) -> number | nil
static int SceneGetFogHeightFalloff(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_height_falloff");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).heightFalloff));
    return 1;
}

/// uz.scene.get_fog_base_height(entity) -> number | nil
static int SceneGetFogBaseHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_base_height");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).baseHeight));
    return 1;
}

/// uz.scene.get_fog_max_opacity(entity) -> number | nil
static int SceneGetFogMaxOpacity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_max_opacity");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).maxOpacity));
    return 1;
}

/// uz.scene.is_fog_enabled(entity) -> bool
static int SceneIsFogEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_fog_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<HeightFogComponent>(entity).enabled ? 1 : 0);
    return 1;
}

/// uz.scene.get_fog_start_distance(entity) -> number | nil
static int SceneGetFogStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).startDistance));
    return 1;
}

/// uz.scene.get_fog_cutoff_distance(entity) -> number | nil
static int SceneGetFogCutoffDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_cutoff_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).fogCutoffDistance));
    return 1;
}

/// uz.scene.get_fog_second_density(entity) -> number | nil
static int SceneGetFogSecondDensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_second_density");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).secondFogDensity));
    return 1;
}

/// uz.scene.get_fog_second_height_falloff(entity) -> number | nil
static int SceneGetFogSecondHeightFalloff(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_second_height_falloff");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).secondFogHeightFalloff));
    return 1;
}

/// uz.scene.get_fog_second_base_height(entity) -> number | nil
static int SceneGetFogSecondBaseHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_second_base_height");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).secondFogBaseHeight));
    return 1;
}

/// uz.scene.get_fog_directional_inscattering_color(entity) -> r, g, b | nil
static int SceneGetFogDirectionalInscatteringColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_directional_inscattering_color");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& fog = world->GetComponent<HeightFogComponent>(entity);
    lua_pushnumber(L, static_cast<double>(fog.directionalInscatteringColor.x));
    lua_pushnumber(L, static_cast<double>(fog.directionalInscatteringColor.y));
    lua_pushnumber(L, static_cast<double>(fog.directionalInscatteringColor.z));
    return 3;
}

/// uz.scene.get_fog_directional_inscattering_exponent(entity) -> number | nil
static int SceneGetFogDirectionalInscatteringExponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_directional_inscattering_exponent");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).directionalInscatteringExponent));
    return 1;
}

/// uz.scene.get_fog_directional_inscattering_start_distance(entity) -> number | nil
static int SceneGetFogDirectionalInscatteringStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_directional_inscattering_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<HeightFogComponent>(entity).directionalInscatteringStartDistance));
    return 1;
}

/// uz.scene.is_fog_volumetric(entity) -> bool
static int SceneIsFogVolumetric(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_fog_volumetric");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<HeightFogComponent>(entity).enableVolumetricFog ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — volumetric fog extended properties
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_fog_volumetric_scattering_distribution(entity, value)
///
/// Sets the phase function asymmetry for volumetric fog [-1,1]. Default: 0.2.
static int SceneSetFogVolumetricScatteringDistribution(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_scattering_distribution: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_scattering_distribution");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_scattering_distribution: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).volumetricFogScatteringDistribution =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_volumetric_scattering_distribution(entity) -> number | nil
static int SceneGetFogVolumetricScatteringDistribution(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_scattering_distribution");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).volumetricFogScatteringDistribution));
    return 1;
}

/// uz.scene.set_fog_volumetric_albedo(entity, r, g, b)
///
/// Sets the single-scattering albedo of the volumetric fog medium. Default: (1,1,1).
static int SceneSetFogVolumetricAlbedo(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_albedo: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_albedo");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_albedo: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    auto& fog = world->GetComponent<HeightFogComponent>(entity);
    fog.volumetricFogAlbedo.x = static_cast<float>(luaL_checknumber(L, 2));
    fog.volumetricFogAlbedo.y = static_cast<float>(luaL_checknumber(L, 3));
    fog.volumetricFogAlbedo.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.get_fog_volumetric_albedo(entity) -> r, g, b | nil
static int SceneGetFogVolumetricAlbedo(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_albedo");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& v = world->GetComponent<HeightFogComponent>(entity).volumetricFogAlbedo;
    lua_pushnumber(L, static_cast<double>(v.x));
    lua_pushnumber(L, static_cast<double>(v.y));
    lua_pushnumber(L, static_cast<double>(v.z));
    return 3;
}

/// uz.scene.set_fog_volumetric_emissive(entity, r, g, b)
///
/// Sets the self-emissive color of the fog medium (linear RGB). Default: (0,0,0).
static int SceneSetFogVolumetricEmissive(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_emissive: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_emissive");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_emissive: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    auto& fog = world->GetComponent<HeightFogComponent>(entity);
    fog.volumetricFogEmissive.x = static_cast<float>(luaL_checknumber(L, 2));
    fog.volumetricFogEmissive.y = static_cast<float>(luaL_checknumber(L, 3));
    fog.volumetricFogEmissive.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.get_fog_volumetric_emissive(entity) -> r, g, b | nil
static int SceneGetFogVolumetricEmissive(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_emissive");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& v = world->GetComponent<HeightFogComponent>(entity).volumetricFogEmissive;
    lua_pushnumber(L, static_cast<double>(v.x));
    lua_pushnumber(L, static_cast<double>(v.y));
    lua_pushnumber(L, static_cast<double>(v.z));
    return 3;
}

/// uz.scene.set_fog_volumetric_extinction_scale(entity, scale)
///
/// Sets the extinction coefficient multiplier for volumetric fog. Default: 1.0.
static int SceneSetFogVolumetricExtinctionScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_extinction_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_extinction_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_extinction_scale: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).volumetricFogExtinctionScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_volumetric_extinction_scale(entity) -> number | nil
static int SceneGetFogVolumetricExtinctionScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_extinction_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).volumetricFogExtinctionScale));
    return 1;
}

/// uz.scene.set_fog_volumetric_distance(entity, cm)
///
/// Sets the maximum distance for volumetric fog evaluation in cm. Default: 6400.0 cm.
static int SceneSetFogVolumetricDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).volumetricFogDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_volumetric_distance(entity) -> number | nil
static int SceneGetFogVolumetricDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).volumetricFogDistance));
    return 1;
}

/// uz.scene.set_fog_volumetric_start_distance(entity, cm)
///
/// Sets the minimum distance before volumetric fog begins in cm. Default: 0.0 cm.
static int SceneSetFogVolumetricStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_start_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_start_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).volumetricFogStartDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_volumetric_start_distance(entity) -> number | nil
static int SceneGetFogVolumetricStartDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_start_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).volumetricFogStartDistance));
    return 1;
}

/// uz.scene.set_fog_volumetric_near_fade_in_distance(entity, cm)
///
/// Sets the distance over which volumetric fog fades in near the camera in cm. Default: 300.0 cm.
static int SceneSetFogVolumetricNearFadeInDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_near_fade_in_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_near_fade_in_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_near_fade_in_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).volumetricFogNearFadeInDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_volumetric_near_fade_in_distance(entity) -> number | nil
static int SceneGetFogVolumetricNearFadeInDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_near_fade_in_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).volumetricFogNearFadeInDistance));
    return 1;
}

/// uz.scene.set_fog_volumetric_static_lighting_scattering_intensity(entity, intensity)
///
/// Sets the multiplier for static lighting scattering contribution. Default: 1.0.
static int SceneSetFogVolumetricStaticLightingScatteringIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_static_lighting_scattering_intensity: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_volumetric_static_lighting_scattering_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_volumetric_static_lighting_scattering_intensity: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).volumetricFogStaticLightingScatteringIntensity =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_volumetric_static_lighting_scattering_intensity(entity) -> number | nil
static int SceneGetFogVolumetricStaticLightingScatteringIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_volumetric_static_lighting_scattering_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).volumetricFogStaticLightingScatteringIntensity));
    return 1;
}

/// uz.scene.set_fog_override_light_colors_with_fog_inscattering_colors(entity, bool)
///
/// When true, light colors are overridden by fog inscattering colors for volumetric fog. Default: false.
static int SceneSetFogOverrideLightColorsWithFogInscatteringColors(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_override_light_colors_with_fog_inscattering_colors: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_override_light_colors_with_fog_inscattering_colors");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_override_light_colors_with_fog_inscattering_colors: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).overrideLightColorsWithFogInscatteringColors =
        lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.get_fog_override_light_colors_with_fog_inscattering_colors(entity) -> bool | nil
static int SceneGetFogOverrideLightColorsWithFogInscatteringColors(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_override_light_colors_with_fog_inscattering_colors");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<HeightFogComponent>(entity).overrideLightColorsWithFogInscatteringColors ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — fog inscattering texture properties
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_fog_inscattering_color_cubemap_id(entity, id)
///
/// Sets the handle/ID of the cubemap texture for inscattering color lookup. 0 = none.
static int SceneSetFogInscatteringColorCubemapId(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_inscattering_color_cubemap_id: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_inscattering_color_cubemap_id");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_inscattering_color_cubemap_id: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).inscatteringColorCubemapId =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_fog_inscattering_color_cubemap_id(entity) -> int | nil
static int SceneGetFogInscatteringColorCubemapId(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_inscattering_color_cubemap_id");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(
        world->GetComponent<HeightFogComponent>(entity).inscatteringColorCubemapId));
    return 1;
}

/// uz.scene.set_fog_inscattering_color_cubemap_angle(entity, degrees)
///
/// Sets the rotation angle for the inscattering cubemap in degrees. Default: 0.0.
static int SceneSetFogInscatteringColorCubemapAngle(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_inscattering_color_cubemap_angle: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_inscattering_color_cubemap_angle");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_inscattering_color_cubemap_angle: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).inscatteringColorCubemapAngle =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_inscattering_color_cubemap_angle(entity) -> number | nil
static int SceneGetFogInscatteringColorCubemapAngle(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_inscattering_color_cubemap_angle");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).inscatteringColorCubemapAngle));
    return 1;
}

/// uz.scene.set_fog_inscattering_texture_tint(entity, r, g, b)
///
/// Sets the tint color applied to the inscattering cubemap. Default: (1,1,1).
static int SceneSetFogInscatteringTextureTint(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_inscattering_texture_tint: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_inscattering_texture_tint");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_inscattering_texture_tint: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    auto& fog = world->GetComponent<HeightFogComponent>(entity);
    fog.inscatteringTextureTint.x = static_cast<float>(luaL_checknumber(L, 2));
    fog.inscatteringTextureTint.y = static_cast<float>(luaL_checknumber(L, 3));
    fog.inscatteringTextureTint.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.get_fog_inscattering_texture_tint(entity) -> r, g, b | nil
static int SceneGetFogInscatteringTextureTint(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_inscattering_texture_tint");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& v = world->GetComponent<HeightFogComponent>(entity).inscatteringTextureTint;
    lua_pushnumber(L, static_cast<double>(v.x));
    lua_pushnumber(L, static_cast<double>(v.y));
    lua_pushnumber(L, static_cast<double>(v.z));
    return 3;
}

/// uz.scene.set_fog_fully_directional_inscattering_color_distance(entity, cm)
///
/// Sets the distance at which inscattering becomes fully directional in cm. Default: 100000.0 cm.
static int SceneSetFogFullyDirectionalInscatteringColorDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_fully_directional_inscattering_color_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_fully_directional_inscattering_color_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_fully_directional_inscattering_color_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).fullyDirectionalInscatteringColorDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_fully_directional_inscattering_color_distance(entity) -> number | nil
static int SceneGetFogFullyDirectionalInscatteringColorDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_fully_directional_inscattering_color_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).fullyDirectionalInscatteringColorDistance));
    return 1;
}

/// uz.scene.set_fog_non_directional_inscattering_color_distance(entity, cm)
///
/// Sets the distance at which non-directional inscattering starts blending in cm. Default: 50000.0 cm.
static int SceneSetFogNonDirectionalInscatteringColorDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_non_directional_inscattering_color_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_non_directional_inscattering_color_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_non_directional_inscattering_color_distance: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    world->GetComponent<HeightFogComponent>(entity).nonDirectionalInscatteringColorDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_fog_non_directional_inscattering_color_distance(entity) -> number | nil
static int SceneGetFogNonDirectionalInscatteringColorDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_non_directional_inscattering_color_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<HeightFogComponent>(entity).nonDirectionalInscatteringColorDistance));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — fog atmosphere contribution properties
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_fog_sky_atmosphere_ambient_contribution_color_scale(entity, r, g, b)
///
/// Sets the per-channel scale for sky atmosphere ambient contribution to fog. Default: (1,1,1).
static int SceneSetFogSkyAtmosphereAmbientContributionColorScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_fog_sky_atmosphere_ambient_contribution_color_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_fog_sky_atmosphere_ambient_contribution_color_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<HeightFogComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_fog_sky_atmosphere_ambient_contribution_color_scale: entity {} has no HeightFogComponent", entity.id);
        return 0;
    }
    auto& fog = world->GetComponent<HeightFogComponent>(entity);
    fog.skyAtmosphereAmbientContributionColorScale.x = static_cast<float>(luaL_checknumber(L, 2));
    fog.skyAtmosphereAmbientContributionColorScale.y = static_cast<float>(luaL_checknumber(L, 3));
    fog.skyAtmosphereAmbientContributionColorScale.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.get_fog_sky_atmosphere_ambient_contribution_color_scale(entity) -> r, g, b | nil
static int SceneGetFogSkyAtmosphereAmbientContributionColorScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_fog_sky_atmosphere_ambient_contribution_color_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<HeightFogComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& v = world->GetComponent<HeightFogComponent>(entity).skyAtmosphereAmbientContributionColorScale;
    lua_pushnumber(L, static_cast<double>(v.x));
    lua_pushnumber(L, static_cast<double>(v.y));
    lua_pushnumber(L, static_cast<double>(v.z));
    return 3;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterFogAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterFogAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterFogAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    lua_pushcfunction(L, SceneCreateFog);                              lua_setfield(L, -2, "create_fog");
    lua_pushcfunction(L, SceneSetFogDensity);                          lua_setfield(L, -2, "set_fog_density");
    lua_pushcfunction(L, SceneSetFogColor);                            lua_setfield(L, -2, "set_fog_color");
    lua_pushcfunction(L, SceneSetFogHeightFalloff);                    lua_setfield(L, -2, "set_fog_height_falloff");
    lua_pushcfunction(L, SceneSetFogBaseHeight);                       lua_setfield(L, -2, "set_fog_base_height");
    lua_pushcfunction(L, SceneSetFogMaxOpacity);                       lua_setfield(L, -2, "set_fog_max_opacity");
    lua_pushcfunction(L, SceneSetFogEnabled);                          lua_setfield(L, -2, "set_fog_enabled");
    lua_pushcfunction(L, SceneSetFogStartDistance);                    lua_setfield(L, -2, "set_fog_start_distance");
    lua_pushcfunction(L, SceneSetFogCutoffDistance);                   lua_setfield(L, -2, "set_fog_cutoff_distance");
    lua_pushcfunction(L, SceneSetFogSecondDensity);                    lua_setfield(L, -2, "set_fog_second_density");
    lua_pushcfunction(L, SceneSetFogSecondHeightFalloff);              lua_setfield(L, -2, "set_fog_second_height_falloff");
    lua_pushcfunction(L, SceneSetFogSecondBaseHeight);                 lua_setfield(L, -2, "set_fog_second_base_height");
    lua_pushcfunction(L, SceneSetFogDirectionalInscatteringColor);     lua_setfield(L, -2, "set_fog_directional_inscattering_color");
    lua_pushcfunction(L, SceneSetFogDirectionalInscatteringExponent);  lua_setfield(L, -2, "set_fog_directional_inscattering_exponent");
    lua_pushcfunction(L, SceneSetFogDirectionalInscatteringStartDistance); lua_setfield(L, -2, "set_fog_directional_inscattering_start_distance");
    lua_pushcfunction(L, SceneSetFogVolumetric);                       lua_setfield(L, -2, "set_fog_volumetric");
    lua_pushcfunction(L, SceneGetFogDensity);                          lua_setfield(L, -2, "get_fog_density");
    lua_pushcfunction(L, SceneGetFogColor);                            lua_setfield(L, -2, "get_fog_color");
    lua_pushcfunction(L, SceneGetFogHeightFalloff);                    lua_setfield(L, -2, "get_fog_height_falloff");
    lua_pushcfunction(L, SceneGetFogBaseHeight);                       lua_setfield(L, -2, "get_fog_base_height");
    lua_pushcfunction(L, SceneGetFogMaxOpacity);                       lua_setfield(L, -2, "get_fog_max_opacity");
    lua_pushcfunction(L, SceneIsFogEnabled);                           lua_setfield(L, -2, "is_fog_enabled");
    lua_pushcfunction(L, SceneGetFogStartDistance);                    lua_setfield(L, -2, "get_fog_start_distance");
    lua_pushcfunction(L, SceneGetFogCutoffDistance);                   lua_setfield(L, -2, "get_fog_cutoff_distance");
    lua_pushcfunction(L, SceneGetFogSecondDensity);                    lua_setfield(L, -2, "get_fog_second_density");
    lua_pushcfunction(L, SceneGetFogSecondHeightFalloff);              lua_setfield(L, -2, "get_fog_second_height_falloff");
    lua_pushcfunction(L, SceneGetFogSecondBaseHeight);                 lua_setfield(L, -2, "get_fog_second_base_height");
    lua_pushcfunction(L, SceneGetFogDirectionalInscatteringColor);     lua_setfield(L, -2, "get_fog_directional_inscattering_color");
    lua_pushcfunction(L, SceneGetFogDirectionalInscatteringExponent);  lua_setfield(L, -2, "get_fog_directional_inscattering_exponent");
    lua_pushcfunction(L, SceneGetFogDirectionalInscatteringStartDistance); lua_setfield(L, -2, "get_fog_directional_inscattering_start_distance");
    lua_pushcfunction(L, SceneIsFogVolumetric);                        lua_setfield(L, -2, "is_fog_volumetric");

    // Volumetric fog extended properties
    lua_pushcfunction(L, SceneSetFogVolumetricScatteringDistribution);         lua_setfield(L, -2, "set_fog_volumetric_scattering_distribution");
    lua_pushcfunction(L, SceneGetFogVolumetricScatteringDistribution);         lua_setfield(L, -2, "get_fog_volumetric_scattering_distribution");
    lua_pushcfunction(L, SceneSetFogVolumetricAlbedo);                         lua_setfield(L, -2, "set_fog_volumetric_albedo");
    lua_pushcfunction(L, SceneGetFogVolumetricAlbedo);                         lua_setfield(L, -2, "get_fog_volumetric_albedo");
    lua_pushcfunction(L, SceneSetFogVolumetricEmissive);                       lua_setfield(L, -2, "set_fog_volumetric_emissive");
    lua_pushcfunction(L, SceneGetFogVolumetricEmissive);                       lua_setfield(L, -2, "get_fog_volumetric_emissive");
    lua_pushcfunction(L, SceneSetFogVolumetricExtinctionScale);                lua_setfield(L, -2, "set_fog_volumetric_extinction_scale");
    lua_pushcfunction(L, SceneGetFogVolumetricExtinctionScale);                lua_setfield(L, -2, "get_fog_volumetric_extinction_scale");
    lua_pushcfunction(L, SceneSetFogVolumetricDistance);                       lua_setfield(L, -2, "set_fog_volumetric_distance");
    lua_pushcfunction(L, SceneGetFogVolumetricDistance);                       lua_setfield(L, -2, "get_fog_volumetric_distance");
    lua_pushcfunction(L, SceneSetFogVolumetricStartDistance);                  lua_setfield(L, -2, "set_fog_volumetric_start_distance");
    lua_pushcfunction(L, SceneGetFogVolumetricStartDistance);                  lua_setfield(L, -2, "get_fog_volumetric_start_distance");
    lua_pushcfunction(L, SceneSetFogVolumetricNearFadeInDistance);             lua_setfield(L, -2, "set_fog_volumetric_near_fade_in_distance");
    lua_pushcfunction(L, SceneGetFogVolumetricNearFadeInDistance);             lua_setfield(L, -2, "get_fog_volumetric_near_fade_in_distance");
    lua_pushcfunction(L, SceneSetFogVolumetricStaticLightingScatteringIntensity);  lua_setfield(L, -2, "set_fog_volumetric_static_lighting_scattering_intensity");
    lua_pushcfunction(L, SceneGetFogVolumetricStaticLightingScatteringIntensity);  lua_setfield(L, -2, "get_fog_volumetric_static_lighting_scattering_intensity");
    lua_pushcfunction(L, SceneSetFogOverrideLightColorsWithFogInscatteringColors); lua_setfield(L, -2, "set_fog_override_light_colors_with_fog_inscattering_colors");
    lua_pushcfunction(L, SceneGetFogOverrideLightColorsWithFogInscatteringColors); lua_setfield(L, -2, "get_fog_override_light_colors_with_fog_inscattering_colors");

    // Inscattering texture properties
    lua_pushcfunction(L, SceneSetFogInscatteringColorCubemapId);               lua_setfield(L, -2, "set_fog_inscattering_color_cubemap_id");
    lua_pushcfunction(L, SceneGetFogInscatteringColorCubemapId);               lua_setfield(L, -2, "get_fog_inscattering_color_cubemap_id");
    lua_pushcfunction(L, SceneSetFogInscatteringColorCubemapAngle);            lua_setfield(L, -2, "set_fog_inscattering_color_cubemap_angle");
    lua_pushcfunction(L, SceneGetFogInscatteringColorCubemapAngle);            lua_setfield(L, -2, "get_fog_inscattering_color_cubemap_angle");
    lua_pushcfunction(L, SceneSetFogInscatteringTextureTint);                  lua_setfield(L, -2, "set_fog_inscattering_texture_tint");
    lua_pushcfunction(L, SceneGetFogInscatteringTextureTint);                  lua_setfield(L, -2, "get_fog_inscattering_texture_tint");
    lua_pushcfunction(L, SceneSetFogFullyDirectionalInscatteringColorDistance); lua_setfield(L, -2, "set_fog_fully_directional_inscattering_color_distance");
    lua_pushcfunction(L, SceneGetFogFullyDirectionalInscatteringColorDistance); lua_setfield(L, -2, "get_fog_fully_directional_inscattering_color_distance");
    lua_pushcfunction(L, SceneSetFogNonDirectionalInscatteringColorDistance);  lua_setfield(L, -2, "set_fog_non_directional_inscattering_color_distance");
    lua_pushcfunction(L, SceneGetFogNonDirectionalInscatteringColorDistance);  lua_setfield(L, -2, "get_fog_non_directional_inscattering_color_distance");

    // Atmosphere contribution
    lua_pushcfunction(L, SceneSetFogSkyAtmosphereAmbientContributionColorScale); lua_setfield(L, -2, "set_fog_sky_atmosphere_ambient_contribution_color_scale");
    lua_pushcfunction(L, SceneGetFogSkyAtmosphereAmbientContributionColorScale); lua_setfield(L, -2, "get_fog_sky_atmosphere_ambient_contribution_color_scale");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene fog API registered");
}

} // namespace uz
