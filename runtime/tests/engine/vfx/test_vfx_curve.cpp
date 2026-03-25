/// Unit tests for VFXCurve -- keyframe-based animation curves.
///
/// Covers: linear interpolation, smoothstep interpolation, out-of-range clamping,
/// single-keyframe curves, empty curves, Vec4 component interpolation, and
/// GPU texture baking via mock device. All tests are CPU-only.

#include <gtest/gtest.h>

#include "curves/vfx_curve.h"
#include "mock_rhi_device.h"

using namespace uz;
using uz::test::MockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Linear interpolation on a float curve
// ─────────────────────────────────────────────────────────────────────────────

/// Float curve with keys {0.0→0.0}, {0.5→1.0}, {1.0→0.0}.
/// Sample at t=0, 0.25, 0.5, 0.75, 1.0 and verify linear blend values.
TEST(VFXCurve, LinearInterpolation_FloatCurve_CorrectValues) {
    VFXCurve curve(
        std::vector<Keyframe>{
            {0.0f, 0.0f},
            {0.5f, 1.0f},
            {1.0f, 0.0f}
        },
        InterpolationMode::Linear
    );

    EXPECT_FALSE(curve.IsEmpty());
    EXPECT_EQ(curve.GetKeyCount(), 3u);
    EXPECT_EQ(curve.GetType(), CurveType::Float);
    EXPECT_EQ(curve.GetInterpolation(), InterpolationMode::Linear);

    // Exact key positions.
    EXPECT_FLOAT_EQ(curve.SampleFloat(0.0f), 0.0f)
        << "At t=0, value must be exactly 0.0 (first key)";
    EXPECT_FLOAT_EQ(curve.SampleFloat(0.5f), 1.0f)
        << "At t=0.5, value must be exactly 1.0 (second key)";
    EXPECT_FLOAT_EQ(curve.SampleFloat(1.0f), 0.0f)
        << "At t=1.0, value must be exactly 0.0 (third key)";

    // Midpoints between keys: linear lerp.
    // Between t=0 and t=0.5: at t=0.25 → lerp(0, 1, 0.5) = 0.5
    EXPECT_NEAR(curve.SampleFloat(0.25f), 0.5f, 1e-5f)
        << "At t=0.25 (midpoint of [0,0.5] segment) linear lerp must give 0.5";
    // Between t=0.5 and t=1.0: at t=0.75 → lerp(1, 0, 0.5) = 0.5
    EXPECT_NEAR(curve.SampleFloat(0.75f), 0.5f, 1e-5f)
        << "At t=0.75 (midpoint of [0.5,1.0] segment) linear lerp must give 0.5";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Smoothstep interpolation differs from linear
// ─────────────────────────────────────────────────────────────────────────────

/// Same three keys but with Smoothstep mode. The S-curve shape means the
/// midpoint value (t=0.25) should differ from the linear value (0.5) and
/// be lower (since smoothstep accelerates at t=0 and decelerates before t=0.5).
TEST(VFXCurve, SmoothstepInterpolation_DiffersFromLinear) {
    VFXCurve linearCurve(
        std::vector<Keyframe>{{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}},
        InterpolationMode::Linear
    );
    VFXCurve smoothCurve(
        std::vector<Keyframe>{{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}},
        InterpolationMode::Smoothstep
    );

    EXPECT_EQ(smoothCurve.GetInterpolation(), InterpolationMode::Smoothstep);

    // At key boundaries both must agree.
    EXPECT_FLOAT_EQ(smoothCurve.SampleFloat(0.0f), 0.0f)
        << "Smoothstep must return exact key value at t=0";
    EXPECT_FLOAT_EQ(smoothCurve.SampleFloat(0.5f), 1.0f)
        << "Smoothstep must return exact key value at t=0.5";
    EXPECT_FLOAT_EQ(smoothCurve.SampleFloat(1.0f), 0.0f)
        << "Smoothstep must return exact key value at t=1.0";

    // At t=0.125 the local t within segment [0,0.5] is 0.25.
    // Smoothstep(0.25) = 0.25^2*(3-2*0.25) = 0.0625*2.5 = 0.15625 → value = 0.15625.
    // Linear(0.25) = 0.25 → value = 0.25.
    // (Note: t=0.25 maps to local t=0.5 where smoothstep == linear, so we
    // must use t=0.125 to test off the midpoint.)
    const float linearAt125  = linearCurve.SampleFloat(0.125f);
    const float smoothAt125  = smoothCurve.SampleFloat(0.125f);

    EXPECT_NE(linearAt125, smoothAt125)
        << "Smoothstep and linear must give different values at t=0.125";

    // Smoothstep value should be less than linear at t=0.125
    // (S-curve is below the diagonal in the first quarter).
    EXPECT_LT(smoothAt125, linearAt125)
        << "Smoothstep value at t=0.125 must be less than linear value "
        << "(S-curve is sub-linear in the first quarter of a rising segment)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Out-of-range clamping
// ─────────────────────────────────────────────────────────────────────────────

/// Sample at t=-0.5 (below 0) and t=1.5 (above 1). Must return the first/last
/// key value respectively -- not extrapolate, crash, or return NaN.
TEST(VFXCurve, ClampAtBoundaries_OutOfRangeInputsClamped) {
    VFXCurve curve(
        std::vector<Keyframe>{{0.0f, 3.0f}, {0.5f, 7.5f}, {1.0f, 2.0f}},
        InterpolationMode::Linear
    );

    const float belowRange = curve.SampleFloat(-0.5f);
    EXPECT_FLOAT_EQ(belowRange, 3.0f)
        << "t=-0.5 must clamp to first key value 3.0";

    const float aboveRange = curve.SampleFloat(1.5f);
    EXPECT_FLOAT_EQ(aboveRange, 2.0f)
        << "t=1.5 must clamp to last key value 2.0";

    // Verify no NaN or infinity produced by clamping.
    EXPECT_TRUE(std::isfinite(belowRange)) << "Clamped below-range value must be finite";
    EXPECT_TRUE(std::isfinite(aboveRange)) << "Clamped above-range value must be finite";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: Single keyframe
// ─────────────────────────────────────────────────────────────────────────────

/// A curve constructed with a single Keyframe must not crash and must return
/// a consistent, finite value when sampled at any t. (The API doc says >= 2
/// keys are required; single-key behavior is implementation-defined but must
/// not crash or return NaN.)
TEST(VFXCurve, SingleKeyframe_SamplesWithoutCrash) {
    // Default-construct, then use SetKeys with one key.
    VFXCurve curve;
    curve.SetKeys(std::vector<Keyframe>{{0.5f, 42.0f}});

    // Regardless of single-key handling, it must not crash and must return
    // a finite value.
    const float result = curve.SampleFloat(0.25f);
    EXPECT_TRUE(std::isfinite(result))
        << "Single-key curve must return a finite value, got " << result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: Empty curve returns 0.0f
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructed VFXCurve (empty) must return 0.0f from SampleFloat
/// and a zero Vec4 from SampleVec4 without crashing.
TEST(VFXCurve, EmptyCurve_SampleFloat_ReturnsZero) {
    VFXCurve curve; // default-constructed → empty

    EXPECT_TRUE(curve.IsEmpty())
        << "Default-constructed VFXCurve must be empty";
    EXPECT_EQ(curve.GetKeyCount(), 0u)
        << "Empty curve must have 0 keys";

    EXPECT_FLOAT_EQ(curve.SampleFloat(0.0f), 0.0f)
        << "Empty curve SampleFloat must return 0.0f";
    EXPECT_FLOAT_EQ(curve.SampleFloat(0.5f), 0.0f)
        << "Empty curve SampleFloat at t=0.5 must return 0.0f";
    EXPECT_FLOAT_EQ(curve.SampleFloat(1.0f), 0.0f)
        << "Empty curve SampleFloat at t=1.0 must return 0.0f";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: Vec4 curve -- all 4 components interpolate correctly
// ─────────────────────────────────────────────────────────────────────────────

/// Create a Vec4 curve from {t=0, (0,0,0,0)} to {t=1, (1,2,3,4)}.
/// Sample at t=0, 0.5, 1.0 and verify all 4 channels.
TEST(VFXCurve, Vec4Curve_AllComponentsInterpolateCorrectly) {
    VFXCurve curve(
        std::vector<KeyframeVec4>{
            {0.0f, Vec4{0.0f, 0.0f, 0.0f, 0.0f}},
            {1.0f, Vec4{1.0f, 2.0f, 3.0f, 4.0f}}
        },
        CurveType::Vec4,
        InterpolationMode::Linear
    );

    EXPECT_FALSE(curve.IsEmpty());
    EXPECT_EQ(curve.GetType(), CurveType::Vec4);
    EXPECT_EQ(curve.GetKeyCount(), 2u);

    // SampleFloat on a Vec4 curve must return 0.0f (wrong type).
    EXPECT_FLOAT_EQ(curve.SampleFloat(0.5f), 0.0f)
        << "SampleFloat on a Vec4 curve must return 0.0f";

    // At t=0 must be the first key exactly.
    const Vec4 atZero = curve.SampleVec4(0.0f);
    EXPECT_FLOAT_EQ(atZero.x, 0.0f);
    EXPECT_FLOAT_EQ(atZero.y, 0.0f);
    EXPECT_FLOAT_EQ(atZero.z, 0.0f);
    EXPECT_FLOAT_EQ(atZero.w, 0.0f);

    // At t=1.0 must be the last key exactly.
    const Vec4 atOne = curve.SampleVec4(1.0f);
    EXPECT_FLOAT_EQ(atOne.x, 1.0f);
    EXPECT_FLOAT_EQ(atOne.y, 2.0f);
    EXPECT_FLOAT_EQ(atOne.z, 3.0f);
    EXPECT_FLOAT_EQ(atOne.w, 4.0f);

    // At t=0.5 linear lerp: each component is midpoint.
    const Vec4 atHalf = curve.SampleVec4(0.5f);
    EXPECT_NEAR(atHalf.x, 0.5f, 1e-5f) << "x at t=0.5 must be 0.5";
    EXPECT_NEAR(atHalf.y, 1.0f, 1e-5f) << "y at t=0.5 must be 1.0";
    EXPECT_NEAR(atHalf.z, 1.5f, 1e-5f) << "z at t=0.5 must be 1.5";
    EXPECT_NEAR(atHalf.w, 2.0f, 1e-5f) << "w at t=0.5 must be 2.0";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: BakeToTexture -- valid handle returned via mock device
// ─────────────────────────────────────────────────────────────────────────────

/// BakeToTexture must call CreateTexture on the device exactly once and return
/// a valid (non-zero index) texture handle stored internally.
TEST(VFXCurve, BakeToTexture_FloatCurve_CreatesTextureOnDevice) {
    MockRHIDevice device;

    VFXCurve curve(
        std::vector<Keyframe>{{0.0f, 0.0f}, {1.0f, 1.0f}},
        InterpolationMode::Linear
    );

    ASSERT_EQ(device.texturesCreated.load(), 0u)
        << "No textures should be created before BakeToTexture";

    const bool ok = curve.BakeToTexture(&device, 64u);

    EXPECT_TRUE(ok) << "BakeToTexture must succeed with a working mock device";
    EXPECT_EQ(device.texturesCreated.load(), 1u)
        << "Exactly one texture must be created by BakeToTexture";

    const RHITextureHandle handle = curve.GetBakedTexture();
    EXPECT_NE(handle.index, 0u)
        << "Baked texture handle index must be non-zero after successful bake";
    EXPECT_EQ(handle.generation, 1u)
        << "Mock device sets generation=1 on created textures";
}

/// BakeToTexture on an empty curve must not crash and returns false
/// (nothing to bake).
TEST(VFXCurve, BakeToTexture_EmptyCurve_ReturnsFalse) {
    MockRHIDevice device;
    VFXCurve curve; // empty

    const bool ok = curve.BakeToTexture(&device, 256u);

    // Empty curve has nothing to bake -- implementation must return false
    // rather than creating a texture with garbage data.
    EXPECT_FALSE(ok)
        << "BakeToTexture on an empty curve must return false";
    EXPECT_EQ(device.texturesCreated.load(), 0u)
        << "No texture must be created when baking an empty curve";
}
