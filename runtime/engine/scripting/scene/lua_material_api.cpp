#include "lua_material_api.h"

#include "log.h"
#include "world.h"
#include "static_mesh_component.h"
#include "material/material_asset.h"
#include "scene_renderer.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time guarantee: void* must be at least 64 bits to encode Entity.
// ─────────────────────────────────────────────────────────────────────────────

static_assert(sizeof(void*) >= 8,
    "uz::Entity requires 64-bit pointers for lightuserdata encoding");

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lua-created material storage
// ─────────────────────────────────────────────────────────────────────────────

/// Materials created by Lua scripts via `uz.scene.set_material()`. Owned here;
/// referenced by StaticMeshComponent via non-owning pointers. Cleared when the
/// Lua VM shuts down (process exit or script reload).
static std::vector<std::unique_ptr<Material>> s_luaCreatedMaterials;

// ─────────────────────────────────────────────────────────────────────────────
// Registry key constants
// ─────────────────────────────────────────────────────────────────────────────

/// Key used to store the World* in the Lua registry.
static constexpr const char* kWorldRegistryKey = "uz_world";

/// Key used to store the SceneRenderer* in the Lua registry (shared with lua_renderer_api.cpp).
static constexpr const char* kRendererRegistryKey = "uz_scene_renderer";

// ─────────────────────────────────────────────────────────────────────────────
// Entity <-> lightuserdata encoding helpers
// ─────────────────────────────────────────────────────────────────────────────

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
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the World* from the Lua registry. Returns null if not set.
[[nodiscard]] static World* GetWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<World*>(p);
}

/// Retrieves the SceneRenderer* from the Lua registry. Returns null if not set.
[[nodiscard]] static SceneRenderer* GetSceneRenderer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRendererRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<SceneRenderer*>(p);
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
/// Assigns texture IDs and material parameters to the entity's mesh component.
/// All fields are optional — omitted fields use Material defaults.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
/// * `material_table` (table) — Table with optional fields:
///   **Textures** (integer IDs from `uz.texture.load`):
///   - `albedo`, `normal`, `roughness`, `metallic`, `ao`, `emissive`
///   - `orm` (packed ORM: R=AO, G=Roughness, B=Metallic)
///   - `thickness` (SSS thickness map, R channel)
///
///   **PBR scalars** (float):
///   - `roughness_multiplier` (1.0), `metallic_multiplier` (1.0)
///   - `normal_intensity` (1.0), `ao_intensity` (1.0), `opacity` (1.0)
///   - `specular` (0.5 → F0=0.04), `env_multiplier` (1.0)
///   - `base_color_r/g/b` (1.0), `uv_scale_x/y` (1.0), `uv_offset_x/y` (0.0)
///   - `emissive_color_r/g/b` (0.0), `emissive_intensity` (0.0)
///
///   **Subsurface** (float):
///   - `subsurface_color_r` (1.0), `subsurface_color_g` (0.2), `subsurface_color_b` (0.1)
///   - `subsurface_radius` (1.0)
///
///   **Flags** (boolean):
///   - `two_sided` (false), `alpha_test` (false), `use_orm` (false)
///   - `alpha_clip_threshold` (0.3333, float)
///
///   **Enums** (string):
///   - `shading_model`: "unlit", "default_lit", "subsurface", "subsurface_profile",
///     "clear_coat", "two_sided_foliage", "hair", "cloth", "eye", "thin_translucent"
///   - `blend_mode`: "opaque", "masked", "translucent", "additive", "modulate"
///   - `material_domain`: "surface", "decal", "post_process", "light_function", "ui", "volume"
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
    const uint32_t emissiveId  = readTexId("emissive");
    const uint32_t ormId       = readTexId("orm");
    const uint32_t thicknessId = readTexId("thickness");

    // Read emissive color and intensity float fields (optional).
    auto readFloat = [&](const char* field, float defaultVal) -> float {
        lua_getfield(L, 2, field);
        float val = defaultVal;
        if (lua_isnumber(L, -1)) {
            val = static_cast<float>(lua_tonumber(L, -1));
        }
        lua_pop(L, 1);
        return val;
    };

    // Create a Material asset owned by the Lua material storage.
    auto mat = std::make_unique<Material>();

    // ── Material parameters (all optional, use Material defaults if absent) ──

    // Emissive
    mat->emissiveColorR    = readFloat("emissive_color_r", 0.0f);
    mat->emissiveColorG    = readFloat("emissive_color_g", 0.0f);
    mat->emissiveColorB    = readFloat("emissive_color_b", 0.0f);
    mat->emissiveIntensity = readFloat("emissive_intensity", 0.0f);

    // PBR multipliers
    mat->roughnessMultiplier = readFloat("roughness_multiplier", 1.0f);
    mat->metallicMultiplier  = readFloat("metallic_multiplier", 1.0f);
    mat->normalIntensity     = readFloat("normal_intensity", 1.0f);
    mat->aoIntensity         = readFloat("ao_intensity", 1.0f);
    mat->opacity             = readFloat("opacity", 1.0f);

    // Base color tint
    mat->baseColorTintR = readFloat("base_color_r", 1.0f);
    mat->baseColorTintG = readFloat("base_color_g", 1.0f);
    mat->baseColorTintB = readFloat("base_color_b", 1.0f);

    // UV transform
    mat->uvScaleX  = readFloat("uv_scale_x", 1.0f);
    mat->uvScaleY  = readFloat("uv_scale_y", 1.0f);
    mat->uvOffsetX = readFloat("uv_offset_x", 0.0f);
    mat->uvOffsetY = readFloat("uv_offset_y", 0.0f);

    // Specular & environment
    mat->specular      = readFloat("specular", 0.5f);
    mat->envMultiplier = readFloat("env_multiplier", 1.0f);

    // Subsurface scattering
    mat->subsurfaceColorR = readFloat("subsurface_color_r", 1.0f);
    mat->subsurfaceColorG = readFloat("subsurface_color_g", 0.2f);
    mat->subsurfaceColorB = readFloat("subsurface_color_b", 0.1f);
    mat->subsurfaceRadius = readFloat("subsurface_radius", 1.0f);

    // Rendering flags
    auto readBool = [&](const char* field, bool defaultVal) -> bool {
        lua_getfield(L, 2, field);
        bool val = defaultVal;
        if (lua_isboolean(L, -1)) val = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return val;
    };
    mat->twoSided           = readBool("two_sided", false);
    mat->alphaTest           = readBool("alpha_test", false);
    mat->alphaClipThreshold  = readFloat("alpha_clip_threshold", 0.3333f);
    mat->useORM              = readBool("use_orm", false);

    // Shading model (string → enum)
    auto readString = [&](const char* field) -> const char* {
        lua_getfield(L, 2, field);
        const char* val = nullptr;
        if (lua_isstring(L, -1)) val = lua_tostring(L, -1);
        lua_pop(L, 1);
        return val;
    };

    if (const char* sm = readString("shading_model")) {
        if      (strcmp(sm, "unlit") == 0)               mat->shadingModel = EShadingModel::Unlit;
        else if (strcmp(sm, "default_lit") == 0)          mat->shadingModel = EShadingModel::DefaultLit;
        else if (strcmp(sm, "subsurface") == 0)           mat->shadingModel = EShadingModel::Subsurface;
        else if (strcmp(sm, "subsurface_profile") == 0)   mat->shadingModel = EShadingModel::SubsurfaceProfile;
        else if (strcmp(sm, "clear_coat") == 0)           mat->shadingModel = EShadingModel::ClearCoat;
        else if (strcmp(sm, "two_sided_foliage") == 0)    mat->shadingModel = EShadingModel::TwoSidedFoliage;
        else if (strcmp(sm, "hair") == 0)                 mat->shadingModel = EShadingModel::Hair;
        else if (strcmp(sm, "cloth") == 0)                mat->shadingModel = EShadingModel::Cloth;
        else if (strcmp(sm, "eye") == 0)                  mat->shadingModel = EShadingModel::Eye;
        else if (strcmp(sm, "thin_translucent") == 0)     mat->shadingModel = EShadingModel::ThinTranslucent;
        else UZ_LOG(Scripting, Warning, "uz.scene.set_material: unknown shading_model '{}'", sm);
    }

    if (const char* bm = readString("blend_mode")) {
        if      (strcmp(bm, "opaque") == 0)      mat->blendMode = EBlendMode::Opaque;
        else if (strcmp(bm, "masked") == 0)       mat->blendMode = EBlendMode::Masked;
        else if (strcmp(bm, "translucent") == 0)  mat->blendMode = EBlendMode::Translucent;
        else if (strcmp(bm, "additive") == 0)     mat->blendMode = EBlendMode::Additive;
        else if (strcmp(bm, "modulate") == 0)     mat->blendMode = EBlendMode::Modulate;
        else UZ_LOG(Scripting, Warning, "uz.scene.set_material: unknown blend_mode '{}'", bm);
    }

    if (const char* md = readString("material_domain")) {
        if      (strcmp(md, "surface") == 0)        mat->materialDomain = EMaterialDomain::Surface;
        else if (strcmp(md, "decal") == 0)           mat->materialDomain = EMaterialDomain::Decal;
        else if (strcmp(md, "post_process") == 0)    mat->materialDomain = EMaterialDomain::PostProcess;
        else if (strcmp(md, "light_function") == 0)  mat->materialDomain = EMaterialDomain::LightFunction;
        else if (strcmp(md, "ui") == 0)              mat->materialDomain = EMaterialDomain::UI;
        else if (strcmp(md, "volume") == 0)          mat->materialDomain = EMaterialDomain::Volume;
        else UZ_LOG(Scripting, Warning, "uz.scene.set_material: unknown material_domain '{}'", md);
    }

    // Resolve texture IDs back to VFS paths and set them on the Material.
    // The renderer's SyncWorldToDrawList uses these paths to bind textures.
    SceneRenderer* renderer = GetSceneRenderer(L);

    auto resolveAndSet = [&](uint32_t texId, std::string& pathField) {
        if (texId == UINT32_MAX) return;
        if (!renderer) {
            UZ_LOG(Scripting, Warning,
                "uz.scene.set_material: SceneRenderer not available, cannot resolve texture ID {}", texId);
            return;
        }
        std::string path = renderer->GetTexturePath(texId);
        if (!path.empty()) {
            pathField = std::move(path);
        }
    };

    if (albedoId != UINT32_MAX) {
        meshComp->materialId = albedoId;
        resolveAndSet(albedoId, mat->albedoPath);
    }
    resolveAndSet(normalId,    mat->normalPath);
    resolveAndSet(roughnessId, mat->roughnessPath);
    resolveAndSet(metallicId,  mat->metallicPath);
    resolveAndSet(aoId,        mat->aoPath);
    resolveAndSet(emissiveId,  mat->emissivePath);
    resolveAndSet(ormId,       mat->ormPath);
    resolveAndSet(thicknessId, mat->thicknessPath);

    meshComp->SetMaterial(0, mat.get());
    s_luaCreatedMaterials.push_back(std::move(mat));

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
/// Returns a table with the entity's material parameters. Includes the albedo
/// texture ID (from `materialId`) and, if a `Material` is assigned via the
/// override material slot system, all scalar/boolean material parameters.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity.
///
/// # Returns
/// - Table with material parameters on success.
/// - `nil` if the component is missing or `materialLoaded` is false.
static int SceneGetMaterial(lua_State* L) {
    StaticMeshComponent* meshComp = GetMeshComponent(L, "get_material");
    if (!meshComp || !meshComp->materialLoaded) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    // materialId is used as the albedo texture ID in the MVP asset system.
    if (meshComp->materialId != 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(meshComp->materialId));
        lua_setfield(L, -2, "albedo");
    }

    // If a Material is assigned via the slot system, expose its parameters.
    const MaterialInterface* matIface = meshComp->GetMaterial(0);
    if (matIface) {
        const auto* mat = static_cast<const Material*>(matIface);

        auto pushFloat = [&](const char* name, float val) {
            lua_pushnumber(L, static_cast<double>(val));
            lua_setfield(L, -2, name);
        };

        pushFloat("roughness_multiplier", mat->roughnessMultiplier);
        pushFloat("metallic_multiplier", mat->metallicMultiplier);
        pushFloat("normal_intensity", mat->normalIntensity);
        pushFloat("ao_intensity", mat->aoIntensity);
        pushFloat("opacity", mat->opacity);
        pushFloat("specular", mat->specular);
        pushFloat("env_multiplier", mat->envMultiplier);
        pushFloat("emissive_color_r", mat->emissiveColorR);
        pushFloat("emissive_color_g", mat->emissiveColorG);
        pushFloat("emissive_color_b", mat->emissiveColorB);
        pushFloat("emissive_intensity", mat->emissiveIntensity);
        pushFloat("base_color_r", mat->baseColorTintR);
        pushFloat("base_color_g", mat->baseColorTintG);
        pushFloat("base_color_b", mat->baseColorTintB);
        pushFloat("uv_scale_x", mat->uvScaleX);
        pushFloat("uv_scale_y", mat->uvScaleY);
        pushFloat("uv_offset_x", mat->uvOffsetX);
        pushFloat("uv_offset_y", mat->uvOffsetY);

        // Subsurface scattering
        pushFloat("subsurface_color_r", mat->subsurfaceColorR);
        pushFloat("subsurface_color_g", mat->subsurfaceColorG);
        pushFloat("subsurface_color_b", mat->subsurfaceColorB);
        pushFloat("subsurface_radius", mat->subsurfaceRadius);

        // Flags
        lua_pushboolean(L, mat->twoSided ? 1 : 0);
        lua_setfield(L, -2, "two_sided");
        lua_pushboolean(L, mat->alphaTest ? 1 : 0);
        lua_setfield(L, -2, "alpha_test");
        lua_pushboolean(L, mat->useORM ? 1 : 0);
        lua_setfield(L, -2, "use_orm");
        pushFloat("alpha_clip_threshold", mat->alphaClipThreshold);

        // Enums as strings
        lua_pushstring(L, ShadingModelToString(mat->shadingModel));
        lua_setfield(L, -2, "shading_model");
        lua_pushstring(L, BlendModeToString(mat->blendMode));
        lua_setfield(L, -2, "blend_mode");
        lua_pushstring(L, MaterialDomainToString(mat->materialDomain));
        lua_setfield(L, -2, "material_domain");
    }

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterMaterialAPI(lua_State* L) {
    // Retrieve the existing uz table.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterMaterialAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // Retrieve the existing uz.scene sub-table.
    lua_getfield(L, -1, "scene");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterMaterialAPI: 'uz.scene' is not a table — call LuaRegisterSceneAPI first");
        lua_pop(L, 2);
        return;
    }

    // Add mesh/material functions to uz.scene.
    lua_pushcfunction(L, SceneSetMesh);
    lua_setfield(L, -2, "set_mesh");

    lua_pushcfunction(L, SceneSetMaterial);
    lua_setfield(L, -2, "set_material");

    lua_pushcfunction(L, SceneGetMesh);
    lua_setfield(L, -2, "get_mesh");

    lua_pushcfunction(L, SceneGetMaterial);
    lua_setfield(L, -2, "get_material");

    // Pop uz.scene and uz tables.
    lua_pop(L, 2);

    UZ_LOG(Scripting, Info, "uz.scene material/mesh API registered");
}

} // namespace uz
