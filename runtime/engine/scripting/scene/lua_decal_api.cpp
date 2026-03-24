#include "lua_decal_api.h"

#include "log.h"
#include "world.h"
#include "environment/decal_component.h"

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
// DecalComponent bindings
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.create_decal — idempotent: adds DecalComponent if absent.
static int SceneCreateDecal(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.create_decal: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "create_decal");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        world->AddComponent<DecalComponent>(entity);
    }
    return 0;
}

/// uz.scene.remove_decal — removes DecalComponent if present.
static int SceneRemoveDecal(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.remove_decal: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "remove_decal");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (world->HasComponent<DecalComponent>(entity)) {
        world->RemoveComponent<DecalComponent>(entity);
    }
    return 0;
}

/// uz.scene.set_decal_extents — sets DecalComponent::extentsX/Y/Z.
static int SceneSetDecalExtents(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_extents: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_extents");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_extents: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    auto& decal = world->GetComponent<DecalComponent>(entity);
    decal.extentsX = static_cast<float>(luaL_checknumber(L, 2));
    decal.extentsY = static_cast<float>(luaL_checknumber(L, 3));
    decal.extentsZ = static_cast<float>(luaL_checknumber(L, 4));
    return 0;
}

/// uz.scene.get_decal_extents — returns extentsX, extentsY, extentsZ or nil.
static int SceneGetDecalExtents(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_extents: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_extents");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    const auto& decal = world->GetComponent<DecalComponent>(entity);
    lua_pushnumber(L, static_cast<double>(decal.extentsX));
    lua_pushnumber(L, static_cast<double>(decal.extentsY));
    lua_pushnumber(L, static_cast<double>(decal.extentsZ));
    return 3;
}

/// uz.scene.set_decal_albedo_texture — sets DecalComponent::albedoTextureId.
static int SceneSetDecalAlbedoTexture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_albedo_texture: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_albedo_texture");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_albedo_texture: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).albedoTextureId =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_decal_albedo_texture — returns albedoTextureId or nil.
static int SceneGetDecalAlbedoTexture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_albedo_texture: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_albedo_texture");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<DecalComponent>(entity).albedoTextureId));
    return 1;
}

/// uz.scene.set_decal_normal_texture — sets DecalComponent::normalTextureId.
static int SceneSetDecalNormalTexture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_normal_texture: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_normal_texture");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_normal_texture: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).normalTextureId =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_decal_normal_texture — returns normalTextureId or nil.
static int SceneGetDecalNormalTexture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_normal_texture: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_normal_texture");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<DecalComponent>(entity).normalTextureId));
    return 1;
}

/// uz.scene.set_decal_material_texture — sets DecalComponent::materialTextureId.
static int SceneSetDecalMaterialTexture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_material_texture: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_material_texture");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_material_texture: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).materialTextureId =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_decal_material_texture — returns materialTextureId or nil.
static int SceneGetDecalMaterialTexture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_material_texture: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_material_texture");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<DecalComponent>(entity).materialTextureId));
    return 1;
}

/// uz.scene.set_decal_opacity — sets DecalComponent::opacity.
static int SceneSetDecalOpacity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_opacity: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_opacity");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_opacity: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).opacity =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_decal_opacity — returns opacity or nil.
static int SceneGetDecalOpacity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_opacity: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_opacity");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<DecalComponent>(entity).opacity));
    return 1;
}

/// uz.scene.set_decal_normal_blend — sets DecalComponent::normalBlend.
static int SceneSetDecalNormalBlend(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_normal_blend: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_normal_blend");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_normal_blend: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).normalBlend =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_decal_normal_blend — returns normalBlend or nil.
static int SceneGetDecalNormalBlend(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_normal_blend: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_normal_blend");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<DecalComponent>(entity).normalBlend));
    return 1;
}

/// uz.scene.set_decal_roughness — sets DecalComponent::roughness.
static int SceneSetDecalRoughness(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_roughness: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_roughness");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_roughness: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).roughness =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_decal_roughness — returns roughness or nil.
static int SceneGetDecalRoughness(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_roughness: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_roughness");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<DecalComponent>(entity).roughness));
    return 1;
}

/// uz.scene.set_decal_metallic — sets DecalComponent::metallic.
static int SceneSetDecalMetallic(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_metallic: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_metallic");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_metallic: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).metallic =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_decal_metallic — returns metallic or nil.
static int SceneGetDecalMetallic(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_metallic: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_metallic");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<DecalComponent>(entity).metallic));
    return 1;
}

/// uz.scene.set_decal_priority — sets DecalComponent::priority.
static int SceneSetDecalPriority(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_priority: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_priority");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_priority: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).priority =
        static_cast<int32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_decal_priority — returns priority or nil.
static int SceneGetDecalPriority(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_priority: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_priority");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<DecalComponent>(entity).priority));
    return 1;
}

/// uz.scene.set_decal_fade_distance — sets DecalComponent::fadeDistance.
static int SceneSetDecalFadeDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_fade_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_fade_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_fade_distance: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).fadeDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_decal_fade_distance — returns fadeDistance or nil.
static int SceneGetDecalFadeDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_fade_distance: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_fade_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<DecalComponent>(entity).fadeDistance));
    return 1;
}

/// uz.scene.set_decal_angle_fade_start — sets DecalComponent::angleFadeStart.
static int SceneSetDecalAngleFadeStart(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_angle_fade_start: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_angle_fade_start");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_angle_fade_start: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).angleFadeStart =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_decal_angle_fade_start — returns angleFadeStart or nil.
static int SceneGetDecalAngleFadeStart(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_decal_angle_fade_start: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_decal_angle_fade_start");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<DecalComponent>(entity).angleFadeStart));
    return 1;
}

/// uz.scene.set_decal_enabled — sets DecalComponent::enabled.
static int SceneSetDecalEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_decal_enabled: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_decal_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<DecalComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_decal_enabled: entity {} has no DecalComponent", entity.id);
        return 0;
    }
    world->GetComponent<DecalComponent>(entity).enabled = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.is_decal_enabled — returns enabled or nil.
static int SceneIsDecalEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.is_decal_enabled: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_decal_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<DecalComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<DecalComponent>(entity).enabled ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterDecalAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterDecalAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterDecalAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    lua_pushcfunction(L, SceneCreateDecal);              lua_setfield(L, -2, "create_decal");
    lua_pushcfunction(L, SceneRemoveDecal);              lua_setfield(L, -2, "remove_decal");
    lua_pushcfunction(L, SceneSetDecalExtents);          lua_setfield(L, -2, "set_decal_extents");
    lua_pushcfunction(L, SceneGetDecalExtents);          lua_setfield(L, -2, "get_decal_extents");
    lua_pushcfunction(L, SceneSetDecalAlbedoTexture);    lua_setfield(L, -2, "set_decal_albedo_texture");
    lua_pushcfunction(L, SceneGetDecalAlbedoTexture);    lua_setfield(L, -2, "get_decal_albedo_texture");
    lua_pushcfunction(L, SceneSetDecalNormalTexture);    lua_setfield(L, -2, "set_decal_normal_texture");
    lua_pushcfunction(L, SceneGetDecalNormalTexture);    lua_setfield(L, -2, "get_decal_normal_texture");
    lua_pushcfunction(L, SceneSetDecalMaterialTexture);  lua_setfield(L, -2, "set_decal_material_texture");
    lua_pushcfunction(L, SceneGetDecalMaterialTexture);  lua_setfield(L, -2, "get_decal_material_texture");
    lua_pushcfunction(L, SceneSetDecalOpacity);          lua_setfield(L, -2, "set_decal_opacity");
    lua_pushcfunction(L, SceneGetDecalOpacity);          lua_setfield(L, -2, "get_decal_opacity");
    lua_pushcfunction(L, SceneSetDecalNormalBlend);      lua_setfield(L, -2, "set_decal_normal_blend");
    lua_pushcfunction(L, SceneGetDecalNormalBlend);      lua_setfield(L, -2, "get_decal_normal_blend");
    lua_pushcfunction(L, SceneSetDecalRoughness);        lua_setfield(L, -2, "set_decal_roughness");
    lua_pushcfunction(L, SceneGetDecalRoughness);        lua_setfield(L, -2, "get_decal_roughness");
    lua_pushcfunction(L, SceneSetDecalMetallic);         lua_setfield(L, -2, "set_decal_metallic");
    lua_pushcfunction(L, SceneGetDecalMetallic);         lua_setfield(L, -2, "get_decal_metallic");
    lua_pushcfunction(L, SceneSetDecalPriority);         lua_setfield(L, -2, "set_decal_priority");
    lua_pushcfunction(L, SceneGetDecalPriority);         lua_setfield(L, -2, "get_decal_priority");
    lua_pushcfunction(L, SceneSetDecalFadeDistance);     lua_setfield(L, -2, "set_decal_fade_distance");
    lua_pushcfunction(L, SceneGetDecalFadeDistance);     lua_setfield(L, -2, "get_decal_fade_distance");
    lua_pushcfunction(L, SceneSetDecalAngleFadeStart);   lua_setfield(L, -2, "set_decal_angle_fade_start");
    lua_pushcfunction(L, SceneGetDecalAngleFadeStart);   lua_setfield(L, -2, "get_decal_angle_fade_start");
    lua_pushcfunction(L, SceneSetDecalEnabled);          lua_setfield(L, -2, "set_decal_enabled");
    lua_pushcfunction(L, SceneIsDecalEnabled);           lua_setfield(L, -2, "is_decal_enabled");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene decal API registered");
}

} // namespace uz
