/// Lua bindings for extended per-light-type properties (directional 15.5, point 15.6, spot 15.7).

#include "lua_light_extended_api.h"

#include "log.h"
#include "world.h"
#include "light_component.h"

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
// Lua C functions — directional light extended properties (15.5)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_light_atmosphere_sun_index(entity, index)
///
/// Sets which atmosphere sun slot this directional light occupies.
/// 0 = primary sun, 1 = secondary sun/moon.
static int SceneSetLightAtmosphereSunIndex(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_atmosphere_sun_index: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_atmosphere_sun_index");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_atmosphere_sun_index: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).atmosphereSunLightIndex =
        static_cast<int>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_light_atmosphere_sun_index(entity) -> int | nil
static int SceneGetLightAtmosphereSunIndex(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_atmosphere_sun_index");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, world->GetComponent<DirectionalLightComponent>(entity).atmosphereSunLightIndex);
    return 1;
}

/// uz.scene.set_light_source_angle(entity, degrees)
///
/// Sets the angular diameter of the sun disk in degrees (visual size in sky).
/// Default: 0.5357 deg (Earth's sun).
static int SceneSetLightSourceAngle(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_source_angle: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_source_angle");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_source_angle: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).lightSourceAngle =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_source_angle(entity) -> number | nil
static int SceneGetLightSourceAngle(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_source_angle");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).lightSourceAngle));
    return 1;
}

/// uz.scene.set_light_source_soft_angle(entity, degrees)
///
/// Sets the soft edge angle of the sun disk in degrees.
/// Default: 0.0.
static int SceneSetLightSourceSoftAngle(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_source_soft_angle: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_source_soft_angle");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_source_soft_angle: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).lightSourceSoftAngle =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_source_soft_angle(entity) -> number | nil
static int SceneGetLightSourceSoftAngle(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_source_soft_angle");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).lightSourceSoftAngle));
    return 1;
}

/// uz.scene.set_light_shadow_cascades(entity, count)
///
/// Sets the number of CSM cascades (0 = no shadows, 1-4 = cascade count).
/// Default: 4.
static int SceneSetLightShadowCascades(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_shadow_cascades: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_shadow_cascades");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_shadow_cascades: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).shadowCascades =
        static_cast<int>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_light_shadow_cascades(entity) -> int | nil
static int SceneGetLightShadowCascades(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_shadow_cascades");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, world->GetComponent<DirectionalLightComponent>(entity).shadowCascades);
    return 1;
}

/// uz.scene.set_light_cascade_distribution_exponent(entity, exponent)
///
/// Sets the cascade distribution exponent. 1.0 = linear, >1 = more cascades near camera.
/// Default: 3.0.
static int SceneSetLightCascadeDistributionExponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_cascade_distribution_exponent: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_cascade_distribution_exponent");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_cascade_distribution_exponent: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).cascadeDistributionExponent =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_cascade_distribution_exponent(entity) -> number | nil
static int SceneGetLightCascadeDistributionExponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_cascade_distribution_exponent");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).cascadeDistributionExponent));
    return 1;
}

/// uz.scene.set_light_cascade_transition_fraction(entity, fraction)
///
/// Sets the cascade transition fade fraction [0, 1]. Smooth blend between cascades.
/// Default: 0.1.
static int SceneSetLightCascadeTransitionFraction(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_cascade_transition_fraction: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_cascade_transition_fraction");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_cascade_transition_fraction: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).cascadeTransitionFraction =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_cascade_transition_fraction(entity) -> number | nil
static int SceneGetLightCascadeTransitionFraction(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_cascade_transition_fraction");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).cascadeTransitionFraction));
    return 1;
}

/// uz.scene.set_light_shadow_distance_fadeout_fraction(entity, fraction)
///
/// Sets the shadow distance fadeout fraction [0, 1] at the far CSM extent.
/// Default: 0.1.
static int SceneSetLightShadowDistanceFadeoutFraction(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_shadow_distance_fadeout_fraction: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_shadow_distance_fadeout_fraction");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_shadow_distance_fadeout_fraction: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).shadowDistanceFadeoutFraction =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_shadow_distance_fadeout_fraction(entity) -> number | nil
static int SceneGetLightShadowDistanceFadeoutFraction(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_shadow_distance_fadeout_fraction");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).shadowDistanceFadeoutFraction));
    return 1;
}

/// uz.scene.set_light_dynamic_shadow_distance(entity, cm)
///
/// Sets the maximum CSM shadow distance in centimeters.
/// Default: 40000 cm (400 m).
static int SceneSetLightDynamicShadowDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_dynamic_shadow_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_dynamic_shadow_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_dynamic_shadow_distance: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).dynamicShadowDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_dynamic_shadow_distance(entity) -> number | nil
static int SceneGetLightDynamicShadowDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_dynamic_shadow_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).dynamicShadowDistance));
    return 1;
}

/// uz.scene.set_light_contact_shadow_length(entity, length)
///
/// Sets the screen-space contact shadow ray length (0 = disabled).
/// Default: 0.
static int SceneSetLightContactShadowLength(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_contact_shadow_length: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_contact_shadow_length");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_contact_shadow_length: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).contactShadowLength =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_contact_shadow_length(entity) -> number | nil
static int SceneGetLightContactShadowLength(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_contact_shadow_length");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).contactShadowLength));
    return 1;
}

/// uz.scene.set_light_far_shadow_cascade_count(entity, count)
///
/// Sets the number of far shadow cascades beyond the main CSM distance.
/// 0 = disabled. Default: 0.
static int SceneSetLightFarShadowCascadeCount(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_far_shadow_cascade_count: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_far_shadow_cascade_count");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_far_shadow_cascade_count: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).farShadowCascadeCount =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_light_far_shadow_cascade_count(entity) -> int | nil
static int SceneGetLightFarShadowCascadeCount(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_far_shadow_cascade_count");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(
        world->GetComponent<DirectionalLightComponent>(entity).farShadowCascadeCount));
    return 1;
}

/// uz.scene.set_light_far_shadow_distance(entity, cm)
///
/// Sets the maximum far shadow distance in centimeters.
/// Only used when farShadowCascadeCount > 0. Default: 300000 cm (3 km).
static int SceneSetLightFarShadowDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_far_shadow_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_far_shadow_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_far_shadow_distance: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<DirectionalLightComponent>(entity).farShadowDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_far_shadow_distance(entity) -> number | nil
static int SceneGetLightFarShadowDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_far_shadow_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<DirectionalLightComponent>(entity).farShadowDistance));
    return 1;
}

/// uz.scene.set_light_atmosphere_sun_disk_color_scale(entity, r, g, b)
///
/// Sets the tint color applied to the sun disk in the sky atmosphere.
/// Default: (1, 1, 1) = no tint.
static int SceneSetLightAtmosphereSunDiskColorScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_atmosphere_sun_disk_color_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_atmosphere_sun_disk_color_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_atmosphere_sun_disk_color_scale: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }
    auto& light = world->GetComponent<DirectionalLightComponent>(entity);
    light.atmosphereSunDiskColorScale.x = static_cast<float>(luaL_checknumber(L, 2));
    light.atmosphereSunDiskColorScale.y = static_cast<float>(luaL_checknumber(L, 3));
    light.atmosphereSunDiskColorScale.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.get_light_atmosphere_sun_disk_color_scale(entity) -> r, g, b | nil
static int SceneGetLightAtmosphereSunDiskColorScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_atmosphere_sun_disk_color_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DirectionalLightComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& scale = world->GetComponent<DirectionalLightComponent>(entity).atmosphereSunDiskColorScale;
    lua_pushnumber(L, static_cast<double>(scale.x));
    lua_pushnumber(L, static_cast<double>(scale.y));
    lua_pushnumber(L, static_cast<double>(scale.z));
    return 3;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — point light extended properties (15.6)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.get_light_use_inverse_squared_falloff(entity) -> bool | nil
///
/// Returns whether the point/spot light uses physically-correct inverse-square falloff.
/// Returns nil if the entity has no PointLightComponent or SpotLightComponent.
static int SceneGetLightUseInverseSquaredFalloff(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_use_inverse_squared_falloff");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        lua_pushboolean(L, world->GetComponent<SpotLightComponent>(entity).useInverseSquaredFalloff ? 1 : 0);
        return 1;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        lua_pushboolean(L, world->GetComponent<PointLightComponent>(entity).useInverseSquaredFalloff ? 1 : 0);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

/// uz.scene.get_light_falloff_exponent(entity) -> number | nil
///
/// Returns the custom falloff exponent used when inverse squared is disabled.
/// Returns nil if the entity has no PointLightComponent or SpotLightComponent.
/// NOTE: Use set_light_falloff(entity, use_inverse_squared, exponent) to set both.
static int SceneGetLightFalloffExponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_falloff_exponent");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<SpotLightComponent>(entity).lightFalloffExponent));
        return 1;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<PointLightComponent>(entity).lightFalloffExponent));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

/// uz.scene.set_light_soft_source_radius(entity, radius)
///
/// Sets the soft edge sphere radius for smoother shadow penumbra on a point/spot light.
/// Default: 0.
static int SceneSetLightSoftSourceRadius(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_soft_source_radius: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_soft_source_radius");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    const float radius = static_cast<float>(luaL_checknumber(L, 2));
    if (world->HasComponent<SpotLightComponent>(entity)) {
        world->GetComponent<SpotLightComponent>(entity).softSourceRadius = radius;
        return 0;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        world->GetComponent<PointLightComponent>(entity).softSourceRadius = radius;
        return 0;
    }
    UZ_LOG(Scripting, Warning,
        "uz.scene.set_light_soft_source_radius: entity {} has no point or spot light component", entity.id);
    return 0;
}

/// uz.scene.get_light_soft_source_radius(entity) -> number | nil
static int SceneGetLightSoftSourceRadius(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_soft_source_radius");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<SpotLightComponent>(entity).softSourceRadius));
        return 1;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<PointLightComponent>(entity).softSourceRadius));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

/// uz.scene.set_light_source_length(entity, length)
///
/// Sets the tube/capsule length for elongated area light approximation.
/// 0 = sphere only. Default: 0.
static int SceneSetLightSourceLength(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_source_length: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_source_length");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    const float length = static_cast<float>(luaL_checknumber(L, 2));
    if (world->HasComponent<SpotLightComponent>(entity)) {
        world->GetComponent<SpotLightComponent>(entity).sourceLength = length;
        return 0;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        world->GetComponent<PointLightComponent>(entity).sourceLength = length;
        return 0;
    }
    UZ_LOG(Scripting, Warning,
        "uz.scene.set_light_source_length: entity {} has no point or spot light component", entity.id);
    return 0;
}

/// uz.scene.get_light_source_length(entity) -> number | nil
static int SceneGetLightSourceLength(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_source_length");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<SpotLightComponent>(entity).sourceLength));
        return 1;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<PointLightComponent>(entity).sourceLength));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

/// uz.scene.set_light_point_shadow_bias(entity, bias)
///
/// Sets the per-light shadow depth bias for a point light's cubemap shadow maps.
/// Default: 0.5.
static int SceneSetLightPointShadowBias(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_point_shadow_bias: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_point_shadow_bias");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<PointLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_point_shadow_bias: entity {} has no PointLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<PointLightComponent>(entity).pointShadowBias =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_point_shadow_bias(entity) -> number | nil
static int SceneGetLightPointShadowBias(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_point_shadow_bias");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<PointLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<PointLightComponent>(entity).pointShadowBias));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — spot light extended properties (15.7)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_light_spot_shadow_bias(entity, bias)
///
/// Sets the per-light shadow depth bias for a spot light's shadow map.
/// Default: 0.5.
static int SceneSetLightSpotShadowBias(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_spot_shadow_bias: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_light_spot_shadow_bias");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SpotLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_spot_shadow_bias: entity {} has no SpotLightComponent", entity.id);
        return 0;
    }
    world->GetComponent<SpotLightComponent>(entity).spotShadowBias =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_spot_shadow_bias(entity) -> number | nil
static int SceneGetLightSpotShadowBias(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_spot_shadow_bias");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<SpotLightComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(
        world->GetComponent<SpotLightComponent>(entity).spotShadowBias));
    return 1;
}

/// uz.scene.get_light_source_radius(entity) -> number | nil
///
/// Returns the sphere radius used for area light approximation on a point/spot light.
/// Returns nil if the entity has no PointLightComponent or SpotLightComponent.
static int SceneGetLightSourceRadius(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_light_source_radius");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<SpotLightComponent>(entity).sourceRadius));
        return 1;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<PointLightComponent>(entity).sourceRadius));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterLightExtendedAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterLightExtendedAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterLightExtendedAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    // Directional light extended (15.5)
    lua_pushcfunction(L, SceneSetLightAtmosphereSunIndex);
    lua_setfield(L, -2, "set_light_atmosphere_sun_index");

    lua_pushcfunction(L, SceneGetLightAtmosphereSunIndex);
    lua_setfield(L, -2, "get_light_atmosphere_sun_index");

    lua_pushcfunction(L, SceneSetLightSourceAngle);
    lua_setfield(L, -2, "set_light_source_angle");

    lua_pushcfunction(L, SceneGetLightSourceAngle);
    lua_setfield(L, -2, "get_light_source_angle");

    lua_pushcfunction(L, SceneSetLightSourceSoftAngle);
    lua_setfield(L, -2, "set_light_source_soft_angle");

    lua_pushcfunction(L, SceneGetLightSourceSoftAngle);
    lua_setfield(L, -2, "get_light_source_soft_angle");

    lua_pushcfunction(L, SceneSetLightShadowCascades);
    lua_setfield(L, -2, "set_light_shadow_cascades");

    lua_pushcfunction(L, SceneGetLightShadowCascades);
    lua_setfield(L, -2, "get_light_shadow_cascades");

    lua_pushcfunction(L, SceneSetLightCascadeDistributionExponent);
    lua_setfield(L, -2, "set_light_cascade_distribution_exponent");

    lua_pushcfunction(L, SceneGetLightCascadeDistributionExponent);
    lua_setfield(L, -2, "get_light_cascade_distribution_exponent");

    lua_pushcfunction(L, SceneSetLightCascadeTransitionFraction);
    lua_setfield(L, -2, "set_light_cascade_transition_fraction");

    lua_pushcfunction(L, SceneGetLightCascadeTransitionFraction);
    lua_setfield(L, -2, "get_light_cascade_transition_fraction");

    lua_pushcfunction(L, SceneSetLightShadowDistanceFadeoutFraction);
    lua_setfield(L, -2, "set_light_shadow_distance_fadeout_fraction");

    lua_pushcfunction(L, SceneGetLightShadowDistanceFadeoutFraction);
    lua_setfield(L, -2, "get_light_shadow_distance_fadeout_fraction");

    lua_pushcfunction(L, SceneSetLightDynamicShadowDistance);
    lua_setfield(L, -2, "set_light_dynamic_shadow_distance");

    lua_pushcfunction(L, SceneGetLightDynamicShadowDistance);
    lua_setfield(L, -2, "get_light_dynamic_shadow_distance");

    lua_pushcfunction(L, SceneSetLightContactShadowLength);
    lua_setfield(L, -2, "set_light_contact_shadow_length");

    lua_pushcfunction(L, SceneGetLightContactShadowLength);
    lua_setfield(L, -2, "get_light_contact_shadow_length");

    lua_pushcfunction(L, SceneSetLightFarShadowCascadeCount);
    lua_setfield(L, -2, "set_light_far_shadow_cascade_count");

    lua_pushcfunction(L, SceneGetLightFarShadowCascadeCount);
    lua_setfield(L, -2, "get_light_far_shadow_cascade_count");

    lua_pushcfunction(L, SceneSetLightFarShadowDistance);
    lua_setfield(L, -2, "set_light_far_shadow_distance");

    lua_pushcfunction(L, SceneGetLightFarShadowDistance);
    lua_setfield(L, -2, "get_light_far_shadow_distance");

    lua_pushcfunction(L, SceneSetLightAtmosphereSunDiskColorScale);
    lua_setfield(L, -2, "set_light_atmosphere_sun_disk_color_scale");

    lua_pushcfunction(L, SceneGetLightAtmosphereSunDiskColorScale);
    lua_setfield(L, -2, "get_light_atmosphere_sun_disk_color_scale");

    // Point light extended (15.6)
    lua_pushcfunction(L, SceneGetLightUseInverseSquaredFalloff);
    lua_setfield(L, -2, "get_light_use_inverse_squared_falloff");

    lua_pushcfunction(L, SceneGetLightFalloffExponent);
    lua_setfield(L, -2, "get_light_falloff_exponent");

    lua_pushcfunction(L, SceneSetLightSoftSourceRadius);
    lua_setfield(L, -2, "set_light_soft_source_radius");

    lua_pushcfunction(L, SceneGetLightSoftSourceRadius);
    lua_setfield(L, -2, "get_light_soft_source_radius");

    lua_pushcfunction(L, SceneSetLightSourceLength);
    lua_setfield(L, -2, "set_light_source_length");

    lua_pushcfunction(L, SceneGetLightSourceLength);
    lua_setfield(L, -2, "get_light_source_length");

    lua_pushcfunction(L, SceneSetLightPointShadowBias);
    lua_setfield(L, -2, "set_light_point_shadow_bias");

    lua_pushcfunction(L, SceneGetLightPointShadowBias);
    lua_setfield(L, -2, "get_light_point_shadow_bias");

    lua_pushcfunction(L, SceneGetLightSourceRadius);
    lua_setfield(L, -2, "get_light_source_radius");

    // Spot light extended (15.7)
    lua_pushcfunction(L, SceneSetLightSpotShadowBias);
    lua_setfield(L, -2, "set_light_spot_shadow_bias");

    lua_pushcfunction(L, SceneGetLightSpotShadowBias);
    lua_setfield(L, -2, "get_light_spot_shadow_bias");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene light extended API registered");
}

} // namespace uz
