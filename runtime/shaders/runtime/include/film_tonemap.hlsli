/// ACES-based tone curve — full artist-parameterized tonemap with ACES color science.
///
/// Implements a complete ACES-based tonemap curve including:
/// - ACES glow module (subtle saturation-dependent glow in dark regions)
/// - ACES red modifier (hue-preserving red desaturation)
/// - Pre/post desaturation (AP1 working space)
/// - Custom S-curve with 5 artist parameters (Slope, Toe, Shoulder, BlackClip, WhiteClip)
///
/// The S-curve uses log10 domain with piecewise sigmoid segments for toe and shoulder,
/// connected by a straight segment in the middle. The parameters control the shape:
/// - toneCurveSlope:     Steepness of the straight (linear) segment. Default 0.88.
/// - toneCurveToe:       Size of the toe region (dark rolloff). Default 0.55.
/// - toneCurveShoulder:  Size of the shoulder region (highlight rolloff). Default 0.26.
/// - toneCurveBlackClip: Lifts black level (adds to min output). Default 0.0.
/// - toneCurveWhiteClip: Lowers white ceiling (subtracts from max output). Default 0.04.
///
/// @ref ACES Reference Implementation (Academy S-2014-006)
/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — rgb_2_saturation, rgb_2_hue, center_hue, rgb_2_yc, sigmoid_shaper, glow_fwd
/// @constants See individual @ref citations below for every constant
/// @deviations NONE

#ifndef FILM_TONEMAP_HLSLI
#define FILM_TONEMAP_HLSLI

#include "math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// ACES Color Space Matrices
// ─────────────────────────────────────────────────────────────────────────────
// All matrices from the ACES CTL Reference Implementation (Academy S-2014-006), exact values.

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — AP0_2_XYZ_MAT
static const float3x3 ACES_AP0_2_XYZ_MAT =
{
    0.9525523959, 0.0000000000, 0.0000936786,
    0.3439664498, 0.7281660966,-0.0721325464,
    0.0000000000, 0.0000000000, 1.0088251844,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — XYZ_2_AP0_MAT
static const float3x3 ACES_XYZ_2_AP0_MAT =
{
     1.0498110175, 0.0000000000,-0.0000974845,
    -0.4959030231, 1.3733130458, 0.0982400361,
     0.0000000000, 0.0000000000, 0.9912520182,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — AP1_2_XYZ_MAT
static const float3x3 ACES_AP1_2_XYZ_MAT =
{
     0.6624541811, 0.1340042065, 0.1561876870,
     0.2722287168, 0.6740817658, 0.0536895174,
    -0.0055746495, 0.0040607335, 1.0103391003,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — XYZ_2_AP1_MAT
static const float3x3 ACES_XYZ_2_AP1_MAT =
{
     1.6410233797, -0.3248032942, -0.2364246952,
    -0.6636628587,  1.6153315917,  0.0167563477,
     0.0117218943, -0.0082844420,  0.9883948585,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — AP0_2_AP1_MAT
static const float3x3 ACES_AP0_2_AP1_MAT =
{
     1.4514393161, -0.2365107469, -0.2149285693,
    -0.0765537734,  1.1762296998, -0.0996759264,
     0.0083161484, -0.0060324498,  0.9977163014,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — AP1_2_AP0_MAT
static const float3x3 ACES_AP1_2_AP0_MAT =
{
     0.6954522414,  0.1406786965,  0.1638690622,
     0.0447945634,  0.8596711185,  0.0955343182,
    -0.0055258826,  0.0040252103,  1.0015006723,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — AP1_RGB2Y (luminance weights for ACEScg)
static const float3 ACES_AP1_RGB2Y =
{
    0.2722287168,
    0.6740817658,
    0.0536895174,
};

// ─────────────────────────────────────────────────────────────────────────────
// ACES Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — rgb_2_saturation
float RgbToSaturation(float3 rgb)
{
    float minrgb = min(min(rgb.r, rgb.g), rgb.b);
    float maxrgb = max(max(rgb.r, rgb.g), rgb.b);
    return (max(maxrgb, 1e-10) - max(minrgb, 1e-10)) / max(maxrgb, 1e-2);
}

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — rgb_2_hue
float RgbToHue(float3 rgb)
{
    float hue;
    if (rgb[0] == rgb[1] && rgb[1] == rgb[2]) {
        hue = 0;
    }
    else {
        hue = (180.0 * M_1_PI) * atan2(sqrt(3.0) * (rgb[1] - rgb[2]), 2 * rgb[0] - rgb[1] - rgb[2]);
    }

    if (hue < 0.0) hue = hue + 360;

    return clamp(hue, 0, 360);
}

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — center_hue
float CenterHue(float hue, float centerH)
{
    float hueCentered = hue - centerH;
    if (hueCentered < -180.0) hueCentered += 360;
    else if (hueCentered > 180.0) hueCentered -= 360;
    return hueCentered;
}

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — rgb_2_yc
/// @constants ycRadiusWeight = 1.75
float RgbToLumaChroma(float3 rgb, float ycRadiusWeight = 1.75)
{
    float r = rgb[0];
    float g = rgb[1];
    float b = rgb[2];

    float chroma = sqrt(b * (b - g) + g * (g - r) + r * (r - b));

    return (b + g + r + ycRadiusWeight * chroma) / 3.0;
}

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — sigmoid_shaper
float SigmoidShaper(float x)
{
    float t = max(1 - abs(0.5 * x), 0);
    float y = 1 + sign(x) * (1 - t * t);
    return 0.5 * y;
}

/// @ref ACES CTL Reference Implementation (Academy S-2014-006) — glow_fwd
float GlowModuleFwd(float ycIn, float glowGainIn, float glowMid)
{
    float glowGainOut;

    if (ycIn <= 2.0 / 3.0 * glowMid) {
        glowGainOut = glowGainIn;
    }
    else if (ycIn >= 2 * glowMid) {
        glowGainOut = 0;
    }
    else {
        glowGainOut = glowGainIn * (glowMid / ycIn - 0.5);
    }

    return glowGainOut;
}

// ─────────────────────────────────────────────────────────────────────────────
// ApplyToneCurve — ACES-based tone curve implementation
// ─────────────────────────────────────────────────────────────────────────────

/// ACES-based tone curve with artist parameters.
///
/// Input: Linear HDR color in AP1 (ACEScg) color space.
/// Output: Tonemapped color in AP1 color space, [0, 1+WhiteClip].
///
/// The curve operates in log10 domain with three segments:
/// - Toe: sigmoid below ToeMatch (dark compression)
/// - Straight: linear segment at toneCurveSlope
/// - Shoulder: sigmoid above ShoulderMatch (highlight compression)
/// Segments are smoothly blended with a cubic Hermite interpolant.
///
/// @ref ACES Reference Implementation (Academy S-2014-006) — tone curve
/// @constants RRT_GLOW_GAIN=0.05, RRT_GLOW_MID=0.08
/// @constants RRT_RED_SCALE=0.82, RRT_RED_PIVOT=0.03
/// @constants RRT_RED_HUE=0, RRT_RED_WIDTH=135
/// @constants pre_desaturate=0.96, post_desaturate=0.93
/// @constants InMatch=0.18, OutMatch=0.18
/// @deviations NONE
half3 ApplyToneCurve(half3 LinearColor,
                  float toneCurveSlope,
                  float toneCurveToe,
                  float toneCurveShoulder,
                  float toneCurveBlackClip,
                  float toneCurveWhiteClip)
{
    // Input is already in AP1 (ACEScg) working space.
    // ColorAP1 = LinearColor (the sRGB_2_AP1 mul is commented out in ACES RRT)
    float3 ColorAP1 = LinearColor;

    // Convert to AP0 for glow and red modifier modules
    float3 ColorAP0 = mul(ACES_AP1_2_AP0_MAT, ColorAP1);

    // ── Glow module ──────────────────────────────────────────────────────
    // @ref ACES Reference Implementation (Academy S-2014-006) — RRT glow module
    // @constants RRT_GLOW_GAIN=0.05, RRT_GLOW_MID=0.08
    const float RRT_GLOW_GAIN = 0.05;
    const float RRT_GLOW_MID = 0.08;

    float saturation = RgbToSaturation(ColorAP0);
    float ycIn = RgbToLumaChroma(ColorAP0);
    float s = SigmoidShaper((saturation - 0.4) / 0.2);
    float addedGlow = 1 + GlowModuleFwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
    ColorAP0 *= addedGlow;

    // ── Red modifier ─────────────────────────────────────────────────────
    // @ref ACES Reference Implementation (Academy S-2014-006) — RRT red modifier
    // @constants RRT_RED_SCALE=0.82, RRT_RED_PIVOT=0.03, RRT_RED_HUE=0, RRT_RED_WIDTH=135
    const float RRT_RED_SCALE = 0.82;
    const float RRT_RED_PIVOT = 0.03;
    const float RRT_RED_HUE = 0;
    const float RRT_RED_WIDTH = 135;
    float hue = RgbToHue(ColorAP0);
    float centeredHue = CenterHue(hue, RRT_RED_HUE);
    float hueWeight = Pow2(smoothstep(0, 1, 1 - abs(2 * centeredHue / RRT_RED_WIDTH)));

    ColorAP0.r += hueWeight * saturation * (RRT_RED_PIVOT - ColorAP0.r) * (1.0 - RRT_RED_SCALE);

    // ── Convert to ACEScg (AP1) working space ────────────────────────────
    float3 WorkingColor = mul(ACES_AP0_2_AP1_MAT, ColorAP0);

    WorkingColor = max(0, WorkingColor);

    // ── Pre-desaturate ───────────────────────────────────────────────────
    // @constants pre_desaturate_factor = 0.96
    WorkingColor = lerp(dot(WorkingColor, ACES_AP1_RGB2Y), WorkingColor, 0.96);

    // ── Custom S-curve with artist parameters ────────────────────────────

    const half ToeScale      = 1 + toneCurveBlackClip - toneCurveToe;
    const half ShoulderScale = 1 + toneCurveWhiteClip - toneCurveShoulder;

    // @constants InMatch=0.18, OutMatch=0.18
    const float InMatch = 0.18;
    const float OutMatch = 0.18;

    float ToeMatch;
    if (toneCurveToe > 0.8)
    {
        // 0.18 will be on straight segment
        ToeMatch = (1 - toneCurveToe - OutMatch) / toneCurveSlope + log10(InMatch);
    }
    else
    {
        // 0.18 will be on toe segment
        // Solve for ToeMatch such that input of InMatch gives output of OutMatch.
        const float bt = (OutMatch + toneCurveBlackClip) / ToeScale - 1;
        ToeMatch = log10(InMatch) - 0.5 * log((1 + bt) / (1 - bt)) * (ToeScale / toneCurveSlope);
    }

    float StraightMatch = (1 - toneCurveToe) / toneCurveSlope - ToeMatch;
    float ShoulderMatch = toneCurveShoulder / toneCurveSlope - StraightMatch;

    half3 LogColor = log10(WorkingColor);
    half3 StraightColor = toneCurveSlope * (LogColor + StraightMatch);

    // Toe sigmoid: maps dark values with BlackClip offset
    half3 ToeColor      = (-toneCurveBlackClip) + (2 * ToeScale) / (1 + exp((-2 * toneCurveSlope / ToeScale) * (LogColor - ToeMatch)));
    // Shoulder sigmoid: maps bright values with WhiteClip ceiling
    half3 ShoulderColor = (1 + toneCurveWhiteClip) - (2 * ShoulderScale) / (1 + exp((2 * toneCurveSlope / ShoulderScale) * (LogColor - ShoulderMatch)));

    // Select toe or straight based on position relative to ToeMatch
    // Use lerp+step for component-wise selection — works on both FXC (SM5) and DXC (SM6.6+).
    // step(edge, x) returns 1 if x >= edge, 0 otherwise (per-component).
    ToeColor      = lerp(ToeColor,      StraightColor, step(ToeMatch, LogColor));
    ShoulderColor = lerp(ShoulderColor,  StraightColor, step(LogColor, ShoulderMatch));

    // Smooth blend between toe and shoulder using cubic Hermite interpolation
    half3 t = saturate((LogColor - ToeMatch) / (ShoulderMatch - ToeMatch));
    t = lerp(t, 1 - t, step(ShoulderMatch, ToeMatch));
    t = (3 - 2 * t) * t * t;
    half3 ToneColor = lerp(ToeColor, ShoulderColor, t);

    // ── Post-desaturate ──────────────────────────────────────────────────
    // @constants post_desaturate_factor = 0.93
    ToneColor = lerp(dot(float3(ToneColor), ACES_AP1_RGB2Y), ToneColor, 0.93);

    // Returning positive AP1 values
    return max(0, ToneColor);
}

/// Convenience overload with default tone curve parameters.
///
/// Default values: toneCurveSlope=0.88, toneCurveToe=0.55, toneCurveShoulder=0.26, toneCurveBlackClip=0.0, toneCurveWhiteClip=0.04
/// @deviations NONE
half3 ApplyToneCurve(half3 LinearColor)
{
    return ApplyToneCurve(LinearColor, 0.88, 0.55, 0.26, 0.0, 0.04);
}

// ─────────────────────────────────────────────────────────────────────────────
// Alternative Tone Mapping Operators
// ─────────────────────────────────────────────────────────────────────────────

/// Simple Reinhard tonemap: x / (1 + x).
/// @ref Reinhard et al. 2002 "Photographic Tone Reproduction for Digital Images"
float3 ReinhardTonemap(float3 x) {
    return x / (1.0 + x);
}

/// Modified Reinhard with white point: x * (1 + x/whitePoint²) / (1 + x).
/// Allows highlights above whitePoint to clip to white.
/// @ref Reinhard et al. 2002 — extended operator with white point
float3 ReinhardModifiedTonemap(float3 x, float whitePoint) {
    float wp2 = whitePoint * whitePoint;
    return x * (1.0 + x / wp2) / (1.0 + x);
}

/// Uncharted 2 filmic S-curve (Hable tone mapping).
/// @ref Hable 2010 "Filmic Tonemapping Operators" GDC
/// @constants A=0.15 ShoulderStrength, B=0.50 LinearStrength, C=0.10 LinearAngle,
///   D=0.20 ToeStrength, E=0.02 ToeNumerator, F=0.30 ToeDenominator
float3 HableUC2Curve(float3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 HableUC2Tonemap(float3 x) {
    const float W = 11.2; // Linear white point
    float3 curr = HableUC2Curve(x);
    float3 whiteScale = 1.0 / HableUC2Curve(float3(W, W, W));
    return curr * whiteScale;
}

// White balance: D_IlluminantChromaticity + WhiteBalance are defined in
// color_grading_lut_cs.hlsl (Judd 1964 corrected version). Use that shader's
// implementation for color grading. For tonemap_ps.hlsl white balance,
// use the color grading LUT pipeline instead of inline computation.

// ─────────────────────────────────────────────────────────────────────────────
// Physical Camera Exposure
// ─────────────────────────────────────────────────────────────────────────────

/// Compute EV100 from physical camera settings.
/// @ref ISO 12232:2006 — Saturation-based speed (SBS) exposure value
/// @constants EV100 = log2(N² / t * 100 / S)
float EV100(float aperture, float shutterSpeed, float ISO) {
    return log2((aperture * aperture) / shutterSpeed * 100.0 / ISO);
}

/// Convert EV100 to linear exposure multiplier.
/// @ref Lagarde 2014 "Moving Frostbite to PBR" — exposure from EV100
/// @constants exposure = 1 / (1.2 * 2^EV100)
float EV100ToExposure(float ev100) {
    return 1.0 / (1.2 * exp2(ev100));
}

#endif // FILM_TONEMAP_HLSLI
