/// Unit tests for VfxTimestepAccumulator — deterministic sub-stepping.
///
/// Covers:
///   - FrameRate default (1 sub-step per frame, dt mirrored).
///   - FixedRate accumulator residual carried across frames.
///   - Anti-spiral clamp at `maxSubSteps` drops excess accumulator.
///   - NaN/Inf/negative frameDt rejected.
///   - Determinism: identical frame sequences yield identical sub-step counts
///     regardless of total real time.
///   - Degenerate `fixedDt <= 0` falls back to FrameRate mode.
///
/// @ref phase7_7.8.0-vfx-fixed-timestep-accumulator

#include <gtest/gtest.h>

#include "engine/vfx/core/vfx_timestep.h"

#include <cmath>
#include <limits>

using namespace uz;

// ─── FrameRate (default) ───────────────────────────────────────────────────

TEST(VfxTimestepAccumulator, DefaultsToFrameRate) {
    VfxTimestepAccumulator a;
    EXPECT_EQ(a.GetMode(), VfxTimestepMode::FrameRate);
}

TEST(VfxTimestepAccumulator, FrameRateReturnsOneStepAndMirrorsDt) {
    VfxTimestepAccumulator a;
    EXPECT_EQ(a.Advance(1.0f / 60.0f), 1u);
    EXPECT_FLOAT_EQ(a.SubStepDt(), 1.0f / 60.0f);

    EXPECT_EQ(a.Advance(1.0f / 144.0f), 1u);
    EXPECT_FLOAT_EQ(a.SubStepDt(), 1.0f / 144.0f);
}

TEST(VfxTimestepAccumulator, FrameRateRejectsNonPositive) {
    VfxTimestepAccumulator a;
    EXPECT_EQ(a.Advance(0.0f), 0u);
    EXPECT_EQ(a.Advance(-0.001f), 0u);
    EXPECT_EQ(a.Advance(std::numeric_limits<float>::quiet_NaN()), 0u);
    EXPECT_EQ(a.Advance(std::numeric_limits<float>::infinity()), 0u);
}

// ─── FixedRate basic stepping ──────────────────────────────────────────────

TEST(VfxTimestepAccumulator, FixedRateExactMultipleReturnsN) {
    VfxTimestepAccumulator a;
    a.SetFixedRate(1.0f / 60.0f);
    // 4 * (1/60) exactly -> 4 sub-steps, zero residual.
    EXPECT_EQ(a.Advance(4.0f / 60.0f), 4u);
    EXPECT_FLOAT_EQ(a.GetAccumulator(), 0.0f);
    EXPECT_FLOAT_EQ(a.SubStepDt(), 1.0f / 60.0f);
}

TEST(VfxTimestepAccumulator, FixedRateCarriesResidualAcrossFrames) {
    VfxTimestepAccumulator a;
    a.SetFixedRate(1.0f / 60.0f);
    // Frame 1: 2.5 * (1/60). Expect 2 steps with 0.5 * (1/60) residual.
    EXPECT_EQ(a.Advance(2.5f / 60.0f), 2u);
    EXPECT_NEAR(a.GetAccumulator(), 0.5f / 60.0f, 1e-6f);

    // Frame 2: another 1.0 * (1/60). Residual now 1.5, so 1 step + 0.5 carries over.
    EXPECT_EQ(a.Advance(1.0f / 60.0f), 1u);
    EXPECT_NEAR(a.GetAccumulator(), 0.5f / 60.0f, 1e-6f);
}

TEST(VfxTimestepAccumulator, FixedRateSubFixedDtReturnsZero) {
    // Frame too short to trigger a sub-step — accumulator just grows.
    VfxTimestepAccumulator a;
    a.SetFixedRate(1.0f / 30.0f);
    EXPECT_EQ(a.Advance(0.01f), 0u);
    EXPECT_NEAR(a.GetAccumulator(), 0.01f, 1e-6f);
}

// ─── Anti-spiral clamp ─────────────────────────────────────────────────────

TEST(VfxTimestepAccumulator, ClampsAtMaxSubStepsAndDropsExcess) {
    VfxTimestepAccumulator a;
    a.SetFixedRate(1.0f / 60.0f, /*maxSubSteps=*/ 4);
    // 10 * (1/60) would demand 10 steps; clamp to 4 and drop residual.
    EXPECT_EQ(a.Advance(10.0f / 60.0f), 4u);
    EXPECT_FLOAT_EQ(a.GetAccumulator(), 0.0f);
}

TEST(VfxTimestepAccumulator, ZeroMaxSubStepsClampsToOne) {
    // Defensive: passing 0 max-sub-steps should be treated as 1 to avoid a
    // permanently-stalled simulation.
    VfxTimestepAccumulator a;
    a.SetFixedRate(1.0f / 60.0f, /*maxSubSteps=*/ 0);
    EXPECT_EQ(a.GetMaxSubSteps(), 1u);
}

// ─── Degenerate configuration fall-back ────────────────────────────────────

TEST(VfxTimestepAccumulator, NonPositiveFixedDtFallsBackToFrameRate) {
    VfxTimestepAccumulator a;
    a.SetFixedRate(0.0f);
    EXPECT_EQ(a.GetMode(), VfxTimestepMode::FrameRate);

    a.SetFixedRate(-1.0f);
    EXPECT_EQ(a.GetMode(), VfxTimestepMode::FrameRate);

    a.SetFixedRate(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(a.GetMode(), VfxTimestepMode::FrameRate);
}

// ─── Reset ─────────────────────────────────────────────────────────────────

TEST(VfxTimestepAccumulator, ResetClearsResidual) {
    VfxTimestepAccumulator a;
    a.SetFixedRate(1.0f / 60.0f);
    EXPECT_EQ(a.Advance(0.5f / 60.0f), 0u);
    EXPECT_GT(a.GetAccumulator(), 0.0f);

    a.Reset();
    EXPECT_FLOAT_EQ(a.GetAccumulator(), 0.0f);
}

// ─── Determinism across framerates ────────────────────────────────────────

TEST(VfxTimestepAccumulator, IdenticalTotalTimeYieldsSameStepCount) {
    // Summed sub-step count must depend only on total simulated real time,
    // not how that time was sliced up. Small floating-point residuals allowed.
    VfxTimestepAccumulator slow;
    VfxTimestepAccumulator fast;
    slow.SetFixedRate(1.0f / 60.0f);
    fast.SetFixedRate(1.0f / 60.0f);

    // 10 frames at 30 Hz vs 20 frames at 60 Hz — same total 1/3 second.
    uint32_t slowTotal = 0;
    for (int i = 0; i < 10; ++i) {
        slowTotal += slow.Advance(1.0f / 30.0f);
    }

    uint32_t fastTotal = 0;
    for (int i = 0; i < 20; ++i) {
        fastTotal += fast.Advance(1.0f / 60.0f);
    }

    // Both traverse ~0.333s of real time -> ~20 fixed sub-steps at 60Hz.
    // Residuals may differ by 1 step across paths; accept 19-20.
    EXPECT_GE(slowTotal, 19u);
    EXPECT_LE(slowTotal, 20u);
    EXPECT_GE(fastTotal, 19u);
    EXPECT_LE(fastTotal, 20u);
    EXPECT_EQ(slowTotal, fastTotal);
}

// ─── Mode switching ────────────────────────────────────────────────────────

TEST(VfxTimestepAccumulator, SwitchingModesClearsAccumulator) {
    VfxTimestepAccumulator a;
    a.SetFixedRate(1.0f / 60.0f);
    (void)a.Advance(0.005f);
    EXPECT_GT(a.GetAccumulator(), 0.0f);

    a.SetFrameRate();
    EXPECT_FLOAT_EQ(a.GetAccumulator(), 0.0f);
    EXPECT_EQ(a.GetMode(), VfxTimestepMode::FrameRate);
}
