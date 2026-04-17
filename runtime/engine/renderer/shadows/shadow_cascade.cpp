/// UzEngine Cascaded Shadow Map — Standard Rendering Parity Implementation
///
/// All formulas match reference 5.6 source. See phase0-0.1.0-csm-ue5-parity for
/// exact file:line references.

#include "shadow_cascade.h"

#include "engine_assert.h"
#include "log.h"

#include <algorithm>
#include <cmath>

namespace uz {
using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Number of corners in a frustum (near plane 4 + far plane 4).
constexpr uint32_t kFrustumCornerCount = 8;

/// Threshold for detecting a near-vertical light direction.
constexpr float kVerticalThreshold = 0.999f;

/// Texel snap factor for shadow map stabilization.
/// Snaps the cascade projection to integer texel boundaries so shadows
/// don't shimmer when the camera moves. A factor of 1.0 snaps to
/// individual texels (most stable). Higher values coarsen the grid.
/// @ref Valient 2014 "Deferred Rendering in Killzone" — CSM texel snapping
constexpr float kMaxDownsampleFactor = 1.0f;

/// Extracts camera near and far planes from a left-handed perspective
/// projection matrix (DX convention, z in [0,1]).
///
/// For `XMMatrixPerspectiveFovLH`:
///   m(2,2) = far / (far - near)
///   m(3,2) = -near * far / (far - near)
///
/// Solving: near = -m(3,2) / m(2,2), far = m(3,2) / (m(2,2) - 1)
void ExtractNearFar(const Mat4& proj, float& outNear, float& outFar) {
    const float c22 = proj(2, 2);
    const float c32 = proj(3, 2);

    if (std::abs(c22) < 1e-6f) {
        outNear = 0.1f;
        outFar = 1000.0f;
        UZ_LOG(Shadow, Warning, "Degenerate projection matrix — using fallback near={}, far={}", outNear, outFar);
        return;
    }

    // Camera uses reversed-Z perspective: m22 = -near/(far-near), m32 = near*far/(far-near).
    // With reversed-Z: -c32/c22 = far, -c32/(c22-1) = near.
    outFar  = -c32 / c22;
    outNear = -c32 / (c22 - 1.0f);

    // DEBUG: log raw matrix coefficients and extracted values
    static bool loggedExtract = false;
    if (!loggedExtract) {
        UZ_LOG(Shadow, Info, "ExtractNearFar: c22={:.6f} c32={:.6f} -> outNear={:.2f} outFar={:.2f}",
               c22, c32, outNear, outFar);
        loggedExtract = true;
    }
}

/// Computes the 8 world-space corners of a sub-frustum defined by
/// [splitNear, splitFar] within the camera's full frustum.
void ComputeFrustumCorners(
    const Mat4& cameraView,
    const Mat4& cameraProj,
    float splitNear,
    float splitFar,
    Vec3* outCorners) {

    const Mat4 invViewProj = (cameraView * cameraProj).Inverse();

    // NDC corners of the full clip volume.
    // Reversed-Z (standard): near plane = z=1.0, far plane = z=0.0.
    // Standard DX would be near=0, far=1 — but Mat4::Perspective() outputs reversed-Z.
    static constexpr float kNdcCorners[kFrustumCornerCount][3] = {
        {-1.0f, -1.0f, 1.0f}, // near bottom-left  (z=1 = near in reversed-Z)
        { 1.0f, -1.0f, 1.0f}, // near bottom-right
        { 1.0f,  1.0f, 1.0f}, // near top-right
        {-1.0f,  1.0f, 1.0f}, // near top-left
        {-1.0f, -1.0f, 0.0f}, // far bottom-left   (z=0 = far in reversed-Z)
        { 1.0f, -1.0f, 0.0f}, // far bottom-right
        { 1.0f,  1.0f, 0.0f}, // far top-right
        {-1.0f,  1.0f, 0.0f}, // far top-left
    };

    // Transform NDC corners to world space using DirectXMath SIMD directly,
    // bypassing Vec4 construction overhead and using SIMD reciprocal for
    // the perspective divide.
    Vec3 worldCorners[kFrustumCornerCount];
    const XMMATRIX invVP = invViewProj.toXM();
    for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
        const XMVECTOR ndc = XMVectorSet(kNdcCorners[i][0], kNdcCorners[i][1], kNdcCorners[i][2], 1.0f);
        const XMVECTOR world = XMVector4Transform(ndc, invVP);
        const XMVECTOR invW = XMVectorReciprocal(XMVectorSplatW(world));
        const XMVECTOR proj = XMVectorMultiply(world, invW);
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&worldCorners[i]), proj);
    }

    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    ExtractNearFar(cameraProj, cameraNear, cameraFar);

    const float range = cameraFar - cameraNear;
    const float tNear = (splitNear - cameraNear) / range;
    const float tFar  = (splitFar  - cameraNear) / range;

    for (uint32_t i = 0; i < 4; ++i) {
        const Vec3& nearPt = worldCorners[i];
        const Vec3& farPt  = worldCorners[i + 4];
        const Vec3 ray = farPt - nearPt;

        outCorners[i]     = nearPt + ray * tNear;
        outCorners[i + 4] = nearPt + ray * tFar;
    }
}

/// Chooses an up-vector for the light view matrix that avoids degeneracy
/// when the light direction is nearly parallel to world-up.
/// UzEngine uses Z-up coordinate system (standard).
Vec3 ChooseLightUp(const Vec3& lightDir) {
    const Vec3 worldUp{0.0f, 0.0f, 1.0f};  // Z-up
    if (std::abs(lightDir.Dot(worldUp)) > kVerticalThreshold) {
        return Vec3{1.0f, 0.0f, 0.0f};
    }
    return worldUp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Exponential Accumulation Split Scheme
// Source: DirectionalLightComponent.cpp:753-776
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the accumulated scale for a cascade split using the reference engine's exponential
/// distribution formula.
///
/// The formula iterates `currentScale *= exponent` for each cascade,
/// accumulates, and normalizes by the total. This produces a distribution
/// where near cascades get more resolution than far cascades, controlled
/// by the exponent value.
///
/// # Arguments
/// * `exponent`     — Distribution exponent (Default: 3.0).
/// * `splitIndex`   — Which split boundary (0 to cascadeCount).
/// * `cascadeCount` — Total number of cascades.
///
/// # Returns
/// Normalized fraction in [0, 1] indicating where this split falls.
float ComputeAccumulatedScale(float exponent, uint32_t splitIndex, uint32_t cascadeCount) {
    float currentScale = 1.0f;
    float accumulated = 0.0f;
    float totalScale = 0.0f;

    for (uint32_t i = 0; i < cascadeCount; ++i) {
        if (i < splitIndex) {
            accumulated += currentScale;
        }
        totalScale += currentScale;
        currentScale *= exponent;
    }

    if (totalScale < 1e-6f) {
        return static_cast<float>(splitIndex) / static_cast<float>(cascadeCount);
    }

    return accumulated / totalScale;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — Reference Parity
// ─────────────────────────────────────────────────────────────────────────────

void ComputeCascades(
    const Mat4& cameraView,
    const Mat4& cameraProj,
    const Vec3& lightDir,
    const ShadowConfig& config,
    ShadowCascade* outCascades,
    uint32_t* outTotalCascadeCount) {

    UZ_CHECK(config.cascadeCount > 0 && config.cascadeCount <= kMaxMainCascades);
    UZ_CHECK(config.farShadowCascadeCount <= kMaxFarCascades);
    UZ_CHECK((config.cascadeCount + config.farShadowCascadeCount) <= kMaxShadowCascades);
    UZ_CHECK(lightDir.LengthSq() > 1e-6f);
    UZ_CHECK(outTotalCascadeCount != nullptr);

    const Vec3 lightDirNorm = lightDir.Normalized();
    const Vec3 lightUp = ChooseLightUp(lightDirNorm);

    // --- Step 1: Extract camera near/far, clamp far to maxDistance ---

    float cameraNear = 0.0f;
    float cameraFar  = 0.0f;
    ExtractNearFar(cameraProj, cameraNear, cameraFar);

    const float shadowFar = std::min(cameraFar, config.maxDistance);
    const float shadowNear = cameraNear;
    const float shadowRange = shadowFar - shadowNear;

    // --- Step 2: Compute split distances (reference exponential accumulation) ---
    // Source: DirectionalLightComponent.cpp:784-814

    const uint32_t N = config.cascadeCount;
    float splits[kMaxMainCascades + 1];
    splits[0] = shadowNear;

    for (uint32_t i = 1; i <= N; ++i) {
        const float scale = ComputeAccumulatedScale(
            config.cascadeDistributionExponent, i, N);
        splits[i] = shadowNear + scale * shadowRange;
    }

    // --- Step 3: Per-cascade light-space matrix construction ---

    for (uint32_t cascade = 0; cascade < N; ++cascade) {
        const float splitNear = splits[cascade];
        const float splitFar  = splits[cascade + 1];

        // Store original far for blend start, then extend for overlap.
        float extendedFar = splitFar;
        if (cascade < N - 1) {
            // Extend non-last cascades for cascade blending overlap.
            // Source: DirectionalLightComponent.cpp:921-966
            const float fadeExtension = (splitFar - splitNear) * config.cascadeTransitionFraction;
            extendedFar = splitFar + fadeExtension;
        }

        // 3a. Compute frustum corners using the EXTENDED far for matrix construction.
        //     This ensures the shadow map actually covers the blend region.
        Vec3 corners[kFrustumCornerCount];
        ComputeFrustumCorners(cameraView, cameraProj, splitNear, extendedFar, corners);

        // 3b. Compute optimal bounding sphere center (reference diagonal offset).
        // Source: DirectionalLightComponent.cpp:880
        //
        // Instead of using the centroid of the 8 corners (which wastes shadow map
        // space), reference computes an optimal Z-offset along the view axis that
        // minimizes the bounding sphere radius.
        //
        // DiagonalASq = length² of near-plane diagonal
        // DiagonalBSq = length² of far-plane diagonal
        // OptimalOffset = (DiagonalBSq - DiagonalASq) / (2 * frustumLength) + frustumLength * 0.5
        const Vec3 nearDiag = corners[2] - corners[0]; // near top-right - near bottom-left
        const Vec3 farDiag  = corners[6] - corners[4]; // far top-right  - far bottom-left
        const float diagASq = nearDiag.Dot(nearDiag);
        const float diagBSq = farDiag.Dot(farDiag);
        const float frustumLength = extendedFar - splitNear;

        float optimalOffset = frustumLength * 0.5f;
        if (frustumLength > 1e-6f) {
            optimalOffset = (diagBSq - diagASq) / (2.0f * frustumLength) + frustumLength * 0.5f;
            optimalOffset = std::clamp(optimalOffset, 0.0f, frustumLength);
        }

        // Center along the view ray — interpolate between near and far centroids
        Vec3 nearCenter = Vec3::Zero();
        Vec3 farCenter  = Vec3::Zero();
        for (uint32_t i = 0; i < 4; ++i) {
            nearCenter += corners[i];
            farCenter  += corners[i + 4];
        }
        nearCenter = nearCenter * 0.25f;
        farCenter  = farCenter  * 0.25f;

        // The center is positioned at optimalOffset along the near-to-far axis
        const float t = (frustumLength > 1e-6f) ? (optimalOffset / frustumLength) : 0.5f;
        const Vec3 center = nearCenter + (farCenter - nearCenter) * t;

        // 3c. Compute the radius of the bounding sphere around the optimal center.
        // Uses distance-squared comparison to avoid 8 sqrt calls — only one sqrt
        // at the end to convert the maximum squared distance to the actual radius.
        const XMVECTOR vCenter = center.toXM();
        XMVECTOR vMaxDistSq = XMVectorZero();
        for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
            const XMVECTOR diff = XMVectorSubtract(corners[i].toXM(), vCenter);
            const XMVECTOR distSq = XMVector3Dot(diff, diff);
            vMaxDistSq = XMVectorMax(vMaxDistSq, distSq);
        }
        float radius = std::sqrt(XMVectorGetX(vMaxDistSq));
        // Equivalent: clamp minimum radius to 1.0 to avoid division by zero.
        // Source: DirectionalLightComponent.cpp:890
        radius = std::max(radius, 1.0f);

        // 3d. Build the light view matrix.
        //
        // Caster pullback: the light eye is pulled back `radius * kCasterPullbackScale`
        // (NOT just `radius`) so that shadow casters that lie BETWEEN the sun
        // and the frustum's bounding sphere — but are themselves outside the
        // sphere — are still rasterized into the shadow map.
        //
        // The previous value (`radius * 1`) produced a shadow volume that
        // ENDED at the back of the frustum sphere. Walls behind the camera
        // or above it (e.g. Sponza's exterior arcade walls) were excluded
        // from the shadow atlas, so the sun leaked through them and lit
        // interior geometry — the "sun follows the camera indoors" bug
        // (Sponza, 2026-04-10). Rotating the camera moved the sphere and
        // therefore the set of included casters, causing the indoor lighting
        // to flicker between dark and bright as different walls entered/left
        // the caster volume.
        //
        // Value 8.0 is a practical default: it multiplies the rendered
        // depth range by ~8x, slightly reducing depth precision but still
        // well within 24-bit shadow map accuracy for scenes up to a few
        // hundred meters. Future: replace with a per-frame scene-AABB-
        // driven CastersBox (UE5 ShadowSetup.cpp pattern).
        // @ref UE5 ShadowSetup.cpp — FShadowCascadeSettings::ShadowBoundsAccurate
        const float kCasterPullbackScale = 8.0f;
        const float pullback = radius * kCasterPullbackScale;
        const Vec3  lightEye = center - lightDirNorm * pullback;
        const Mat4  lightView = Mat4::LookAt(lightEye, center, lightUp);

        // 3e. Build the orthographic projection.
        //
        // Shadow maps use STANDARD-Z (0=near, 1=far) while main view uses
        // reversed-Z. XMMatrixOrthographicOffCenterLH naturally produces
        // standard-Z; the shadow comparison sampler (SCF_Less) and clear
        // value (kShadowDepthClear=1.0) are tuned for this space.
        //
        // `orthoFar` must extend from the pulled-back eye to the far side
        // of the frustum sphere, i.e. `pullback + radius`, so that the
        // whole sphere is inside the ortho volume AND everything between
        // the pulled-back eye and the sphere is a valid caster.
        const float orthoNear = 0.0f;
        const float orthoFar  = pullback + radius;

        const Mat4 lightProj{XMMatrixOrthographicOffCenterLH(
            -radius, radius, -radius, radius, orthoNear, orthoFar)};

        // 3f. Snap to texel grid to prevent shadow shimmer.
        // uses MaxDownsampleFactor=4 for a coarser snap grid.
        // Source: ShadowSetup.cpp:1020-1036
        const float snapGrid = 2.0f * kMaxDownsampleFactor / static_cast<float>(config.resolution);

        Mat4 shadowVP = lightView * lightProj;

        shadowVP.m._41 = std::floor(shadowVP.m._41 / snapGrid) * snapGrid;
        shadowVP.m._42 = std::floor(shadowVP.m._42 / snapGrid) * snapGrid;

        // 3g. Compute per-cascade bias.
        // Source: ShadowSetup.cpp:1844-1846
        const float worldUnitsPerTexel = (2.0f * radius) / static_cast<float>(config.resolution);
        const float depthRange = orthoFar - orthoNear;
        const float constantBias = (depthRange > 1e-6f) ? (config.csmDepthBias / depthRange) : 0.0f;
        const float worldScaledBias = constantBias * worldUnitsPerTexel;

        // Lerp between constant and world-scaled bias based on distribution setting
        const float cascadeDepthBias = constantBias * (1.0f - config.cascadeBiasDistribution)
                                     + worldScaledBias * config.cascadeBiasDistribution;

        // Normal bias: floor of config.normalBias (cm), but NEVER less than
        // ~1.5 texels of the current cascade. Far cascades have enormous texels
        // (cm/texel grows linearly with frustum radius), so a fixed 2 cm normal
        // bias is far smaller than one texel and causes shadow acne over the
        // entire far-cascade footprint. Scaling with texel size eliminates it.
        const float finalNormalBias = std::max(config.normalBias, worldUnitsPerTexel * 1.5f);

        // 3h. Store results.
        outCascades[cascade].viewProj           = shadowVP;
        outCascades[cascade].splitNear          = splitNear;
        outCascades[cascade].splitFar           = extendedFar;
        outCascades[cascade].splitFarFadeStart  = splitFar; // original, un-extended
        outCascades[cascade].texelSize          = worldUnitsPerTexel;
        outCascades[cascade].computedDepthBias  = cascadeDepthBias;
        outCascades[cascade].computedNormalBias = finalNormalBias;
        outCascades[cascade].center             = center;
        outCascades[cascade].bFarShadowCascade  = false;
    }

    // --- Step 4: Far shadow cascades (standard) ---
    // Source: DLC.cpp:1023 (FarShadowDistance), DLC.h:112 (FarShadowCascadeCount)
    //
    // Far cascades extend shadow coverage beyond maxDistance up to farShadowDistance.
    // They use the same split formula and matrix construction as main cascades,
    // just over a different distance range. The renderer can apply farShadowLodBias
    // to select coarser mesh LODs for these cascades.

    const uint32_t farN = config.farShadowCascadeCount;
    const float farStart = shadowFar;
    const float farEnd = config.farShadowDistance;
    const float kMinFarRange = 100.0f; // Minimum range in cm to justify far cascades

    if (farN > 0 && (farEnd - farStart) > kMinFarRange) {
        float farSplits[kMaxFarCascades + 1];
        farSplits[0] = farStart;
        const float farRange = farEnd - farStart;

        for (uint32_t i = 1; i <= farN; ++i) {
            const float scale = ComputeAccumulatedScale(
                config.cascadeDistributionExponent, i, farN);
            farSplits[i] = farStart + scale * farRange;
        }

        for (uint32_t fc = 0; fc < farN; ++fc) {
            const uint32_t outIdx = N + fc;
            const float fSplitNear = farSplits[fc];
            const float fSplitFar  = farSplits[fc + 1];

            // Extend non-last far cascades for cascade blending overlap
            float fExtendedFar = fSplitFar;
            if (fc < farN - 1) {
                const float fadeExtension = (fSplitFar - fSplitNear) * config.cascadeTransitionFraction;
                fExtendedFar = fSplitFar + fadeExtension;
            }

            // Compute frustum corners using the extended far for matrix construction
            Vec3 corners[kFrustumCornerCount];
            ComputeFrustumCorners(cameraView, cameraProj, fSplitNear, fExtendedFar, corners);

            // Optimal bounding sphere center (same algorithm as main cascades)
            const Vec3 nearDiag = corners[2] - corners[0];
            const Vec3 farDiag  = corners[6] - corners[4];
            const float diagASq = nearDiag.Dot(nearDiag);
            const float diagBSq = farDiag.Dot(farDiag);
            const float frustumLength = fExtendedFar - fSplitNear;

            float optimalOffset = frustumLength * 0.5f;
            if (frustumLength > 1e-6f) {
                optimalOffset = (diagBSq - diagASq) / (2.0f * frustumLength) + frustumLength * 0.5f;
                optimalOffset = std::clamp(optimalOffset, 0.0f, frustumLength);
            }

            Vec3 nearCenter = Vec3::Zero();
            Vec3 farCenter  = Vec3::Zero();
            for (uint32_t i = 0; i < 4; ++i) {
                nearCenter += corners[i];
                farCenter  += corners[i + 4];
            }
            nearCenter = nearCenter * 0.25f;
            farCenter  = farCenter  * 0.25f;

            const float t = (frustumLength > 1e-6f) ? (optimalOffset / frustumLength) : 0.5f;
            const Vec3 center = nearCenter + (farCenter - nearCenter) * t;

            // Distance-squared comparison avoids 8 sqrt calls — single sqrt at the end.
            const XMVECTOR vFarCenter = center.toXM();
            XMVECTOR vFarMaxDistSq = XMVectorZero();
            for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
                const XMVECTOR diff = XMVectorSubtract(corners[i].toXM(), vFarCenter);
                const XMVECTOR distSq = XMVector3Dot(diff, diff);
                vFarMaxDistSq = XMVectorMax(vFarMaxDistSq, distSq);
            }
            float radius = std::sqrt(XMVectorGetX(vFarMaxDistSq));
            radius = std::max(radius, 1.0f);

            const Vec3 lightEye = center - lightDirNorm * radius;
            const Mat4 lightView = Mat4::LookAt(lightEye, center, lightUp);

            const float orthoNear = 0.0f;
            const float orthoFar  = 2.0f * radius;

            const Mat4 lightProj{XMMatrixOrthographicOffCenterLH(
                -radius, radius, -radius, radius, orthoNear, orthoFar)};

            // Texel snap grid (same formula as main cascades)
            const float snapGrid = 2.0f * kMaxDownsampleFactor / static_cast<float>(config.resolution);

            Mat4 shadowVP = lightView * lightProj;
            shadowVP.m._41 = std::floor(shadowVP.m._41 / snapGrid) * snapGrid;
            shadowVP.m._42 = std::floor(shadowVP.m._42 / snapGrid) * snapGrid;

            // Per-cascade bias (same formula as main cascades)
            const float worldUnitsPerTexel = (2.0f * radius) / static_cast<float>(config.resolution);
            const float depthRange = orthoFar - orthoNear;
            const float constantBias = (depthRange > 1e-6f) ? (config.csmDepthBias / depthRange) : 0.0f;
            const float worldScaledBias = constantBias * worldUnitsPerTexel;
            const float cascadeDepthBias = constantBias * (1.0f - config.cascadeBiasDistribution)
                                         + worldScaledBias * config.cascadeBiasDistribution;
            const float normalBiasScale = std::min(worldUnitsPerTexel * 0.01f, 2.0f);
            const float finalNormalBias = config.normalBias + config.normalBias * normalBiasScale;

            outCascades[outIdx].viewProj           = shadowVP;
            outCascades[outIdx].splitNear          = fSplitNear;
            outCascades[outIdx].splitFar           = fExtendedFar;
            outCascades[outIdx].splitFarFadeStart  = fSplitFar;
            outCascades[outIdx].texelSize          = worldUnitsPerTexel;
            outCascades[outIdx].computedDepthBias  = cascadeDepthBias;
            outCascades[outIdx].computedNormalBias = finalNormalBias;
            outCascades[outIdx].center             = center;
            outCascades[outIdx].bFarShadowCascade  = true;
        }

        *outTotalCascadeCount = N + farN;
    } else {
        *outTotalCascadeCount = N;
    }
}

} // namespace uz
