#include "lua_environment_api.h"

#include "log.h"
#include "world.h"
#include "environment/height_fog_component.h"
#include "environment/sky_light_component.h"
#include "environment/sky_atmosphere_component.h"
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

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmosphere (15.5)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.create_sky_atmosphere — idempotent: adds SkyAtmosphereComponent if absent.
static int SceneCreateSkyAtmosphere(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.create_sky_atmosphere: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "create_sky_atmosphere");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        world->AddComponent<SkyAtmosphereComponent>(entity);
    }
    return 0;
}

/// uz.scene.set_sky_rayleigh_scale — sets SkyAtmosphereComponent::rayleighScatteringScale.
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

/// uz.scene.set_sky_mie_scale — sets SkyAtmosphereComponent::mieScatteringScale.
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

/// uz.scene.set_sky_multi_scattering — sets SkyAtmosphereComponent::multiScatteringFactor.
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

/// uz.scene.set_sky_bottom_radius — sets SkyAtmosphereComponent::bottomRadiusKm.
static int SceneSetSkyBottomRadius(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_bottom_radius: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_bottom_radius");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_bottom_radius: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).bottomRadiusKm =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_atmosphere_height — sets SkyAtmosphereComponent::atmosphereHeightKm.
static int SceneSetSkyAtmosphereHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_atmosphere_height: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_atmosphere_height");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_atmosphere_height: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).atmosphereHeightKm =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_rayleigh_scattering — sets SkyAtmosphereComponent::rayleighScattering (r, g, b) in km^-1.
static int SceneSetSkyRayleighScattering(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_rayleigh_scattering: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_rayleigh_scattering");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_rayleigh_scattering: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    auto& atmo = world->GetComponent<SkyAtmosphereComponent>(entity);
    atmo.rayleighScattering.x = static_cast<float>(luaL_checknumber(L, 2));
    atmo.rayleighScattering.y = static_cast<float>(luaL_checknumber(L, 3));
    atmo.rayleighScattering.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_sky_rayleigh_scale_height — sets SkyAtmosphereComponent::rayleighScaleHeightKm.
static int SceneSetSkyRayleighScaleHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_rayleigh_scale_height: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_rayleigh_scale_height");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_rayleigh_scale_height: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).rayleighScaleHeightKm =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_mie_scattering — sets SkyAtmosphereComponent::mieScattering (km^-1).
static int SceneSetSkyMieScattering(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_scattering: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_mie_scattering");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_scattering: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).mieScattering =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_mie_absorption — sets SkyAtmosphereComponent::mieAbsorption (km^-1).
static int SceneSetSkyMieAbsorption(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_absorption: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_mie_absorption");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_absorption: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).mieAbsorption =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_mie_absorption_scale — sets SkyAtmosphereComponent::mieAbsorptionScale.
static int SceneSetSkyMieAbsorptionScale(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_absorption_scale: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_mie_absorption_scale");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_absorption_scale: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).mieAbsorptionScale =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_mie_anisotropy — sets SkyAtmosphereComponent::mieAnisotropy.
static int SceneSetSkyMieAnisotropy(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_anisotropy: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_mie_anisotropy");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_anisotropy: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).mieAnisotropy =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_mie_scale_height — sets SkyAtmosphereComponent::mieScaleHeightKm.
static int SceneSetSkyMieScaleHeight(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_scale_height: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_mie_scale_height");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_mie_scale_height: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).mieScaleHeightKm =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_ground_albedo — sets SkyAtmosphereComponent::groundAlbedo (r, g, b).
static int SceneSetSkyGroundAlbedo(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ground_albedo: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_ground_albedo");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ground_albedo: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    auto& atmo = world->GetComponent<SkyAtmosphereComponent>(entity);
    atmo.groundAlbedo.x = static_cast<float>(luaL_checknumber(L, 2));
    atmo.groundAlbedo.y = static_cast<float>(luaL_checknumber(L, 3));
    atmo.groundAlbedo.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_sky_ozone_absorption — sets SkyAtmosphereComponent::ozoneAbsorption (r, g, b) in km^-1.
static int SceneSetSkyOzoneAbsorption(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_absorption: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_ozone_absorption");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_absorption: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    auto& atmo = world->GetComponent<SkyAtmosphereComponent>(entity);
    atmo.ozoneAbsorption.x = static_cast<float>(luaL_checknumber(L, 2));
    atmo.ozoneAbsorption.y = static_cast<float>(luaL_checknumber(L, 3));
    atmo.ozoneAbsorption.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_sky_ozone_tip_altitude — sets SkyAtmosphereComponent::ozoneTipAltitude (km).
static int SceneSetSkyOzoneTipAltitude(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_tip_altitude: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_ozone_tip_altitude");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_tip_altitude: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).ozoneTipAltitude =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_ozone_tip_value — sets SkyAtmosphereComponent::ozoneTipValue.
static int SceneSetSkyOzoneTipValue(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_tip_value: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_ozone_tip_value");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_tip_value: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).ozoneTipValue =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_ozone_width — sets SkyAtmosphereComponent::ozoneWidth (km).
static int SceneSetSkyOzoneWidth(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_width: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_ozone_width");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_ozone_width: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).ozoneWidth =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.set_sky_luminance_factor — sets SkyAtmosphereComponent::skyLuminanceFactor (r, g, b).
static int SceneSetSkyLuminanceFactor(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_luminance_factor: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_luminance_factor");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_luminance_factor: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    auto& atmo = world->GetComponent<SkyAtmosphereComponent>(entity);
    atmo.skyLuminanceFactor.x = static_cast<float>(luaL_checknumber(L, 2));
    atmo.skyLuminanceFactor.y = static_cast<float>(luaL_checknumber(L, 3));
    atmo.skyLuminanceFactor.z = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.set_sky_height_fog_contribution — sets SkyAtmosphereComponent::heightFogContribution.
static int SceneSetSkyHeightFogContribution(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_sky_height_fog_contribution: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_sky_height_fog_contribution");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<SkyAtmosphereComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_sky_height_fog_contribution: entity {} has no SkyAtmosphereComponent", entity.id);
        return 0;
    }
    world->GetComponent<SkyAtmosphereComponent>(entity).heightFogContribution =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
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

    // SkyLight
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

    // SkyAtmosphere
    lua_pushcfunction(L, SceneCreateSkyAtmosphere);        lua_setfield(L, -2, "create_sky_atmosphere");
    lua_pushcfunction(L, SceneSetSkyRayleighScale);        lua_setfield(L, -2, "set_sky_rayleigh_scale");
    lua_pushcfunction(L, SceneSetSkyMieScale);             lua_setfield(L, -2, "set_sky_mie_scale");
    lua_pushcfunction(L, SceneSetSkyMultiScattering);      lua_setfield(L, -2, "set_sky_multi_scattering");
    lua_pushcfunction(L, SceneSetSkyBottomRadius);         lua_setfield(L, -2, "set_sky_bottom_radius");
    lua_pushcfunction(L, SceneSetSkyAtmosphereHeight);     lua_setfield(L, -2, "set_sky_atmosphere_height");
    lua_pushcfunction(L, SceneSetSkyRayleighScattering);   lua_setfield(L, -2, "set_sky_rayleigh_scattering");
    lua_pushcfunction(L, SceneSetSkyRayleighScaleHeight);  lua_setfield(L, -2, "set_sky_rayleigh_scale_height");
    lua_pushcfunction(L, SceneSetSkyMieScattering);        lua_setfield(L, -2, "set_sky_mie_scattering");
    lua_pushcfunction(L, SceneSetSkyMieAbsorption);        lua_setfield(L, -2, "set_sky_mie_absorption");
    lua_pushcfunction(L, SceneSetSkyMieAbsorptionScale);   lua_setfield(L, -2, "set_sky_mie_absorption_scale");
    lua_pushcfunction(L, SceneSetSkyMieAnisotropy);        lua_setfield(L, -2, "set_sky_mie_anisotropy");
    lua_pushcfunction(L, SceneSetSkyMieScaleHeight);       lua_setfield(L, -2, "set_sky_mie_scale_height");
    lua_pushcfunction(L, SceneSetSkyGroundAlbedo);         lua_setfield(L, -2, "set_sky_ground_albedo");
    lua_pushcfunction(L, SceneSetSkyOzoneAbsorption);      lua_setfield(L, -2, "set_sky_ozone_absorption");
    lua_pushcfunction(L, SceneSetSkyOzoneTipAltitude);     lua_setfield(L, -2, "set_sky_ozone_tip_altitude");
    lua_pushcfunction(L, SceneSetSkyOzoneTipValue);        lua_setfield(L, -2, "set_sky_ozone_tip_value");
    lua_pushcfunction(L, SceneSetSkyOzoneWidth);           lua_setfield(L, -2, "set_sky_ozone_width");
    lua_pushcfunction(L, SceneSetSkyLuminanceFactor);      lua_setfield(L, -2, "set_sky_luminance_factor");
    lua_pushcfunction(L, SceneSetSkyHeightFogContribution);lua_setfield(L, -2, "set_sky_height_fog_contribution");

    // VolumetricClouds
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
    lua_pushcfunction(L, SceneSetCloudsTracingMaxDistance);      lua_setfield(L, -2, "set_clouds_tracing_max_distance");
    lua_pushcfunction(L, SceneSetCloudsPrimarySteps);           lua_setfield(L, -2, "set_clouds_primary_steps");
    lua_pushcfunction(L, SceneSetCloudsLightSteps);             lua_setfield(L, -2, "set_clouds_light_steps");
    lua_pushcfunction(L, SceneSetCloudsEnabled);                lua_setfield(L, -2, "set_clouds_enabled");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene environment API registered");
}

} // namespace uz
