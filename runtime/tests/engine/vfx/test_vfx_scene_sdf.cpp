/// Unit tests for phase 8.22.0 — VFX Scene SDF data interface.
///
/// Validates atlas descriptor, analytic SDF primitives, central-difference
/// gradient sampler, and collision push-out.
///
/// @ref engine/vfx/data_interfaces/vfx_scene_sdf_di.h
/// @ref docs/engine/25-vfx/39-scene-sdf.md

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_scene_sdf_di.h"

#include <cmath>
#include <cstring>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Atlas descriptor
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSceneSDFAtlas, DefaultUnbound) {
    VfxSceneSDFAtlasDesc d;
    EXPECT_FALSE(d.IsBound());
}

TEST(VfxSceneSDFAtlas, BoundWhenAllFieldsValid) {
    VfxSceneSDFAtlasDesc d;
    d.atlasSrvIndex = 3u;
    d.worldMin = Vec3{-8, -8, -8};
    d.worldMax = Vec3{ 8,  8,  8};
    d.resX = d.resY = d.resZ = 64;
    EXPECT_TRUE(d.IsBound());
}

TEST(VfxSceneSDFAtlas, DegenerateAABBUnbound) {
    VfxSceneSDFAtlasDesc d;
    d.atlasSrvIndex = 3u;
    d.worldMin = Vec3{0, 0, 0};
    d.worldMax = Vec3{0, 0, 0};
    d.resX = d.resY = d.resZ = 64;
    EXPECT_FALSE(d.IsBound());
}

TEST(VfxSceneSDFAtlas, VoxelSizeMatchesAABB) {
    VfxSceneSDFAtlasDesc d;
    d.atlasSrvIndex = 1u;
    d.worldMin = Vec3{-8, -8, -8};
    d.worldMax = Vec3{ 8,  8,  8};
    d.resX = 32; d.resY = 16; d.resZ = 8;
    const Vec3 v = VfxSceneSDFVoxelSize(d);
    EXPECT_FLOAT_EQ(v.x, 0.5f); // 16m / 32 voxels
    EXPECT_FLOAT_EQ(v.y, 1.0f); // 16m / 16
    EXPECT_FLOAT_EQ(v.z, 2.0f); // 16m / 8
}

// ─────────────────────────────────────────────────────────────────────────────
// Analytic SDF primitives
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSDFSphere, SurfaceIsZero) {
    EXPECT_NEAR(VfxSDFSphere(Vec3{1, 0, 0}, Vec3{0, 0, 0}, 1.0f), 0.0f, 1e-5f);
}

TEST(VfxSDFSphere, OutsidePositive) {
    EXPECT_NEAR(VfxSDFSphere(Vec3{2, 0, 0}, Vec3{0, 0, 0}, 1.0f), 1.0f, 1e-5f);
}

TEST(VfxSDFSphere, InsideNegative) {
    EXPECT_NEAR(VfxSDFSphere(Vec3{0, 0, 0}, Vec3{0, 0, 0}, 1.0f), -1.0f, 1e-5f);
}

TEST(VfxSDFBox, SurfaceIsZero) {
    EXPECT_NEAR(VfxSDFBox(Vec3{1, 0, 0}, Vec3{0, 0, 0}, Vec3{1, 1, 1}), 0.0f, 1e-5f);
}

TEST(VfxSDFBox, OutsideCorner_EquidistantToPythagoreanDist) {
    // Corner offset (2, 2, 2) from box with extents (1,1,1) — distance is
    // length((1, 1, 1)) = √3.
    const float d = VfxSDFBox(Vec3{2, 2, 2}, Vec3{0, 0, 0}, Vec3{1, 1, 1});
    EXPECT_NEAR(d, std::sqrt(3.0f), 1e-5f);
}

TEST(VfxSDFBox, InsideNegativeDistance) {
    const float d = VfxSDFBox(Vec3{0, 0, 0}, Vec3{0, 0, 0}, Vec3{1, 1, 1});
    EXPECT_LT(d, 0.0f);
    EXPECT_NEAR(d, -1.0f, 1e-5f); // center of a unit cube is 1 unit from each face
}

// ─────────────────────────────────────────────────────────────────────────────
// Central-difference gradient
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSceneSDFGradient, SphereGradientPointsOutward) {
    auto sphere = [](const Vec3& p) {
        return VfxSDFSphere(p, Vec3{0, 0, 0}, 1.0f);
    };
    // Sample just outside the unit sphere along +X.
    const Vec3 g = VfxSceneSDFGradient(sphere, Vec3{2.0f, 0.0f, 0.0f}, 1e-3f);
    // Gradient magnitude should be ~1 (SDF is unit-gradient everywhere
    // away from the center) and direction ~+X.
    const float mag = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
    EXPECT_NEAR(mag, 1.0f, 1e-3f);
    EXPECT_NEAR(g.x, 1.0f, 1e-3f);
    EXPECT_NEAR(g.y, 0.0f, 1e-3f);
    EXPECT_NEAR(g.z, 0.0f, 1e-3f);
}

TEST(VfxSceneSDFGradient, BoxGradientOnPositiveFace) {
    auto box = [](const Vec3& p) {
        return VfxSDFBox(p, Vec3{0, 0, 0}, Vec3{1, 1, 1});
    };
    // A point just outside the +Y face: gradient should be ~(0, 1, 0).
    const Vec3 g = VfxSceneSDFGradient(box, Vec3{0.0f, 2.0f, 0.0f}, 1e-3f);
    EXPECT_NEAR(g.x, 0.0f, 1e-3f);
    EXPECT_NEAR(g.y, 1.0f, 1e-3f);
    EXPECT_NEAR(g.z, 0.0f, 1e-3f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision response
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSceneSDFCollision, NoCollisionWhenDistanceExceedsRadius) {
    Vec3 pos{0, 0, 0}, vel{1, 0, 0};
    const Vec3 grad{1, 0, 0};
    EXPECT_FALSE(VfxSceneSDFResolveCollision(pos, vel, /*distance=*/1.0f,
                                                grad, /*radius=*/0.5f, 0.3f));
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(vel.x, 1.0f);
}

TEST(VfxSceneSDFCollision, PushesOutAlongGradient) {
    Vec3 pos{0, 0, 0}, vel{-1, 0, 0}; // moving into the wall
    const Vec3 grad{1, 0, 0};         // wall normal is +X
    const bool hit = VfxSceneSDFResolveCollision(pos, vel, /*distance=*/0.2f,
                                                    grad, /*radius=*/0.5f, 0.5f);
    EXPECT_TRUE(hit);
    // Push-out = radius - distance = 0.3 along +X.
    EXPECT_NEAR(pos.x, 0.3f, 1e-5f);
    // Velocity reflects: v - (1 + e)·(v·n)·n. v·n = -1, e = 0.5, so
    // new v.x = -1 - (1.5 * -1 * 1) = -1 + 1.5 = 0.5.
    EXPECT_NEAR(vel.x, 0.5f, 1e-5f);
}

TEST(VfxSceneSDFCollision, RestitutionZeroAbsorbs) {
    Vec3 pos{0, 0, 0}, vel{-1, 0, 0};
    const Vec3 grad{1, 0, 0};
    VfxSceneSDFResolveCollision(pos, vel, 0.0f, grad, 0.5f, 0.0f);
    // (1 + 0) * -1 * 1 = -1; new v.x = -1 - (-1) = 0 — slides along
    // the surface.
    EXPECT_NEAR(vel.x, 0.0f, 1e-5f);
}

TEST(VfxSceneSDFCollision, RestitutionOneFullyReflects) {
    Vec3 pos{0, 0, 0}, vel{-1, 0, 0};
    const Vec3 grad{1, 0, 0};
    VfxSceneSDFResolveCollision(pos, vel, 0.0f, grad, 0.5f, 1.0f);
    // (1 + 1) * -1 * 1 = -2; new v.x = -1 - (-2) = 1 — full bounce.
    EXPECT_NEAR(vel.x, 1.0f, 1e-5f);
}

TEST(VfxSceneSDFCollision, VelocityAwayFromSurfaceUnchanged) {
    // Particle is inside the surface but moving away — push out but
    // don't flip the already-escaping velocity.
    Vec3 pos{0, 0, 0}, vel{1, 0, 0}; // +X, same direction as normal
    const Vec3 grad{1, 0, 0};
    VfxSceneSDFResolveCollision(pos, vel, 0.0f, grad, 0.5f, 0.8f);
    EXPECT_FLOAT_EQ(vel.x, 1.0f); // unmodified
}

TEST(VfxSceneSDFCollision, ZeroGradientFallsBackToZAxis) {
    Vec3 pos{0, 0, 0}, vel{0, 0, -1};
    const Vec3 grad{0, 0, 0}; // degenerate
    const bool hit = VfxSceneSDFResolveCollision(pos, vel, 0.0f, grad, 0.5f, 0.5f);
    EXPECT_TRUE(hit);
    // Fallback normal is +Z, push-out 0.5 along +Z.
    EXPECT_FLOAT_EQ(pos.z, 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL snippet
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSceneSDFSnippet, DeclaresSampleAndGradient) {
    const char* s = VfxSceneSDFEmitHLSLSnippet();
    ASSERT_NE(s, nullptr);
    EXPECT_NE(std::strstr(s, "SampleSceneSDF"), nullptr);
    EXPECT_NE(std::strstr(s, "GradientSceneSDF"), nullptr);
    EXPECT_NE(std::strstr(s, "ResourceDescriptorHeap"), nullptr);
    EXPECT_NE(std::strstr(s, "SampleLevel"), nullptr);
}

TEST(VfxSceneSDFSnippet, OutOfBoundsGuardedWithLargeDistance) {
    // Out-of-atlas samples return a big positive number so the caller
    // treats them as far-from-any-surface rather than zero (which would
    // look like on-surface).
    const char* s = VfxSceneSDFEmitHLSLSnippet();
    EXPECT_NE(std::strstr(s, "1e9"), nullptr);
}
