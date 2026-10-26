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
///   alignmentMode (= facingMode, key retained for BC), customFacingX/Y/Z, stretchFactor,
///   spawnShape, spawnOnSurface,
///   shapeParam1/2/3,
///   blendMode,
///   curlStrength, curlFrequency, curlSpeed,
///   vortexOriginX/Y/Z, vortexDirX/Y/Z,
///   vortexTangential, vortexRadial, vortexMaxRadius,
///   windDirX/Y/Z, windStrength,
///   litMode, groundCollision, restitution,
///   orbitEnabled, orbitRadius, orbitSpeed, orbitAxisX/Y/Z
///
/// # VFS Integration
/// The serializer provides memory-based overloads (`LoadFromMemory`,
/// `SerializeToString`) that decouple parsing from I/O. This enables VFS
/// integration without adding an IVFS dependency to the demo:
///
/// ```cpp
/// // Reading via VFS:
/// auto bytes = vfs->Read("vfx/presets/fire.json");
/// VFXPresetSerializer::LoadFromMemory(bytes.data(), bytes.size(), name, cfg, rate);
///
/// // Writing via VFS (LooseFileVFS only — PakVFS is read-only):
/// auto json = VFXPresetSerializer::SerializeToString("fire", cfg, rate);
/// std::optional<std::string> resolved = vfs->ResolvePath("vfx/presets/fire.json");
/// // nullopt = no filesystem mapping or path outside the VFS root;
/// // otherwise write `json` to `*resolved` via std::ofstream or platform file_write_all
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Call all methods from the main/render thread only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

struct VFXEmitterConfig;

// ─────────────────────────────────────────────────────────────────────────────
// VFS Mount Path Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Logical VFS prefix for VFX preset JSON files.
/// Maps to `<exe_dir>/content/vfx/presets/` on disk when using LooseFileVFS.
static constexpr const char* kVFXPresetMountPath = "vfx/presets/";

/// Logical VFS prefix for VFX shader files.
/// Maps to `shaders/vfx/` on disk. ShaderVFS already mounts `/engine/` so
/// VFX shaders are accessible as `/engine/vfx/<name>.hlsl`.
static constexpr const char* kVFXShaderMountPath = "/engine/vfx/";

// ─────────────────────────────────────────────────────────────────────────────
// VFX Preset Serializer
// ─────────────────────────────────────────────────────────────────────────────

/// Static utility class for saving and loading VFX emitter presets as JSON.
///
/// Provides two layers of API:
/// - **File-based** (`Save`, `Load`, `ScanDirectory`) — reads/writes directly
///   to the filesystem via `std::ifstream`/`std::ofstream`. Used by the
///   standalone `vfx_viewport` demo.
/// - **Memory-based** (`SerializeToString`, `LoadFromMemory`) — pure in-memory
///   serialization with no I/O. Enables VFS integration: callers read bytes
///   from `IVFS::Read()` and pass them to `LoadFromMemory()`, or serialize to
///   a string and write via `IVFS::ResolvePath()` + file write.
///
/// The file-based methods are implemented in terms of the memory-based core,
/// so behavior is identical regardless of which API is used.
///
/// No instances are needed — all methods are static.
class VFXPresetSerializer {
public:
    VFXPresetSerializer() = delete;

    // ── Memory-based API (VFS-compatible) ────────────────────────────────

    /// Serializes a VFXEmitterConfig + spawn rate to a JSON string.
    ///
    /// The returned string is a complete, valid JSON document that can be
    /// written to disk or passed to `IVFS::ResolvePath()` + file write.
    ///
    /// # Arguments
    /// * `name`      — Human-readable preset name stored in the JSON.
    /// * `cfg`       — The emitter configuration to serialize.
    /// * `spawnRate` — Spawn rate (particles/second).
    ///
    /// # Returns
    /// A non-empty JSON string on success. Returns an empty string only if
    /// an internal formatting error occurs (should not happen in practice).
    ///
    /// # Example
    /// ```cpp
    /// auto json = VFXPresetSerializer::SerializeToString("Fire", config, 500.0f);
    /// vfs->Write("vfx/presets/fire.json", json.data(), json.size());
    /// ```
    [[nodiscard]] static std::string SerializeToString(const std::string& name,
                                                       const VFXEmitterConfig& cfg,
                                                       float spawnRate);

    /// Deserializes a VFXEmitterConfig + spawn rate from a raw memory buffer.
    ///
    /// The buffer must contain a valid JSON document (null-terminator is not
    /// required). Unknown keys are silently ignored for forward-compatibility.
    /// If the JSON contains a `"version"` field other than 1, a warning is
    /// logged but loading continues (best-effort migration).
    ///
    /// # Arguments
    /// * `data`        — Pointer to the JSON bytes. Must not be null.
    /// * `size`        — Number of bytes in the buffer.
    /// * `outName`     — Receives the preset name string.
    /// * `outCfg`      — Receives the loaded VFXEmitterConfig.
    /// * `outSpawnRate`— Receives the loaded spawn rate.
    ///
    /// # Returns
    /// `true` on success, `false` if `data` is null, `size` is zero, or
    /// the buffer does not contain recognizable JSON key/value pairs.
    ///
    /// # Example
    /// ```cpp
    /// auto bytes = vfs->Read("vfx/presets/fire.json");
    /// VFXPresetSerializer::LoadFromMemory(bytes.data(), bytes.size(), name, cfg, rate);
    /// ```
    [[nodiscard]] static bool LoadFromMemory(const void* data,
                                             size_t size,
                                             std::string& outName,
                                             VFXEmitterConfig& outCfg,
                                             float& outSpawnRate);

    // ── File-based API (standalone demo) ─────────────────────────────────

    /// Serializes a VFXEmitterConfig + spawn rate to a JSON file.
    ///
    /// Internally calls `SerializeToString()` and writes the result to disk.
    /// Creates parent directories as needed.
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
    /// Internally reads the file into memory and calls `LoadFromMemory()`.
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
    ///
    /// @note Directory listing is only meaningful for filesystem-backed storage.
    /// When integrating with VFS, use `LooseFileVFS::ResolvePath()` to get the
    /// real directory path and pass it to this method. `PakVFS` does not support
    /// directory enumeration — `ScanDirectory()` will return an empty list for
    /// pak-resolved paths.
    [[nodiscard]] static std::vector<std::string> ScanDirectory(const std::string& dir);
};

} // namespace uz
