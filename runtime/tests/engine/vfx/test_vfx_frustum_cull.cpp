/// Unit tests for VFX frustum culling (vfx_frustum_cull.h / vfx_frustum_cull.cpp).
///
/// Covers: FrustumPlanes extraction from identity and perspective matrices,
/// IsSphereInsideFrustum for inside/outside/partially-intersecting spheres,
/// ComputeEmitterBoundingSphereRadius formula verification,
/// EmitterCullState::Update hysteresis with various transition sequences.
///
/// No GPU hardware required — all tests are CPU-only and stateless.

#include <gtest/gtest.h>

#include "lod/vfx_frustum_cull.h"

#include <cmath>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a left-handed perspective projection matrix using Mat4::Perspective.
Mat4 MakePerspective(float fovY = 1.0472f, // ~60 degrees
                     float aspect = 16.0f / 9.0f,
                     float nearZ = 0.1f,
                     float farZ = 1000.0f) {
    return Mat4::Perspective(fovY, aspect, nearZ, farZ);
}

/// Builds a simple view matrix looking along +Z from the origin.
Mat4 MakeViewLookingForward() {
    return Mat4::LookAt(Vec3::Zero(), Vec3{0.0f, 0.0f, 1.0f}, Vec3::Up());
}

/// Builds a view-projection matrix: camera at origin looking along +Z.
Mat4 MakeDefaultVP() {
    return MakeViewLookingForward() * MakePerspective();
}

/// Tolerance for floating-point comparisons.
constexpr float kEpsilon = 1e-4f;

} // anonymous namespace

// =============================================================================
// ExtractFrustumPlanes — basic sanity
// =============================================================================

TEST(VFXFrustumCull, ExtractFromIdentity_SphereAtOriginIsInside) {
    // Identity VP: the "frustum" is the canonical clip cube.
    // A sphere at the origin with small radius should be inside.
    const FrustumPlanes frustum = ExtractFrustumPlanes(Mat4::Identity());
    EXPECT_TRUE(IsSphereInsideFrustum(frustum, Vec3::Zero(), 0.1f));
}

TEST(VFXFrustumCull, ExtractFromIdentity_PlanesAreNormalized) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(Mat4::Identity());
    for (int i = 0; i < 6; ++i) {
        const Vec4& p = frustum.planes[i];
        const float normalLen = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        EXPECT_NEAR(normalLen, 1.0f, kEpsilon)
            << "Plane " << i << " normal is not unit length";
    }
}

TEST(VFXFrustumCull, ExtractFromPerspective_PlanesAreNormalized) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    for (int i = 0; i < 6; ++i) {
        const Vec4& p = frustum.planes[i];
        const float normalLen = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        EXPECT_NEAR(normalLen, 1.0f, kEpsilon)
            << "Plane " << i << " normal is not unit length";
    }
}

// =============================================================================
// IsSphereInsideFrustum — visibility tests
// =============================================================================

TEST(VFXFrustumCull, SphereAtCenter_InsidePerspective) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    // Sphere at (0, 0, 10) — well inside a frustum looking along +Z.
    EXPECT_TRUE(IsSphereInsideFrustum(frustum, Vec3{0.0f, 0.0f, 10.0f}, 1.0f));
}

TEST(VFXFrustumCull, SphereBehindCamera_OutsidePerspective) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    // Sphere at (0, 0, -50) — behind the camera (looking along +Z).
    EXPECT_FALSE(IsSphereInsideFrustum(frustum, Vec3{0.0f, 0.0f, -50.0f}, 1.0f));
}

TEST(VFXFrustumCull, SphereFarToLeft_OutsidePerspective) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    // Sphere far to the left at distance 10 along Z.
    // With a 60-degree FOV and 16:9 aspect, the horizontal half-angle is roughly
    // atan(tan(30deg) * 16/9) ~ 43 degrees. At Z=10, the frustum width is ~9.3.
    // A sphere at X=-100 is well outside.
    EXPECT_FALSE(IsSphereInsideFrustum(frustum, Vec3{-100.0f, 0.0f, 10.0f}, 1.0f));
}

TEST(VFXFrustumCull, SphereFarToRight_OutsidePerspective) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    EXPECT_FALSE(IsSphereInsideFrustum(frustum, Vec3{100.0f, 0.0f, 10.0f}, 1.0f));
}

TEST(VFXFrustumCull, SphereFarAbove_OutsidePerspective) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    EXPECT_FALSE(IsSphereInsideFrustum(frustum, Vec3{0.0f, 100.0f, 10.0f}, 1.0f));
}

TEST(VFXFrustumCull, SphereFarBelow_OutsidePerspective) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    EXPECT_FALSE(IsSphereInsideFrustum(frustum, Vec3{0.0f, -100.0f, 10.0f}, 1.0f));
}

TEST(VFXFrustumCull, SphereBeyondFarPlane_OutsidePerspective) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    // Sphere well beyond the far plane (1000 units).
    EXPECT_FALSE(IsSphereInsideFrustum(frustum, Vec3{0.0f, 0.0f, 2000.0f}, 1.0f));
}

TEST(VFXFrustumCull, LargeSpherePartiallyInside_ReturnsTrue) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(MakeDefaultVP());
    // Sphere behind camera but with a huge radius that reaches into the frustum.
    // Center at Z=-5, radius=100 — extends from Z=-105 to Z=+95, overlapping the frustum.
    EXPECT_TRUE(IsSphereInsideFrustum(frustum, Vec3{0.0f, 0.0f, -5.0f}, 100.0f));
}

TEST(VFXFrustumCull, ZeroRadiusSphere_AtOrigin_InsideIdentity) {
    const FrustumPlanes frustum = ExtractFrustumPlanes(Mat4::Identity());
    // A point (zero-radius sphere) at the origin is inside the identity frustum.
    EXPECT_TRUE(IsSphereInsideFrustum(frustum, Vec3::Zero(), 0.0f));
}

// =============================================================================
// ComputeEmitterBoundingSphereRadius
// =============================================================================

TEST(VFXFrustumCull, BoundingSphereRadius_BasicMath) {
    // radius = spawnRadius + maxSpeed * maxLifetime
    // 5.0 + 10.0 * 2.0 = 25.0
    EXPECT_FLOAT_EQ(ComputeEmitterBoundingSphereRadius(5.0f, 10.0f, 2.0f), 25.0f);
}

TEST(VFXFrustumCull, BoundingSphereRadius_ZeroSpawnRadius) {
    // 0.0 + 50.0 * 3.0 = 150.0
    EXPECT_FLOAT_EQ(ComputeEmitterBoundingSphereRadius(0.0f, 50.0f, 3.0f), 150.0f);
}

TEST(VFXFrustumCull, BoundingSphereRadius_ZeroSpeed) {
    // 10.0 + 0.0 * 5.0 = 10.0
    EXPECT_FLOAT_EQ(ComputeEmitterBoundingSphereRadius(10.0f, 0.0f, 5.0f), 10.0f);
}

TEST(VFXFrustumCull, BoundingSphereRadius_ZeroLifetime) {
    // 10.0 + 50.0 * 0.0 = 10.0
    EXPECT_FLOAT_EQ(ComputeEmitterBoundingSphereRadius(10.0f, 50.0f, 0.0f), 10.0f);
}

TEST(VFXFrustumCull, BoundingSphereRadius_AllZero) {
    EXPECT_FLOAT_EQ(ComputeEmitterBoundingSphereRadius(0.0f, 0.0f, 0.0f), 0.0f);
}

// =============================================================================
// EmitterCullState::Update — hysteresis behavior
// =============================================================================

TEST(VFXFrustumCull, CullState_InitialState_AllFalseZero) {
    EmitterCullState state;
    EXPECT_FALSE(state.culledThisFrame);
    EXPECT_FALSE(state.culledPrevFrame);
    EXPECT_EQ(state.framesSinceCulled, 0u);
}

TEST(VFXFrustumCull, CullState_AlwaysVisible_AlwaysRenders) {
    EmitterCullState state;
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(state.Update(/*isInsideFrustum=*/true))
            << "Frame " << i << ": visible emitter should always render";
        EXPECT_EQ(state.framesSinceCulled, 0u);
    }
}

TEST(VFXFrustumCull, CullState_FirstCulledFrame_StillRenders) {
    EmitterCullState state;
    // Frame 0: visible
    EXPECT_TRUE(state.Update(true));
    // Frame 1: culled — but only 1 frame, hysteresis keeps it rendering.
    EXPECT_TRUE(state.Update(false));
}

TEST(VFXFrustumCull, CullState_TwoConsecutiveCulled_SuppressesRendering) {
    EmitterCullState state;
    // Frame 0: visible
    state.Update(true);
    // Frame 1: first culled frame — hysteresis, still renders
    EXPECT_TRUE(state.Update(false));
    // Frame 2: second consecutive culled frame — now suppressed
    EXPECT_FALSE(state.Update(false));
    EXPECT_EQ(state.framesSinceCulled, 1u);
}

TEST(VFXFrustumCull, CullState_CulledThenVisible_ResetsCounter) {
    EmitterCullState state;
    // Build up culled state
    state.Update(true);   // visible
    state.Update(false);  // first culled (hysteresis)
    state.Update(false);  // second culled (suppressed)
    EXPECT_EQ(state.framesSinceCulled, 1u);

    // Now visible again
    EXPECT_TRUE(state.Update(true));
    EXPECT_EQ(state.framesSinceCulled, 0u);
}

TEST(VFXFrustumCull, CullState_FramesSinceCulled_Increments) {
    EmitterCullState state;
    // Frame 0: visible
    state.Update(true);
    // Frame 1: first culled (hysteresis keeps rendering)
    state.Update(false);
    // Frames 2..5: consecutive culled frames
    for (uint32_t i = 1; i <= 4; ++i) {
        EXPECT_FALSE(state.Update(false));
        EXPECT_EQ(state.framesSinceCulled, i);
    }
}

TEST(VFXFrustumCull, CullState_FlickeringVisibility_NeverSuppresses) {
    EmitterCullState state;
    // Alternating visible/culled should never reach 2 consecutive culled frames.
    for (int i = 0; i < 10; ++i) {
        bool visible = (i % 2 == 0);
        EXPECT_TRUE(state.Update(visible))
            << "Frame " << i << ": flickering emitter should never be suppressed";
    }
}

TEST(VFXFrustumCull, CullState_CulledFromStart_SuppressesAfterTwoFrames) {
    EmitterCullState state;
    // Frame 0: culled (prevFrame was false=not culled initially, so hysteresis keeps it)
    EXPECT_TRUE(state.Update(false));
    // Frame 1: second consecutive culled frame — now suppressed
    EXPECT_FALSE(state.Update(false));
}
