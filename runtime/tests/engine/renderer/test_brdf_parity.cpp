/// Unit tests for CPU-side BRDF parity with published reference implementations.
///
/// All tests are pure CPU math — no GPU device, no RHI backend, no audio hardware.
/// Each function under test is implemented in `engine/renderer/lighting/brdf_math.h`
/// as a direct C++ translation of the corresponding HLSL in `pbr_common.hlsli`
/// and `film_tonemap.hlsli`.
///
/// # Test Groups
/// - `BurleyDiffuse`          — DiffuseDisneyBurley(): Lambert parity, known values, retroreflection
/// - `SmithJointApprox`       — VisibilitySmithGGX(): mirror case, rough/smooth limits, symmetry
/// - `ApplyToneCurve`            — ApplyToneCurve(): black, monotonicity, HDR rolloff, gray/white range
/// - `ApplyToneCurveComponents`  — Glow module, red modifier, and S-curve sub-functions
///
/// # Reference
/// - Burley 2012 "Physically Based Shading at Disney" — diffuse model
/// - Heitz 2014 "Understanding the Masking-Shadowing Function" — Smith visibility
/// - ACES Reference Implementation (Academy S-2014-006) — tone curve
/// - Turquin 2018 "Practical multiple scattering compensation" — energy fit

#include "brdf_math.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

using namespace uz::brdf;

static constexpr float kEps  = 1e-4f;   // tolerance for stable formulas
static constexpr float kEps3 = 1e-3f;   // tolerance for chained exp/log operations

// ─────────────────────────────────────────────────────────────────────────────
// §7.1 — Disney Burley Diffuse BRDF
// ─────────────────────────────────────────────────────────────────────────────

/// At normal incidence (NdotV=1, NdotL=1, VdotH=1) with any roughness, FdV=FdL=1.
/// The Burley term collapses to plain Lambert (albedo/PI).
TEST(BurleyDiffuse, NormalIncidence_EqualsLambert_Roughness0) {
    const Vec3 albedo = {0.8f, 0.4f, 0.2f};
    const Vec3 result = DiffuseDisneyBurley(albedo, 0.0f, 1.0f, 1.0f, 1.0f);
    // FD90 = 0.5 + 2*1^2*0 = 0.5; FdV = FdL = 1 + (0.5-1)*0 = 1
    // result = albedo/PI
    EXPECT_NEAR(result.x, albedo.x / kPI, kEps);
    EXPECT_NEAR(result.y, albedo.y / kPI, kEps);
    EXPECT_NEAR(result.z, albedo.z / kPI, kEps);
}

/// Same collapse for roughness=1 at normal incidence: FdV=FdL=1.
TEST(BurleyDiffuse, NormalIncidence_EqualsLambert_Roughness1) {
    const Vec3 albedo = {1.0f, 1.0f, 1.0f};
    const Vec3 result = DiffuseDisneyBurley(albedo, 1.0f, 1.0f, 1.0f, 1.0f);
    // FD90 = 0.5 + 2*1*1 = 2.5; FdV = 1 + (2.5-1)*(1-1)^5 = 1; FdL = 1
    EXPECT_NEAR(result.x, 1.0f / kPI, kEps);
    EXPECT_NEAR(result.y, 1.0f / kPI, kEps);
    EXPECT_NEAR(result.z, 1.0f / kPI, kEps);
}

/// Known value: roughness=1, NdotV=0.5, NdotL=0.5, VdotH=0.7, albedo=(1,1,1).
///
/// Hand-computed:
///   FD90 = 0.5 + 2*(0.7^2)*1.0 = 0.5 + 0.98 = 1.48
///   FdV  = 1 + (1.48-1)*(1-0.5)^5 = 1 + 0.48*0.03125 = 1.015
///   FdL  = 1.015  (symmetric: NdotV == NdotL)
///   result = (1/PI)*1.015*1.015 = (1/PI)*1.030225 ≈ 0.32793
TEST(BurleyDiffuse, KnownValue_Rough1_MidAngle) {
    const Vec3 albedo = {1.0f, 1.0f, 1.0f};
    const Vec3 result = DiffuseDisneyBurley(albedo, 1.0f, 0.5f, 0.5f, 0.7f);

    constexpr float FD90    = 0.5f + 2.0f * 0.7f * 0.7f * 1.0f;     // 1.48
    constexpr float FdVL    = 1.0f + (FD90 - 1.0f) * (0.5f * 0.5f * 0.5f * 0.5f * 0.5f); // 1.015
    constexpr float expected = (1.0f / 3.14159265359f) * FdVL * FdVL;

    EXPECT_NEAR(result.x, expected, kEps);
    EXPECT_NEAR(result.y, expected, kEps);
    EXPECT_NEAR(result.z, expected, kEps);
}

/// FD90 = 0.5 at roughness=0.  At NdotV→0, FdV → 1 + (FD90-1)*1 = FD90 = 0.5.
/// Verify that FdV at extreme grazing equals FD90 for roughness=0.
TEST(BurleyDiffuse, GrazingNdotV_FdV_EqualsFD90_Roughness0) {
    // Use NdotV=0, NdotL=1, VdotH=0 so only FdV is non-trivial.
    // FD90 = 0.5 + 2*0*0 = 0.5
    // FdV  = 1 + (0.5-1)*(1-0)^5 = 0.5
    // FdL  = 1 + (0.5-1)*(1-1)^5 = 1
    // result = albedo*(1/PI)*0.5*1 = albedo*0.5/PI
    const Vec3 albedo = {1.0f, 1.0f, 1.0f};
    const Vec3 result = DiffuseDisneyBurley(albedo, 0.0f, 0.0f, 1.0f, 0.0f);

    constexpr float expected = 0.5f / kPI;
    EXPECT_NEAR(result.x, expected, kEps);
    EXPECT_NEAR(result.y, expected, kEps);
    EXPECT_NEAR(result.z, expected, kEps);
}

/// Retroreflection: at grazing view angle with high roughness, Burley exceeds Lambert.
///
/// roughness=1, NdotV=0.01, NdotL=0.5, VdotH=0.7, albedo=(1,1,1)
///   FD90 = 0.5 + 2*(0.49)*1 = 1.48
///   FdV  = 1 + 0.48*(0.99)^5 ≈ 1 + 0.48*0.9510 ≈ 1.4565
///   FdL  = 1 + 0.48*(0.5)^5 = 1.015
///   result ≈ (1/PI)*1.4565*1.015 ≈ 0.4704
///
/// Lambert = albedo/PI ≈ 0.3183
/// Burley result > Lambert demonstrates retroreflection boost.
TEST(BurleyDiffuse, GrazingView_Roughness1_ExceedsLambert) {
    const Vec3 albedo    = {1.0f, 1.0f, 1.0f};
    const Vec3 burley    = DiffuseDisneyBurley(albedo, 1.0f, 0.01f, 0.5f, 0.7f);
    const float lambert  = 1.0f / kPI;

    EXPECT_GT(burley.x, lambert)
        << "Burley should exceed Lambert at grazing view with roughness=1 (retroreflection)";
}

/// All channels of the result must be non-negative for any physical inputs.
TEST(BurleyDiffuse, OutputIsNonNegative_AllInputCombinations) {
    const Vec3 albedo = {0.5f, 0.7f, 0.3f};
    const float roughnessValues[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float angleValues[]     = {0.001f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f};

    for (float r : roughnessValues) {
        for (float NdotV : angleValues) {
            for (float NdotL : angleValues) {
                for (float VdotH : angleValues) {
                    const Vec3 result = DiffuseDisneyBurley(albedo, r, NdotV, NdotL, VdotH);
                    EXPECT_GE(result.x, 0.0f)
                        << "Negative R: r=" << r << " NdotV=" << NdotV
                        << " NdotL=" << NdotL << " VdotH=" << VdotH;
                    EXPECT_GE(result.y, 0.0f)
                        << "Negative G: r=" << r << " NdotV=" << NdotV
                        << " NdotL=" << NdotL << " VdotH=" << VdotH;
                    EXPECT_GE(result.z, 0.0f)
                        << "Negative B: r=" << r << " NdotV=" << NdotV
                        << " NdotL=" << NdotL << " VdotH=" << VdotH;
                }
            }
        }
    }
}

/// The diffuse output scales linearly with albedo (no non-linear albedo dependence).
TEST(BurleyDiffuse, LinearAlbedoScaling) {
    const Vec3 alb1  = {0.5f, 0.3f, 0.8f};
    const Vec3 alb2  = {1.0f, 0.6f, 1.6f};  // alb2 = alb1 * 2

    const Vec3 r1 = DiffuseDisneyBurley(alb1, 0.6f, 0.7f, 0.8f, 0.6f);
    const Vec3 r2 = DiffuseDisneyBurley(alb2, 0.6f, 0.7f, 0.8f, 0.6f);

    EXPECT_NEAR(r2.x, r1.x * 2.0f, kEps);
    EXPECT_NEAR(r2.y, r1.y * 2.0f, kEps);
    EXPECT_NEAR(r2.z, r1.z * 2.0f, kEps);
}

/// FdV is symmetric with FdL: swapping NdotV and NdotL gives the same result
/// when VdotH is unchanged (bidirectional reflectance symmetry in the V/L terms).
TEST(BurleyDiffuse, NdotV_NdotL_Symmetry) {
    const Vec3 albedo = {1.0f, 1.0f, 1.0f};
    const Vec3 r1 = DiffuseDisneyBurley(albedo, 0.6f, 0.3f, 0.8f, 0.5f);
    const Vec3 r2 = DiffuseDisneyBurley(albedo, 0.6f, 0.8f, 0.3f, 0.5f);

    EXPECT_NEAR(r1.x, r2.x, kEps);
    EXPECT_NEAR(r1.y, r2.y, kEps);
    EXPECT_NEAR(r1.z, r2.z, kEps);
}

/// FD90 = 0.5 when roughness=0 (regardless of VdotH). Verify endpoint behavior:
/// NdotV=0 → FdV=FD90=0.5, NdotV=1 → FdV=1.
TEST(BurleyDiffuse, Roughness0_FdV_Endpoints) {
    const Vec3 albedo = {1.0f, 1.0f, 1.0f};

    // NdotV=0, NdotL=1: only FdV is at grazing, FdL=1
    const Vec3 grazing  = DiffuseDisneyBurley(albedo, 0.0f, 0.0f, 1.0f, 0.5f);
    // NdotV=1, NdotL=1: both Fd terms = 1
    const Vec3 normal   = DiffuseDisneyBurley(albedo, 0.0f, 1.0f, 1.0f, 0.5f);

    // At grazing: result = (1/PI)*FD90*1 = 0.5/PI
    EXPECT_NEAR(grazing.x, 0.5f / kPI, kEps);
    // At normal incidence: result = (1/PI)*1*1 = 1/PI
    EXPECT_NEAR(normal.x,  1.0f / kPI, kEps);
    // Grazing < normal (FdV decreases below 1 at grazing for roughness=0)
    EXPECT_LT(grazing.x, normal.x);
}

// ─────────────────────────────────────────────────────────────────────────────
// §7.2 — Smith Joint Approximate Visibility Term
// ─────────────────────────────────────────────────────────────────────────────

/// Perfect mirror (a2=0): Vis = 0.25 / (NoV*NoL).
///
/// At a2=0 the Smith term simplifies to 0.5 / (2*NoV*NoL) = 0.25 / (NoV*NoL).
/// Degenerate case derivation — Smith joint approximation at roughness=0
TEST(SmithJointApprox, PerfectMirror_a2Zero_EqualsDerived) {
    constexpr float NoV = 0.5f;
    constexpr float NoL = 0.7f;

    const float result   = VisibilitySmithGGX(0.0f, NoV, NoL);
    const float expected = 0.25f / (NoV * NoL);  // 0.25 / 0.35 ≈ 0.71429

    EXPECT_NEAR(result, expected, kEps);
}

/// Maximum roughness (a2=1): Vis = 0.5 / (NoL + NoV).
///
/// At a2=1, a=1, so (1-a)=0. Both Vis_SmithV=NoL and Vis_SmithL=NoV.
/// Return value = 0.5 / (NoL + NoV).
TEST(SmithJointApprox, MaxRoughness_a2One_EqualsSimplification) {
    constexpr float NoV = 0.5f;
    constexpr float NoL = 0.7f;

    const float result   = VisibilitySmithGGX(1.0f, NoV, NoL);
    const float expected = 0.5f / (NoV + NoL);  // 0.5/1.2 ≈ 0.41667

    EXPECT_NEAR(result, expected, kEps);
}

/// Normal incidence (NoV=NoL=1): Vis = 0.25 for any roughness.
///
///   Vis_SmithV = 1*(1*(1-a) + a) = 1
///   Vis_SmithL = 1*(1*(1-a) + a) = 1
///   Vis = 0.5 / 2 = 0.25
TEST(SmithJointApprox, NormalIncidence_NoVNoL1_IsPointTwentyFive_Roughness0) {
    EXPECT_NEAR(VisibilitySmithGGX(0.0f, 1.0f, 1.0f), 0.25f, kEps);
}

TEST(SmithJointApprox, NormalIncidence_NoVNoL1_IsPointTwentyFive_Roughness0_5) {
    EXPECT_NEAR(VisibilitySmithGGX(0.5f, 1.0f, 1.0f), 0.25f, kEps);
}

TEST(SmithJointApprox, NormalIncidence_NoVNoL1_IsPointTwentyFive_Roughness1) {
    EXPECT_NEAR(VisibilitySmithGGX(1.0f, 1.0f, 1.0f), 0.25f, kEps);
}

/// Symmetry: Vis(a2, NoV, NoL) == Vis(a2, NoL, NoV).
///
/// The formula is symmetric under V↔L swap:
///   Vis_SmithV(a, NoV, NoL) = NoL*(NoV*(1-a)+a)  →  swapped = NoV*(NoL*(1-a)+a) = Vis_SmithL
/// Therefore sum is identical and Vis is the same.
TEST(SmithJointApprox, Symmetry_NoV_NoL) {
    const float a2values[]   = {0.0f, 0.01f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float angleValues[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};

    for (float a2 : a2values) {
        for (float NoV : angleValues) {
            for (float NoL : angleValues) {
                const float vis_vl = VisibilitySmithGGX(a2, NoV, NoL);
                const float vis_lv = VisibilitySmithGGX(a2, NoL, NoV);
                EXPECT_NEAR(vis_vl, vis_lv, kEps)
                    << "Symmetry violated at a2=" << a2 << " NoV=" << NoV << " NoL=" << NoL;
            }
        }
    }
}

/// Output is strictly positive for all valid inputs (NoV, NoL > 0, a2 in [0,1]).
TEST(SmithJointApprox, OutputIsPositive_AllValidInputs) {
    const float a2values[]    = {0.0f, 0.01f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float angleValues[] = {0.001f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f};

    for (float a2 : a2values) {
        for (float NoV : angleValues) {
            for (float NoL : angleValues) {
                const float vis = VisibilitySmithGGX(a2, NoV, NoL);
                EXPECT_GT(vis, 0.0f)
                    << "Vis must be positive: a2=" << a2 << " NoV=" << NoV << " NoL=" << NoL;
            }
        }
    }
}

/// As a2 increases (rougher surface), Vis decreases at mid-angles.
/// Smooth surfaces concentrate energy in a narrow lobe (higher Vis at those angles),
/// rough surfaces spread energy wider (lower Vis per direction).
TEST(SmithJointApprox, IncreasingRoughness_DecreasesVis_MidAngle) {
    constexpr float NoV = 0.5f;
    constexpr float NoL = 0.5f;

    const float vis_smooth = VisibilitySmithGGX(0.0f, NoV, NoL);
    const float vis_mid    = VisibilitySmithGGX(0.25f, NoV, NoL);
    const float vis_rough  = VisibilitySmithGGX(1.0f, NoV, NoL);

    EXPECT_GT(vis_smooth, vis_mid);
    EXPECT_GT(vis_mid, vis_rough);
}

/// Known value: a2=0.25, NoV=0.6, NoL=0.8.
///
///   a = sqrt(0.25) = 0.5
///   Vis_SmithV = 0.8 * (0.6*(0.5) + 0.5) = 0.8 * (0.3 + 0.5) = 0.8 * 0.8 = 0.64
///   Vis_SmithL = 0.6 * (0.8*(0.5) + 0.5) = 0.6 * (0.4 + 0.5) = 0.6 * 0.9 = 0.54
///   Vis = 0.5 / (0.64 + 0.54) = 0.5 / 1.18 ≈ 0.42373
TEST(SmithJointApprox, KnownValue_a2_0_25_MidAngle) {
    const float result   = VisibilitySmithGGX(0.25f, 0.6f, 0.8f);
    constexpr float a         = 0.5f;
    constexpr float SmithV    = 0.8f * (0.6f * (1.0f - a) + a);  // 0.8*(0.3+0.5)=0.64
    constexpr float SmithL    = 0.6f * (0.8f * (1.0f - a) + a);  // 0.6*(0.4+0.5)=0.54
    constexpr float expected  = 0.5f / (SmithV + SmithL);          // 0.5/1.18≈0.42373

    EXPECT_NEAR(result, expected, kEps);
}

// ─────────────────────────────────────────────────────────────────────────────
// §7.3 — ApplyToneCurve
// ─────────────────────────────────────────────────────────────────────────────

/// Black input maps to black output (no lift of pure black).
/// The toe segment must satisfy ApplyToneCurve(0,0,0) = (0,0,0) for default params.
TEST(ApplyToneCurve, Black_MapsToBlack) {
    const Vec3 result = ApplyToneCurve({0.0f, 0.0f, 0.0f});
    EXPECT_NEAR(result.x, 0.0f, kEps3);
    EXPECT_NEAR(result.y, 0.0f, kEps3);
    EXPECT_NEAR(result.z, 0.0f, kEps3);
}

/// Very large HDR input must approach 1.0 (white clip rolls off near 1).
/// The S-curve shoulder asymptotically approaches (1 + FilmWhiteClip) before
/// post-desaturation; the final output should be very close to 1.
TEST(ApplyToneCurve, VeryLargeHDR_ApproachesOne) {
    const Vec3 result = ApplyToneCurve({1000.0f, 1000.0f, 1000.0f});
    // White clip default = 0.04 → output ceiling ≈ 1.04 before desaturation.
    // After post-desaturation the value stays very close to 1.
    EXPECT_GT(result.x, 0.95f)  << "HDR 1000 should map to near-white";
    EXPECT_LT(result.x, 1.05f)  << "HDR 1000 must not exceed ceiling significantly";
    EXPECT_NEAR(result.x, result.y, kEps3);
    EXPECT_NEAR(result.y, result.z, kEps3);
}

/// Monotonicity: for equal-channel (gray) inputs, a brighter input always produces
/// a brighter (or equal) output. Tests 200 samples from 0 to 50.
TEST(ApplyToneCurve, Monotonic_GrayInputs) {
    float prevLum = 0.0f;
    for (int i = 0; i <= 200; ++i) {
        const float x      = static_cast<float>(i) * 0.25f;  // 0 to 50 in steps of 0.25
        const Vec3  result = ApplyToneCurve({x, x, x});
        const float lum    = result.Dot(Vec3{1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f});
        EXPECT_GE(lum, prevLum - kEps3)
            << "ApplyToneCurve must be monotonically non-decreasing at x=" << x;
        prevLum = lum;
    }
}

/// Mid-gray (0.18, 0.18, 0.18) must tonemap to a perceptually mid value.
/// default S-curve is calibrated so that 0.18 in → ~0.18 out (InMatch=OutMatch=0.18).
/// After the ACES glow/red modules and desaturation steps the output may deviate slightly,
/// but must stay in [0.13, 0.23] for the default parameters.
TEST(ApplyToneCurve, MidGray_InExpectedRange) {
    const Vec3 result = ApplyToneCurve({0.18f, 0.18f, 0.18f});
    EXPECT_GT(result.x, 0.13f) << "Mid-gray should not be too dark";
    EXPECT_LT(result.x, 0.23f) << "Mid-gray should not be too bright";
    EXPECT_NEAR(result.x, result.y, kEps3);
    EXPECT_NEAR(result.y, result.z, kEps3);
}

/// White (1,1,1) must tonemap to a value in the upper LDR range [0.65, 0.97].
/// The S-curve compresses highlights so pure-linear-white lands below 1.0.
TEST(ApplyToneCurve, White_InExpectedRange) {
    const Vec3 result = ApplyToneCurve({1.0f, 1.0f, 1.0f});
    EXPECT_GT(result.x, 0.65f) << "Linear white should map to bright LDR value";
    EXPECT_LT(result.x, 0.97f) << "Linear white should not clip to full white with default params";
    EXPECT_NEAR(result.x, result.y, kEps3);
    EXPECT_NEAR(result.y, result.z, kEps3);
}

/// HDR 10 (10,10,10) must be very close to white clip ceiling.
TEST(ApplyToneCurve, HDR10_CloseToOne) {
    const Vec3 result = ApplyToneCurve({10.0f, 10.0f, 10.0f});
    EXPECT_GT(result.x, 0.95f) << "HDR×10 should be very close to white";
    EXPECT_LT(result.x, 1.05f) << "HDR×10 must not overshoot ceiling significantly";
    EXPECT_NEAR(result.x, result.y, kEps3);
    EXPECT_NEAR(result.y, result.z, kEps3);
}

/// Output of black (0,0,0) is unaffected by explicit artist parameters matching defaults.
TEST(ApplyToneCurve, ExplicitDefaults_BlackMapsToBlack) {
    const Vec3 result = ApplyToneCurve({0.0f, 0.0f, 0.0f}, 0.88f, 0.55f, 0.26f, 0.0f, 0.04f);
    EXPECT_NEAR(result.x, 0.0f, kEps3);
    EXPECT_NEAR(result.y, 0.0f, kEps3);
    EXPECT_NEAR(result.z, 0.0f, kEps3);
}

/// Default overload matches explicit overload with default params for a range of inputs.
TEST(ApplyToneCurve, DefaultOverload_MatchesExplicit) {
    const Vec3 inputs[] = {
        {0.0f,  0.0f,  0.0f},
        {0.18f, 0.18f, 0.18f},
        {0.5f,  0.5f,  0.5f},
        {1.0f,  1.0f,  1.0f},
        {5.0f,  5.0f,  5.0f},
    };
    for (const Vec3& inp : inputs) {
        const Vec3 def = ApplyToneCurve(inp);
        const Vec3 exp = ApplyToneCurve(inp, 0.88f, 0.55f, 0.26f, 0.0f, 0.04f);
        EXPECT_NEAR(def.x, exp.x, kEps3)
            << "Default overload mismatch at (" << inp.x << ")";
        EXPECT_NEAR(def.y, exp.y, kEps3);
        EXPECT_NEAR(def.z, exp.z, kEps3);
    }
}

/// Output channels are equal for equal-channel (gray) input: R == G == B.
TEST(ApplyToneCurve, GrayInput_OutputIsGray) {
    const float values[] = {0.0f, 0.05f, 0.1f, 0.18f, 0.5f, 1.0f, 3.0f, 10.0f};
    for (float v : values) {
        const Vec3 result = ApplyToneCurve({v, v, v});
        EXPECT_NEAR(result.x, result.y, kEps3)
            << "Gray input (" << v << ") should produce R==G";
        EXPECT_NEAR(result.y, result.z, kEps3)
            << "Gray input (" << v << ") should produce G==B";
    }
}

/// Non-negative output for all tested HDR values (no negative LDR output).
TEST(ApplyToneCurve, OutputIsNonNegative_WideRange) {
    const float hdrValues[] = {
        0.0f, 0.001f, 0.01f, 0.05f, 0.1f, 0.18f, 0.5f,
        1.0f, 2.0f, 5.0f, 10.0f, 50.0f, 100.0f, 1000.0f
    };
    for (float v : hdrValues) {
        const Vec3 result = ApplyToneCurve({v, v, v});
        EXPECT_GE(result.x, 0.0f) << "Negative R at input " << v;
        EXPECT_GE(result.y, 0.0f) << "Negative G at input " << v;
        EXPECT_GE(result.z, 0.0f) << "Negative B at input " << v;
    }
}

/// Increasing FilmSlope increases contrast in the straight segment (higher output for mid values).
TEST(ApplyToneCurve, HigherFilmSlope_IncreasesContrast_MidTone) {
    constexpr float FilmToe       = 0.55f;
    constexpr float FilmShoulder  = 0.26f;
    constexpr float FilmBlackClip = 0.0f;
    constexpr float FilmWhiteClip = 0.04f;

    const Vec3 resultLowSlope  = ApplyToneCurve({0.5f, 0.5f, 0.5f}, 0.5f, FilmToe, FilmShoulder, FilmBlackClip, FilmWhiteClip);
    const Vec3 resultHighSlope = ApplyToneCurve({0.5f, 0.5f, 0.5f}, 1.0f, FilmToe, FilmShoulder, FilmBlackClip, FilmWhiteClip);

    // Higher slope → brighter midtones
    EXPECT_GT(resultHighSlope.x, resultLowSlope.x)
        << "Higher FilmSlope should increase brightness of mid-tones";
}

/// FilmWhiteClip=0 vs FilmWhiteClip>0: larger white clip narrows the ceiling range,
/// which for inputs at the shoulder (bright values) pushes the output downward
/// (more compression). Equivalently, ShoulderScale = 1 + FilmWhiteClip - FilmShoulder
/// increases with FilmWhiteClip, widening the shoulder sigmoid → brighter outputs.
///
/// For a bright mid-HDR value (1.5), larger FilmWhiteClip yields brighter output
/// because the ShoulderScale is bigger (more headroom above the straight segment).
TEST(ApplyToneCurve, FilmWhiteClip_IncreasesShoulderHeadroom) {
    constexpr Vec3 brightInput = {1.5f, 1.5f, 1.5f};

    const Vec3 zeroClip = ApplyToneCurve(brightInput, 0.88f, 0.55f, 0.26f, 0.0f, 0.0f);
    const Vec3 highClip = ApplyToneCurve(brightInput, 0.88f, 0.55f, 0.26f, 0.0f, 0.1f);

    // Higher white clip adds headroom, resulting in brighter output at the shoulder
    EXPECT_GT(highClip.x, zeroClip.x)
        << "FilmWhiteClip > 0 should allow brighter output at shoulder region";
}

// ─────────────────────────────────────────────────────────────────────────────
// §7.4 — ApplyToneCurve sub-function parity (ACES helpers)
// ─────────────────────────────────────────────────────────────────────────────

/// RgbToSaturation: pure gray has saturation 0.
TEST(ApplyToneCurveComponents, Saturation_Gray_IsZero) {
    EXPECT_NEAR(RgbToSaturation({0.5f, 0.5f, 0.5f}), 0.0f, kEps);
}

/// RgbToSaturation: pure red (1,0,0) in AP0 has maximum saturation = 1.
TEST(ApplyToneCurveComponents, Saturation_PureRed_IsOne) {
    // max=1, min=0 → (1-0)/max(1,1e-2) = 1
    EXPECT_NEAR(RgbToSaturation({1.0f, 0.0f, 0.0f}), 1.0f, kEps);
}

/// RgbToSaturation: output in [0, 1] for typical HDR values.
TEST(ApplyToneCurveComponents, Saturation_BoundedRange) {
    const Vec3 samples[] = {
        {0.8f, 0.2f, 0.1f},
        {0.0f, 1.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {2.0f, 0.1f, 0.1f},
    };
    for (const Vec3& s : samples) {
        const float sat = RgbToSaturation(s);
        EXPECT_GE(sat, 0.0f) << "Saturation must be >= 0";
        EXPECT_LE(sat, 1.0f) << "Saturation must be <= 1";
    }
}

/// RgbToHue: achromatic (equal channels) returns hue=0.
TEST(ApplyToneCurveComponents, Hue_Achromatic_IsZero) {
    EXPECT_NEAR(RgbToHue({0.5f, 0.5f, 0.5f}), 0.0f, kEps);
}

/// RgbToHue: pure red (1, 0, 0) → hue=0 degrees.
/// atan2(sqrt(3)*(0-0), 2*1-0-0) = atan2(0, 2) = 0 degrees
TEST(ApplyToneCurveComponents, Hue_PureRed_IsZeroDegrees) {
    EXPECT_NEAR(RgbToHue({1.0f, 0.0f, 0.0f}), 0.0f, kEps);
}

/// RgbToHue: output in [0, 360] for any input.
TEST(ApplyToneCurveComponents, Hue_BoundedRange) {
    const Vec3 samples[] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.5f, 0.8f, 0.3f},
        {2.0f, 0.5f, 1.5f},
    };
    for (const Vec3& s : samples) {
        const float hue = RgbToHue(s);
        EXPECT_GE(hue, 0.0f)   << "Hue must be >= 0";
        EXPECT_LE(hue, 360.0f) << "Hue must be <= 360";
    }
}

/// CenterHue: output is in (-180, 180] after centering.
TEST(ApplyToneCurveComponents, CenterHue_OutputInRange) {
    const float hues[]    = {0.0f, 90.0f, 180.0f, 270.0f, 360.0f};
    const float centers[] = {0.0f, 45.0f, 135.0f, 270.0f};

    for (float h : hues) {
        for (float c : centers) {
            const float centered = CenterHue(h, c);
            EXPECT_GE(centered, -180.0f)
                << "center_hue output must be >= -180 for h=" << h << " c=" << c;
            EXPECT_LE(centered, 180.0f)
                << "center_hue output must be <= 180 for h=" << h << " c=" << c;
        }
    }
}

/// RgbToLumaChroma: pure gray returns same value as its channel.
/// rgb_2_yc({v,v,v}) = (v+v+v + 1.75*chroma) / 3.
/// chroma for (v,v,v): b*(b-g)+g*(g-r)+r*(r-b) = v*(v-v)+v*(v-v)+v*(v-v) = 0
/// So rgb_2_yc({v,v,v}) = v.
TEST(ApplyToneCurveComponents, Yc_Gray_EqualsChannelValue) {
    const float values[] = {0.0f, 0.18f, 0.5f, 1.0f};
    for (float v : values) {
        EXPECT_NEAR(RgbToLumaChroma({v, v, v}), v, kEps)
            << "rgb_2_yc of gray(" << v << ") must equal " << v;
    }
}

/// SigmoidShaper: output at x=0 is 0.5.
/// t = max(1-0, 0) = 1; y = 1 + sign(0)*(1-1) → sign(0)=0 → y=1; return 0.5*1=0.5
TEST(ApplyToneCurveComponents, SigmoidShaper_Zero_IsPointFive) {
    EXPECT_NEAR(SigmoidShaper(0.0f), 0.5f, kEps);
}

/// SigmoidShaper: output at x=2 is 1.0.
/// t = max(1-1, 0) = 0; y = 1 + 1*(1-0) = 2; return 0.5*2 = 1
TEST(ApplyToneCurveComponents, SigmoidShaper_TwoPlus_IsOne) {
    EXPECT_NEAR(SigmoidShaper(2.0f), 1.0f, kEps);
}

/// SigmoidShaper: output at x=-2 is 0.0.
/// t = max(1-1, 0) = 0; y = 1 + (-1)*(1-0) = 0; return 0
TEST(ApplyToneCurveComponents, SigmoidShaper_TwoMinus_IsZero) {
    EXPECT_NEAR(SigmoidShaper(-2.0f), 0.0f, kEps);
}

/// SigmoidShaper: anti-symmetric around 0.5 (sign flip of input flips output around 0.5).
/// sigmoid_shaper(-x) = 1 - sigmoid_shaper(x)
TEST(ApplyToneCurveComponents, SigmoidShaper_AntiSymmetric) {
    const float xvalues[] = {0.1f, 0.5f, 1.0f, 1.5f};
    for (float x : xvalues) {
        const float pos = SigmoidShaper(x);
        const float neg = SigmoidShaper(-x);
        EXPECT_NEAR(pos + neg, 1.0f, kEps)
            << "sigmoid_shaper must be anti-symmetric at x=" << x;
    }
}

/// GlowModuleFwd: below (2/3)*glowMid region → returns glowGainIn unchanged.
TEST(ApplyToneCurveComponents, GlowFwd_BelowThird_ReturnsGain) {
    constexpr float glowGain = 0.05f;
    constexpr float glowMid  = 0.08f;
    constexpr float ycBelow  = 0.04f;  // < 2/3 * 0.08 ≈ 0.0533

    EXPECT_NEAR(GlowModuleFwd(ycBelow, glowGain, glowMid), glowGain, kEps);
}

/// GlowModuleFwd: above 2*glowMid → returns 0 (no glow for bright pixels).
TEST(ApplyToneCurveComponents, GlowFwd_AboveTwoGlowMid_ReturnsZero) {
    constexpr float glowGain = 0.05f;
    constexpr float glowMid  = 0.08f;
    constexpr float ycAbove  = 0.20f;  // > 2 * 0.08 = 0.16

    EXPECT_NEAR(GlowModuleFwd(ycAbove, glowGain, glowMid), 0.0f, kEps);
}

/// GlowModuleFwd: output is non-negative for all typical inputs.
TEST(ApplyToneCurveComponents, GlowFwd_NonNegative) {
    const float ycValues[]   = {0.0f, 0.04f, 0.08f, 0.1f, 0.16f, 0.5f};
    constexpr float glowGain = 0.05f;
    constexpr float glowMid  = 0.08f;

    for (float yc : ycValues) {
        EXPECT_GE(GlowModuleFwd(yc, glowGain, glowMid), 0.0f)
            << "glow_fwd must be non-negative at yc=" << yc;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// §7.5 — Pow5 and Saturate helpers (guards on helpers used by all BRDF functions)
// ─────────────────────────────────────────────────────────────────────────────

/// Pow5(0) = 0.
TEST(BRDFHelpers, Pow5_Zero_IsZero) {
    EXPECT_NEAR(Pow5(0.0f), 0.0f, kEps);
}

/// Pow5(1) = 1.
TEST(BRDFHelpers, Pow5_One_IsOne) {
    EXPECT_NEAR(Pow5(1.0f), 1.0f, kEps);
}

/// Pow5(2) = 32.
TEST(BRDFHelpers, Pow5_Two_Is32) {
    EXPECT_NEAR(Pow5(2.0f), 32.0f, kEps);
}

/// Pow5(0.5) = 0.03125 (1/32).
TEST(BRDFHelpers, Pow5_Half_Is0_03125) {
    EXPECT_NEAR(Pow5(0.5f), 0.03125f, kEps);
}

/// Saturate clamps below 0.
TEST(BRDFHelpers, Saturate_NegativeInput_ClampsToZero) {
    EXPECT_FLOAT_EQ(Saturate(-1.0f), 0.0f);
    EXPECT_FLOAT_EQ(Saturate(-100.0f), 0.0f);
}

/// Saturate clamps above 1.
TEST(BRDFHelpers, Saturate_AboveOne_ClampsToOne) {
    EXPECT_FLOAT_EQ(Saturate(2.0f), 1.0f);
    EXPECT_FLOAT_EQ(Saturate(100.0f), 1.0f);
}

/// Saturate preserves values in [0, 1].
TEST(BRDFHelpers, Saturate_InRange_IsIdentity) {
    EXPECT_FLOAT_EQ(Saturate(0.0f),  0.0f);
    EXPECT_FLOAT_EQ(Saturate(0.5f),  0.5f);
    EXPECT_FLOAT_EQ(Saturate(1.0f),  1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// D_GGX — Trowbridge-Reitz Normal Distribution Function
// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
// ─────────────────────────────────────────────────────────────────────────────

/// At NdotH=1 (half-vector aligned with normal), D_GGX = 1 / (PI * alpha^2).
/// This is the peak of the distribution.
TEST(GGX_NDF, PeakAtNdotH1) {
    const float alpha = 0.5f;
    const float result = D_GGX(alpha, 1.0f);
    const float expected = 1.0f / (kPI * alpha * alpha);
    EXPECT_NEAR(result, expected, kEps);
}

/// Very smooth surface (alpha → 0): D_GGX becomes very large at NdotH=1
/// (approaching a delta function). alpha=0.01 → D ≈ 1/(PI*0.0001) ≈ 3183.
TEST(GGX_NDF, SmoothSurface_HighPeak) {
    const float result = D_GGX(0.01f, 1.0f);
    EXPECT_GT(result, 3000.0f);
}

/// Very rough surface (alpha=1): D_GGX at NdotH=1 = 1/PI ≈ 0.318.
TEST(GGX_NDF, RoughSurface_LowPeak) {
    const float result = D_GGX(1.0f, 1.0f);
    EXPECT_NEAR(result, 1.0f / kPI, kEps);
}

/// D_GGX is always non-negative.
TEST(GGX_NDF, AlwaysNonNegative) {
    for (float alpha : {0.01f, 0.1f, 0.5f, 1.0f}) {
        for (float NdotH : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}) {
            EXPECT_GE(D_GGX(alpha, NdotH), 0.0f);
        }
    }
}

/// Known value: alpha=0.25, NdotH=0.8.
/// a2 = 0.0625, d = ((0.8*0.0625 - 0.8)*0.8 + 1) = (0.05-0.8)*0.8+1 = -0.6+1 = 0.4
/// Wait: d = (NdotH*a2 - NdotH)*NdotH + 1 = (0.8*0.0625 - 0.8)*0.8 + 1
///       = (0.05 - 0.8)*0.8 + 1 = -0.75*0.8 + 1 = -0.6 + 1 = 0.4
/// D = a2 / (d*d*PI) = 0.0625 / (0.16 * PI) = 0.0625 / 0.5027 ≈ 0.1243
TEST(GGX_NDF, KnownValue) {
    const float result = D_GGX(0.25f, 0.8f);
    EXPECT_NEAR(result, 0.1243f, 1e-3f);
}

// ─────────────────────────────────────────────────────────────────────────────
// F_Schlick — Fresnel Approximation
// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
// ─────────────────────────────────────────────────────────────────────────────

/// At normal incidence (cosTheta=1), Fresnel = f0 exactly.
TEST(Fresnel_Schlick, NormalIncidence_EqualsF0) {
    const Vec3 f0 = {0.04f, 0.04f, 0.04f}; // dielectric
    const Vec3 result = F_Schlick(f0, 1.0f);
    EXPECT_NEAR(result.x, 0.04f, kEps);
    EXPECT_NEAR(result.y, 0.04f, kEps);
    EXPECT_NEAR(result.z, 0.04f, kEps);
}

/// At grazing incidence (cosTheta=0), Fresnel → f90 (= 1.0 by default).
TEST(Fresnel_Schlick, GrazingIncidence_EqualsF90) {
    const Vec3 f0 = {0.04f, 0.04f, 0.04f};
    const Vec3 result = F_Schlick(f0, 0.0f);
    EXPECT_NEAR(result.x, 1.0f, kEps);
    EXPECT_NEAR(result.y, 1.0f, kEps);
    EXPECT_NEAR(result.z, 1.0f, kEps);
}

/// Metallic F0 at normal incidence: gold (0.96, 0.69, 0.22).
TEST(Fresnel_Schlick, Metallic_NormalIncidence) {
    const Vec3 f0 = {0.96f, 0.69f, 0.22f}; // gold
    const Vec3 result = F_Schlick(f0, 1.0f);
    EXPECT_NEAR(result.x, 0.96f, kEps);
    EXPECT_NEAR(result.y, 0.69f, kEps);
    EXPECT_NEAR(result.z, 0.22f, kEps);
}

/// Scalar version: f0=0.04, f90=1.0, cosTheta=0.5.
/// F = 0.04 + (1 - 0.04) * (1-0.5)^5 = 0.04 + 0.96 * 0.03125 = 0.04 + 0.03 = 0.07
TEST(Fresnel_Schlick, ScalarKnownValue) {
    const float result = F_SchlickScalar(0.04f, 1.0f, 0.5f);
    const float expected = 0.04f + 0.96f * Pow5(0.5f);
    EXPECT_NEAR(result, expected, kEps);
}

/// Fresnel with explicit f90: F_Schlick(f0, f90, cosTheta).
TEST(Fresnel_Schlick, ExplicitF90) {
    const Vec3 f0 = {0.04f, 0.04f, 0.04f};
    const Vec3 f90 = {0.5f, 0.5f, 0.5f};
    const Vec3 result = F_Schlick(f0, f90, 0.0f); // grazing → f90
    EXPECT_NEAR(result.x, 0.5f, kEps);
    EXPECT_NEAR(result.y, 0.5f, kEps);
    EXPECT_NEAR(result.z, 0.5f, kEps);
}

/// Monotonicity: Fresnel increases as cosTheta decreases (approaching grazing).
TEST(Fresnel_Schlick, Monotonicity) {
    const Vec3 f0 = {0.04f, 0.04f, 0.04f};
    float prev = 0.0f;
    for (float cos = 1.0f; cos >= 0.0f; cos -= 0.1f) {
        const Vec3 r = F_Schlick(f0, cos);
        EXPECT_GE(r.x, prev - kEps);
        prev = r.x;
    }
}
