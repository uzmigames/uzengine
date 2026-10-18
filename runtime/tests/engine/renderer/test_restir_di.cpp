#include "pathtracing/pipeline_path_tracer_pass.h"
#include "../../../shaders/runtime/include/restir_math.hlsli"
#include <gtest/gtest.h>
#include <cstddef>

TEST(RestirDIStage1, StructuredBufferLayouts) {
    EXPECT_EQ(sizeof(uz::RestirLightSample), 44u);
    EXPECT_EQ(sizeof(uz::RestirReservoir), 112u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, weightSum), 44u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, M), 48u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, W), 52u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, surfacePosition), 56u);
    EXPECT_EQ(sizeof(uz::RestirSurface), 64u);
    EXPECT_EQ(sizeof(uz::PipelinePathRoot), 96u);
    EXPECT_EQ(sizeof(uz::PipelinePathMaterial), 128u);
    EXPECT_EQ(offsetof(uz::PipelinePathMaterial, emission), 80u);
    EXPECT_EQ(offsetof(uz::PipelinePathMaterial, emissiveSrv), 92u);
    EXPECT_EQ(sizeof(uz::PipelinePathDirectionalLight), 32u);
}

TEST(RestirDIStage1, StreamingRISSelectsProportionalToWeights) {
    unsigned selectedSecond = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        float sum = 0.f;
        EXPECT_TRUE(uz::RestirStream(sum, 1.f, 0.7f));
        selectedSecond += uz::RestirStream(sum, 3.f, (float(i) + 0.5f) / 10000.f);
        EXPECT_FLOAT_EQ(sum, 4.f);
    }
    EXPECT_EQ(selectedSecond, 7500u);
}

TEST(RestirDIStage1, ZeroWeightNeverReplacesSelectedSample) {
    float sum = 0.f;
    EXPECT_FALSE(uz::RestirStream(sum, 0.f, 0.f));
    EXPECT_FLOAT_EQ(uz::RestirWeight(0.f, 0u, 0.f), 0.f);
    EXPECT_TRUE(uz::RestirStream(sum, 2.f, 0.5f));
    EXPECT_FALSE(uz::RestirStream(sum, 0.f, 0.f));
    EXPECT_FLOAT_EQ(uz::RestirWeight(sum, 2u, 2.f), 0.5f);
    EXPECT_FLOAT_EQ(uz::RestirWeight(sum, 2u, 0.f), 0.f);
}

TEST(RestirDIStage1, InitialWeightIsUnbiasedForDiscreteCandidates) {
    // Two equally likely lights, target contributions 1 and 3:
    // RIS probabilities 1/4, 3/4; selected estimate is always total 4.
    const float sum = 1.f / 0.5f + 3.f / 0.5f;
    const float low = uz::RestirWeight(sum, 2u, 1.f);
    const float high = uz::RestirWeight(sum, 2u, 3.f);
    EXPECT_FLOAT_EQ(low * 1.f, 4.f);
    EXPECT_FLOAT_EQ(high * 3.f, 4.f);
}

TEST(RestirDIStage1, TemporalCombinationReevaluatesCurrentTarget) {
    const float previousWeight = uz::RestirCombineWeight(4.f, 0.5f, 9u);
    EXPECT_FLOAT_EQ(previousWeight, 18.f);
    const float currentWeight = uz::RestirCombineWeight(2.f, 1.f, 1u);
    EXPECT_FLOAT_EQ(uz::RestirWeight(previousWeight + currentWeight, 10u, 4.f), 0.5f);
}

TEST(RestirDIStage1, HistoryCapPreservesContributionWeight) {
    const float capped = uz::RestirCappedSum(400.f, 40u, 19u);
    EXPECT_FLOAT_EQ(capped, 190.f);
    EXPECT_FLOAT_EQ(uz::RestirWeight(capped, 19u, 2.f),
                    uz::RestirWeight(400.f, 40u, 2.f));
    EXPECT_FLOAT_EQ(uz::RestirCappedSum(20.f, 2u, 19u), 20.f);
    // Re-evaluated history plus one new frame never exceeds twenty frames.
    EXPECT_FLOAT_EQ(uz::RestirCombineWeight(2.f, 5.f, 19u), capped);
}
