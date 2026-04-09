/// CPU unit tests for the author-placed light probe volume / irradiance grid.
///
/// These tests cover the data layout and helper math that does not require a
/// GPU device — `LightProbeVolumeComponent` defaults and accessors plus a CPU
/// reference implementation of the trilinear-blend SH evaluation that matches
/// the HLSL path in `deferred_lighting_ps.hlsl`.
///
/// # References
/// @ref Tatarchuk 2005 "Irradiance Volumes for Games"
/// @ref Greger 1998 "The Irradiance Volume"

#include "engine/ecs/environment/light_probe_volume_component.h"
#include "engine/renderer/gi/ddgi_system.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(LightProbeVolumeComponent, DefaultsMatchSpec) {
    LightProbeVolumeComponent v;
    EXPECT_EQ(v.gridDimensionsX, 8u);
    EXPECT_EQ(v.gridDimensionsY, 4u);
    EXPECT_EQ(v.gridDimensionsZ, 8u);
    EXPECT_TRUE(v.enabled);
    EXPECT_TRUE(v.needsRebuild);
    EXPECT_FALSE(v.autoFitScene);
    EXPECT_FLOAT_EQ(v.autoFitPadding, 100.0f);
    EXPECT_EQ(v.probeBufferSrv, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper math
// ─────────────────────────────────────────────────────────────────────────────

TEST(LightProbeVolumeComponent, ProbeCount) {
    LightProbeVolumeComponent v;
    EXPECT_EQ(GetProbeCount(v), 8u * 4u * 8u);

    v.gridDimensionsX = 16;
    v.gridDimensionsY = 6;
    v.gridDimensionsZ = 12;
    EXPECT_EQ(GetProbeCount(v), 16u * 6u * 12u);
}

TEST(LightProbeVolumeComponent, VolumeCenterAndSize) {
    LightProbeVolumeComponent v;
    v.extentMin = Vec3{-200.0f, 0.0f, -300.0f};
    v.extentMax = Vec3{ 200.0f, 400.0f, 300.0f};

    const Vec3 c = GetVolumeCenter(v);
    EXPECT_FLOAT_EQ(c.x,   0.0f);
    EXPECT_FLOAT_EQ(c.y, 200.0f);
    EXPECT_FLOAT_EQ(c.z,   0.0f);

    const Vec3 s = GetVolumeSize(v);
    EXPECT_FLOAT_EQ(s.x, 400.0f);
    EXPECT_FLOAT_EQ(s.y, 400.0f);
    EXPECT_FLOAT_EQ(s.z, 600.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU reference for the HLSL trilinear-blend path
// ─────────────────────────────────────────────────────────────────────────────
//
// Mirrors the per-corner blend in `deferred_lighting_ps.hlsl` using only
// scalar SH coefficients. We blend `kProbeFloat4PerProbe`-sized records of
// constant value so the expected output equals the trilinear weight applied
// to that constant.

namespace {

constexpr uint32_t kFloat4PerProbe = 7;

struct GridDesc {
    uint32_t dimX;
    uint32_t dimY;
    uint32_t dimZ;
    Vec3     originMin;
    Vec3     invSize;
};

float TrilinearBlendConstant(const GridDesc&  g,
                             const std::vector<float>& probeValuesL0R,
                             Vec3 worldPos) {
    // Map worldPos to [0, dim - 1] cell coordinates (matches HLSL path).
    auto saturate = [](float x) { return std::min(1.0f, std::max(0.0f, x)); };
    const float u = saturate((worldPos.x - g.originMin.x) * g.invSize.x);
    const float v = saturate((worldPos.y - g.originMin.y) * g.invSize.y);
    const float w = saturate((worldPos.z - g.originMin.z) * g.invSize.z);

    const float cx = u * std::max(static_cast<float>(g.dimX) - 1.0f, 0.0f);
    const float cy = v * std::max(static_cast<float>(g.dimY) - 1.0f, 0.0f);
    const float cz = w * std::max(static_cast<float>(g.dimZ) - 1.0f, 0.0f);

    const int x0 = static_cast<int>(std::floor(cx));
    const int y0 = static_cast<int>(std::floor(cy));
    const int z0 = static_cast<int>(std::floor(cz));
    const int x1 = std::min(x0 + 1, static_cast<int>(g.dimX) - 1);
    const int y1 = std::min(y0 + 1, static_cast<int>(g.dimY) - 1);
    const int z1 = std::min(z0 + 1, static_cast<int>(g.dimZ) - 1);

    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);
    const float fz = cz - static_cast<float>(z0);

    float blended = 0.0f;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const int ix = (corner & 1) ? x1 : x0;
        const int iy = (corner & 2) ? y1 : y0;
        const int iz = (corner & 4) ? z1 : z0;

        const float wx = (corner & 1) ? fx : (1.0f - fx);
        const float wy = (corner & 2) ? fy : (1.0f - fy);
        const float wz = (corner & 4) ? fz : (1.0f - fz);
        const float wt = wx * wy * wz;

        const uint32_t probeLinear =
            static_cast<uint32_t>(ix) +
            static_cast<uint32_t>(iy) * g.dimX +
            static_cast<uint32_t>(iz) * g.dimX * g.dimY;
        blended += wt * probeValuesL0R[probeLinear];
    }
    return blended;
}

}  // namespace

TEST(LightProbeVolumeTrilinear, UniformProbesReturnConstant) {
    // 4x2x4 grid with all probes equal to 0.5 — every interpolation must
    // return 0.5 regardless of position.
    GridDesc g{4, 2, 4,
               Vec3{0.0f, 0.0f, 0.0f},
               Vec3{1.0f / 300.0f, 1.0f / 100.0f, 1.0f / 300.0f}};
    std::vector<float> probes(g.dimX * g.dimY * g.dimZ, 0.5f);

    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{0.0f,   0.0f, 0.0f}),  0.5f, 1e-6f);
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{150.0f, 50.0f, 150.0f}), 0.5f, 1e-6f);
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{300.0f, 100.0f, 300.0f}), 0.5f, 1e-6f);
}

TEST(LightProbeVolumeTrilinear, EndpointSamplesProbeExactly) {
    // 2x2x2 grid; corner positions must return the corresponding corner probe.
    GridDesc g{2, 2, 2,
               Vec3{0.0f, 0.0f, 0.0f},
               Vec3{1.0f / 100.0f, 1.0f / 100.0f, 1.0f / 100.0f}};
    std::vector<float> probes = {
        0.0f, 1.0f, // (0,0,0), (1,0,0)
        2.0f, 3.0f, // (0,1,0), (1,1,0)
        4.0f, 5.0f, // (0,0,1), (1,0,1)
        6.0f, 7.0f, // (0,1,1), (1,1,1)
    };

    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{  0.0f,   0.0f,   0.0f}), 0.0f, 1e-6f);
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{100.0f,   0.0f,   0.0f}), 1.0f, 1e-6f);
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{  0.0f, 100.0f,   0.0f}), 2.0f, 1e-6f);
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{100.0f, 100.0f,   0.0f}), 3.0f, 1e-6f);
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{100.0f, 100.0f, 100.0f}), 7.0f, 1e-6f);
}

TEST(LightProbeVolumeTrilinear, MidpointAveragesAllCorners) {
    // 2x2x2 grid, midpoint must be the average of all 8 probe values.
    GridDesc g{2, 2, 2,
               Vec3{0.0f, 0.0f, 0.0f},
               Vec3{1.0f / 100.0f, 1.0f / 100.0f, 1.0f / 100.0f}};
    std::vector<float> probes = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    const float expected = (1.0f + 2.0f + 3.0f + 4.0f + 5.0f + 6.0f + 7.0f + 8.0f) / 8.0f;
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{50.0f, 50.0f, 50.0f}),
                expected, 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// DDGISystem — atlas dimensions, default state
// ─────────────────────────────────────────────────────────────────────────────

TEST(DDGISystem, AtlasDimensionsMatchSpec) {
    DDGISystem sys;
    // Defaults — kProbesPerAxis=16, irradiance probe size=6, depth size=16.
    // Layout: width  = probesPerAxis^2 * (size + 2)
    //         height = probesPerAxis   * (size + 2)
    EXPECT_EQ(sys.GetIrradianceAtlasWidth(),
              DDGISystem::kProbesPerAxis * DDGISystem::kProbesPerAxis
              * (DDGISystem::kIrradianceProbeSize + 2u));
    EXPECT_EQ(sys.GetIrradianceAtlasHeight(),
              DDGISystem::kProbesPerAxis
              * (DDGISystem::kIrradianceProbeSize + 2u));
    EXPECT_EQ(sys.GetDepthAtlasWidth(),
              DDGISystem::kProbesPerAxis * DDGISystem::kProbesPerAxis
              * (DDGISystem::kDepthProbeSize + 2u));
    EXPECT_EQ(sys.GetDepthAtlasHeight(),
              DDGISystem::kProbesPerAxis
              * (DDGISystem::kDepthProbeSize + 2u));
}

TEST(DDGISystem, DefaultStateIsUninitialized) {
    DDGISystem sys;
    EXPECT_FALSE(sys.IsInitialized());
    EXPECT_EQ(sys.GetIrradianceSrv(), UINT32_MAX);
    EXPECT_EQ(sys.GetIrradianceUav(), UINT32_MAX);
    EXPECT_EQ(sys.GetDepthSrv(),       UINT32_MAX);
    EXPECT_EQ(sys.GetDepthUav(),       UINT32_MAX);
    EXPECT_FALSE(sys.HasValidUavs());
}

TEST(DDGISystem, GridOriginAndSpacingAccessors) {
    DDGISystem sys;
    sys.SetGridOrigin(Vec3{100.0f, 200.0f, 300.0f});
    sys.SetProbeSpacing(50.0f);
    const Vec3 o = sys.GetGridOrigin();
    EXPECT_FLOAT_EQ(o.x, 100.0f);
    EXPECT_FLOAT_EQ(o.y, 200.0f);
    EXPECT_FLOAT_EQ(o.z, 300.0f);
    EXPECT_FLOAT_EQ(sys.GetProbeSpacing(), 50.0f);
}

TEST(DDGISystem, ProbeCountMatchesProbesPerAxisCubed) {
    EXPECT_EQ(DDGISystem::kTotalProbes,
              DDGISystem::kProbesPerAxis
              * DDGISystem::kProbesPerAxis
              * DDGISystem::kProbesPerAxis);
}

TEST(LightProbeVolumeTrilinear, OutsideVolumeClampsToBoundary) {
    // Sampling outside the AABB must clamp (saturate) to the closest face.
    GridDesc g{2, 2, 2,
               Vec3{0.0f, 0.0f, 0.0f},
               Vec3{1.0f / 100.0f, 1.0f / 100.0f, 1.0f / 100.0f}};
    std::vector<float> probes = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};

    // Far past the +X+Y+Z corner — must clamp to probe 7.
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{1000.0f, 1000.0f, 1000.0f}),
                7.0f, 1e-6f);
    // Far past the -X-Y-Z corner — must clamp to probe 0.
    EXPECT_NEAR(TrilinearBlendConstant(g, probes, Vec3{-1000.0f, -1000.0f, -1000.0f}),
                0.0f, 1e-6f);
}
