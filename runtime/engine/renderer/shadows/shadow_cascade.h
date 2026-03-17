/// UzEngine Cascaded Shadow Map — UE5 Exact Parity
///
/// Computes cascade split distances and per-cascade light-space view-projection
/// matrices for Cascaded Shadow Maps (CSM). Uses UE5's exponential accumulation
/// split scheme (DirectionalLightComponent.cpp:753-814) with configurable
/// distribution exponent (default 3.0).
///
/// # Algorithm Overview
/// 1. Extract camera near/far from the projection matrix.
/// 2. Compute N+1 split planes using UE5 `ComputeAccumulatedScale(exponent, i, N)`.
/// 3. For each cascade, compute the 8 frustum corners of the sub-frustum,
///    build an optimal bounding sphere (UE5 diagonal-based center offset),
///    construct an orthographic projection snapped to texel boundaries.
/// 4. Extend non-last cascade splitFar for blending overlap (CPU-side).
/// 5. Compute per-cascade bias scaled by world texel size.
///
/// # Thread Safety
/// All functions are stateless and thread-safe. No global or mutable state.
///
/// # Coordinate System
/// Left-handed, DX clip space (z in [0, 1]).
///
/// # UE5 Reference
/// All parameters and formulas sourced from UE5 5.6 at W:\UE_5.6_Source.
/// See proposal in phase0-0.1.0-csm-ue5-parity for exact file:line references.

#pragma once

#include "vector3.h"
#include "mat4.h"
#include "log.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Hard limit on the total number of shadow cascades (main + far).
///
/// GPU constant buffer layouts and shader arrays are sized to this value.
/// Requesting more cascades than this is a programming error.
/// Supports up to 4 main cascades + 4 far shadow cascades.
/// Source: UE5 DLC.h:112 (FarShadowCascadeCount), DLC.cpp:1023 (FarShadowDistance).
constexpr uint32_t kMaxShadowCascades = 8;

/// Maximum number of main (near) shadow cascades.
///
/// Far shadow cascades extend beyond this limit using `farShadowCascadeCount`.
constexpr uint32_t kMaxMainCascades = 4;

/// Maximum number of far shadow cascades.
constexpr uint32_t kMaxFarCascades = 4;

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
    /// For non-last cascades, this is EXTENDED by cascadeTransitionFraction to
    /// create real geometric overlap for cascade blending.
    float splitFar = 0.0f;

    /// Original (un-extended) far split distance.
    /// Used as the blend start point in the shader. Blending happens between
    /// [splitFarFadeStart, splitFar] where splitFar is the extended value.
    float splitFarFadeStart = 0.0f;

    /// World-space size of one shadow map texel along the largest cascade axis.
    ///
    /// Used to scale depth/normal bias proportionally to the cascade's
    /// coverage area, so bias is consistent across all cascades.
    float texelSize = 0.0f;

    /// Per-cascade depth bias, computed from global bias scaled by
    /// cascade bias distribution and world texel size.
    /// Source: UE5 ShadowSetup.cpp:1844-1846.
    float computedDepthBias = 0.0f;

    /// Per-cascade normal bias, scaled proportionally to texel size.
    float computedNormalBias = 0.0f;

    /// World-space center of the cascade bounding sphere.
    ///
    /// Computed from the optimal offset along the frustum near-to-far axis.
    /// Used by CSM scrolling to determine overlap between old and new cascade bounds.
    /// Corresponds to UE5's negative PreShadowTranslation.
    /// @source UE5 ShadowSetup.cpp:5612-5613 — PreShadowTranslation
    Vec3 center;

    /// True if this cascade is a far shadow cascade (extended range, lower LOD).
    /// Far cascades cover the distance beyond the main CSM `maxDistance` up to
    /// `farShadowDistance`. Renderers can use this flag to select coarser mesh LODs.
    /// Source: UE5 DLC.cpp:68 (FarShadowLODBias).
    bool bFarShadowCascade = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShadowConfig — cascade configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the cascaded shadow map system — UE5 exact parity.
///
/// All distances are in world-space centimeters (UE5 convention).
/// All default values match UE5 5.6 DirectionalLightComponent defaults.
struct ShadowConfig {
    /// Number of cascades to compute (1 to kMaxShadowCascades).
    /// UE5 default: 4 (DynamicShadowCascades). Source: DLC.cpp:1019.
    uint32_t cascadeCount = 4;

    /// Shadow map resolution per cascade in texels (width = height).
    /// UE5 default: 2048. Source: r.Shadow.MaxCSMResolution.
    uint32_t resolution = 2048;

    /// Maximum shadow distance from the camera in cm.
    /// UE5 default: 40000 cm (400m). Source: DLC.cpp:1017.
    float maxDistance = 40000.0f;

    /// Cascade distribution exponent — controls how splits are distributed.
    /// Higher values concentrate more resolution near the camera.
    /// UE5 default: 3.0 (range 0.1–10.0). Source: DLC.cpp:1029.
    ///
    /// Replaces the old `splitLambda` parameter. UE5 uses an exponential
    /// accumulation formula, NOT the Nvidia GPU Gems lerp(log,linear) scheme.
    float cascadeDistributionExponent = 3.0f;

    /// Cascade transition blending fraction.
    /// Fraction of each cascade's range used for blending with the next cascade.
    /// Non-last cascades extend their splitFar by this fraction for overlap.
    /// UE5 default: 0.1 (10%), range [0.001, 0.3]. Source: DLC.cpp:1030.
    float cascadeTransitionFraction = 0.1f;

    /// Shadow distance fadeout fraction — applied to the LAST cascade only.
    /// Fades shadows to nothing at max distance instead of hard cutoff.
    /// UE5 default: 0.1 (10%). Source: DLC.cpp:1031.
    float shadowDistanceFadeoutFraction = 0.1f;

    /// Base normal-direction bias in cm, scaled per cascade by texel size.
    /// Applied as world-space offset along surface normal before shadow sampling.
    float normalBias = 2.0f;

    /// CSM constant depth bias (UE5-style, normalized by depth range).
    /// UE5 default: 10.0. Source: ShadowRendering.cpp:32.
    /// Applied as: bias = csmDepthBias / (maxSubjectZ - minSubjectZ).
    float csmDepthBias = 10.0f;

    /// CSM slope-scaled depth bias.
    /// UE5 default: 3.0. Source: ShadowRendering.cpp:38.
    /// Applied via rasterizer state SlopeScaledDepthBias in shadow PSO.
    float slopeScaleDepthBias = 3.0f;

    /// Cascade bias distribution — blends between constant and world-scaled bias.
    /// 0.0 = pure constant bias. 1.0 = fully world-space-scaled per cascade.
    /// UE5 default: 1.0. Source: ShadowSetup.cpp:1844-1846.
    float cascadeBiasDistribution = 1.0f;

    /// Shadow receiver bias for shadow sampling (reduces acne).
    /// UE5 default: 0.9 (range 0.0–1.0). Source: ShadowRendering.cpp:76.
    float receiverBias = 0.9f;

    /// Shadow filter method. 0 = 5x5 PCF grid (25 taps, default).
    /// 1 = 16-tap Poisson disk with per-pixel rotation (softer, less aliased).
    /// 2 = PCSS (blocker search + penumbra estimation + adaptive PCF).
    /// UE5: r.Shadow.FilterMethod (0=PCF, 1=PCSS). Source: ShadowRendering.cpp:180-186.
    uint32_t filterMethod = 0;

    /// Maximum PCSS soft kernel size in texels.
    /// Controls the search radius for blocker search and caps the PCF filter radius.
    /// UE5 default: 40.0. Source: ShadowRendering.cpp:188-192.
    float maxSoftKernelSize = 40.0f;

    // ── Far Shadow Cascades — UE5 parity ────────────────────────────────

    /// Maximum far shadow distance from the camera in cm.
    /// Far cascades cover the range [maxDistance, farShadowDistance] using
    /// additional cascade tiers with coarser mesh LODs.
    /// UE5 default: 300000 cm (3km). Source: DLC.cpp:1023.
    /// Only used when `farShadowCascadeCount > 0`.
    float farShadowDistance = 300000.0f;

    /// Number of far shadow cascades to compute (0 to kMaxFarCascades).
    /// 0 = far shadows disabled (default — backward compatible).
    /// Far cascades are appended after the main cascades in the output array.
    /// Total cascade count = cascadeCount + farShadowCascadeCount.
    /// Source: UE5 DLC.h:112 (FarShadowCascadeCount).
    uint32_t farShadowCascadeCount = 0;

    /// LOD bias applied to meshes rendered into far shadow cascades.
    /// Positive values select coarser mesh LODs, reducing GPU cost for the
    /// extended range where per-texel resolution is much lower.
    /// UE5 default: 0. Source: DLC.cpp:68 (FarShadowLODBias).
    int32_t farShadowLodBias = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — main entry point
// ─────────────────────────────────────────────────────────────────────────────

/// Computes cascade split distances and light-space matrices for CSM.
///
/// When `config.farShadowCascadeCount > 0`, far cascades are appended after
/// the main cascades in `outCascades`. The total number of cascades written
/// is returned via `outTotalCascadeCount`.
///
/// # Arguments
/// * `cameraView`  — The camera's view matrix (world-to-view).
/// * `cameraProj`  — The camera's perspective projection matrix.
///   Near/far planes are extracted from `cameraProj(2,2)` and `cameraProj(3,2)`.
/// * `lightDir`    — Normalized direction the light is shining **toward**
///   (e.g., for a sun at noon pointing down: `{0, -1, 0}`). Must not be zero.
/// * `config`      — Shadow configuration (cascade count, resolution, etc.).
/// * `outCascades` — Output array with at least
///   `config.cascadeCount + config.farShadowCascadeCount` elements.
///   Each element is fully written; no prior initialization is required.
/// * `outTotalCascadeCount` — Receives the total number of cascades written
///   (main + far). Must not be null.
///
/// # Panics
/// - `UZ_CHECK` if `config.cascadeCount` is 0 or exceeds `kMaxMainCascades`.
/// - `UZ_CHECK` if total cascade count exceeds `kMaxShadowCascades`.
/// - `UZ_CHECK` if `lightDir` is approximately zero-length.
///
/// # Shadow Shimmer Prevention
/// The orthographic projection for each cascade is snapped to shadow-map texel
/// boundaries so that sub-pixel camera movement does not cause shadow edges to
/// flicker. This is achieved by rounding the light-space AABB min/max to
/// multiples of the world-space texel size.
///
/// # Backward Compatibility
/// When `config.farShadowCascadeCount == 0` (default), this function behaves
/// identically to the previous version — `outTotalCascadeCount` equals
/// `config.cascadeCount` and no far cascades are produced.
void ComputeCascades(
    const Mat4& cameraView,
    const Mat4& cameraProj,
    const Vec3& lightDir,
    const ShadowConfig& config,
    ShadowCascade* outCascades,
    uint32_t* outTotalCascadeCount);

} // namespace uz
