/// scene_loader.cpp — Implementation of .uzscene JSON → World entity loading.
///
/// Parses JSON, recurses through the entity/children hierarchy, and emplaces
/// components into the World. Non-copyable components (SceneComponent and its
/// subclasses) are added via World::AddComponent<T>(entity) with no constructor
/// arguments, relying on in-place construction inside EnTT's sparse set.
/// Fields are then written through the returned mutable reference.

#include "scene_loader.h"

#include "component_serializer.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "skeletal_mesh_component.h"
#include "lod_group.h"
#include "camera_component.h"
#include "light_component.h"
#include "material/material_asset.h"
#include "../scripting/script_component.h"
#include "../logging/log.h"
#include "../core/cvar.h"
#include "../math/vector3.h"
#include "../math/quaternion.h"

#include "sky_atmosphere_component.h"
#include "sky_light_component.h"
#include "height_fog_component.h"
#include "reflection_probe_component.h"
#include "volumetric_clouds_component.h"

#include <json.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../asset/vfs.h"

namespace uz {

/// Thread-local VFS pointer set during LoadScene for .uzmaterial loading.
static IVFS* s_loadVfs = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Degrees-to-radians conversion factor.
static constexpr float kDegToRad = 3.14159265358979323846f / 180.f;

// ─────────────────────────────────────────────────────────────────────────────
// Scene-level material storage
// ─────────────────────────────────────────────────────────────────────────────

/// Materials created by the scene loader for entities that define a "material"
/// component in JSON. These are owned here and referenced by StaticMeshComponent
/// via non-owning pointers (overrideMaterials). Cleared when a new scene is loaded.
static std::vector<std::unique_ptr<Material>> s_sceneLoadedMaterials;

/// CVar overrides applied by the currently-loaded scene.
/// Key = cvar name, value = original value before the scene overrode it.
/// Reverted on the next LoadScene() call so scene overrides do not leak
/// into default/global configuration.
static std::unordered_map<std::string, float> s_sceneCVarOverrides;

/// Reverts all CVars previously overridden by a loaded scene to their
/// pre-override values, then clears the override map.
static void RevertSceneCVarOverrides() {
    auto& reg = CVarRegistry::Get();
    for (const auto& [name, prevValue] : s_sceneCVarOverrides) {
        reg.Set(name, prevValue);
    }
    s_sceneCVarOverrides.clear();
}

/// Parses a top-level "cvars" JSON object and applies each entry as a CVar
/// override. Records the previous value of each touched CVar in
/// s_sceneCVarOverrides so RevertSceneCVarOverrides() can restore them later.
///
/// Expected JSON shape:
///   "cvars": {
///     "r.PathTracing": 1,
///     "r.ScreenPercentage": 100.0,
///     "r.TAA": false
///   }
///
/// Values may be number or bool. Unknown CVars are logged and skipped
/// (Set() on an unregistered name is already a no-op, but we warn so
/// typos in scene files are visible).
static void ApplySceneCVarOverrides(const nlohmann::json& cvarsJson,
                                    const std::string& sourceName) {
    if (!cvarsJson.is_object()) {
        UZ_LOG(Scene, Warning,
               "LoadScene: '{}': 'cvars' is not an object, ignoring", sourceName);
        return;
    }
    auto& reg = CVarRegistry::Get();
    for (auto it = cvarsJson.begin(); it != cvarsJson.end(); ++it) {
        const std::string& name = it.key();
        float newValue = 0.f;
        if (it.value().is_number()) {
            newValue = it.value().get<float>();
        } else if (it.value().is_boolean()) {
            newValue = it.value().get<bool>() ? 1.f : 0.f;
        } else {
            UZ_LOG(Scene, Warning,
                   "LoadScene: '{}': cvar '{}' has unsupported value type, skipping",
                   sourceName, name);
            continue;
        }
        const CVar* existing = reg.Find(name);
        if (!existing) {
            UZ_LOG(Scene, Warning,
                   "LoadScene: '{}': cvar '{}' is not registered, skipping",
                   sourceName, name);
            continue;
        }
        // Remember the original value (first time we touch this cvar in this scene).
        if (s_sceneCVarOverrides.find(name) == s_sceneCVarOverrides.end()) {
            s_sceneCVarOverrides[name] = existing->value;
        }
        reg.Set(name, newValue);
        UZ_LOG(Scene, Info,
               "LoadScene: '{}': cvar override {} = {}",
               sourceName, name, newValue);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Reads a 3-element float array from a JSON value into x/y/z.
///
/// Returns `{defaultX, defaultY, defaultZ}` if the key is absent, not an array,
/// or has fewer than 3 elements. Extra elements beyond index 2 are ignored.
[[nodiscard]] Vec3 ReadVec3(const nlohmann::json& obj,
                             const char* key,
                             float defaultX,
                             float defaultY,
                             float defaultZ) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_array() || it->size() < 3) {
        return Vec3{defaultX, defaultY, defaultZ};
    }
    const auto& arr = *it;
    return Vec3{
        arr[0].is_number() ? arr[0].get<float>() : defaultX,
        arr[1].is_number() ? arr[1].get<float>() : defaultY,
        arr[2].is_number() ? arr[2].get<float>() : defaultZ,
    };
}

/// Reads a float field, returning `defaultValue` when the key is absent or not a number.
[[nodiscard]] float ReadFloat(const nlohmann::json& obj, const char* key, float defaultValue) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) {
        return defaultValue;
    }
    return it->get<float>();
}

/// Reads a boolean field, returning `defaultValue` when the key is absent or not a boolean.
[[nodiscard]] bool ReadBool(const nlohmann::json& obj, const char* key, bool defaultValue) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) {
        return defaultValue;
    }
    return it->get<bool>();
}

/// Reads a string field, returning an empty string when the key is absent or not a string.
[[nodiscard]] std::string ReadString(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Component parsers
// ─────────────────────────────────────────────────────────────────────────────

/// Parses the `"transform"` component JSON and emplaces a `SceneComponent`.
///
/// Rotation is given in Euler degrees (pitch, yaw, roll) and converted to a
/// quaternion via `Quat::FromEuler` with radians.
void ParseTransformComponent(World& world,
                              Entity entity,
                              const nlohmann::json& comp) {
    const Vec3 position = ReadVec3(comp, "position", 0.f, 0.f, 0.f);
    const Vec3 rotDeg   = ReadVec3(comp, "rotation", 0.f, 0.f, 0.f);
    const Vec3 scale    = ReadVec3(comp, "scale",    1.f, 1.f, 1.f);

    // SceneComponent is non-copyable (Component base deletes copy), so we use
    // registry.emplace directly instead of World::AddComponent (which uses emplace_or_replace).
    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
    auto& sc = world.GetRegistry().emplace<SceneComponent>(enttEntity);
    sc.SetPosition(position);
    sc.SetRotation(Quat::FromEuler(rotDeg.x * kDegToRad,
                                   rotDeg.y * kDegToRad,
                                   rotDeg.z * kDegToRad));
    sc.SetScale(scale);
}

/// Parses the `"script"` component JSON and emplaces a `CScript`.
void ParseScriptComponent(World& world,
                           Entity entity,
                           const nlohmann::json& comp) {
    const std::string scriptPath = ReadString(comp, "path");
    if (scriptPath.empty()) {
        UZ_LOG(Scene, Warning,
               "LoadScene: 'script' component has no 'path' field — entity {:d} skipped",
               entity.id);
        return;
    }
    // CScript is plain data (no Component base), so AddComponent works directly.
    world.AddComponent<CScript>(entity, CScript{.scriptPath = scriptPath});
}

/// Parses the `"camera"` component JSON and emplaces a `CameraComponent`.
///
/// If a `SceneComponent` (from "transform") already exists on this entity,
/// its position/rotation/scale are copied into the `CameraComponent` (which
/// inherits from `SceneComponent`), and the standalone `SceneComponent` is
/// removed so that the renderer reads transform data from the camera directly.
void ParseCameraComponent(World& world,
                           Entity entity,
                           const nlohmann::json& comp) {
    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
    auto& registry = world.GetRegistry();

    // Capture transform from existing SceneComponent before emplacing camera.
    Vec3 pos   = {0.f, 0.f, 0.f};
    Quat rot   = Quat::Identity();
    Vec3 scale = {1.f, 1.f, 1.f};
    bool hasTransform = false;

    if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
        pos   = sc->GetPosition();
        rot   = sc->GetRotation();
        scale = sc->GetScale();
        hasTransform = true;
    }

    // CameraComponent is non-copyable — use registry.emplace directly.
    auto& cam = registry.emplace<CameraComponent>(enttEntity);
    cam.fov       = ReadFloat(comp, "fov",    60.f);
    cam.nearPlane = comp.contains("near_clip") ? ReadFloat(comp, "near_clip", 0.1f)
                                               : ReadFloat(comp, "near",      0.1f);
    cam.farPlane  = comp.contains("far_clip")  ? ReadFloat(comp, "far_clip", 1000.f)
                                               : ReadFloat(comp, "far",      1000.f);
    cam.isActive  = ReadBool (comp, "active", true);

    // Copy transform into the CameraComponent's SceneComponent base.
    if (hasTransform) {
        cam.SetPosition(pos);
        cam.SetRotation(rot);
        cam.SetScale(scale);
        // Remove standalone SceneComponent — CameraComponent IS a SceneComponent.
        registry.erase<SceneComponent>(enttEntity);
    }

    // Optional look_at target — bypasses quaternion rotation entirely.
    // Avoids Euler-to-quaternion convention mismatches with the fly controller.
    if (comp.contains("look_at")) {
        cam.lookTarget = ReadVec3(comp, "look_at", 0.f, 0.f, 0.f);
        cam.hasLookTarget = true;
    }
}

/// Returns the first present key between a snake_case and a camelCase alias,
/// or nullptr when neither exists. Scene files use snake_case; engine tooling
/// may emit camelCase.
[[nodiscard]] const char* FindLightKey(const nlohmann::json& comp,
                                       const char* snake,
                                       const char* camel) {
    if (comp.contains(snake)) return snake;
    if (camel != nullptr && comp.contains(camel)) return camel;
    return nullptr;
}

/// Parses a `"directional"` light sub-object into the given component.
///
/// Besides color/intensity, accepts the optional directional fields
/// (snake_case on-disk contract, camelCase aliases):
/// `atmosphere_sun_light`, `light_source_angle` (degrees, full angular
/// diameter), `shadow_cascades`, `cascade_distribution_exponent`, and
/// `dynamic_shadow_distance`. Absent fields keep component defaults.
/// Unknown keys emit a warning (likely a typo that would silently no-op).
///
/// These were previously dropped on load: Sponza authors
/// `light_source_angle: 0.53` (the sun's real angular diameter) and silently
/// got the 1.2 degree component default, so every PCSS penumbra was more than
/// twice as wide as authored.
void ApplyDirectionalLightFields(DirectionalLightComponent& light,
                                  const nlohmann::json& comp) {
    light.color     = ReadVec3(comp, "color", 1.f, 1.f, 1.f);
    light.intensity = ReadFloat(comp, "intensity", 1.f);
    // Directional lights have no range or cone angle fields.

    if (const char* k = FindLightKey(comp, "atmosphere_sun_light", "atmosphereSunLight")) {
        light.atmosphereSunLight = ReadBool(comp, k, light.atmosphereSunLight);
    }
    if (const char* k = FindLightKey(comp, "light_source_angle", "lightSourceAngle")) {
        light.lightSourceAngle = ReadFloat(comp, k, light.lightSourceAngle);
    }
    if (const char* k = FindLightKey(comp, "shadow_cascades", "shadowCascades")) {
        light.shadowCascades =
            static_cast<int>(ReadFloat(comp, k, static_cast<float>(light.shadowCascades)));
    }
    if (const char* k = FindLightKey(comp, "cascade_distribution_exponent",
                                     "cascadeDistributionExponent")) {
        light.cascadeDistributionExponent =
            ReadFloat(comp, k, light.cascadeDistributionExponent);
    }
    if (const char* k = FindLightKey(comp, "dynamic_shadow_distance",
                                     "dynamicShadowDistance")) {
        light.dynamicShadowDistance =
            ReadFloat(comp, k, light.dynamicShadowDistance);
    }

    // Warn on keys we do not consume — a typo here would silently no-op.
    static constexpr const char* kKnown[] = {
        "type", "color", "intensity",
        "cast_shadows", "castShadows",  // accepted; inert for directional (CSM always on)
        "atmosphere_sun_light", "atmosphereSunLight",
        "light_source_angle", "lightSourceAngle",
        "shadow_cascades", "shadowCascades",
        "cascade_distribution_exponent", "cascadeDistributionExponent",
        "dynamic_shadow_distance", "dynamicShadowDistance",
    };
    for (const auto& [key, value] : comp.items()) {
        bool known = false;
        for (const char* k : kKnown) {
            if (key == k) { known = true; break; }
        }
        if (!known) {
            UZ_LOG(Scene, Warning,
                   "LoadScene: directional light has unrecognized key '{}' — ignored",
                   key);
        }
    }
}

/// Parses a `"point"` light sub-object into the given component.
void ApplyPointLightFields(PointLightComponent& light,
                            const nlohmann::json& comp) {
    light.color            = ReadVec3(comp, "color", 1.f, 1.f, 1.f);
    light.intensity        = ReadFloat(comp, "intensity",  1.f);
    light.range            = ReadFloat(comp, "range",     10.f);
    light.castShadows      = ReadBool(comp, "castShadows", false);
    light.pointShadowBias  = ReadFloat(comp, "shadowBias", 0.5f);
    // Block B.7: the parameters the lighting shader now honours.
    light.useInverseSquaredFalloff = ReadBool(comp, "useInverseSquaredFalloff", true);
    light.lightFalloffExponent     = ReadFloat(comp, "falloffExponent", 8.f);
    light.sourceRadius             = ReadFloat(comp, "sourceRadius", 0.f);
    light.specularScale            = ReadFloat(comp, "specularScale", 1.f);
    light.diffuseScale             = ReadFloat(comp, "diffuseScale", 1.f);
}

/// Parses a `"spot"` light sub-object into the given component.
///
/// `innerAngle` and `outerAngle` are expected in degrees; stored as radians.
void ApplySpotLightFields(SpotLightComponent& light,
                           const nlohmann::json& comp) {
    light.color         = ReadVec3(comp, "color", 1.f, 1.f, 1.f);
    light.intensity     = ReadFloat(comp, "intensity",    1.f);
    light.range         = ReadFloat(comp, "range",       10.f);
    light.innerAngleDeg = ReadFloat(comp, "innerAngle", 30.f);
    light.outerAngleDeg = ReadFloat(comp, "outerAngle", 45.f);
    light.castShadows   = ReadBool(comp, "castShadows", false);
    light.spotShadowBias = ReadFloat(comp, "shadowBias", 0.5f);
    // Block B.7: the parameters the lighting shader now honours.
    light.useInverseSquaredFalloff = ReadBool(comp, "useInverseSquaredFalloff", true);
    light.lightFalloffExponent     = ReadFloat(comp, "falloffExponent", 8.f);
    light.specularScale            = ReadFloat(comp, "specularScale", 1.f);
    light.diffuseScale             = ReadFloat(comp, "diffuseScale", 1.f);
}

/// Parses the `"light"` component JSON and emplaces the appropriate light type.
///
/// Dispatches on the `"type"` string: "directional", "point", or "spot".
/// Logs a warning and skips if the type is missing or unrecognized.
void ParseLightComponent(World& world,
                          Entity entity,
                          const nlohmann::json& comp,
                          const std::string& sourceName) {
    const std::string lightType = ReadString(comp, "type");

    // Light components are non-copyable — use registry.emplace directly.
    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
    if (lightType == "directional") {
        auto& light = world.GetRegistry().emplace<DirectionalLightComponent>(enttEntity);
        ApplyDirectionalLightFields(light, comp);
    } else if (lightType == "point") {
        auto& light = world.GetRegistry().emplace<PointLightComponent>(enttEntity);
        ApplyPointLightFields(light, comp);
    } else if (lightType == "spot") {
        auto& light = world.GetRegistry().emplace<SpotLightComponent>(enttEntity);
        ApplySpotLightFields(light, comp);
    } else {
        UZ_LOG(Scene, Warning,
               "LoadScene: '{}': 'light' component has unknown or missing type '{}' — skipped",
               sourceName, lightType);
    }
}

/// Reads a float array from a JSON value into a C array.
///
/// Returns the number of elements actually read (up to `maxCount`).
/// Missing, non-array, or non-number elements are filled with `defaultVal`.
uint32_t ReadFloatArray(const nlohmann::json& obj,
                        const char* key,
                        float* out,
                        uint32_t maxCount,
                        float defaultVal) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) {
        for (uint32_t i = 0; i < maxCount; ++i) out[i] = defaultVal;
        return 0;
    }
    const auto& arr = *it;
    const uint32_t count = std::min(maxCount, static_cast<uint32_t>(arr.size()));
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = arr[i].is_number() ? arr[i].get<float>() : defaultVal;
    }
    for (uint32_t i = count; i < maxCount; ++i) {
        out[i] = defaultVal;
    }
    return count;
}

/// Parses the `"mesh"` component JSON and emplaces a `StaticMeshComponent`.
///
/// Requires an `"asset"` field with the VFS-relative mesh path. Skips with a
/// warning if the field is absent or empty.
///
/// # LOD Fields (optional)
/// - `"lod1"` .. `"lod7"`: VFS-relative paths to LOD meshes (progressively simpler).
/// - `"lodScreenSizes"`: Array of up to 8 screen size thresholds (LOD0..LOD7).
///   Overrides the auto-computed thresholds from `LODGroup::InitAutoThresholds()`.
/// - `"forcedLOD"`: Integer to force a specific LOD level (-1 = automatic).
/// - `"boundingSphereRadius"`: Override for LOD screen-size computation.
///
/// When any LOD path is present, `lodGroup.numLevels` is set to the highest
/// LOD index + 1, and auto-thresholds are applied for any levels without
/// explicit screen sizes.
void ParseMeshComponent(World& world,
                         Entity entity,
                         const nlohmann::json& comp) {
    const std::string asset = ReadString(comp, "asset");
    if (asset.empty()) {
        UZ_LOG(Scene, Warning,
               "LoadScene: 'mesh' component has no 'asset' field — skipped");
        return;
    }
    // StaticMeshComponent is non-copyable — use registry.emplace directly.
    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
    auto& mesh = world.GetRegistry().emplace<StaticMeshComponent>(enttEntity);
    mesh.meshPath = asset;

    // ── LOD mesh paths (LOD1..LOD7) ─────────────────────────────────────
    // Keys "lod1" through "lod7" map to lodMeshPaths[0..6].
    static constexpr const char* kLodKeys[] = {
        "lod1", "lod2", "lod3", "lod4", "lod5", "lod6", "lod7"
    };
    uint32_t highestLodWithPath = 0; // Highest LOD level that has a mesh path (0 = LOD0 only)
    for (uint32_t i = 0; i < kMaxLODs - 1; ++i) {
        mesh.lodMeshPaths[i] = ReadString(comp, kLodKeys[i]);
        if (!mesh.lodMeshPaths[i].empty()) {
            highestLodWithPath = i + 1; // lodMeshPaths[i] = LOD level i+1
        }
    }

    // If any LOD paths were specified, configure the LOD group.
    if (highestLodWithPath > 0) {
        const uint32_t numLevels = highestLodWithPath + 1;

        // Build mesh index array for InitAutoThresholds. All indices start at
        // 0 (LOD0 mesh) — the actual LOD mesh indices are assigned during
        // lazy-load in SyncWorldToDrawList when the meshes are uploaded to GPU.
        uint32_t meshIndices[kMaxLODs] = {};
        mesh.lodGroup.InitAutoThresholds(meshIndices, numLevels);

        // Override screen size thresholds if explicitly provided.
        float screenSizes[kMaxLODs] = {};
        const uint32_t readCount = ReadFloatArray(comp, "lodScreenSizes",
                                                   screenSizes, kMaxLODs, 0.0f);
        if (readCount > 0) {
            for (uint32_t i = 0; i < numLevels && i < readCount; ++i) {
                mesh.lodGroup.levels[i].screenSizeThreshold = screenSizes[i];
            }
        }
    }

    // ── Optional LOD overrides ──────────────────────────────────────────
    {
        auto forcedIt = comp.find("forcedLOD");
        if (forcedIt != comp.end() && forcedIt->is_number_integer()) {
            mesh.forcedLOD = forcedIt->get<int32_t>();
        }
    }
    mesh.boundingSphereRadius = ReadFloat(comp, "boundingSphereRadius", 0.0f);
}

/// Parses the `"skeletal_mesh"` component JSON.
///
/// Emplaces a `StaticMeshComponent` (so the rendering path works unchanged)
/// and a `SkeletalMeshComponent` holding animation metadata. The subsystem
/// finalises both components after scene load by loading the mesh/skeleton,
/// registering an output mesh, and binding the controller.
void ParseSkeletalMeshComponent(World& world,
                                 Entity entity,
                                 const nlohmann::json& comp) {
    const std::string meshAsset = ReadString(comp, "asset");
    if (meshAsset.empty()) {
        UZ_LOG(Scene, Warning,
               "LoadScene: 'skeletal_mesh' component has no 'asset' field — skipped");
        return;
    }

    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));

    // Rendering component — meshPath points to the skinned .uzmesh so the
    // scene renderer lazily loads it (including its skinnedVertexData).
    auto& mesh = world.GetRegistry().emplace<StaticMeshComponent>(enttEntity);
    mesh.meshPath = meshAsset;

    // Animation metadata.
    SkeletalMeshComponent skel;
    skel.meshPath        = meshAsset;
    skel.skeletonPath    = ReadString(comp, "skeleton");
    skel.initialClipName = ReadString(comp, "initialClip");

    auto clipsIt = comp.find("clips");
    if (clipsIt != comp.end() && clipsIt->is_array()) {
        for (const auto& item : *clipsIt) {
            if (item.is_string()) {
                skel.animationClipPaths.push_back(item.get<std::string>());
            }
        }
    }

    world.GetRegistry().emplace<SkeletalMeshComponent>(enttEntity, std::move(skel));
}

/// Parses the `"material"` component JSON and writes texture paths into an
/// existing `StaticMeshComponent`.
///
/// The entity MUST already have a `StaticMeshComponent` (i.e., "mesh" must
/// appear before "material" in the JSON). Skips with a warning otherwise.
void ParseMaterialComponent(World& world,
                              Entity entity,
                              const nlohmann::json& comp) {
    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
    if (!world.GetRegistry().all_of<StaticMeshComponent>(enttEntity)) {
        UZ_LOG(Scene, Warning,
               "LoadScene: 'material' component without 'mesh' — skipped");
        return;
    }
    auto& mesh = world.GetRegistry().get<StaticMeshComponent>(enttEntity);

    // ── .uzmaterial asset loading ────────────────────────────────────────
    // When "asset" field is present, load the .uzmaterial JSON file and
    // extract texture paths + properties from it instead of inline fields.
    const std::string matAssetPath = ReadString(comp, "asset");
    if (!matAssetPath.empty() && s_loadVfs) {
        auto matBytes = s_loadVfs->Read(matAssetPath);
        if (!matBytes.empty()) {
            try {
                auto matJson = nlohmann::json::parse(matBytes.begin(), matBytes.end());
                auto texIt = matJson.find("textures");
                auto propIt = matJson.find("properties");

                auto mat = std::make_unique<Material>();
                if (texIt != matJson.end() && texIt->is_object()) {
                    auto bc = texIt->find("base_color_map");
                    if (bc != texIt->end() && bc->is_string()) mat->albedoPath = bc->get<std::string>();
                    auto nm = texIt->find("normal_map");
                    if (nm != texIt->end() && nm->is_string()) mat->normalPath = nm->get<std::string>();
                    auto mr = texIt->find("metallic_roughness_map");
                    if (mr != texIt->end() && mr->is_string()) {
                        mat->ormPath = mr->get<std::string>();
                        mat->useORM = true;
                    }
                    auto rm = texIt->find("roughness_map");
                    if (rm != texIt->end() && rm->is_string()) mat->roughnessPath = rm->get<std::string>();
                    auto mm = texIt->find("metallic_map");
                    if (mm != texIt->end() && mm->is_string()) mat->metallicPath = mm->get<std::string>();
                    auto ao = texIt->find("ao_map");
                    if (ao != texIt->end() && ao->is_string()) mat->aoPath = ao->get<std::string>();
                }
                if (propIt != matJson.end() && propIt->is_object()) {
                    mat->roughnessMultiplier = ReadFloat(*propIt, "roughness", 1.0f);
                    mat->metallicMultiplier  = ReadFloat(*propIt, "metallic", 0.0f);
                    mat->normalIntensity     = ReadFloat(*propIt, "normal_strength", 1.0f);
                }
                mesh.SetMaterial(0, mat.get());
                s_sceneLoadedMaterials.push_back(std::move(mat));
                return;
            } catch (const nlohmann::json::parse_error& e) {
                UZ_LOG(Scene, Warning, "Failed to parse material '{}': {}", matAssetPath, e.what());
            }
        } else {
            UZ_LOG(Scene, Warning, "Failed to read material asset '{}'", matAssetPath);
        }
        // Fall through to inline material parsing if .uzmaterial loading fails
    }

    // Material graph path (.uzmat) — stored on the component for lazy-loading
    // by the renderer. The graph material will take precedence once the
    // MaterialPipeline compiles its PSO. Until then, the fixed-PBR textures
    // below serve as fallback so the mesh isn't invisible.
    const std::string matGraphPath = ReadString(comp, "material_graph");

    // Parsed .uzmat metadata (blendMode, twoSided, textures) to propagate to
    // the fixed Material so the GBuffer pass can route Masked/Translucent draws
    // and bind opacity textures even before the material graph PSO is compiled.
    std::string uzmatBlendMode;
    bool        uzmatTwoSided = false;
    float       uzmatAlphaClip = 0.3333f;

    // Fully-resolved texture paths parsed from the PBROutput pin graph. An
    // empty string means the pin is either unbound or the graph is malformed,
    // and the fixed-PBR fallback below will try to fill it from inline JSON.
    std::string uzmatAlbedoPath;
    std::string uzmatNormalPath;
    std::string uzmatRoughnessPath;
    std::string uzmatMetallicPath;
    std::string uzmatAoPath;
    std::string uzmatEmissivePath;
    std::string uzmatOrmPath;          ///< Non-empty = AO/R/M routed from one texture.
    bool        uzmatHasOrm = false;

    if (!matGraphPath.empty()) {
        mesh.materialGraphPath = matGraphPath;
        UZ_LOG(Scene, Info, "Material graph assigned: '{}'", matGraphPath);

        if (s_loadVfs) {
            auto matBytes = s_loadVfs->Read(matGraphPath);
            if (!matBytes.empty()) {
                try {
                    auto mj = nlohmann::json::parse(matBytes.begin(), matBytes.end());
                    uzmatBlendMode = mj.value("blendMode", "");
                    uzmatTwoSided  = mj.value("twoSided", false);

                    if (mj.contains("expressions") && mj["expressions"].is_array()) {
                        // Step 1 — index TextureSample expressions by id so we
                        // can resolve PBROutput inputs that reference them.
                        std::unordered_map<int64_t, std::string> texByExprId;
                        const nlohmann::json* pbrOutput = nullptr;

                        for (const auto& expr : mj["expressions"]) {
                            const std::string type = expr.value("type", "");
                            const int64_t id       = expr.value("id", int64_t{-1});

                            if ((type == "TextureSample" || type == "TextureParameter") && id >= 0) {
                                if (expr.contains("stringParams") && expr["stringParams"].is_object()) {
                                    const auto tp = expr["stringParams"].value("texturePath", "");
                                    if (!tp.empty()) texByExprId[id] = tp;
                                }
                            } else if (type == "PBROutput") {
                                pbrOutput = &expr;
                                if (expr.contains("floatParams") && expr["floatParams"].is_object()) {
                                    uzmatAlphaClip = expr["floatParams"].value("alphaClipThreshold", 0.3333f);
                                }
                            }
                        }

                        // Step 2 — walk the PBROutput input list, mapping each
                        // destination pin to the source texture's expression id.
                        struct PinRouting {
                            int64_t srcExpr = -1;
                            std::string srcPin; ///< "RGB", "R", "G", "B" or empty.
                        };
                        std::unordered_map<std::string, PinRouting> pinMap;

                        auto canonPin = [](const std::string& raw) {
                            std::string s = raw;
                            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            if (s == "basecolor")   return std::string("BaseColor");
                            if (s == "metallic")    return std::string("Metallic");
                            if (s == "roughness")   return std::string("Roughness");
                            if (s == "normal")      return std::string("Normal");
                            if (s == "emissive")    return std::string("Emissive");
                            if (s == "opacity")     return std::string("Opacity");
                            if (s == "ao" || s == "occlusion") return std::string("AO");
                            return std::string{};
                        };

                        if (pbrOutput && pbrOutput->contains("inputs") && (*pbrOutput)["inputs"].is_array()) {
                            for (const auto& in : (*pbrOutput)["inputs"]) {
                                std::string dst;
                                if (in.contains("dstPin") && in["dstPin"].is_string()) {
                                    dst = canonPin(in["dstPin"].get<std::string>());
                                } else if (in.contains("pin") && in["pin"].is_number_integer()) {
                                    // Numeric pin indices follow material_expression.cpp:
                                    // 0=BaseColor, 1=Metallic, 2=Roughness, 3=Normal,
                                    // 4=Emissive, 5=Opacity, 6=AO.
                                    static constexpr const char* kNames[] = {
                                        "BaseColor", "Metallic", "Roughness", "Normal",
                                        "Emissive",  "Opacity",  "AO" };
                                    const int pin = in["pin"].get<int>();
                                    if (pin >= 0 && pin < 7) dst = kNames[pin];
                                }
                                if (dst.empty()) continue;

                                PinRouting r;
                                r.srcExpr = in.value("srcExpr", int64_t{-1});
                                if (in.contains("srcPin")) {
                                    if (in["srcPin"].is_string()) {
                                        r.srcPin = in["srcPin"].get<std::string>();
                                    } else if (in["srcPin"].is_number_integer()) {
                                        // Numeric srcPin indices on TextureSample output:
                                        // 0 = RGBA, 1 = R, 2 = G, 3 = B, 4 = A.
                                        static constexpr const char* kSlots[] = {
                                            "RGBA", "R", "G", "B", "A" };
                                        const int sp = in["srcPin"].get<int>();
                                        if (sp >= 0 && sp < 5) r.srcPin = kSlots[sp];
                                    }
                                }
                                pinMap[dst] = r;
                            }
                        }

                        auto pinTexture = [&](const std::string& dst) -> std::string {
                            auto it = pinMap.find(dst);
                            if (it == pinMap.end()) return {};
                            auto texIt = texByExprId.find(it->second.srcExpr);
                            return (texIt == texByExprId.end()) ? std::string{} : texIt->second;
                        };

                        uzmatAlbedoPath   = pinTexture("BaseColor");
                        uzmatNormalPath   = pinTexture("Normal");
                        uzmatEmissivePath = pinTexture("Emissive");

                        // AO / Roughness / Metallic — detect the ORM convention
                        // (R→AO, G→Roughness, B→Metallic from a single texture).
                        const std::string aoTex = pinTexture("AO");
                        const std::string rgTex = pinTexture("Roughness");
                        const std::string mtTex = pinTexture("Metallic");
                        const bool sameTex =
                            !aoTex.empty() && aoTex == rgTex && rgTex == mtTex;

                        auto pinSrcPin = [&](const std::string& dst) {
                            auto it = pinMap.find(dst);
                            return (it == pinMap.end()) ? std::string{} : it->second.srcPin;
                        };

                        if (sameTex &&
                            pinSrcPin("AO")        == "R" &&
                            pinSrcPin("Roughness") == "G" &&
                            pinSrcPin("Metallic")  == "B") {
                            uzmatOrmPath  = aoTex;
                            uzmatHasOrm   = true;
                        } else {
                            uzmatAoPath        = aoTex;
                            uzmatRoughnessPath = rgTex;
                            uzmatMetallicPath  = mtTex;
                        }
                    }
                } catch (const nlohmann::json::parse_error& e) {
                    UZ_LOG(Scene, Warning, "Failed to parse material graph '{}': {}",
                           matGraphPath, e.what());
                }
            } else {
                UZ_LOG(Scene, Warning, "Failed to read material graph '{}'", matGraphPath);
            }
        }
    }

    // Fixed-PBR path: Create a Material asset owned by the scene loader and assign it to slot 0.
    // Also serves as texture-loading fallback when material_graph is set but not yet compiled.
    //
    // When a `.uzmat` graph was parsed above, its resolved texture paths take
    // precedence over inline JSON fields — the graph authoritatively describes
    // which texture feeds each PBR pin (including ORM R→AO routing).
    auto mat = std::make_unique<Material>();
    auto pickPath = [&](const std::string& graphPath, const char* inlineKey) {
        if (!graphPath.empty()) return graphPath;
        return ReadString(comp, inlineKey);
    };
    mat->albedoPath    = pickPath(uzmatAlbedoPath,   "albedo");
    mat->normalPath    = pickPath(uzmatNormalPath,   "normal");
    mat->roughnessPath = pickPath(uzmatRoughnessPath, "roughness");
    mat->metallicPath  = pickPath(uzmatMetallicPath, "metallic");
    mat->aoPath        = pickPath(uzmatAoPath,       "ao");
    mat->emissivePath  = pickPath(uzmatEmissivePath, "emissive");
    if (uzmatHasOrm) {
        mat->ormPath = uzmatOrmPath;
        mat->useORM  = true;
        // Clear individual R/M paths: they're now read from ORM.G / ORM.B.
        mat->roughnessPath.clear();
        mat->metallicPath.clear();
        mat->aoPath.clear();
    }
    mat->baseColorTintR      = ReadFloat(comp, "baseColorR", 1.0f);
    mat->baseColorTintG      = ReadFloat(comp, "baseColorG", 1.0f);
    mat->baseColorTintB      = ReadFloat(comp, "baseColorB", 1.0f);
    mat->roughnessMultiplier = ReadFloat(comp, "roughnessMultiplier", 1.0f);
    mat->metallicMultiplier  = ReadFloat(comp, "metallicMultiplier", 1.0f);
    mat->normalIntensity     = ReadFloat(comp, "normalIntensity", 1.0f);
    mat->aoIntensity         = ReadFloat(comp, "aoIntensity", 1.0f);
    mat->specular            = ReadFloat(comp, "specular", 0.5f);
    mat->transmission        = ReadFloat(comp, "transmission", 0.0f);
    mat->ior                 = ReadFloat(comp, "ior", 1.5f);
    mat->thicknessScale      = ReadFloat(comp, "thicknessScale", 0.5f);

    // Blend mode — auto-detect from transmission or explicit string.
    // Translucent materials are rendered in a separate forward pass after
    // deferred lighting, with access to the HDR scene color for refraction.
    {
        auto bm = ReadString(comp, "blendMode");
        if (bm == "Masked")           mat->blendMode = uz::EBlendMode::Masked;
        else if (bm == "Translucent") mat->blendMode = uz::EBlendMode::Translucent;
        else if (bm == "Additive")    mat->blendMode = uz::EBlendMode::Additive;
        else if (bm == "Modulate")    mat->blendMode = uz::EBlendMode::Modulate;
    }

    // Propagate .uzmat metadata to the fixed Material when inline fields are absent.
    // This ensures Masked/Translucent blend modes, twoSided, and opacity wiring work
    // even when the entity only specifies "material_graph" without inline overrides.
    if (mat->blendMode == uz::EBlendMode::Opaque && !uzmatBlendMode.empty()) {
        if (uzmatBlendMode == "Masked")           mat->blendMode = uz::EBlendMode::Masked;
        else if (uzmatBlendMode == "Translucent") mat->blendMode = uz::EBlendMode::Translucent;
        else if (uzmatBlendMode == "Additive")    mat->blendMode = uz::EBlendMode::Additive;
        else if (uzmatBlendMode == "Modulate")    mat->blendMode = uz::EBlendMode::Modulate;
    }
    if (uzmatTwoSided) {
        mat->twoSided = true;
    }
    // For Masked materials, enable alpha test. The GBuffer shader samples the
    // albedo texture's alpha channel as the mask source when no separate opacity
    // texture is bound (opacitySrvIndex == 0).
    if (mat->blendMode == uz::EBlendMode::Masked) {
        mat->alphaTest = true;
        // A threshold of 0 would never discard anything; use 0.3333 as default.
        mat->alphaClipThreshold = (uzmatAlphaClip > 0.0f) ? uzmatAlphaClip : 0.3333f;
        // Set albedo path from .uzmat if not already set inline, so the texture
        // gets loaded and its alpha channel is available for the mask.
        if (mat->albedoPath.empty() && !uzmatAlbedoPath.empty()) {
            mat->albedoPath = uzmatAlbedoPath;
        }
    }

    // Auto-promote to Translucent when transmission is set (glass materials).
    if (mat->transmission > 0.0f && mat->blendMode == uz::EBlendMode::Opaque) {
        mat->blendMode = uz::EBlendMode::Translucent;
    }

    // Shading model — auto-detect from transmission or explicit string
    if (mat->transmission > 0.0f) {
        mat->shadingModel = uz::EShadingModel::ThinTranslucent;
    }
    {
        auto sm = ReadString(comp, "shadingModel");
        if (sm == "Unlit")            mat->shadingModel = uz::EShadingModel::Unlit;
        else if (sm == "Subsurface")  mat->shadingModel = uz::EShadingModel::Subsurface;
        else if (sm == "ClearCoat")   mat->shadingModel = uz::EShadingModel::ClearCoat;
        else if (sm == "Cloth")       mat->shadingModel = uz::EShadingModel::Cloth;
        else if (sm == "ThinTranslucent") mat->shadingModel = uz::EShadingModel::ThinTranslucent;
    }
    mat->emissiveColorR    = ReadFloat(comp, "emissiveColorR", 0.0f);
    mat->emissiveColorG    = ReadFloat(comp, "emissiveColorG", 0.0f);
    mat->emissiveColorB    = ReadFloat(comp, "emissiveColorB", 0.0f);
    mat->emissiveIntensity = ReadFloat(comp, "emissiveIntensity", 0.0f);
    mat->ormPath           = ReadString(comp, "orm");
    if (!mat->ormPath.empty()) {
        mat->useORM = true;
    }

    mesh.SetMaterial(0, mat.get());
    s_sceneLoadedMaterials.push_back(std::move(mat));
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity creation (recursive)
// ─────────────────────────────────────────────────────────────────────────────

/// Processes a single entity JSON object, creating an entity and recursing into children.
///
/// # Returns
/// Number of entities successfully created (1 + all descendants).
size_t ProcessEntityJson(World& world,
                          const nlohmann::json& entityJson,
                          const std::string& sourceName) {
    if (!entityJson.is_object()) {
        UZ_LOG(Scene, Warning,
               "LoadScene: '{}': entity entry is not a JSON object — skipped",
               sourceName);
        return 0;
    }

    const Entity entity = world.CreateEntity();
    size_t entityCount  = 1;

    // ── Name ─────────────────────────────────────────────────────────────────
    const std::string name = ReadString(entityJson, "name");
    if (!name.empty()) {
        world.AddComponent<CName>(entity, CName{name});
    }

    // ── Components ────────────────────────────────────────────────────────────
    auto compIt = entityJson.find("components");
    if (compIt != entityJson.end() && compIt->is_object()) {
        // Three-pass dispatch:
        // Pass 0: "transform" first — other components (camera, mesh) depend on SceneComponent.
        // Pass 1: all components except "transform" and "material".
        // Pass 2: "material" last — requires StaticMeshComponent from pass 1.
        auto transformIt = compIt->find("transform");
        if (transformIt != compIt->end() && transformIt->is_object()) {
            ParseTransformComponent(world, entity, *transformIt);
        }

        // Pass 1: all components except "transform" and "material".
        for (const auto& [compKey, compValue] : compIt->items()) {
            if (!compValue.is_object()) {
                UZ_LOG(Scene, Warning,
                       "LoadScene: '{}': component '{}' value is not a JSON object — skipped",
                       sourceName, compKey);
                continue;
            }

            if (compKey == "material" || compKey == "transform") {
                continue; // transform already processed in pass 0, material deferred to pass 2
            } else if (compKey == "script") {
                ParseScriptComponent(world, entity, compValue);
            } else if (compKey == "camera") {
                ParseCameraComponent(world, entity, compValue);
            } else if (compKey == "mesh") {
                ParseMeshComponent(world, entity, compValue);
            } else if (compKey == "skeletal_mesh") {
                ParseSkeletalMeshComponent(world, entity, compValue);
            } else if (compKey == "light") {
                ParseLightComponent(world, entity, compValue, sourceName);
            } else if (!ComponentSerializerRegistry::Get().Deserialize(compKey, world, entity, &compValue)) {
                UZ_LOG(Scene, Warning,
                       "LoadScene: '{}': unknown component type '{}' — skipped",
                       sourceName, compKey);
            }
        }

        // Pass 2: "material" (requires StaticMeshComponent from pass 1).
        auto matIt = compIt->find("material");
        if (matIt != compIt->end() && matIt->is_object()) {
            ParseMaterialComponent(world, entity, *matIt);
        }
    }

    // ── Prefab instantiation ────────────────────────────────────────────────
    // When an entity has `"prefab": { "asset": "path.uzprefab" }`, load the
    // prefab JSON and process its children as children of this entity.
    auto prefabIt = entityJson.find("prefab");
    if (prefabIt != entityJson.end() && prefabIt->is_object()) {
        const std::string prefabPath = ReadString(*prefabIt, "asset");
        if (!prefabPath.empty() && s_loadVfs) {
            auto prefabBytes = s_loadVfs->Read(prefabPath);
            if (!prefabBytes.empty()) {
                try {
                    auto prefabJson = nlohmann::json::parse(prefabBytes.begin(), prefabBytes.end());

                    // Process prefab's own components (if any) on this entity
                    auto prefabCompIt = prefabJson.find("components");
                    if (prefabCompIt != prefabJson.end() && prefabCompIt->is_object()) {
                        for (const auto& [ck, cv] : prefabCompIt->items()) {
                            if (!cv.is_object() || ck == "transform") continue;
                            if (ck == "mesh") ParseMeshComponent(world, entity, cv);
                            else if (ck == "light") ParseLightComponent(world, entity, cv, sourceName);
                            else if (ck == "script") ParseScriptComponent(world, entity, cv);
                            else if (ck == "camera") ParseCameraComponent(world, entity, cv);
                        }
                        auto prefabMatIt = prefabCompIt->find("material");
                        if (prefabMatIt != prefabCompIt->end() && prefabMatIt->is_object())
                            ParseMaterialComponent(world, entity, *prefabMatIt);
                    }

                    // Process prefab's children as children of this entity
                    auto prefabChildrenIt = prefabJson.find("children");
                    if (prefabChildrenIt != prefabJson.end() && prefabChildrenIt->is_array()) {
                        for (const auto& child : *prefabChildrenIt) {
                            entityCount += ProcessEntityJson(world, child, sourceName);
                        }
                    }

                    UZ_LOG(Scene, Info, "Prefab instantiated: '{}' ({} children)",
                           prefabPath, prefabJson.value("children", nlohmann::json::array()).size());
                } catch (const nlohmann::json::parse_error& e) {
                    UZ_LOG(Scene, Warning, "Failed to parse prefab '{}': {}", prefabPath, e.what());
                }
            } else {
                UZ_LOG(Scene, Warning, "Failed to read prefab asset '{}'", prefabPath);
            }
        } else if (!prefabPath.empty()) {
            UZ_LOG(Scene, Warning, "Prefab '{}' requires VFS (use LoadScene with VFS overload)", prefabPath);
        }
    }

    // ── Children (recursive) ──────────────────────────────────────────────────
    auto childrenIt = entityJson.find("children");
    if (childrenIt != entityJson.end() && childrenIt->is_array()) {
        for (const auto& child : *childrenIt) {
            entityCount += ProcessEntityJson(world, child, sourceName);
        }
    }

    return entityCount;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// LoadScene — public entry point
// ─────────────────────────────────────────────────────────────────────────────

size_t LoadScene(World& world,
                 const std::vector<uint8_t>& sceneJson,
                 const std::string& sourceName) {
    // Release materials from any previously loaded scene.
    s_sceneLoadedMaterials.clear();

    // Revert any CVar overrides applied by a previously-loaded scene so the
    // new scene starts from global defaults before applying its own overrides.
    RevertSceneCVarOverrides();

    if (sceneJson.empty()) {
        UZ_LOG(Scene, Error, "LoadScene: '{}': input byte buffer is empty", sourceName);
        return 0;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(sceneJson.begin(), sceneJson.end());
    } catch (const nlohmann::json::parse_error& ex) {
        UZ_LOG(Scene, Error,
               "LoadScene: '{}': JSON parse error at byte {}: {}",
               sourceName, ex.byte, ex.what());
        return 0;
    }

    if (!root.is_object()) {
        UZ_LOG(Scene, Error,
               "LoadScene: '{}': root element is not a JSON object", sourceName);
        return 0;
    }

    // Apply top-level "cvars" overrides (if any) BEFORE entity construction,
    // so components that read CVars during construction see the overridden values.
    auto cvarsIt = root.find("cvars");
    if (cvarsIt != root.end()) {
        ApplySceneCVarOverrides(*cvarsIt, sourceName);
    }

    auto entitiesIt = root.find("entities");
    if (entitiesIt == root.end() || !entitiesIt->is_array()) {
        UZ_LOG(Scene, Error,
               "LoadScene: '{}': missing or invalid 'entities' array", sourceName);
        return 0;
    }

    size_t totalCount = 0;
    for (const auto& entityJson : *entitiesIt) {
        totalCount += ProcessEntityJson(world, entityJson, sourceName);
    }

    UZ_LOG(Scene, Info,
           "LoadScene: '{}': loaded {} entities", sourceName, totalCount);
    return totalCount;
}

size_t LoadScene(World& world,
                 const std::vector<uint8_t>& sceneJson,
                 const std::string& sourceName,
                 IVFS* vfs) {
    s_loadVfs = vfs;
    size_t result = LoadScene(world, sceneJson, sourceName);
    s_loadVfs = nullptr;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveScene — public entry point
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Radians-to-degrees conversion factor.
static constexpr float kRadToDeg = 180.f / 3.14159265358979323846f;

/// Writes a Vec3 as a JSON array [x, y, z].
nlohmann::json WriteVec3(const Vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

/// Converts a quaternion to Euler angles (degrees) for serialization.
/// Returns [pitch, yaw, roll] matching the load convention.
nlohmann::json WriteEulerDeg(const Quat& q) {
    Vec3 euler = q.get_euler();
    return nlohmann::json::array({
        euler.x * kRadToDeg,
        euler.y * kRadToDeg,
        euler.z * kRadToDeg
    });
}

/// Serializes a single entity and its components to JSON.
nlohmann::json SerializeEntity(const World& world, entt::entity enttEntity) {
    const auto& registry = world.GetRegistry();
    nlohmann::json entityJson;

    // Name
    if (const auto* name = registry.try_get<CName>(enttEntity)) {
        entityJson["name"] = name->name;
    }

    nlohmann::json components;

    // Transform (SceneComponent or any subclass that provides position/rotation/scale)
    if (const auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
        nlohmann::json transform;
        transform["position"] = WriteVec3(sc->GetPosition());
        transform["rotation"] = WriteEulerDeg(sc->GetRotation());
        transform["scale"]    = WriteVec3(sc->GetScale());
        components["transform"] = std::move(transform);
    }

    // Camera (CameraComponent inherits from SceneComponent — transform is already above)
    if (const auto* cam = registry.try_get<CameraComponent>(enttEntity)) {
        nlohmann::json camera;
        camera["fov"]    = cam->fov;
        camera["near"]   = cam->nearPlane;
        camera["far"]    = cam->farPlane;
        camera["active"] = cam->isActive;
        components["camera"] = std::move(camera);

        // CameraComponent IS a SceneComponent, so write transform from it
        nlohmann::json transform;
        transform["position"] = WriteVec3(cam->GetPosition());
        transform["rotation"] = WriteEulerDeg(cam->GetRotation());
        transform["scale"]    = WriteVec3(cam->GetScale());
        components["transform"] = std::move(transform);
    }

    // Mesh
    if (const auto* mesh = registry.try_get<StaticMeshComponent>(enttEntity)) {
        if (!mesh->meshPath.empty()) {
            nlohmann::json meshJson;
            meshJson["asset"] = mesh->meshPath;

            // ── Serialize LOD mesh paths (LOD1..LOD7) ───────────────────
            static constexpr const char* kLodKeys[] = {
                "lod1", "lod2", "lod3", "lod4", "lod5", "lod6", "lod7"
            };
            for (uint32_t i = 0; i < kMaxLODs - 1; ++i) {
                if (!mesh->lodMeshPaths[i].empty()) {
                    meshJson[kLodKeys[i]] = mesh->lodMeshPaths[i];
                }
            }

            // Serialize LOD screen size thresholds if multiple levels exist.
            if (mesh->lodGroup.numLevels > 1) {
                nlohmann::json screenSizes = nlohmann::json::array();
                for (uint32_t i = 0; i < mesh->lodGroup.numLevels; ++i) {
                    screenSizes.push_back(mesh->lodGroup.levels[i].screenSizeThreshold);
                }
                meshJson["lodScreenSizes"] = std::move(screenSizes);
            }

            // Serialize forcedLOD if not automatic.
            if (mesh->forcedLOD >= 0) {
                meshJson["forcedLOD"] = mesh->forcedLOD;
            }

            // Serialize bounding sphere radius if overridden.
            if (mesh->boundingSphereRadius > 0.0f) {
                meshJson["boundingSphereRadius"] = mesh->boundingSphereRadius;
            }

            components["mesh"] = std::move(meshJson);
        }

        // Material (read from MaterialInterface at slot 0)
        const MaterialInterface* mat = mesh->GetMaterial(0);
        if (mat) {
            const auto& albedo    = mat->GetAlbedoPath();
            const auto& normal    = mat->GetNormalPath();
            const auto& roughness = mat->GetRoughnessPath();
            const auto& metallic  = mat->GetMetallicPath();
            const auto& ao        = mat->GetAoPath();
            const auto& emissive  = mat->GetEmissivePath();
            const float emIntensity = mat->GetEmissiveIntensity();

            bool hasMaterial = !albedo.empty() || !normal.empty()
                            || !roughness.empty() || !metallic.empty()
                            || !ao.empty() || !emissive.empty()
                            || emIntensity > 0.0f;
            if (hasMaterial) {
                nlohmann::json material;
                if (!albedo.empty())    material["albedo"]    = albedo;
                if (!normal.empty())    material["normal"]    = normal;
                if (!roughness.empty()) material["roughness"] = roughness;
                if (!metallic.empty())  material["metallic"]  = metallic;
                if (!ao.empty())        material["ao"]        = ao;
                if (!emissive.empty())  material["emissive"]  = emissive;
                if (emIntensity > 0.0f) {
                    float r, g, b;
                    mat->GetEmissiveColor(r, g, b);
                    material["emissiveColorR"]    = r;
                    material["emissiveColorG"]    = g;
                    material["emissiveColorB"]    = b;
                    material["emissiveIntensity"] = emIntensity;
                }
                components["material"] = std::move(material);
            }
        }

        // StaticMeshComponent also IS a SceneComponent, write its transform
        nlohmann::json transform;
        transform["position"] = WriteVec3(mesh->GetPosition());
        transform["rotation"] = WriteEulerDeg(mesh->GetRotation());
        transform["scale"]    = WriteVec3(mesh->GetScale());
        components["transform"] = std::move(transform);
    }

    // Directional Light
    if (const auto* light = registry.try_get<DirectionalLightComponent>(enttEntity)) {
        nlohmann::json lightJson;
        lightJson["type"]      = "directional";
        lightJson["color"]     = WriteVec3(light->color);
        lightJson["intensity"] = light->intensity;
        components["light"] = std::move(lightJson);

        nlohmann::json transform;
        transform["position"] = WriteVec3(light->GetPosition());
        transform["rotation"] = WriteEulerDeg(light->GetRotation());
        transform["scale"]    = WriteVec3(light->GetScale());
        components["transform"] = std::move(transform);
    }

    // Point Light
    if (const auto* light = registry.try_get<PointLightComponent>(enttEntity);
        light && !registry.all_of<SpotLightComponent>(enttEntity)) {
        nlohmann::json lightJson;
        lightJson["type"]        = "point";
        lightJson["color"]       = WriteVec3(light->color);
        lightJson["intensity"]   = light->intensity;
        lightJson["range"]       = light->range;
        lightJson["castShadows"] = light->castShadows;
        lightJson["shadowBias"]  = light->pointShadowBias;
        lightJson["useInverseSquaredFalloff"] = light->useInverseSquaredFalloff;
        lightJson["falloffExponent"] = light->lightFalloffExponent;
        lightJson["sourceRadius"] = light->sourceRadius;
        lightJson["specularScale"] = light->specularScale;
        lightJson["diffuseScale"] = light->diffuseScale;
        components["light"] = std::move(lightJson);

        nlohmann::json transform;
        transform["position"] = WriteVec3(light->GetPosition());
        transform["rotation"] = WriteEulerDeg(light->GetRotation());
        transform["scale"]    = WriteVec3(light->GetScale());
        components["transform"] = std::move(transform);
    }

    // Spot Light (inherits from PointLightComponent)
    if (const auto* light = registry.try_get<SpotLightComponent>(enttEntity)) {
        nlohmann::json lightJson;
        lightJson["type"]        = "spot";
        lightJson["color"]       = WriteVec3(light->color);
        lightJson["intensity"]   = light->intensity;
        lightJson["range"]       = light->range;
        lightJson["innerAngle"]  = light->innerAngleDeg;
        lightJson["outerAngle"]  = light->outerAngleDeg;
        lightJson["castShadows"] = light->castShadows;
        lightJson["shadowBias"]  = light->spotShadowBias;
        lightJson["useInverseSquaredFalloff"] = light->useInverseSquaredFalloff;
        lightJson["falloffExponent"] = light->lightFalloffExponent;
        lightJson["specularScale"] = light->specularScale;
        lightJson["diffuseScale"] = light->diffuseScale;
        components["light"] = std::move(lightJson);

        nlohmann::json transform;
        transform["position"] = WriteVec3(light->GetPosition());
        transform["rotation"] = WriteEulerDeg(light->GetRotation());
        transform["scale"]    = WriteVec3(light->GetScale());
        components["transform"] = std::move(transform);
    }

    // Script
    if (const auto* script = registry.try_get<CScript>(enttEntity)) {
        if (!script->scriptPath.empty()) {
            nlohmann::json scriptJson;
            scriptJson["path"] = script->scriptPath;
            components["script"] = std::move(scriptJson);
        }
    }

    // Registry-based serialization for self-registered component types.
    // This covers environment components (sky_atmosphere, sky_light, height_fog,
    // reflection_probe, volumetric_clouds, decal) and any future components that
    // register via UZ_REGISTER_COMPONENT_SERIALIZER.
    {
        const Entity entity = world.EntityFromEntt(enttEntity);
        const auto& entries = ComponentSerializerRegistry::Get().GetEntries();
        for (const auto& [key, entry] : entries) {
            nlohmann::json compJson;
            if (entry.serialize(world, entity, &compJson)) {
                components[key] = std::move(compJson);
            }
        }
    }

    if (!components.empty()) {
        entityJson["components"] = std::move(components);
    }

    entityJson["children"] = nlohmann::json::array();

    return entityJson;
}

} // namespace

std::vector<uint8_t> SaveScene(const World& world) {
    const auto& registry = world.GetRegistry();

    nlohmann::json root;
    nlohmann::json entities = nlohmann::json::array();

    const auto& entityStorage = *registry.storage<entt::entity>();
    for (auto it = entityStorage.begin(); it != entityStorage.end(); ++it) {
        const auto enttEntity = *it;
        if (!registry.valid(enttEntity)) continue;
        entities.push_back(SerializeEntity(world, enttEntity));
    }

    root["entities"] = std::move(entities);

    // Preserve any active scene CVar overrides so a load/save round-trip
    // does not drop the "cvars" section authored in the source .uzscene.
    if (!s_sceneCVarOverrides.empty()) {
        nlohmann::json cvarsJson = nlohmann::json::object();
        auto& reg = CVarRegistry::Get();
        for (const auto& kv : s_sceneCVarOverrides) {
            // Write the *current* (overridden) value, not the saved original.
            if (const CVar* cv = reg.Find(kv.first)) {
                cvarsJson[kv.first] = cv->value;
            }
        }
        root["cvars"] = std::move(cvarsJson);
    }

    const std::string jsonStr = root.dump(2);
    return {jsonStr.begin(), jsonStr.end()};
}

} // namespace uz
