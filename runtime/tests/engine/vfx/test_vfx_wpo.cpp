/// Unit tests for phase 8.18.0 — VFX World Position Offset.
///
/// Validates the splice generator (empty / non-empty attributes) and the
/// CPU built-in evaluator that stands in for HLSL evaluation in tests.
///
/// @ref engine/vfx/modules/vfx_wpo.h
/// @ref docs/engine/25-vfx/35-world-position-offset.md

#include <gtest/gtest.h>

#include "engine/vfx/modules/vfx_wpo.h"
#include "vector3.h"

#include <cmath>
#include <string>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Attributes
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxWPOAttributes, DefaultIsDisabled) {
    VfxWPOMaterialAttributes a;
    EXPECT_FALSE(a.IsEnabled());
}

TEST(VfxWPOAttributes, EnabledWhenExpressionSet) {
    VfxWPOMaterialAttributes a;
    a.hlslExpression = "float3(0, 0, 0)";
    EXPECT_TRUE(a.IsEnabled());
}

// ─────────────────────────────────────────────────────────────────────────────
// EmitEvaluateBody
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxWPOEmit, EmptyAttrsReturnsZero) {
    VfxWPOMaterialAttributes a;
    const auto body = VfxWPOEmitEvaluateBody(a);
    EXPECT_NE(body.find("float3(0.0, 0.0, 0.0)"), std::string::npos);
    EXPECT_NE(body.find("return"), std::string::npos);
}

TEST(VfxWPOEmit, EnabledAttrsWrapsExpression) {
    VfxWPOMaterialAttributes a;
    a.hlslExpression = "sin(time) * worldPos";
    const auto body = VfxWPOEmitEvaluateBody(a);
    EXPECT_NE(body.find("sin(time) * worldPos"), std::string::npos);
    EXPECT_NE(body.find("return"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// SpliceVS — current frame
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxWPOSplice, DisabledEmitsEmptyString) {
    VfxWPOMaterialAttributes a;
    EXPECT_TRUE(VfxWPOSpliceVS(a).empty());
}

TEST(VfxWPOSplice, EnabledEmitsCallAndAdd) {
    VfxWPOMaterialAttributes a;
    a.hlslExpression = "float3(1, 0, 0)";
    const auto splice = VfxWPOSpliceVS(a);
    EXPECT_NE(splice.find("EvaluateWPO"), std::string::npos);
    EXPECT_NE(splice.find("worldPos += wpo"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// SplicePrevVS — prev frame for motion vectors (phase 8.1.0)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxWPOSplicePrev, DisabledEmitsEmpty) {
    VfxWPOMaterialAttributes a;
    EXPECT_TRUE(VfxWPOSplicePrevVS(a).empty());
}

TEST(VfxWPOSplicePrev, EnabledReadsPrevFrameInputs) {
    VfxWPOMaterialAttributes a;
    a.hlslExpression = "float3(0, 0, 0)";
    const auto splice = VfxWPOSplicePrevVS(a);
    // Must read prevWorldPos + prevTime + prevNormalizedAge, NOT their
    // current-frame counterparts — this is the guarantee TAA relies on.
    EXPECT_NE(splice.find("prevWorldPos"), std::string::npos);
    EXPECT_NE(splice.find("prevTime"), std::string::npos);
    EXPECT_NE(splice.find("prevNormalizedAge"), std::string::npos);
    EXPECT_NE(splice.find("prevWpo"), std::string::npos);
    // Must NOT reference current-frame `time` or `normalizedAge` by itself.
    EXPECT_EQ(splice.find(", time,"), std::string::npos);
    EXPECT_EQ(splice.find(", normalizedAge)"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Built-in CPU evaluator
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxWPOEvaluate, ZeroReturnsZero) {
    const Vec3 r = VfxEvaluateWPOBuiltin("zero",
                                           Vec3{1, 2, 3}, Vec3{4, 5, 6},
                                           1.0f, 0.5f);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.z, 0.0f);
}

TEST(VfxWPOEvaluate, SinTimeXShiftsZAxisOnly) {
    const Vec3 r = VfxEvaluateWPOBuiltin("sin_time_x",
                                           Vec3{0, 0, 0}, Vec3{1.0f, 2.0f, 3.0f},
                                           0.5f, 0.0f);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_NEAR(r.z, std::sin(0.5f + 1.0f), 1e-5f);
}

TEST(VfxWPOEvaluate, BreatheScalesAllAxesEqually) {
    const Vec3 worldPos{1.0f, 2.0f, 3.0f};
    const float time = 1.0f;
    const Vec3 r = VfxEvaluateWPOBuiltin("breathe",
                                           Vec3{0, 0, 0}, worldPos,
                                           time, 0.0f);
    const float scale = 0.1f * std::sin(time);
    EXPECT_NEAR(r.x, worldPos.x * scale, 1e-5f);
    EXPECT_NEAR(r.y, worldPos.y * scale, 1e-5f);
    EXPECT_NEAR(r.z, worldPos.z * scale, 1e-5f);
}

TEST(VfxWPOEvaluate, TwistRotatesXYByAge) {
    const Vec3 worldPos{1.0f, 0.0f, 0.0f};
    // Age = 0.25 → 90° rotation.
    const Vec3 r = VfxEvaluateWPOBuiltin("twist",
                                           Vec3{0, 0, 0}, worldPos,
                                           0.0f, 0.25f);
    // Rotated (1, 0) → (0, 1); offset = (0, 1, 0) - (1, 0, 0) = (-1, 1, 0).
    EXPECT_NEAR(r.x, -1.0f, 1e-4f);
    EXPECT_NEAR(r.y,  1.0f, 1e-4f);
    EXPECT_NEAR(r.z,  0.0f, 1e-4f);
}

TEST(VfxWPOEvaluate, UnknownIdReturnsZero) {
    const Vec3 r = VfxEvaluateWPOBuiltin("nonexistent",
                                           Vec3{1, 1, 1}, Vec3{1, 1, 1},
                                           1.0f, 0.5f);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.z, 0.0f);
}

TEST(VfxWPOEvaluate, NullIdSafe) {
    const Vec3 r = VfxEvaluateWPOBuiltin(nullptr,
                                           Vec3{0, 0, 0}, Vec3{0, 0, 0},
                                           0.0f, 0.0f);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion vector invariant: non-zero WPO + identical prev/current inputs
// must produce the same offset both frames, so the motion vector from the
// offset alone is zero.
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxWPOEvaluate, IdenticalInputsYieldIdenticalOffsets) {
    const Vec3 worldPos{2.0f, 3.0f, 4.0f};
    const Vec3 prev = VfxEvaluateWPOBuiltin("breathe",
                                               Vec3{0, 0, 0}, worldPos,
                                               1.0f, 0.0f);
    const Vec3 curr = VfxEvaluateWPOBuiltin("breathe",
                                               Vec3{0, 0, 0}, worldPos,
                                               1.0f, 0.0f);
    EXPECT_FLOAT_EQ(prev.x, curr.x);
    EXPECT_FLOAT_EQ(prev.y, curr.y);
    EXPECT_FLOAT_EQ(prev.z, curr.z);
}
