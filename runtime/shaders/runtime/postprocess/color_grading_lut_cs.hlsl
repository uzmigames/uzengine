/// Color Grading LUT Generation Compute Shader
///
/// Generates a 32x32x32 3D LUT that bakes the entire color grading + tonemapping
/// pipeline into a single texture lookup. This is the UE5 approach: instead of
/// computing white balance + color correction + FilmToneMap + sRGB per pixel,
/// we pre-bake all of it into a 3D LUT and sample it in the tonemap shader.
///
/// The pipeline per texel matches UE5 PostProcessCombineLUTs.usf:CombineLUTsCommon():
///   1. Decode log-encoded neutral color to linear via LogToLin
///   2. White balance (Planckian locus + D-illuminant + Bradford chromatic adaptation)
///   3. Convert to AP1 (ACEScg) working space
///   4. Expand gamut (fake wide gamut for saturated brights)
///   5. Nuke-style color correction (shadow/midtone/highlight zones)
///   6. Blue correction (desaturate problematic blues)
///   7. FilmToneMap (ACES-based custom S-curve)
///   8. Blue uncorrection (restore white point)
///   9. ColorCorrection polynomial (r.Color.Min/Mid/Max mapping)
///  10. Apply color scale + overlay
///  11. Apply gamma curve
///  12. Convert to sRGB output encoding
///  13. Divide by 1.05 (UE5 LUT output normalization)
///
/// @source UE5 PostProcessCombineLUTs.usf:159-423 — CombineLUTsCommon
/// @source UE5 PostProcessCombineLUTs.usf:446-455 — MainCS (volume LUT path)
/// @source UE5 PostProcessCombineLUTs.cpp:322 — GroupSize = 8
/// @source UE5 PostProcessCombineLUTs.cpp:39 — r.LUT.Size default = 32
/// @deviations
///   - No external LUT blending (BLENDCOUNT=1, neutral only)
///   - No HDR output paths (ACES1000nit/2000nit/ScRGB/LinearEXR/NoToneCurve/WithToneCurve)
///   - No WorkingColorSpace uniform buffer (assumes sRGB working space, bIsSRGB=true)
///   - No ACES 2.0 path (USE_ACES_2=0)
///   - No mobile tonemapper path
///   - FilmToneMap parameters passed via PerFrameCB instead of dedicated uniform

#include "../include/film_tonemap.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// LUT Generation Parameters (root constants at b0)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 PostProcessCombineLUTs.usf:39-46,66-68 — parameter declarations
/// @source UE5 Scene.cpp:373-407 — default values
cbuffer LutGenParams : register(b2) {
    // ── White balance ────────────────────────────────────────────────────
    /// @source UE5 Scene.cpp:373 — WhiteTemp = 6500.0f
    float WhiteTemp;
    /// @source UE5 Scene.cpp:374 — WhiteTint = 0.0f
    float WhiteTint;

    // ── Global color correction ──────────────────────────────────────────
    /// @source UE5 Scene.cpp:377-381 — default (1,1,1,1) / (0,0,0,0) for Offset
    float4 ColorSaturation;
    float4 ColorContrast;
    float4 ColorGamma;
    float4 ColorGain;
    float4 ColorOffset;

    // ── Shadow zone color correction ─────────────────────────────────────
    /// @source UE5 Scene.cpp:383-387
    float4 ColorSaturationShadows;
    float4 ColorContrastShadows;
    float4 ColorGammaShadows;
    float4 ColorGainShadows;
    float4 ColorOffsetShadows;

    // ── Midtone zone color correction ────────────────────────────────────
    /// @source UE5 Scene.cpp:389-393
    float4 ColorSaturationMidtones;
    float4 ColorContrastMidtones;
    float4 ColorGammaMidtones;
    float4 ColorGainMidtones;
    float4 ColorOffsetMidtones;

    // ── Highlight zone color correction ──────────────────────────────────
    /// @source UE5 Scene.cpp:395-399
    float4 ColorSaturationHighlights;
    float4 ColorContrastHighlights;
    float4 ColorGammaHighlights;
    float4 ColorGainHighlights;
    float4 ColorOffsetHighlights;

    // ── Zone boundaries ──────────────────────────────────────────────────
    /// @source UE5 Scene.cpp:401-403
    float ColorCorrectionShadowsMax;    ///< Default 0.09
    float ColorCorrectionHighlightsMin; ///< Default 0.5
    float ColorCorrectionHighlightsMax; ///< Default 1.0

    // ── Film tonemap parameters ──────────────────────────────────────────
    /// @source UE5 PostProcessSettings.h — defaults
    float FilmSlope;                    ///< Default 0.88
    float FilmToe;                      ///< Default 0.55
    float FilmShoulder;                 ///< Default 0.26
    float FilmBlackClip;                ///< Default 0.0
    float FilmWhiteClip;                ///< Default 0.04

    // ── Additional parameters ────────────────────────────────────────────
    /// @source UE5 Scene.cpp:405-407
    float BlueCorrection;               ///< Default 0.6
    float ExpandGamut;                  ///< Default 1.0
    float ToneCurveAmount;              ///< Default 1.0

    // ── ColorCorrection polynomial (r.Color.Min/Mid/Max) ─────────────────
    /// @source UE5 PostProcessCombineLUTs.cpp:86-98 — GetMappingPolynomial
    /// @source UE5 PostProcessCommon.ush:129 — MappingPolynomial = (a, b, c) where y = a*x^2 + b*x + c
    /// Default: Min=0, Mid=0.5, Max=1.0 → a=0, b=1, c=0 (identity)
    float3 MappingPolynomial;

    // ── Color scale + overlay ────────────────────────────────────────────
    /// @source UE5 PostProcessCombineLUTs.usf:31-32 — ColorScale, OverlayColor
    float3 ColorScale;                  ///< Default (1,1,1) — Fade track multiplier
    float4 OverlayColor;                ///< Default (0,0,0,0) — Fade track blend color

    // ── InverseGamma ─────────────────────────────────────────────────────
    /// @source UE5 TonemapCommon.ush:43 — InverseGamma (usually 1/2.2; .y used for gamma curve)
    /// For sRGB output, .y = 1.0 (sRGB conversion handles gamma, no extra pow needed)
    float InverseGammaY;

    // ── LUT dimensions ───────────────────────────────────────────────────
    /// @source UE5 PostProcessCombineLUTs.cpp:39 — r.LUT.Size = 32
    float LUTSize;

    // ── Output extent inverse (for UV computation) ───────────────────────
    /// @source UE5 PostProcessCombineLUTs.usf:440 — OutputExtentInverse
    float2 OutputExtentInverse;
};

// ── Output 3D texture ────────────────────────────────────────────────────────
/// @source UE5 PostProcessCombineLUTs.usf:443 — RWTexture3D<float4> RWOutputTexture
RWTexture3D<float4> RWOutputTexture : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Additional ACES color space matrices (beyond those in film_tonemap.hlsli)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ACESCommon.ush:67-72 — XYZ_2_sRGB_MAT
static const float3x3 ACES_XYZ_2_sRGB_MAT =
{
     3.2409699419, -1.5373831776, -0.4986107603,
    -0.9692436363,  1.8759675015,  0.0415550574,
     0.0556300797, -0.2039769589,  1.0569715142,
};

/// @source UE5 ACESCommon.ush:74-79 — sRGB_2_XYZ_MAT
static const float3x3 ACES_sRGB_2_XYZ_MAT =
{
    0.4123907993, 0.3575843394, 0.1804807884,
    0.2126390059, 0.7151686788, 0.0721923154,
    0.0193308187, 0.1191947798, 0.9505321522,
};

/// @source UE5 ACESCommon.ush:112-117 — D65_2_D60_CAT (Bradford)
static const float3x3 ACES_D65_2_D60_CAT =
{
     1.0130349146, 0.0061052578, -0.0149709436,
     0.0076982301, 0.9981633521, -0.0050320385,
    -0.0028413174, 0.0046851567,  0.9245061375,
};

/// @source UE5 ACESCommon.ush:119-124 — D60_2_D65_CAT (Bradford)
static const float3x3 ACES_D60_2_D65_CAT =
{
     0.9872240087, -0.0061132286, 0.0159532883,
    -0.0075983718,  1.0018614847, 0.0053300358,
     0.0030725771, -0.0050959615, 1.0816806031,
};

// ── Derived matrices ─────────────────────────────────────────────────────────
// These are computed inline in UE5 CombineLUTsCommon at line 192-194.
// We precompute them as static constants for clarity.

/// AP1 -> sRGB = XYZ_2_sRGB * D60_2_D65 * AP1_2_XYZ
/// @source UE5 PostProcessCombineLUTs.usf:192 — AP1_2_sRGB
static const float3x3 ACES_AP1_2_sRGB = {
    // mul(XYZ_2_sRGB_MAT, mul(D60_2_D65_CAT, AP1_2_XYZ_MAT))
    // Precomputed to match UE5 exactly (they compute at runtime, same result)
     1.70505188,  -0.62179212,  -0.08325976,
    -0.13025722,   1.14080280,  -0.01054558,
    -0.02400329,  -0.12896885,   1.15297214,
};

/// sRGB -> AP1 = XYZ_2_AP1 * D65_2_D60 * sRGB_2_XYZ
/// @source UE5 PostProcessCombineLUTs.usf (implicit, used by FilmToneMap and gamut expand)
static const float3x3 ACES_sRGB_2_AP1 = {
    // mul(XYZ_2_AP1_MAT, mul(D65_2_D60_CAT, sRGB_2_XYZ_MAT))
     0.61309740,   0.33952315,   0.04737945,
     0.07019372,   0.91635388,   0.01345240,
     0.02061561,   0.10956977,   0.86981462,
};

// ─────────────────────────────────────────────────────────────────────────────
// LogToLin — UE5's log-encoded LUT input to linear conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts log-encoded LUT texel value to linear scene-referred color.
/// UE5 encodes the LUT domain in log space to better distribute precision
/// across the HDR range. The encoding uses a "pure log" formula parameterized
/// by a grey point and dynamic range.
///
/// @source UE5 GammaCorrectionCommon.ush:80-93 — LogToLin
/// @constants LinearRange=14 (GammaCorrectionCommon.ush:82),
///            LinearGrey=0.18 (GammaCorrectionCommon.ush:83),
///            ExposureGrey=444 (GammaCorrectionCommon.ush:84)
/// @deviations NONE
float3 LogToLin(float3 LogColor)
{
    const float LinearRange = 14;
    const float LinearGrey = 0.18;
    const float ExposureGrey = 444;

    // Using stripped down, 'pure log', formula.
    // Parameterized by grey points and dynamic range covered.
    float3 LinearColor = exp2((LogColor - ExposureGrey / 1023.0) * LinearRange) * LinearGrey;

    return LinearColor;
}

// ─────────────────────────────────────────────────────────────────────────────
// White Balance — CIE D-illuminant + Planckian locus + Bradford CAT
// ─────────────────────────────────────────────────────────────────────────────

/// Compute CIE 1931 xy chromaticity from Planckian locus.
/// Accurate for 1000K < Temp < 15000K.
///
/// @source UE5 TonemapCommon.ush:529-538 — PlanckianLocusChromaticity
/// @constants [Krystek 1985] polynomial coefficients (TonemapCommon.ush:531-532)
/// @deviations NONE
float2 PlanckianLocusChromaticity(float Temp)
{
    float u = (0.860117757f + 1.54118254e-4f * Temp + 1.28641212e-7f * Temp * Temp) /
              (1.0f + 8.42420235e-4f * Temp + 7.08145163e-7f * Temp * Temp);
    float v = (0.317398726f + 4.22806245e-5f * Temp + 4.20481691e-8f * Temp * Temp) /
              (1.0f - 2.89741816e-5f * Temp + 1.61456053e-7f * Temp * Temp);

    float x = 3 * u / (2 * u - 8 * v + 4);
    float y = 2 * v / (2 * u - 8 * v + 4);

    return float2(x, y);
}

/// Compute CIE 1931 xy chromaticity for D-illuminant series.
/// Accurate for 4000K < Temp < 25000K.
///
/// @source UE5 TonemapCommon.ush:543-556 — D_IlluminantChromaticity
/// @constants Planck's law correction factor 1.4388/1.438 (TonemapCommon.ush:547)
/// @deviations NONE
float2 D_IlluminantChromaticity(float Temp)
{
    // Correct for revision of Planck's law — makes 6500 == D65
    Temp *= 1.4388 / 1.438;
    float OneOverTemp = 1.0 / Temp;
    float x = Temp <= 7000 ?
        0.244063 + (0.09911e3 + (2.9678e6 - 4.6070e9 * OneOverTemp) * OneOverTemp) * OneOverTemp :
        0.237040 + (0.24748e3 + (1.9018e6 - 2.0064e9 * OneOverTemp) * OneOverTemp) * OneOverTemp;

    float y = -3 * x * x + 2.87 * x - 0.275;

    return float2(x, y);
}

/// Compute Planckian isothermal offset for tint control.
///
/// @source UE5 TonemapCommon.ush:566-584 — PlanckianIsothermal
/// @constants Tint scale 0.05 (TonemapCommon.ush:577-578) — "CCT meaningful within +/- 0.05"
/// @deviations NONE
float2 PlanckianIsothermal(float Temp, float Tint)
{
    float u = (0.860117757f + 1.54118254e-4f * Temp + 1.28641212e-7f * Temp * Temp) /
              (1.0f + 8.42420235e-4f * Temp + 7.08145163e-7f * Temp * Temp);
    float v = (0.317398726f + 4.22806245e-5f * Temp + 4.20481691e-8f * Temp * Temp) /
              (1.0f - 2.89741816e-5f * Temp + 1.61456053e-7f * Temp * Temp);

    float ud = (-1.13758118e9f - 1.91615621e6f * Temp - 1.53177f * Temp * Temp) /
               Pow2(1.41213984e6f + 1189.62f * Temp + Temp * Temp);
    float vd = (1.97471536e9f - 705674.0f * Temp - 308.607f * Temp * Temp) /
               Pow2(6.19363586e6f - 179.456f * Temp + Temp * Temp);

    float2 uvd = normalize(float2(ud, vd));

    // Correlated color temperature is meaningful within +/- 0.05
    u += uvd.y * Tint * 0.05;
    v += -uvd.x * Tint * 0.05;

    float x = 3 * u / (2 * u - 8 * v + 4);
    float y = 2 * v / (2 * u - 8 * v + 4);

    return float2(x, y);
}

/// Convert CIE xyY to XYZ.
///
/// @source UE5 ACESCommon.ush:188-196 — xyY_2_XYZ
/// @deviations NONE
float3 xyY_2_XYZ(float3 xyY)
{
    float3 XYZ;
    XYZ[0] = xyY[0] * xyY[2] / max(xyY[1], 1e-10);
    XYZ[1] = xyY[2];
    XYZ[2] = (1.0 - xyY[0] - xyY[1]) * xyY[2] / max(xyY[1], 1e-10);
    return XYZ;
}

/// Bradford chromatic adaptation matrix between two white points.
///
/// @source UE5 ACESCommon.ush:199-231 — ChromaticAdaptation
/// @constants Bradford cone response matrix (ACESCommon.ush:204-215)
/// @deviations NONE
float3x3 ChromaticAdaptation(float2 src_xy, float2 dst_xy)
{
    // Bradford cone response matrix
    // @source UE5 ACESCommon.ush:204-209
    const float3x3 ConeResponse =
    {
         0.8951,  0.2664, -0.1614,
        -0.7502,  1.7135,  0.0367,
         0.0389, -0.0685,  1.0296,
    };
    // @source UE5 ACESCommon.ush:210-215
    const float3x3 InvConeResponse =
    {
         0.9869929, -0.1470543,  0.1599627,
         0.4323053,  0.5183603,  0.0492912,
        -0.0085287,  0.0400428,  0.9684867,
    };

    float3 src_XYZ = xyY_2_XYZ(float3(src_xy, 1));
    float3 dst_XYZ = xyY_2_XYZ(float3(dst_xy, 1));

    float3 src_coneResp = mul(ConeResponse, src_XYZ);
    float3 dst_coneResp = mul(ConeResponse, dst_XYZ);

    float3x3 VonKriesMat =
    {
        { dst_coneResp[0] / src_coneResp[0], 0.0, 0.0 },
        { 0.0, dst_coneResp[1] / src_coneResp[1], 0.0 },
        { 0.0, 0.0, dst_coneResp[2] / src_coneResp[2] }
    };

    return mul(InvConeResponse, mul(VonKriesMat, ConeResponse));
}

/// Full white balance transform using D-illuminant or Planckian locus.
///
/// @source UE5 TonemapCommon.ush:586-611 — WhiteBalance
/// @constants D65White = (0.31270, 0.32900) (TonemapCommon.ush:592)
/// @deviations bIsTemperatureWhiteBalance always true (we only support temperature mode).
///   UE5 supports both temperature mode and manual white point mode; we use temperature
///   which is the more common artist workflow. The WCS_2_XYZ and XYZ_2_WCS matrices
///   use sRGB_2_XYZ and XYZ_2_sRGB since our working color space is sRGB (bIsSRGB=true).
float3 WhiteBalance(float3 LinearColor, float InWhiteTemp, float InWhiteTint)
{
    float2 SrcWhiteDaylight = D_IlluminantChromaticity(InWhiteTemp);
    float2 SrcWhitePlankian = PlanckianLocusChromaticity(InWhiteTemp);

    // Use Planckian below 4000K, D-illuminant above
    // @source UE5 TonemapCommon.ush:591
    float2 SrcWhite = InWhiteTemp < 4000 ? SrcWhitePlankian : SrcWhiteDaylight;
    // @source UE5 TonemapCommon.ush:592
    float2 D65White = float2(0.31270, 0.32900);

    // Offset along isotherm for tint control
    // @source UE5 TonemapCommon.ush:595-598
    {
        float2 Isothermal = PlanckianIsothermal(InWhiteTemp, InWhiteTint) - SrcWhitePlankian;
        SrcWhite += Isothermal;
    }

    // bIsTemperatureWhiteBalance = true path (default mode)
    // @source UE5 TonemapCommon.ush:600-605
    // In temperature mode, we adapt FROM the source white TO D65.
    // (In non-temperature mode, the direction is reversed — not implemented here.)

    // Build Bradford chromatic adaptation matrix in working color space
    // @source UE5 TonemapCommon.ush:607-608
    // WCS_2_XYZ = sRGB_2_XYZ (since bIsSRGB), XYZ_2_WCS = XYZ_2_sRGB
    float3x3 WhiteBalanceMat = ChromaticAdaptation(SrcWhite, D65White);
    WhiteBalanceMat = mul(ACES_XYZ_2_sRGB_MAT, mul(WhiteBalanceMat, ACES_sRGB_2_XYZ_MAT));

    return mul(WhiteBalanceMat, LinearColor);
}

// ─────────────────────────────────────────────────────────────────────────────
// Nuke-style Color Correction (per-zone: shadows, midtones, highlights)
// ─────────────────────────────────────────────────────────────────────────────

/// Single-zone color correction: saturation, contrast, gamma, gain, offset.
/// Applied in AP1 color space.
///
/// @source UE5 PostProcessCombineLUTs.usf:88-102 — ColorCorrect
/// @constants pivot = 0.18 (implicit from 1.0/0.18 at line 98)
/// @deviations NONE
float3 ColorCorrect(float3 WorkingColor,
    float4 InColorSaturation,
    float4 InColorContrast,
    float4 InColorGamma,
    float4 InColorGain,
    float4 InColorOffset)
{
    // Compute luminance in AP1 for saturation control
    // @source UE5 PostProcessCombineLUTs.usf:96
    float Luma = dot(WorkingColor, ACES_AP1_RGB2Y);

    // Saturation: lerp toward monochrome (per-channel .xyz * global .w)
    // @source UE5 PostProcessCombineLUTs.usf:97
    WorkingColor = max(0, lerp(Luma.xxx, WorkingColor, InColorSaturation.xyz * InColorSaturation.w));

    // Contrast: pivot at 0.18 (18% grey), raise to power
    // @source UE5 PostProcessCombineLUTs.usf:98
    WorkingColor = pow(WorkingColor * (1.0 / 0.18), InColorContrast.xyz * InColorContrast.w) * 0.18;

    // Gamma: inverse power
    // @source UE5 PostProcessCombineLUTs.usf:99
    WorkingColor = pow(WorkingColor, 1.0 / (InColorGamma.xyz * InColorGamma.w));

    // Gain + Offset: linear scale and shift
    // @source UE5 PostProcessCombineLUTs.usf:100
    WorkingColor = WorkingColor * (InColorGain.xyz * InColorGain.w) + (InColorOffset.xyz + InColorOffset.w);

    return WorkingColor;
}

/// Nuke-style color correction with shadow/midtone/highlight zones.
/// Each zone has independent corrections weighted by luminance.
///
/// @source UE5 PostProcessCombineLUTs.usf:105-140 — ColorCorrectAll
/// @deviations NONE
float3 ColorCorrectAll(float3 WorkingColor)
{
    // Compute luminance for zone weighting
    // @source UE5 PostProcessCombineLUTs.usf:107
    float Luma = dot(WorkingColor, ACES_AP1_RGB2Y);

    // ── Shadow zone ──────────────────────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:110-116
    // Per-zone params are multiplied by global params (composited)
    float3 CCColorShadows = ColorCorrect(WorkingColor,
        ColorSaturationShadows * ColorSaturation,
        ColorContrastShadows * ColorContrast,
        ColorGammaShadows * ColorGamma,
        ColorGainShadows * ColorGain,
        ColorOffsetShadows + ColorOffset);
    float CCWeightShadows = 1 - smoothstep(0, ColorCorrectionShadowsMax, Luma);

    // ── Highlight zone ───────────────────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:119-125
    float3 CCColorHighlights = ColorCorrect(WorkingColor,
        ColorSaturationHighlights * ColorSaturation,
        ColorContrastHighlights * ColorContrast,
        ColorGammaHighlights * ColorGamma,
        ColorGainHighlights * ColorGain,
        ColorOffsetHighlights + ColorOffset);
    float CCWeightHighlights = smoothstep(ColorCorrectionHighlightsMin, ColorCorrectionHighlightsMax, Luma);

    // ── Midtone zone ─────────────────────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:128-134
    float3 CCColorMidtones = ColorCorrect(WorkingColor,
        ColorSaturationMidtones * ColorSaturation,
        ColorContrastMidtones * ColorContrast,
        ColorGammaMidtones * ColorGamma,
        ColorGainMidtones * ColorGain,
        ColorOffsetMidtones + ColorOffset);
    float CCWeightMidtones = 1 - CCWeightShadows - CCWeightHighlights;

    // Blend all three zones
    // @source UE5 PostProcessCombineLUTs.usf:137
    float3 WorkingColorSMH = CCColorShadows * CCWeightShadows +
                              CCColorMidtones * CCWeightMidtones +
                              CCColorHighlights * CCWeightHighlights;

    return WorkingColorSMH;
}

// ─────────────────────────────────────────────────────────────────────────────
// CombineLUTsCommon — the main LUT generation pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Main LUT generation function. Matches UE5 CombineLUTsCommon exactly
/// for sRGB SDR output with BLENDCOUNT=1 (no external LUT blending).
///
/// @source UE5 PostProcessCombineLUTs.usf:159-423 — CombineLUTsCommon
/// @deviations See file header for list of unsupported paths.
float4 CombineLUTsCommon(float2 InUV, uint InLayerIndex)
{
    // ── Construct neutral color from 3D volume position ──────────────────
    // @source UE5 PostProcessCombineLUTs.usf:162-168 — USE_VOLUME_LUT=1 path
    float4 Neutral;
    {
        float2 UV = InUV - float2(0.5f / LUTSize, 0.5f / LUTSize);
        Neutral = float4(UV * LUTSize / (LUTSize - 1), InLayerIndex / (LUTSize - 1), 0);
    }

    float4 OutColor = 0;

    // ── Precomputed gamut mapping matrices ────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:192-196
    // AP1_2_sRGB, AP0_2_AP1, AP1_2_AP0 — used throughout the pipeline
    // AP1_2_Output = AP1_2_sRGB for sRGB output (OutputGamut=0)
    const float3x3 AP1_2_Output = ACES_AP1_2_sRGB;

    float3 LUTEncodedColor = Neutral.rgb;
    float3 LinearColor;

    // ── Decode log values to linear ──────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:212
    // For sRGB output (not HDR ST2084), decode from log encoding
    LinearColor = LogToLin(LUTEncodedColor) - LogToLin(0);

    float3 BalancedColor = LinearColor;

    // ── White balance ────────────────────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:218-221
    // @constants DefaultTemperature=6500.0 (PostProcessCombineLUTs.usf:84),
    //            DefaultTint=0.0 (PostProcessCombineLUTs.usf:85),
    //            Epsilon=1e-8 (PostProcessCombineLUTs.usf:86)
    static const float DefaultTemperature = 6500.0;
    static const float DefaultTint = 0.0;
    static const float Epsilon = 1e-8;

    if (abs(WhiteTemp - DefaultTemperature) > Epsilon || abs(WhiteTint - DefaultTint) > Epsilon)
    {
        BalancedColor = WhiteBalance(LinearColor, WhiteTemp, WhiteTint);
    }

    // ── Convert to AP1 (ACEScg) working space ────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:223
    // WorkingColorSpace.ToAP1 = sRGB_2_AP1 when bIsSRGB=true
    float3 ColorAP1 = mul(ACES_sRGB_2_AP1, BalancedColor);

    // ── Expand gamut (fake wide gamut for saturated brights) ─────────────
    // @source UE5 PostProcessCombineLUTs.usf:226-249
    float LumaAP1 = dot(ColorAP1, ACES_AP1_RGB2Y);
    float3 ChromaAP1 = ColorAP1 / LumaAP1;

    float ChromaDistSqr = dot(ChromaAP1 - 1, ChromaAP1 - 1);
    float ExpandAmount = (1 - exp2(-4 * ChromaDistSqr)) * (1 - exp2(-4 * ExpandGamut * LumaAP1 * LumaAP1));

    // Bizarre matrix that expands sRGB to between P3 and AP1
    // CIE 1931 chromaticities: Red(0.6965, 0.3065), Green(0.245, 0.718), Blue(0.1302, 0.0456), White(0.3127, 0.329)
    // @source UE5 PostProcessCombineLUTs.usf:238-243
    const float3x3 Wide_2_XYZ_MAT =
    {
        0.5441691,  0.2395926,  0.1666943,
        0.2394656,  0.7021530,  0.0583814,
       -0.0023439,  0.0361834,  1.0552183,
    };

    // @source UE5 PostProcessCombineLUTs.usf:245-246
    const float3x3 Wide_2_AP1 = mul(ACES_XYZ_2_AP1_MAT, Wide_2_XYZ_MAT);
    const float3x3 ExpandMat = mul(Wide_2_AP1, ACES_AP1_2_sRGB);

    float3 ColorExpand = mul(ExpandMat, ColorAP1);
    // @source UE5 PostProcessCombineLUTs.usf:249
    ColorAP1 = lerp(ColorAP1, ColorExpand, ExpandAmount);

    // ── Nuke-style color correction (shadow/midtone/highlight) ───────────
    // @source UE5 PostProcessCombineLUTs.usf:251
    ColorAP1 = ColorCorrectAll(ColorAP1);

    // Store for Linear HDR output without tone curve (not used for sRGB output)
    // @source UE5 PostProcessCombineLUTs.usf:254
    // float3 GradedColor = mul(WorkingColorSpace.FromAP1, ColorAP1);
    // (Omitted — only needed for HDR output paths)

    // ── Blue correction ──────────────────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:256-269
    const float3x3 BlueCorrectMat =
    {
        0.9404372683, -0.0183068787, 0.0778696104,
        0.0083786969,  0.8286599939, 0.1629613092,
        0.0005471261, -0.0008833746, 1.0003362486
    };
    const float3x3 BlueCorrectInvMat =
    {
        1.06318,     0.0233956, -0.0865726,
       -0.0106337,   1.20632,   -0.19569,
       -0.000590887, 0.00105248, 0.999538
    };
    // @source UE5 PostProcessCombineLUTs.usf:268-269
    const float3x3 BlueCorrectAP1    = mul(ACES_AP0_2_AP1_MAT, mul(BlueCorrectMat,    ACES_AP1_2_AP0_MAT));
    const float3x3 BlueCorrectInvAP1 = mul(ACES_AP0_2_AP1_MAT, mul(BlueCorrectInvMat, ACES_AP1_2_AP0_MAT));

    // Apply blue correction before tonemapping
    // @source UE5 PostProcessCombineLUTs.usf:272
    ColorAP1 = lerp(ColorAP1, mul(BlueCorrectAP1, ColorAP1), BlueCorrection);

    // ── FilmToneMap ──────────────────────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:275-276
    float3 ToneMappedColorAP1 = FilmToneMap(ColorAP1, FilmSlope, FilmToe, FilmShoulder, FilmBlackClip, FilmWhiteClip);
    ColorAP1 = lerp(ColorAP1, ToneMappedColorAP1, ToneCurveAmount);

    // Uncorrect blue to maintain white point
    // @source UE5 PostProcessCombineLUTs.usf:279
    ColorAP1 = lerp(ColorAP1, mul(BlueCorrectInvAP1, ColorAP1), BlueCorrection);

    // ── Convert from AP1 to working color space, clip out-of-gamut ───────
    // @source UE5 PostProcessCombineLUTs.usf:282
    // WorkingColorSpace.FromAP1 is identity when bIsSRGB? No — it's AP1_2_sRGB.
    // But for the sRGB output path (line 337-348), it converts AGAIN with AP1_2_Output.
    // Actually, looking at UE5 more carefully: bIsSRGB workaround at line 344 means
    // for sRGB working space, FilmColor stays as-is (no double transform).
    // So we need FilmColor in sRGB space = mul(AP1_2_sRGB, ColorAP1).
    float3 FilmColor = max(0, mul(ACES_AP1_2_sRGB, ColorAP1));

    // ── ColorCorrection polynomial (r.Color.Min/Mid/Max) ─────────────────
    // @source UE5 PostProcessCombineLUTs.usf:320
    // @source UE5 PostProcessCommon.ush:132-135 — ColorCorrection
    // Default MappingPolynomial = (0, 1, 0) → identity mapping
    FilmColor = MappingPolynomial.x * (FilmColor * FilmColor) + MappingPolynomial.y * FilmColor + MappingPolynomial.z;

    // ── Apply color scale + overlay (Fade track in Cinematics) ───────────
    // @source UE5 PostProcessCombineLUTs.usf:323
    float3 FilmColorNoGamma = lerp(FilmColor * ColorScale, OverlayColor.rgb, OverlayColor.a);

    // ── Apply "gamma" curve adjustment ───────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:329
    // InverseGamma.y is typically 1.0 for sRGB output (sRGB conversion handles gamma)
    FilmColor = pow(max(0, FilmColorNoGamma), InverseGammaY);

    // ── sRGB output encoding ─────────────────────────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:337-348 — TONEMAPPER_OUTPUT_sRGB path
    // For sRGB output with sRGB working space (bIsSRGB=true), the FIXME workaround
    // at line 344 means OutputGamutColor = FilmColor directly (no extra gamut transform).
    float3 OutDeviceColor = LinearToSrgb(FilmColor);

    // ── Final LUT output with 1.05 normalization ─────────────────────────
    // @source UE5 PostProcessCombineLUTs.usf:419
    // UE5 divides by 1.05 to leave headroom for out-of-gamut values.
    // The tonemap shader multiplies by 1.05 when sampling the LUT.
    OutColor.rgb = OutDeviceColor / 1.05;
    OutColor.a = 0;

    return OutColor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 PostProcessCombineLUTs.usf:445-455 — MainCS (USE_VOLUME_LUT=1)
/// @source UE5 PostProcessCombineLUTs.cpp:322 — GroupSize = 8
/// @deviations NONE — same dispatch pattern as UE5 (8x8x8 thread groups for 32x32x32 = 4x4x4 groups)
[numthreads(8, 8, 8)]
void main(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    // Compute UV from thread ID — maps [0, LUTSize) to [0.5/LUTSize, (LUTSize-0.5)/LUTSize]
    // @source UE5 PostProcessCombineLUTs.usf:448
    float2 UV = ((float2)DispatchThreadId.xy + 0.5f) * OutputExtentInverse;
    uint LayerIndex = DispatchThreadId.z;

    // Generate the LUT color for this texel
    // @source UE5 PostProcessCombineLUTs.usf:450
    float4 OutColor = CombineLUTsCommon(UV, LayerIndex);

    // Write to 3D UAV
    // @source UE5 PostProcessCombineLUTs.usf:453-454
    uint3 PixelPos = DispatchThreadId;
    RWOutputTexture[PixelPos] = OutColor;
}
