#include "lua_atmosphere_api.h"

#include "log.h"
#include "world.h"
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
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterAtmosphereAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterAtmosphereAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterAtmosphereAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

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

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene atmosphere API registered");
}

} // namespace uz
