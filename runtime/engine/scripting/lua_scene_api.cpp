#include "lua_scene_api.h"

#include "log.h"
#include "../ecs/world.h"
#include "../ecs/name_component.h"
#include "../ecs/scene_component.h"
#include "../ecs/static_mesh_component.h"
#include "../math/vector3.h"
#include "../math/quaternion.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cmath>
#include <cstring>

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
/// The null sentinel (id = UINT32_MAX) encodes cleanly to an all-high-bits value.
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

/// Tries to get the SceneComponent for an entity.
/// Returns nullptr and logs a warning if the world, entity, or component is missing.
[[nodiscard]] static SceneComponent* GetSceneComponent(
    lua_State* L, const char* funcName)
{
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.{}: World not set", funcName);
        return nullptr;
    }

    const Entity entity = CheckEntity(L, 1, funcName);
    if (!entity.IsValid() || !world->IsValid(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.{}: invalid or stale entity", funcName);
        return nullptr;
    }

    if (!world->HasComponent<SceneComponent>(entity)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.{}: entity {} has no SceneComponent", funcName, entity.id);
        return nullptr;
    }

    return &world->GetComponent<SceneComponent>(entity);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.scene.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.find(name) -> entity | nil
///
/// Iterates all entities with a CName component and returns the first whose
/// name matches `name` as a lightuserdata. Returns nil if not found.
static int SceneFind(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    if (!name || name[0] == '\0') {
        // Empty string search is never useful — return nil immediately.
        lua_pushnil(L);
        return 1;
    }

    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.find: World not set");
        lua_pushnil(L);
        return 1;
    }

    // Iterate CName view to find first match.
    // EnTT encodes generation in the upper bits of the entity integral value.
    // We recover it via `registry.current(enttEntity)` which returns the current
    // generation for that slot — matching what World::CreateEntity() stored.
    for (auto [enttEntity, cname] : world->View<CName>().each()) {
        if (cname.name == name) {
            // Extract the slot index using to_entity() (masks out version bits),
            // matching what World::CreateEntity stores in Entity::id.
            //
            // Generation mapping: World::CreateEntity increments generations_[slot]
            // starting from 0 BEFORE storing, so the first alive entity has generation=1.
            // EnTT's registry::current() returns the version which starts at 0 for the
            // first entity in a slot. Therefore: Entity::generation = entt_version + 1.
            const uint32_t slot = static_cast<uint32_t>(entt::to_entity(enttEntity));
            const uint32_t gen  = static_cast<uint32_t>(
                world->GetRegistry().current(enttEntity)) + 1u;
            Entity found;
            found.id         = slot;
            found.generation = gen;
            lua_pushlightuserdata(L, EntityToUserdata(found));
            return 1;
        }
    }

    lua_pushnil(L);
    return 1;
}

/// uz.scene.get_position(entity) -> x, y, z | nil
static int SceneGetPosition(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "get_position");
    if (!sc) {
        lua_pushnil(L);
        return 1;
    }
    const Vec3& pos = sc->GetPosition();
    lua_pushnumber(L, static_cast<double>(pos.x));
    lua_pushnumber(L, static_cast<double>(pos.y));
    lua_pushnumber(L, static_cast<double>(pos.z));
    return 3;
}

/// uz.scene.set_position(entity, x, y, z)
static int SceneSetPosition(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "set_position");
    if (!sc) return 0;

    const float x = static_cast<float>(luaL_checknumber(L, 2));
    const float y = static_cast<float>(luaL_checknumber(L, 3));
    const float z = static_cast<float>(luaL_checknumber(L, 4));
    sc->SetPosition({x, y, z});
    return 0;
}

/// uz.scene.get_rotation(entity) -> pitch, yaw, roll (degrees) | nil
static int SceneGetRotation(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "get_rotation");
    if (!sc) {
        lua_pushnil(L);
        return 1;
    }
    // get_euler() returns Vec3(pitch, yaw, roll) in radians.
    const Vec3 eulerRad = sc->GetRotation().get_euler();
    constexpr float kRadToDeg = 180.f / 3.14159265358979323846f;
    lua_pushnumber(L, static_cast<double>(eulerRad.x * kRadToDeg));
    lua_pushnumber(L, static_cast<double>(eulerRad.y * kRadToDeg));
    lua_pushnumber(L, static_cast<double>(eulerRad.z * kRadToDeg));
    return 3;
}

/// uz.scene.set_rotation(entity, pitch, yaw, roll) — degrees
static int SceneSetRotation(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "set_rotation");
    if (!sc) return 0;

    constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
    const float pitch = static_cast<float>(luaL_checknumber(L, 2)) * kDegToRad;
    const float yaw   = static_cast<float>(luaL_checknumber(L, 3)) * kDegToRad;
    const float roll  = static_cast<float>(luaL_checknumber(L, 4)) * kDegToRad;
    sc->SetRotation(Quat::FromEuler(pitch, yaw, roll));
    return 0;
}

/// uz.scene.get_scale(entity) -> x, y, z | nil
static int SceneGetScale(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "get_scale");
    if (!sc) {
        lua_pushnil(L);
        return 1;
    }
    const Vec3& scale = sc->GetScale();
    lua_pushnumber(L, static_cast<double>(scale.x));
    lua_pushnumber(L, static_cast<double>(scale.y));
    lua_pushnumber(L, static_cast<double>(scale.z));
    return 3;
}

/// uz.scene.set_scale(entity, x, y, z)
static int SceneSetScale(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "set_scale");
    if (!sc) return 0;

    const float x = static_cast<float>(luaL_checknumber(L, 2));
    const float y = static_cast<float>(luaL_checknumber(L, 3));
    const float z = static_cast<float>(luaL_checknumber(L, 4));
    sc->SetScale({x, y, z});
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — StaticMeshComponent access
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves or creates a StaticMeshComponent for the entity at stack index 1.
///
/// Returns nullptr and logs a warning if the world is not set or the entity is
/// invalid. Creates the component if the entity doesn't have one yet, which
/// matches the expected scripting ergonomics (call set_mesh on any entity).
[[nodiscard]] static StaticMeshComponent* GetOrAddMeshComponent(
    lua_State* L, const char* funcName)
{
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.{}: World not set", funcName);
        return nullptr;
    }

    const Entity entity = CheckEntity(L, 1, funcName);
    if (!entity.IsValid() || !world->IsValid(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.{}: invalid or stale entity", funcName);
        return nullptr;
    }

    if (!world->HasComponent<StaticMeshComponent>(entity)) {
        world->AddComponent<StaticMeshComponent>(entity);
    }
    return &world->GetComponent<StaticMeshComponent>(entity);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.scene.set_mesh / uz.scene.set_material
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.set_mesh(entity, mesh_id)
///
/// Assigns a mesh (by ID returned from `uz.mesh.load`) to an entity.
/// Creates a `StaticMeshComponent` on the entity if it doesn't have one.
/// Sets `meshLoaded = true` because the ID was obtained from `uz.mesh.load`
/// which already queued the GPU upload.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
/// * `mesh_id` (integer)      — Mesh ID from `uz.mesh.load`.
static int SceneSetMesh(lua_State* L) {
    StaticMeshComponent* meshComp = GetOrAddMeshComponent(L, "set_mesh");
    if (!meshComp) return 0;

    const lua_Integer meshId = luaL_checkinteger(L, 2);
    if (meshId < 0) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_mesh: invalid mesh_id {}", meshId);
        return 0;
    }

    meshComp->meshId     = static_cast<uint32_t>(meshId);
    meshComp->meshLoaded = true;   // GPU upload was already queued by uz.mesh.load
    return 0;
}

/// uz.scene.set_material(entity, material_table)
///
/// Assigns texture IDs from a material table to the entity's mesh component.
/// All fields are optional — omitted fields leave the existing value unchanged.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
/// * `material_table` (table) — Table with optional fields:
///   - `albedo`    (integer) — Texture ID from `uz.texture.load`.
///   - `normal`    (integer) — Texture ID from `uz.texture.load`.
///   - `roughness` (integer) — Texture ID from `uz.texture.load`.
///   - `metallic`  (integer) — Texture ID from `uz.texture.load`.
///   - `ao`        (integer) — Texture ID from `uz.texture.load`.
///
/// For MVP the albedo texture ID is also used as `materialId` so the draw
/// system can pick a material index without a full material asset system.
static int SceneSetMaterial(lua_State* L) {
    StaticMeshComponent* meshComp = GetOrAddMeshComponent(L, "set_material");
    if (!meshComp) return 0;

    if (!lua_istable(L, 2)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_material: argument 2 must be a table, got {}",
            lua_typename(L, lua_type(L, 2)));
        return 0;
    }

    // Helper: read an integer field from the table at stack index 2.
    // Returns UINT32_MAX if the field is absent or nil.
    auto readTexId = [&](const char* field) -> uint32_t {
        lua_getfield(L, 2, field);
        uint32_t id = UINT32_MAX;
        if (lua_isnumber(L, -1)) {
            const lua_Integer raw = lua_tointeger(L, -1);
            if (raw >= 0) {
                id = static_cast<uint32_t>(raw);
            }
        }
        lua_pop(L, 1);
        return id;
    };

    const uint32_t albedoId    = readTexId("albedo");
    const uint32_t normalId    = readTexId("normal");
    const uint32_t roughnessId = readTexId("roughness");
    const uint32_t metallicId  = readTexId("metallic");
    const uint32_t aoId        = readTexId("ao");

    // Apply only the fields that were provided (non-UINT32_MAX).
    if (albedoId    != UINT32_MAX) { meshComp->materialId = albedoId; }
    if (normalId    != UINT32_MAX) { (void)normalId;    /* future: normal tex slot */ }
    if (roughnessId != UINT32_MAX) { (void)roughnessId; /* future: roughness slot  */ }
    if (metallicId  != UINT32_MAX) { (void)metallicId;  /* future: metallic slot   */ }
    if (aoId        != UINT32_MAX) { (void)aoId;        /* future: ao slot         */ }

    meshComp->materialLoaded = true;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterSceneAPI(lua_State* L) {
    // Retrieve the existing uz table.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterSceneAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // Create the uz.scene sub-table.
    lua_newtable(L);

    lua_pushcfunction(L, SceneFind);
    lua_setfield(L, -2, "find");

    lua_pushcfunction(L, SceneGetPosition);
    lua_setfield(L, -2, "get_position");

    lua_pushcfunction(L, SceneSetPosition);
    lua_setfield(L, -2, "set_position");

    lua_pushcfunction(L, SceneGetRotation);
    lua_setfield(L, -2, "get_rotation");

    lua_pushcfunction(L, SceneSetRotation);
    lua_setfield(L, -2, "set_rotation");

    lua_pushcfunction(L, SceneGetScale);
    lua_setfield(L, -2, "get_scale");

    lua_pushcfunction(L, SceneSetScale);
    lua_setfield(L, -2, "set_scale");

    lua_pushcfunction(L, SceneSetMesh);
    lua_setfield(L, -2, "set_mesh");

    lua_pushcfunction(L, SceneSetMaterial);
    lua_setfield(L, -2, "set_material");

    // uz.scene = { ... }
    lua_setfield(L, -2, "scene");

    // Pop the uz table.
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.scene API registered");
}

void LuaSetWorld(lua_State* L, World* world) {
    lua_pushlightuserdata(L, static_cast<void*>(world));
    lua_setfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    UZ_LOG(Scripting, Debug, "LuaSetWorld: world={}", static_cast<void*>(world));
}

} // namespace uz
