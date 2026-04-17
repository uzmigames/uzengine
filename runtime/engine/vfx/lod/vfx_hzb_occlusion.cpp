#include "vfx_hzb_occlusion.h"

#include <algorithm>
#include <cmath>

namespace uz {

namespace {

/// Row-major 4×4 matrix × float4. Mirrors the VS-side transform.
Vec4 Mul(const float m[16], const Vec4& v) noexcept {
    return Vec4{
        m[ 0]*v.x + m[ 1]*v.y + m[ 2]*v.z + m[ 3]*v.w,
        m[ 4]*v.x + m[ 5]*v.y + m[ 6]*v.z + m[ 7]*v.w,
        m[ 8]*v.x + m[ 9]*v.y + m[10]*v.z + m[11]*v.w,
        m[12]*v.x + m[13]*v.y + m[14]*v.z + m[15]*v.w,
    };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// AABB projection
// ─────────────────────────────────────────────────────────────────────────────

VfxScreenRect VfxProjectAABBToScreen(const VfxAABB& aabb,
                                       const float viewProj[16]) noexcept {
    const Vec3 corners[8] = {
        {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
        {aabb.max.x, aabb.max.y, aabb.max.z},
    };

    float uvMinX =  1e9f, uvMinY =  1e9f;
    float uvMaxX = -1e9f, uvMaxY = -1e9f;
    float nearestDepth = 0.0f; // reversed-Z: start at "far"

    for (int i = 0; i < 8; ++i) {
        const Vec4 cs = Mul(viewProj, Vec4{corners[i].x, corners[i].y, corners[i].z, 1.0f});
        // Any negative w means a corner is behind the near plane — the
        // AABB straddles the camera so the projection is unreliable.
        // Return a full-screen rect at max depth so the emitter is
        // treated as visible.
        if (cs.w <= 1e-5f) {
            VfxScreenRect rect;
            rect.uvMinX = 0.0f; rect.uvMinY = 0.0f;
            rect.uvMaxX = 1.0f; rect.uvMaxY = 1.0f;
            rect.nearestDepth = 1.0f;
            rect.offscreen = false;
            return rect;
        }
        const float invW = 1.0f / cs.w;
        const float ndcX = cs.x * invW;
        const float ndcY = cs.y * invW;
        const float ndcZ = cs.z * invW; // reversed-Z already in [0, 1]

        // NDC → UV: DirectX convention flips Y so V grows downward.
        const float uvX = ndcX * 0.5f + 0.5f;
        const float uvY = ndcY * -0.5f + 0.5f;

        uvMinX = std::min(uvMinX, uvX);
        uvMinY = std::min(uvMinY, uvY);
        uvMaxX = std::max(uvMaxX, uvX);
        uvMaxY = std::max(uvMaxY, uvY);
        nearestDepth = std::max(nearestDepth, ndcZ);
    }

    const bool offscreen = (uvMaxX < 0.0f) || (uvMinX > 1.0f)
                          || (uvMaxY < 0.0f) || (uvMinY > 1.0f);

    VfxScreenRect rect;
    rect.uvMinX = std::clamp(uvMinX, 0.0f, 1.0f);
    rect.uvMinY = std::clamp(uvMinY, 0.0f, 1.0f);
    rect.uvMaxX = std::clamp(uvMaxX, 0.0f, 1.0f);
    rect.uvMaxY = std::clamp(uvMaxY, 0.0f, 1.0f);
    rect.nearestDepth = std::clamp(nearestDepth, 0.0f, 1.0f);
    rect.offscreen    = offscreen;
    return rect;
}

// ─────────────────────────────────────────────────────────────────────────────
// HZB mip selection
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxSelectHZBMip(const VfxScreenRect& rect,
                           uint32_t baseWidth,
                           uint32_t baseHeight,
                           uint32_t mipCount) noexcept {
    if (mipCount == 0u) return 0u;

    const float rectWUV = std::max(0.0f, rect.uvMaxX - rect.uvMinX);
    const float rectHUV = std::max(0.0f, rect.uvMaxY - rect.uvMinY);

    // Rect size in mip-0 texels.
    const float rectWTx = rectWUV * static_cast<float>(baseWidth);
    const float rectHTx = rectHUV * static_cast<float>(baseHeight);
    const float rectMaxTx = std::max(rectWTx, rectHTx);

    if (rectMaxTx < 1e-4f) return 0u; // pinpoint — mip 0 is fine

    // A 2x2 footprint in mip `m` covers `2 · 2^m` base texels. Smallest
    // `m` satisfying `2 · 2^m ≥ rectMaxTx` ⇒ `m ≥ log2(rectMaxTx / 2)`.
    const float required = std::log2(std::max(rectMaxTx * 0.5f, 1.0f));
    const int   mip      = static_cast<int>(std::ceil(required));
    const int   clamped  = std::clamp(mip, 0, static_cast<int>(mipCount) - 1);
    return static_cast<uint32_t>(clamped);
}

// ─────────────────────────────────────────────────────────────────────────────
// Occlusion test
// ─────────────────────────────────────────────────────────────────────────────

bool VfxIsOccludedByHZB(float nearestDepth, float hzbDepth, float epsilon) noexcept {
    // Reversed-Z: closer = larger. Occluded when the nearest point of
    // the emitter is strictly behind the HZB's farthest depth.
    return nearestDepth + epsilon < hzbDepth;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hysteresis
// ─────────────────────────────────────────────────────────────────────────────

bool VfxOcclusionHysteresis::Update(bool rawOccludedVote) noexcept {
    if (rawOccludedVote == committedOccluded_) {
        dissentCount_ = 0u;
        return committedOccluded_;
    }
    ++dissentCount_;
    if (dissentCount_ >= confirmFrames_) {
        committedOccluded_ = rawOccludedVote;
        dissentCount_      = 0u;
    }
    return committedOccluded_;
}

} // namespace uz
