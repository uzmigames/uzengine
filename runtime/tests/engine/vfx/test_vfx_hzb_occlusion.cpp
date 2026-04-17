/// Unit tests for phase 8.20.0 — VFX HZB occlusion culling.
///
/// Validates AABB projection, mip selection, occlusion compare, and the
/// hysteresis state machine on synthetic inputs.
///
/// @ref engine/vfx/lod/vfx_hzb_occlusion.h
/// @ref docs/engine/25-vfx/37-hzb-occlusion.md

#include <gtest/gtest.h>

#include "engine/vfx/lod/vfx_hzb_occlusion.h"

#include <array>
#include <cmath>

using namespace uz;

namespace {

/// Builds a row-major 4×4 perspective reverse-Z matrix — simple enough
/// to hand-check but correct for AABB projection tests.
///
/// Maps view-space z in [nearPlane, farPlane] to NDC z in [1, 0]
/// (reversed-Z: near → 1). Identity in x/y so we can dial AABBs directly.
std::array<float, 16> SimpleReverseZProj(float nearPlane, float farPlane) {
    const float f = farPlane;
    const float n = nearPlane;
    // Standard reversed-Z perspective (square FOV, 90°):
    //   [1  0          0          0 ]
    //   [0  1          0          0 ]
    //   [0  0   n/(n-f)       -nf/(n-f)]
    //   [0  0          1          0 ]
    std::array<float, 16> m{};
    m[0]  = 1.0f; m[5] = 1.0f;
    m[10] = n / (n - f);
    m[11] = -f * n / (n - f);
    m[14] = 1.0f;
    return m;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// AABB projection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxProjectAABB, AABBInFrontProducesNonDegenerateRect) {
    const auto m = SimpleReverseZProj(0.1f, 100.0f);
    VfxAABB aabb;
    aabb.min = Vec3{-1.0f, -1.0f, 5.0f};
    aabb.max = Vec3{ 1.0f,  1.0f, 6.0f};
    const auto rect = VfxProjectAABBToScreen(aabb, m.data());

    EXPECT_FALSE(rect.offscreen);
    EXPECT_LT(rect.uvMinX, rect.uvMaxX);
    EXPECT_LT(rect.uvMinY, rect.uvMaxY);
    // Nearest corner is at z=5; reversed-Z should be positive.
    EXPECT_GT(rect.nearestDepth, 0.0f);
}

TEST(VfxProjectAABB, AABBOffscreenMarkedOffscreen) {
    const auto m = SimpleReverseZProj(0.1f, 100.0f);
    // AABB far to the right of the frustum.
    VfxAABB aabb;
    aabb.min = Vec3{100.0f,  0.0f, 5.0f};
    aabb.max = Vec3{101.0f,  1.0f, 6.0f};
    const auto rect = VfxProjectAABBToScreen(aabb, m.data());
    EXPECT_TRUE(rect.offscreen);
}

TEST(VfxProjectAABB, AABBBehindCamera_ReturnsFullScreenSafeRect) {
    const auto m = SimpleReverseZProj(0.1f, 100.0f);
    VfxAABB aabb;
    aabb.min = Vec3{-1.0f, -1.0f, -5.0f};
    aabb.max = Vec3{ 1.0f,  1.0f, -4.0f};
    const auto rect = VfxProjectAABBToScreen(aabb, m.data());
    // Full screen, depth = 1, offscreen = false.
    EXPECT_FLOAT_EQ(rect.uvMinX, 0.0f);
    EXPECT_FLOAT_EQ(rect.uvMaxX, 1.0f);
    EXPECT_FLOAT_EQ(rect.uvMinY, 0.0f);
    EXPECT_FLOAT_EQ(rect.uvMaxY, 1.0f);
    EXPECT_FLOAT_EQ(rect.nearestDepth, 1.0f);
    EXPECT_FALSE(rect.offscreen);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mip selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSelectHZBMip, SmallRectSelectsLowMip) {
    VfxScreenRect rect;
    rect.uvMinX = 0.5f; rect.uvMaxX = 0.51f; // 0.01 UV = ~10 mip-0 texels at 1024
    rect.uvMinY = 0.5f; rect.uvMaxY = 0.51f;
    // 2·2^m ≥ 10 ⇒ m ≥ log2(5) ≈ 2.32 ⇒ m = 3.
    const uint32_t mip = VfxSelectHZBMip(rect, 1024, 1024, 10);
    EXPECT_EQ(mip, 3u);
}

TEST(VfxSelectHZBMip, FullScreenRectSelectsHighMip) {
    VfxScreenRect rect;
    rect.uvMinX = 0.0f; rect.uvMaxX = 1.0f;
    rect.uvMinY = 0.0f; rect.uvMaxY = 1.0f;
    // 1024 mip-0 texels, need 2·2^m ≥ 1024 ⇒ m = 9.
    const uint32_t mip = VfxSelectHZBMip(rect, 1024, 1024, 10);
    EXPECT_EQ(mip, 9u);
}

TEST(VfxSelectHZBMip, PinpointRectClampsToZero) {
    VfxScreenRect rect;
    rect.uvMinX = 0.5f; rect.uvMaxX = 0.5f;
    rect.uvMinY = 0.5f; rect.uvMaxY = 0.5f;
    EXPECT_EQ(VfxSelectHZBMip(rect, 1024, 1024, 10), 0u);
}

TEST(VfxSelectHZBMip, ClampedToMaxMip) {
    VfxScreenRect rect;
    rect.uvMinX = 0.0f; rect.uvMaxX = 1.0f;
    rect.uvMinY = 0.0f; rect.uvMaxY = 1.0f;
    // Only 3 mips available — result must clamp at 2 regardless of size.
    EXPECT_EQ(VfxSelectHZBMip(rect, 4096, 4096, 3), 2u);
}

TEST(VfxSelectHZBMip, ZeroMipCountReturnsZero) {
    VfxScreenRect rect;
    rect.uvMaxX = 1.0f; rect.uvMaxY = 1.0f;
    EXPECT_EQ(VfxSelectHZBMip(rect, 1024, 1024, 0), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Occlusion compare
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxIsOccluded, NearestBehindHZB_IsOccluded) {
    // Reversed-Z: HZB = 0.8 (close wall), emitter nearest = 0.3 (far).
    EXPECT_TRUE(VfxIsOccludedByHZB(0.3f, 0.8f));
}

TEST(VfxIsOccluded, NearestInFrontOfHZB_NotOccluded) {
    EXPECT_FALSE(VfxIsOccludedByHZB(0.9f, 0.3f));
}

TEST(VfxIsOccluded, EqualDepths_NotOccluded) {
    EXPECT_FALSE(VfxIsOccludedByHZB(0.5f, 0.5f));
}

TEST(VfxIsOccluded, EpsilonSlackPreventsSelfOcclusion) {
    // Nearest is just behind the HZB (within epsilon) → not occluded
    // (prevents self-occlusion on planar alignment).
    EXPECT_FALSE(VfxIsOccludedByHZB(0.49999f, 0.5f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Hysteresis
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOcclusionHysteresis, StartsVisible) {
    VfxOcclusionHysteresis h(3);
    EXPECT_FALSE(h.CommittedOccluded());
    EXPECT_EQ(h.Update(false), false);
}

TEST(VfxOcclusionHysteresis, FlipsAfterConfirmFrames) {
    VfxOcclusionHysteresis h(3);
    EXPECT_FALSE(h.Update(true));  // vote 1, still visible
    EXPECT_FALSE(h.Update(true));  // vote 2, still visible
    EXPECT_TRUE (h.Update(true));  // vote 3, flips to occluded
    EXPECT_TRUE (h.CommittedOccluded());
}

TEST(VfxOcclusionHysteresis, DissentResetsWhenMatchingVote) {
    VfxOcclusionHysteresis h(3);
    EXPECT_FALSE(h.Update(true));  // dissent 1
    EXPECT_FALSE(h.Update(true));  // dissent 2
    EXPECT_FALSE(h.Update(false)); // matches committed → reset
    EXPECT_EQ(h.DissentCount(), 0u);
    EXPECT_FALSE(h.Update(true));  // dissent restarts at 1
}

TEST(VfxOcclusionHysteresis, FlipBackRequiresThreeConsecutive) {
    VfxOcclusionHysteresis h(3);
    // Flip to occluded
    h.Update(true); h.Update(true); h.Update(true);
    EXPECT_TRUE(h.CommittedOccluded());

    // Two visible votes shouldn't flip back.
    EXPECT_TRUE(h.Update(false));
    EXPECT_TRUE(h.Update(false));
    // Third does.
    EXPECT_FALSE(h.Update(false));
}

TEST(VfxOcclusionHysteresis, ResetGoesBackToVisible) {
    VfxOcclusionHysteresis h(3);
    h.Update(true); h.Update(true); h.Update(true);
    ASSERT_TRUE(h.CommittedOccluded());
    h.Reset();
    EXPECT_FALSE(h.CommittedOccluded());
    EXPECT_EQ(h.DissentCount(), 0u);
}
