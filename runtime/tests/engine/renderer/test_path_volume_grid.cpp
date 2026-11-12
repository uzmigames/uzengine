#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "volume_asset.h"
#include "../../../shaders/runtime/pathtracing/pathtracer_volume.hlsli"

// The heterogeneous grid medium: .uzvol round trip, the built-in generators,
// the shader-equivalent trilinear sampler, and the Woodcock/ratio estimators
// on a sampled grid against midpoint quadrature of the same density.

TEST(PathVolumeGrid, UzVolRoundTripAndValidation) {
    for (auto format : {uz::VolumeFormat::R8, uz::VolumeFormat::R16F}) {
        const auto source = uz::VolumeAsset::GenerateSphereVolume(12, .8f, .75f, format);
        ASSERT_TRUE(source.IsValid());
        const auto bytes = source.Serialize();
        ASSERT_EQ(bytes.size(), sizeof(uz::UzVolHeader) + source.Data().size());
        EXPECT_EQ(std::memcmp(bytes.data(), "UZVOL\0\0\0", 8), 0);
        uz::VolumeAsset loaded;
        std::string error;
        ASSERT_TRUE(loaded.Load(bytes, &error)) << error;
        EXPECT_EQ(loaded.Width(), 12u);
        EXPECT_EQ(loaded.Height(), 12u);
        EXPECT_EQ(loaded.Depth(), 12u);
        EXPECT_EQ(loaded.Format(), format);
        EXPECT_EQ(loaded.Data(), source.Data());
        EXPECT_FLOAT_EQ(loaded.GetMaxDensity(), source.GetMaxDensity());
        for (uint32_t z = 0; z < 12; ++z)
            for (uint32_t y = 0; y < 12; ++y)
                for (uint32_t x = 0; x < 12; ++x)
                    EXPECT_FLOAT_EQ(loaded.Voxel(x, y, z), source.Voxel(x, y, z));
    }
    const auto good = uz::VolumeAsset::GenerateConstantVolume(4, 1.f).Serialize();
    uz::VolumeAsset asset;
    std::string error;
    auto corrupt = good; corrupt[0] = 'X';
    EXPECT_FALSE(asset.Load(corrupt, &error));
    EXPECT_FALSE(asset.IsValid());
    auto truncated = good; truncated.pop_back();
    EXPECT_FALSE(asset.Load(truncated, &error));
    auto huge = good; uint32_t edge = 257; std::memcpy(huge.data() + 12, &edge, 4);
    EXPECT_FALSE(asset.Load(huge, &error));
    auto badFormat = good; uint32_t format = 7; std::memcpy(badFormat.data() + 24, &format, 4);
    EXPECT_FALSE(asset.Load(badFormat, &error));
    EXPECT_TRUE(asset.Load(good, &error)) << error;
}

TEST(PathVolumeGrid, GeneratorsHaveExpectedMajorantsAndShapes) {
    const auto constant = uz::VolumeAsset::GenerateConstantVolume(16, 1.f);
    EXPECT_FLOAT_EQ(constant.GetMaxDensity(), 1.f);
    EXPECT_FLOAT_EQ(constant.Sample(.123f, .987f, .5f), 1.f);

    const auto sphere = uz::VolumeAsset::GenerateSphereVolume(32, .9f, 1.f);
    EXPECT_FLOAT_EQ(sphere.GetMaxDensity(), 1.f);
    EXPECT_FLOAT_EQ(sphere.Voxel(16, 16, 16), 1.f);
    EXPECT_FLOAT_EQ(sphere.Voxel(0, 0, 0), 0.f);
    EXPECT_FLOAT_EQ(sphere.Sample(.5f, .5f, .5f), 1.f);
    EXPECT_FLOAT_EQ(sphere.Sample(0.f, 0.f, 0.f), 0.f);

    // The ramp stores voxel-centre u in half precision, so trilinear sampling
    // reproduces the linear profile up to half rounding (<= 2.5e-4).
    const auto ramp = uz::VolumeAsset::GenerateRampVolume(64);
    EXPECT_EQ(ramp.Format(), uz::VolumeFormat::R16F);
    EXPECT_NEAR(ramp.GetMaxDensity(), 63.5f / 64.f, 2.5e-4f);
    for (int i = 0; i <= 100; ++i) {
        const float u = float(i) / 100.f;
        const float expected = std::fmin(std::fmax(u, .5f / 64.f), 1.f - .5f / 64.f);
        EXPECT_NEAR(ramp.Sample(u, .5f, .5f), expected, 2.5e-4f) << u;
        EXPECT_NEAR(ramp.Sample(u, 0.f, 1.f), expected, 2.5e-4f) << u;
    }
    // Clamp-to-texel-centre addressing: the faces return the edge voxels.
    EXPECT_FLOAT_EQ(ramp.Sample(-5.f, .5f, .5f), ramp.Voxel(0, 0, 0));
    EXPECT_FLOAT_EQ(ramp.Sample(5.f, .5f, .5f), ramp.Voxel(63, 0, 0));

    const auto cloud = uz::VolumeAsset::GenerateCloudVolume(48, 7u);
    EXPECT_GT(cloud.GetMaxDensity(), .5f);
    EXPECT_LE(cloud.GetMaxDensity(), 1.f);
    uint64_t occupied = 0, faces = 0;
    for (uint32_t z = 0; z < 48; ++z)
        for (uint32_t y = 0; y < 48; ++y)
            for (uint32_t x = 0; x < 48; ++x) {
                occupied += cloud.Voxel(x, y, z) > 0.f;
                if (x == 0 || y == 0 || z == 0 || x == 47 || y == 47 || z == 47) faces += cloud.Voxel(x, y, z) > 0.f;
            }
    const double fraction = double(occupied) / (48.0 * 48.0 * 48.0);
    EXPECT_GT(fraction, .02);
    EXPECT_LT(fraction, .6);
    EXPECT_EQ(faces, 0u) << "cloud density must fade out before the box faces";
    // Deterministic for a seed, different across seeds.
    EXPECT_EQ(uz::VolumeAsset::GenerateCloudVolume(48, 7u).Data(), cloud.Data());
    EXPECT_NE(uz::VolumeAsset::GenerateCloudVolume(48, 8u).Data(), cloud.Data());
    std::printf("Grid generators: cloud max=%.6f occupancy=%.4f ramp max=%.6f\n",
                cloud.GetMaxDensity(), fraction, ramp.GetMaxDensity());
}

namespace {
// The shader's delta/ratio loops on a world ray of length `distance` whose
// normalized grid coordinate runs from u0 to u1 (v = w = .5): the same
// majorant-Poisson thinning as PathVolumeGridDelta / PathVolumeGridRatio.
float GridDelta(const uz::VolumeAsset& grid, float sigmaT, float majorantDensity, float u0, float u1,
                float distance, float firstU, uint32_t state) {
    const float majorant = sigmaT * majorantDensity;
    float t = uz::PathVolumeDistance(majorant, firstU);
    while (t < distance) {
        const float acceptance = grid.Sample(u0 + (u1 - u0) * t / distance, .5f, .5f) / majorantDensity;
        if (uz::PathVolumeRandom(state) < acceptance) return t;
        t += uz::PathVolumeDistance(majorant, uz::PathVolumeRandom(state));
    }
    return t;
}
float GridRatio(const uz::VolumeAsset& grid, float sigmaT, float majorantDensity, float u0, float u1,
                float distance, uint32_t state) {
    const float majorant = sigmaT * majorantDensity;
    float weight = 1.f;
    float t = uz::PathVolumeDistance(majorant, uz::PathVolumeRandom(state));
    while (t < distance) {
        weight *= 1.f - grid.Sample(u0 + (u1 - u0) * t / distance, .5f, .5f) / majorantDensity;
        if (weight == 0.f) break;
        t += uz::PathVolumeDistance(majorant, uz::PathVolumeRandom(state));
    }
    return weight;
}
} // namespace

TEST(PathVolumeGrid, DeltaAndRatioTrackingOnSampledGridsMatchQuadrature) {
    constexpr int n = 262144;
    constexpr float distance = 200.f;
    struct Case { const char* name; uz::VolumeAsset grid; float sigmaT, u0, u1; };
    const Case cases[] = {
        {"constant", uz::VolumeAsset::GenerateConstantVolume(16, 1.f), .005f, 0.f, 1.f},
        {"ramp", uz::VolumeAsset::GenerateRampVolume(64), .01f, 0.f, 1.f},
        {"ramp-partial", uz::VolumeAsset::GenerateRampVolume(64), .012f, .2f, .9f},
        {"cloud-chord", uz::VolumeAsset::GenerateCloudVolume(48, 7u), .02f, .1f, .95f},
    };
    for (const auto& c : cases) {
        const float majorantDensity = c.grid.GetMaxDensity();
        ASSERT_GT(majorantDensity, 0.f);
        // Midpoint quadrature of the same trilinear density: the analytic
        // optical depth of the ray the estimators see.
        constexpr int intervals = 16384;
        double tau = 0;
        for (int i = 0; i < intervals; ++i) {
            const float t = distance * (float(i) + .5f) / intervals;
            tau += c.grid.Sample(c.u0 + (c.u1 - c.u0) * t / distance, .5f, .5f);
        }
        tau *= double(c.sigmaT) * distance / intervals;
        const double expected = std::exp(-tau);
        double survival = 0, ratio = 0;
        for (int i = 0; i < n; ++i) {
            uint32_t state = uz::PathVolumeHash(uint32_t(i) ^ 0x51f15e2du);
            const float u = uz::PathVolumeRandom(state);
            survival += GridDelta(c.grid, c.sigmaT, majorantDensity, c.u0, c.u1, distance, u, state) >= distance;
            ratio += GridRatio(c.grid, c.sigmaT, majorantDensity, c.u0, c.u1, distance,
                               uz::PathVolumeHash(uint32_t(i) ^ 0xa511e9b3u));
        }
        EXPECT_NEAR(survival / n, expected, .003) << c.name;
        EXPECT_NEAR(ratio / n, expected, .003) << c.name;
        std::printf("Grid %s: tau=%.6f delta=%.10f ratio=%.10f analytic=%.10f n=%d\n",
                    c.name, tau, survival / n, ratio / n, expected, n);
    }
    // The constant slab is the exact Beer-Lambert check: tau = .005 * 200 = 1.
    uint32_t state = 3;
    EXPECT_FLOAT_EQ(GridRatio(cases[0].grid, .005f, 1.f, 0.f, 1.f, 0.f, state), 1.f);
}

TEST(PathVolumeGrid, ResolveBuiltinsAndRejectUnknown) {
    uz::VolumeAsset asset;
    std::string error;
    for (const char* name : {"builtin:sphere", "builtin:cloud", "builtin:constant", "builtin:ramp"}) {
        EXPECT_TRUE(uz::VolumeAsset::Resolve(name, nullptr, asset, &error)) << name << ": " << error;
        EXPECT_TRUE(asset.IsValid()) << name;
        EXPECT_GT(asset.GetMaxDensity(), 0.f) << name;
    }
    EXPECT_FALSE(uz::VolumeAsset::Resolve("builtin:nope", nullptr, asset, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(uz::VolumeAsset::Resolve("../escape.uzvol", nullptr, asset, &error));
}
