/// lua_animation_api — Implementation.

#include "lua_animation_api.h"

#include "animation_controller.h"
#include "skeletal_mesh_component.h"
#include "skeletal_mesh_subsystem.h"
#include "world.h"
#include "log.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>
#include <string>

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key (mirrors lua_entity_api.cpp)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kWorldRegistryKey = "uz_world";

namespace {

[[nodiscard]] World* GetWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<World*>(p);
}

[[nodiscard]] bool UnpackEntity(lua_State* L, int index, Entity& out) {
    if (!lua_islightuserdata(L, index)) return false;
    void* p = lua_touserdata(L, index);
    uint64_t packed;
    static_assert(sizeof(packed) == sizeof(p));
    std::memcpy(&packed, &p, sizeof(packed));
    out.id         = static_cast<uint32_t>(packed >> 32);
    out.generation = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
    return true;
}

/// Resolves the AnimationController attached to `entity`.
/// Returns nullptr if the entity has no SkeletalMeshComponent, the component
/// was never finalised, or the subsystem has no controller at that slot.
[[nodiscard]] AnimationController* ResolveController(lua_State* L, int entityIndex, const char* where) {
    World* world = GetWorld(L);
    if (world == nullptr) {
        UZ_LOG(Scripting, Warning, "uz.animation.{}: World not set", where);
        return nullptr;
    }
    Entity entity;
    if (!UnpackEntity(L, entityIndex, entity)) {
        UZ_LOG(Scripting, Warning, "uz.animation.{}: entity argument is not a lightuserdata", where);
        return nullptr;
    }

    auto& registry = world->GetRegistry();
    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
    if (!registry.valid(enttEntity)) {
        UZ_LOG(Scripting, Warning, "uz.animation.{}: invalid entity (id={})", where, entity.id);
        return nullptr;
    }
    if (!registry.all_of<SkeletalMeshComponent>(enttEntity)) {
        UZ_LOG(Scripting, Warning, "uz.animation.{}: entity has no SkeletalMeshComponent", where);
        return nullptr;
    }

    const auto& skel = registry.get<SkeletalMeshComponent>(enttEntity);
    if (skel.controllerId == UINT32_MAX) {
        UZ_LOG(Scripting, Warning, "uz.animation.{}: entity's controller is not finalised yet", where);
        return nullptr;
    }

    AnimationController* ctrl = SkeletalMeshSubsystem::Get().GetController(skel.controllerId);
    if (ctrl == nullptr) {
        UZ_LOG(Scripting, Warning, "uz.animation.{}: controllerId {} out of range", where, skel.controllerId);
    }
    return ctrl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions
// ─────────────────────────────────────────────────────────────────────────────

/// uz.animation.play(entity, clipName, loop?, speed?) -> boolean
int AnimPlay(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "play");
    if (ctrl == nullptr) { lua_pushboolean(L, 0); return 1; }
    if (!lua_isstring(L, 2)) { lua_pushboolean(L, 0); return 1; }

    const std::string clipName = lua_tostring(L, 2);
    const bool  loop  = lua_isboolean(L, 3) ? (lua_toboolean(L, 3) != 0) : true;
    const float speed = lua_isnumber(L, 4) ? static_cast<float>(lua_tonumber(L, 4)) : 1.0f;

    if (!ctrl->HasClip(clipName)) {
        UZ_LOG(Scripting, Warning, "uz.animation.play: clip '{}' not registered", clipName);
        lua_pushboolean(L, 0);
        return 1;
    }
    ctrl->Play(clipName, loop, speed);
    lua_pushboolean(L, 1);
    return 1;
}

/// uz.animation.crossfade(entity, clipName, blendTime, loop?, speed?) -> boolean
int AnimCrossfade(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "crossfade");
    if (ctrl == nullptr) { lua_pushboolean(L, 0); return 1; }
    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) { lua_pushboolean(L, 0); return 1; }

    const std::string clipName = lua_tostring(L, 2);
    const float blendTime = static_cast<float>(lua_tonumber(L, 3));
    const bool  loop  = lua_isboolean(L, 4) ? (lua_toboolean(L, 4) != 0) : true;
    const float speed = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 1.0f;

    if (!ctrl->HasClip(clipName)) {
        UZ_LOG(Scripting, Warning, "uz.animation.crossfade: clip '{}' not registered", clipName);
        lua_pushboolean(L, 0);
        return 1;
    }
    ctrl->Crossfade(clipName, blendTime, loop, speed);
    lua_pushboolean(L, 1);
    return 1;
}

/// uz.animation.stop(entity)
int AnimStop(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "stop");
    if (ctrl != nullptr) ctrl->Stop();
    return 0;
}

/// uz.animation.get_current_clip(entity) -> string | nil
int AnimGetCurrentClip(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "get_current_clip");
    if (ctrl == nullptr) { lua_pushnil(L); return 1; }
    const std::string name = ctrl->GetCurrentClipName();
    if (name.empty()) { lua_pushnil(L); return 1; }
    lua_pushstring(L, name.c_str());
    return 1;
}

/// uz.animation.get_current_time(entity) -> number
int AnimGetCurrentTime(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "get_current_time");
    if (ctrl == nullptr) { lua_pushnumber(L, 0.0); return 1; }
    lua_pushnumber(L, static_cast<double>(ctrl->GetCurrentTime()));
    return 1;
}

/// uz.animation.set_bool(entity, name, value)
int AnimSetBool(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "set_bool");
    if (ctrl == nullptr) return 0;
    if (!lua_isstring(L, 2)) return 0;
    ctrl->SetBool(lua_tostring(L, 2), lua_toboolean(L, 3) != 0);
    return 0;
}

/// uz.animation.set_float(entity, name, value)
int AnimSetFloat(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "set_float");
    if (ctrl == nullptr) return 0;
    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) return 0;
    ctrl->SetFloat(lua_tostring(L, 2), static_cast<float>(lua_tonumber(L, 3)));
    return 0;
}

/// uz.animation.set_trigger(entity, name)
int AnimSetTrigger(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "set_trigger");
    if (ctrl == nullptr) return 0;
    if (!lua_isstring(L, 2)) return 0;
    ctrl->SetTrigger(lua_tostring(L, 2));
    return 0;
}

/// uz.animation.get_bool(entity, name) -> boolean
int AnimGetBool(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "get_bool");
    if (ctrl == nullptr || !lua_isstring(L, 2)) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, ctrl->GetBool(lua_tostring(L, 2)) ? 1 : 0);
    return 1;
}

/// uz.animation.get_float(entity, name) -> number
int AnimGetFloat(lua_State* L) {
    AnimationController* ctrl = ResolveController(L, 1, "get_float");
    if (ctrl == nullptr || !lua_isstring(L, 2)) { lua_pushnumber(L, 0.0); return 1; }
    lua_pushnumber(L, static_cast<double>(ctrl->GetFloat(lua_tostring(L, 2))));
    return 1;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterAnimationAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
               "LuaRegisterAnimationAPI: 'uz' global is not a table — register base table first");
        lua_pop(L, 1);
        return;
    }

    lua_newtable(L);
    lua_pushcfunction(L, AnimPlay);            lua_setfield(L, -2, "play");
    lua_pushcfunction(L, AnimCrossfade);       lua_setfield(L, -2, "crossfade");
    lua_pushcfunction(L, AnimStop);            lua_setfield(L, -2, "stop");
    lua_pushcfunction(L, AnimGetCurrentClip);  lua_setfield(L, -2, "get_current_clip");
    lua_pushcfunction(L, AnimGetCurrentTime);  lua_setfield(L, -2, "get_current_time");
    lua_pushcfunction(L, AnimSetBool);         lua_setfield(L, -2, "set_bool");
    lua_pushcfunction(L, AnimSetFloat);        lua_setfield(L, -2, "set_float");
    lua_pushcfunction(L, AnimSetTrigger);      lua_setfield(L, -2, "set_trigger");
    lua_pushcfunction(L, AnimGetBool);         lua_setfield(L, -2, "get_bool");
    lua_pushcfunction(L, AnimGetFloat);        lua_setfield(L, -2, "get_float");

    lua_setfield(L, -2, "animation");  // uz.animation = <table>
    lua_pop(L, 1);                     // pop uz

    UZ_LOG(Scripting, Info, "Lua animation API registered (uz.animation)");
}

} // namespace uz
