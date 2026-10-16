#include "keyframe_gate.h"

#include <gtest/gtest.h>

TEST(KeyframeGate, FreshGate_HoldsDeltasUntilFirstKey) {
    uz::KeyframeGate gate;
    EXPECT_TRUE(gate.AwaitingKeyframe());
    EXPECT_FALSE(gate.ShouldDeliver(false));
    EXPECT_FALSE(gate.ShouldDeliver(false));
    EXPECT_TRUE(gate.ShouldDeliver(true));
    EXPECT_FALSE(gate.AwaitingKeyframe());
}

TEST(KeyframeGate, AfterKey_DeliversDeltas) {
    uz::KeyframeGate gate;
    ASSERT_TRUE(gate.ShouldDeliver(true));
    EXPECT_TRUE(gate.ShouldDeliver(false));
    EXPECT_TRUE(gate.ShouldDeliver(false));
    EXPECT_FALSE(gate.AwaitingKeyframe());
}

TEST(KeyframeGate, AfterDrop_HoldsDeltasUntilNextKey_ThenReopensOnce) {
    uz::KeyframeGate gate;
    ASSERT_TRUE(gate.ShouldDeliver(true));
    ASSERT_TRUE(gate.ShouldDeliver(false));
    gate.OnDropped();
    EXPECT_TRUE(gate.AwaitingKeyframe());
    EXPECT_FALSE(gate.ShouldDeliver(false));
    EXPECT_FALSE(gate.ShouldDeliver(false));
    EXPECT_TRUE(gate.ShouldDeliver(true));
    EXPECT_TRUE(gate.ShouldDeliver(false));
    EXPECT_FALSE(gate.AwaitingKeyframe());
}

TEST(KeyframeGate, DropWhileAwaiting_StaysArmed) {
    uz::KeyframeGate gate;
    gate.OnDropped();
    gate.OnDropped();
    EXPECT_TRUE(gate.AwaitingKeyframe());
    EXPECT_FALSE(gate.ShouldDeliver(false));
    EXPECT_TRUE(gate.ShouldDeliver(true));
}

TEST(KeyframeGate, ArmAfterKey_HoldsDeltasUntilNewKey) {
    uz::KeyframeGate gate;
    ASSERT_TRUE(gate.ShouldDeliver(true));
    gate.Arm();
    EXPECT_TRUE(gate.AwaitingKeyframe());
    EXPECT_FALSE(gate.ShouldDeliver(false));
    EXPECT_TRUE(gate.ShouldDeliver(true));
    EXPECT_TRUE(gate.ShouldDeliver(false));
}

TEST(KeyframeGate, DroppedRecoveryKey_RequiresAnotherKey) {
    uz::KeyframeGate gate;
    ASSERT_TRUE(gate.ShouldDeliver(true));
    gate.OnDropped();
    ASSERT_TRUE(gate.ShouldDeliver(true));
    gate.OnDropped(); // Recovery IDR could not enter the bounded queue.
    EXPECT_FALSE(gate.ShouldDeliver(false));
    EXPECT_TRUE(gate.ShouldDeliver(true));
}

TEST(NalUtils, IdrAfterSpsPps_IsKey) {
    const uint8_t data[] = {
        0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1E,
        0, 0, 0, 1, 0x68, 0xCE, 0x06,
        0, 0, 0, 1, 0x65, 0x88
    };
    EXPECT_TRUE(uz::IsAnnexBKeyframe(data, sizeof(data)));
}

TEST(NalUtils, AudThenIdr_IsKey) {
    const uint8_t data[] = {0, 0, 1, 0x09, 0xF0, 0, 0, 0, 1, 0x65, 0x88};
    EXPECT_TRUE(uz::IsAnnexBKeyframe(data, sizeof(data)));
}

TEST(NalUtils, NonIdrSlice_IsNotKey) {
    for (uint8_t type = 1; type <= 4; ++type) {
        const uint8_t data[] = {0, 0, 0, 1, type, 0x88};
        EXPECT_FALSE(uz::IsAnnexBKeyframe(data, sizeof(data))) << int(type);
    }
}

TEST(NalUtils, FirstVclDeterminesKeyframe) {
    const uint8_t deltaFirst[] = {0, 0, 1, 0x41, 0x88, 0, 0, 1, 0x65, 0x88};
    const uint8_t keyFirst[] = {0, 0, 1, 0x65, 0x88, 0, 0, 1, 0x41, 0x88};
    EXPECT_FALSE(uz::IsAnnexBKeyframe(deltaFirst, sizeof(deltaFirst)));
    EXPECT_TRUE(uz::IsAnnexBKeyframe(keyFirst, sizeof(keyFirst)));
}

TEST(NalUtils, SpsPpsOnly_IsNotKey) {
    const uint8_t data[] = {
        0, 0, 0, 1, 0x67, 0x42,
        0, 0, 1, 0x68, 0xCE
    };
    EXPECT_FALSE(uz::IsAnnexBKeyframe(data, sizeof(data)));
}

TEST(NalUtils, TruncatedOrEmpty_IsNotKey) {
    EXPECT_FALSE(uz::IsAnnexBKeyframe(nullptr, 0));
    EXPECT_FALSE(uz::IsAnnexBKeyframe(nullptr, 8));
    const uint8_t fourBytePrefix[] = {0, 0, 0, 1};
    for (size_t size = 0; size <= sizeof(fourBytePrefix); ++size) {
        EXPECT_FALSE(uz::IsAnnexBKeyframe(fourBytePrefix, size)) << size;
    }
    const uint8_t threeBytePrefix[] = {0, 0, 1};
    EXPECT_FALSE(uz::IsAnnexBKeyframe(threeBytePrefix, sizeof(threeBytePrefix)));
    const uint8_t noStartCode[] = {0x65, 0x88, 0x01};
    EXPECT_FALSE(uz::IsAnnexBKeyframe(noStartCode, sizeof(noStartCode)));
}

TEST(NalUtils, ThreeByteStartCode_IsHandled) {
    const uint8_t data[] = {0, 0, 1, 0x65, 0x88};
    EXPECT_TRUE(uz::IsAnnexBKeyframe(data, sizeof(data)));
}

TEST(NalUtils, EmulationPrevention_IsNotStartCode) {
    const uint8_t data[] = {0, 0, 1, 0x67, 0, 0, 3, 1, 0x65};
    EXPECT_FALSE(uz::IsAnnexBKeyframe(data, sizeof(data)));
}
