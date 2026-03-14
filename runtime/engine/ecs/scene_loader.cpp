/// scene_loader.cpp — Implementation of .uzscene JSON → World entity loading.
///
/// Parses JSON, recurses through the entity/children hierarchy, and emplaces
/// components into the World. Non-copyable components (SceneComponent and its
/// subclasses) are added via World::AddComponent<T>(entity) with no constructor
/// arguments, relying on in-place construction inside EnTT's sparse set.
/// Fields are then written through the returned mutable reference.

#include "scene_loader.h"

#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "camera_component.h"
#include "light_component.h"
#include "../scripting/script_component.h"
#include "../logging/log.h"
#include "../math/vector3.h"
#include "../math/quaternion.h"

#include <json.hpp>

#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Degrees-to-radians conversion factor.
static constexpr float kDegToRad = 3.14159265358979323846f / 180.f;

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
void ParseCameraComponent(World& world,
                           Entity entity,
                           const nlohmann::json& comp) {
    // CameraComponent is non-copyable — use registry.emplace directly.
    auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
    auto& cam = world.GetRegistry().emplace<CameraComponent>(enttEntity);
    cam.fov       = ReadFloat(comp, "fov",    60.f);
    cam.nearPlane = ReadFloat(comp, "near",    0.1f);
    cam.farPlane  = ReadFloat(comp, "far",  1000.f);
    cam.isActive  = ReadBool (comp, "active", false);
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
    light.color     = ReadVec3(comp, "color", 1.f, 1.f, 1.f);
    light.intensity = ReadFloat(comp, "intensity",  1.f);
    light.range     = ReadFloat(comp, "range",     10.f);
}

/// Parses a `"spot"` light sub-object into the given component.
///
/// `innerAngle` and `outerAngle` are expected in degrees; stored as radians.
void ApplySpotLightFields(SpotLightComponent& light,
                           const nlohmann::json& comp) {
    light.color      = ReadVec3(comp, "color", 1.f, 1.f, 1.f);
    light.intensity  = ReadFloat(comp, "intensity",    1.f);
    light.range      = ReadFloat(comp, "range",       10.f);
    light.innerAngle = ReadFloat(comp, "innerAngle", 17.f) * kDegToRad;
    light.outerAngle = ReadFloat(comp, "outerAngle", 28.f) * kDegToRad;
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

/// Parses the `"mesh"` component JSON and emplaces a `StaticMeshComponent`.
///
/// Requires an `"asset"` field with the VFS-relative mesh path. Skips with a
/// warning if the field is absent or empty.
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
    mesh.albedoPath    = ReadString(comp, "albedo");
    mesh.normalPath    = ReadString(comp, "normal");
    mesh.roughnessPath = ReadString(comp, "roughness");
    mesh.metallicPath  = ReadString(comp, "metallic");
    mesh.aoPath        = ReadString(comp, "ao");
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
        for (const auto& [compKey, compValue] : compIt->items()) {
            if (!compValue.is_object()) {
                UZ_LOG(Scene, Warning,
                       "LoadScene: '{}': component '{}' value is not a JSON object — skipped",
                       sourceName, compKey);
                continue;
            }

            if (compKey == "transform") {
                ParseTransformComponent(world, entity, compValue);
            } else if (compKey == "script") {
                ParseScriptComponent(world, entity, compValue);
            } else if (compKey == "camera") {
                ParseCameraComponent(world, entity, compValue);
            } else if (compKey == "mesh") {
                ParseMeshComponent(world, entity, compValue);
            } else if (compKey == "material") {
                ParseMaterialComponent(world, entity, compValue);
            } else if (compKey == "light") {
                ParseLightComponent(world, entity, compValue, sourceName);
            } else {
                UZ_LOG(Scene, Warning,
                       "LoadScene: '{}': unknown component type '{}' — skipped",
                       sourceName, compKey);
            }
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

} // namespace uz
