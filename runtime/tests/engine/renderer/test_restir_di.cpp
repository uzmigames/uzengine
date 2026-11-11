#include "pathtracing/pipeline_path_tracer_pass.h"
#include "../../../shaders/runtime/include/restir_math.hlsli"
#include "mat4.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <cstring>

TEST(RestirDIStage1, StructuredBufferLayouts) {
    EXPECT_EQ(sizeof(uz::RestirLightSample), 44u);
    EXPECT_EQ(sizeof(uz::RestirReservoir), 116u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, visibility), 112u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, weightSum), 44u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, M), 48u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, W), 52u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, surfacePosition), 56u);
    EXPECT_EQ(sizeof(uz::RestirSurface), 64u);
    EXPECT_EQ(sizeof(uz::PipelinePathRoot), 96u);
    EXPECT_EQ(sizeof(uz::PipelinePathMaterial), 204u);
    EXPECT_EQ(offsetof(uz::PipelinePathMaterial, emission), 80u);
    EXPECT_EQ(offsetof(uz::PipelinePathMaterial, emissiveSrv), 92u);
    EXPECT_EQ(sizeof(uz::PipelinePathDirectionalLight), 32u);
}

// Temporal history across camera motion (expanded-roadmap item 2.1): the
// pass-private previous-camera record, the pure history rule, and the
// reprojection convention restir_temporal_cs mirrors.
TEST(RestirDIHistory, CameraHistoryLayout) {
    EXPECT_EQ(sizeof(uz::RestirCameraHistory), 96u);
    EXPECT_EQ(offsetof(uz::RestirCameraHistory, viewProjRows), 0u);
    EXPECT_EQ(offsetof(uz::RestirCameraHistory, cameraPos), 64u);
    EXPECT_EQ(offsetof(uz::RestirCameraHistory, cameraMoved), 80u);
    EXPECT_EQ(sizeof(uz::Mat4), sizeof(uz::RestirCameraHistory::viewProjRows));
}

TEST(RestirDIHistory, HistorySurvivesCameraOnlyChanges) {
    uz::RestirHistoryInputs in{};
    in.enabled = true; in.previousEnabled = true;
    in.frameIndex = 101u; in.previousFrame = 100u;
    in.resetGeneration = 3u; in.previousResetGeneration = 3u;
    in.sceneDirtyFlags = 0u;
    // The transport signature excludes the camera: a camera-only change keeps it.
    in.transportSignature = 0x1234u; in.previousTransportSignature = 0x1234u;
    EXPECT_TRUE(uz::RestirHistoryValid(in));
    auto broken = in; broken.enabled = false;
    EXPECT_FALSE(uz::RestirHistoryValid(broken));
    broken = in; broken.previousEnabled = false;      // ReSTIR did not run last frame
    EXPECT_FALSE(uz::RestirHistoryValid(broken));
    broken = in; broken.previousFrame = UINT32_MAX;   // first frame after creation
    EXPECT_FALSE(uz::RestirHistoryValid(broken));
    broken = in; broken.frameIndex = 102u;            // frame gap
    EXPECT_FALSE(uz::RestirHistoryValid(broken));
    broken = in; broken.resetGeneration = 4u;         // manual reset
    EXPECT_FALSE(uz::RestirHistoryValid(broken));
    broken = in; broken.sceneDirtyFlags = 1u;         // scene edit / TLAS change
    EXPECT_FALSE(uz::RestirHistoryValid(broken));
    broken = in; broken.transportSignature = 0x1235u; // lights/materials/CVars changed
    EXPECT_FALSE(uz::RestirHistoryValid(broken));
}

TEST(RestirDIHistory, RowExpansionReprojectsToTheGeneratingPixel) {
    // The shader computes clip = P.x * row0 + P.y * row1 + P.z * row2 + row3 from
    // the 16 floats memcpy'd out of PerFrameCB.viewProj. That must equal the
    // engine's own transform (mul(float4(P, 1), viewProj) in HLSL), and the
    // inverse of the raygen's pixel mapping must land on the generating pixel.
    const uz::Mat4 view = uz::Mat4::LookAt(uz::Vec3(0.f, -400.f, 640.f), uz::Vec3(0.f, 0.f, 0.f),
                                           uz::Vec3(0.f, 0.f, 1.f));
    const uz::Mat4 projection = uz::Mat4::Perspective(45.f * 3.14159265f / 180.f, 640.f / 360.f, 1.f, 10000.f);
    const uz::Mat4 viewProj = view * projection;
    const uz::Mat4 inverse = viewProj.Inverse();
    uz::RestirCameraHistory history{};
    std::memcpy(history.viewProjRows, &viewProj, sizeof(history.viewProjRows));
    const float width = 640.f, height = 360.f;
    unsigned checked = 0u;
    for (int py = 0; py < 360; py += 37) {
        for (int px = 0; px < 640; px += 41) {
            // Raygen: uv = (pixel + 0.5 + jitter) / size, ndc = (2u - 1, 1 - 2v),
            // target = mul(float4(ndc, 0.5, 1), invViewProj), direction toward target.
            const float jx = 0.31f, jy = -0.47f;
            const float u = (float(px) + 0.5f + jx) / width, v = (float(py) + 0.5f + jy) / height;
            const uz::Vec4 target = inverse * uz::Vec4(2.f * u - 1.f, 1.f - 2.f * v, 0.5f, 1.f);
            const uz::Vec3 world(target.x / target.w, target.y / target.w, target.z / target.w);
            const uz::Vec4 expected = viewProj * uz::Vec4(world.x, world.y, world.z, 1.f);
            float clip[4];
            for (int c = 0; c < 4; ++c)
                clip[c] = world.x * history.viewProjRows[0][c] + world.y * history.viewProjRows[1][c]
                        + world.z * history.viewProjRows[2][c] + history.viewProjRows[3][c];
            EXPECT_NEAR(clip[0], expected.x, 1e-3f * std::fabs(expected.w));
            EXPECT_NEAR(clip[1], expected.y, 1e-3f * std::fabs(expected.w));
            EXPECT_NEAR(clip[3], expected.w, 1e-3f * std::fabs(expected.w));
            ASSERT_GT(clip[3], 0.f);
            const float ndcX = clip[0] / clip[3], ndcY = clip[1] / clip[3];
            const int hx = int(std::floor((0.5f + 0.5f * ndcX) * width));
            const int hy = int(std::floor((0.5f - 0.5f * ndcY) * height));
            EXPECT_EQ(hx, px);
            EXPECT_EQ(hy, py);
            ++checked;
        }
    }
    EXPECT_EQ(checked, 160u);
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
