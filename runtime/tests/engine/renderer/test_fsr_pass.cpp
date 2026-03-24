/// UzEngine FSR 1.0 Pass — CPU-Side Unit Tests
///
/// Tests the pure-CPU utility functions and CVar registrations in FSRPass.
/// The Execute() method is NOT tested here — it requires an RHI device and
/// command list (GPU).  Everything below is compute-only, no GPU needed.
///
/// @ref AMD 2021 "FidelityFX Super Resolution" — GPUOpen published algorithm

#include "engine/renderer/passes/postprocess/fsr_pass.h"
#include "cvar.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <cstdint>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Reinterpret 4 bytes of a uint32_t as a float.
static float UintAsFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. FsrRcasCon — sharpness 0.2 (AMD recommended default)
// ─────────────────────────────────────────────────────────────────────────────

TEST(FsrRcasCon, DefaultSharpness_PacksExp2Neg0Point2) {
    // @ref AMD ffx_fsr1.h — FsrRcasCon packs exp2(-sharpness) in con0[0].
    // For sharpness = 0.2: exp2(-0.2) ≈ 0.87055
    uint32_t con0[4] = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
    FsrRcasCon(con0, 0.2f);

    const float packed = UintAsFloat(con0[0]);
    const float expected = std::exp2f(-0.2f);

    EXPECT_NEAR(packed, expected, 1e-5f)
        << "FsrRcasCon(0.2): con0[0] must encode exp2(-0.2) = " << expected;

    // Remaining elements must be zero (unused by shader).
    EXPECT_EQ(con0[1], 0u);
    EXPECT_EQ(con0[2], 0u);
    EXPECT_EQ(con0[3], 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. FsrRcasCon — sharpness 0.0 (maximum sharpening)
// ─────────────────────────────────────────────────────────────────────────────

TEST(FsrRcasCon, ZeroSharpness_MaximumSharpening_PacksOne) {
    // sharpness = 0.0 → attenuation = exp2(0) = 1.0 → maximum sharpening.
    uint32_t con0[4] = {};
    FsrRcasCon(con0, 0.0f);

    const float packed = UintAsFloat(con0[0]);
    const float expected = std::exp2f(0.0f); // = 1.0f exactly

    EXPECT_NEAR(packed, expected, 1e-6f)
        << "FsrRcasCon(0.0): con0[0] must encode exp2(0) = 1.0 (max sharpness)";
    EXPECT_EQ(con0[1], 0u);
    EXPECT_EQ(con0[2], 0u);
    EXPECT_EQ(con0[3], 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. FsrRcasCon — sharpness 2.0 (minimal sharpening / soft)
// ─────────────────────────────────────────────────────────────────────────────

TEST(FsrRcasCon, MaxSharpness_MinimalSharpening_PacksQuarter) {
    // sharpness = 2.0 → attenuation = exp2(-2) = 0.25 → minimal sharpening.
    uint32_t con0[4] = {};
    FsrRcasCon(con0, 2.0f);

    const float packed = UintAsFloat(con0[0]);
    const float expected = std::exp2f(-2.0f); // = 0.25f

    EXPECT_NEAR(packed, expected, 1e-6f)
        << "FsrRcasCon(2.0): con0[0] must encode exp2(-2) = 0.25 (soft)";
    EXPECT_EQ(con0[1], 0u);
    EXPECT_EQ(con0[2], 0u);
    EXPECT_EQ(con0[3], 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. FsrRcasCon — clamps inputs outside [0, 2]
// ─────────────────────────────────────────────────────────────────────────────

TEST(FsrRcasCon, SharpnessAbove2_ClampedTo2) {
    uint32_t con0_clamped[4] = {};
    uint32_t con0_exact[4]   = {};

    FsrRcasCon(con0_clamped, 999.0f); // way above max
    FsrRcasCon(con0_exact,   2.0f);   // expected clamp target

    // Both should encode the same value (2.0f clamped).
    EXPECT_EQ(con0_clamped[0], con0_exact[0])
        << "Sharpness > 2.0 must be clamped to 2.0 (minimal sharpening)";
}

TEST(FsrRcasCon, SharpnessBelow0_ClampedTo0) {
    uint32_t con0_clamped[4] = {};
    uint32_t con0_exact[4]   = {};

    FsrRcasCon(con0_clamped, -5.0f); // below min
    FsrRcasCon(con0_exact,    0.0f); // expected clamp target

    EXPECT_EQ(con0_clamped[0], con0_exact[0])
        << "Sharpness < 0.0 must be clamped to 0.0 (maximum sharpening)";
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. FsrRcasCon — attenuation is monotonically decreasing with sharpness
// ─────────────────────────────────────────────────────────────────────────────

TEST(FsrRcasCon, MonotonicallyDecreasing_AsSharpnessIncreases) {
    // Higher sharpness parameter → lower attenuation (softer result).
    // Test 10 evenly spaced points across [0, 2].
    float prev = UintAsFloat([]{
        uint32_t c[4]; FsrRcasCon(c, 0.0f); return c[0];
    }());

    for (int i = 1; i <= 10; ++i) {
        const float s = static_cast<float>(i) * 0.2f; // 0.2 .. 2.0
        uint32_t con0[4] = {};
        FsrRcasCon(con0, s);
        const float curr = UintAsFloat(con0[0]);

        EXPECT_LT(curr, prev + 1e-6f)
            << "Attenuation at sharpness=" << s
            << " should be <= attenuation at sharpness=" << (s - 0.2f);
        prev = curr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. FsrRcasCon — midpoint 1.0 cross-check
// ─────────────────────────────────────────────────────────────────────────────

TEST(FsrRcasCon, Sharpness1Point0_PacksExp2Neg1) {
    // sharpness = 1.0 → exp2(-1) = 0.5
    uint32_t con0[4] = {};
    FsrRcasCon(con0, 1.0f);

    const float packed   = UintAsFloat(con0[0]);
    const float expected = std::exp2f(-1.0f); // = 0.5f

    EXPECT_NEAR(packed, expected, 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. FSRInput — struct layout and field semantics
//
// FSRInput is a plain aggregate with no in-class initializers.
// Value-initialization ({}) zero-initializes all fields.
// The documented "Default: 0.2" and "Default: true" are caller conventions,
// not C++ in-class defaults — the caller must set them before calling Execute().
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSRInput, ValueInitialized_SharpnessIsZero) {
    // {} zero-initializes all plain struct members (no in-class initializers).
    FSRInput input{};
    EXPECT_FLOAT_EQ(input.sharpness, 0.0f)
        << "FSRInput zero-initialization must produce sharpness=0.0";
}

TEST(FSRInput, ValueInitialized_EnableRCASIsFalse) {
    // bool zero-initializes to false.
    FSRInput input{};
    EXPECT_FALSE(input.enableRCAS)
        << "FSRInput zero-initialization must produce enableRCAS=false";
}

TEST(FSRInput, ValueInitialized_ZeroDimensions) {
    FSRInput input{};
    EXPECT_EQ(input.inputWidth,   0u);
    EXPECT_EQ(input.inputHeight,  0u);
    EXPECT_EQ(input.outputWidth,  0u);
    EXPECT_EQ(input.outputHeight, 0u);
}

TEST(FSRInput, ManuallySet_SharpnessAndRCAS_ValuesRoundtrip) {
    // Verify the fields are writable and readable correctly.
    FSRInput input{};
    input.sharpness   = 0.2f;  // AMD recommended default
    input.enableRCAS  = true;
    input.inputWidth  = 1280;
    input.inputHeight = 720;
    input.outputWidth  = 1920;
    input.outputHeight = 1080;

    EXPECT_FLOAT_EQ(input.sharpness, 0.2f);
    EXPECT_TRUE(input.enableRCAS);
    EXPECT_EQ(input.inputWidth,   1280u);
    EXPECT_EQ(input.inputHeight,   720u);
    EXPECT_EQ(input.outputWidth,  1920u);
    EXPECT_EQ(input.outputHeight, 1080u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. CVar Registration — r.FSR and r.FSR.Sharpness must be in registry
// ─────────────────────────────────────────────────────────────────────────────

// These CVars are registered by static initializers in fsr_pass.cpp when the
// translation unit is linked.  The test binary links fsr_pass.cpp, so the
// statics run at process startup.

TEST(FSRPassCVars, RFSR_CVar_ExistsInRegistry) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR");
    ASSERT_NE(cv, nullptr) << "r.FSR CVar must be registered by fsr_pass.cpp";

    // Default must be false (stored as 0.0f, clamped to [0, 1]).
    EXPECT_FLOAT_EQ(cv->defaultValue, 0.0f)
        << "r.FSR default must be 0.0f (false)";
    EXPECT_FLOAT_EQ(cv->minValue, 0.0f);
    EXPECT_FLOAT_EQ(cv->maxValue, 1.0f);
}

TEST(FSRPassCVars, RFSRSharpness_CVar_ExistsWithCorrectRange) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR.Sharpness");
    ASSERT_NE(cv, nullptr) << "r.FSR.Sharpness CVar must be registered by fsr_pass.cpp";

    // @ref AMD 2021 "FidelityFX Super Resolution" — recommended default 0.2, range [0, 2].
    EXPECT_FLOAT_EQ(cv->defaultValue, 0.2f)
        << "r.FSR.Sharpness default must be 0.2 (AMD recommended quality mode)";
    EXPECT_FLOAT_EQ(cv->minValue, 0.0f)
        << "r.FSR.Sharpness min must be 0.0 (maximum sharpness)";
    EXPECT_FLOAT_EQ(cv->maxValue, 2.0f)
        << "r.FSR.Sharpness max must be 2.0 (minimal sharpness)";
}

TEST(FSRPassCVars, RFSR_GetBool_DefaultIsFalse) {
    // The CVar is registered by fsr_pass.cpp with default false.
    EXPECT_FALSE(CVarRegistry::Get().GetBool("r.FSR"))
        << "r.FSR must default to false (FSR 1.0 disabled by default)";
}

TEST(FSRPassCVars, RFSRSharpness_GetFloat_DefaultIs0Point2) {
    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("r.FSR.Sharpness"), 0.2f)
        << "r.FSR.Sharpness must default to 0.2";
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. FSRPass — IsInitialized starts false (no GPU required to verify this)
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSRPass, DefaultConstructed_IsNotInitialized) {
    FSRPass pass;
    EXPECT_FALSE(pass.IsInitialized())
        << "FSRPass must not be initialized until Init() is called";
}

} // namespace uz::test
