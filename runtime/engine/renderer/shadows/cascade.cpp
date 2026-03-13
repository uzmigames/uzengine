/// UzEngine Cascaded Shadow Map — Frustum Splitting Implementation
///
/// Implements the practical split scheme from Nvidia GPU Gems 3, Ch. 10.
/// Each cascade gets a tight-fitting orthographic projection in light space,
/// snapped to texel boundaries to prevent shadow shimmer on camera movement.

#include "cascade.h"

#include "engine_assert.h"
#include "log.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Number of corners in a frustum (near plane 4 + far plane 4).
constexpr uint32_t kFrustumCornerCount = 8;

/// Threshold for detecting a near-vertical light direction.
///
/// When the absolute dot product of lightDir and world-up exceeds this,
/// we switch the LookAt up-vector to avoid a degenerate view matrix.
constexpr float kVerticalThreshold = 0.999f;

/// Extracts camera near and far planes from a left-handed perspective
/// projection matrix (DX convention, z in [0,1]).
///
/// For `XMMatrixPerspectiveFovLH`:
///   m(2,2) = far / (far - near)
///   m(3,2) = -near * far / (far - near)
///
/// Solving: near = -m(3,2) / m(2,2), far = m(3,2) / (m(2,2) - 1)
/// (Row indices are 0-based; DirectXMath stores row-major in XMFLOAT4X4.)
void ExtractNearFar(const Mat4& proj, float& outNear, float& outFar) {
    const float c22 = proj(2, 2);
    const float c32 = proj(3, 2);

    // Avoid division by zero for degenerate projections.
    if (std::abs(c22) < 1e-6f) {
        outNear = 0.1f;
        outFar = 1000.0f;
        UZ_LOG(Shadow, Warning, "Degenerate projection matrix — using fallback near={}, far={}", outNear, outFar);
        return;
    }

    outNear = -c32 / c22;
    outFar = -c32 / (c22 - 1.0f);
}

/// Computes the 8 world-space corners of a sub-frustum defined by
/// [splitNear, splitFar] within the camera's full frustum.
///
/// The approach: build NDC corners for the full clip volume (DX: z in [0,1]),
/// un-project them to world space via `inverse(viewProj)`, then interpolate
/// along each near-to-far ray to get the sub-frustum boundaries.
void ComputeFrustumCorners(
    const Mat4& cameraView,
    const Mat4& cameraProj,
    float splitNear,
    float splitFar,
    Vec3* outCorners) {

    // Inverse of the full view-projection to go from NDC -> world.
    const Mat4 invViewProj = (cameraView * cameraProj).Inverse();

    // NDC corners of the full clip volume (DX: x,y in [-1,1], z in [0,1]).
    // Order: 4 near-plane corners then 4 far-plane corners.
    static constexpr float kNdcCorners[kFrustumCornerCount][3] = {
        {-1.0f, -1.0f, 0.0f}, // near bottom-left
        { 1.0f, -1.0f, 0.0f}, // near bottom-right
        { 1.0f,  1.0f, 0.0f}, // near top-right
        {-1.0f,  1.0f, 0.0f}, // near top-left
        {-1.0f, -1.0f, 1.0f}, // far bottom-left
        { 1.0f, -1.0f, 1.0f}, // far bottom-right
        { 1.0f,  1.0f, 1.0f}, // far top-right
        {-1.0f,  1.0f, 1.0f}, // far top-left
    };

    // Un-project all 8 NDC corners to world space (perspective divide).
    Vec3 worldCorners[kFrustumCornerCount];
    for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
        Vec4 ndc{kNdcCorners[i][0], kNdcCorners[i][1], kNdcCorners[i][2], 1.0f};
        Vec4 world = invViewProj * ndc;
        // Perspective divide to recover Cartesian coordinates.
        const float invW = 1.0f / world.w;
        worldCorners[i] = Vec3{world.x * invW, world.y * invW, world.z * invW};
    }

    // Extract near/far of the full camera frustum from the projection.
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    ExtractNearFar(cameraProj, cameraNear, cameraFar);

    // Compute interpolation factors to remap the full frustum to [splitNear, splitFar].
    const float range = cameraFar - cameraNear;
    const float tNear = (splitNear - cameraNear) / range;
    const float tFar  = (splitFar  - cameraNear) / range;

    // Interpolate along each near-to-far ray.
    for (uint32_t i = 0; i < 4; ++i) {
        const Vec3& nearPt = worldCorners[i];
        const Vec3& farPt  = worldCorners[i + 4];
        const Vec3 ray = farPt - nearPt;

        outCorners[i]     = nearPt + ray * tNear; // sub-frustum near corner
        outCorners[i + 4] = nearPt + ray * tFar;  // sub-frustum far corner
    }
}

/// Chooses an up-vector for the light view matrix that avoids degeneracy
/// when the light direction is nearly parallel to world-up (0,1,0).
Vec3 ChooseLightUp(const Vec3& lightDir) {
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    if (std::abs(lightDir.Dot(worldUp)) > kVerticalThreshold) {
        return Vec3{1.0f, 0.0f, 0.0f};
    }
    return worldUp;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades
// ─────────────────────────────────────────────────────────────────────────────

void ComputeCascades(
    const Mat4& cameraView,
    const Mat4& cameraProj,
    const Vec3& lightDir,
    const ShadowConfig& config,
    ShadowCascade* outCascades) {

    UZ_CHECK(config.cascadeCount > 0 && config.cascadeCount <= kMaxShadowCascades);
    UZ_CHECK(lightDir.LengthSq() > 1e-6f);

    const Vec3 lightDirNorm = lightDir.Normalized();
    const Vec3 lightUp = ChooseLightUp(lightDirNorm);

    // --- Step 1: Extract camera near/far, clamp far to maxDistance ---

    float cameraNear = 0.0f;
    float cameraFar  = 0.0f;
    ExtractNearFar(cameraProj, cameraNear, cameraFar);

    const float shadowFar = std::min(cameraFar, config.maxDistance);
    const float shadowNear = cameraNear;

    // --- Step 2: Compute split distances (practical split scheme) ---

    const uint32_t N = config.cascadeCount;
    // N+1 split planes: splits[0] = near, splits[N] = far.
    float splits[kMaxShadowCascades + 1];
    splits[0] = shadowNear;
    splits[N] = shadowFar;

    for (uint32_t i = 1; i < N; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(N);

        // Logarithmic split: near * (far/near)^(i/N)
        const float logSplit = shadowNear * std::pow(shadowFar / shadowNear, fraction);

        // Linear split: near + (far - near) * (i/N)
        const float linearSplit = shadowNear + (shadowFar - shadowNear) * fraction;

        // Blend: lambda=1 is fully logarithmic, lambda=0 is fully linear.
        splits[i] = config.splitLambda * logSplit + (1.0f - config.splitLambda) * linearSplit;
    }

    // --- Step 3: Per-cascade light-space matrix construction ---

    for (uint32_t cascade = 0; cascade < N; ++cascade) {
        const float splitNear = splits[cascade];
        const float splitFar  = splits[cascade + 1];

        // 3a. Compute the 8 world-space corners of this cascade's sub-frustum.
        Vec3 corners[kFrustumCornerCount];
        ComputeFrustumCorners(cameraView, cameraProj, splitNear, splitFar, corners);

        // 3b. Compute the centroid of the 8 corners.
        Vec3 center = Vec3::Zero();
        for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
            center += corners[i];
        }
        center = center * (1.0f / static_cast<float>(kFrustumCornerCount));

        // 3c. Compute the radius of a bounding sphere around the centroid.
        //     Using a sphere (rather than tight AABB) makes the projection
        //     rotationally stable, further reducing shimmer on camera rotation.
        float radius = 0.0f;
        for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
            const float dist = (corners[i] - center).Length();
            radius = std::max(radius, dist);
        }
        // Round up to a small multiple to avoid jitter from floating-point drift.
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // 3d. Build the light view matrix.
        //     Position the light camera at `center - lightDir * radius` so the
        //     entire sub-frustum fits within the near-to-far range [0, 2*radius].
        const Vec3 lightEye = center - lightDirNorm * radius;
        const Mat4 lightView = Mat4::LookAt(lightEye, center, lightUp);

        // 3e. Build the orthographic projection from the bounding sphere.
        //     The sphere guarantees the AABB is [-radius, radius] in X and Y.
        const float orthoLeft   = -radius;
        const float orthoRight  =  radius;
        const float orthoBottom = -radius;
        const float orthoTop    =  radius;
        const float orthoNear   = 0.0f;
        const float orthoFar    = 2.0f * radius;

        // Use XMMatrixOrthographicOffCenterLH directly because Mat4::Orthographic
        // only supports centered projections (width, height), not arbitrary bounds.
        const Mat4 lightProj{XMMatrixOrthographicOffCenterLH(
            orthoLeft, orthoRight, orthoBottom, orthoTop, orthoNear, orthoFar)};

        // 3f. Snap to texel grid to prevent shadow shimmer.
        //     Compute world-space texel size, then round the light-space origin
        //     to the nearest texel boundary.
        const float worldUnitsPerTexel = (2.0f * radius) / static_cast<float>(config.resolution);

        Mat4 shadowMatrix = lightView * lightProj;

        // Transform the origin to shadow space, snap it, then adjust the matrix.
        Vec4 origin = shadowMatrix * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
        origin.x /= worldUnitsPerTexel;
        origin.y /= worldUnitsPerTexel;
        origin.x = std::floor(origin.x);
        origin.y = std::floor(origin.y);
        origin.x *= worldUnitsPerTexel;
        origin.y *= worldUnitsPerTexel;

        // Compute the rounding offset and apply as a translation to the projection.
        Vec4 currentOrigin = shadowMatrix * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
        const float offsetX = origin.x - currentOrigin.x;
        const float offsetY = origin.y - currentOrigin.y;

        // Apply the texel-snapping offset directly to the projection matrix's
        // translation row (row 3, columns 0 and 1 in row-major XMFLOAT4X4).
        Mat4 snappedProj = lightProj;
        snappedProj.m._41 += offsetX;
        snappedProj.m._42 += offsetY;

        // 3g. Store results.
        outCascades[cascade].viewProj  = lightView * snappedProj;
        outCascades[cascade].splitNear = splitNear;
        outCascades[cascade].splitFar  = splitFar;
        outCascades[cascade].texelSize = worldUnitsPerTexel;

    }
}

} // namespace uz
