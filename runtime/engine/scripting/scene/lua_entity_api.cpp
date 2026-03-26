/// Lua bindings for the entity API (create, destroy, components, transforms, hierarchy).

#include "lua_entity_api.h"

#include "log.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "camera_component.h"
#include "light_component.h"
#include "script_component.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>

/// Lua bindings for the entity/node API (transform, components, hierarchy, lifecycle).

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time guarantee: void* must be at least 64 bits to encode Entity.
// ─────────────────────────────────────────────────────────────────────────────

static_assert(sizeof(void*) >= 8,
    "uz::Entity requires 64-bit pointers for lightuserdata encoding");

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key constants
// ─────────────────────────────────────────────────────────────────────────────

/// Key used to store the World* in the Lua registry.
static constexpr const char* kWorldRegistryKey = "uz_world";

// ─────────────────────────────────────────────────────────────────────────────
// Entity <-> lightuserdata encoding helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Packs an Entity into a void* for use as Lua lightuserdata.
///
/// High 32 bits = entity.id, low 32 bits = entity.generation.
[[nodiscard]] static void* EntityToUserdata(Entity e) noexcept {
    const uint64_t packed = (static_cast<uint64_t>(e.id) << 32)
                          | static_cast<uint64_t>(e.generation);
    void* p;
    static_assert(sizeof(packed) == sizeof(p));
    std::memcpy(&p, &packed, sizeof(p));
    return p;
}

/// Unpacks an Entity from a void* lightuserdata.
[[nodiscard]] static Entity UserdataToEntity(void* p) noexcept {
    uint64_t packed;
    static_assert(sizeof(packed) == sizeof(p));
    std::memcpy(&packed, &p, sizeof(packed));
    Entity e;
    e.id         = static_cast<uint32_t>(packed >> 32);
    e.generation = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — retrieve World and validate entity
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the World* from the Lua registry. Returns null if not set.
[[nodiscard]] static World* GetWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<World*>(p);
}

/// Reads an entity lightuserdata from stack position `idx`.
/// Returns Entity::Invalid() and logs a warning if the value is not lightuserdata.
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
// Lua C functions — uz.scene.* (entity lifecycle)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.create_entity(name?) -> entity | nil
///
/// Creates a new entity via World::CreateEntity(). If an optional string
/// argument is provided, attaches a CName component. Always adds a
/// SceneComponent so the entity has a transform. Returns the entity as
/// lightuserdata, or nil if the World is not set.
static int SceneCreateEntity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.create_entity: World not set");
        lua_pushnil(L);
        return 1;
    }

    const Entity entity = world->CreateEntity();

    // Add SceneComponent so the entity has a transform.
    world->AddComponent<SceneComponent>(entity);

    // Optional name argument.
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        const char* name = lua_tostring(L, 1);
        if (name && name[0] != '\0') {
            world->AddComponent<CName>(entity, CName{name});
        }
    }

    lua_pushlightuserdata(L, EntityToUserdata(entity));
    return 1;
}

/// uz.scene.destroy_entity(entity) -> void
///
/// Destroys the entity referenced by the lightuserdata argument.
/// No-op if the entity is invalid or stale.
static int SceneDestroyEntity(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.destroy_entity: World not set");
        return 0;
    }

    const Entity entity = CheckEntity(L, 1, "destroy_entity");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        return 0;
    }

    world->DestroyEntity(entity);
    return 0;
}

/// uz.scene.is_valid(entity) -> bool
///
/// Returns true if the entity lightuserdata refers to a living entity
/// whose generation matches. Returns false if the World is not set,
/// the argument is not lightuserdata, or the entity is stale/destroyed.
static int SceneIsValid(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (!lua_islightuserdata(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const Entity entity = UserdataToEntity(lua_touserdata(L, 1));
    lua_pushboolean(L, entity.IsValid() && world->IsValid(entity) ? 1 : 0);
    return 1;
}

/// uz.scene.get_name(entity) -> string | nil
///
/// Returns the CName.name string for the entity, or nil if the entity
/// has no CName component, is invalid, or the World is not set.
static int SceneGetName(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.get_name: World not set");
        lua_pushnil(L);
        return 1;
    }

    const Entity entity = CheckEntity(L, 1, "get_name");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        lua_pushnil(L);
        return 1;
    }

    if (!world->HasComponent<CName>(entity)) {
        lua_pushnil(L);
        return 1;
    }

    const CName& cname = world->GetComponent<CName>(entity);
    lua_pushstring(L, cname.name.c_str());
    return 1;
}

/// uz.scene.set_name(entity, name) -> void
///
/// Sets or replaces the CName component on the entity. If the entity
/// doesn't have a CName, one is added. If it already has one, the name
/// field is updated in place.
static int SceneSetName(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_name: World not set");
        return 0;
    }

    const Entity entity = CheckEntity(L, 1, "set_name");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        return 0;
    }

    const char* name = luaL_checkstring(L, 2);

    if (world->HasComponent<CName>(entity)) {
        world->GetComponent<CName>(entity).name = name;
    } else {
        world->AddComponent<CName>(entity, CName{name});
    }

    return 0;
}

/// uz.scene.entity_count() -> integer
///
/// Returns the number of currently alive entities in the World.
/// Returns 0 if the World is not set.
static int SceneEntityCount(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(world->EntityCount()));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Component name → type helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Supported component names and their meaning:
///   "transform" → SceneComponent
///   "camera"    → CameraComponent
///   "light"     → any of DirectionalLightComponent / PointLightComponent / SpotLightComponent
///   "mesh"      → StaticMeshComponent
///   "script"    → CScript
///   "name"      → CName

/// Returns true if `entity` has the component identified by `name`.
///
/// Returns false and logs a warning for unrecognised component names.
[[nodiscard]] static bool EntityHasNamedComponent(
    World& world, Entity entity, const char* name)
{
    if (std::strcmp(name, "transform") == 0) {
        return world.HasComponent<SceneComponent>(entity);
    }
    if (std::strcmp(name, "camera") == 0) {
        return world.HasComponent<CameraComponent>(entity);
    }
    if (std::strcmp(name, "light") == 0) {
        return world.HasComponent<DirectionalLightComponent>(entity)
            || world.HasComponent<PointLightComponent>(entity)
            || world.HasComponent<SpotLightComponent>(entity);
    }
    if (std::strcmp(name, "mesh") == 0) {
        return world.HasComponent<StaticMeshComponent>(entity);
    }
    if (std::strcmp(name, "script") == 0) {
        return world.HasComponent<CScript>(entity);
    }
    if (std::strcmp(name, "name") == 0) {
        return world.HasComponent<CName>(entity);
    }
    return false;  // Unknown name — caller logs as needed.
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.scene.has_component / uz.scene.remove_component
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.has_component(entity, component_name) -> bool
///
/// Returns true when the entity has the named component.
///
/// Supported component names:
/// - `"transform"` — `SceneComponent`
/// - `"camera"`    — `CameraComponent`
/// - `"light"`     — any of the three light component types
/// - `"mesh"`      — `StaticMeshComponent`
/// - `"script"`    — `CScript`
/// - `"name"`      — `CName`
///
/// Returns false for unknown names (with a Warning log) and for invalid entities.
///
/// # Arguments
/// * `entity`         (lightuserdata) — Target entity.
/// * `component_name` (string)        — One of the supported names above.
///
/// # Returns
/// `true` if the component exists on the entity, `false` otherwise.
static int SceneHasComponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.has_component: World not set");
        lua_pushboolean(L, 0);
        return 1;
    }

    const Entity entity = CheckEntity(L, 1, "has_component");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* name = luaL_checkstring(L, 2);
    const bool has = EntityHasNamedComponent(*world, entity, name);

    if (!has) {
        // Distinguish "missing component" from "unknown name" by checking the name.
        const bool knownName =
            std::strcmp(name, "transform") == 0 ||
            std::strcmp(name, "camera")    == 0 ||
            std::strcmp(name, "light")     == 0 ||
            std::strcmp(name, "mesh")      == 0 ||
            std::strcmp(name, "script")    == 0 ||
            std::strcmp(name, "name")      == 0;

        if (!knownName) {
            UZ_LOG(Scripting, Warning,
                "uz.scene.has_component: unknown component name '{}'. "
                "Supported: transform, camera, light, mesh, script, name",
                name);
        }
    }

    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

/// uz.scene.remove_component(entity, component_name) -> void
///
/// Removes the named component from the entity. No-op if the entity does not
/// have the component. For "light", removes whichever light component type is
/// present (at most one type is expected per entity in MVP).
///
/// Supported component names:
/// - `"transform"` — `SceneComponent`
/// - `"camera"`    — `CameraComponent`
/// - `"light"`     — whichever light type the entity has
/// - `"mesh"`      — `StaticMeshComponent`
/// - `"script"`    — `CScript`
/// - `"name"`      — `CName`
///
/// Logs a Warning for unknown component names. Removing "transform" is logged
/// at Warning level because it leaves the entity without spatial state and will
/// break most scene API calls.
///
/// # Arguments
/// * `entity`         (lightuserdata) — Target entity.
/// * `component_name` (string)        — One of the supported names above.
static int SceneRemoveComponent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.remove_component: World not set");
        return 0;
    }

    const Entity entity = CheckEntity(L, 1, "remove_component");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        return 0;
    }

    const char* name = luaL_checkstring(L, 2);

    if (std::strcmp(name, "transform") == 0) {
        // Removing the transform breaks spatial API — warn the caller.
        if (world->HasComponent<SceneComponent>(entity)) {
            UZ_LOG(Scripting, Warning,
                "uz.scene.remove_component: removing 'transform' from entity {} — "
                "scene API calls on this entity will no longer work",
                entity.id);
            world->RemoveComponent<SceneComponent>(entity);
        }
    } else if (std::strcmp(name, "camera") == 0) {
        if (world->HasComponent<CameraComponent>(entity)) {
            world->RemoveComponent<CameraComponent>(entity);
        }
    } else if (std::strcmp(name, "light") == 0) {
        // Remove whichever light type is present (MVP: one light type per entity).
        if (world->HasComponent<DirectionalLightComponent>(entity)) {
            world->RemoveComponent<DirectionalLightComponent>(entity);
        } else if (world->HasComponent<PointLightComponent>(entity)) {
            world->RemoveComponent<PointLightComponent>(entity);
        } else if (world->HasComponent<SpotLightComponent>(entity)) {
            world->RemoveComponent<SpotLightComponent>(entity);
        }
    } else if (std::strcmp(name, "mesh") == 0) {
        if (world->HasComponent<StaticMeshComponent>(entity)) {
            world->RemoveComponent<StaticMeshComponent>(entity);
        }
    } else if (std::strcmp(name, "script") == 0) {
        if (world->HasComponent<CScript>(entity)) {
            world->RemoveComponent<CScript>(entity);
        }
    } else if (std::strcmp(name, "name") == 0) {
        if (world->HasComponent<CName>(entity)) {
            world->RemoveComponent<CName>(entity);
        }
    } else {
        UZ_LOG(Scripting, Warning,
            "uz.scene.remove_component: unknown component name '{}'. "
            "Supported: transform, camera, light, mesh, script, name",
            name);
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.scene.get_entities / get_entities_with / clear
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.get_entities() -> table (array of entity lightuserdata)
///
/// Returns a Lua array containing a lightuserdata handle for every alive entity
/// in the World. The table is 1-indexed (standard Lua convention).
///
/// Uses `View<SceneComponent>()` as the iteration source because every entity
/// created via `uz.scene.create_entity` receives a `SceneComponent`. Entities
/// created directly via the C++ World API without a SceneComponent will not
/// appear in this list.
///
/// # Returns
/// A Lua table (array) of lightuserdata entity handles. Empty table if the
/// World is not set or no entities exist.
static int SceneGetEntities(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.get_entities: World not set");
        lua_newtable(L);
        return 1;
    }

    lua_newtable(L);  // result array

    lua_Integer index = 1;
    for (auto [enttEntity, comp] : world->View<SceneComponent>().each()) {
        (void)comp;
        const Entity entity = world->EntityFromEntt(enttEntity);
        lua_pushlightuserdata(L, EntityToUserdata(entity));
        lua_rawseti(L, -2, index++);
    }

    return 1;
}

/// uz.scene.get_entities_with(component_name) -> table (array of entity lightuserdata)
///
/// Returns a Lua array of entity handles filtered to those that have the named
/// component. Supports the same component name set as `uz.scene.has_component`.
///
/// # Component names
/// - `"transform"` — `SceneComponent`
/// - `"camera"`    — `CameraComponent`
/// - `"light"`     — any of `DirectionalLightComponent`, `PointLightComponent`,
///                   `SpotLightComponent` (entity is included if it has any one)
/// - `"mesh"`      — `StaticMeshComponent`
/// - `"script"`    — `CScript`
/// - `"name"`      — `CName`
///
/// # Arguments
/// * `component_name` (string) — One of the supported names listed above.
///
/// # Returns
/// A Lua table (array) of lightuserdata entity handles. Returns an empty table
/// for unknown component names (with a Warning log) or if the World is not set.
static int SceneGetEntitiesWith(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.get_entities_with: World not set");
        lua_newtable(L);
        return 1;
    }

    const char* name = luaL_checkstring(L, 1);

    lua_newtable(L);  // result array
    lua_Integer index = 1;

    auto pushEntity = [&](entt::entity enttEntity) {
        const Entity entity = world->EntityFromEntt(enttEntity);
        lua_pushlightuserdata(L, EntityToUserdata(entity));
        lua_rawseti(L, -2, index++);
    };

    if (std::strcmp(name, "transform") == 0) {
        for (auto [e, c] : world->View<SceneComponent>().each())              { (void)c; pushEntity(e); }
    } else if (std::strcmp(name, "camera") == 0) {
        for (auto [e, c] : world->View<CameraComponent>().each())             { (void)c; pushEntity(e); }
    } else if (std::strcmp(name, "light") == 0) {
        // Collect entities with any light type. Use a view per type; an entity
        // may theoretically have more than one but we push each entt entity once
        // by iterating each light storage. In the MVP, entities carry at most
        // one light type, so duplicate entries are not expected.
        for (auto [e, c] : world->View<DirectionalLightComponent>().each())   { (void)c; pushEntity(e); }
        for (auto [e, c] : world->View<PointLightComponent>().each())          { (void)c; pushEntity(e); }
        for (auto [e, c] : world->View<SpotLightComponent>().each())           { (void)c; pushEntity(e); }
    } else if (std::strcmp(name, "mesh") == 0) {
        for (auto [e, c] : world->View<StaticMeshComponent>().each())         { (void)c; pushEntity(e); }
    } else if (std::strcmp(name, "script") == 0) {
        for (auto [e, c] : world->View<CScript>().each())                     { (void)c; pushEntity(e); }
    } else if (std::strcmp(name, "name") == 0) {
        for (auto [e, c] : world->View<CName>().each())                       { (void)c; pushEntity(e); }
    } else {
        UZ_LOG(Scripting, Warning,
            "uz.scene.get_entities_with: unknown component name '{}'. "
            "Supported: transform, camera, light, mesh, script, name",
            name);
    }

    return 1;
}

/// uz.scene.clear() -> void
///
/// Destroys all entities and their components by calling `World::Clear()`.
/// All previously returned entity handles become stale after this call.
///
/// # Side Effects
/// Logs at Info level (including entity count) via the World implementation.
static int SceneClear(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.clear: World not set");
        return 0;
    }

    UZ_LOG(Scripting, Info, "uz.scene.clear: clearing all entities");
    world->Clear();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterEntityAPI(lua_State* L) {
    // Retrieve the existing uz table.
    lua_getglobal(L, "uz");                         // [uz]
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterEntityAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // Retrieve the existing uz.scene sub-table.
    lua_getfield(L, -1, "scene");                   // [uz, scene]
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterEntityAPI: 'uz.scene' is not a table — call LuaRegisterSceneAPI first");
        lua_pop(L, 2);
        return;
    }

    // Add entity lifecycle functions to the existing scene table.
    lua_pushcfunction(L, SceneCreateEntity);
    lua_setfield(L, -2, "create_entity");

    lua_pushcfunction(L, SceneDestroyEntity);
    lua_setfield(L, -2, "destroy_entity");

    lua_pushcfunction(L, SceneIsValid);
    lua_setfield(L, -2, "is_valid");

    lua_pushcfunction(L, SceneGetName);
    lua_setfield(L, -2, "get_name");

    lua_pushcfunction(L, SceneSetName);
    lua_setfield(L, -2, "set_name");

    lua_pushcfunction(L, SceneEntityCount);
    lua_setfield(L, -2, "entity_count");

    lua_pushcfunction(L, SceneHasComponent);
    lua_setfield(L, -2, "has_component");

    lua_pushcfunction(L, SceneRemoveComponent);
    lua_setfield(L, -2, "remove_component");

    lua_pushcfunction(L, SceneGetEntities);
    lua_setfield(L, -2, "get_entities");

    lua_pushcfunction(L, SceneGetEntitiesWith);
    lua_setfield(L, -2, "get_entities_with");

    lua_pushcfunction(L, SceneClear);
    lua_setfield(L, -2, "clear");

    lua_pop(L, 2);  // pop scene and uz

    UZ_LOG(Scripting, Info, "uz.scene entity API registered");
}

} // namespace uz
