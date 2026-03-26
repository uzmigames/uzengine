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

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene fog API registered");
}

} // namespace uz
