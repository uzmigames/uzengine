/// DecalAsset -- deserialized representation of a .uzdecal JSON file.
///
/// Defines the visual and behavioral properties of a decal type: material,
/// blend mode, lifetime/fade, normal intensity, roughness override, sort order,
/// and oriented bounding box half-extents.
///
/// # File Format (.uzdecal)
/// ```json
/// {
///     "format": "uzdecal",
///     "version": 1,
///     "material": "materials/bullet_hole.uzmat",
///     "blend_mode": "Opaque",
///     "lifetime": 10.0,
///     "fade_duration": 2.0,
///     "normal_intensity": 0.8,
///     "roughness_override": -1.0,
///     "sort_order": 0,
///     "half_extents": [0.5, 0.5, 0.2]
/// }
/// ```
///
/// # Loading
/// Use `DecalAsset::Load` (from file) or `DecalAsset::LoadFromString` (from JSON).
/// Both return `nullptr` on validation failure and log the reason via `UZ_LOG(VFX, ...)`.

#pragma once

#include "engine/vfx/decals/decal_component.h"

#include <memory>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DecalAsset -- loaded .uzdecal data
// ─────────────────────────────────────────────────────────────────────────────

/// Immutable asset data loaded from a .uzdecal JSON file.
///
/// Returned via `std::unique_ptr` from the static factory methods. Not copyable
/// to prevent accidental duplication of asset data; movable for container storage.
///
/// # Fields
/// All fields are public for direct read access. Mutation after loading is not
/// intended -- treat as read-only once constructed.
///
/// # Validation (performed during Load)
/// - `format` field must be `"uzdecal"`
/// - `materialPath` must not be empty
/// - `lifetime` must be > 0
/// - `blendMode` must be a recognized enum string
/// - `normalIntensity` is clamped to [0, 1]
/// - `halfExtents` components must all be > 0
struct DecalAsset {
    /// Path to the material asset (e.g. "materials/bullet_hole.uzmat").
    /// Must not be empty.
    std::string materialPath;

    /// How the decal composites onto the surface.
    DecalBlendMode blendMode = DecalBlendMode::Opaque;

    /// Seconds before the decal is removed from the scene.
    /// Must be > 0.
    float lifetime = 10.0f;

    /// Seconds over which the decal fades out at the end of its lifetime.
    /// A value of 0 means instant removal (no fade). Clamped to [0, lifetime].
    float fadeDuration = 2.0f;

    /// Strength of normal map influence on the surface.
    /// 0 = no normal perturbation, 1 = full normal override.
    /// Clamped to [0, 1] during loading.
    float normalIntensity = 0.8f;

    /// Override for the surface roughness under the decal.
    /// -1.0 means no override (use the surface's own roughness).
    /// Values in [0, 1] override the surface roughness directly.
    float roughnessOverride = -1.0f;

    /// Rendering sort order among overlapping decals.
    /// Higher values render on top of lower values.
    int sortOrder = 0;

    /// Oriented bounding box half-extents defining the decal projection volume.
    /// x = width/2, y = height/2, z = projection depth/2.
    /// All components must be > 0.
    Vec3 halfExtents{0.5f, 0.5f, 0.2f};

    /// Filesystem path this asset was loaded from (empty if loaded from string).
    std::string sourcePath;

    // ── Factory Methods ──────────────────────────────────────────────────────

    /// Loads a DecalAsset from a .uzdecal JSON file on disk.
    ///
    /// Reads the file at `path`, parses JSON, validates all fields, and returns
    /// the populated asset. The `sourcePath` field is set to `path`.
    ///
    /// # Arguments
    /// * `path` - Filesystem path to the .uzdecal file (UTF-8).
    ///
    /// # Returns
    /// - `std::unique_ptr<DecalAsset>` on success.
    /// - `nullptr` if the file cannot be read, JSON is malformed, or validation fails.
    ///
    /// # Errors
    /// Logs `VFX::Error` with path and reason on failure.
    [[nodiscard]] static std::unique_ptr<DecalAsset> Load(const std::string& path);

    /// Loads a DecalAsset from a JSON string (no disk I/O).
    ///
    /// Useful for testing and procedural decal definitions. The `sourcePath`
    /// field will be empty.
    ///
    /// # Arguments
    /// * `jsonStr` - UTF-8 JSON string following the .uzdecal schema.
    ///
    /// # Returns
    /// - `std::unique_ptr<DecalAsset>` on success.
    /// - `nullptr` if JSON is malformed or validation fails.
    ///
    /// # Errors
    /// Logs `VFX::Error` with reason on failure.
    [[nodiscard]] static std::unique_ptr<DecalAsset> LoadFromString(const std::string& jsonStr);

    // ── Lifecycle ────────────────────────────────────────────────────────────

    DecalAsset() = default;
    ~DecalAsset() = default;

    DecalAsset(const DecalAsset&) = delete;
    DecalAsset& operator=(const DecalAsset&) = delete;

    DecalAsset(DecalAsset&&) noexcept = default;
    DecalAsset& operator=(DecalAsset&&) noexcept = default;
};

} // namespace uz
