/// Unit tests for the PCSS penumbra estimate of the directional CSM (shadow_pcss.h) and the
/// ShadowDataCB fields that carry it to the shader. No GPU, no window.
///
/// The shader (shadow.hlsli, SampleShadowCascadeSoft) mirrors these one-liners; what is checked
/// here is the similar-triangles behaviour they encode: no penumbra at contact, a penumbra that
/// grows linearly with the receiver-blocker distance and with r.Shadows.SoftPCSS.LightSize, the
/// clamp window, the search/filter radius ordering the early-out relies on, and that the per-cascade
/// constant makes the world-space penumbra independent of which cascade a pixel lands in.

#include "shadow_pcss.h"
#include "shadow_cascade.h"
#include "constant_buffers.h"
#include "shadow_data_params.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cmath>

#include <DirectXMath.h>

using namespace uz;
using namespace DirectX;

namespace {

/// tan(0.5 * 0.5357 deg): the sun's angular radius, DirectionalLightComponent::lightSourceAngle.
constexpr float kSunTanHalf = 0.0046749f;

struct TestCamera {
    Mat4 view;
    Mat4 proj;
    Vec3 lightDir;
};

TestCamera MakeStandardCamera() {
    TestCamera tc;
    tc.view = Mat4::LookAt(Vec3{0.0f, 500.0f, 200.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f});
    tc.proj = Mat4::Perspective(XMConvertToRadians(60.0f), 16.0f / 9.0f, 5.0f, 10000.0f);
    tc.lightDir = Vec3{-0.5f, 0.3f, -0.8f}.Normalized();
    return tc;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Penumbra estimate
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowPcss, PenumbraIsZeroWhenReceiverTouchesBlocker) {
    EXPECT_FLOAT_EQ(PcssPenumbraTexels(0.4f, 0.4f, 1000.0f), 0.0f);
    EXPECT_FLOAT_EQ(PcssPenumbraTexels(1.0f, 1.0f, 1000.0f), 0.0f);
}

TEST(ShadowPcss, LocalPenumbraUsesPerspectiveSimilarTriangles) {
    EXPECT_FLOAT_EQ(PcssLocalPenumbraRadius(100.0f, 100.0f, 5.0f), 0.0f);
    EXPECT_FLOAT_EQ(PcssLocalPenumbraRadius(200.0f, 100.0f, 5.0f), 5.0f);
    EXPECT_FLOAT_EQ(PcssLocalPenumbraRadius(300.0f, 100.0f, 5.0f), 10.0f);
    EXPECT_FLOAT_EQ(PcssLocalPenumbraRadius(200.0f, 100.0f, 10.0f), 10.0f);
    EXPECT_FLOAT_EQ(PcssLocalPenumbraRadius(300.0f, 200.0f, 5.0f), 2.5f);
    EXPECT_FLOAT_EQ(PcssLocalPenumbraRadius(100.0f, 200.0f, 5.0f), 0.0f);
    EXPECT_FLOAT_EQ(PcssLocalPenumbraRadius(200.0f, 100.0f, 0.0f), 0.0f);
    EXPECT_TRUE(std::isfinite(PcssLocalPenumbraRadius(200.0f, 0.0f, 5.0f)));
}

TEST(ShadowPcss, PenumbraGrowsWithBlockerDistance) {
    const float depthToTexels = 1000.0f;
    float previous = PcssPenumbraTexels(0.5f, 0.5f, depthToTexels);
    for (float blocker = 0.45f; blocker >= 0.049f; blocker -= 0.05f) {
        const float radius = PcssPenumbraTexels(0.5f, blocker, depthToTexels);
        EXPECT_GT(radius, previous) << "blocker depth " << blocker;
        previous = radius;
    }
}

TEST(ShadowPcss, PenumbraIsLinearInGapAndScale) {
    // Similar triangles with the sun at infinity: radius = gap * constant.
    const float base = PcssPenumbraTexels(0.6f, 0.4f, 50.0f);
    EXPECT_NEAR(base, 10.0f, 1e-4f);
    EXPECT_NEAR(PcssPenumbraTexels(0.8f, 0.4f, 50.0f), 2.0f * base, 1e-4f);   // twice the gap
    EXPECT_NEAR(PcssPenumbraTexels(0.6f, 0.4f, 100.0f), 2.0f * base, 1e-4f);  // twice the constant
}

TEST(ShadowPcss, BlockerBehindReceiverGivesZeroNotNegative) {
    EXPECT_FLOAT_EQ(PcssPenumbraTexels(0.3f, 0.5f, 100.0f), 0.0f);
}

TEST(ShadowPcss, DepthToTexelsFoldsSunAngleDepthRangeAndTexelSize) {
    // A 24 m wall top seen from 4800 cm along a 30-degree sun: 4800 * tan(0.268 deg) = 22.4 cm of
    // penumbra radius, 29.9 texels of 0.75 cm.
    const float depthRange = 12000.0f;
    const float texel = 0.75f;
    const float k = PcssDepthToPenumbraTexels(kSunTanHalf, 1.0f, depthRange, texel);
    const float gap = 4800.0f / depthRange;
    EXPECT_NEAR(PcssPenumbraTexels(gap, 0.0f, k), 4800.0f * kSunTanHalf / texel, 1e-2f);
    // r.Shadows.SoftPCSS.LightSize scales the penumbra linearly; 0 degenerates to a hard shadow.
    EXPECT_NEAR(PcssDepthToPenumbraTexels(kSunTanHalf, 4.0f, depthRange, texel), 4.0f * k, 1e-3f);
    EXPECT_FLOAT_EQ(PcssDepthToPenumbraTexels(kSunTanHalf, 0.0f, depthRange, texel), 0.0f);
    // No cascade (no texel size): no penumbra rather than a division by zero.
    EXPECT_FLOAT_EQ(PcssDepthToPenumbraTexels(kSunTanHalf, 1.0f, depthRange, 0.0f), 0.0f);
}

TEST(ShadowPcss, ClampWindowAndInvertedRange) {
    EXPECT_FLOAT_EQ(PcssClampRadiusTexels(0.1f, 0.5f, 16.0f), 0.5f);
    EXPECT_FLOAT_EQ(PcssClampRadiusTexels(40.0f, 0.5f, 16.0f), 16.0f);
    EXPECT_FLOAT_EQ(PcssClampRadiusTexels(3.0f, 0.5f, 16.0f), 3.0f);
    EXPECT_FLOAT_EQ(PcssClampRadiusTexels(3.0f, 4.0f, 2.0f), 4.0f);  // max below min collapses onto min
}

TEST(ShadowPcss, SearchRadiusNeverBelowFilterRadius) {
    // The blocker search returns "fully lit" when it finds nothing; that is only exact if the filter
    // footprint is inside the search footprint for every blocker depth the search can report.
    const float depthToTexels = 37.0f;
    for (float receiver : {0.05f, 0.3f, 0.9f}) {
        for (float blocker : {0.0f, 0.5f * receiver, receiver}) {
            const float search = PcssSearchRadiusTexels(receiver, depthToTexels, 0.5f, 16.0f);
            const float filter = PcssClampRadiusTexels(
                PcssPenumbraTexels(receiver, blocker, depthToTexels), 0.5f, 16.0f);
            EXPECT_GE(search, filter) << "receiver " << receiver << " blocker " << blocker;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Through ComputeCascades: the per-cascade constant
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowPcss, WorldPenumbraIsConsistentAcrossComputedCascades) {
    const TestCamera tc = MakeStandardCamera();
    ShadowConfig cfg;
    cfg.resolution = 4096;
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t count = 0;
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &count);
    ASSERT_EQ(count, cfg.cascadeCount);

    const float blockerDistanceWorld = 1000.0f;  // cm along the light direction
    for (uint32_t i = 0; i < count; ++i) {
        const ShadowCascade& c = cascades[i];
        ASSERT_GT(c.depthRange, 0.0f) << "cascade " << i;
        ASSERT_GT(c.texelSize, 0.0f) << "cascade " << i;
        // Main cascades: ortho depth = pullback (8 r) + r, texel = 2 r / resolution.
        EXPECT_NEAR(c.depthRange / c.texelSize, 4.5f * static_cast<float>(cfg.resolution),
                    0.01f * 4.5f * static_cast<float>(cfg.resolution)) << "cascade " << i;

        const float k = PcssDepthToPenumbraTexels(kSunTanHalf, 1.0f, c.depthRange, c.texelSize);
        const float gap = blockerDistanceWorld / c.depthRange;
        const float worldRadius = PcssPenumbraTexels(gap, 0.0f, k) * c.texelSize;
        const float expected = blockerDistanceWorld * kSunTanHalf;
        EXPECT_NEAR(worldRadius, expected, 1e-3f * expected) << "cascade " << i;
    }
    // The constant is the same for every main cascade (depth range and texel both scale with the
    // sphere radius); a coarser cascade uses fewer texels because the same world distance is a
    // smaller normalised depth gap there.
    for (uint32_t i = 1; i < count; ++i) {
        const float kPrev = PcssDepthToPenumbraTexels(kSunTanHalf, 1.0f, cascades[i - 1].depthRange, cascades[i - 1].texelSize);
        const float kThis = PcssDepthToPenumbraTexels(kSunTanHalf, 1.0f, cascades[i].depthRange, cascades[i].texelSize);
        EXPECT_NEAR(kThis, kPrev, 1e-3f * kPrev) << "the ratio depthRange/texelSize is the same for every main cascade";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB carries the fields the shader reads
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowDataCB, PcssFieldOffsetsMatchShadowHlsli) {
    EXPECT_EQ(offsetof(ShadowDataCB, localShadowNormalOffset), 704u);
    EXPECT_EQ(offsetof(ShadowDataCB, softPCSS), 708u);
    EXPECT_EQ(offsetof(ShadowDataCB, pcssMinPenumbraTexels), 712u);
    EXPECT_EQ(offsetof(ShadowDataCB, pcssMaxPenumbraTexels), 716u);
    EXPECT_EQ(offsetof(ShadowDataCB, cascadePcssDepthToTexels), 720u);
    EXPECT_EQ(offsetof(ShadowDataCB, localSoftPCSS), 752u);
    EXPECT_EQ(offsetof(ShadowDataCB, localPcssSourceRadius), 756u);
    EXPECT_EQ(offsetof(ShadowDataCB, localShadows), 768u);
    EXPECT_EQ(sizeof(ShadowDataCB), 2304u);

    EXPECT_EQ(offsetof(ShadowDataParams, softPCSS), 708u);
    EXPECT_EQ(offsetof(ShadowDataParams, pcssMinPenumbraTexels), 712u);
    EXPECT_EQ(offsetof(ShadowDataParams, pcssMaxPenumbraTexels), 716u);
    EXPECT_EQ(offsetof(ShadowDataParams, cascadePcssDepthToTexels), 720u);
    EXPECT_EQ(offsetof(ShadowDataParams, localSoftPCSS), 752u);
    EXPECT_EQ(offsetof(ShadowDataParams, localPcssSourceRadius), 756u);
    EXPECT_EQ(offsetof(ShadowDataParams, localShadows), 768u);
}

TEST(ShadowDataCB, PcssDefaultsSelectTheFixedPcfPath) {
    // A CB nobody filled (no sun, tests, tools) samples the r.Shadows.SoftPCSS 0 path; the CVar
    // default of 1 is applied by SetupShadowFrame / frame_constants.cpp, not by the struct.
    const ShadowDataCB cb{};
    EXPECT_EQ(cb.softPCSS, 0u);
    EXPECT_EQ(cb.localSoftPCSS, 0u);
    EXPECT_FLOAT_EQ(cb.pcssMinPenumbraTexels, 0.5f);
    EXPECT_FLOAT_EQ(cb.pcssMaxPenumbraTexels, 16.0f);
    for (float k : cb.cascadePcssDepthToTexels) EXPECT_FLOAT_EQ(k, 0.0f);
}
