/// UE5 ACES FilmToneMap — full parity with UE5's artist-parameterized tonemap curve.
///
/// Implements the complete UE5 FilmToneMap from TonemapCommon.ush:105-220, including:
/// - ACES glow module (subtle saturation-dependent glow in dark regions)
/// - ACES red modifier (hue-preserving red desaturation)
/// - Pre/post desaturation (AP1 working space)
/// - Custom S-curve with 5 artist parameters (Slope, Toe, Shoulder, BlackClip, WhiteClip)
///
/// The S-curve uses log10 domain with piecewise sigmoid segments for toe and shoulder,
/// connected by a straight segment in the middle. The parameters control the shape:
/// - FilmSlope:     Steepness of the straight (linear) segment. Default 0.88.
/// - FilmToe:       Size of the toe region (dark rolloff). Default 0.55.
/// - FilmShoulder:  Size of the shoulder region (highlight rolloff). Default 0.26.
/// - FilmBlackClip: Lifts black level (adds to min output). Default 0.0.
/// - FilmWhiteClip: Lowers white ceiling (subtracts from max output). Default 0.04.
///
/// @source UE5 TonemapCommon.ush:105-220 — FilmToneMap
/// @source UE5 ACESCommon.ush:140-299 — rgb_2_saturation, rgb_2_hue, center_hue, rgb_2_yc, sigmoid_shaper, glow_fwd
/// @constants See individual @source citations below for every constant
/// @deviations NONE

#ifndef FILM_TONEMAP_HLSLI
#define FILM_TONEMAP_HLSLI

#include "math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// ACES Color Space Matrices
// ─────────────────────────────────────────────────────────────────────────────
// All matrices from UE5 ACESCommon.ush, exact values.

/// @source UE5 ACESCommon.ush:17-22 — AP0_2_XYZ_MAT
static const float3x3 ACES_AP0_2_XYZ_MAT =
{
    0.9525523959, 0.0000000000, 0.0000936786,
    0.3439664498, 0.7281660966,-0.0721325464,
    0.0000000000, 0.0000000000, 1.0088251844,
};

/// @source UE5 ACESCommon.ush:24-29 — XYZ_2_AP0_MAT
static const float3x3 ACES_XYZ_2_AP0_MAT =
{
     1.0498110175, 0.0000000000,-0.0000974845,
    -0.4959030231, 1.3733130458, 0.0982400361,
     0.0000000000, 0.0000000000, 0.9912520182,
};

/// @source UE5 ACESCommon.ush:31-36 — AP1_2_XYZ_MAT
static const float3x3 ACES_AP1_2_XYZ_MAT =
{
     0.6624541811, 0.1340042065, 0.1561876870,
     0.2722287168, 0.6740817658, 0.0536895174,
    -0.0055746495, 0.0040607335, 1.0103391003,
};

/// @source UE5 ACESCommon.ush:38-43 — XYZ_2_AP1_MAT
static const float3x3 ACES_XYZ_2_AP1_MAT =
{
     1.6410233797, -0.3248032942, -0.2364246952,
    -0.6636628587,  1.6153315917,  0.0167563477,
     0.0117218943, -0.0082844420,  0.9883948585,
};

/// @source UE5 ACESCommon.ush:45-50 — AP0_2_AP1_MAT
static const float3x3 ACES_AP0_2_AP1_MAT =
{
     1.4514393161, -0.2365107469, -0.2149285693,
    -0.0765537734,  1.1762296998, -0.0996759264,
     0.0083161484, -0.0060324498,  0.9977163014,
};

/// @source UE5 ACESCommon.ush:52-57 — AP1_2_AP0_MAT
static const float3x3 ACES_AP1_2_AP0_MAT =
{
     0.6954522414,  0.1406786965,  0.1638690622,
     0.0447945634,  0.8596711185,  0.0955343182,
    -0.0055258826,  0.0040252103,  1.0015006723,
};

/// @source UE5 ACESCommon.ush:59-64 — AP1_RGB2Y (luminance weights for ACEScg)
static const float3 ACES_AP1_RGB2Y =
{
    0.2722287168,
    0.6740817658,
    0.0536895174,
};

// ─────────────────────────────────────────────────────────────────────────────
// ACES Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ACESCommon.ush:140-145 — rgb_2_saturation
float aces_rgb_2_saturation(float3 rgb)
{
    float minrgb = min(min(rgb.r, rgb.g), rgb.b);
    float maxrgb = max(max(rgb.r, rgb.g), rgb.b);
    return (max(maxrgb, 1e-10) - max(minrgb, 1e-10)) / max(maxrgb, 1e-2);
}

/// @source UE5 ACESCommon.ush:148-164 — rgb_2_hue
float aces_rgb_2_hue(float3 rgb)
{
    float hue;
    if (rgb[0] == rgb[1] && rgb[1] == rgb[2]) {
        hue = 0;
    }
    else {
        hue = (180.0 / 3.14159265359) * atan2(sqrt(3.0) * (rgb[1] - rgb[2]), 2 * rgb[0] - rgb[1] - rgb[2]);
    }

    if (hue < 0.0) hue = hue + 360;

    return clamp(hue, 0, 360);
}

/// @source UE5 ACESCommon.ush:166-172 — center_hue
float aces_center_hue(float hue, float centerH)
{
    float hueCentered = hue - centerH;
    if (hueCentered < -180.0) hueCentered += 360;
    else if (hueCentered > 180.0) hueCentered -= 360;
    return hueCentered;
}

/// @source UE5 ACESCommon.ush:268-290 — rgb_2_yc
/// @constants ycRadiusWeight = 1.75 (ACESCommon.ush:268)
float aces_rgb_2_yc(float3 rgb, float ycRadiusWeight = 1.75)
{
    float r = rgb[0];
    float g = rgb[1];
    float b = rgb[2];

    float chroma = sqrt(b * (b - g) + g * (g - r) + r * (r - b));

    return (b + g + r + ycRadiusWeight * chroma) / 3.0;
}

/// @source UE5 ACESCommon.ush:292-299 — sigmoid_shaper
float aces_sigmoid_shaper(float x)
{
    float t = max(1 - abs(0.5 * x), 0);
    float y = 1 + sign(x) * (1 - t * t);
    return 0.5 * y;
}

/// @source UE5 ACESCommon.ush:234-249 — glow_fwd
float aces_glow_fwd(float ycIn, float glowGainIn, float glowMid)
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
// FilmToneMap — UE5 exact implementation
// ─────────────────────────────────────────────────────────────────────────────

/// UE5 FilmToneMap — full ACES-based tonemap with artist parameters.
///
/// Input: Linear HDR color in AP1 (ACEScg) color space.
/// Output: Tonemapped color in AP1 color space, [0, 1+WhiteClip].
///
/// The curve operates in log10 domain with three segments:
/// - Toe: sigmoid below ToeMatch (dark compression)
/// - Straight: linear segment at FilmSlope
/// - Shoulder: sigmoid above ShoulderMatch (highlight compression)
/// Segments are smoothly blended with a cubic Hermite interpolant.
///
/// @source UE5 TonemapCommon.ush:105-220 — FilmToneMap
/// @constants RRT_GLOW_GAIN=0.05 (TonemapCommon.ush:146), RRT_GLOW_MID=0.08 (TonemapCommon.ush:147)
/// @constants RRT_RED_SCALE=0.82 (TonemapCommon.ush:158), RRT_RED_PIVOT=0.03 (TonemapCommon.ush:159)
/// @constants RRT_RED_HUE=0 (TonemapCommon.ush:160), RRT_RED_WIDTH=135 (TonemapCommon.ush:161)
/// @constants pre_desaturate=0.96 (TonemapCommon.ush:175), post_desaturate=0.93 (TonemapCommon.ush:216)
/// @constants InMatch=0.18 (TonemapCommon.ush:180), OutMatch=0.18 (TonemapCommon.ush:181)
/// @deviations NONE
half3 FilmToneMap(half3 LinearColor,
                  float FilmSlope,
                  float FilmToe,
                  float FilmShoulder,
                  float FilmBlackClip,
                  float FilmWhiteClip)
{
    // Input is already in AP1 (ACEScg) working space.
    // UE5 TonemapCommon.ush:116-117 — ColorAP1 = LinearColor (the sRGB_2_AP1 mul is commented out)
    float3 ColorAP1 = LinearColor;

    // Convert to AP0 for glow and red modifier modules
    // @source UE5 TonemapCommon.ush:133
    float3 ColorAP0 = mul(ACES_AP1_2_AP0_MAT, ColorAP1);

    // ── Glow module ──────────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:145-154
    // @constants RRT_GLOW_GAIN=0.05 (TonemapCommon.ush:146), RRT_GLOW_MID=0.08 (TonemapCommon.ush:147)
    const float RRT_GLOW_GAIN = 0.05;
    const float RRT_GLOW_MID = 0.08;

    float saturation = aces_rgb_2_saturation(ColorAP0);
    float ycIn = aces_rgb_2_yc(ColorAP0);
    float s = aces_sigmoid_shaper((saturation - 0.4) / 0.2);
    float addedGlow = 1 + aces_glow_fwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
    ColorAP0 *= addedGlow;

    // ── Red modifier ─────────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:157-167
    // @constants RRT_RED_SCALE=0.82, RRT_RED_PIVOT=0.03, RRT_RED_HUE=0, RRT_RED_WIDTH=135
    const float RRT_RED_SCALE = 0.82;
    const float RRT_RED_PIVOT = 0.03;
    const float RRT_RED_HUE = 0;
    const float RRT_RED_WIDTH = 135;
    float hue = aces_rgb_2_hue(ColorAP0);
    float centeredHue = aces_center_hue(hue, RRT_RED_HUE);
    float hueWeight = Pow2(smoothstep(0, 1, 1 - abs(2 * centeredHue / RRT_RED_WIDTH)));

    ColorAP0.r += hueWeight * saturation * (RRT_RED_PIVOT - ColorAP0.r) * (1.0 - RRT_RED_SCALE);

    // ── Convert to ACEScg (AP1) working space ────────────────────────────
    // @source UE5 TonemapCommon.ush:170
    float3 WorkingColor = mul(ACES_AP0_2_AP1_MAT, ColorAP0);

    WorkingColor = max(0, WorkingColor);

    // ── Pre-desaturate ───────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:175
    // @constants pre_desaturate_factor = 0.96
    WorkingColor = lerp(dot(WorkingColor, ACES_AP1_RGB2Y), WorkingColor, 0.96);

    // ── Custom S-curve with artist parameters ────────────────────────────
    // @source UE5 TonemapCommon.ush:177-213

    const half ToeScale      = 1 + FilmBlackClip - FilmToe;
    const half ShoulderScale = 1 + FilmWhiteClip - FilmShoulder;

    // @constants InMatch=0.18, OutMatch=0.18 (TonemapCommon.ush:180-181)
    const float InMatch = 0.18;
    const float OutMatch = 0.18;

    float ToeMatch;
    if (FilmToe > 0.8)
    {
        // 0.18 will be on straight segment
        // @source UE5 TonemapCommon.ush:187
        ToeMatch = (1 - FilmToe - OutMatch) / FilmSlope + log10(InMatch);
    }
    else
    {
        // 0.18 will be on toe segment
        // Solve for ToeMatch such that input of InMatch gives output of OutMatch.
        // @source UE5 TonemapCommon.ush:193-195
        const float bt = (OutMatch + FilmBlackClip) / ToeScale - 1;
        ToeMatch = log10(InMatch) - 0.5 * log((1 + bt) / (1 - bt)) * (ToeScale / FilmSlope);
    }

    // @source UE5 TonemapCommon.ush:198-199
    float StraightMatch = (1 - FilmToe) / FilmSlope - ToeMatch;
    float ShoulderMatch = FilmShoulder / FilmSlope - StraightMatch;

    // @source UE5 TonemapCommon.ush:201-202
    half3 LogColor = log10(WorkingColor);
    half3 StraightColor = FilmSlope * (LogColor + StraightMatch);

    // @source UE5 TonemapCommon.ush:204-205
    // Toe sigmoid: maps dark values with BlackClip offset
    half3 ToeColor      = (-FilmBlackClip) + (2 * ToeScale) / (1 + exp((-2 * FilmSlope / ToeScale) * (LogColor - ToeMatch)));
    // Shoulder sigmoid: maps bright values with WhiteClip ceiling
    half3 ShoulderColor = (1 + FilmWhiteClip) - (2 * ShoulderScale) / (1 + exp((2 * FilmSlope / ShoulderScale) * (LogColor - ShoulderMatch)));

    // Select toe or straight based on position relative to ToeMatch
    // @source UE5 TonemapCommon.ush:207-208
    // DXC SM6.6+ requires select() for vector conditional; UE5 uses same pattern.
    ToeColor      = select(LogColor < ToeMatch,      ToeColor,      StraightColor);
    ShoulderColor = select(LogColor > ShoulderMatch,  ShoulderColor, StraightColor);

    // Smooth blend between toe and shoulder using cubic Hermite interpolation
    // @source UE5 TonemapCommon.ush:210-213
    half3 t = saturate((LogColor - ToeMatch) / (ShoulderMatch - ToeMatch));
    t = (ShoulderMatch < ToeMatch) ? 1 - t : t;
    t = (3 - 2 * t) * t * t;
    half3 ToneColor = lerp(ToeColor, ShoulderColor, t);

    // ── Post-desaturate ──────────────────────────────────────────────────
    // @source UE5 TonemapCommon.ush:216
    // @constants post_desaturate_factor = 0.93
    ToneColor = lerp(dot(float3(ToneColor), ACES_AP1_RGB2Y), ToneColor, 0.93);

    // Returning positive AP1 values
    // @source UE5 TonemapCommon.ush:219
    return max(0, ToneColor);
}

/// Convenience overload with UE5 default parameters.
///
/// Default values from UE5 PostProcessSettings.h:
/// @source UE5 PostProcessSettings.h — FilmSlope=0.88, FilmToe=0.55, FilmShoulder=0.26, FilmBlackClip=0.0, FilmWhiteClip=0.04
/// @deviations NONE
half3 FilmToneMap(half3 LinearColor)
{
    return FilmToneMap(LinearColor, 0.88, 0.55, 0.26, 0.0, 0.04);
}

#endif // FILM_TONEMAP_HLSLI
