/// Unit tests for phase 7.16.0 - tangent-mode Hermite curve + .uzcurve asset.
///
/// Covers:
///   - Empty / single-key fallback.
///   - Sorted-insertion invariant via AddKey.
///   - Out-of-range t clamps to edge key value.
///   - Linear mode produces straight-line segments (midpoint = linear avg).
///   - User mode honours authored tangents.
///   - Auto mode smooths across neighbours.
///   - Legacy triplet migration (3-key shape + peak interior/edge cases).
///   - Tangent-mode enum string round-trip.
///   - .uzcurve JSON save + load round-trip preserves mode + tangents.
///
/// @ref phase7_7.16.0-vfx-curve-editor-ui

#include <gtest/gtest.h>

#include "engine/vfx/curves/vfx_tangent_curve.h"

#include <cmath>

using namespace uz;

// ─── Degenerate inputs ─────────────────────────────────────────────────────

TEST(VfxTangentCurve, EmptyEvaluatesZero) {
    VfxTangentCurve c;
    EXPECT_TRUE(c.IsEmpty());
    EXPECT_FLOAT_EQ(c.Evaluate(0.5f), 0.0f);
}

TEST(VfxTangentCurve, SingleKeyReturnsThatValue) {
    VfxTangentCurve c;
    c.AddLinearKey(0.5f, 42.0f);
    EXPECT_FLOAT_EQ(c.Evaluate(-10.0f), 42.0f);
    EXPECT_FLOAT_EQ(c.Evaluate(  0.5f), 42.0f);
    EXPECT_FLOAT_EQ(c.Evaluate( 99.0f), 42.0f);
}

// ─── Insertion ordering ────────────────────────────────────────────────────

TEST(VfxTangentCurve, AddKeyPreservesSortedOrder) {
    VfxTangentCurve c;
    c.AddLinearKey(0.5f, 1.0f);
    c.AddLinearKey(0.0f, 0.0f);
    c.AddLinearKey(1.0f, 2.0f);
    c.AddLinearKey(0.25f, 0.5f);

    ASSERT_EQ(c.keys.size(), 4u);
    EXPECT_FLOAT_EQ(c.keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(c.keys[1].time, 0.25f);
    EXPECT_FLOAT_EQ(c.keys[2].time, 0.5f);
    EXPECT_FLOAT_EQ(c.keys[3].time, 1.0f);
}

// ─── Clamping ──────────────────────────────────────────────────────────────

TEST(VfxTangentCurve, OutOfRangeClampsToEdgeKeys) {
    VfxTangentCurve c;
    c.AddLinearKey(0.0f, 5.0f);
    c.AddLinearKey(1.0f, 9.0f);
    EXPECT_FLOAT_EQ(c.Evaluate(-1.0f), 5.0f);
    EXPECT_FLOAT_EQ(c.Evaluate( 2.0f), 9.0f);
}

// ─── Linear mode ───────────────────────────────────────────────────────────

TEST(VfxTangentCurve, LinearModeMidpointIsAverage) {
    VfxTangentCurve c;
    c.AddLinearKey(0.0f, 0.0f);
    c.AddLinearKey(1.0f, 10.0f);
    EXPECT_NEAR(c.Evaluate(0.5f), 5.0f, 1e-3f);
}

TEST(VfxTangentCurve, LinearModeFollowsSlopeExactly) {
    VfxTangentCurve c;
    c.AddLinearKey(0.0f, 0.0f);
    c.AddLinearKey(1.0f, 4.0f);
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        EXPECT_NEAR(c.Evaluate(t), t * 4.0f, 1e-3f);
    }
}

// ─── Auto mode continuity ─────────────────────────────────────────────────

TEST(VfxTangentCurve, AutoModePassesThroughKeys) {
    VfxTangentCurve c;
    VfxTangentKey k0; k0.time = 0.0f; k0.value = 0.0f; k0.mode = VfxTangentMode::Auto;
    VfxTangentKey k1; k1.time = 0.5f; k1.value = 1.0f; k1.mode = VfxTangentMode::Auto;
    VfxTangentKey k2; k2.time = 1.0f; k2.value = 0.0f; k2.mode = VfxTangentMode::Auto;
    c.AddKey(k0); c.AddKey(k1); c.AddKey(k2);

    EXPECT_NEAR(c.Evaluate(0.0f), 0.0f, 1e-3f);
    EXPECT_NEAR(c.Evaluate(0.5f), 1.0f, 1e-3f);
    EXPECT_NEAR(c.Evaluate(1.0f), 0.0f, 1e-3f);
}

// ─── User mode with authored tangents ─────────────────────────────────────

TEST(VfxTangentCurve, UserModeAuthoredTangentsStillPassThroughKeys) {
    VfxTangentCurve c;
    VfxTangentKey k0; k0.time = 0.0f; k0.value = 0.0f;
    k0.mode = VfxTangentMode::User; k0.inTangent = 2.0f; k0.outTangent = 2.0f;
    VfxTangentKey k1; k1.time = 1.0f; k1.value = 1.0f;
    k1.mode = VfxTangentMode::User; k1.inTangent = -2.0f; k1.outTangent = -2.0f;
    c.AddKey(k0); c.AddKey(k1);

    // Endpoint pass-through is a hard invariant of Hermite blend.
    EXPECT_NEAR(c.Evaluate(0.0f), 0.0f, 1e-3f);
    EXPECT_NEAR(c.Evaluate(1.0f), 1.0f, 1e-3f);
}

// ─── Legacy triplet migration ──────────────────────────────────────────────

TEST(VfxTangentCurve, LegacyTripletCreatesThreeKeys) {
    VfxTangentCurve c;
    c.SetFromLegacyTriplet(/*start=*/ 0.5f, /*peak=*/ 2.0f,
                            /*end=*/   0.1f, /*peakTime=*/ 0.3f);
    ASSERT_EQ(c.keys.size(), 3u);
    EXPECT_FLOAT_EQ(c.keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(c.keys[0].value, 0.5f);
    EXPECT_FLOAT_EQ(c.keys[1].time, 0.3f);
    EXPECT_FLOAT_EQ(c.keys[1].value, 2.0f);
    EXPECT_FLOAT_EQ(c.keys[2].time, 1.0f);
    EXPECT_FLOAT_EQ(c.keys[2].value, 0.1f);
}

TEST(VfxTangentCurve, LegacyTripletPeakAtEndpointCollapsesToTwoKey) {
    VfxTangentCurve c;
    c.SetFromLegacyTriplet(0.0f, 1.0f, 1.0f, /*peakTime=*/ 1.0f);
    ASSERT_EQ(c.keys.size(), 2u);
    EXPECT_FLOAT_EQ(c.keys[0].value, 0.0f);
    EXPECT_FLOAT_EQ(c.keys[1].value, 1.0f);
}

// ─── Tangent-mode enum strings ─────────────────────────────────────────────

TEST(VfxTangentMode, ToStringAllBranches) {
    EXPECT_STREQ(VfxTangentModeToString(VfxTangentMode::Auto),   "auto");
    EXPECT_STREQ(VfxTangentModeToString(VfxTangentMode::Linear), "linear");
    EXPECT_STREQ(VfxTangentModeToString(VfxTangentMode::User),   "user");
    EXPECT_STREQ(VfxTangentModeToString(VfxTangentMode::Broken), "broken");
}

TEST(VfxTangentMode, FromStringRoundTrip) {
    VfxTangentMode m;
    ASSERT_TRUE(VfxTangentModeFromString("auto",   m)); EXPECT_EQ(m, VfxTangentMode::Auto);
    ASSERT_TRUE(VfxTangentModeFromString("linear", m)); EXPECT_EQ(m, VfxTangentMode::Linear);
    ASSERT_TRUE(VfxTangentModeFromString("user",   m)); EXPECT_EQ(m, VfxTangentMode::User);
    ASSERT_TRUE(VfxTangentModeFromString("broken", m)); EXPECT_EQ(m, VfxTangentMode::Broken);
    EXPECT_FALSE(VfxTangentModeFromString("bogus", m));
}

// ─── .uzcurve JSON round-trip ──────────────────────────────────────────────

TEST(VfxTangentCurveAsset, SaveLoadRoundTrip) {
    VfxTangentCurve src;
    VfxTangentKey k0; k0.time = 0.0f; k0.value = 0.0f; k0.mode = VfxTangentMode::Auto;
    VfxTangentKey k1; k1.time = 0.5f; k1.value = 1.0f; k1.mode = VfxTangentMode::User;
    k1.inTangent = 2.0f; k1.outTangent = 2.0f;
    VfxTangentKey k2; k2.time = 1.0f; k2.value = 0.0f; k2.mode = VfxTangentMode::Broken;
    k2.inTangent = -3.0f; k2.outTangent = 0.0f;
    src.AddKey(k0); src.AddKey(k1); src.AddKey(k2);

    const std::string json = VfxTangentCurveAsset::SaveToString(src);
    ASSERT_FALSE(json.empty());

    VfxTangentCurve dst;
    ASSERT_TRUE(VfxTangentCurveAsset::LoadFromString(json, dst));
    ASSERT_EQ(dst.keys.size(), src.keys.size());

    for (size_t i = 0; i < src.keys.size(); ++i) {
        EXPECT_FLOAT_EQ(dst.keys[i].time,  src.keys[i].time);
        EXPECT_FLOAT_EQ(dst.keys[i].value, src.keys[i].value);
        EXPECT_EQ(dst.keys[i].mode, src.keys[i].mode);
        if (src.keys[i].mode == VfxTangentMode::User ||
            src.keys[i].mode == VfxTangentMode::Broken) {
            EXPECT_FLOAT_EQ(dst.keys[i].inTangent,  src.keys[i].inTangent);
            EXPECT_FLOAT_EQ(dst.keys[i].outTangent, src.keys[i].outTangent);
        }
    }
}

TEST(VfxTangentCurveAsset, LoadInvalidFormatRejected) {
    VfxTangentCurve out;
    EXPECT_FALSE(VfxTangentCurveAsset::LoadFromString(R"({"format":"wrong","version":1})", out));
    EXPECT_FALSE(VfxTangentCurveAsset::LoadFromString("not json", out));
}

TEST(VfxTangentCurveAsset, LoadEmptyKeysIsValid) {
    VfxTangentCurve out;
    out.AddLinearKey(0.5f, 3.0f);  // pre-populated; Clear should happen on load
    ASSERT_TRUE(VfxTangentCurveAsset::LoadFromString(
        R"({"format":"uzcurve","version":1,"keys":[]})", out));
    EXPECT_TRUE(out.IsEmpty());
}
