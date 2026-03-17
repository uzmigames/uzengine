/// scene_loader.h — .uzscene JSON loader.
///
/// Parses a `.uzscene` JSON byte buffer and populates a `World` with entities
/// and their components. The loader supports hierarchical entities via the
/// `"children"` key and the following component types:
///
/// | JSON key      | C++ component         |
/// |---------------|-----------------------|
/// | `"transform"` | `SceneComponent`      |
/// | `"script"`    | `CScript`             |
/// | `"camera"`    | `CameraComponent`     |
/// | `"light"`     | Scene light types     |
///
/// Unknown component keys are logged as warnings and skipped — they do NOT
/// cause the load to fail.
///
/// # JSON Format
/// ```json
/// {
///   "entities": [
///     {
///       "name": "player",
///       "components": {
///         "transform": { "position": [0, 1, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
///         "script":    { "path": "scripts/player.lua" },
///         "camera":    { "fov": 60, "near": 0.1, "far": 1000, "active": true }
///       },
///       "children": []
///     }
///   ]
/// }
/// ```
///
/// # Error Policy
/// - Returns 0 and logs `Scene::Error` if the JSON is malformed or missing the
///   `"entities"` array.
/// - Individual entity parse errors are logged as `Scene::Warning` and that
///   entity is skipped; loading continues for the remaining entities.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace uz {

class World;

// ─────────────────────────────────────────────────────────────────────────────
// LoadScene
// ─────────────────────────────────────────────────────────────────────────────

/// Loads entities from a `.uzscene` JSON byte buffer into a `World`.
///
/// Parses `sceneJson` as UTF-8 JSON, walks the `"entities"` array recursively
/// (following `"children"` arrays), and creates one `Entity` per entry.
///
/// # Component Dispatch
/// For each entity the loader reads the `"components"` object and emplaces the
/// corresponding C++ components:
///
/// - **`"transform"`**: Emplaces `SceneComponent`. Reads `"position"`,
///   `"rotation"` (Euler degrees → radians → `Quat::FromEuler`), and `"scale"`
///   as `[x, y, z]` float arrays. Missing arrays default to `[0,0,0]` for
///   position/rotation and `[1,1,1]` for scale.
///
/// - **`"script"`**: Emplaces `CScript` with `scriptPath` set to the `"path"` string.
///   `luaRef`, `envRef`, and `started` are left at their sentinel defaults; the
///   `ScriptSystem` completes initialization on first update.
///
/// - **`"camera"`**: Emplaces `CameraComponent`. Reads `"fov"` (degrees),
///   `"near"`, `"far"`, and `"active"`.
///
/// - **`"light"`**: Emplaces a light component based on `"type"`. Supported
///   types: `"directional"` → `DirectionalLightComponent`,
///   `"point"` → `PointLightComponent`, `"spot"` → `SpotLightComponent`.
///   Reads `"color"` (`[r, g, b]`), `"intensity"`, and type-specific fields
///   (`"range"`, `"innerAngle"`, `"outerAngle"` in degrees).
///
/// - **Unknown key**: Logged at `Scene::Warning` level; entity otherwise succeeds.
///
/// # Non-Copyable Components
/// `SceneComponent`, `CameraComponent`, and light components inherit from
/// `Component` which deletes the copy constructor. The loader uses
/// `world.GetRegistry().emplace<T>(enttEntity)` to construct in-place, then
/// sets fields through the returned reference.
///
/// # Parameters
/// - `world`: Target world. Entities are created inside this world. Must outlive
///   any use of the returned entity handles.
/// - `sceneJson`: Raw JSON bytes. Must be valid UTF-8. Need not be null-terminated.
/// - `sourceName`: Human-readable identifier for the source (file path, asset name)
///   used in log messages. Does not affect loading behaviour.
///
/// # Returns
/// - Number of top-level + child entities successfully created. Returns 0 if the
///   JSON cannot be parsed or the `"entities"` key is missing/invalid.
///
/// # Side Effects
/// - Logs `Scene::Info` with the entity count and `sourceName` on success.
/// - Logs `Scene::Error` on fatal parse failures.
/// - Logs `Scene::Warning` for skipped entities or unknown component types.
///
/// # Examples
/// ```cpp
/// std::vector<uint8_t> bytes = vfs.Read("scenes/main.uzscene");
/// size_t count = uz::LoadScene(world, bytes, "scenes/main.uzscene");
/// UZ_LOG(Scene, Info, "Loaded {} entities", count);
/// ```
[[nodiscard]] size_t LoadScene(World& world,
                                const std::vector<uint8_t>& sceneJson,
                                const std::string& sourceName);

/// Serializes the World's entities to `.uzscene` JSON format.
///
/// Iterates all entities in the World, serializes their components
/// (transform, mesh, material, light, camera, script) to JSON, and
/// returns the result as a UTF-8 byte vector.
///
/// # Parameters
/// - `world`: The world to serialize.
///
/// # Returns
/// UTF-8 JSON bytes. Empty if the world has no entities.
[[nodiscard]] std::vector<uint8_t> SaveScene(const World& world);

} // namespace uz
