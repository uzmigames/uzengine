/// Unit tests for phase 7.10.0 - static mesh surface sampling DI.
///
/// Locks the CPU reference that the HLSL sampler port must match.
/// Covers:
///   - Uniform barycentric helper (sums to 1, lies inside triangle).
///   - Area-weighted CDF construction (normalised, ends at 1.0).
///   - Empty / degenerate mesh returns empty sample without crashing.
///   - Sample position lies on the triangle plane (barycentric interior).
///   - Normal interpolation re-normalises.
///   - Uniform RNG input visits triangles ~proportional to area.
///
/// @ref phase7_7.10.0-vfx-static-mesh-sampling-di

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_static_mesh_di.h"

#include <cmath>
#include <vector>

using namespace uz;

namespace {

/// Build a mesh with one large triangle and one tiny triangle so area-
/// weighted sampling is easy to verify (big tri should be picked
/// proportionally more often).
VfxStaticMeshDI MakeTwoTriangleMesh() {
    VfxStaticMeshDI m;
    // Big triangle (area 50 in XY plane).
    m.positions.push_back({0.0f, 0.0f, 0.0f});
    m.positions.push_back({10.0f, 0.0f, 0.0f});
    m.positions.push_back({0.0f, 10.0f, 0.0f});
    // Tiny triangle (area 0.5 in XY plane, offset far along +Z).
    m.positions.push_back({100.0f, 0.0f, 100.0f});
    m.positions.push_back({101.0f, 0.0f, 100.0f});
    m.positions.push_back({100.0f, 1.0f, 100.0f});

    m.normals.resize(6, Vec3{0.0f, 0.0f, 1.0f});
    m.uvs.resize(6, Vec2{0.0f, 0.0f});
    m.indices = {0, 1, 2, 3, 4, 5};
    return m;
}

} // namespace

// ─── Uniform barycentric helper ────────────────────────────────────────────

TEST(VfxUniformBarycentric, SumsToOne) {
    for (float u : {0.0f, 0.25f, 0.5f, 0.9f}) {
        for (float v : {0.0f, 0.3f, 0.7f, 0.99f}) {
            const auto b = VfxUniformBarycentric(u, v);
            EXPECT_NEAR(b.a + b.b + b.c, 1.0f, 1e-4f) << "u=" << u << " v=" << v;
        }
    }
}

TEST(VfxUniformBarycentric, AllWeightsNonNegative) {
    const auto b = VfxUniformBarycentric(0.5f, 0.5f);
    EXPECT_GE(b.a, 0.0f);
    EXPECT_GE(b.b, 0.0f);
    EXPECT_GE(b.c, 0.0f);
}

TEST(VfxUniformBarycentric, ClampsOutOfRangeInputs) {
    // Inputs outside [0, 1) should clamp, not crash.
    const auto b = VfxUniformBarycentric(-1.0f, 2.0f);
    EXPECT_GE(b.a, 0.0f);
    EXPECT_LE(b.a + b.b + b.c, 1.0f + 1e-3f);
}

// ─── CDF construction ──────────────────────────────────────────────────────

TEST(VfxStaticMeshDI, BuildAreaCdfHandlesEmptyMesh) {
    VfxStaticMeshDI m;
    m.BuildAreaCdf();
    EXPECT_EQ(m.GetTriangleCount(), 0u);
    EXPECT_FLOAT_EQ(m.totalArea, 0.0f);
    EXPECT_TRUE(m.triangleAreaCdf.empty());
}

TEST(VfxStaticMeshDI, BuildAreaCdfProducesMonotonicNormalisedTable) {
    auto m = MakeTwoTriangleMesh();
    m.BuildAreaCdf();
    ASSERT_EQ(m.triangleAreaCdf.size(), 2u);
    EXPECT_GT(m.triangleAreaCdf[0], 0.0f);
    EXPECT_LT(m.triangleAreaCdf[0], 1.0f);
    EXPECT_FLOAT_EQ(m.triangleAreaCdf[1], 1.0f);
    EXPECT_GT(m.triangleAreaCdf[1], m.triangleAreaCdf[0]);
    EXPECT_NEAR(m.totalArea, 50.0f + 0.5f, 1e-3f);
}

// ─── Degenerate and defensive paths ────────────────────────────────────────

TEST(VfxStaticMeshDI, EmptyMeshReturnsEmptySample) {
    VfxStaticMeshDI m;
    m.BuildAreaCdf();
    const auto s = m.SampleSurface(0.5f, 0.5f, 0.5f);
    // Default-constructed sample should be all zeros.
    EXPECT_FLOAT_EQ(s.position.x, 0.0f);
    EXPECT_FLOAT_EQ(s.position.y, 0.0f);
    EXPECT_FLOAT_EQ(s.position.z, 0.0f);
}

TEST(VfxStaticMeshDI, ZeroAreaMeshReturnsEmptySample) {
    // Three coincident vertices -> zero-area triangle.
    VfxStaticMeshDI m;
    m.positions = {Vec3{1.0f, 1.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    m.normals.assign(3, Vec3{0.0f, 0.0f, 1.0f});
    m.uvs.assign(3, Vec2{});
    m.indices = {0, 1, 2};
    m.BuildAreaCdf();

    EXPECT_FLOAT_EQ(m.totalArea, 0.0f);
    const auto s = m.SampleSurface(0.5f, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(s.position.x, 0.0f);  // default sample
}

// ─── Sample stays on the mesh ──────────────────────────────────────────────

TEST(VfxStaticMeshDI, SampleLandsInsideChosenTriangle) {
    auto m = MakeTwoTriangleMesh();
    m.BuildAreaCdf();

    // rng0 in the big triangle's CDF range.
    const auto s = m.SampleSurface(0.01f, 0.3f, 0.6f);
    EXPECT_EQ(s.triangleIndex, 0u);
    // Big triangle lies in z=0 plane.
    EXPECT_NEAR(s.position.z, 0.0f, 1e-3f);
    // Inside the triangle: x in [0, 10], y in [0, 10], x + y <= 10.
    EXPECT_GE(s.position.x, 0.0f);
    EXPECT_GE(s.position.y, 0.0f);
    EXPECT_LE(s.position.x + s.position.y, 10.0f + 1e-3f);
}

TEST(VfxStaticMeshDI, NormalIsUnitLength) {
    auto m = MakeTwoTriangleMesh();
    m.BuildAreaCdf();
    const auto s = m.SampleSurface(0.5f, 0.5f, 0.5f);
    const float len = std::sqrt(s.normal.x * s.normal.x +
                                 s.normal.y * s.normal.y +
                                 s.normal.z * s.normal.z);
    EXPECT_NEAR(len, 1.0f, 1e-3f);
}

// ─── Area weighting statistical check ─────────────────────────────────────

TEST(VfxStaticMeshDI, TrianglePickProportionalToArea) {
    auto m = MakeTwoTriangleMesh();
    m.BuildAreaCdf();

    // 1000 samples with deterministic RNG (linear sweep of rng0). Expect
    // the large triangle (area 50 / total 50.5 ≈ 99%) to be picked vastly
    // more often than the small one.
    uint32_t hitsBig = 0;
    uint32_t hitsSmall = 0;
    const uint32_t kSamples = 1000;
    for (uint32_t i = 0; i < kSamples; ++i) {
        const float r = static_cast<float>(i) / static_cast<float>(kSamples);
        const auto s = m.SampleSurface(r, 0.5f, 0.5f);
        if (s.triangleIndex == 0) ++hitsBig;
        else if (s.triangleIndex == 1) ++hitsSmall;
    }
    // Big triangle has ~99% of the area — expect ~990/1000 ± some tolerance.
    EXPECT_GT(hitsBig, 950u);
    EXPECT_LT(hitsSmall, 50u);
}
