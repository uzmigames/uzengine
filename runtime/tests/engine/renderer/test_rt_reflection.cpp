#include "../../../shaders/runtime/reflections/rt_reflection_contract.hlsli"
#include "passes/raytracing/rt_reflection_pass.h"

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>

using namespace uz;
using namespace uz::rtreflection;

TEST(RTReflectionStage1, MirrorDirectionAndSurfaceBias) {
    const Vec3 normal(0, 0, 1);
    const Vec3 incident = Vec3(0, 4, -3).Normalized();
    const Vec3 direction = ReflectionDirection(incident, normal);
    EXPECT_NEAR(direction.x, 0.0f, 1e-6f);
    EXPECT_NEAR(direction.y, 0.8f, 1e-6f);
    EXPECT_NEAR(direction.z, 0.6f, 1e-6f);
    const Vec3 origin = Vec3(350, -84, 0) + normal * kReflectionSurfaceBias;
    EXPECT_FLOAT_EQ(origin.x, 350.0f);
    EXPECT_FLOAT_EQ(origin.z, 2.0f);
    EXPECT_FLOAT_EQ(kReflectionRayTMin, 1.0f);
}

TEST(RTReflectionStage1, GrazingRayStaysAboveSurface) {
    const Vec3 incident = Vec3(1, 0, -0.0001f).Normalized();
    const Vec3 direction = ReflectionDirection(incident, Vec3(0, 0, 1));
    EXPECT_GT(direction.z, 0.0f);
    EXPECT_NEAR(direction.Dot(direction), 1.0f, 1e-6f);
}

TEST(RTReflectionStage1, RoughnessCutoffIncludesBoundaryAndExcludesSky) {
    EXPECT_TRUE(ReflectionEligible(0.5f, 0.0f, 0.35f));
    EXPECT_TRUE(ReflectionEligible(0.5f, 0.35f, 0.35f));
    EXPECT_FALSE(ReflectionEligible(0.5f, std::nextafter(0.35f, 1.0f), 0.35f));
    EXPECT_FALSE(ReflectionEligible(0.0f, 0.02f, 0.35f));
}

TEST(RTReflectionStage1, CurrentScreenConfidenceSelectsFallback) {
    EXPECT_FALSE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 1.0f));
    EXPECT_FALSE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 0.002f));
    EXPECT_TRUE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 0.001f));
    EXPECT_TRUE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 0.0f));
    EXPECT_FALSE(ReflectionNeedsRT(0.5f, 0.5f, 0.35f, 0.0f));
}

TEST(RTReflectionStage1, UncoveredAndMissingHitsSelectProbeOrSky) {
    EXPECT_TRUE(ReflectionHitUsable(true, 0.0f, 1.0f));
    EXPECT_FALSE(ReflectionHitUsable(false, 0.0f, 1.0f));
    EXPECT_FALSE(ReflectionHitUsable(true, 1e9f, 1.0f));
    EXPECT_FALSE(ReflectionHitUsable(true, 0.0f, 0.0f));
}

// The pass binding contract (rt_reflection_cs.hlsl b0 / b2): the root block fits the engine's
// 20-DWORD bindless budget and the fields sit where the shader's cbuffers read them.
TEST(RTReflectionStage1, RootConstantsFitTheBindlessBudget) {
    EXPECT_EQ(sizeof(RTReflectionRootConstants), kRTReflectionRootDwords * sizeof(uint32_t));
    EXPECT_LE(kRTReflectionRootDwords, 20u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, tlasSrvIdx), 16u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, outputHeight), 24u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, maxRoughness), 28u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, roughnessMaskScale), 40u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, materialTableSrv), 44u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, skyCubemapSrv), 48u);
}

// The rows of the shared b2 layout the shader reads through gi_trace_common.hlsli (rows 3-10 of
// RadianceCacheTraceConstants): a shift here would move every field the GDF sampling reads.
TEST(RTReflectionStage1, TraceConstantsRowsMatchTheSharedLayout) {
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, gdfCenter0X), 48u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, gdfExtent3), 108u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, gdfClipmapSrv0), 112u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, numGdfClipmaps), 128u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, skyColorR), 132u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, lightDirX), 144u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, lightColorR), 160u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, groundAlbedo), 172u);
}

TEST(RTReflectionStage1, UnsetClipmapsStayAtTheSentinelAndTheLightingIsTheFrames) {
    PerFrameCB perFrame{};
    perFrame.lightDir          = Vec4(0.0f, 0.6f, 0.8f, 0.0f);
    perFrame.lightColor        = Vec4(3.0f, 2.7f, 2.4f, 0.0f);   // colour x intensity, as the lighting reads it
    perFrame.skyLightIntensity = 0.03f;
    perFrame.skyLightColorR    = 1.0f;
    perFrame.skyLightColorG    = 0.5f;
    perFrame.skyLightColorB    = 0.0f;

    // An uninitialised field: every clipmap at the sentinel, which gdf_surface.hlsli skips.
    GlobalDistanceField noField;
    RadianceCacheTraceConstants tc{};
    FillRTReflectionTraceConstants(tc, noField, perFrame);
    EXPECT_EQ(tc.numGdfClipmaps, 0u);
    EXPECT_EQ(tc.gdfClipmapSrv0, UINT32_MAX);
    EXPECT_EQ(tc.gdfClipmapSrv3, UINT32_MAX);
    EXPECT_FLOAT_EQ(tc.gdfExtent0, 0.0f);
    EXPECT_FLOAT_EQ(tc.lightDirY, 0.6f);
    EXPECT_FLOAT_EQ(tc.lightDirZ, 0.8f);
    EXPECT_FLOAT_EQ(tc.lightIntensity, 1.0f);
    EXPECT_FLOAT_EQ(tc.lightColorR, 3.0f);
    EXPECT_FLOAT_EQ(tc.lightColorB, 2.4f);
    EXPECT_FLOAT_EQ(tc.skyColorR, 0.03f);
    EXPECT_FLOAT_EQ(tc.skyColorG, 0.015f);
    EXPECT_FLOAT_EQ(tc.skyColorB, 0.0f);
    EXPECT_FLOAT_EQ(tc.groundAlbedo, 0.18f);

    SetRTReflectionClipmap(tc, 0, Vec3(350, -84, 12), 2500.0f, 24741u);
    SetRTReflectionClipmap(tc, 3, Vec3(-1, -2, -3), 160000.0f, 24753u);
    SetRTReflectionClipmap(tc, 4, Vec3(9, 9, 9), 1.0f, 7u);   // beyond the four rows: ignored
    EXPECT_FLOAT_EQ(tc.gdfCenter0X, 350.0f);
    EXPECT_FLOAT_EQ(tc.gdfCenter0Z, 12.0f);
    EXPECT_FLOAT_EQ(tc.gdfExtent0, 2500.0f);
    EXPECT_EQ(tc.gdfClipmapSrv0, 24741u);
    EXPECT_FLOAT_EQ(tc.gdfCenter3Y, -2.0f);
    EXPECT_EQ(tc.gdfClipmapSrv3, 24753u);
    EXPECT_EQ(tc.gdfClipmapSrv1, UINT32_MAX);
    EXPECT_FLOAT_EQ(tc.gdfExtent1, 0.0f);
    EXPECT_EQ(tc.gdfClipmapSrv2, UINT32_MAX);
    EXPECT_FLOAT_EQ(tc.gdfExtent2, 0.0f);
}
