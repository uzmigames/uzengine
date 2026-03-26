/// VFX LOD system — distance- and screen-coverage-based particle quality selection.
///
/// Determines particle system quality tiers based on the emitter's distance from
/// the camera and its screen-space coverage. The goal is to spend particle budget
/// only where it is visually meaningful.
///
/// # Quality Tiers
/// | Level   | Spawn factor | Intent                                |
/// |---------|--------------|---------------------------------------|
/// | Full    | 1.00         | Full quality, emitter is close/large  |
/// | Reduced | 0.50         | Half spawn rate, medium distance      |
/// | Minimal | 0.25         | Quarter spawn rate, far distance      |
/// | Culled  | 0.00         | Completely disabled, invisible/tiny   |
///
/// # Usage
/// ```cpp
/// VFXLodSettings settings;  // default: 50 / 100 / 200 m
///
/// VFXLodLevel lod = VFXLodEvaluator::Evaluate(
///     emitter.position, camera.position,
///     emitter.boundingRadius, viewportHeight,
///     settings
/// );
///
/// float spawnFactor = VFXLodLevelToSpawnFactor(lod);
/// emitter.SetSpawnRateMultiplier(spawnFactor);
/// ```
///
/// # Thread Safety
/// All functions in this header are stateless and thread-safe.

#pragma once

#include "vector3.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodLevel — particle system quality tiers
// ─────────────────────────────────────────────────────────────────────────────

/// Quality tier assigned to a particle emitter each frame.
///
/// Higher numeric values represent lower quality. The evaluator selects the
/// most aggressive (highest numeric) tier that any criterion demands.
enum class VFXLodLevel : uint8_t {
    Full    = 0, ///< 100% spawn rate — emitter is close or fills the screen.
    Reduced = 1, ///< 50% spawn rate — medium distance or reduced screen coverage.
    Minimal = 2, ///< 25% spawn rate — far distance or very small on screen.
    Culled  = 3, ///< 0% spawn rate — emitter is invisible or beyond cull range.
};

/// Converts a VFXLodLevel to its human-readable string.
///
/// # Returns
/// Static string (e.g., "Full", "Reduced", "Minimal", "Culled").
/// Returns "Unknown" for values outside the defined range.
[[nodiscard]] const char* VFXLodLevelToString(VFXLodLevel level) noexcept;

/// Returns the spawn rate multiplier corresponding to a LOD level.
///
/// Multiply the emitter's base spawn rate by this factor each frame.
///
/// | Level   | Returned value |
/// |---------|----------------|
/// | Full    | 1.0f           |
/// | Reduced | 0.5f           |
/// | Minimal | 0.25f          |
/// | Culled  | 0.0f           |
///
/// # Parameters
/// - `level`: The LOD level to convert.
///
/// # Returns
/// A float in {0.0, 0.25, 0.5, 1.0}. Returns 0.0 for unknown values.
[[nodiscard]] float VFXLodLevelToSpawnFactor(VFXLodLevel level) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodSettings — per-emitter distance threshold configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Distance thresholds (in world units) that define LOD tier transitions.
///
/// The three values divide the [0, ∞) distance range into four bands:
///
/// ```
/// [0,         distances[0]) → Full
/// [distances[0], distances[1]) → Reduced
/// [distances[1], distances[2]) → Minimal
/// [distances[2],           ∞) → Culled
/// ```
///
/// Defaults are tuned for a standard game camera with metres as the world unit.
struct VFXLodSettings {
    /// Distance at which Full transitions to Reduced (default: 50 m).
    /// Distance at which Reduced transitions to Minimal (default: 100 m).
    /// Distance at which Minimal transitions to Culled (default: 200 m).
    float distances[3] = {50.0f, 100.0f, 200.0f};

    /// Validates that all thresholds are positive and strictly increasing.
    ///
    /// A settings struct is valid when:
    ///   `distances[0] > 0 && distances[0] < distances[1] < distances[2]`
    ///
    /// # Returns
    /// `true` if the thresholds are well-formed; `false` otherwise.
    [[nodiscard]] bool IsValid() const noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXScreenCuller — screen-space pixel coverage utility
// ─────────────────────────────────────────────────────────────────────────────

/// Computes and evaluates the screen-space pixel coverage of a bounding sphere.
///
/// Used by `VFXLodEvaluator` to cull emitters whose bounding sphere projects to
/// fewer pixels than the `minScreenPixels` threshold. This catches cases where
/// a distant but fast-moving emitter would otherwise only be partially culled by
/// the distance criterion.
///
/// # Formula
/// ```
/// screenPixels = (radius / max(distance, ε)) * screenHeight * 0.5
/// ```
/// This approximates the projected diameter of a sphere in pixels, where
/// `screenHeight * 0.5` normalises for the vertical half-FOV.
class VFXScreenCuller {
public:
    /// Computes the approximate screen-space coverage of a sphere in pixels.
    ///
    /// # Parameters
    /// - `boundingRadius`: World-space radius of the emitter's bounding sphere (metres).
    ///   Must be >= 0; negative values are treated as zero.
    /// - `distance`:       Distance from the camera to the sphere centre (metres).
    ///   Clamped to a minimum of 0.001 to avoid division by zero.
    /// - `screenHeight`:   Render target height in pixels.
    ///
    /// # Returns
    /// Approximate diameter in pixels occupied by the sphere on screen.
    /// Returns 0 if `boundingRadius <= 0` or `screenHeight <= 0`.
    [[nodiscard]] static float ComputeScreenPixels(
        float boundingRadius,
        float distance,
        float screenHeight
    ) noexcept;

    /// Returns true if the emitter's screen coverage is below the cull threshold.
    ///
    /// # Parameters
    /// - `boundingRadius`:   World-space bounding sphere radius (metres).
    /// - `distance`:         Camera-to-emitter distance (metres).
    /// - `screenHeight`:     Render target height in pixels.
    /// - `minScreenPixels`:  Coverage threshold in pixels. Emitters smaller than
    ///                       this are culled. Default: 4.0 px.
    ///
    /// # Returns
    /// `true` if the computed screen coverage is strictly less than
    /// `minScreenPixels`; `false` otherwise.
    [[nodiscard]] static bool ShouldCull(
        float boundingRadius,
        float distance,
        float screenHeight,
        float minScreenPixels = 4.0f
    ) noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodEvaluator — combined distance + screen-coverage LOD selection
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the LOD level for a particle emitter each frame.
///
/// Two independent criteria are evaluated:
/// 1. **Distance** — camera-to-emitter world-space distance versus `VFXLodSettings`
///    thresholds.
/// 2. **Screen size** — projected sphere coverage in pixels via `VFXScreenCuller`.
///
/// The *more aggressive* (numerically higher, lower quality) result wins.
/// This ensures that an emitter is only promoted to a higher quality tier when
/// *both* criteria agree it deserves it.
///
/// # Example
/// ```cpp
/// VFXLodSettings settings;
/// settings.distances[0] = 30.0f;
/// settings.distances[1] = 80.0f;
/// settings.distances[2] = 150.0f;
///
/// VFXLodLevel lod = VFXLodEvaluator::Evaluate(
///     emitterPos, cameraPos,
///     /*boundingRadius=*/2.5f,
///     /*screenHeight=*/1080.0f,
///     settings
/// );
/// ```
class VFXLodEvaluator {
public:
    /// Computes LOD level from camera distance alone.
    ///
    /// Compares the squared distance between `emitterPos` and `cameraPos` against
    /// the squared thresholds from `settings` to avoid a square-root per call.
    ///
    /// # Parameters
    /// - `emitterPos`: World-space emitter centre.
    /// - `cameraPos`:  World-space camera position.
    /// - `settings`:   Distance thresholds. Must satisfy `settings.IsValid()`;
    ///                 behaviour is undefined for invalid settings.
    ///
    /// # Returns
    /// The LOD tier matching the emitter's distance from the camera.
    [[nodiscard]] static VFXLodLevel ComputeFromDistance(
        const Vec3& emitterPos,
        const Vec3& cameraPos,
        const VFXLodSettings& settings
    ) noexcept;

    /// Computes LOD level from projected screen coverage.
    ///
    /// Delegates to `VFXScreenCuller::ShouldCull()`. Returns `Culled` when the
    /// sphere is too small to be worth simulating; otherwise returns `Full`.
    /// Screen-size alone is a binary cull — detailed quality tiers are left to
    /// the distance criterion in `Evaluate()`.
    ///
    /// # Parameters
    /// - `boundingRadius`:  World-space bounding sphere radius (metres).
    /// - `distance`:        Camera-to-emitter distance (metres).
    /// - `screenHeight`:    Render target height in pixels.
    /// - `minScreenPixels`: Below this pixel count the emitter is culled. Default: 4.0f.
    ///
    /// # Returns
    /// `VFXLodLevel::Culled` if screen coverage < `minScreenPixels`;
    /// `VFXLodLevel::Full` otherwise.
    [[nodiscard]] static VFXLodLevel ComputeFromScreenSize(
        float boundingRadius,
        float distance,
        float screenHeight,
        float minScreenPixels = 4.0f
    ) noexcept;

    /// Evaluates the final LOD level using both distance and screen-coverage criteria.
    ///
    /// Computes both `ComputeFromDistance()` and `ComputeFromScreenSize()` and
    /// returns whichever yields the higher (more aggressive / lower quality) value.
    ///
    /// # Parameters
    /// - `emitterPos`:      World-space emitter centre.
    /// - `cameraPos`:       World-space camera position.
    /// - `boundingRadius`:  World-space bounding sphere radius (metres).
    /// - `screenHeight`:    Render target height in pixels.
    /// - `settings`:        Distance thresholds for the emitter.
    /// - `minScreenPixels`: Screen-size cull threshold in pixels. Default: 4.0f.
    ///
    /// # Returns
    /// The most restrictive LOD level demanded by either criterion.
    ///
    /// # Side Effects
    /// None. This function is pure and stateless.
    [[nodiscard]] static VFXLodLevel Evaluate(
        const Vec3& emitterPos,
        const Vec3& cameraPos,
        float boundingRadius,
        float screenHeight,
        const VFXLodSettings& settings,
        float minScreenPixels = 4.0f
    ) noexcept;
};

} // namespace uz
