/// VFX Frustum Culling — implementation of plane extraction, sphere test, and cull state.

#include "vfx_frustum_cull.h"

#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ExtractFrustumPlanes — Gribb-Hartmann column extraction (row-vector convention)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Gribb & Hartmann 2001 "Fast Extraction of Viewing Frustum Planes"
///
/// In UzEngine's row-vector convention, clip = point * VP. The clip-space
/// coordinate c.x = dot(point, column_0(VP)), etc. The frustum half-space
/// conditions in clip space are:
///   -c.w <= c.x <= c.w   (left / right)
///   -c.w <= c.y <= c.w   (bottom / top)
///    0   <= c.z <= c.w   (near / far)   [DX convention: z in [0,1]]
///
/// Rearranging, each plane is a linear combination of two columns of VP.
/// We normalize each plane so that ||normal|| = 1 for metric distance tests.
FrustumPlanes ExtractFrustumPlanes(const Mat4& viewProj) noexcept {
    FrustumPlanes result;

    // Helper: extract a column from the row-major XMFLOAT4X4.
    // Column c = { m(0,c), m(1,c), m(2,c), m(3,c) }
    auto col = [&](int c) -> Vec4 {
        return Vec4{
            viewProj(0, c),
            viewProj(1, c),
            viewProj(2, c),
            viewProj(3, c),
        };
    };

    // Column 3 = the W column (used in all plane combinations).
    const Vec4 c0 = col(0);
    const Vec4 c1 = col(1);
    const Vec4 c2 = col(2);
    const Vec4 c3 = col(3);

    // Left:   c.w + c.x >= 0  →  col3 + col0
    // Right:  c.w - c.x >= 0  →  col3 - col0
    // Bottom: c.w + c.y >= 0  →  col3 + col1
    // Top:    c.w - c.y >= 0  →  col3 - col1
    // Near:   c.z >= 0        →  col2          (DX: z maps to [0,1])
    // Far:    c.w - c.z >= 0  →  col3 - col2
    result.planes[static_cast<uint8_t>(FrustumPlane::Left)]   = c3 + c0;
    result.planes[static_cast<uint8_t>(FrustumPlane::Right)]  = c3 - c0;
    result.planes[static_cast<uint8_t>(FrustumPlane::Bottom)] = c3 + c1;
    result.planes[static_cast<uint8_t>(FrustumPlane::Top)]    = c3 - c1;
    result.planes[static_cast<uint8_t>(FrustumPlane::Near)]   = c2;
    result.planes[static_cast<uint8_t>(FrustumPlane::Far)]    = c3 - c2;

    // Normalize each plane so that the normal (A,B,C) has unit length.
    // This makes the signed distance test produce correct metric distances.
    for (int i = 0; i < 6; ++i) {
        Vec4& p = result.planes[i];
        const float normalLength = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (normalLength > 1e-8f) {
            const float invLen = 1.0f / normalLength;
            p.x *= invLen;
            p.y *= invLen;
            p.z *= invLen;
            p.w *= invLen;
        }
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// IsSphereInsideFrustum — sphere vs 6 normalized planes
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Assarsson & Moller 2000 "Optimized View Frustum Culling Algorithms"
///
/// For each plane (A,B,C,D) with normalized normal, the signed distance from
/// the sphere center to the plane is `d = A*cx + B*cy + C*cz + D`.
/// If `d < -radius` for any plane, the sphere is entirely outside that
/// half-space and therefore outside the frustum.
bool IsSphereInsideFrustum(const FrustumPlanes& frustum,
                           const Vec3& center,
                           float radius) noexcept {
    for (int i = 0; i < 6; ++i) {
        const Vec4& p = frustum.planes[i];
        const float distance = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeEmitterBoundingSphereRadius
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Assarsson & Moller 2000 — conservative bounding sphere for particle cloud.
///
/// The worst-case particle displacement from the emitter center is bounded by
/// the spawn region radius (particles can start anywhere within it) plus the
/// maximum distance any single particle can travel (maxSpeed * maxLifetime).
float ComputeEmitterBoundingSphereRadius(float spawnRadius,
                                          float maxSpeed,
                                          float maxLifetime) noexcept {
    return spawnRadius + maxSpeed * maxLifetime;
}

// ─────────────────────────────────────────────────────────────────────────────
// EmitterCullState::Update — 2-frame hysteresis
// ─────────────────────────────────────────────────────────────────────────────

bool EmitterCullState::Update(bool isInsideFrustum) noexcept {
    culledPrevFrame = culledThisFrame;
    culledThisFrame = !isInsideFrustum;

    if (culledThisFrame && culledPrevFrame) {
        // Culled for 2 consecutive frames — suppress rendering.
        ++framesSinceCulled;
        return false;
    }

    if (!culledThisFrame) {
        // Visible this frame — reset counter, render.
        framesSinceCulled = 0;
        return true;
    }

    // Transition frame: culled this frame but was visible last frame (or vice versa).
    // Keep rendering to hide the pop.
    return true;
}

} // namespace uz
