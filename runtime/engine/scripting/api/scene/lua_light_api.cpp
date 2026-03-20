#include "lua_light_api.h"

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
// Internal helper — get any LightComponent from an entity
// ─────────────────────────────────────────────────────────────────────────────

/// Tries to get a LightComponent (any subclass) from the entity.
///
/// Checks DirectionalLightComponent, PointLightComponent, SpotLightComponent
/// in order. Returns the first found, or nullptr.
[[nodiscard]] static LightComponent* GetAnyLight(World* world, Entity entity) {
    if (world->HasComponent<DirectionalLightComponent>(entity)) {
        return &world->GetComponent<DirectionalLightComponent>(entity);
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        return &world->GetComponent<PointLightComponent>(entity);
    }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        return &world->GetComponent<SpotLightComponent>(entity);
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — light creation
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.add_directional_light(entity, r, g, b, intensity)
///
/// Adds a DirectionalLightComponent to the entity with the given color and intensity.
/// Direction is derived from the entity's SceneComponent rotation.
static int SceneAddDirectionalLight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.add_directional_light: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "add_directional_light");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    const float r = static_cast<float>(luaL_checknumber(L, 2));
    const float g = static_cast<float>(luaL_checknumber(L, 3));
    const float b = static_cast<float>(luaL_checknumber(L, 4));
    const float intensity = static_cast<float>(luaL_checknumber(L, 5));

    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        world->AddComponent<DirectionalLightComponent>(entity);
    }
    auto& light = world->GetComponent<DirectionalLightComponent>(entity);
    light.color     = Vec3{r, g, b};
    light.intensity = intensity;
    return 0;
}

/// uz.scene.add_point_light(entity, r, g, b, intensity, range)
///
/// Adds a PointLightComponent to the entity.
static int SceneAddPointLight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.add_point_light: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "add_point_light");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    const float r         = static_cast<float>(luaL_checknumber(L, 2));
    const float g         = static_cast<float>(luaL_checknumber(L, 3));
    const float b_val     = static_cast<float>(luaL_checknumber(L, 4));
    const float intensity = static_cast<float>(luaL_checknumber(L, 5));
    const float range     = static_cast<float>(luaL_checknumber(L, 6));

    if (!world->HasComponent<PointLightComponent>(entity)) {
        world->AddComponent<PointLightComponent>(entity);
    }
    auto& light = world->GetComponent<PointLightComponent>(entity);
    light.color     = Vec3{r, g, b_val};
    light.intensity = intensity;
    light.range     = range;
    return 0;
}

/// uz.scene.add_spot_light(entity, r, g, b, intensity, range, inner_deg, outer_deg)
///
/// Adds a SpotLightComponent to the entity. Cone angles are in degrees.
static int SceneAddSpotLight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.add_spot_light: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "add_spot_light");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    const float r         = static_cast<float>(luaL_checknumber(L, 2));
    const float g         = static_cast<float>(luaL_checknumber(L, 3));
    const float b_val     = static_cast<float>(luaL_checknumber(L, 4));
    const float intensity = static_cast<float>(luaL_checknumber(L, 5));
    const float range     = static_cast<float>(luaL_checknumber(L, 6));
    const float innerDeg  = static_cast<float>(luaL_checknumber(L, 7));
    const float outerDeg  = static_cast<float>(luaL_checknumber(L, 8));

    constexpr float kDegToRad = 3.14159265358979323846f / 180.f;

    if (!world->HasComponent<SpotLightComponent>(entity)) {
        world->AddComponent<SpotLightComponent>(entity);
    }
    auto& light = world->GetComponent<SpotLightComponent>(entity);
    light.color      = Vec3{r, g, b_val};
    light.intensity  = intensity;
    light.range      = range;
    light.innerAngleDeg = innerDeg;
    light.outerAngleDeg = outerDeg;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — light property access
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_light_color(entity, r, g, b)
static int SceneSetLightColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_color: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_color");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    LightComponent* light = GetAnyLight(world, entity);
    if (!light) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_color: entity {} has no light component", entity.id);
        return 0;
    }

    light->color.x = static_cast<float>(luaL_checknumber(L, 2));
    light->color.y = static_cast<float>(luaL_checknumber(L, 3));
    light->color.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_light_intensity(entity, intensity)
static int SceneSetLightIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_intensity: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    LightComponent* light = GetAnyLight(world, entity);
    if (!light) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_light_intensity: entity {} has no light component", entity.id);
        return 0;
    }

    light->intensity = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_light_color(entity) -> r, g, b | nil
static int SceneGetLightColor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }

    const Entity entity = CheckEntity(L, 1, "get_light_color");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }

    const LightComponent* light = GetAnyLight(world, entity);
    if (!light) { lua_pushnil(L); return 1; }

    lua_pushnumber(L, static_cast<double>(light->color.x));
    lua_pushnumber(L, static_cast<double>(light->color.y));
    lua_pushnumber(L, static_cast<double>(light->color.z));
    return 3;
}

/// uz.scene.get_light_intensity(entity) -> number | nil
static int SceneGetLightIntensity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }

    const Entity entity = CheckEntity(L, 1, "get_light_intensity");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }

    const LightComponent* light = GetAnyLight(world, entity);
    if (!light) { lua_pushnil(L); return 1; }

    lua_pushnumber(L, static_cast<double>(light->intensity));
    return 1;
}

/// uz.scene.set_light_range(entity, range) — point/spot lights only
static int SceneSetLightRange(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_range: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_range");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    const float range = static_cast<float>(luaL_checknumber(L, 2));

    if (world->HasComponent<PointLightComponent>(entity)) {
        world->GetComponent<PointLightComponent>(entity).range = range;
    } else if (world->HasComponent<SpotLightComponent>(entity)) {
        world->GetComponent<SpotLightComponent>(entity).range = range;
    } else {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_light_range: entity {} has no point or spot light", entity.id);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — extended property access
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.get_light_range(entity) -> number | nil
///
/// Returns the effective attenuation radius of a PointLightComponent or
/// SpotLightComponent. Returns nil for DirectionalLightComponent or entities
/// without any light component, as directional lights have no finite range.
static int SceneGetLightRange(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }

    const Entity entity = CheckEntity(L, 1, "get_light_range");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }

    if (world->HasComponent<PointLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<PointLightComponent>(entity).range));
        return 1;
    }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        lua_pushnumber(L, static_cast<double>(
            world->GetComponent<SpotLightComponent>(entity).range));
        return 1;
    }
    // Directional lights and entities with no light return nil.
    lua_pushnil(L);
    return 1;
}

/// uz.scene.get_spot_angles(entity) -> inner_deg, outer_deg | nil
///
/// Returns the inner and outer cone half-angles of a SpotLightComponent in
/// degrees. Returns nil if the entity has no SpotLightComponent.
static int SceneGetSpotAngles(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }

    const Entity entity = CheckEntity(L, 1, "get_spot_angles");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }

    if (!world->HasComponent<SpotLightComponent>(entity)) {
        lua_pushnil(L);
        return 1;
    }

    constexpr float kRadToDeg = 180.f / 3.14159265358979323846f;
    const auto& spot = world->GetComponent<SpotLightComponent>(entity);
    lua_pushnumber(L, static_cast<double>(spot.innerAngleDeg));
    lua_pushnumber(L, static_cast<double>(spot.outerAngleDeg));
    return 2;
}

/// uz.scene.set_spot_angles(entity, inner_deg, outer_deg)
///
/// Sets the inner and outer cone half-angles (in degrees) of a
/// SpotLightComponent, converting to radians internally.
/// Logs a warning and no-ops if the entity has no SpotLightComponent.
static int SceneSetSpotAngles(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_spot_angles: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_spot_angles");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    if (!world->HasComponent<SpotLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_spot_angles: entity {} has no SpotLightComponent", entity.id);
        return 0;
    }

    constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
    const float innerDeg = static_cast<float>(luaL_checknumber(L, 2));
    const float outerDeg = static_cast<float>(luaL_checknumber(L, 3));

    auto& spot = world->GetComponent<SpotLightComponent>(entity);
    spot.innerAngleDeg = innerDeg;
    spot.outerAngleDeg = outerDeg;
    return 0;
}

/// uz.scene.get_light_direction(entity) -> x, y, z | nil
///
/// Returns the world-space unit direction vector of a DirectionalLightComponent.
/// Returns nil for PointLightComponent, SpotLightComponent, or entities without
/// any light component, as those lights have positional rather than directional
/// emission.
static int SceneGetLightDirection(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }

    const Entity entity = CheckEntity(L, 1, "get_light_direction");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }

    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        lua_pushnil(L);
        return 1;
    }

    const Vec3 dir = world->GetComponent<DirectionalLightComponent>(entity).GetDirection();
    lua_pushnumber(L, static_cast<double>(dir.x));
    lua_pushnumber(L, static_cast<double>(dir.y));
    lua_pushnumber(L, static_cast<double>(dir.z));
    return 3;
}

/// uz.scene.set_cast_shadows(entity, enabled)
///
/// Sets the `castShadows` flag on any PrimitiveComponent (lights, meshes, etc.).
/// Logs a warning and no-ops if the entity has no PrimitiveComponent.
static int SceneSetCastShadows(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_cast_shadows: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_cast_shadows");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    // LightComponent inherits from PrimitiveComponent — check all light types first,
    // then fall through to any other PrimitiveComponent in the future.
    if (world->HasComponent<DirectionalLightComponent>(entity)) {
        world->GetComponent<DirectionalLightComponent>(entity).castShadows =
            lua_toboolean(L, 2) != 0;
        return 0;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        world->GetComponent<PointLightComponent>(entity).castShadows =
            lua_toboolean(L, 2) != 0;
        return 0;
    }
    if (world->HasComponent<SpotLightComponent>(entity)) {
        world->GetComponent<SpotLightComponent>(entity).castShadows =
            lua_toboolean(L, 2) != 0;
        return 0;
    }

    UZ_LOG(Scripting, Warning,
        "uz.scene.set_cast_shadows: entity {} has no PrimitiveComponent", entity.id);
    return 0;
}

/// uz.scene.remove_light(entity)
///
/// Removes whichever LightComponent exists on the entity (directional, point,
/// or spot). No-op if the entity has no light component.
static int SceneRemoveLight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.remove_light: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "remove_light");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    if (world->HasComponent<DirectionalLightComponent>(entity)) {
        world->RemoveComponent<DirectionalLightComponent>(entity);
    } else if (world->HasComponent<PointLightComponent>(entity)) {
        world->RemoveComponent<PointLightComponent>(entity);
    } else if (world->HasComponent<SpotLightComponent>(entity)) {
        world->RemoveComponent<SpotLightComponent>(entity);
    }
    // Silently no-op when no light is present — caller may call unconditionally.
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — temperature (15.1)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_light_temperature(entity, kelvin [, use_temperature])
///
/// Sets the blackbody color temperature on any light component.
///
/// # Parameters
/// - `entity`: Any entity with a DirectionalLightComponent, PointLightComponent,
///   or SpotLightComponent.
/// - `kelvin`: Color temperature in Kelvin (e.g. 3000 = warm, 6500 = daylight).
/// - `use_temperature`: Optional bool (default: true). When false, the temperature
///   value is stored but the light uses its raw `color` instead.
///
/// # Errors
/// Logs a warning and no-ops if the entity has no light component.
static int SceneSetLightTemperature(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_temperature: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_temperature");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    LightComponent* light = GetAnyLight(world, entity);
    if (!light) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_light_temperature: entity {} has no light component", entity.id);
        return 0;
    }

    light->temperature    = static_cast<float>(luaL_checknumber(L, 2));
    // Third argument is optional; default to true when absent.
    light->useTemperature = lua_isnoneornil(L, 3) ? true : (lua_toboolean(L, 3) != 0);
    return 0;
}

/// uz.scene.get_light_temperature(entity) -> kelvin, use_temperature | nil
///
/// Returns the temperature (Kelvin) and use_temperature flag from any light entity.
/// Returns nil if the entity has no light component.
static int SceneGetLightTemperature(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { lua_pushnil(L); return 1; }

    const Entity entity = CheckEntity(L, 1, "get_light_temperature");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }

    const LightComponent* light = GetAnyLight(world, entity);
    if (!light) { lua_pushnil(L); return 1; }

    lua_pushnumber(L, static_cast<double>(light->temperature));
    lua_pushboolean(L, light->useTemperature ? 1 : 0);
    return 2;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — specular/diffuse scale (15.2)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_light_specular_scale(entity, scale)
///
/// Sets the non-physical specular highlight multiplier for any light component.
///
/// # Parameters
/// - `entity`: Any entity with a light component.
/// - `scale`: Float in [0, 1]. 0 = no specular, 1 = normal.
///
/// # Errors
/// Logs a warning and no-ops if the entity has no light component.
static int SceneSetLightSpecularScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_specular_scale: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_specular_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    LightComponent* light = GetAnyLight(world, entity);
    if (!light) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_light_specular_scale: entity {} has no light component", entity.id);
        return 0;
    }

    light->specularScale = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_light_diffuse_scale(entity, scale)
///
/// Sets the non-physical diffuse contribution multiplier for any light component.
///
/// # Parameters
/// - `entity`: Any entity with a light component.
/// - `scale`: Float in [0, 1]. 0 = no diffuse, 1 = normal.
///
/// # Errors
/// Logs a warning and no-ops if the entity has no light component.
static int SceneSetLightDiffuseScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_diffuse_scale: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_diffuse_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    LightComponent* light = GetAnyLight(world, entity);
    if (!light) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_light_diffuse_scale: entity {} has no light component", entity.id);
        return 0;
    }

    light->diffuseScale = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — point light falloff (15.3)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_light_falloff(entity, use_inverse_squared, exponent)
///
/// Controls the distance falloff mode for PointLightComponent and
/// SpotLightComponent.
///
/// # Parameters
/// - `entity`: Entity with a PointLightComponent or SpotLightComponent.
/// - `use_inverse_squared`: Bool. When true, physically-correct inverse-square
///   falloff is used and `exponent` is ignored. When false, `exponent` drives
///   a power-curve falloff.
/// - `exponent`: Float falloff exponent used when `use_inverse_squared` is false.
///   Default: 8.0.
///
/// # Errors
/// Logs a warning and no-ops if the entity has no point or spot light.
static int SceneSetLightFalloff(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_falloff: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_falloff");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    const bool  useInvSq   = lua_toboolean(L, 2) != 0;
    const float exponent   = static_cast<float>(luaL_checknumber(L, 3));

    // SpotLightComponent inherits from PointLightComponent — check spot first
    // so we write through the most-derived type.
    if (world->HasComponent<SpotLightComponent>(entity)) {
        auto& spot = world->GetComponent<SpotLightComponent>(entity);
        spot.useInverseSquaredFalloff = useInvSq;
        spot.lightFalloffExponent     = exponent;
        return 0;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        auto& point = world->GetComponent<PointLightComponent>(entity);
        point.useInverseSquaredFalloff = useInvSq;
        point.lightFalloffExponent     = exponent;
        return 0;
    }

    UZ_LOG(Scripting, Warning,
        "uz.scene.set_light_falloff: entity {} has no point or spot light component", entity.id);
    return 0;
}

/// uz.scene.set_light_source_radius(entity, radius)
///
/// Sets the sphere radius used for area light approximation (representative point
/// technique) on a PointLightComponent or SpotLightComponent.
///
/// # Parameters
/// - `entity`: Entity with a PointLightComponent or SpotLightComponent.
/// - `radius`: Sphere radius in world units. 0 = point light (no area). Default: 0.
///
/// # Errors
/// Logs a warning and no-ops if the entity has no point or spot light.
static int SceneSetLightSourceRadius(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_light_source_radius: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_light_source_radius");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    const float radius = static_cast<float>(luaL_checknumber(L, 2));

    // SpotLightComponent inherits from PointLightComponent — check spot first.
    if (world->HasComponent<SpotLightComponent>(entity)) {
        world->GetComponent<SpotLightComponent>(entity).sourceRadius = radius;
        return 0;
    }
    if (world->HasComponent<PointLightComponent>(entity)) {
        world->GetComponent<PointLightComponent>(entity).sourceRadius = radius;
        return 0;
    }

    UZ_LOG(Scripting, Warning,
        "uz.scene.set_light_source_radius: entity {} has no point or spot light component", entity.id);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — atmosphere sun link (15.4)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_atmosphere_sun_light(entity, enabled)
///
/// Marks a DirectionalLightComponent as the atmospheric sun driver.
///
/// When enabled, the light's direction, color, and intensity are passed to
/// the SkyAtmosphereComponent for physically-based sky rendering.
/// Only DirectionalLightComponent supports this flag; all other light types
/// are silently ignored with a warning.
///
/// # Parameters
/// - `entity`: Entity with a DirectionalLightComponent.
/// - `enabled`: Bool. true = this light drives the sky atmosphere sun.
///
/// # Errors
/// Logs a warning and no-ops if the entity has no DirectionalLightComponent.
static int SceneSetAtmosphereSunLight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_atmosphere_sun_light: World not set"); return 0; }

    const Entity entity = CheckEntity(L, 1, "set_atmosphere_sun_light");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;

    if (!world->HasComponent<DirectionalLightComponent>(entity)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_atmosphere_sun_light: entity {} has no DirectionalLightComponent", entity.id);
        return 0;
    }

    world->GetComponent<DirectionalLightComponent>(entity).atmosphereSunLight =
        lua_toboolean(L, 2) != 0;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterLightAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterLightAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterLightAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    lua_pushcfunction(L, SceneAddDirectionalLight);
    lua_setfield(L, -2, "add_directional_light");

    lua_pushcfunction(L, SceneAddPointLight);
    lua_setfield(L, -2, "add_point_light");

    lua_pushcfunction(L, SceneAddSpotLight);
    lua_setfield(L, -2, "add_spot_light");

    lua_pushcfunction(L, SceneSetLightColor);
    lua_setfield(L, -2, "set_light_color");

    lua_pushcfunction(L, SceneSetLightIntensity);
    lua_setfield(L, -2, "set_light_intensity");

    lua_pushcfunction(L, SceneGetLightColor);
    lua_setfield(L, -2, "get_light_color");

    lua_pushcfunction(L, SceneGetLightIntensity);
    lua_setfield(L, -2, "get_light_intensity");

    lua_pushcfunction(L, SceneSetLightRange);
    lua_setfield(L, -2, "set_light_range");

    lua_pushcfunction(L, SceneGetLightRange);
    lua_setfield(L, -2, "get_light_range");

    lua_pushcfunction(L, SceneGetSpotAngles);
    lua_setfield(L, -2, "get_spot_angles");

    lua_pushcfunction(L, SceneSetSpotAngles);
    lua_setfield(L, -2, "set_spot_angles");

    lua_pushcfunction(L, SceneGetLightDirection);
    lua_setfield(L, -2, "get_light_direction");

    lua_pushcfunction(L, SceneSetCastShadows);
    lua_setfield(L, -2, "set_cast_shadows");

    lua_pushcfunction(L, SceneRemoveLight);
    lua_setfield(L, -2, "remove_light");

    lua_pushcfunction(L, SceneSetLightTemperature);
    lua_setfield(L, -2, "set_light_temperature");

    lua_pushcfunction(L, SceneGetLightTemperature);
    lua_setfield(L, -2, "get_light_temperature");

    lua_pushcfunction(L, SceneSetLightSpecularScale);
    lua_setfield(L, -2, "set_light_specular_scale");

    lua_pushcfunction(L, SceneSetLightDiffuseScale);
    lua_setfield(L, -2, "set_light_diffuse_scale");

    lua_pushcfunction(L, SceneSetLightFalloff);
    lua_setfield(L, -2, "set_light_falloff");

    lua_pushcfunction(L, SceneSetLightSourceRadius);
    lua_setfield(L, -2, "set_light_source_radius");

    lua_pushcfunction(L, SceneSetAtmosphereSunLight);
    lua_setfield(L, -2, "set_atmosphere_sun_light");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene light API registered");
}

} // namespace uz
