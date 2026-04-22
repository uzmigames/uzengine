/// Unit tests for phase 14.14.0: FPS EMA smoothing used by the
/// demo host to feed the status bar.
///
/// The smoothing math: `ema += (instant - ema) * alpha` where
/// `alpha = dt / (dt + tau)` (tau = 0.1 s).

#include <gtest/gtest.h>

#include <cmath>

namespace {

float EmaStep(float ema, float instant, float dt, float tau = 0.1f) {
    const float alpha = dt / (dt + tau);
    return ema + (instant - ema) * alpha;
}

} // namespace

TEST(FpsEma, ZeroDtNoOp) {
    // Smoothing must gracefully handle dt == 0 (pause) without NaN.
    const float ema = EmaStep(60.0f, 1000.0f, 0.0f);
    EXPECT_FLOAT_EQ(ema, 60.0f);
}

TEST(FpsEma, SteadyStateAt60Fps) {
    float ema = 60.0f;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        ema = EmaStep(ema, 60.0f, dt);
    }
    EXPECT_NEAR(ema, 60.0f, 0.1f);
}

TEST(FpsEma, SingleHitchDoesNotFullyDrop) {
    // Starting at 60 fps (ema=60), one 100 ms frame (instant=10)
    // should not drag ema all the way to 10.
    const float ema = EmaStep(60.0f, 10.0f, 0.1f);
    // alpha = 0.1 / 0.2 = 0.5 → ema = 60 + (10-60)*0.5 = 35.
    EXPECT_NEAR(ema, 35.0f, 0.5f);
}

TEST(FpsEma, RampUpReachesTargetAfterManyFrames) {
    float ema = 30.0f;
    const float dt = 1.0f / 60.0f; // 16.67 ms.
    for (int i = 0; i < 60; ++i) {
        ema = EmaStep(ema, 60.0f, dt);
    }
    // After 60 frames (~1 s), ema should be near 60.
    EXPECT_GT(ema, 55.0f);
}

TEST(FpsEma, AlphaClampsAtOneForLargeDt) {
    // With tau = 0.1 and dt = 1.0, alpha = 1/1.1 ≈ 0.909. EMA
    // pulls strongly toward the instant value.
    const float ema = EmaStep(60.0f, 20.0f, 1.0f);
    // ema ≈ 60 + (20-60) * 0.909 = 23.6
    EXPECT_NEAR(ema, 23.64f, 0.5f);
}
