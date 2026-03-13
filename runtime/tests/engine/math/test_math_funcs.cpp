/// Unit tests for Math utility functions — clamp, lerp, inverse_lerp, snap,
/// is_zero_approx, is_equal_approx, smoothstep, remap, step, fposmod, posmod,
/// sign, fract, linear_to_db, db_to_linear, cubic_interpolate, ease,
/// deg_to_rad, rad_to_deg.
#include "math_funcs.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace uz;

static constexpr float kEps  = 1e-5f;

// ── clamp ────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Clamp_InRange_Unchanged) {
    EXPECT_FLOAT_EQ(Math::clamp(0.5f, 0.0f, 1.0f), 0.5f);
}

TEST(MathFuncsTest, Clamp_BelowMin_ReturnsMin) {
    EXPECT_FLOAT_EQ(Math::clamp(-5.0f, 0.0f, 1.0f), 0.0f);
}

TEST(MathFuncsTest, Clamp_AboveMax_ReturnsMax) {
    EXPECT_FLOAT_EQ(Math::clamp(10.0f, 0.0f, 1.0f), 1.0f);
}

TEST(MathFuncsTest, Clamp_AtMin_ReturnsMin) {
    EXPECT_FLOAT_EQ(Math::clamp(0.0f, 0.0f, 1.0f), 0.0f);
}

TEST(MathFuncsTest, Clamp_AtMax_ReturnsMax) {
    EXPECT_FLOAT_EQ(Math::clamp(1.0f, 0.0f, 1.0f), 1.0f);
}

TEST(MathFuncsTest, Clamp_NegativeRange) {
    EXPECT_FLOAT_EQ(Math::clamp(-2.5f, -5.0f, -1.0f), -2.5f);
    EXPECT_FLOAT_EQ(Math::clamp(-10.0f, -5.0f, -1.0f), -5.0f);
    EXPECT_FLOAT_EQ(Math::clamp(0.0f, -5.0f, -1.0f), -1.0f);
}

// ── lerp ─────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Lerp_AtZero_ReturnsA) {
    EXPECT_FLOAT_EQ(Math::lerp(1.0f, 5.0f, 0.0f), 1.0f);
}

TEST(MathFuncsTest, Lerp_AtOne_ReturnsB) {
    EXPECT_FLOAT_EQ(Math::lerp(1.0f, 5.0f, 1.0f), 5.0f);
}

TEST(MathFuncsTest, Lerp_AtHalf_ReturnsMidpoint) {
    EXPECT_FLOAT_EQ(Math::lerp(0.0f, 10.0f, 0.5f), 5.0f);
}

TEST(MathFuncsTest, Lerp_Extrapolation_ExceedsRange) {
    EXPECT_FLOAT_EQ(Math::lerp(0.0f, 10.0f, 2.0f), 20.0f);
}

TEST(MathFuncsTest, Lerp_NegativeT_GoesBeforeA) {
    EXPECT_FLOAT_EQ(Math::lerp(0.0f, 10.0f, -0.5f), -5.0f);
}

// ── inverse_lerp ─────────────────────────────────────────────────────────────

TEST(MathFuncsTest, InverseLerp_AtStart_ReturnsZero) {
    EXPECT_FLOAT_EQ(Math::inverse_lerp(0.0f, 10.0f, 0.0f), 0.0f);
}

TEST(MathFuncsTest, InverseLerp_AtEnd_ReturnsOne) {
    EXPECT_FLOAT_EQ(Math::inverse_lerp(0.0f, 10.0f, 10.0f), 1.0f);
}

TEST(MathFuncsTest, InverseLerp_AtMid_ReturnsHalf) {
    EXPECT_FLOAT_EQ(Math::inverse_lerp(0.0f, 10.0f, 5.0f), 0.5f);
}

TEST(MathFuncsTest, InverseLerp_LerpInverseLerp_RoundTrip) {
    float a = 3.0f, b = 9.0f, t = 0.4f;
    float val = Math::lerp(a, b, t);
    EXPECT_NEAR(Math::inverse_lerp(a, b, val), t, kEps);
}

// ── snap ─────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Snap_RoundsToNearestMultiple) {
    EXPECT_FLOAT_EQ(Math::snap(3.7f, 2.0f), 4.0f);
    EXPECT_FLOAT_EQ(Math::snap(3.0f, 2.0f), 4.0f);
    EXPECT_FLOAT_EQ(Math::snap(2.9f, 2.0f), 2.0f);  // just below rounding boundary
}

TEST(MathFuncsTest, Snap_ZeroStep_ReturnsOriginal) {
    EXPECT_FLOAT_EQ(Math::snap(3.7f, 0.0f), 3.7f);
}

TEST(MathFuncsTest, Snap_NegativeValue_SnapsCorrectly) {
    EXPECT_FLOAT_EQ(Math::snap(-3.7f, 2.0f), -4.0f);
}

TEST(MathFuncsTest, Snap_ExactMultiple_Unchanged) {
    EXPECT_FLOAT_EQ(Math::snap(6.0f, 2.0f), 6.0f);
}

// ── is_zero_approx ───────────────────────────────────────────────────────────

TEST(MathFuncsTest, IsZeroApprox_ExactlyZero_ReturnsTrue) {
    EXPECT_TRUE(Math::is_zero_approx(0.0f));
}

TEST(MathFuncsTest, IsZeroApprox_SmallValue_ReturnsTrue) {
    EXPECT_TRUE(Math::is_zero_approx(1e-7f));
    EXPECT_TRUE(Math::is_zero_approx(-1e-7f));
}

TEST(MathFuncsTest, IsZeroApprox_AboveEpsilon_ReturnsFalse) {
    EXPECT_FALSE(Math::is_zero_approx(1e-5f));
    EXPECT_FALSE(Math::is_zero_approx(0.001f));
}

// ── is_equal_approx ──────────────────────────────────────────────────────────

TEST(MathFuncsTest, IsEqualApprox_SameValues_ReturnsTrue) {
    EXPECT_TRUE(Math::is_equal_approx(1.0f, 1.0f));
}

TEST(MathFuncsTest, IsEqualApprox_TinyDifference_ReturnsTrue) {
    EXPECT_TRUE(Math::is_equal_approx(1.0f, 1.0f + 1e-7f));
}

TEST(MathFuncsTest, IsEqualApprox_LargeDifference_ReturnsFalse) {
    EXPECT_FALSE(Math::is_equal_approx(1.0f, 2.0f));
}

TEST(MathFuncsTest, IsEqualApprox_AboveEpsilon_ReturnsFalse) {
    EXPECT_FALSE(Math::is_equal_approx(0.0f, Math::EPSILON * 2.0f));
}

// ── smoothstep ───────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Smoothstep_AtFromEdge_ReturnsZero) {
    EXPECT_NEAR(Math::smoothstep(0.0f, 1.0f, 0.0f), 0.0f, kEps);
}

TEST(MathFuncsTest, Smoothstep_AtToEdge_ReturnsOne) {
    EXPECT_NEAR(Math::smoothstep(0.0f, 1.0f, 1.0f), 1.0f, kEps);
}

TEST(MathFuncsTest, Smoothstep_AtMid_IsHalf) {
    EXPECT_NEAR(Math::smoothstep(0.0f, 1.0f, 0.5f), 0.5f, kEps);
}

TEST(MathFuncsTest, Smoothstep_BelowFrom_ClampsToZero) {
    EXPECT_NEAR(Math::smoothstep(0.0f, 1.0f, -1.0f), 0.0f, kEps);
}

TEST(MathFuncsTest, Smoothstep_AboveTo_ClampsToOne) {
    EXPECT_NEAR(Math::smoothstep(0.0f, 1.0f, 2.0f), 1.0f, kEps);
}

TEST(MathFuncsTest, Smoothstep_ValueIsSymmetricAroundMid) {
    float val = 0.3f;
    float s1 = Math::smoothstep(0.0f, 1.0f, val);
    float s2 = Math::smoothstep(0.0f, 1.0f, 1.0f - val);
    EXPECT_NEAR(s1 + s2, 1.0f, kEps);
}

// ── remap ────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Remap_StartValue_MapsToOutputStart) {
    EXPECT_FLOAT_EQ(Math::remap(0.0f, 0.0f, 1.0f, 10.0f, 20.0f), 10.0f);
}

TEST(MathFuncsTest, Remap_EndValue_MapsToOutputEnd) {
    EXPECT_FLOAT_EQ(Math::remap(1.0f, 0.0f, 1.0f, 10.0f, 20.0f), 20.0f);
}

TEST(MathFuncsTest, Remap_MidValue_MapsToOutputMid) {
    EXPECT_FLOAT_EQ(Math::remap(0.5f, 0.0f, 1.0f, 10.0f, 20.0f), 15.0f);
}

// ── step ─────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Step_ValueBelowEdge_ReturnsZero) {
    EXPECT_FLOAT_EQ(Math::step(1.0f, 0.5f), 0.0f);
}

TEST(MathFuncsTest, Step_ValueAtEdge_ReturnsOne) {
    EXPECT_FLOAT_EQ(Math::step(1.0f, 1.0f), 1.0f);
}

TEST(MathFuncsTest, Step_ValueAboveEdge_ReturnsOne) {
    EXPECT_FLOAT_EQ(Math::step(1.0f, 2.0f), 1.0f);
}

// ── fposmod ──────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Fposmod_PositiveValue_ReturnsRemainder) {
    EXPECT_NEAR(Math::fposmod(5.5f, 3.0f), 2.5f, kEps);
}

TEST(MathFuncsTest, Fposmod_NegativeValue_ReturnsPositive) {
    // -1.0 mod 3.0 → 2.0 (wrap into [0, 3))
    EXPECT_NEAR(Math::fposmod(-1.0f, 3.0f), 2.0f, kEps);
}

TEST(MathFuncsTest, Fposmod_ExactMultiple_ReturnsZero) {
    EXPECT_NEAR(Math::fposmod(6.0f, 3.0f), 0.0f, kEps);
}

TEST(MathFuncsTest, Fposmod_ResultAlwaysNonNegative) {
    EXPECT_GE(Math::fposmod(-7.3f, 2.5f), 0.0f);
}

// ── posmod (integer) ─────────────────────────────────────────────────────────

TEST(MathFuncsTest, Posmod_PositiveValue_ReturnsRemainder) {
    EXPECT_EQ(Math::posmod(7, 3), 1);
}

TEST(MathFuncsTest, Posmod_NegativeValue_ReturnsPositive) {
    EXPECT_EQ(Math::posmod(-1, 3), 2);
}

TEST(MathFuncsTest, Posmod_ExactMultiple_ReturnsZero) {
    EXPECT_EQ(Math::posmod(9, 3), 0);
}

// ── sign ─────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Sign_Positive_ReturnsOne) {
    EXPECT_FLOAT_EQ(Math::sign(5.0f), 1.0f);
    EXPECT_FLOAT_EQ(Math::sign(0.001f), 1.0f);
}

TEST(MathFuncsTest, Sign_Negative_ReturnsMinusOne) {
    EXPECT_FLOAT_EQ(Math::sign(-5.0f), -1.0f);
    EXPECT_FLOAT_EQ(Math::sign(-0.001f), -1.0f);
}

TEST(MathFuncsTest, Sign_Zero_ReturnsZero) {
    EXPECT_FLOAT_EQ(Math::sign(0.0f), 0.0f);
}

// ── fract ────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Fract_PositiveValue_ReturnsFractionalPart) {
    EXPECT_NEAR(Math::fract(3.75f), 0.75f, kEps);
}

TEST(MathFuncsTest, Fract_IntegerValue_ReturnsZero) {
    EXPECT_NEAR(Math::fract(5.0f), 0.0f, kEps);
}

TEST(MathFuncsTest, Fract_NegativeValue_InRange) {
    float f = Math::fract(-1.25f);
    EXPECT_GE(f, 0.0f);
    EXPECT_LT(f, 1.0f);
}

// ── linear_to_db / db_to_linear ──────────────────────────────────────────────

TEST(MathFuncsTest, LinearToDb_One_ReturnsZero) {
    EXPECT_NEAR(Math::linear_to_db(1.0f), 0.0f, 1e-3f);
}

TEST(MathFuncsTest, LinearToDb_HalfAmplitude_IsApproxMinus6dB) {
    // half amplitude ~ -6.02 dB
    EXPECT_NEAR(Math::linear_to_db(0.5f), -6.0206f, 0.01f);
}

TEST(MathFuncsTest, DbToLinear_ZeroDB_ReturnsOne) {
    EXPECT_NEAR(Math::db_to_linear(0.0f), 1.0f, 1e-4f);
}

TEST(MathFuncsTest, DbToLinear_Minus6dB_IsApproxHalf) {
    EXPECT_NEAR(Math::db_to_linear(-6.0206f), 0.5f, 0.01f);
}

TEST(MathFuncsTest, LinearToDb_DbToLinear_RoundTrip) {
    float linear = 0.75f;
    EXPECT_NEAR(Math::db_to_linear(Math::linear_to_db(linear)), linear, 1e-4f);
}

// ── cubic_interpolate ────────────────────────────────────────────────────────

TEST(MathFuncsTest, CubicInterpolate_AtZero_ReturnsP1) {
    EXPECT_NEAR(Math::cubic_interpolate(-1.0f, 0.0f, 3.0f, 5.0f, 0.0f), 0.0f, kEps);
}

TEST(MathFuncsTest, CubicInterpolate_AtOne_ReturnsP2) {
    EXPECT_NEAR(Math::cubic_interpolate(-1.0f, 0.0f, 3.0f, 5.0f, 1.0f), 3.0f, kEps);
}

TEST(MathFuncsTest, CubicInterpolate_LinearPoints_AtHalfIsMidpoint) {
    // All points collinear: p0=-1, p1=0, p2=1, p3=2 → t=0.5 → 0.5
    EXPECT_NEAR(Math::cubic_interpolate(-1.0f, 0.0f, 1.0f, 2.0f, 0.5f), 0.5f, kEps);
}

// ── ease ─────────────────────────────────────────────────────────────────────

TEST(MathFuncsTest, Ease_AtZero_ReturnsZero) {
    EXPECT_NEAR(Math::ease(0.0f, 2.0f), 0.0f, kEps);
}

TEST(MathFuncsTest, Ease_AtOne_ReturnsOne) {
    EXPECT_NEAR(Math::ease(1.0f, 2.0f), 1.0f, kEps);
}

TEST(MathFuncsTest, Ease_BelowZero_ClampedToZero) {
    EXPECT_NEAR(Math::ease(-1.0f, 2.0f), 0.0f, kEps);
}

TEST(MathFuncsTest, Ease_AboveOne_ClampedToOne) {
    EXPECT_NEAR(Math::ease(2.0f, 2.0f), 1.0f, kEps);
}

TEST(MathFuncsTest, Ease_CurveGreaterThanOne_EaseIn) {
    // ease-in: value at 0.5 should be less than 0.5
    EXPECT_LT(Math::ease(0.5f, 2.0f), 0.5f);
}

// ── deg_to_rad / rad_to_deg ──────────────────────────────────────────────────

TEST(MathFuncsTest, DegToRad_180_ReturnsPi) {
    EXPECT_NEAR(Math::deg_to_rad(180.0f), Math::PI, kEps);
}

TEST(MathFuncsTest, DegToRad_90_ReturnsHalfPi) {
    EXPECT_NEAR(Math::deg_to_rad(90.0f), Math::PI / 2.0f, kEps);
}

TEST(MathFuncsTest, DegToRad_Zero_ReturnsZero) {
    EXPECT_FLOAT_EQ(Math::deg_to_rad(0.0f), 0.0f);
}

TEST(MathFuncsTest, RadToDeg_Pi_Returns180) {
    EXPECT_NEAR(Math::rad_to_deg(Math::PI), 180.0f, kEps);
}

TEST(MathFuncsTest, RadToDeg_HalfPi_Returns90) {
    EXPECT_NEAR(Math::rad_to_deg(Math::PI / 2.0f), 90.0f, kEps);
}

TEST(MathFuncsTest, DegToRad_RadToDeg_RoundTrip) {
    float deg = 137.5f;
    EXPECT_NEAR(Math::rad_to_deg(Math::deg_to_rad(deg)), deg, 1e-4f); // float precision loss in round trip
}
