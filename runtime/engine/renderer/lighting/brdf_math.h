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
/// - UE5 BRDF.ush:183-188 — Diffuse_Burley
/// - UE5 BRDF.ush:346-351 — Vis_SmithJointApprox
/// - UE5 TonemapCommon.ush:105-220 — FilmToneMap
/// - UE5 ACESCommon.ush:140-299 — ACES helper functions

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
///
/// @source UE5 MathUtil.ush — Pow5
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
// Disney Burley Diffuse BRDF
// ─────────────────────────────────────────────────────────────────────────────

/// Disney Burley diffuse BRDF (CPU equivalent of DiffuseBurley in pbr_common.hlsli).
///
/// @source UE5 BRDF.ush:183-188 — Diffuse_Burley
/// @constants FD90 = 0.5 + 2 * VoH^2 * Roughness (BRDF.ush:185)
/// @deviations NONE
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
[[nodiscard]] inline Vec3 DiffuseBurley(
    Vec3 albedo,
    float roughness,
    float NdotV,
    float NdotL,
    float VdotH) noexcept
{
    // UE5 BRDF.ush:185 — FD90 = 0.5 + 2 * VoH^2 * Roughness
    const float FD90 = 0.5f + 2.0f * VdotH * VdotH * roughness;
    // UE5 BRDF.ush:186 — FdV = 1 + (FD90 - 1) * Pow5(1 - NoV)
    const float FdV = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotV);
    // UE5 BRDF.ush:187 — FdL = 1 + (FD90 - 1) * Pow5(1 - NoL)
    const float FdL = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotL);
    // UE5 BRDF.ush:188 — albedo * (1/PI) * FdV * FdL
    return albedo * (1.0f / kPI) * FdV * FdL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Smith Joint Approximate Visibility Term
// ─────────────────────────────────────────────────────────────────────────────

/// Approximate joint Smith visibility term for GGX (CPU equivalent of Vis_SmithJointApprox).
///
/// @source UE5 BRDF.ush:346-351 — Vis_SmithJointApprox
/// @constants NONE (pure formula)
/// @deviations NONE
///
/// Returns Vis = G / (4*NoV*NoL) — the full denominator is absorbed,
/// so specular BRDF is D * Vis * F directly.
///
/// # Parameters
/// - a2:  roughness alpha squared (alpha^2 = roughness^4 in UE5 convention,
///        or perceptual_roughness^4 depending on how alpha was derived).
/// - NoV: saturated dot(N, V).
/// - NoL: saturated dot(N, L).
///
/// # Returns
/// Visibility term (non-negative scalar).
[[nodiscard]] inline float Vis_SmithJointApprox(
    float a2,
    float NoV,
    float NoL) noexcept
{
    // UE5 BRDF.ush:347 — a = sqrt(a2)
    const float a = std::sqrt(a2);
    // UE5 BRDF.ush:348 — Vis_SmithV = NoL * (NoV * (1 - a) + a)
    const float Vis_SmithV = NoL * (NoV * (1.0f - a) + a);
    // UE5 BRDF.ush:349 — Vis_SmithL = NoV * (NoL * (1 - a) + a)
    const float Vis_SmithL = NoV * (NoL * (1.0f - a) + a);
    // UE5 BRDF.ush:350 — 0.5 / (Vis_SmithV + Vis_SmithL)
    return 0.5f / (Vis_SmithV + Vis_SmithL);
}

// ─────────────────────────────────────────────────────────────────────────────
// ACES Color Space Matrices
// ─────────────────────────────────────────────────────────────────────────────
// Exact values from UE5 ACESCommon.ush.

/// @source UE5 ACESCommon.ush:52-57 — AP1_2_AP0_MAT
static constexpr Mat3 kACES_AP1_2_AP0_MAT = {{
    Vec3{ 0.6954522414f,  0.1406786965f,  0.1638690622f},
    Vec3{ 0.0447945634f,  0.8596711185f,  0.0955343182f},
    Vec3{-0.0055258826f,  0.0040252103f,  1.0015006723f},
}};

/// @source UE5 ACESCommon.ush:45-50 — AP0_2_AP1_MAT
static constexpr Mat3 kACES_AP0_2_AP1_MAT = {{
    Vec3{ 1.4514393161f, -0.2365107469f, -0.2149285693f},
    Vec3{-0.0765537734f,  1.1762296998f, -0.0996759264f},
    Vec3{ 0.0083161484f, -0.0060324498f,  0.9977163014f},
}};

/// @source UE5 ACESCommon.ush:59-64 — AP1_RGB2Y (luminance weights for ACEScg)
static constexpr Vec3 kACES_AP1_RGB2Y = {0.2722287168f, 0.6740817658f, 0.0536895174f};

// ─────────────────────────────────────────────────────────────────────────────
// ACES Helper Functions (mirrors of film_tonemap.hlsli)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ACESCommon.ush:140-145 — rgb_2_saturation
[[nodiscard]] inline float aces_rgb_2_saturation(Vec3 rgb) noexcept {
    const float minrgb = std::min({rgb.x, rgb.y, rgb.z});
    const float maxrgb = std::max({rgb.x, rgb.y, rgb.z});
    return (std::max(maxrgb, 1e-10f) - std::max(minrgb, 1e-10f)) /
           std::max(maxrgb, 1e-2f);
}

/// @source UE5 ACESCommon.ush:148-164 — rgb_2_hue
[[nodiscard]] inline float aces_rgb_2_hue(Vec3 rgb) noexcept {
    float hue = 0.0f;
    if (!(rgb.x == rgb.y && rgb.y == rgb.z)) {
        // atan2(sqrt(3)*(G-B), 2R - G - B) in degrees
        hue = (180.0f / kPI) *
              std::atan2(std::sqrt(3.0f) * (rgb.y - rgb.z),
                         2.0f * rgb.x - rgb.y - rgb.z);
    }
    if (hue < 0.0f) { hue += 360.0f; }
    return std::clamp(hue, 0.0f, 360.0f);
}

/// @source UE5 ACESCommon.ush:166-172 — center_hue
[[nodiscard]] inline float aces_center_hue(float hue, float centerH) noexcept {
    float hueCentered = hue - centerH;
    if (hueCentered < -180.0f) { hueCentered += 360.0f; }
    else if (hueCentered > 180.0f) { hueCentered -= 360.0f; }
    return hueCentered;
}

/// @source UE5 ACESCommon.ush:268-290 — rgb_2_yc
/// @constants ycRadiusWeight = 1.75 (ACESCommon.ush:268)
[[nodiscard]] inline float aces_rgb_2_yc(Vec3 rgb, float ycRadiusWeight = 1.75f) noexcept {
    const float r = rgb.x;
    const float g = rgb.y;
    const float b = rgb.z;
    const float chroma = std::sqrt(b * (b - g) + g * (g - r) + r * (r - b));
    return (b + g + r + ycRadiusWeight * chroma) / 3.0f;
}

/// @source UE5 ACESCommon.ush:292-299 — sigmoid_shaper
[[nodiscard]] inline float aces_sigmoid_shaper(float x) noexcept {
    const float t = std::max(1.0f - std::abs(0.5f * x), 0.0f);
    const float y = 1.0f + (x >= 0.0f ? 1.0f : -1.0f) * (1.0f - t * t);
    return 0.5f * y;
}

/// @source UE5 ACESCommon.ush:234-249 — glow_fwd
[[nodiscard]] inline float aces_glow_fwd(float ycIn, float glowGainIn, float glowMid) noexcept {
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
// FilmToneMap — UE5 exact S-curve with ACES glow/red modules
// ─────────────────────────────────────────────────────────────────────────────

/// UE5 ACES FilmToneMap — CPU equivalent of FilmToneMap() in film_tonemap.hlsli.
///
/// @source UE5 TonemapCommon.ush:105-220 — FilmToneMap
/// @constants RRT_GLOW_GAIN=0.05 (TonemapCommon.ush:146)
/// @constants RRT_GLOW_MID=0.08 (TonemapCommon.ush:147)
/// @constants RRT_RED_SCALE=0.82 (TonemapCommon.ush:158)
/// @constants RRT_RED_PIVOT=0.03 (TonemapCommon.ush:159)
/// @constants RRT_RED_HUE=0 (TonemapCommon.ush:160)
/// @constants RRT_RED_WIDTH=135 (TonemapCommon.ush:161)
/// @constants pre_desaturate=0.96 (TonemapCommon.ush:175)
/// @constants post_desaturate=0.93 (TonemapCommon.ush:216)
/// @constants InMatch=0.18 (TonemapCommon.ush:180), OutMatch=0.18 (TonemapCommon.ush:181)
/// @deviations NONE
///
/// # Parameters
/// - LinearColor: HDR linear color in AP1 (ACEScg) color space.
/// - FilmSlope, FilmToe, FilmShoulder, FilmBlackClip, FilmWhiteClip: artist parameters.
///
/// # Returns
/// Tonemapped color in AP1, clamped to >= 0. Channels may slightly exceed 1.0
/// due to the post-desaturation step, but values below 0 are clipped.
[[nodiscard]] inline Vec3 FilmToneMap(
    Vec3 LinearColor,
    float FilmSlope,
    float FilmToe,
    float FilmShoulder,
    float FilmBlackClip,
    float FilmWhiteClip) noexcept
{
    // Input is AP1 (ACEScg) — mirrors TonemapCommon.ush:116
    Vec3 ColorAP1 = LinearColor;

    // Convert to AP0 for glow and red modifier — TonemapCommon.ush:133
    Vec3 ColorAP0 = kACES_AP1_2_AP0_MAT.MulVec(ColorAP1);

    // ── Glow module ───────────────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:145-154
    // @constants RRT_GLOW_GAIN=0.05, RRT_GLOW_MID=0.08
    constexpr float RRT_GLOW_GAIN = 0.05f;
    constexpr float RRT_GLOW_MID  = 0.08f;

    const float saturation = aces_rgb_2_saturation(ColorAP0);
    const float ycIn       = aces_rgb_2_yc(ColorAP0);
    const float s          = aces_sigmoid_shaper((saturation - 0.4f) / 0.2f);
    const float addedGlow  = 1.0f + aces_glow_fwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
    ColorAP0 = ColorAP0 * addedGlow;

    // ── Red modifier ──────────────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:157-167
    // @constants RRT_RED_SCALE=0.82, RRT_RED_PIVOT=0.03, RRT_RED_HUE=0, RRT_RED_WIDTH=135
    constexpr float RRT_RED_SCALE = 0.82f;
    constexpr float RRT_RED_PIVOT = 0.03f;
    constexpr float RRT_RED_HUE   = 0.0f;
    constexpr float RRT_RED_WIDTH = 135.0f;

    const float hue          = aces_rgb_2_hue(ColorAP0);
    const float centeredHue  = aces_center_hue(hue, RRT_RED_HUE);
    // smoothstep(0,1, 1 - abs(2*centeredHue/RRT_RED_WIDTH))
    const float hueWeight    = Pow2(Smoothstep(0.0f, 1.0f,
                                    1.0f - std::abs(2.0f * centeredHue / RRT_RED_WIDTH)));

    ColorAP0.x += hueWeight * saturation * (RRT_RED_PIVOT - ColorAP0.x) * (1.0f - RRT_RED_SCALE);

    // ── Convert to ACEScg (AP1) working space ─────────────────────────────────
    // @source UE5 TonemapCommon.ush:170
    Vec3 WorkingColor = kACES_AP0_2_AP1_MAT.MulVec(ColorAP0);
    WorkingColor = WorkingColor.Max(0.0f);

    // ── Pre-desaturate ────────────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:175 — pre_desaturate_factor = 0.96
    const float lum0 = WorkingColor.Dot(kACES_AP1_RGB2Y);
    WorkingColor = Vec3{lum0, lum0, lum0} * (1.0f - 0.96f) + WorkingColor * 0.96f;

    // ── Custom S-curve with artist parameters ─────────────────────────────────
    // @source UE5 TonemapCommon.ush:177-213

    const float ToeScale      = 1.0f + FilmBlackClip - FilmToe;
    const float ShoulderScale = 1.0f + FilmWhiteClip - FilmShoulder;

    // @constants InMatch=0.18, OutMatch=0.18 (TonemapCommon.ush:180-181)
    constexpr float InMatch  = 0.18f;
    constexpr float OutMatch = 0.18f;

    float ToeMatch;
    if (FilmToe > 0.8f) {
        // 0.18 will be on straight segment — TonemapCommon.ush:187
        ToeMatch = (1.0f - FilmToe - OutMatch) / FilmSlope + std::log10(InMatch);
    } else {
        // 0.18 will be on toe segment — TonemapCommon.ush:193-195
        const float bt = (OutMatch + FilmBlackClip) / ToeScale - 1.0f;
        ToeMatch = std::log10(InMatch) -
                   0.5f * std::log((1.0f + bt) / (1.0f - bt)) * (ToeScale / FilmSlope);
    }

    // @source UE5 TonemapCommon.ush:198-199
    const float StraightMatch  = (1.0f - FilmToe) / FilmSlope - ToeMatch;
    const float ShoulderMatch  = FilmShoulder / FilmSlope - StraightMatch;

    // Per-component log10 — mirrors HLSL log10()
    const Vec3 LogColor = {
        std::log10(std::max(WorkingColor.x, 1e-10f)),
        std::log10(std::max(WorkingColor.y, 1e-10f)),
        std::log10(std::max(WorkingColor.z, 1e-10f)),
    };

    // Straight segment — TonemapCommon.ush:202
    const Vec3 StraightColor = {
        FilmSlope * (LogColor.x + StraightMatch),
        FilmSlope * (LogColor.y + StraightMatch),
        FilmSlope * (LogColor.z + StraightMatch),
    };

    // Toe sigmoid — TonemapCommon.ush:204
    // ToeColor = -FilmBlackClip + 2*ToeScale / (1 + exp(-2*FilmSlope/ToeScale * (LogColor - ToeMatch)))
    const auto ToeFunc = [&](float lc) -> float {
        return (-FilmBlackClip) + (2.0f * ToeScale) /
               (1.0f + std::exp((-2.0f * FilmSlope / ToeScale) * (lc - ToeMatch)));
    };

    // Shoulder sigmoid — TonemapCommon.ush:205
    // ShoulderColor = (1 + FilmWhiteClip) - 2*ShoulderScale / (1 + exp(2*FilmSlope/ShoulderScale * (LogColor - ShoulderMatch)))
    const auto ShoulderFunc = [&](float lc) -> float {
        return (1.0f + FilmWhiteClip) - (2.0f * ShoulderScale) /
               (1.0f + std::exp((2.0f * FilmSlope / ShoulderScale) * (lc - ShoulderMatch)));
    };

    // Per-component toe and shoulder — TonemapCommon.ush:207-208
    // Select toe or straight based on log position relative to ToeMatch
    // (step(ToeMatch, LogColor) == 1 when LogColor >= ToeMatch)
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

    // Smooth blend between toe and shoulder via cubic Hermite — TonemapCommon.ush:210-213
    const auto BlendTS = [&](float lc, float tc, float sc) -> float {
        float t = Saturate((lc - ToeMatch) / (ShoulderMatch - ToeMatch));
        // lerp(t, 1-t, step(ShoulderMatch, ToeMatch)) — when ShoulderMatch < ToeMatch, flip
        if (ShoulderMatch < ToeMatch) { t = 1.0f - t; }
        t = (3.0f - 2.0f * t) * t * t;  // cubic Hermite
        return tc + t * (sc - tc);
    };

    Vec3 ToneColor = {
        BlendTS(LogColor.x, ToeColor.x, ShoulderColor.x),
        BlendTS(LogColor.y, ToeColor.y, ShoulderColor.y),
        BlendTS(LogColor.z, ToeColor.z, ShoulderColor.z),
    };

    // ── Post-desaturate ───────────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:216 — post_desaturate_factor = 0.93
    const float lum1 = Vec3{ToneColor.x, ToneColor.y, ToneColor.z}.Dot(kACES_AP1_RGB2Y);
    ToneColor = Vec3{lum1, lum1, lum1} * (1.0f - 0.93f) + ToneColor * 0.93f;

    // Clip to >= 0 — TonemapCommon.ush:219
    return ToneColor.Max(0.0f);
}

/// Convenience overload with UE5 default parameters.
///
/// @source UE5 PostProcessSettings.h — FilmSlope=0.88, FilmToe=0.55, FilmShoulder=0.26,
///   FilmBlackClip=0.0, FilmWhiteClip=0.04
[[nodiscard]] inline Vec3 FilmToneMap(Vec3 LinearColor) noexcept {
    return FilmToneMap(LinearColor, 0.88f, 0.55f, 0.26f, 0.0f, 0.04f);
}

} // namespace uz::brdf
