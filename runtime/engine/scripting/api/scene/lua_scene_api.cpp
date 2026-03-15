#include "lua_scene_api.h"

#include "log.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "vector3.h"
#include "quaternion.h"
#include "scene_loader.h"
#include "vfs.h"

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

/// Key used to store the IVFS* in the Lua registry.
static constexpr const char* kVfsRegistryKey = "uz_vfs";

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

/// Retrieves the IVFS* from the Lua registry. Returns null if not set.
[[nodiscard]] static IVFS* GetVFS(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kVfsRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<IVFS*>(p);
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
// Lua C functions — uz.scene hierarchy
// ─────────────────────────────────────────────────────────────────────────────

/// Performs an O(N) reverse lookup to find the entity that owns `targetSc`.
///
/// Iterates all entities with a `SceneComponent` and compares their component
/// address against `targetSc`. Returns the matching `Entity` on success, or
/// `Entity::Invalid()` if no match is found. Acceptable for Phase 0; should be
/// replaced by a SceneComponent → Entity index in a later phase.
///
/// # Arguments
/// * `world`    - The ECS world to iterate.
/// * `targetSc` - Pointer to the SceneComponent whose owning entity is sought.
///
/// # Returns
/// The entity that owns `targetSc`, or `Entity::Invalid()` if not found.
[[nodiscard]] static Entity FindEntityBySceneComponent(
    World* world, const SceneComponent* targetSc)
{
    for (auto [enttEntity, sc] : world->View<SceneComponent>().each()) {
        if (&sc == targetSc) {
            const uint32_t slot = static_cast<uint32_t>(entt::to_entity(enttEntity));
            const uint32_t gen  = static_cast<uint32_t>(
                world->GetRegistry().current(enttEntity)) + 1u;
            Entity found;
            found.id         = slot;
            found.generation = gen;
            return found;
        }
    }
    return Entity::Invalid();
}

/// uz.scene.get_world_position(entity) -> x, y, z | nil
///
/// Returns the entity's world-space position computed from the full parent chain.
/// Returns nil if the entity has no `SceneComponent`.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
///
/// # Returns
/// Three numbers `x, y, z` on success, or `nil` if no `SceneComponent`.
static int SceneGetWorldPosition(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "get_world_position");
    if (!sc) {
        lua_pushnil(L);
        return 1;
    }
    const Vec3 pos = sc->GetWorldPosition();
    lua_pushnumber(L, static_cast<double>(pos.x));
    lua_pushnumber(L, static_cast<double>(pos.y));
    lua_pushnumber(L, static_cast<double>(pos.z));
    return 3;
}

/// uz.scene.get_world_matrix(entity) -> 16 numbers (row-major) | nil
///
/// Returns the entity's cached world-space transform matrix as 16 floats pushed
/// individually onto the Lua stack (row-major, DirectX convention: row 0 first).
/// Returns nil if the entity has no `SceneComponent`.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
///
/// # Returns
/// 16 numbers in row-major order on success, or `nil` if no `SceneComponent`.
static int SceneGetWorldMatrix(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "get_world_matrix");
    if (!sc) {
        lua_pushnil(L);
        return 1;
    }
    const Mat4& mat = sc->GetWorldMatrix();
    // XMFLOAT4X4 stores rows contiguously: _11,_12,_13,_14, _21,_22,...,_44.
    const float* data = &mat.m._11;
    for (int i = 0; i < 16; ++i) {
        lua_pushnumber(L, static_cast<double>(data[i]));
    }
    return 16;
}

/// uz.scene.attach_to(child_entity, parent_entity)
///
/// Attaches `child_entity` as a child of `parent_entity` in the scene hierarchy.
/// Both entities must have a `SceneComponent`. Logs a warning and returns
/// immediately if either entity is missing the component.
///
/// # Arguments
/// * `child_entity`  (lightuserdata) — The entity to be attached.
/// * `parent_entity` (lightuserdata) — The entity to attach to.
static int SceneAttachTo(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.attach_to: World not set");
        return 0;
    }

    // Read child entity from arg 1.
    const Entity childEntity = CheckEntity(L, 1, "attach_to");
    if (!childEntity.IsValid() || !world->IsValid(childEntity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.attach_to: invalid or stale child_entity");
        return 0;
    }

    // Read parent entity from arg 2.
    if (!lua_islightuserdata(L, 2)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.attach_to: argument 2 must be an entity (lightuserdata), got {}",
            lua_typename(L, lua_type(L, 2)));
        return 0;
    }
    const Entity parentEntity = UserdataToEntity(lua_touserdata(L, 2));
    if (!parentEntity.IsValid() || !world->IsValid(parentEntity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.attach_to: invalid or stale parent_entity");
        return 0;
    }

    if (!world->HasComponent<SceneComponent>(childEntity)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.attach_to: child entity {} has no SceneComponent", childEntity.id);
        return 0;
    }
    if (!world->HasComponent<SceneComponent>(parentEntity)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.attach_to: parent entity {} has no SceneComponent", parentEntity.id);
        return 0;
    }

    SceneComponent* childSc  = &world->GetComponent<SceneComponent>(childEntity);
    SceneComponent* parentSc = &world->GetComponent<SceneComponent>(parentEntity);
    childSc->AttachTo(parentSc);
    return 0;
}

/// uz.scene.detach(entity)
///
/// Detaches `entity` from its current parent in the scene hierarchy.
/// No-op if the entity has no `SceneComponent` or is already a root node.
///
/// # Arguments
/// * `entity` (lightuserdata) — The entity to detach.
static int SceneDetach(lua_State* L) {
    SceneComponent* sc = GetSceneComponent(L, "detach");
    if (!sc) return 0;   // No SceneComponent — silently ignore per spec.
    sc->Detach();
    return 0;
}

/// uz.scene.get_parent(entity) -> parent_entity | nil
///
/// Returns the entity handle of this entity's parent in the scene hierarchy.
/// Performs an O(N) reverse lookup over all entities with `SceneComponent` to
/// resolve the parent pointer back to an entity handle.
///
/// # Arguments
/// * `entity` (lightuserdata) — The entity whose parent is requested.
///
/// # Returns
/// A lightuserdata entity handle on success, or `nil` if this entity has no
/// parent, has no `SceneComponent`, or the parent cannot be resolved.
static int SceneGetParent(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.get_parent: World not set");
        lua_pushnil(L);
        return 1;
    }

    const Entity entity = CheckEntity(L, 1, "get_parent");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.get_parent: invalid or stale entity");
        lua_pushnil(L);
        return 1;
    }

    if (!world->HasComponent<SceneComponent>(entity)) {
        lua_pushnil(L);
        return 1;
    }

    const SceneComponent* sc        = &world->GetComponent<SceneComponent>(entity);
    const SceneComponent* parentPtr = sc->GetParent();
    if (!parentPtr) {
        lua_pushnil(L);
        return 1;
    }

    const Entity parentEntity = FindEntityBySceneComponent(world, parentPtr);
    if (!parentEntity.IsValid()) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.get_parent: parent SceneComponent* not found in world (dangling?)");
        lua_pushnil(L);
        return 1;
    }

    lua_pushlightuserdata(L, EntityToUserdata(parentEntity));
    return 1;
}

/// uz.scene.get_children(entity) -> array of entity handles
///
/// Returns a Lua array (table with integer keys starting at 1) of entity handles
/// for all direct children of `entity` in the scene hierarchy. Performs an O(N)
/// reverse lookup per child pointer to resolve each `SceneComponent*` to an entity.
///
/// # Arguments
/// * `entity` (lightuserdata) — The entity whose children are requested.
///
/// # Returns
/// A Lua table (array) of lightuserdata entity handles. Empty table if the entity
/// has no `SceneComponent`, has no children, or a child pointer cannot be resolved.
static int SceneGetChildren(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.get_children: World not set");
        lua_newtable(L);
        return 1;
    }

    const Entity entity = CheckEntity(L, 1, "get_children");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.get_children: invalid or stale entity");
        lua_newtable(L);
        return 1;
    }

    lua_newtable(L);   // result array

    if (!world->HasComponent<SceneComponent>(entity)) {
        return 1;      // Empty table — no hierarchy data.
    }

    const SceneComponent& sc       = world->GetComponent<SceneComponent>(entity);
    const auto&           children = sc.GetChildren();

    int arrayIndex = 1;
    for (const SceneComponent* childPtr : children) {
        if (!childPtr) continue;

        const Entity childEntity = FindEntityBySceneComponent(world, childPtr);
        if (!childEntity.IsValid()) {
            UZ_LOG(Scripting, Warning,
                "uz.scene.get_children: a child SceneComponent* could not be resolved — "
                "skipping (dangling pointer?)");
            continue;
        }

        lua_pushlightuserdata(L, EntityToUserdata(childEntity));
        lua_rawseti(L, -2, arrayIndex);
        ++arrayIndex;
    }

    return 1;
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
// Internal helpers — read-only StaticMeshComponent access
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the StaticMeshComponent for the entity at stack index 1 (read-only).
///
/// Returns nullptr without logging if the entity simply lacks the component —
/// callers distinguish "no component" (nil return) from errors (logged here).
[[nodiscard]] static StaticMeshComponent* GetMeshComponent(
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
        return nullptr;  // Not an error — caller pushes nil.
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

/// uz.scene.get_mesh(entity) -> mesh_id (integer) | nil
///
/// Returns the `meshId` from the entity's `StaticMeshComponent` when the mesh
/// has been uploaded to the GPU (i.e., `meshLoaded == true`). Returns nil if
/// the entity has no `StaticMeshComponent` or the mesh has not been loaded yet.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
///
/// # Returns
/// - Integer `mesh_id` on success.
/// - `nil` if the component is missing or `meshLoaded` is false.
static int SceneGetMesh(lua_State* L) {
    StaticMeshComponent* meshComp = GetMeshComponent(L, "get_mesh");
    if (!meshComp || !meshComp->meshLoaded) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(meshComp->meshId));
    return 1;
}

/// uz.scene.get_material(entity) -> table | nil
///
/// Returns a table of non-zero texture IDs from the entity's `StaticMeshComponent`
/// when the material has been loaded (`materialLoaded == true`). Only fields with
/// a non-zero ID are included in the returned table (albedo, normal, roughness,
/// metallic, ao). Returns nil if the component is missing or not yet loaded.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
///
/// # Returns
/// - Table `{albedo?, normal?, roughness?, metallic?, ao?}` on success.
///   Each field is an integer texture ID. Fields whose ID is 0 are omitted.
/// - `nil` if the component is missing or `materialLoaded` is false.
static int SceneGetMaterial(lua_State* L) {
    StaticMeshComponent* meshComp = GetMeshComponent(L, "get_material");
    if (!meshComp || !meshComp->materialLoaded) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    // Only emit non-zero IDs — zero is the sentinel "null" value.
    // materialId is used as the albedo ID in the MVP asset system.
    if (meshComp->materialId != 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(meshComp->materialId));
        lua_setfield(L, -2, "albedo");
    }

    // Phase 2 texture slots are not yet stored as uint32_t IDs in the component
    // (they are paths). Nothing else to emit until the asset system adds them.

    return 1;
}

/// uz.scene.set_visible(entity, enabled)
///
/// Sets the `visible` flag on the entity's `StaticMeshComponent`. Logs a warning
/// if the entity has no `StaticMeshComponent` — the component is NOT created
/// automatically, because visibility is meaningless without mesh geometry.
///
/// # Arguments
/// * `entity`  (lightuserdata) — Target entity.
/// * `enabled` (boolean)       — True to show, false to hide.
static int SceneSetVisible(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_visible: World not set");
        return 0;
    }

    const Entity entity = CheckEntity(L, 1, "set_visible");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        UZ_LOG(Scripting, Warning, "uz.scene.set_visible: invalid or stale entity");
        return 0;
    }

    if (!world->HasComponent<StaticMeshComponent>(entity)) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.set_visible: entity {} has no StaticMeshComponent — "
            "call uz.scene.set_mesh first",
            entity.id);
        return 0;
    }

    const bool enabled = lua_toboolean(L, 2) != 0;
    world->GetComponent<StaticMeshComponent>(entity).visible = enabled;
    return 0;
}

/// uz.scene.is_visible(entity) -> bool
///
/// Returns the `visible` flag from the entity's `StaticMeshComponent`.
/// Returns `true` (the default) when the entity has no `StaticMeshComponent`,
/// matching the "assume visible until told otherwise" convention.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
///
/// # Returns
/// - `true` if the entity is visible or has no mesh component.
/// - `false` if the entity's mesh component has `visible = false`.
static int SceneIsVisible(lua_State* L) {
    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.is_visible: World not set");
        lua_pushboolean(L, 1);  // Default visible.
        return 1;
    }

    const Entity entity = CheckEntity(L, 1, "is_visible");
    if (!entity.IsValid() || !world->IsValid(entity)) {
        lua_pushboolean(L, 1);  // Default visible for stale entities.
        return 1;
    }

    if (!world->HasComponent<StaticMeshComponent>(entity)) {
        lua_pushboolean(L, 1);  // No mesh component → default visible.
        return 1;
    }

    const bool vis = world->GetComponent<StaticMeshComponent>(entity).visible;
    lua_pushboolean(L, vis ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.scene scene load/save
// ─────────────────────────────────────────────────────────────────────────────

/// uz.scene.load_scene(path) -> bool
///
/// Destroys all existing entities in the world and loads a new scene from the
/// given VFS-relative path (e.g. `"scenes/demo.uzscene"`).
///
/// The file is read through the VFS registered via `LuaSetVFS`. The world is
/// cleared via `World::Clear()` before parsing begins, so partial failures
/// leave the world empty rather than in a mixed state.
///
/// # Arguments
/// * `path` (string) — VFS-relative path to the `.uzscene` file.
///
/// # Returns
/// - `true` if at least one entity was successfully created.
/// - `false` if the World or VFS is not set, the file is not found, or the
///   JSON is malformed (all failures are logged at Warning/Error level).
static int SceneLoadScene(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    World* world = GetWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.scene.load_scene: World not set");
        lua_pushboolean(L, 0);
        return 1;
    }

    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        UZ_LOG(Scripting, Warning, "uz.scene.load_scene: VFS not set");
        lua_pushboolean(L, 0);
        return 1;
    }

    UZ_LOG(Scripting, Info, "uz.scene.load_scene: loading '{}'", path);

    const std::vector<uint8_t> data = vfs->Read(path);
    if (data.empty()) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.load_scene: file not found or empty — path='{}'", path);
        lua_pushboolean(L, 0);
        return 1;
    }

    world->Clear();

    const size_t count = uz::LoadScene(*world, data, path);
    if (count == 0) {
        UZ_LOG(Scripting, Warning,
            "uz.scene.load_scene: no entities loaded from '{}'", path);
        lua_pushboolean(L, 0);
        return 1;
    }

    UZ_LOG(Scripting, Info,
        "uz.scene.load_scene: loaded {} entities from '{}'", count, path);
    lua_pushboolean(L, 1);
    return 1;
}

/// uz.scene.save_scene(path) -> bool
///
/// Placeholder — scene serialization is not yet implemented.
/// Always returns false and logs a warning.
///
/// # Arguments
/// * `path` (string) — VFS-relative destination path (ignored).
///
/// # Returns
/// Always `false`.
static int SceneSaveScene(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    UZ_LOG(Scripting, Warning,
        "uz.scene.save_scene: not yet implemented — path='{}'", path);
    lua_pushboolean(L, 0);
    return 1;
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

    lua_pushcfunction(L, SceneGetMesh);
    lua_setfield(L, -2, "get_mesh");

    lua_pushcfunction(L, SceneGetMaterial);
    lua_setfield(L, -2, "get_material");

    lua_pushcfunction(L, SceneSetVisible);
    lua_setfield(L, -2, "set_visible");

    lua_pushcfunction(L, SceneIsVisible);
    lua_setfield(L, -2, "is_visible");

    lua_pushcfunction(L, SceneGetWorldPosition);
    lua_setfield(L, -2, "get_world_position");

    lua_pushcfunction(L, SceneGetWorldMatrix);
    lua_setfield(L, -2, "get_world_matrix");

    lua_pushcfunction(L, SceneAttachTo);
    lua_setfield(L, -2, "attach_to");

    lua_pushcfunction(L, SceneDetach);
    lua_setfield(L, -2, "detach");

    lua_pushcfunction(L, SceneGetParent);
    lua_setfield(L, -2, "get_parent");

    lua_pushcfunction(L, SceneGetChildren);
    lua_setfield(L, -2, "get_children");

    lua_pushcfunction(L, SceneLoadScene);
    lua_setfield(L, -2, "load_scene");

    lua_pushcfunction(L, SceneSaveScene);
    lua_setfield(L, -2, "save_scene");

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

void LuaSetVFS(lua_State* L, IVFS* vfs) {
    lua_pushlightuserdata(L, static_cast<void*>(vfs));
    lua_setfield(L, LUA_REGISTRYINDEX, kVfsRegistryKey);
    UZ_LOG(Scripting, Debug, "LuaSetVFS: vfs={}", static_cast<void*>(vfs));
}

} // namespace uz
