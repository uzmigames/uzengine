#include "pathtracing/pipeline_path_tracer_pass.h"
#include <gtest/gtest.h>
#include <cstddef>

TEST(PipelinePathTracerStage1, MaterialStructuredBufferLayout) {
    using M = uz::PipelinePathMaterial;
    EXPECT_EQ(sizeof(M), 96u);
    EXPECT_EQ(offsetof(M, albedoSrv), 12u);
    EXPECT_EQ(offsetof(M, vbSrv), 16u);
    EXPECT_EQ(offsetof(M, normalOffset), 32u);
    EXPECT_EQ(offsetof(M, metallicSrv), 48u);
    EXPECT_EQ(offsetof(M, uvScale), 64u);
    EXPECT_EQ(offsetof(M, emission), 80u);
    EXPECT_EQ(offsetof(M, emissiveSrv), 92u);
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
}

TEST(PipelinePathTracerStage1, PipelineBudgetIncludesNestedShadowRay) {
    using P = uz::PipelinePathTracerPass;
    EXPECT_LE(P::kRootDwords, 24u);
    EXPECT_EQ(P::kRootDwords * sizeof(uint32_t), sizeof(uz::PipelinePathRoot));
    EXPECT_EQ(P::kPayloadBytes, 3u * sizeof(float) * 2u + 2u * sizeof(uint32_t));
    EXPECT_EQ(P::kRecursionDepth, 2u);
}

TEST(PipelinePathTracerStage1, DirectionalLightStructuredBufferLayout) {
    using L = uz::PipelinePathDirectionalLight;
    EXPECT_EQ(sizeof(L), 32u);
    EXPECT_EQ(offsetof(L, angularRadius), 12u);
    EXPECT_EQ(offsetof(L, radiance), 16u);
    EXPECT_EQ(offsetof(L, padding), 28u);
}
