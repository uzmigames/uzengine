/// Unit tests for TimeManager — Init state, delta time clamping, time scaling,
/// fixed-timestep accumulator, ConsumeFixedTicks, SetFixedDeltaTime, SetTimeScale,
/// pause behavior, frame counting, and first-frame zero-delta guarantee.

#include "time_manager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — fresh TimeManager for each test
// ─────────────────────────────────────────────────────────────────────────────

class TimeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tm_.Init());
    }

    TimeManager tm_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Init resets all state
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, Init_ResetsDeltaTimeToZero) {
    EXPECT_FLOAT_EQ(tm_.GetDeltaTime(), 0.0f);
}

TEST_F(TimeManagerTest, Init_ResetsTotalTimeToZero) {
    EXPECT_FLOAT_EQ(tm_.GetTotalTime(), 0.0f);
}

TEST_F(TimeManagerTest, Init_ResetsFrameCountToZero) {
    EXPECT_EQ(tm_.GetFrameCount(), 0u);
}

TEST_F(TimeManagerTest, Init_ResetsRawDeltaTimeToZero) {
    EXPECT_FLOAT_EQ(tm_.GetRawDeltaTime(), 0.0f);
}

TEST_F(TimeManagerTest, Init_SetsDefaultFixedDeltaTime) {
    EXPECT_FLOAT_EQ(tm_.GetFixedDeltaTime(), kDefaultFixedDeltaTime);
}

TEST_F(TimeManagerTest, Init_SetsDefaultTimeScaleToOne) {
    EXPECT_FLOAT_EQ(tm_.GetTimeScale(), 1.0f);
}

TEST_F(TimeManagerTest, Init_ReturnsTrue) {
    TimeManager fresh;
    EXPECT_TRUE(fresh.Init());
}

TEST_F(TimeManagerTest, Init_AfterModification_ResetsAllFields) {
    tm_.SetTimeScale(0.5f);
    tm_.SetFixedDeltaTime(1.0f / 30.0f);
    tm_.BeginFrame(); // bump frameCount_

    ASSERT_TRUE(tm_.Init());

    EXPECT_FLOAT_EQ(tm_.GetTimeScale(),      1.0f);
    EXPECT_FLOAT_EQ(tm_.GetFixedDeltaTime(), kDefaultFixedDeltaTime);
    EXPECT_EQ(tm_.GetFrameCount(),           0u);
    EXPECT_FLOAT_EQ(tm_.GetDeltaTime(),      0.0f);
    EXPECT_FLOAT_EQ(tm_.GetTotalTime(),      0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// First BeginFrame — must report zero delta
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, FirstBeginFrame_DeltaTimeIsZero) {
    tm_.BeginFrame();
    EXPECT_FLOAT_EQ(tm_.GetDeltaTime(), 0.0f);
}

TEST_F(TimeManagerTest, FirstBeginFrame_RawDeltaTimeIsZero) {
    tm_.BeginFrame();
    EXPECT_FLOAT_EQ(tm_.GetRawDeltaTime(), 0.0f);
}

TEST_F(TimeManagerTest, FirstBeginFrame_DoesNotIncrementFrameCount) {
    // First BeginFrame records the timestamp but must NOT count as a completed frame.
    // Looking at the implementation: frameCount_ is only incremented on non-first frames.
    tm_.BeginFrame();
    EXPECT_EQ(tm_.GetFrameCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame counting
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, FrameCount_IncreasesEachNonFirstBeginFrame) {
    tm_.BeginFrame(); // first frame — no increment
    tm_.BeginFrame(); // second call — frameCount becomes 1
    EXPECT_EQ(tm_.GetFrameCount(), 1u);
}

TEST_F(TimeManagerTest, FrameCount_AfterTenFrames_IsNine) {
    // First BeginFrame = 0 count, each subsequent = +1
    for (int i = 0; i < 10; ++i) {
        tm_.BeginFrame();
    }
    EXPECT_EQ(tm_.GetFrameCount(), 9u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Delta time — real timing
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, SecondBeginFrame_DeltaTimeIsPositive) {
    tm_.BeginFrame(); // first — zero delta
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    tm_.BeginFrame(); // second — measures real elapsed

    EXPECT_GT(tm_.GetDeltaTime(), 0.0f);
}

TEST_F(TimeManagerTest, TotalTime_AccumulatesOverFrames) {
    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    tm_.BeginFrame();

    EXPECT_GT(tm_.GetTotalTime(), 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Delta time clamping
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, DeltaTimeClamping_NeverExceedsMaxDeltaTime) {
    // Sleep just past the clamping threshold (kMaxDeltaTime is typically 0.25s = 250ms).
    // 300ms is sufficient to exceed it without wasting time.
    tm_.BeginFrame(); // record base timestamp
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    tm_.BeginFrame();

    EXPECT_LE(tm_.GetDeltaTime(), kMaxDeltaTime)
        << "deltaTime must be clamped to kMaxDeltaTime=" << kMaxDeltaTime;
}

TEST_F(TimeManagerTest, RawDeltaTime_NotClamped_CanExceedMaxDeltaTime) {
    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    tm_.BeginFrame();

    // rawDeltaTime_ records the actual elapsed time before clamping
    EXPECT_GT(tm_.GetRawDeltaTime(), kMaxDeltaTime)
        << "rawDeltaTime must reflect actual wall-clock elapsed (> kMaxDeltaTime after 300ms sleep)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Time scale
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, TimeScale_HalfSpeed_DeltaTimeIsHalved) {
    tm_.SetTimeScale(0.5f);

    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tm_.BeginFrame();

    const float dt      = tm_.GetDeltaTime();
    const float rawDt   = tm_.GetRawDeltaTime();

    // dt should be approximately rawDt * 0.5
    EXPECT_NEAR(dt, rawDt * 0.5f, rawDt * 0.05f) // within 5% tolerance
        << "deltaTime must equal rawDeltaTime * 0.5 with timeScale=0.5";
}

TEST_F(TimeManagerTest, TimeScale_DoubleSpeed_DeltaTimeIsDoubled) {
    tm_.SetTimeScale(2.0f);

    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tm_.BeginFrame();

    const float dt    = tm_.GetDeltaTime();
    const float rawDt = tm_.GetRawDeltaTime();

    EXPECT_NEAR(dt, rawDt * 2.0f, rawDt * 0.05f);
}

// ─────────────────────────────────────────────────────────────────────────────
// SetTimeScale — clamps to [0, 10]
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, SetTimeScale_NegativeValue_ClampedToZero) {
    tm_.SetTimeScale(-1.0f);
    EXPECT_FLOAT_EQ(tm_.GetTimeScale(), 0.0f);
}

TEST_F(TimeManagerTest, SetTimeScale_ExceedsMax_ClampedToTen) {
    tm_.SetTimeScale(100.0f);
    EXPECT_FLOAT_EQ(tm_.GetTimeScale(), 10.0f);
}

TEST_F(TimeManagerTest, SetTimeScale_ValidValue_Applied) {
    tm_.SetTimeScale(3.5f);
    EXPECT_FLOAT_EQ(tm_.GetTimeScale(), 3.5f);
}

TEST_F(TimeManagerTest, SetTimeScale_Zero_Accepted) {
    tm_.SetTimeScale(0.0f);
    EXPECT_FLOAT_EQ(tm_.GetTimeScale(), 0.0f);
}

TEST_F(TimeManagerTest, SetTimeScale_Ten_Accepted) {
    tm_.SetTimeScale(10.0f);
    EXPECT_FLOAT_EQ(tm_.GetTimeScale(), 10.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pause (timeScale=0) — deltaTime=0, no fixed ticks
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, Pause_TimeScaleZero_DeltaTimeIsZero) {
    tm_.SetTimeScale(0.0f);

    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tm_.BeginFrame();

    EXPECT_FLOAT_EQ(tm_.GetDeltaTime(), 0.0f);
}

TEST_F(TimeManagerTest, Pause_TimeScaleZero_NoFixedTicks) {
    tm_.SetTimeScale(0.0f);

    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); // some wall time
    tm_.BeginFrame();

    EXPECT_EQ(tm_.ConsumeFixedTicks(), 0)
        << "With timeScale=0, no fixed ticks should accumulate";
}

TEST_F(TimeManagerTest, Pause_TimeScaleZero_TotalTimeDoesNotAdvance) {
    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tm_.BeginFrame(); // accumulate some totalTime

    tm_.SetTimeScale(0.0f);
    const float totalBefore = tm_.GetTotalTime();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tm_.BeginFrame();

    EXPECT_FLOAT_EQ(tm_.GetTotalTime(), totalBefore)
        << "Total time must not advance while paused";
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixed timestep accumulator
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, ConsumeFixedTicks_OnFirstFrame_ReturnsZero) {
    tm_.BeginFrame(); // first frame: deltaTime=0, nothing accumulates
    EXPECT_EQ(tm_.ConsumeFixedTicks(), 0);
}

TEST_F(TimeManagerTest, ConsumeFixedTicks_OneFixedStepFrame_ReturnsOne) {
    // Fixed step = 1/60 ≈ 0.01667s. Inject exactly one fixed step worth via time scale trick.
    // We use a direct approach: set a large fixed step so a real-time sleep covers exactly one.
    tm_.SetFixedDeltaTime(0.05f); // 20 Hz fixed step

    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(60)); // > 50ms → 1 tick
    tm_.BeginFrame();

    const int ticks = tm_.ConsumeFixedTicks();
    EXPECT_GE(ticks, 1) << "At least one fixed tick expected after 60ms with 50ms fixed step";
}

TEST_F(TimeManagerTest, ConsumeFixedTicks_TwoStepsPerFrame_ReturnsTwoOrMore) {
    // 1/30s frame should produce 2 fixed ticks at 1/60s
    tm_.SetFixedDeltaTime(1.0f / 60.0f);

    tm_.BeginFrame();
    // Sleep for ~35ms (> 2 * 16.67ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    tm_.BeginFrame();

    const int ticks = tm_.ConsumeFixedTicks();
    EXPECT_GE(ticks, 2) << "35ms frame with 1/60s fixed step should produce >= 2 fixed ticks";
}

TEST_F(TimeManagerTest, ConsumeFixedTicks_DrainsAccumulator) {
    tm_.SetFixedDeltaTime(0.05f);

    tm_.BeginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    tm_.BeginFrame();

    tm_.ConsumeFixedTicks(); // drain

    // Calling again immediately without new BeginFrame should return 0
    EXPECT_EQ(tm_.ConsumeFixedTicks(), 0)
        << "ConsumeFixedTicks must drain the accumulator — second call returns 0";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetFixedDeltaTime
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, SetFixedDeltaTime_ChangesValue) {
    const float newStep = 1.0f / 30.0f;
    tm_.SetFixedDeltaTime(newStep);
    EXPECT_FLOAT_EQ(tm_.GetFixedDeltaTime(), newStep);
}

TEST_F(TimeManagerTest, SetFixedDeltaTime_VerySmallPositive_IsAccepted) {
    tm_.SetFixedDeltaTime(0.001f);
    EXPECT_FLOAT_EQ(tm_.GetFixedDeltaTime(), 0.001f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ISubsystem interface
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TimeManagerTest, GetName_ReturnsTimeManager) {
    EXPECT_STREQ(tm_.GetName(), "TimeManager");
}

TEST_F(TimeManagerTest, GetInitPhase_IsPreInit) {
    EXPECT_EQ(tm_.GetInitPhase(), InitPhase::PreInit);
}

TEST_F(TimeManagerTest, GetTickGroup_IsNone) {
    EXPECT_EQ(tm_.GetTickGroup(), TickGroup::None);
}

TEST_F(TimeManagerTest, Shutdown_IsNoOp_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(tm_.Shutdown());
}
