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
#include "lod_group.h"
#include "camera_component.h"
#include "light_component.h"
#include "material/material_asset.h"
#include "../scripting/script_component.h"
#include "../logging/log.h"
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
#include <vector>

namespace uz {

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
    cam.nearPlane = ReadFloat(comp, "near",    0.1f);
    cam.farPlane  = ReadFloat(comp, "far",  1000.f);
    cam.isActive  = ReadBool (comp, "active", false);

    // Copy transform into the CameraComponent's SceneComponent base.
    if (hasTransform) {
        cam.SetPosition(pos);
        cam.SetRotation(rot);
        cam.SetScale(scale);
        // Remove standalone SceneComponent — CameraComponent IS a SceneComponent.
        registry.erase<SceneComponent>(enttEntity);
    }
}

/// Parses a `"directional"` light sub-object into the given component.
void ApplyDirectionalLightFields(DirectionalLightComponent& light,
                                  const nlohmann::json& comp) {
    light.color     = ReadVec3(comp, "color", 1.f, 1.f, 1.f);
    light.intensity = ReadFloat(comp, "intensity", 1.f);
    // Directional lights have no range or cone angle fields.
}

/// Parses a `"point"` light sub-object into the given component.
void ApplyPointLightFields(PointLightComponent& light,
                            const nlohmann::json& comp) {
    light.color            = ReadVec3(comp, "color", 1.f, 1.f, 1.f);
    light.intensity        = ReadFloat(comp, "intensity",  1.f);
    light.range            = ReadFloat(comp, "range",     10.f);
    light.castShadows      = ReadBool(comp, "castShadows", false);
    light.pointShadowBias  = ReadFloat(comp, "shadowBias", 0.5f);
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

    // Create a Material asset owned by the scene loader and assign it to slot 0.
    auto mat = std::make_unique<Material>();
    mat->albedoPath    = ReadString(comp, "albedo");
    mat->normalPath    = ReadString(comp, "normal");
    mat->roughnessPath = ReadString(comp, "roughness");
    mat->metallicPath  = ReadString(comp, "metallic");
    mat->aoPath        = ReadString(comp, "ao");
    mat->emissivePath  = ReadString(comp, "emissive");
    mat->roughnessMultiplier = ReadFloat(comp, "roughnessMultiplier", 1.0f);
    mat->metallicMultiplier  = ReadFloat(comp, "metallicMultiplier", 1.0f);
    mat->normalIntensity     = ReadFloat(comp, "normalIntensity", 1.0f);
    mat->aoIntensity         = ReadFloat(comp, "aoIntensity", 1.0f);
    mat->emissiveColorR    = ReadFloat(comp, "emissiveColorR", 0.0f);
    mat->emissiveColorG    = ReadFloat(comp, "emissiveColorG", 0.0f);
    mat->emissiveColorB    = ReadFloat(comp, "emissiveColorB", 0.0f);
    mat->emissiveIntensity = ReadFloat(comp, "emissiveIntensity", 0.0f);

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

    const std::string jsonStr = root.dump(2);
    return {jsonStr.begin(), jsonStr.end()};
}

} // namespace uz
