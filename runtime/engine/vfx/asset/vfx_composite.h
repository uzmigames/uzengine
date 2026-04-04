#pragma once

/// VFX Composite effect -- multiple emitters with offsets and delays.
///
/// A composite effect groups multiple VFX graphs into a single spawnable
/// unit. Each entry has a position offset and an optional start delay,
/// enabling complex effects like explosions (fire + smoke + debris).
///
/// # File Format (.uzvfx_composite)
/// ```json
/// {
///   "format": "uzvfx_composite",
///   "version": 1,
///   "name": "explosion",
///   "emitters": [
///     { "name": "fire",   "graph": "vfx/fire.uzvfx",   "offset": [0,0,0], "delay": 0.0 },
///     { "name": "smoke",  "graph": "vfx/smoke.uzvfx",  "offset": [0,0.5,0], "delay": 0.2 },
///     { "name": "debris", "graph": "vfx/debris.uzvfx", "offset": [0,0,0], "delay": 0.0 }
///   ]
/// }
/// ```
///
/// # Thread Safety
/// Not thread-safe. Loaded and accessed on the main thread only.

#include "vector3.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {
class IVFS;

// ─────────────────────────────────────────────────────────────────────────────
// VFXCompositeEntry -- single emitter within a composite effect
// ─────────────────────────────────────────────────────────────────────────────

/// Single emitter entry within a composite effect.
///
/// Each entry references a .uzvfx graph file and specifies the local
/// position offset and start delay relative to the composite's origin
/// and spawn time.
struct VFXCompositeEntry {
    /// Human-readable name for this sub-emitter (e.g., "fire", "smoke").
    std::string name;

    /// Filesystem path to the .uzvfx graph asset.
    std::string graphPath;

    /// Local position offset from the composite origin (world units).
    Vec3 offset = {};

    /// Delay in seconds before this emitter begins spawning.
    /// Must be >= 0. A delay of 0 means immediate emission.
    float delay = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXComposite -- loaded .uzvfx_composite asset
// ─────────────────────────────────────────────────────────────────────────────

/// A loaded composite VFX effect definition.
///
/// Owns a list of VFXCompositeEntry objects parsed from a .uzvfx_composite
/// JSON file. Non-copyable due to unique_ptr factory pattern, but the
/// underlying data is movable.
///
/// # Lifetime
/// Created via `Load()` or `LoadFromString()`. The composite definition
/// is referenced when spawning multi-emitter effects; the caller is
/// responsible for keeping it alive while emitters are active.
class VFXComposite {
public:
    VFXComposite() = default;
    ~VFXComposite() = default;

    /// Non-copyable (returned via unique_ptr from factory).
    VFXComposite(const VFXComposite&) = delete;
    VFXComposite& operator=(const VFXComposite&) = delete;

    /// Movable for container storage.
    VFXComposite(VFXComposite&&) noexcept = default;
    VFXComposite& operator=(VFXComposite&&) noexcept = default;

    /// Loads a VFXComposite from a .uzvfx_composite JSON file on disk.
    ///
    /// Reads the file at `path` using FileReadAllText, then delegates
    /// to LoadFromString for parsing.
    ///
    /// # Parameters
    /// - `path`: Filesystem path to the .uzvfx_composite file.
    ///
    /// # Returns
    /// - Owning pointer to the loaded composite on success.
    /// - `nullptr` if the file cannot be read or JSON is invalid.
    ///
    /// # Side Effects
    /// Logs `VFX/Error` on failure with path and reason.
    [[nodiscard]] static std::unique_ptr<VFXComposite> Load(const std::string& path, IVFS* vfs = nullptr);

    /// Loads a VFXComposite from a JSON string (primarily for testing).
    ///
    /// # Parameters
    /// - `jsonStr`: UTF-8 JSON string in .uzvfx_composite format.
    ///
    /// # Returns
    /// - Owning pointer to the loaded composite on success.
    /// - `nullptr` if JSON is malformed, format field is wrong, or
    ///   the emitters array is empty/invalid.
    ///
    /// # Side Effects
    /// Logs `VFX/Error` on parse failure with the specific reason.
    [[nodiscard]] static std::unique_ptr<VFXComposite> LoadFromString(const std::string& jsonStr);

    // ── Accessors ────────────────────────────────────────────────────────────

    /// Returns the composite effect name (e.g., "explosion").
    [[nodiscard]] const std::string& GetName() const noexcept { return name_; }

    /// Returns the .uzvfx_composite format version.
    [[nodiscard]] uint32_t GetVersion() const noexcept { return version_; }

    /// Returns all emitter entries in this composite.
    [[nodiscard]] const std::vector<VFXCompositeEntry>& GetEntries() const noexcept {
        return entries_;
    }

    /// Returns the number of emitter entries.
    [[nodiscard]] size_t GetEmitterCount() const noexcept { return entries_.size(); }

private:
    std::string name_;
    uint32_t version_ = 0;
    std::vector<VFXCompositeEntry> entries_;
};

} // namespace uz
