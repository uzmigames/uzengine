/// UzEngine LOD Selector — screen size computation and LOD level selection.
///
/// Provides the core LOD selection algorithm: project a bounding sphere to
/// screen space, compare against per-LOD thresholds, and return the
/// appropriate LOD index. Follows standard screen size metric.
///
/// # Algorithm
///
/// # Usage
/// ```cpp
/// float screenSize = ComputeScreenSize(sphereRadius, distance,
///                                       projMatrix00, projMatrix11);
/// uint32_t lod = SelectLOD(lodGroup, screenSize, lodBias);
/// ```
///
/// # Thread Safety
/// All functions are pure (no side effects) and safe to call from any thread.

#pragma once

#include "lod_group.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ComputeScreenSize — project bounding sphere to screen-space diameter
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the screen-space projected diameter of a bounding sphere.
///
///
/// The formula is:
///   ScreenMultiple = max(0.5 * ProjM[0][0], 0.5 * ProjM[1][1])
///   ScreenRadius = ScreenMultiple * SphereRadius / max(1.0, Distance)
///   ScreenSize = ScreenRadius * 2.0
///
/// This metric is NOT normalised to [0,1]. For an object that fills the
/// entire screen height, ScreenSize equals approximately ProjM[1][1].
/// LOD 0 defaults to 2.0 (fills the full screen).
///
/// # Arguments
/// * `sphereRadius` — World-space radius of the object's bounding sphere.
///   Must be > 0. Objects with zero radius always return 0 (lowest LOD).
/// * `distance` — World-space distance from camera origin to sphere center.
///   Clamped to a minimum of 1.0 to avoid division by zero at camera origin.
/// * `projM00` — Projection matrix element [0][0] (X-axis FOV factor).
/// * `projM11` — Projection matrix element [1][1] (Y-axis FOV factor).
///
/// # Returns
/// Screen-space projected diameter (screen-space projected diameter).
/// Always >= 0. Returns 0 if sphereRadius <= 0.
///
/// 
/// 
/// 
/// 
[[nodiscard]] inline float ComputeScreenSize(float sphereRadius,
                                              float distance,
                                              float projM00,
                                              float projM11) noexcept
{
    if (sphereRadius <= 0.0f) {
        return 0.0f;
    }

    // Get projection multiple accounting for view scaling.
    const float screenMultiple = std::max(0.5f * projM00, 0.5f * projM11);

    // Calculate screen-space projected radius
    const float screenRadius = screenMultiple * sphereRadius / std::max(1.0f, distance);

    // For clarity, we end up comparing the diameter
    return screenRadius * 2.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// SelectLOD — choose LOD level from screen size and thresholds
// ─────────────────────────────────────────────────────────────────────────────

/// Selects the appropriate LOD level for a given screen size.
///
///
/// Walks the LOD levels from coarsest to finest (highest index to 0).
/// Returns the first (coarsest) LOD whose screen size threshold is GREATER
/// than the object's screen size. The comparison is:
///   if (Square(MeshScreenSize * 0.5) > ScreenRadiusSquared) return LODIndex;
///
/// The lodBias parameter is applied as a scale factor to each threshold,
/// A bias > 1.0 increases
/// screen size thresholds (more aggressive LOD reduction = lower quality).
/// A bias < 1.0 decreases thresholds (keeps higher LODs longer = higher quality).
///
/// # Arguments
/// * `group` — LOD group with at least 1 valid level.
/// * `screenSize` — Object's screen-space projected diameter (from ComputeScreenSize).
/// * `lodBias` — Multiplier applied to all screen size thresholds (default 1.0).
///   Range: typically [0.25, 4.0]. Values outside this range are clamped.
///   - 0.5 = keep higher LODs 2x longer (higher quality)
///   - 1.0 = default behavior
///   - 2.0 = switch to lower LODs 2x sooner (higher performance)
///
/// # Returns
/// LOD index in [0, group.numLevels - 1]. 0 = highest detail.
///
/// 
/// 
[[nodiscard]] inline uint32_t SelectLOD(const LODGroup& group,
                                         float screenSize,
                                         float lodBias = 1.0f) noexcept
{
    // Clamp lodBias to a sane range to prevent extreme LOD behaviour.
    lodBias = std::clamp(lodBias, 0.25f, 4.0f);

    const uint32_t numLevels = group.numLevels < 1 ? 1 : group.numLevels;

    // Walk backwards from the coarsest LOD and return the first level whose
    // scaled screen size threshold exceeds the object's screen size.
    //
    // Uses a squared comparison for performance:
    //   if (Square(MeshScreenSize * 0.5) > ScreenRadiusSquared)
    //
    // We use the equivalent non-squared form since we already have the
    // linear screenSize from ComputeScreenSize:
    //   if (scaledThreshold > screenSize)
    //
    // Both forms select the same LOD index.
    for (int32_t i = static_cast<int32_t>(numLevels) - 1; i >= 0; --i) {
        const float scaledThreshold = group.levels[i].screenSizeThreshold * lodBias;
        if (scaledThreshold > screenSize) {
            return static_cast<uint32_t>(i);
        }
    }

    // Screen size is large enough for LOD 0 (full detail).
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeScreenSizeFromFov — alternative using FOV angle instead of proj matrix
// ─────────────────────────────────────────────────────────────────────────────

/// Computes screen size from FOV angle and screen dimensions.
///
/// This is a convenience wrapper that derives projection matrix elements
/// from the vertical FOV and aspect ratio, then delegates to the main
/// ComputeScreenSize function.
///
/// # Arguments
/// * `sphereRadius` — World-space bounding sphere radius.
/// * `distance` — World-space distance from camera to sphere center.
/// * `verticalFovRadians` — Camera vertical field of view in radians.
/// * `aspectRatio` — Viewport width / height.
///
/// # Returns
/// Screen-space projected diameter (same units as ComputeScreenSize).
[[nodiscard]] inline float ComputeScreenSizeFromFov(float sphereRadius,
                                                     float distance,
                                                     float verticalFovRadians,
                                                     float aspectRatio) noexcept
{
    const float halfFov = verticalFovRadians * 0.5f;
    const float tanHalfFov = std::tan(halfFov);

    // Perspective projection matrix diagonal elements:
    //   M[1][1] = 1.0 / tan(fov/2)
    //   M[0][0] = M[1][1] / aspectRatio
    const float projM11 = (tanHalfFov > 1e-6f) ? (1.0f / tanHalfFov) : 1.0f;
    const float projM00 = (aspectRatio > 1e-6f) ? (projM11 / aspectRatio) : projM11;

    return ComputeScreenSize(sphereRadius, distance, projM00, projM11);
}

} // namespace uz
