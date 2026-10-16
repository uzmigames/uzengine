#include "engine/renderer/scene/temporal_history_state.h"
#include "engine/renderer/passes/postprocess/fsr2_upscaler.h"

#include <gtest/gtest.h>

namespace {
using namespace uz;

TemporalFrameKey Key(uint32_t frame) {
    TemporalFrameKey key{};
    key.frameIndex = frame;
    key.upscalerType = 1;
    key.outputW = 1920;
    key.outputH = 1080;
    key.cameraFwd = {1.f, 0.f, 0.f};
    key.unjitteredViewProj.m._41 = static_cast<float>(frame);
    return key;
}

class TemporalHistoryStateTest : public ::testing::Test {
protected:
    uz::TemporalHistoryState state;
    TemporalThresholds thresholds;

    void Seed() {
        state.Advance(Key(0), 1, thresholds);
        state.MarkWritten(0);
    }
};

// Use the suite name from the design while keeping the primitive type unambiguous.
using TemporalHistoryState = TemporalHistoryStateTest;

TEST_F(TemporalHistoryState, FirstFrame_NoPrev_BothInvalid) {
    const auto d = state.Advance(Key(0), 1, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_TRUE(d.firstFrame());
    EXPECT_EQ(d.matrixReason, TemporalReset::FirstUse);
    EXPECT_EQ(d.contentReason, TemporalReset::FirstUse);
}

TEST_F(TemporalHistoryState, SecondFrame_WrittenSameKey_BothValid) {
    Seed();
    const auto d = state.Advance(Key(1), 0, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_TRUE(d.contentValid);
    EXPECT_FALSE(d.firstFrame());
    EXPECT_EQ(d.prevViewProj.m._41, 0.f);
    EXPECT_EQ(d.matrixReason, TemporalReset::None);
    EXPECT_EQ(d.contentReason, TemporalReset::None);
    EXPECT_EQ(state.SlotUpscaler(0), 1u);
}

TEST_F(TemporalHistoryState, ExecuteFailed_SlotNotWritten_NextFrameIsFirst) {
    Seed();
    state.Advance(Key(1), 0, thresholds);
    state.MarkFailed(1);
    EXPECT_FALSE(state.SlotWritten(1));
    EXPECT_EQ(state.FailureCount(), 1u);
    EXPECT_EQ(state.LastDecision().contentReason, TemporalReset::ExecuteFailed);
    const auto d = state.Advance(Key(2), 1, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_TRUE(d.firstFrame());
    EXPECT_EQ(d.contentReason, TemporalReset::FrameGap);
    // A failure must also invalidate a previously written slot.
    state.MarkWritten(0);
    state.MarkFailed(0);
    EXPECT_FALSE(state.SlotWritten(0));
    EXPECT_EQ(state.FailureCount(), 2u);
}

TEST_F(TemporalHistoryState, TaaGap_ReadSlotStale_ContentInvalid_MatrixValid) {
    Seed();
    state.Advance(Key(1), 0, thresholds); // TAA inactive: no write.
    const auto d = state.Advance(Key(2), 0, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_TRUE(d.firstFrame());
    EXPECT_EQ(d.contentReason, TemporalReset::FrameGap);
    EXPECT_EQ(d.prevViewProj.m._41, 1.f);
}

TEST_F(TemporalHistoryState, UpscalerChanged_ContentInvalid) {
    Seed();
    auto key = Key(1);
    key.upscalerType = 2;
    auto d = state.Advance(key, 0, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.contentReason, TemporalReset::UpscalerChanged);
    state.MarkWritten(1);
    state.Invalidate(TemporalReset::UpscalerChanged);
    key.frameIndex = 2;
    d = state.Advance(key, 1, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_EQ(d.contentReason, TemporalReset::UpscalerChanged);
}

TEST_F(TemporalHistoryState, OutputSizeChanged_BothInvalid) {
    Seed();
    auto key = Key(1);
    key.outputW = 1280;
    const auto d = state.Advance(key, 0, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.matrixReason, TemporalReset::OutputSizeChanged);
    EXPECT_EQ(d.contentReason, TemporalReset::OutputSizeChanged);
}

TEST_F(TemporalHistoryState, CameraIdChange_IsCut) {
    Seed();
    auto key = Key(1);
    key.cameraId = 1;
    const auto d = state.Advance(key, 0, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.matrixReason, TemporalReset::CameraCut);
}

TEST_F(TemporalHistoryState, TeleportBeyondDistance_IsCut) {
    Seed();
    auto key = Key(1);
    key.cameraPos = {301.f, 0.f, 0.f};
    const auto d = state.Advance(key, 0, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_TRUE(d.firstFrame());
    EXPECT_EQ(d.matrixReason, TemporalReset::CameraCut);
}

TEST_F(TemporalHistoryState, TurnBeyondAngle_IsCut) {
    Seed();
    auto key = Key(1);
    key.cameraFwd = {0.f, 1.f, 0.f};
    const auto d = state.Advance(key, 0, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.matrixReason, TemporalReset::CameraCut);
}

TEST_F(TemporalHistoryState, SmallMotion_IsNotCut) {
    Seed();
    auto key = Key(1);
    key.cameraPos = {3.f, 4.f, 0.f};
    key.cameraFwd = {0.99f, 0.14106736f, 0.f};
    const auto d = state.Advance(key, 0, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_FALSE(d.firstFrame());
}

TEST_F(TemporalHistoryState, InvalidateSceneChanged_ContentOnly) {
    Seed();
    state.Invalidate(TemporalReset::SceneChanged);
    const auto d = state.Advance(Key(1), 0, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.contentReason, TemporalReset::SceneChanged);
    state.MarkWritten(1);
    EXPECT_FALSE(state.Advance(Key(2), 1, thresholds).firstFrame());
}

TEST_F(TemporalHistoryState, ResetAll_ClearsSlotsAndPrev) {
    Seed();
    state.Advance(Key(1), 0, thresholds);
    state.MarkWritten(1);
    state.ResetAll(TemporalReset::ResourcesRecreated);
    EXPECT_FALSE(state.SlotWritten(0));
    EXPECT_FALSE(state.SlotWritten(1));
    EXPECT_EQ(state.SlotFrame(0), 0u);
    EXPECT_EQ(state.SlotUpscaler(1), 0u);
    const auto d = state.Advance(Key(2), 1, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.matrixReason, TemporalReset::ResourcesRecreated);
}

TEST_F(TemporalHistoryState, PingPong_ReadSlotAgeTracksLastWrite) {
    for (uint32_t frame = 0; frame < 10; ++frame) {
        const uint32_t write = frame % 2;
        const auto d = state.Advance(Key(frame), 1 - write, thresholds);
        EXPECT_EQ(d.firstFrame(), frame == 0);
        if (frame > 0) EXPECT_EQ(state.SlotFrame(1 - write), frame - 1);
        state.MarkWritten(write);
        EXPECT_EQ(state.SlotFrame(write), frame);
    }
    // The non-read slot is written, but too old to accept if selected again.
    const auto d = state.Advance(Key(10), 0, thresholds);
    EXPECT_TRUE(d.matrixValid);
    EXPECT_EQ(d.contentReason, TemporalReset::FrameGap);
}

TEST_F(TemporalHistoryState, FrameDiscontinuity_BothInvalid) {
    Seed();
    const auto d = state.Advance(Key(3), 0, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.matrixReason, TemporalReset::FrameGap);
}

TEST_F(TemporalHistoryState, ExplicitCameraCut_BothInvalid) {
    Seed();
    state.Invalidate(TemporalReset::CameraCut);
    state.Invalidate(TemporalReset::SceneChanged);
    const auto d = state.Advance(Key(1), 0, thresholds);
    EXPECT_FALSE(d.matrixValid);
    EXPECT_FALSE(d.contentValid);
    EXPECT_EQ(d.matrixReason, TemporalReset::CameraCut);
}

TEST_F(TemporalHistoryState, ThresholdBoundaries_AreContinuous) {
    Seed();
    auto key = Key(1);
    key.cameraPos = {thresholds.cutDistanceCm, 0.f, 0.f};
    key.cameraFwd = {thresholds.cutCosAngle, 0.5f, 0.f};
    EXPECT_FALSE(state.Advance(key, 0, thresholds).firstFrame());
}

#if !UZ_FSR2_ENABLED
TEST(TemporalUpscalerWrite, Fsr2StubExecute_ReturnsFalse) {
    FSR2Upscaler upscaler;
    EXPECT_FALSE(upscaler.Execute(nullptr, {}));
}
#endif
} // namespace
