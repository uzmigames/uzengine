/// UzEngine BRDF Math — CPU-side equivalents of PBR shader functions.
///
/// Provides inline implementations of the BRDF evaluation functions used by
/// the GPU shaders in `shaders/runtime/include/pbr_common.hlsli` and
/// `shaders/runtime/include/film_tonemap.hlsli`.
///
/// These are bit-identical translations from HLSL to C++. They exist so that
/// unit tests can verify mathematical correctness without a GPU.
///
/// # Usage
/// Include this header in test files only. Production rendering uses the
/// HLSL shaders on the GPU.
///
/// # References
/// - Burley 2012 "Physically Based Shading at Disney" — diffuse model
/// - Heitz 2014 "Understanding the Masking-Shadowing Function" — Smith joint visibility
/// - ACES Reference Implementation (Academy S-2014-006) — tone curve + color transforms
/// - Turquin 2018 "Practical multiple scattering compensation" — energy conservation fit

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace uz::brdf {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Mathematical constant PI used throughout BRDF evaluation.
static constexpr float kPI = 3.14159265359f;

// ─────────────────────────────────────────────────────────────────────────────
// Low-level helpers — mirrors of math_util.hlsli
// ─────────────────────────────────────────────────────────────────────────────

/// x^5 via explicit multiply chain — mirrors Pow5() in math_util.hlsli.
[[nodiscard]] inline float Pow5(float x) noexcept {
    const float x2 = x * x;
    return x2 * x2 * x;
}

/// x^2 via explicit multiply — mirrors Pow2() in math_util.hlsli.
[[nodiscard]] inline float Pow2(float x) noexcept {
    return x * x;
}

/// Saturate clamps a value to [0, 1] — mirrors HLSL saturate().
[[nodiscard]] inline float Saturate(float x) noexcept {
    return std::clamp(x, 0.0f, 1.0f);
}

/// smoothstep — mirrors HLSL smoothstep(edge0, edge1, x).
[[nodiscard]] inline float Smoothstep(float edge0, float edge1, float x) noexcept {
    const float t = Saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3-component float vector for matrix/color operations
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal 3-component float vector for CPU-side BRDF and color math.
/// Mirrors HLSL float3 operations (dot, saturate, component-wise arithmetic).
/// Used only within this test-support header — production code uses uz::Vec3.
struct Vec3 {
    float x{}, y{}, z{};

    [[nodiscard]] Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    [[nodiscard]] Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    [[nodiscard]] Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    [[nodiscard]] Vec3 operator*(const Vec3& o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
    [[nodiscard]] bool operator==(const Vec3& o) const noexcept { return x == o.x && y == o.y && z == o.z; }

    /// dot product — mirrors HLSL dot()
    [[nodiscard]] float Dot(const Vec3& o) const noexcept {
        return x * o.x + y * o.y + z * o.z;
    }

    /// Max of each component against a scalar — mirrors HLSL max(v, s)
    [[nodiscard]] Vec3 Max(float s) const noexcept {
        return {std::max(x, s), std::max(y, s), std::max(z, s)};
    }

    /// Min of each component against another vec3 — mirrors HLSL min(v, v)
    [[nodiscard]] Vec3 Min(const Vec3& o) const noexcept {
        return {std::min(x, o.x), std::min(y, o.y), std::min(z, o.z)};
    }

    /// Saturate each component — mirrors HLSL saturate(v)
    [[nodiscard]] Vec3 Saturated() const noexcept {
        return {Saturate(x), Saturate(y), Saturate(z)};
    }

    /// Per-component abs
    [[nodiscard]] Vec3 Abs() const noexcept {
        return {std::abs(x), std::abs(y), std::abs(z)};
    }
};

/// 3x3 matrix for color space transforms.
/// Stored row-major: row[i] is the i-th row, applied as mul(mat, vec).
struct Mat3 {
    Vec3 row[3];

    [[nodiscard]] Vec3 MulVec(const Vec3& v) const noexcept {
        return {
            row[0].Dot(v),
            row[1].Dot(v),
            row[2].Dot(v),
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GGX Normal Distribution Function
// ─────────────────────────────────────────────────────────────────────────────

/// Trowbridge-Reitz (GGX) normal distribution function.
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @param alpha Roughness alpha (roughness^2).
/// @param NdotH Cosine of angle between surface normal and half-vector.
/// @return NDF value D(h).
[[nodiscard]] inline float D_GGX(float alpha, float NdotH) noexcept {
    const float a2 = alpha * alpha;
    const float d = ((NdotH * a2 - NdotH) * NdotH + 1.0f);
    return a2 / (d * d * kPI);
}

// ─────────────────────────────────────────────────────────────────────────────
// Schlick Fresnel Approximation
// ─────────────────────────────────────────────────────────────────────────────

/// Schlick Fresnel approximation (scalar).
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
/// @param f0       Reflectance at normal incidence.
/// @param f90      Reflectance at grazing incidence.
/// @param cosTheta Cosine of angle between view and half-vector.
/// @return Fresnel reflectance.
[[nodiscard]] inline float F_SchlickScalar(float f0, float f90, float cosTheta) noexcept {
    return f0 + (f90 - f0) * Pow5(std::max(1.0f - cosTheta, 0.0f));
}

/// Schlick Fresnel approximation (Vec3, explicit f90).
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
[[nodiscard]] inline Vec3 F_Schlick(Vec3 f0, Vec3 f90, float cosTheta) noexcept {
    const float t = Pow5(std::max(1.0f - cosTheta, 0.0f));
    return f0 + (f90 - f0) * t;
}

/// Schlick Fresnel approximation (Vec3, f90 = 1).
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
[[nodiscard]] inline Vec3 F_Schlick(Vec3 f0, float cosTheta) noexcept {
    return F_Schlick(f0, Vec3{1.0f, 1.0f, 1.0f}, cosTheta);
}

// ─────────────────────────────────────────────────────────────────────────────
// Disney Burley Diffuse BRDF
// ─────────────────────────────────────────────────────────────────────────────

/// Disney Burley diffuse BRDF (CPU equivalent of DiffuseDisneyBurley in pbr_common.hlsli).
///
/// @ref Burley 2012 "Physically Based Shading at Disney" — diffuse model
/// @constants FD90 = 0.5 + 2 * VoH^2 * Roughness
///
/// # Parameters
/// - albedo:    linear surface base color.
/// - roughness: perceptual roughness [0, 1] (NOT alpha = roughness^2).
/// - NdotV:     saturated dot(N, V).
/// - NdotL:     saturated dot(N, L).
/// - VdotH:     saturated dot(V, H).
///
/// # Returns
/// Diffuse BRDF value (includes 1/PI normalization).
[[nodiscard]] inline Vec3 DiffuseDisneyBurley(
    Vec3 albedo,
    float roughness,
    float NdotV,
    float NdotL,
    float VdotH) noexcept
{
    // FD90 = 0.5 + 2 * VoH^2 * Roughness (Burley 2012)
    const float FD90 = 0.5f + 2.0f * VdotH * VdotH * roughness;
    // Fresnel-weighted view factor
    const float FdV = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotV);
    // Fresnel-weighted light factor
    const float FdL = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotL);
    // Final diffuse with 1/PI normalization
    return albedo * (1.0f / kPI) * FdV * FdL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Smith Joint Approximate Visibility Term
// ─────────────────────────────────────────────────────────────────────────────

/// Approximate joint Smith visibility term for GGX.
///
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function" — height-correlated Smith
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — fast approximation
///
/// Returns Vis = G / (4*NoV*NoL) — the full denominator is absorbed,
/// so specular BRDF is D * Vis * F directly.
///
/// # Parameters
/// - a2:  roughness alpha squared.
/// - NoV: saturated dot(N, V).
/// - NoL: saturated dot(N, L).
///
/// # Returns
/// Visibility term (non-negative scalar).
[[nodiscard]] inline float VisibilitySmithGGX(
    float a2,
    float NoV,
    float NoL) noexcept
{
    const float a = std::sqrt(a2);
    const float Vis_SmithV = NoL * (NoV * (1.0f - a) + a);
    const float Vis_SmithL = NoV * (NoL * (1.0f - a) + a);
    return 0.5f / (Vis_SmithV + Vis_SmithL);
}

// ─────────────────────────────────────────────────────────────────────────────
// ACES Color Space Matrices
// ─────────────────────────────────────────────────────────────────────────────
// Values from ACES Reference Implementation (Academy S-2014-006).

/// @ref ACES CTL Reference — AP1 to AP0 conversion matrix
static constexpr Mat3 kACES_AP1_2_AP0_MAT = {{
    Vec3{ 0.6954522414f,  0.1406786965f,  0.1638690622f},
    Vec3{ 0.0447945634f,  0.8596711185f,  0.0955343182f},
    Vec3{-0.0055258826f,  0.0040252103f,  1.0015006723f},
}};

/// @ref ACES CTL Reference — AP0 to AP1 conversion matrix
static constexpr Mat3 kACES_AP0_2_AP1_MAT = {{
    Vec3{ 1.4514393161f, -0.2365107469f, -0.2149285693f},
    Vec3{-0.0765537734f,  1.1762296998f, -0.0996759264f},
    Vec3{ 0.0083161484f, -0.0060324498f,  0.9977163014f},
}};

/// @ref ACES CTL Reference — AP1 luminance weights for ACEScg
static constexpr Vec3 kACES_AP1_RGB2Y = {0.2722287168f, 0.6740817658f, 0.0536895174f};

// ─────────────────────────────────────────────────────────────────────────────
// ACES Helper Functions (mirrors of film_tonemap.hlsli)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref ACES CTL Reference — rgb_2_saturation
[[nodiscard]] inline float RgbToSaturation(Vec3 rgb) noexcept {
    const float minrgb = std::min({rgb.x, rgb.y, rgb.z});
    const float maxrgb = std::max({rgb.x, rgb.y, rgb.z});
    return (std::max(maxrgb, 1e-10f) - std::max(minrgb, 1e-10f)) /
           std::max(maxrgb, 1e-2f);
}

/// @ref ACES CTL Reference — rgb_2_hue (atan2 hue angle in degrees)
[[nodiscard]] inline float RgbToHue(Vec3 rgb) noexcept {
    float hue = 0.0f;
    if (!(rgb.x == rgb.y && rgb.y == rgb.z)) {
        hue = (180.0f / kPI) *
              std::atan2(std::sqrt(3.0f) * (rgb.y - rgb.z),
                         2.0f * rgb.x - rgb.y - rgb.z);
    }
    if (hue < 0.0f) { hue += 360.0f; }
    return std::clamp(hue, 0.0f, 360.0f);
}

/// @ref ACES CTL Reference — center_hue (wrap hue to [-180, 180] around center)
[[nodiscard]] inline float CenterHue(float hue, float centerH) noexcept {
    float hueCentered = hue - centerH;
    if (hueCentered < -180.0f) { hueCentered += 360.0f; }
    else if (hueCentered > 180.0f) { hueCentered -= 360.0f; }
    return hueCentered;
}

/// @ref ACES CTL Reference — rgb_2_yc (luma with chroma weighting)
/// @constants ycRadiusWeight = 1.75
[[nodiscard]] inline float RgbToLumaChroma(Vec3 rgb, float ycRadiusWeight = 1.75f) noexcept {
    const float r = rgb.x;
    const float g = rgb.y;
    const float b = rgb.z;
    const float chroma = std::sqrt(b * (b - g) + g * (g - r) + r * (r - b));
    return (b + g + r + ycRadiusWeight * chroma) / 3.0f;
}

/// @ref ACES CTL Reference — sigmoid_shaper (soft step function)
[[nodiscard]] inline float SigmoidShaper(float x) noexcept {
    const float t = std::max(1.0f - std::abs(0.5f * x), 0.0f);
    const float y = 1.0f + (x >= 0.0f ? 1.0f : -1.0f) * (1.0f - t * t);
    return 0.5f * y;
}

/// @ref ACES CTL Reference — glow_fwd (low-light glow boost)
[[nodiscard]] inline float GlowModuleFwd(float ycIn, float glowGainIn, float glowMid) noexcept {
    float glowGainOut;
    if (ycIn <= 2.0f / 3.0f * glowMid) {
        glowGainOut = glowGainIn;
    } else if (ycIn >= 2.0f * glowMid) {
        glowGainOut = 0.0f;
    } else {
        glowGainOut = glowGainIn * (glowMid / ycIn - 0.5f);
    }
    return glowGainOut;
}

// ─────────────────────────────────────────────────────────────────────────────
// ApplyToneCurve — ACES-based S-curve with glow/red modules
// ─────────────────────────────────────────────────────────────────────────────

/// ACES-based tone curve — CPU equivalent of ApplyToneCurve() in film_tonemap.hlsli.
///
/// @ref ACES Reference Implementation (Academy S-2014-006) — RRT + ODT pipeline
/// @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — artist-parameterized variant
/// @constants RRT_GLOW_GAIN=0.05, RRT_GLOW_MID=0.08
/// @constants RRT_RED_SCALE=0.82, RRT_RED_PIVOT=0.03, RRT_RED_WIDTH=135
/// @constants pre_desaturate=0.96, post_desaturate=0.93
/// @constants InMatch=0.18, OutMatch=0.18
///
/// # Parameters
/// - LinearColor: HDR linear color in AP1 (ACEScg) color space.
/// - toneCurveSlope/Toe/Shoulder/BlackClip/WhiteClip: artist parameters.
///
/// # Returns
/// Tonemapped color in AP1, clamped to >= 0. Channels may slightly exceed 1.0
/// due to the post-desaturation step, but values below 0 are clipped.
[[nodiscard]] inline Vec3 ApplyToneCurve(
    Vec3 LinearColor,
    float FilmSlope,
    float FilmToe,
    float FilmShoulder,
    float FilmBlackClip,
    float FilmWhiteClip) noexcept
{
    // Input is AP1 (ACEScg)
    Vec3 ColorAP1 = LinearColor;

    // Convert to AP0 for glow and red modifier
    Vec3 ColorAP0 = kACES_AP1_2_AP0_MAT.MulVec(ColorAP1);

    // ── Glow module ───────────────────────────────────────────────────────────
    // @ref ACES RRT — low-light glow boost
    constexpr float RRT_GLOW_GAIN = 0.05f;
    constexpr float RRT_GLOW_MID  = 0.08f;

    const float saturation = RgbToSaturation(ColorAP0);
    const float ycIn       = RgbToLumaChroma(ColorAP0);
    const float s          = SigmoidShaper((saturation - 0.4f) / 0.2f);
    const float addedGlow  = 1.0f + GlowModuleFwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
    ColorAP0 = ColorAP0 * addedGlow;

    // ── Red modifier ──────────────────────────────────────────────────────────
    // @ref ACES RRT — red desaturation to prevent oversaturated reds
    constexpr float RRT_RED_SCALE = 0.82f;
    constexpr float RRT_RED_PIVOT = 0.03f;
    constexpr float RRT_RED_HUE   = 0.0f;
    constexpr float RRT_RED_WIDTH = 135.0f;

    const float hue          = RgbToHue(ColorAP0);
    const float centeredHue  = CenterHue(hue, RRT_RED_HUE);
    const float hueWeight    = Pow2(Smoothstep(0.0f, 1.0f,
                                    1.0f - std::abs(2.0f * centeredHue / RRT_RED_WIDTH)));

    ColorAP0.x += hueWeight * saturation * (RRT_RED_PIVOT - ColorAP0.x) * (1.0f - RRT_RED_SCALE);

    // ── Convert to ACEScg (AP1) working space ─────────────────────────────────
    Vec3 WorkingColor = kACES_AP0_2_AP1_MAT.MulVec(ColorAP0);
    WorkingColor = WorkingColor.Max(0.0f);

    // ── Pre-desaturate (factor = 0.96) ────────────────────────────────────────
    const float lum0 = WorkingColor.Dot(kACES_AP1_RGB2Y);
    WorkingColor = Vec3{lum0, lum0, lum0} * (1.0f - 0.96f) + WorkingColor * 0.96f;

    // ── Custom S-curve with artist parameters ─────────────────────────────────
    const float ToeScale      = 1.0f + FilmBlackClip - FilmToe;
    const float ShoulderScale = 1.0f + FilmWhiteClip - FilmShoulder;

    constexpr float InMatch  = 0.18f;
    constexpr float OutMatch = 0.18f;

    float ToeMatch;
    if (FilmToe > 0.8f) {
        // 0.18 will be on straight segment
        ToeMatch = (1.0f - FilmToe - OutMatch) / FilmSlope + std::log10(InMatch);
    } else {
        // 0.18 will be on toe segment
        const float bt = (OutMatch + FilmBlackClip) / ToeScale - 1.0f;
        ToeMatch = std::log10(InMatch) -
                   0.5f * std::log((1.0f + bt) / (1.0f - bt)) * (ToeScale / FilmSlope);
    }

    const float StraightMatch  = (1.0f - FilmToe) / FilmSlope - ToeMatch;
    const float ShoulderMatch  = FilmShoulder / FilmSlope - StraightMatch;

    // Per-component log10
    const Vec3 LogColor = {
        std::log10(std::max(WorkingColor.x, 1e-10f)),
        std::log10(std::max(WorkingColor.y, 1e-10f)),
        std::log10(std::max(WorkingColor.z, 1e-10f)),
    };

    // Straight segment
    const Vec3 StraightColor = {
        FilmSlope * (LogColor.x + StraightMatch),
        FilmSlope * (LogColor.y + StraightMatch),
        FilmSlope * (LogColor.z + StraightMatch),
    };

    // Toe sigmoid
    const auto ToeFunc = [&](float lc) -> float {
        return (-FilmBlackClip) + (2.0f * ToeScale) /
               (1.0f + std::exp((-2.0f * FilmSlope / ToeScale) * (lc - ToeMatch)));
    };

    // Shoulder sigmoid
    const auto ShoulderFunc = [&](float lc) -> float {
        return (1.0f + FilmWhiteClip) - (2.0f * ShoulderScale) /
               (1.0f + std::exp((2.0f * FilmSlope / ShoulderScale) * (lc - ShoulderMatch)));
    };

    // Select toe or straight based on log position
    const auto SelectToe = [&](float lc, float straight) -> float {
        return (lc >= ToeMatch) ? straight : ToeFunc(lc);
    };
    const auto SelectShoulder = [&](float lc, float straight) -> float {
        return (lc <= ShoulderMatch) ? straight : ShoulderFunc(lc);
    };

    Vec3 ToeColor = {
        SelectToe(LogColor.x, StraightColor.x),
        SelectToe(LogColor.y, StraightColor.y),
        SelectToe(LogColor.z, StraightColor.z),
    };
    Vec3 ShoulderColor = {
        SelectShoulder(LogColor.x, StraightColor.x),
        SelectShoulder(LogColor.y, StraightColor.y),
        SelectShoulder(LogColor.z, StraightColor.z),
    };

    // Smooth blend between toe and shoulder via cubic Hermite
    const auto BlendTS = [&](float lc, float tc, float sc) -> float {
        float t = Saturate((lc - ToeMatch) / (ShoulderMatch - ToeMatch));
        if (ShoulderMatch < ToeMatch) { t = 1.0f - t; }
        t = (3.0f - 2.0f * t) * t * t;  // cubic Hermite
        return tc + t * (sc - tc);
    };

    Vec3 ToneColor = {
        BlendTS(LogColor.x, ToeColor.x, ShoulderColor.x),
        BlendTS(LogColor.y, ToeColor.y, ShoulderColor.y),
        BlendTS(LogColor.z, ToeColor.z, ShoulderColor.z),
    };

    // ── Post-desaturate (factor = 0.93) ───────────────────────────────────────
    const float lum1 = Vec3{ToneColor.x, ToneColor.y, ToneColor.z}.Dot(kACES_AP1_RGB2Y);
    ToneColor = Vec3{lum1, lum1, lum1} * (1.0f - 0.93f) + ToneColor * 0.93f;

    // Clip to >= 0
    return ToneColor.Max(0.0f);
}

/// Convenience overload with default artist parameters.
[[nodiscard]] inline Vec3 ApplyToneCurve(Vec3 LinearColor) noexcept {
    return ApplyToneCurve(LinearColor, 0.88f, 0.55f, 0.26f, 0.0f, 0.04f);
}

// ────────────────────────────────────────────────��────────────────────────────
// ACESHillFit — Stephen Hill's fitted RRT+ODT matrices
// ─────────────────────────────────────────────────────────────────────────────

/// ACES filmic tone mapping (Hill 2016 fitted RRT+ODT matrices).
/// CPU-side equivalent of ACESHillFit() in common/color.hlsli.
///
/// @ref Hill 2016 "ACES Fitted Curves" — better hue preservation than Narkowicz
[[nodiscard]] inline Vec3 ACESHillFit(Vec3 x) noexcept
{
    // sRGB -> AP1 (ACEScg working space) input matrix
    constexpr Mat3 ACESInputMat{
        0.59719f, 0.35458f, 0.04823f,
        0.07600f, 0.90834f, 0.01566f,
        0.02840f, 0.13383f, 0.83777f,
    };
    // AP1 -> sRGB output matrix
    constexpr Mat3 ACESOutputMat{
         1.60475f, -0.53108f, -0.07367f,
        -0.10208f,  1.10813f, -0.00605f,
        -0.00327f, -0.07276f,  1.07602f,
    };

    x = ACESInputMat.MulVec(x);

    // RRT+ODT fit (Hill)
    Vec3 a{x.x * (x.x + 0.0245786f) - 0.000090537f,
            x.y * (x.y + 0.0245786f) - 0.000090537f,
            x.z * (x.z + 0.0245786f) - 0.000090537f};
    Vec3 b{x.x * (0.983729f * x.x + 0.4329510f) + 0.238081f,
            x.y * (0.983729f * x.y + 0.4329510f) + 0.238081f,
            x.z * (0.983729f * x.z + 0.4329510f) + 0.238081f};

    x = Vec3{a.x / b.x, a.y / b.y, a.z / b.z};
    x = ACESOutputMat.MulVec(x);

    return Vec3{Saturate(x.x), Saturate(x.y), Saturate(x.z)};
}

/// Narkowicz 2015 ACES fit — kept for A/B comparison.
/// @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve"
[[nodiscard]] inline Vec3 ACESNarkowicz(Vec3 x) noexcept
{
    constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    auto f = [&](float v) -> float {
        return Saturate((v * (a * v + b)) / (v * (c * v + d) + e));
    };
    return Vec3{f(x.x), f(x.y), f(x.z)};
}

// ────────────���───────────────────────��─────────────────────────────���──────────
// ComputeSpecularOcclusion — Lagarde 2014 / Filament
// ───────────────────────────��─────────────────────────────────────────────────

/// Specular occlusion from AO — prevents IBL specular leaking in crevices.
/// CPU-side equivalent of ComputeSpecularOcclusion() in lighting/ibl.hlsli.
///
/// @ref Lagarde 2014 "Moving Frostbite to PBR" — specular occlusion approximation
[[nodiscard]] inline float ComputeSpecularOcclusion(float NdotV, float ao, float roughness) noexcept
{
    return Saturate(std::pow(NdotV + ao, std::exp2(-16.0f * roughness - 1.0f)) - 1.0f + ao);
}

// ─────────────────────────────────────────────────────────────────────────────
// Anisotropic GGX — CPU mirrors of brdf.hlsli aniso functions
// ─────────────────────────────────────────────────────────────────────────────

/// Remap isotropic roughness + anisotropy to per-axis alpha.
/// @ref Burley 2012, KHR_materials_anisotropy
[[nodiscard]] inline std::pair<float, float> RemapAnisotropy(float roughness, float anisotropy) noexcept
{
    constexpr float kMinAlpha = 0.0064f;
    float alpha = std::max(roughness * roughness, kMinAlpha);
    float ax = std::max(alpha * (1.0f + anisotropy), kMinAlpha);
    float ay = std::max(alpha * (1.0f - anisotropy), kMinAlpha);
    return {ax, ay};
}

/// Anisotropic GGX NDF.
/// @ref Burley 2012, Walter et al. 2007
[[nodiscard]] inline float D_GGXAniso(float ax, float ay, float NoH, float XoH, float YoH) noexcept
{
    float a2 = ax * ay;
    float vx = ay * XoH, vy = ax * YoH, vz = a2 * NoH;
    float S = vx * vx + vy * vy + vz * vz;
    return (1.0f / kPI) * a2 * (a2 / (S * S));
}

/// Anisotropic Smith height-correlated visibility.
/// @ref Heitz 2014, Filament
[[nodiscard]] inline float V_SmithGGXCorrelated_Aniso(
    float ax, float ay, float NoV, float NoL,
    float XoV, float XoL, float YoV, float YoL) noexcept
{
    auto len3 = [](float a, float b, float c) { return std::sqrt(a*a + b*b + c*c); };
    float GGXV = NoL * len3(ax * XoV, ay * YoV, NoV);
    float GGXL = NoV * len3(ax * XoL, ay * YoL, NoL);
    return 0.5f / std::max(GGXV + GGXL, 1e-7f);
}

// ─────────────────────────────────────────────────────────────────────────────
// §10 — GGX VNDF Importance Sampling (CPU parity)
// ─────────────────────────────────────────────────────────────────────────────

/// GGX importance sampling (standard NDF) — returns half vector H in tangent space (Z-up).
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @constants cosTheta^2 = (1 - u.x) / (1 + (a^2 - 1) * u.x), phi = 2*PI * u.y
///
/// CPU mirror of shaders/runtime/common/sampling.hlsli::ImportanceSampleGGX.
[[nodiscard]] inline Vec3 ImportanceSampleGGX(float ux, float uy, float roughness) noexcept {
    const float alpha = roughness * roughness;
    const float a2 = alpha * alpha;

    const float cosTheta = std::sqrt((1.0f - ux) / (1.0f + (a2 - 1.0f) * ux));
    const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
    const float phi = 2.0f * kPI * uy;

    return Vec3{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
}

/// Sample the GGX Visible Normal Distribution Function (VNDF) using the
/// spherical cap method. Returns a microfacet normal (half vector) in
/// tangent space visible from the incident direction wi.
///
/// @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps"
///
/// CPU mirror of shaders/runtime/common/sampling.hlsli::SampleGGX_VNDF.
///
/// alpha: isotropic GGX roughness (alpha = roughness^2)
/// wi:    incident direction in tangent space (Z-up), must be normalized
/// ux, uy: 2D uniform random sample in [0,1]^2
[[nodiscard]] inline Vec3 SampleGGX_VNDF(float alpha, Vec3 wi, float ux, float uy) noexcept {
    // Transform the view direction to the hemisphere configuration
    // (stretch the ellipsoid into a hemisphere)
    Vec3 Vh = Vec3{alpha * wi.x, alpha * wi.y, wi.z};
    const float lenVh = std::sqrt(Vh.x * Vh.x + Vh.y * Vh.y + Vh.z * Vh.z);
    if (lenVh > 0.0f) {
        Vh.x /= lenVh;
        Vh.y /= lenVh;
        Vh.z /= lenVh;
    }

    // Sample a spherical cap in the positive hemisphere
    const float phi = 2.0f * kPI * ux;
    const float z = (1.0f - uy) * (1.0f + Vh.z) - Vh.z;
    const float sinTheta = std::sqrt(std::max(1.0f - z * z, 0.0f));
    const float x = sinTheta * std::cos(phi);
    const float y = sinTheta * std::sin(phi);

    // Compute the microfacet normal on the hemisphere
    const float hx = x + Vh.x;
    const float hy = y + Vh.y;
    const float hz = z + Vh.z;

    // Transform back from hemisphere to the ellipsoid configuration
    Vec3 result{alpha * hx, alpha * hy, std::max(hz, 0.0f)};
    const float lenR = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
    if (lenR > 0.0f) {
        result.x /= lenR;
        result.y /= lenR;
        result.z /= lenR;
    }
    return result;
}

} // namespace uz::brdf
