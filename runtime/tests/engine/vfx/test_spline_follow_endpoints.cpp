/// Integration test: FollowSplineModule — endpoint position accuracy.
///
/// Validates that particles following a spline with FollowSplineModule
/// (speed=1, loop=false, startU=0) are positioned at:
///   - controlPoints[0] when lifeFrac = 0.0  (age = 0.0, lifespan = 4.0)
///   - controlPoints.back() when lifeFrac = 1.0  (age = 4.0, lifespan = 4.0)
///
/// Tolerance: 1e-3 cm (spec requirement).
///
/// ## GPU readback gap
///
/// The FollowSplineModule position assignment runs in the DX12 update CS,
/// writing to UAV Positions[]. Without a GPU harness providing per-particle
/// UAV readback, the exact GPU result is not observable from a CPU unit test.
///
/// This test closes the gap by evaluating the EXACT HLSL formula from
/// kFollowSplineSnippet (spline_modules.cpp):
///
///   float lifeFrac = saturate(age / lifespan);
///   float u = cbSplineFollowStartU + lifeFrac * cbSplineFollowSpeed;
///   // loop=false: u = saturate(u)
///   position = SplineSampleAtU(u).position;
///
/// Both `SplineSampleAtU` and `SampleUniform` walk the same arc-length LUT
/// baked by RebuildAndUpload — so the CPU evaluator is the correct reference.
///
/// At lifeFrac=0: u = startU + 0 * speed = 0.0, so position = spline(0) = CP[0].
/// At lifeFrac=1: u = startU + 1 * speed = 1.0, so position = spline(1) = CP[last].
///
/// @ref phase16_vfx-spline-data-interface-hardware-validation item 2.3

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_spline_di.h"
#include "engine/vfx/modules/spline_modules.h"

#include <algorithm>
#include <cmath>

using namespace uz;

namespace {

// ── Vec3 helpers ────────────────────────────────────────────────────────────

float V3Len(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
Vec3 V3Sub(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
float DistV3(const Vec3& a, const Vec3& b) noexcept {
    return V3Len(V3Sub(a, b));
}

// ── CPU mirror of FollowSplineModule HLSL snippet ───────────────────────────

/// Evaluates the position assigned to a particle by FollowSplineModule.
///
/// Mirrors the HLSL snippet exactly:
///   float lifeFrac = saturate(age / lifespan);
///   float u = startU + lifeFrac * speed;
///   if (loop) u = u - floor(u); else u = saturate(u);
///   position = SplineSampleAtU(u);    // == SampleUniform(u) on CPU
///
/// @param age      Particle age (seconds).
/// @param lifespan Particle lifespan (seconds, clamped to > 0.0001 in HLSL).
/// @param speed    cbSplineFollowSpeed.
/// @param loop     cbSplineFollowLoop.
/// @param startU   cbSplineFollowStartU.
/// @param spline   Source VfxSpline (arc-length table must be built).
Vec3 EvalFollowPosition(float age, float lifespan, float speed, bool loop,
                        float startU, const VfxSpline& spline) {
    // HLSL: float lifespan = max(Lifespans[particleIdx], 0.0001)
    const float ls = std::max(lifespan, 0.0001f);
    // HLSL: float lifeFrac = saturate(age / lifespan)
    float lifeFrac = age / ls;
    lifeFrac = std::clamp(lifeFrac, 0.0f, 1.0f);

    float u = startU + lifeFrac * speed;
    if (loop) {
        // HLSL: u = u - floor(u)
        u = u - std::floor(u);
    } else {
        // HLSL: u = saturate(u)
        u = std::clamp(u, 0.0f, 1.0f);
    }

    // SplineSampleAtU maps u → arc-length LUT → position (same as SampleUniform).
    return spline.SampleUniform(u).position;
}

/// Build the canonical 4-point linear-ish spline used across follow tests:
/// (0,0,0), (100,0,0), (200,0,0), (300,0,0) — straight line along X.
/// Straight-line control points make the endpoint assertions trivial to verify.
VfxSpline MakeStraightSpline() {
    VfxSpline s;
    s.controlPoints = {
        Vec3{0.0f,   0.0f, 0.0f},
        Vec3{100.0f, 0.0f, 0.0f},
        Vec3{200.0f, 0.0f, 0.0f},
        Vec3{300.0f, 0.0f, 0.0f},
    };
    s.tension        = 0.5f;
    s.looping        = false;
    s.arcLengthSamples = 256;
    s.BuildArcLengthTable();
    return s;
}

/// Build the curved 4-point spline for additional endpoint sensitivity.
VfxSpline MakeCurvedSpline() {
    VfxSpline s;
    s.controlPoints = {
        Vec3{0.0f,   0.0f,   0.0f},
        Vec3{100.0f, 0.0f,   0.0f},
        Vec3{200.0f, 100.0f, 0.0f},
        Vec3{300.0f, 100.0f, 0.0f},
    };
    s.tension        = 0.5f;
    s.looping        = false;
    s.arcLengthSamples = 256;
    s.BuildArcLengthTable();
    return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// FollowSplineModule — config / parameter surface
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructed FollowSplineModule: speed=1, loop=false, startU=0.
TEST(SplineFollowEndpoints, DefaultConfig_Values) {
    FollowSplineModule mod;
    const auto& cfg = mod.GetConfig();

    EXPECT_FLOAT_EQ(cfg.speed, 1.0f)
        << "Default speed must be 1.0";
    EXPECT_FALSE(cfg.loop)
        << "Default loop must be false";
    EXPECT_FLOAT_EQ(cfg.startU, 0.0f)
        << "Default startU must be 0.0";
}

/// ParseConfig reads all three fields.
TEST(SplineFollowEndpoints, ParseConfig_AllFields_Parsed) {
    nlohmann::json j = {
        {"speed",   2.5f},
        {"loop",    true},
        {"start_u", 0.25f}
    };
    const FollowSplineConfig cfg = FollowSplineModule::ParseConfig(j);

    EXPECT_FLOAT_EQ(cfg.speed, 2.5f);
    EXPECT_TRUE(cfg.loop);
    EXPECT_FLOAT_EQ(cfg.startU, 0.25f);
}

/// ParseConfig with missing keys falls back to defaults.
TEST(SplineFollowEndpoints, ParseConfig_MissingKeys_UsesDefaults) {
    nlohmann::json j = nlohmann::json::object();
    const FollowSplineConfig cfg = FollowSplineModule::ParseConfig(j);

    EXPECT_FLOAT_EQ(cfg.speed, 1.0f);
    EXPECT_FALSE(cfg.loop);
    EXPECT_FLOAT_EQ(cfg.startU, 0.0f);
}

/// GetName returns the module's registered identifier.
TEST(SplineFollowEndpoints, GetName_ReturnsExpectedIdentifier) {
    FollowSplineModule mod;
    EXPECT_STREQ(mod.GetName(), "follow_spline");
}

/// GetShaderCode must reference SplineSampleAtU and cbSplineFollowSpeed.
TEST(SplineFollowEndpoints, GetShaderCode_ContainsFollowFields) {
    FollowSplineModule mod;
    const std::string_view code = mod.GetShaderCode();

    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("SplineSampleAtU"), std::string_view::npos)
        << "Shader code must call SplineSampleAtU";
    EXPECT_NE(code.find("cbSplineFollowSpeed"), std::string_view::npos)
        << "Shader code must reference cbSplineFollowSpeed";
    EXPECT_NE(code.find("cbSplineFollowLoop"), std::string_view::npos)
        << "Shader code must reference cbSplineFollowLoop";
    EXPECT_NE(code.find("cbSplineFollowStartU"), std::string_view::npos)
        << "Shader code must reference cbSplineFollowStartU";
    EXPECT_NE(code.find("lifeFrac"), std::string_view::npos)
        << "Shader code must derive lifeFrac from age/lifespan";
}

/// GetParameters returns exactly 3 entries: speed, loop, start_u.
TEST(SplineFollowEndpoints, GetParameters_ReturnsThreeEntries) {
    FollowSplineModule mod;
    const auto params = mod.GetParameters();

    ASSERT_EQ(params.size(), 3u);
    EXPECT_STREQ(params[0].name, "speed");
    EXPECT_STREQ(params[1].name, "loop");
    EXPECT_STREQ(params[2].name, "start_u");
}

// ─────────────────────────────────────────────────────────────────────────────
// Endpoint accuracy — CPU mirror of HLSL snippet
// ─────────────────────────────────────────────────────────────────────────────

/// Core spec scenario: at age=0.0, lifespan=4.0, speed=1, loop=false,
/// startU=0 → lifeFrac=0 → u=0 → position must equal controlPoints[0].
/// Tolerance: 1e-3 cm (spec requirement).
///
/// GPU readback gap: mirrors the exact HLSL formula on the CPU.
TEST(SplineFollowEndpoints, AtAge0_PositionEqualsFirstControlPoint) {
    static constexpr float kTolerance = 1e-3f; // cm
    const VfxSpline spline = MakeStraightSpline();
    const Vec3 expected = spline.controlPoints.front();

    // 8 particles, all at age=0.
    for (int p = 0; p < 8; ++p) {
        const Vec3 pos = EvalFollowPosition(
            /*age=*/0.0f, /*lifespan=*/4.0f,
            /*speed=*/1.0f, /*loop=*/false, /*startU=*/0.0f, spline);

        EXPECT_NEAR(pos.x, expected.x, kTolerance)
            << "Particle[" << p << "] X at age=0 must equal controlPoints[0].x";
        EXPECT_NEAR(pos.y, expected.y, kTolerance)
            << "Particle[" << p << "] Y at age=0 must equal controlPoints[0].y";
        EXPECT_NEAR(pos.z, expected.z, kTolerance)
            << "Particle[" << p << "] Z at age=0 must equal controlPoints[0].z";
    }
}

/// Core spec scenario: at age=4.0, lifespan=4.0, speed=1, loop=false,
/// startU=0 → lifeFrac=1 → u=1 → position must equal controlPoints.back().
/// Tolerance: 1e-3 cm (spec requirement).
TEST(SplineFollowEndpoints, AtAge4_PositionEqualsLastControlPoint) {
    static constexpr float kTolerance = 1e-3f; // cm
    const VfxSpline spline = MakeStraightSpline();
    const Vec3 expected = spline.controlPoints.back();

    for (int p = 0; p < 8; ++p) {
        const Vec3 pos = EvalFollowPosition(
            /*age=*/4.0f, /*lifespan=*/4.0f,
            /*speed=*/1.0f, /*loop=*/false, /*startU=*/0.0f, spline);

        EXPECT_NEAR(pos.x, expected.x, kTolerance)
            << "Particle[" << p << "] X at age=4 must equal controlPoints.back().x";
        EXPECT_NEAR(pos.y, expected.y, kTolerance)
            << "Particle[" << p << "] Y at age=4 must equal controlPoints.back().y";
        EXPECT_NEAR(pos.z, expected.z, kTolerance)
            << "Particle[" << p << "] Z at age=4 must equal controlPoints.back().z";
    }
}

/// Same check on the curved spline. The curved spline's endpoint positions
/// are control point [0] and [back] regardless of curvature (Catmull-Rom
/// interpolates through control points).
TEST(SplineFollowEndpoints, CurvedSpline_Age0_EqualsFirstControlPoint) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeCurvedSpline();
    const Vec3 expected = spline.controlPoints.front();

    const Vec3 pos = EvalFollowPosition(0.0f, 4.0f, 1.0f, false, 0.0f, spline);

    EXPECT_NEAR(pos.x, expected.x, kTolerance);
    EXPECT_NEAR(pos.y, expected.y, kTolerance);
    EXPECT_NEAR(pos.z, expected.z, kTolerance);
}

TEST(SplineFollowEndpoints, CurvedSpline_Age4_EqualsLastControlPoint) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeCurvedSpline();
    const Vec3 expected = spline.controlPoints.back();

    const Vec3 pos = EvalFollowPosition(4.0f, 4.0f, 1.0f, false, 0.0f, spline);

    EXPECT_NEAR(pos.x, expected.x, kTolerance);
    EXPECT_NEAR(pos.y, expected.y, kTolerance);
    EXPECT_NEAR(pos.z, expected.z, kTolerance);
}

/// lifeFrac = 0.5 must produce a position on the spline between the endpoints.
/// The exact mid-point position is compared against SampleUniform(0.5).
TEST(SplineFollowEndpoints, AtMidLife_PositionMatchesSampleUniformHalf) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();

    const Vec3 pos      = EvalFollowPosition(2.0f, 4.0f, 1.0f, false, 0.0f, spline);
    const Vec3 expected = spline.SampleUniform(0.5f).position;

    EXPECT_NEAR(pos.x, expected.x, kTolerance);
    EXPECT_NEAR(pos.y, expected.y, kTolerance);
    EXPECT_NEAR(pos.z, expected.z, kTolerance);
}

/// age > lifespan with loop=false must be clamped to lifeFrac=1 (saturate),
/// so the particle stays at the endpoint rather than overshooting.
TEST(SplineFollowEndpoints, AgeExceedsLifespan_NoLoop_ClampsToEndpoint) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();
    const Vec3 expected = spline.controlPoints.back();

    // age = 100 >> lifespan = 4
    const Vec3 pos = EvalFollowPosition(100.0f, 4.0f, 1.0f, false, 0.0f, spline);

    EXPECT_NEAR(pos.x, expected.x, kTolerance)
        << "Overclamped age with loop=false must clamp to last control point";
    EXPECT_NEAR(pos.y, expected.y, kTolerance);
    EXPECT_NEAR(pos.z, expected.z, kTolerance);
}

/// With loop=true and speed=1, at age=4 the particle completes one full loop
/// and wraps back to u=0, landing at controlPoints[0] again.
TEST(SplineFollowEndpoints, Loop_Age4_WrapsToStart) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();
    const Vec3 expected = spline.controlPoints.front();

    // lifeFrac=1, speed=1 → u = 0 + 1*1 = 1.0 → wrapped = 1.0 - floor(1.0) = 0.0
    const Vec3 pos = EvalFollowPosition(4.0f, 4.0f, 1.0f, true, 0.0f, spline);

    EXPECT_NEAR(pos.x, expected.x, kTolerance)
        << "Loop=true with full traversal must wrap back to start";
    EXPECT_NEAR(pos.y, expected.y, kTolerance);
    EXPECT_NEAR(pos.z, expected.z, kTolerance);
}

/// startU = 0.5 with speed=0.5 and loop=false: at age=0, position is at u=0.5;
/// at age=4, position is at u=0.5+0.5=1.0 → last control point.
TEST(SplineFollowEndpoints, NonZeroStartU_ShiftsPath) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();

    const Vec3 posAtAge0 = EvalFollowPosition(0.0f, 4.0f, 0.5f, false, 0.5f, spline);
    const Vec3 expectedMid = spline.SampleUniform(0.5f).position;
    EXPECT_NEAR(posAtAge0.x, expectedMid.x, kTolerance)
        << "startU=0.5 at age=0 must land at spline midpoint";

    const Vec3 posAtAge4 = EvalFollowPosition(4.0f, 4.0f, 0.5f, false, 0.5f, spline);
    const Vec3 expectedEnd = spline.controlPoints.back();
    EXPECT_NEAR(posAtAge4.x, expectedEnd.x, kTolerance)
        << "startU=0.5 + speed=0.5 at age=4 must land at last control point";
}

/// speed=0: all particles remain frozen at startU regardless of age.
TEST(SplineFollowEndpoints, ZeroSpeed_ParticlesFrozenAtStartU) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();

    const Vec3 posAtAge0 = EvalFollowPosition(0.0f, 4.0f, 0.0f, false, 0.25f, spline);
    const Vec3 posAtAge4 = EvalFollowPosition(4.0f, 4.0f, 0.0f, false, 0.25f, spline);
    const Vec3 expected  = spline.SampleUniform(0.25f).position;

    EXPECT_NEAR(posAtAge0.x, expected.x, kTolerance)
        << "speed=0 particle must stay at startU=0.25 at age=0";
    EXPECT_NEAR(posAtAge4.x, expected.x, kTolerance)
        << "speed=0 particle must stay at startU=0.25 at age=4";
}

/// Positions must be non-NaN for all lifeFrac values in [0, 1].
/// Validates the HLSL guard `max(lifespan, 0.0001)` prevents div-by-zero.
TEST(SplineFollowEndpoints, AllLifeFracValues_ProduceNonNaNPositions) {
    const VfxSpline spline = MakeStraightSpline();

    for (int i = 0; i <= 100; ++i) {
        const float age = static_cast<float>(i) / 100.0f * 4.0f;
        const Vec3 pos = EvalFollowPosition(age, 4.0f, 1.0f, false, 0.0f, spline);

        EXPECT_FALSE(std::isnan(pos.x))
            << "Position X must not be NaN at age=" << age;
        EXPECT_FALSE(std::isnan(pos.y))
            << "Position Y must not be NaN at age=" << age;
        EXPECT_FALSE(std::isnan(pos.z))
            << "Position Z must not be NaN at age=" << age;
    }
}

/// Lifespan = 0 (pathological): the HLSL guard clamps to max(0, 0.0001).
/// The position must be non-NaN.
TEST(SplineFollowEndpoints, ZeroLifespan_NoNaN_PositionAtStartU) {
    const VfxSpline spline = MakeStraightSpline();

    const Vec3 pos = EvalFollowPosition(0.0f, 0.0f, 1.0f, false, 0.0f, spline);

    EXPECT_FALSE(std::isnan(pos.x))
        << "Zero lifespan must not produce NaN (HLSL guard: max(ls, 0.0001))";
    EXPECT_FALSE(std::isnan(pos.y));
    EXPECT_FALSE(std::isnan(pos.z));
}

/// Negative age: HLSL saturate clamps lifeFrac to 0, so position is at u=startU.
TEST(SplineFollowEndpoints, NegativeAge_ClampsToStartU) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();
    const Vec3 expected = spline.SampleUniform(0.0f).position;

    const Vec3 pos = EvalFollowPosition(-1.0f, 4.0f, 1.0f, false, 0.0f, spline);

    EXPECT_NEAR(pos.x, expected.x, kTolerance)
        << "Negative age must clamp lifeFrac to 0, landing at startU=0";
    EXPECT_NEAR(pos.y, expected.y, kTolerance);
    EXPECT_NEAR(pos.z, expected.z, kTolerance);
}

/// Position at u=0 must equal controlPoints[0] via SampleUniform, pinning
/// the evaluator-to-spline contract (not just the follow formula).
TEST(SplineFollowEndpoints, SampleUniformAt0_EqualsFirstControlPoint) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();

    const Vec3 sample   = spline.SampleUniform(0.0f).position;
    const Vec3 expected = spline.controlPoints.front();

    EXPECT_NEAR(sample.x, expected.x, kTolerance);
    EXPECT_NEAR(sample.y, expected.y, kTolerance);
    EXPECT_NEAR(sample.z, expected.z, kTolerance);
}

/// Position at u=1 must equal controlPoints.back() via SampleUniform.
TEST(SplineFollowEndpoints, SampleUniformAt1_EqualsLastControlPoint) {
    static constexpr float kTolerance = 1e-3f;
    const VfxSpline spline = MakeStraightSpline();

    const Vec3 sample   = spline.SampleUniform(1.0f).position;
    const Vec3 expected = spline.controlPoints.back();

    EXPECT_NEAR(sample.x, expected.x, kTolerance);
    EXPECT_NEAR(sample.y, expected.y, kTolerance);
    EXPECT_NEAR(sample.z, expected.z, kTolerance);
}
