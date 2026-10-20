#include "pathtracing/pipeline_path_tracer_pass.h"
#include <gtest/gtest.h>
#include <cstddef>
#include "../../../shaders/runtime/pathtracing/pathtracer_math.hlsli"
#include <limits>
#include <cmath>

TEST(PipelinePathTracerStage1, MaterialStructuredBufferLayout) {
    using M = uz::PipelinePathMaterial;
    EXPECT_EQ(sizeof(M), 204u);
    EXPECT_EQ(offsetof(M, albedoSrv), 12u);
    EXPECT_EQ(offsetof(M, vbSrv), 16u);
    EXPECT_EQ(offsetof(M, normalOffset), 32u);
    EXPECT_EQ(offsetof(M, metallicSrv), 48u);
    EXPECT_EQ(offsetof(M, uvScale), 64u);
    EXPECT_EQ(offsetof(M, emission), 80u);
    EXPECT_EQ(offsetof(M, emissiveSrv), 92u);
    EXPECT_EQ(offsetof(M, coatWeight), 100u);
    EXPECT_EQ(offsetof(M, coatRoughness), 104u);
    EXPECT_EQ(offsetof(M, coatIOR), 108u);
    EXPECT_EQ(offsetof(M, coatThickness), 112u);
    EXPECT_EQ(offsetof(M, coatSigmaA), 116u);
    EXPECT_EQ(offsetof(M, stackDepth), 128u);
    EXPECT_EQ(offsetof(M, stackRoughness), 132u);
    EXPECT_EQ(offsetof(M, stackIOR), 144u);
    EXPECT_EQ(offsetof(M, stackThickness), 156u);
    EXPECT_EQ(offsetof(M, stackSigmaAR), 168u);
    EXPECT_EQ(offsetof(M, stackSigmaAG), 180u);
    EXPECT_EQ(offsetof(M, stackSigmaAB), 192u);
}

TEST(PipelinePathTracerStage1, MaterialDefaultsDoNotSampleMissingDescriptors) {
    const uz::PipelinePathMaterial material{};
    EXPECT_EQ(material.albedoSrv, UINT32_MAX);
    EXPECT_EQ(material.vbSrv, UINT32_MAX);
    EXPECT_EQ(material.ibSrv, UINT32_MAX);
    EXPECT_EQ(material.roughnessSrv, UINT32_MAX);
    EXPECT_EQ(material.metallicSrv, UINT32_MAX);
    EXPECT_EQ(material.emissiveSrv, UINT32_MAX);
    EXPECT_FLOAT_EQ(material.uvScale[0], 1.f);
    EXPECT_FLOAT_EQ(material.uvScale[1], 1.f);
    EXPECT_FLOAT_EQ(material.emission[0], 0.f);
    EXPECT_FLOAT_EQ(material.coatThickness, 1.f);
    for (float sigmaA : material.coatSigmaA) EXPECT_FLOAT_EQ(sigmaA, 0.f);
    EXPECT_EQ(material.stackDepth, 0u);
}

TEST(PipelinePathTracerStage1, PipelineBudgetIncludesNestedShadowRay) {
    using P = uz::PipelinePathTracerPass;
    EXPECT_LE(P::kRootDwords, 24u);
    EXPECT_EQ(P::kRootDwords * sizeof(uint32_t), sizeof(uz::PipelinePathRoot));
    EXPECT_EQ(P::kPayloadBytes, 3u * sizeof(float) * 6u + sizeof(float) + 3u * sizeof(uint32_t));
    EXPECT_EQ(P::kRecursionDepth, 2u);
}

TEST(PipelinePathTracerStage1, DirectionalLightStructuredBufferLayout) {
    using L = uz::PipelinePathDirectionalLight;
    EXPECT_EQ(sizeof(L), 32u);
    EXPECT_EQ(offsetof(L, angularRadius), 12u);
    EXPECT_EQ(offsetof(L, radiance), 16u);
    EXPECT_EQ(offsetof(L, padding), 28u);
}

TEST(PipelinePathTracerStage2, PrivateRootContainsHistoryAndTransportSettings) {
    using R = uz::PipelinePathRoot;
    EXPECT_EQ(sizeof(R), 96u);
    EXPECT_EQ(offsetof(R, accumulationUav), 24u);
    EXPECT_EQ(offsetof(R, compensationUav), 28u);
    EXPECT_EQ(offsetof(R, accumulatedFrames), 32u);
    EXPECT_EQ(offsetof(R, maxBounces), 36u);
    EXPECT_EQ(offsetof(R, rrEnabled), 40u);
    EXPECT_EQ(offsetof(R, rrMinBounce), 44u);
}

TEST(PipelinePathTracerStage2, RunningMeanAndResetIgnoreOldHistory) {
    float correction = 0.f;
    float mean = 0.f;
    for (uint32_t i = 0; i < 1000; ++i)
        mean = uz::PathAccumulate(float(i + 1), mean, correction, i);
    EXPECT_NEAR(mean, 500.5f, 1e-4f);
    correction = std::numeric_limits<float>::quiet_NaN();
    mean = uz::PathAccumulate(7.f, std::numeric_limits<float>::quiet_NaN(), correction, 0);
    EXPECT_FLOAT_EQ(mean, 7.f);
    EXPECT_FLOAT_EQ(correction, 0.f);
}

TEST(PipelinePathTracerStage2, CompensatedMeanRetainsSmallUpdates) {
    float mean = 1.f, correction = 0.f, naive = 1.f;
    for (uint32_t i = 1000000; i < 1010000; ++i) {
        mean = uz::PathAccumulate(1.01f, mean, correction, i);
        naive += (1.01f - naive) / float(i + 1);
    }
    const double expected = (1000000.0 + 10000.0 * double(1.01f)) / 1010000.0;
    EXPECT_NEAR(mean, expected, 1e-7);
    EXPECT_GT(std::abs(double(naive) - expected), std::abs(double(mean) - expected));
}

TEST(PipelinePathTracerStage2, RouletteSurvivalIsBoundedAndUnbiased) {
    EXPECT_FLOAT_EQ(uz::PathSurvivalProbability(0.f), 0.05f);
    EXPECT_FLOAT_EQ(uz::PathSurvivalProbability(0.4f), 0.4f);
    EXPECT_FLOAT_EQ(uz::PathSurvivalProbability(2.f), 1.f);
    const float probability = uz::PathSurvivalProbability(0.25f);
    float expected = 0.f;
    for (uint32_t i = 0; i < 100; ++i)
        if ((float(i) + 0.5f) / 100.f < probability) expected += 0.25f / probability / 100.f;
    EXPECT_NEAR(expected, 0.25f, 1e-6f);
}

TEST(PipelinePathTracerStage2, BounceBudgetStopsAtConfiguredSurfaceCount) {
    EXPECT_FALSE(uz::PathCanContinue(1u, 1u));
    EXPECT_TRUE(uz::PathCanContinue(1u, 4u));
    EXPECT_TRUE(uz::PathCanContinue(3u, 4u));
    EXPECT_FALSE(uz::PathCanContinue(4u, 4u));
}
