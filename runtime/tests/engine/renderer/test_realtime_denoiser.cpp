#include <gtest/gtest.h>
#include "../../../shaders/runtime/denoising/realtime_denoise_math.hlsli"
#include "passes/realtime_denoiser.h"

TEST(RealtimeDenoiser, PixelMotionAndJitterUseRenderDimensions) {
    EXPECT_FLOAT_EQ(uz::denoise::PreviousCoordinate(0.5f, 64.f, 640.f, 0.f, 0.f), 0.4f);
    EXPECT_FLOAT_EQ(uz::denoise::PreviousCoordinate(0.5f, 0.f, 640.f, 0.25f, -0.25f),
                    0.5f + 0.5f / 640.f);
    // A complete stationary jitter cycle returns to the same lattice coordinate.
    float uv = 0.5f;
    const float jitter[] = {0.f, -0.25f, 0.375f, -0.125f, 0.f};
    for (int i = 1; i < 5; ++i)
        uv = uz::denoise::PreviousCoordinate(uv, 0.f, 640.f, jitter[i-1], jitter[i]);
    EXPECT_FLOAT_EQ(uv, 0.5f);
}

TEST(RealtimeDenoiser, EachHistoryTapRequiresReceiverCompatibility) {
    EXPECT_TRUE(uz::denoise::Compatible(1.f, 0.f, 1.f, 0.f));
    EXPECT_FALSE(uz::denoise::Compatible(0.89f, 0.f, 1.f, 0.f));
    EXPECT_FALSE(uz::denoise::Compatible(1.f, 1.01f, 1.f, 0.f));
    EXPECT_FALSE(uz::denoise::Compatible(1.f, 0.f, 1.f, 0.1f));
}

TEST(RealtimeDenoiser, ClampPreservesConstantFieldsAndBlack) {
    EXPECT_TRUE(uz::denoise::Eligible(1.f)); // Eligibility does not inspect RGB/luminance.
    EXPECT_TRUE(uz::denoise::Eligible(0.0001f)); // Preserve every positive producer weight.
    EXPECT_FALSE(uz::denoise::Eligible(0.f));
    EXPECT_FLOAT_EQ(uz::denoise::ClampLuminance(12.f, 12.f, 0.f, 0.f), 12.f);
    EXPECT_FLOAT_EQ(uz::denoise::ClampLuminance(0.f, 0.f, 0.f, 4.f), 0.f);
    EXPECT_FLOAT_EQ(uz::denoise::ClampLuminance(100.f, 10.f, 2.f, 4.f), 11.f);
    EXPECT_FLOAT_EQ(uz::denoise::ClampLuminance(100.f, 10.f, 2.f, 0.f), 16.f);
}

TEST(RealtimeDenoiser, MotionShortensHistoryWithoutChangingMirrorPolicy) {
    EXPECT_FLOAT_EQ(uz::denoise::HistoryCap(0.5f, 0.f), 7.f);
    EXPECT_FLOAT_EQ(uz::denoise::HistoryCap(0.5f, 2.f), 2.f);
    uz::RealtimeDenoiser denoiser;
    EXPECT_FALSE(denoiser.HasProducedResult());
    EXPECT_FLOAT_EQ(uz::RealtimeDenoiser::Policy{}.mirrorRoughness, 0.04f);
}

TEST(RealtimeDenoiser, HistoryEpochAndFrameGapsCannotResurrectStaleContent) {
    EXPECT_TRUE(uz::RealtimeHistoryReusable(10,11,7,7,false));
    EXPECT_FALSE(uz::RealtimeHistoryReusable(UINT32_MAX,0,7,7,false));
    EXPECT_FALSE(uz::RealtimeHistoryReusable(10,12,7,7,false));
    EXPECT_FALSE(uz::RealtimeHistoryReusable(10,11,7,8,false));
    EXPECT_FALSE(uz::RealtimeHistoryReusable(10,11,7,7,true));
}
