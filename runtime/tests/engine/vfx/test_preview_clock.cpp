/// Unit tests for the VFX editor preview clock (phase 8.4.0).
///
/// Covers every transition of the `VfxPreviewClock` state machine plus the
/// fixed-timestep determinism guarantee: the clock yields a single dt per
/// Tick, so wiring it through an accumulator cannot desync the simulation.
///
/// @ref engine/vfx/editor/vfx_preview_clock.h
/// @ref docs/engine/25-vfx/21-preview-timeline.md

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_preview_clock.h"

#include <cmath>

using namespace uz;

namespace {

constexpr float kDefaultDt = 1.0f / 60.0f;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxPreviewClock, DefaultState_IsPlayingAtOneX) {
    VfxPreviewClock clock;
    EXPECT_EQ(clock.GetState(), VfxPreviewState::Playing);
    EXPECT_FLOAT_EQ(clock.GetPlayRate(), 1.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
    EXPECT_TRUE(clock.IsPlaying());
    EXPECT_FALSE(clock.IsHalted());
}

// ─────────────────────────────────────────────────────────────────────────────
// Playing
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxPreviewClock, Playing_PassesWallDtScaledByPlayRate) {
    VfxPreviewClock clock;
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), kDefaultDt);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), kDefaultDt);

    clock.SetPlayRate(2.0f);
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), kDefaultDt * 2.0f);
}

TEST(VfxPreviewClock, Playing_NegativeWallDtClampedToZero) {
    VfxPreviewClock clock;
    EXPECT_FLOAT_EQ(clock.Tick(-0.5f), 0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
}

TEST(VfxPreviewClock, PlayRateClampedToSaneRange) {
    VfxPreviewClock clock;
    clock.SetPlayRate(-1.0f);
    EXPECT_FLOAT_EQ(clock.GetPlayRate(), 0.0f);
    clock.SetPlayRate(999.0f);
    EXPECT_FLOAT_EQ(clock.GetPlayRate(), 8.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Paused / Frozen
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxPreviewClock, Paused_YieldsZeroDt) {
    VfxPreviewClock clock;
    clock.Pause();
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), 0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
    EXPECT_TRUE(clock.IsHalted());
}

TEST(VfxPreviewClock, Frozen_DistinctFromPausedButIdenticalDt) {
    VfxPreviewClock clock;
    clock.Freeze();
    EXPECT_EQ(clock.GetState(), VfxPreviewState::Frozen);
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), 0.0f);
    EXPECT_TRUE(clock.IsHalted());
}

// ─────────────────────────────────────────────────────────────────────────────
// Step-frame
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxPreviewClock, StepFrame_AdvancesOnceThenAutoPauses) {
    VfxPreviewClock clock;
    clock.Pause();

    clock.StepFrame(0.01f);
    EXPECT_EQ(clock.GetState(), VfxPreviewState::Stepping);
    // Wall dt is ignored — the step dt drives the advance.
    EXPECT_FLOAT_EQ(clock.Tick(1.0f), 0.01f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.01f);
    // Auto-Paused: second Tick yields 0.
    EXPECT_EQ(clock.GetState(), VfxPreviewState::Paused);
    EXPECT_FLOAT_EQ(clock.Tick(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.01f);
}

TEST(VfxPreviewClock, StepFrame_NoArgUsesDefaultStepDt) {
    VfxPreviewClockConfig cfg;
    cfg.defaultStepDt = 0.02f;
    VfxPreviewClock clock(cfg);
    clock.Pause();

    clock.StepFrame(0.0f);
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), 0.02f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scrubbing
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxPreviewClock, Scrub_JumpsTimeAndReportsDeltaOnce) {
    VfxPreviewClock clock;
    clock.Pause();
    clock.Tick(kDefaultDt); // cleared (Paused)
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);

    clock.Scrub(2.0f);
    EXPECT_EQ(clock.GetState(), VfxPreviewState::Scrubbing);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 2.0f);

    // Scrub delta reported once on next Tick.
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), 2.0f);
    EXPECT_EQ(clock.GetState(), VfxPreviewState::Paused);
    // Subsequent ticks yield 0 — no re-issue of the same delta.
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), 0.0f);
}

TEST(VfxPreviewClock, Scrub_ClampsToMaxDuration) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 5.0f;
    VfxPreviewClock clock(cfg);
    clock.Scrub(999.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 5.0f);
}

TEST(VfxPreviewClock, Scrub_NegativeTargetClampedToZero) {
    VfxPreviewClock clock;
    clock.Scrub(1.0f);
    clock.Tick(kDefaultDt);
    clock.Scrub(-1.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), -1.0f); // jump back 1s
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxPreviewClock, Reset_ZerosTimeAndPauses) {
    VfxPreviewClock clock;
    clock.Tick(2.0f);
    clock.StepFrame(0.1f);

    clock.Reset();
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
    EXPECT_EQ(clock.GetState(), VfxPreviewState::Paused);
    // Pending step is cleared.
    EXPECT_FLOAT_EQ(clock.Tick(kDefaultDt), 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixed-timestep determinism
// ─────────────────────────────────────────────────────────────────────────────

/// Running the same sequence through two independently-constructed clocks
/// must produce identical `currentTime_` trajectories (no hidden state,
/// deterministic sequence). Guards against accidental wall-clock coupling.
TEST(VfxPreviewClock, DeterministicAcrossIdenticalSequences) {
    VfxPreviewClock a, b;
    a.Pause(); b.Pause();

    const float steps[] = {0.01f, 0.02f, 0.01f, 0.05f};
    for (float s : steps) {
        a.StepFrame(s);
        b.StepFrame(s);
        const float da = a.Tick(kDefaultDt);
        const float db = b.Tick(kDefaultDt);
        EXPECT_FLOAT_EQ(da, db);
        EXPECT_FLOAT_EQ(a.GetCurrentTime(), b.GetCurrentTime());
    }
}

/// Scrub delta feeding into a fixed-timestep accumulator must sum cleanly:
/// the accumulator + step loop below consumes the scrub jump as regular
/// dt and re-syncs without residue. Guards against the clock silently
/// splitting a jump across multiple Ticks.
TEST(VfxPreviewClock, ScrubDeltaIntegratesIntoFixedAccumulator) {
    VfxPreviewClock clock;
    clock.Pause();
    clock.Scrub(1.0f);

    const float dt    = clock.Tick(0.0f); // scrub jump delta
    constexpr float kFixed = 1.0f / 60.0f;

    float accumulator = dt;
    int fixedTicks = 0;
    while (accumulator + 1e-6f >= kFixed) {
        accumulator -= kFixed;
        ++fixedTicks;
    }

    // 1.0s / (1/60) = exactly 60 fixed ticks.
    EXPECT_EQ(fixedTicks, 60);
    EXPECT_LT(std::fabs(accumulator), 1e-5f);
}
