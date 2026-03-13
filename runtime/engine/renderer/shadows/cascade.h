/// UzEngine Cascaded Shadow Map — Frustum Splitting
///
/// Computes cascade split distances and per-cascade light-space view-projection
/// matrices for Cascaded Shadow Maps (CSM). Uses the "practical split scheme"
/// (Nvidia GPU Gems 3, Ch. 10) that blends logarithmic and linear splitting
/// via a configurable lambda parameter.
///
/// # Algorithm Overview
/// 1. Extract camera near/far from the projection matrix.
/// 2. Compute N+1 split planes using `lerp(log, linear, lambda)`.
/// 3. For each cascade, compute the 8 frustum corners of the sub-frustum,
///    transform them to world space, build a tight light-space orthographic
///    projection, and snap to texel boundaries to prevent shadow shimmer.
///
/// # Thread Safety
/// All functions are stateless and thread-safe. No global or mutable state.
///
/// # Coordinate System
/// Left-handed, DX clip space (z in [0, 1]).

#pragma once

#include "vector3.h"
#include "mat4.h"
#include "log.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Hard limit on the number of shadow cascades.
///
/// GPU constant buffer layouts and shader arrays are sized to this value.
/// Requesting more cascades than this is a programming error.
constexpr uint32_t kMaxShadowCascades = 4;

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCascade — per-cascade output data
// ─────────────────────────────────────────────────────────────────────────────

/// Data for a single shadow cascade, produced by `ComputeCascades`.
///
/// Contains the light-space view-projection matrix used to render the shadow
/// map, the view-space split distances for cascade selection in the pixel
/// shader, and the world-space texel size for computing per-cascade bias.
struct ShadowCascade {
    /// Light-space view-projection matrix (lightProj * lightView).
    ///
    /// Used to transform world-space positions into shadow map UV + depth.
    /// The projection is orthographic, snapped to texel boundaries.
    Mat4 viewProj;

    /// Near split distance in view space (positive, increases with cascade index).
    float splitNear = 0.0f;

    /// Far split distance in view space (positive, increases with cascade index).
    float splitFar = 0.0f;

    /// World-space size of one shadow map texel along the largest cascade axis.
    ///
    /// Used to scale depth/normal bias proportionally to the cascade's
    /// coverage area, so bias is consistent across all cascades.
    float texelSize = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShadowConfig — cascade configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the cascaded shadow map system.
///
/// All distances are in world-space units. The split lambda controls the
/// distribution of cascades between logarithmic (better near-camera quality)
/// and linear (more uniform coverage).
struct ShadowConfig {
    /// Number of cascades to compute (1 to kMaxShadowCascades).
    ///
    /// More cascades improve shadow quality at the cost of additional
    /// draw calls and shadow map memory.
    uint32_t cascadeCount = 4;

    /// Shadow map resolution per cascade in texels (width = height).
    ///
    /// Common values: 1024 (low), 2048 (medium), 4096 (high).
    uint32_t resolution = 2048;

    /// Maximum shadow distance from the camera in world units.
    ///
    /// Shadows beyond this distance are not rendered. The camera's far plane
    /// is clamped to this value when computing split distances.
    float maxDistance = 100.0f;

    /// Practical split scheme interpolation factor.
    ///
    /// - 0.0 = purely linear splits (uniform coverage, poor near quality).
    /// - 1.0 = purely logarithmic splits (best near quality, sparse far).
    /// - 0.75 = recommended default (Nvidia GPU Gems 3).
    float splitLambda = 0.75f;

    /// Normal-direction bias in world units, applied per cascade.
    float normalBias = 0.02f;

    /// Depth bias applied to shadow comparison, per cascade.
    float depthBias = 0.005f;
};

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — main entry point
// ─────────────────────────────────────────────────────────────────────────────

/// Computes cascade split distances and light-space matrices for CSM.
///
/// # Arguments
/// * `cameraView`  — The camera's view matrix (world-to-view).
/// * `cameraProj`  — The camera's perspective projection matrix.
///   Near/far planes are extracted from `cameraProj(2,2)` and `cameraProj(3,2)`.
/// * `lightDir`    — Normalized direction the light is shining **toward**
///   (e.g., for a sun at noon pointing down: `{0, -1, 0}`). Must not be zero.
/// * `config`      — Shadow configuration (cascade count, resolution, etc.).
/// * `outCascades` — Output array with at least `config.cascadeCount` elements.
///   Each element is fully written; no prior initialization is required.
///
/// # Panics
/// - `UZ_CHECK` if `config.cascadeCount` is 0 or exceeds `kMaxShadowCascades`.
/// - `UZ_CHECK` if `lightDir` is approximately zero-length.
///
/// # Shadow Shimmer Prevention
/// The orthographic projection for each cascade is snapped to shadow-map texel
/// boundaries so that sub-pixel camera movement does not cause shadow edges to
/// flicker. This is achieved by rounding the light-space AABB min/max to
/// multiples of the world-space texel size.
void ComputeCascades(
    const Mat4& cameraView,
    const Mat4& cameraProj,
    const Vec3& lightDir,
    const ShadowConfig& config,
    ShadowCascade* outCascades);

} // namespace uz
