#include "lua_reflection_probe_api.h"

#include "log.h"
#include "world.h"
#include "environment/reflection_probe_component.h"

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
// ReflectionProbeComponent bindings
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.create_reflection_probe — idempotent: adds ReflectionProbeComponent if absent.
static int SceneCreateReflectionProbe(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.create_reflection_probe: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "create_reflection_probe");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) {
        world->AddComponent<ReflectionProbeComponent>(entity);
    }
    return 0;
}

/// uz.scene.remove_reflection_probe — removes ReflectionProbeComponent if present.
static int SceneRemoveReflectionProbe(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.remove_reflection_probe: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "remove_reflection_probe");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (world->HasComponent<ReflectionProbeComponent>(entity)) {
        world->RemoveComponent<ReflectionProbeComponent>(entity);
    }
    return 0;
}

/// uz.scene.set_probe_influence_radius — sets ReflectionProbeComponent::influenceRadius.
static int SceneSetProbeInfluenceRadius(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_probe_influence_radius: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_probe_influence_radius");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_probe_influence_radius: entity {} has no ReflectionProbeComponent", entity.id);
        return 0;
    }
    world->GetComponent<ReflectionProbeComponent>(entity).influenceRadius =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_probe_influence_radius — returns influenceRadius or nil.
static int SceneGetProbeInfluenceRadius(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_probe_influence_radius: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_probe_influence_radius");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<ReflectionProbeComponent>(entity).influenceRadius));
    return 1;
}

/// uz.scene.set_probe_blend_distance — sets ReflectionProbeComponent::blendDistance.
static int SceneSetProbeBlendDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_probe_blend_distance: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_probe_blend_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_probe_blend_distance: entity {} has no ReflectionProbeComponent", entity.id);
        return 0;
    }
    world->GetComponent<ReflectionProbeComponent>(entity).blendDistance =
        static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

/// uz.scene.get_probe_blend_distance — returns blendDistance or nil.
static int SceneGetProbeBlendDistance(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_probe_blend_distance: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_probe_blend_distance");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, static_cast<double>(world->GetComponent<ReflectionProbeComponent>(entity).blendDistance));
    return 1;
}

/// uz.scene.set_probe_resolution — sets ReflectionProbeComponent::resolution.
static int SceneSetProbeResolution(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_probe_resolution: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_probe_resolution");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_probe_resolution: entity {} has no ReflectionProbeComponent", entity.id);
        return 0;
    }
    world->GetComponent<ReflectionProbeComponent>(entity).resolution =
        static_cast<uint32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_probe_resolution — returns resolution or nil.
static int SceneGetProbeResolution(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_probe_resolution: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_probe_resolution");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<ReflectionProbeComponent>(entity).resolution));
    return 1;
}

/// uz.scene.set_probe_priority — sets ReflectionProbeComponent::priority.
static int SceneSetProbePriority(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_probe_priority: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_probe_priority");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_probe_priority: entity {} has no ReflectionProbeComponent", entity.id);
        return 0;
    }
    world->GetComponent<ReflectionProbeComponent>(entity).priority =
        static_cast<int32_t>(luaL_checkinteger(L, 2));
    return 0;
}

/// uz.scene.get_probe_priority — returns priority or nil.
static int SceneGetProbePriority(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_probe_priority: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_probe_priority");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(world->GetComponent<ReflectionProbeComponent>(entity).priority));
    return 1;
}

/// uz.scene.set_probe_enabled — sets ReflectionProbeComponent::enabled.
static int SceneSetProbeEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_probe_enabled: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_probe_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_probe_enabled: entity {} has no ReflectionProbeComponent", entity.id);
        return 0;
    }
    world->GetComponent<ReflectionProbeComponent>(entity).enabled = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.is_probe_enabled — returns enabled or nil.
static int SceneIsProbeEnabled(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.is_probe_enabled: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "is_probe_enabled");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<ReflectionProbeComponent>(entity).enabled ? 1 : 0);
    return 1;
}

/// uz.scene.set_probe_needs_capture — sets ReflectionProbeComponent::needsCapture.
static int SceneSetProbeNeedsCapture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.set_probe_needs_capture: World not set"); return 0; }
    const Entity entity = CheckEntity(L, 1, "set_probe_needs_capture");
    if (!entity.IsValid() || !world->IsValid(entity)) return 0;
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_probe_needs_capture: entity {} has no ReflectionProbeComponent", entity.id);
        return 0;
    }
    world->GetComponent<ReflectionProbeComponent>(entity).needsCapture = lua_toboolean(L, 2) != 0;
    return 0;
}

/// uz.scene.get_probe_needs_capture — returns needsCapture or nil.
static int SceneGetProbeNeedsCapture(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) { UZ_LOG(Scripting, Warning, "uz.scene.get_probe_needs_capture: World not set"); lua_pushnil(L); return 1; }
    const Entity entity = CheckEntity(L, 1, "get_probe_needs_capture");
    if (!entity.IsValid() || !world->IsValid(entity)) { lua_pushnil(L); return 1; }
    if (!world->HasComponent<ReflectionProbeComponent>(entity)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, world->GetComponent<ReflectionProbeComponent>(entity).needsCapture ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterReflectionProbeAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterReflectionProbeAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterReflectionProbeAPI: 'uz.scene' is not a table");
        lua_pop(L, 2);
        return;
    }

    lua_pushcfunction(L, SceneCreateReflectionProbe);    lua_setfield(L, -2, "create_reflection_probe");
    lua_pushcfunction(L, SceneRemoveReflectionProbe);    lua_setfield(L, -2, "remove_reflection_probe");
    lua_pushcfunction(L, SceneSetProbeInfluenceRadius);  lua_setfield(L, -2, "set_probe_influence_radius");
    lua_pushcfunction(L, SceneGetProbeInfluenceRadius);  lua_setfield(L, -2, "get_probe_influence_radius");
    lua_pushcfunction(L, SceneSetProbeBlendDistance);    lua_setfield(L, -2, "set_probe_blend_distance");
    lua_pushcfunction(L, SceneGetProbeBlendDistance);    lua_setfield(L, -2, "get_probe_blend_distance");
    lua_pushcfunction(L, SceneSetProbeResolution);       lua_setfield(L, -2, "set_probe_resolution");
    lua_pushcfunction(L, SceneGetProbeResolution);       lua_setfield(L, -2, "get_probe_resolution");
    lua_pushcfunction(L, SceneSetProbePriority);         lua_setfield(L, -2, "set_probe_priority");
    lua_pushcfunction(L, SceneGetProbePriority);         lua_setfield(L, -2, "get_probe_priority");
    lua_pushcfunction(L, SceneSetProbeEnabled);          lua_setfield(L, -2, "set_probe_enabled");
    lua_pushcfunction(L, SceneIsProbeEnabled);           lua_setfield(L, -2, "is_probe_enabled");
    lua_pushcfunction(L, SceneSetProbeNeedsCapture);     lua_setfield(L, -2, "set_probe_needs_capture");
    lua_pushcfunction(L, SceneGetProbeNeedsCapture);     lua_setfield(L, -2, "get_probe_needs_capture");

    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene reflection probe API registered");
}

} // namespace uz
