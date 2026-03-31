/// VFX Preset Serializer — JSON Save / Load for VFXEmitterConfig
///
/// Serializes `VFXEmitterConfig` + spawn rate + name to a human-readable
/// JSON file in `content/vfx/presets/`. The format is hand-rolled (no
/// third-party JSON library) and intentionally simple: one key/value pair
/// per line, numeric values written with full float precision.
///
/// # File Format (schema version 1)
/// ```json
/// {
///   "version": 1,
///   "name": "Fire Fountain",
///   "spawnRate": 500.000000,
///   "posX": 0.000000, "posY": 1.500000, "posZ": 0.000000,
///   ...
/// }
/// ```
///
/// Field list (§1.3 of task 4.2.0, in declaration order):
///   version, name, spawnRate,
///   posX/Y/Z, spawnRadius,
///   velX/Y/Z, velocityVariation, spreadAngle,
///   minLifetime, maxLifetime, initialSize,
///   initColorR/G/B/A,
///   gravX/Y/Z, damping,
///   softFadeDistance, emissiveIntensity,
///   sizeOverLifeStart/Peak/End/PeakTime,
///   colorLifeStartR/G/B/A, colorLifeEndR/G/B/A,
///   alignmentMode, stretchFactor,
///   spawnShape, spawnOnSurface,
///   shapeParam1/2/3,
///   blendMode,
///   curlStrength, curlFrequency, curlSpeed,
///   vortexOriginX/Y/Z, vortexDirX/Y/Z,
///   vortexTangential, vortexRadial, vortexMaxRadius,
///   windDirX/Y/Z, windStrength,
///   litMode, groundCollision, restitution
///
/// # Thread Safety
/// NOT thread-safe. Call all methods from the main/render thread only.

#pragma once

#include <string>
#include <vector>

namespace uz {

struct VFXEmitterConfig;

// ─────────────────────────────────────────────────────────────────────────────
// VFX Preset Serializer
// ─────────────────────────────────────────────────────────────────────────────

/// Static utility class for saving and loading VFX emitter presets as JSON.
///
/// No instances are needed — all methods are static.
class VFXPresetSerializer {
public:
    VFXPresetSerializer() = delete;

    /// Serializes a VFXEmitterConfig + spawn rate to a JSON file.
    ///
    /// # Arguments
    /// * `cfg`       — The emitter configuration to serialize.
    /// * `spawnRate` — Spawn rate (particles/second).
    /// * `name`      — Human-readable preset name stored in the JSON.
    /// * `filePath`  — Absolute or relative path for the output `.json` file.
    ///
    /// # Returns
    /// `true` on success, `false` on any I/O error (error is logged via UZ_LOG).
    [[nodiscard]] static bool Save(const VFXEmitterConfig& cfg,
                                   float spawnRate,
                                   const std::string& name,
                                   const std::string& filePath);

    /// Deserializes a VFXEmitterConfig + spawn rate from a JSON file.
    ///
    /// Unknown keys are silently ignored for forward-compatibility.
    /// If the file contains a `"version"` field other than 1, a warning is
    /// logged but loading continues (best-effort migration).
    ///
    /// # Arguments
    /// * `filePath`    — Path to the `.json` file.
    /// * `outCfg`      — Receives the loaded VFXEmitterConfig.
    /// * `outSpawnRate`— Receives the loaded spawn rate.
    /// * `outName`     — Receives the preset name string.
    ///
    /// # Returns
    /// `true` on success, `false` if the file cannot be opened or parsed.
    [[nodiscard]] static bool Load(const std::string& filePath,
                                   VFXEmitterConfig& outCfg,
                                   float& outSpawnRate,
                                   std::string& outName);

    /// Scans a directory for all `.json` files and returns their absolute paths
    /// sorted alphabetically by filename stem.
    ///
    /// # Arguments
    /// * `dir` — Directory path to scan (relative to CWD or absolute).
    ///
    /// # Returns
    /// Sorted vector of absolute `.json` file paths. Empty if the directory
    /// does not exist or contains no `.json` files.
    [[nodiscard]] static std::vector<std::string> ScanDirectory(const std::string& dir);
};

} // namespace uz
