/// TrailAsset -- deserialized representation of a .uztrail JSON file.
///
/// Defines the visual and behavioral properties of a trail effect: material,
/// width curve (start/end), color gradient (start/end), UV scrolling rate,
/// point lifetime, and minimum point distance.
///
/// # File Format (.uztrail)
/// ```json
/// {
///     "format": "uztrail",
///     "version": 1,
///     "material": "materials/fire_trail.uzmat",
///     "width_start": 0.5,
///     "width_end": 0.05,
///     "color_start": [1.0, 0.8, 0.2, 1.0],
///     "color_end": [1.0, 0.2, 0.0, 0.0],
///     "uv_scroll_rate": 1.5,
///     "max_points": 100,
///     "point_lifetime": 2.0,
///     "min_point_distance": 0.1,
///     "sort_order": 0
/// }
/// ```
///
/// # Loading
/// Use `TrailAsset::Load` (from file) or `TrailAsset::LoadFromString` (from JSON string).
/// Both return `nullptr` on validation failure and log the reason via `UZ_LOG(VFX, ...)`.

#pragma once

#include "vector4.h"

#include <memory>
#include <string>

namespace uz {
class IVFS;

// ─────────────────────────────────────────────────────────────────────────────
// TrailAsset -- loaded .uztrail data
// ─────────────────────────────────────────────────────────────────────────────

/// Immutable asset data loaded from a .uztrail JSON file.
///
/// Returned via `std::unique_ptr` from the static factory methods. Not copyable
/// to prevent accidental duplication of asset data; movable for container storage.
///
/// # Fields
/// All fields are public for direct read access. Mutation after loading is not
/// intended — treat as read-only once constructed.
///
/// # Validation (performed during Load)
/// - `format` field must be `"uztrail"`
/// - `materialPath` must not be empty
/// - `widthStart` must be > 0
/// - `widthEnd` must be >= 0
/// - `maxPoints` must be > 0
/// - `pointLifetime` must be > 0
/// - `minPointDistance` must be > 0
/// - `color_start` and `color_end` RGB and alpha components are clamped to [0, 1]
struct TrailAsset {
    /// Path to the material asset (e.g. "materials/fire_trail.uzmat").
    /// Must not be empty.
    std::string materialPath;

    /// World-space width of the ribbon at the newest (head) point, in metres.
    /// Must be > 0. Controls how wide the trail starts.
    float widthStart = 0.5f;

    /// World-space width of the ribbon at the oldest (tail) point, in metres.
    /// Must be >= 0. Controls how narrow the trail tapers.
    float widthEnd = 0.05f;

    /// RGBA color at the newest (head) point of the trail.
    /// Each component clamped to [0, 1] during loading.
    Vec4 colorStart{1.0f, 1.0f, 1.0f, 1.0f};

    /// RGBA color at the oldest (tail) point of the trail.
    /// Alpha = 0 produces a natural fade-out toward the tail.
    /// Each component clamped to [0, 1] during loading.
    Vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};

    /// Scrolling speed of the UV V coordinate along the trail length, in UV units
    /// per second. 0 disables scrolling; negative values scroll backward.
    float uvScrollRate = 0.0f;

    /// Maximum number of trail points kept alive simultaneously.
    /// Controls ring buffer capacity in `TrailPointBuffer`. Must be > 0.
    uint32_t maxPoints = 100;

    /// Seconds before a trail point is removed from the tail.
    /// Must be > 0. Shorter lifetimes produce shorter trails.
    float pointLifetime = 2.0f;

    /// Minimum world-space distance (metres) the emitter must travel before
    /// a new point is emitted. Must be > 0. Prevents point over-population
    /// when the emitter is nearly stationary.
    float minPointDistance = 0.1f;

    /// Rendering sort order relative to other transparent VFX.
    /// Higher values render on top of lower values.
    int sortOrder = 0;

    /// Filesystem path this asset was loaded from (empty if loaded from string).
    std::string sourcePath;

    // ── Factory Methods ──────────────────────────────────────────────────────

    /// Loads a TrailAsset from a .uztrail JSON file on disk.
    ///
    /// Reads the file at `path`, parses JSON, validates all fields, and returns
    /// the populated asset. The `sourcePath` field is set to `path`.
    ///
    /// # Parameters
    /// - `path`: Filesystem path to the .uztrail file (UTF-8).
    ///
    /// # Returns
    /// - `std::unique_ptr<TrailAsset>` on success.
    /// - `nullptr` if the file cannot be read, JSON is malformed, or validation fails.
    ///
    /// # Errors
    /// Logs `VFX::Error` with path and reason on failure.
    [[nodiscard]] static std::unique_ptr<TrailAsset> Load(const std::string& path, IVFS* vfs = nullptr);

    /// Loads a TrailAsset from a JSON string (no disk I/O).
    ///
    /// Useful for testing and procedural trail definitions. The `sourcePath`
    /// field will be empty.
    ///
    /// # Parameters
    /// - `jsonStr`: UTF-8 JSON string following the .uztrail schema.
    ///
    /// # Returns
    /// - `std::unique_ptr<TrailAsset>` on success.
    /// - `nullptr` if JSON is malformed or validation fails.
    ///
    /// # Errors
    /// Logs `VFX::Error` with reason on failure.
    [[nodiscard]] static std::unique_ptr<TrailAsset> LoadFromString(const std::string& jsonStr);

    // ── Lifecycle ────────────────────────────────────────────────────────────

    TrailAsset() = default;
    ~TrailAsset() = default;

    TrailAsset(const TrailAsset&) = delete;
    TrailAsset& operator=(const TrailAsset&) = delete;

    TrailAsset(TrailAsset&&) noexcept = default;
    TrailAsset& operator=(TrailAsset&&) noexcept = default;
};

} // namespace uz
