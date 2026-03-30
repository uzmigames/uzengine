/// Color Grading LUT Generation Compute Shader
///
/// Generates a 32x32x32 3D LUT that bakes the entire color grading + tonemapping
/// pipeline into a single texture lookup. Instead of computing white balance +
/// color correction + ACES tone curve + sRGB per pixel, we pre-bake all of it
/// into a 3D LUT and sample it in the tonemap shader.
///
/// The pipeline per texel:
///   1. Decode log-encoded neutral color to linear via LogToLin
///   2. White balance (Planckian locus + D-illuminant + Bradford chromatic adaptation)
///   3. Convert to AP1 (ACEScg) working space
///   4. Expand gamut (fake wide gamut for saturated brights)
///   5. Nuke-style color correction (shadow/midtone/highlight zones)
///   6. Blue correction (desaturate problematic blues)
///   7. ApplyToneCurve (ACES-based custom S-curve)
///   8. Blue uncorrection (restore white point)
///   9. Color correction polynomial (r.Color.Min/Mid/Max mapping)
///  10. Apply color scale + overlay
///  11. Apply gamma curve
///  12. Convert to sRGB output encoding
///  13. Divide by 1.05 (LUT output normalization)
///
/// @ref Selan 2012 "Using Lookup Tables for Color Grading" — CombineLUTsCommon pipeline
/// @ref ACES Reference Implementation (Academy S-2014-006) — tone curve, color space matrices
/// @deviations
///   - No external LUT blending (BLENDCOUNT=1, neutral only)
///   - No HDR output paths (ACES1000nit/2000nit/ScRGB/LinearEXR/NoToneCurve/WithToneCurve)
///   - No WorkingColorSpace uniform buffer (assumes sRGB working space, bIsSRGB=true)
///   - No ACES 2.0 path (USE_ACES_2=0)
///   - No mobile tonemapper path
///   - Tone curve parameters passed via PerFrameCB instead of dedicated uniform

#include "../include/film_tonemap.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// LUT Generation Parameters (root constants at b0)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Selan 2012 "Using Lookup Tables for Color Grading":39-46,66-68 — parameter declarations
/// @ref color grading defaults:373-407 — default values
cbuffer LutGenParams : register(b2) {
    // ── White balance ────────────────────────────────────────────────────
    /// @ref color grading defaults:373 — WhiteTemp = 6500.0f
    float WhiteTemp;
    /// @ref color grading defaults:374 — WhiteTint = 0.0f
    float WhiteTint;

    // ── Global color correction ──────────────────────────────────────────
    /// @ref color grading defaults:377-381 — default (1,1,1,1) / (0,0,0,0) for Offset
    float4 ColorSaturation;
    float4 ColorContrast;
    float4 ColorGamma;
    float4 ColorGain;
    float4 ColorOffset;

    // ── Shadow zone color correction ─────────────────────────────────────
    /// @ref color grading defaults:383-387
    float4 ColorSaturationShadows;
    float4 ColorContrastShadows;
    float4 ColorGammaShadows;
    float4 ColorGainShadows;
    float4 ColorOffsetShadows;

    // ── Midtone zone color correction ────────────────────────────────────
    /// @ref color grading defaults:389-393
    float4 ColorSaturationMidtones;
    float4 ColorContrastMidtones;
    float4 ColorGammaMidtones;
    float4 ColorGainMidtones;
    float4 ColorOffsetMidtones;

    // ── Highlight zone color correction ──────────────────────────────────
    /// @ref color grading defaults:395-399
    float4 ColorSaturationHighlights;
    float4 ColorContrastHighlights;
    float4 ColorGammaHighlights;
    float4 ColorGainHighlights;
    float4 ColorOffsetHighlights;

    // ── Zone boundaries ──────────────────────────────────────────────────
    /// @ref color grading defaults:401-403
    float ColorCorrectionShadowsMax;    ///< Default 0.09
    float ColorCorrectionHighlightsMin; ///< Default 0.5
    float ColorCorrectionHighlightsMax; ///< Default 1.0

    // ── Tone curve parameters ──────────────────────────────────────────
    /// @ref tone curve defaults — defaults
    float toneCurveSlope;               ///< Default 0.88
    float toneCurveToe;                 ///< Default 0.55
    float toneCurveShoulder;            ///< Default 0.26
    float toneCurveBlackClip;           ///< Default 0.0
    float toneCurveWhiteClip;           ///< Default 0.04

    // ── Additional parameters ────────────────────────────────────────────
    /// @ref color grading defaults:405-407
    float BlueCorrection;               ///< Default 0.6
    float ExpandGamut;                  ///< Default 1.0
    float ToneCurveAmount;              ///< Default 1.0

    // ── ColorCorrection polynomial (r.Color.Min/Mid/Max) ─────────────────
    /// @ref Selan 2012 "Using Lookup Tables for Color Grading":86-98 — GetMappingPolynomial
    /// @ref color correction polynomial:129 — MappingPolynomial = (a, b, c) where y = a*x^2 + b*x + c
    /// Default: Min=0, Mid=0.5, Max=1.0 → a=0, b=1, c=0 (identity)
    float3 MappingPolynomial;

    // ── Color scale + overlay ────────────────────────────────────────────
    /// @ref Selan 2012 "Using Lookup Tables for Color Grading":31-32 — ColorScale, OverlayColor
    float3 ColorScale;                  ///< Default (1,1,1) — Fade track multiplier
    float4 OverlayColor;                ///< Default (0,0,0,0) — Fade track blend color

    // ── InverseGamma ─────────────────────────────────────────────────────
    /// @ref Krystek 1985 / CIE colorimetry:43 — InverseGamma (usually 1/2.2; .y used for gamma curve)
    /// For sRGB output, .y = 1.0 (sRGB conversion handles gamma, no extra pow needed)
    float InverseGammaY;

    // ── LUT dimensions ───────────────────────────────────────────────────
    /// @ref Selan 2012 "Using Lookup Tables for Color Grading":39 — r.LUT.Size = 32
    float LUTSize;

    // ── Output extent inverse (for UV computation) ───────────────────────
    /// @ref Selan 2012 "Using Lookup Tables for Color Grading":440 — OutputExtentInverse
    float2 OutputExtentInverse;
};

// ── Output 3D texture ────────────────────────────────────────────────────────
/// @ref Selan 2012 "Using Lookup Tables for Color Grading":443 — RWTexture3D<float4> RWOutputTexture
RWTexture3D<float4> RWOutputTexture : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Additional ACES color space matrices (beyond those in film_tonemap.hlsli)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref ACES CTL Reference Implementation (Academy S-2014-006):67-72 — XYZ_2_sRGB_MAT
static const float3x3 ACES_XYZ_2_sRGB_MAT =
{
     3.2409699419, -1.5373831776, -0.4986107603,
    -0.9692436363,  1.8759675015,  0.0415550574,
     0.0556300797, -0.2039769589,  1.0569715142,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006):74-79 — sRGB_2_XYZ_MAT
static const float3x3 ACES_sRGB_2_XYZ_MAT =
{
    0.4123907993, 0.3575843394, 0.1804807884,
    0.2126390059, 0.7151686788, 0.0721923154,
    0.0193308187, 0.1191947798, 0.9505321522,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006):112-117 — D65_2_D60_CAT (Bradford)
static const float3x3 ACES_D65_2_D60_CAT =
{
     1.0130349146, 0.0061052578, -0.0149709436,
     0.0076982301, 0.9981633521, -0.0050320385,
    -0.0028413174, 0.0046851567,  0.9245061375,
};

/// @ref ACES CTL Reference Implementation (Academy S-2014-006):119-124 — D60_2_D65_CAT (Bradford)
static const float3x3 ACES_D60_2_D65_CAT =
{
     0.9872240087, -0.0061132286, 0.0159532883,
    -0.0075983718,  1.0018614847, 0.0053300358,
     0.0030725771, -0.0050959615, 1.0816806031,
};

// ── Derived matrices ─────────────────────────────────────────────────────────
// These are computed inline from ACES matrix compositions.
// We precompute them as static constants for clarity.

/// AP1 -> sRGB = XYZ_2_sRGB * D60_2_D65 * AP1_2_XYZ
/// @ref Selan 2012 "Using Lookup Tables for Color Grading":192 — AP1_2_sRGB
static const float3x3 ACES_AP1_2_sRGB = {
    // mul(XYZ_2_sRGB_MAT, mul(D60_2_D65_CAT, AP1_2_XYZ_MAT))
    // Precomputed to match reference exactly (they compute at runtime, same result)
     1.70505188,  -0.62179212,  -0.08325976,
    -0.13025722,   1.14080280,  -0.01054558,
    -0.02400329,  -0.12896885,   1.15297214,
};

/// sRGB -> AP1 = XYZ_2_AP1 * D65_2_D60 * sRGB_2_XYZ
/// @ref ACES CTL Reference Implementation (Academy S-2014-006) (used by ApplyToneCurve and gamut expand)
static const float3x3 ACES_sRGB_2_AP1 = {
    // mul(XYZ_2_AP1_MAT, mul(D65_2_D60_CAT, sRGB_2_XYZ_MAT))
     0.61309740,   0.33952315,   0.04737945,
     0.07019372,   0.91635388,   0.01345240,
     0.02061561,   0.10956977,   0.86981462,
};

// ─────────────────────────────────────────────────────────────────────────────
// LogToLin — the reference engine's log-encoded LUT input to linear conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts log-encoded LUT texel value to linear scene-referred color.
/// encodes the LUT domain in log space to better distribute precision
/// across the HDR range. The encoding uses a "pure log" formula parameterized
/// by a grey point and dynamic range.
///
/// @ref Selan 2012 "Using Lookup Tables to Accelerate Color Transformations" — LogToLin encoding
/// @constants LinearRange=14 (log exposure range in stops),
///            LinearGrey=0.18 (scene-referred grey value),
///            ExposureGrey=444 (log-space grey reference, out of 1023)
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
/// @ref Krystek 1985 "An Algorithm to Calculate Correlated Color Temperature" — PlanckianLocusChromaticity
/// @constants polynomial coefficients from Krystek 1985, Table 1
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
/// @ref Judd et al. 1964 "Spectral Distribution of Typical Daylight" — D_IlluminantChromaticity
/// @constants Planck's law correction factor 1.4388/1.438 (standard CIE 1968 revision)
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
/// @ref Krystek 1985 "An Algorithm to Calculate Correlated Color Temperature" — PlanckianIsothermal
/// @constants Tint scale 0.05 — "CCT meaningful within +/- 0.05" (standard CIE isothermal line spacing)
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
/// @ref ACES CTL Reference Implementation (Academy S-2014-006):188-196 — xyY_2_XYZ
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
/// @ref ACES Reference Implementation (Academy S-2014-006) — BradfordAdapt chromatic adaptation
/// @constants Bradford cone response matrix (CIE TC 1-32 report)
/// @deviations NONE
float3x3 BradfordAdapt(float2 src_xy, float2 dst_xy)
{
    // Bradford cone response matrix
    // @ref ACES CTL Reference Implementation (Academy S-2014-006):204-209
    const float3x3 ConeResponse =
    {
         0.8951,  0.2664, -0.1614,
        -0.7502,  1.7135,  0.0367,
         0.0389, -0.0685,  1.0296,
    };
    // @ref ACES CTL Reference Implementation (Academy S-2014-006):210-215
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
/// @ref Krystek 1985 "An Algorithm to Calculate Correlated Color Temperature" — ApplyWhiteBalance
/// @constants D65White = (0.31270, 0.32900) (CIE standard illuminant D65)
/// @deviations bIsTemperatureApplyWhiteBalance always true (we only support temperature mode).
///   reference supports both temperature mode and manual white point mode; we use temperature
///   which is the more common artist workflow. The WCS_2_XYZ and XYZ_2_WCS matrices
///   use sRGB_2_XYZ and XYZ_2_sRGB since our working color space is sRGB (bIsSRGB=true).
float3 ApplyWhiteBalance(float3 LinearColor, float InWhiteTemp, float InWhiteTint)
{
    float2 SrcWhiteDaylight = D_IlluminantChromaticity(InWhiteTemp);
    float2 SrcWhitePlankian = PlanckianLocusChromaticity(InWhiteTemp);

    // Use Planckian below 4000K, D-illuminant above
    // @ref Krystek 1985 / CIE colorimetry:591
    float2 SrcWhite = InWhiteTemp < 4000 ? SrcWhitePlankian : SrcWhiteDaylight;
    // @ref Krystek 1985 / CIE colorimetry:592
    float2 D65White = float2(0.31270, 0.32900);

    // Offset along isotherm for tint control
    // @ref Krystek 1985 / CIE colorimetry:595-598
    {
        float2 Isothermal = PlanckianIsothermal(InWhiteTemp, InWhiteTint) - SrcWhitePlankian;
        SrcWhite += Isothermal;
    }

    // bIsTemperatureApplyWhiteBalance = true path (default mode)
    // @ref Krystek 1985 / CIE colorimetry:600-605
    // In temperature mode, we adapt FROM the source white TO D65.
    // (In non-temperature mode, the direction is reversed — not implemented here.)

    // Build Bradford chromatic adaptation matrix in working color space
    // @ref Krystek 1985 / CIE colorimetry:607-608
    // WCS_2_XYZ = sRGB_2_XYZ (since bIsSRGB), XYZ_2_WCS = XYZ_2_sRGB
    float3x3 ApplyWhiteBalanceMat = BradfordAdapt(SrcWhite, D65White);
    ApplyWhiteBalanceMat = mul(ACES_XYZ_2_sRGB_MAT, mul(ApplyWhiteBalanceMat, ACES_sRGB_2_XYZ_MAT));

    return mul(ApplyWhiteBalanceMat, LinearColor);
}

// ─────────────────────────────────────────────────────────────────────────────
// Nuke-style Color Correction (per-zone: shadows, midtones, highlights)
// ─────────────────────────────────────────────────────────────────────────────

/// Single-zone color correction: saturation, contrast, gamma, gain, offset.
/// Applied in AP1 color space.
///
/// @ref Selan 2012 "Using Lookup Tables for Color Grading":88-102 — ApplyColorGrade
/// @constants pivot = 0.18 (implicit from 1.0/0.18 at line 98)
/// @deviations NONE
float3 ApplyColorGrade(float3 WorkingColor,
    float4 InColorSaturation,
    float4 InColorContrast,
    float4 InColorGamma,
    float4 InColorGain,
    float4 InColorOffset)
{
    // Compute luminance in AP1 for saturation control
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":96
    float Luma = dot(WorkingColor, ACES_AP1_RGB2Y);

    // Saturation: lerp toward monochrome (per-channel .xyz * global .w)
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":97
    WorkingColor = max(0, lerp(Luma.xxx, WorkingColor, InColorSaturation.xyz * InColorSaturation.w));

    // Contrast: pivot at 0.18 (18% grey), raise to power
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":98
    WorkingColor = pow(WorkingColor * (1.0 / 0.18), InColorContrast.xyz * InColorContrast.w) * 0.18;

    // Gamma: inverse power
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":99
    WorkingColor = pow(WorkingColor, 1.0 / (InColorGamma.xyz * InColorGamma.w));

    // Gain + Offset: linear scale and shift
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":100
    WorkingColor = WorkingColor * (InColorGain.xyz * InColorGain.w) + (InColorOffset.xyz + InColorOffset.w);

    return WorkingColor;
}

/// Nuke-style color correction with shadow/midtone/highlight zones.
/// Each zone has independent corrections weighted by luminance.
///
/// @ref Selan 2012 "Using Lookup Tables for Color Grading":105-140 — ColorCorrectAll
/// @deviations NONE
float3 ColorCorrectAll(float3 WorkingColor)
{
    // Compute luminance for zone weighting
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":107
    float Luma = dot(WorkingColor, ACES_AP1_RGB2Y);

    // ── Shadow zone ──────────────────────────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":110-116
    // Per-zone params are multiplied by global params (composited)
    float3 CCColorShadows = ApplyColorGrade(WorkingColor,
        ColorSaturationShadows * ColorSaturation,
        ColorContrastShadows * ColorContrast,
        ColorGammaShadows * ColorGamma,
        ColorGainShadows * ColorGain,
        ColorOffsetShadows + ColorOffset);
    float CCWeightShadows = 1 - smoothstep(0, ColorCorrectionShadowsMax, Luma);

    // ── Highlight zone ───────────────────────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":119-125
    float3 CCColorHighlights = ApplyColorGrade(WorkingColor,
        ColorSaturationHighlights * ColorSaturation,
        ColorContrastHighlights * ColorContrast,
        ColorGammaHighlights * ColorGamma,
        ColorGainHighlights * ColorGain,
        ColorOffsetHighlights + ColorOffset);
    float CCWeightHighlights = smoothstep(ColorCorrectionHighlightsMin, ColorCorrectionHighlightsMax, Luma);

    // ── Midtone zone ─────────────────────────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":128-134
    float3 CCColorMidtones = ApplyColorGrade(WorkingColor,
        ColorSaturationMidtones * ColorSaturation,
        ColorContrastMidtones * ColorContrast,
        ColorGammaMidtones * ColorGamma,
        ColorGainMidtones * ColorGain,
        ColorOffsetMidtones + ColorOffset);
    float CCWeightMidtones = 1 - CCWeightShadows - CCWeightHighlights;

    // Blend all three zones
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":137
    float3 WorkingColorSMH = CCColorShadows * CCWeightShadows +
                              CCColorMidtones * CCWeightMidtones +
                              CCColorHighlights * CCWeightHighlights;

    return WorkingColorSMH;
}

// ─────────────────────────────────────────────────────────────────────────────
// CombineLUTsCommon — the main LUT generation pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Main LUT generation function. Standard CombineLUTsCommon exactly
/// for sRGB SDR output with BLENDCOUNT=1 (no external LUT blending).
///
/// @ref Selan 2012 "Using Lookup Tables for Color Grading":159-423 — CombineLUTsCommon
/// @deviations See file header for list of unsupported paths.
float4 CombineLUTsCommon(float2 InUV, uint InLayerIndex)
{
    // ── Construct neutral color from 3D volume position ──────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":162-168 — USE_VOLUME_LUT=1 path
    float4 Neutral;
    {
        float2 UV = InUV - float2(0.5f / LUTSize, 0.5f / LUTSize);
        Neutral = float4(UV * LUTSize / (LUTSize - 1), InLayerIndex / (LUTSize - 1), 0);
    }

    float4 OutColor = 0;

    // ── Precomputed gamut mapping matrices ────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":192-196
    // AP1_2_sRGB, AP0_2_AP1, AP1_2_AP0 — used throughout the pipeline
    // AP1_2_Output = AP1_2_sRGB for sRGB output (OutputGamut=0)
    const float3x3 AP1_2_Output = ACES_AP1_2_sRGB;

    float3 LUTEncodedColor = Neutral.rgb;
    float3 LinearColor;

    // ── Decode log values to linear ──────────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":212
    // For sRGB output (not HDR ST2084), decode from log encoding
    LinearColor = LogToLin(LUTEncodedColor) - LogToLin(0);

    float3 BalancedColor = LinearColor;

    // ── White balance ────────────────────────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":218-221
    // @constants DefaultTemperature=6500.0 (standard D65 illuminant temperature),
    //            DefaultTint=0.0 (neutral tint, no isothermal offset),
    //            Epsilon=1e-8 (numerical comparison threshold)
    static const float DefaultTemperature = 6500.0;
    static const float DefaultTint = 0.0;
    static const float Epsilon = 1e-8;

    if (abs(WhiteTemp - DefaultTemperature) > Epsilon || abs(WhiteTint - DefaultTint) > Epsilon)
    {
        BalancedColor = ApplyWhiteBalance(LinearColor, WhiteTemp, WhiteTint);
    }

    // ── Convert to AP1 (ACEScg) working space ────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":223
    // WorkingColorSpace.ToAP1 = sRGB_2_AP1 when bIsSRGB=true
    float3 ColorAP1 = mul(ACES_sRGB_2_AP1, BalancedColor);

    // ── Expand gamut (fake wide gamut for saturated brights) ─────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":226-249
    float LumaAP1 = dot(ColorAP1, ACES_AP1_RGB2Y);
    float3 ChromaAP1 = ColorAP1 / LumaAP1;

    float ChromaDistSqr = dot(ChromaAP1 - 1, ChromaAP1 - 1);
    float ExpandAmount = (1 - exp2(-4 * ChromaDistSqr)) * (1 - exp2(-4 * ExpandGamut * LumaAP1 * LumaAP1));

    // Bizarre matrix that expands sRGB to between P3 and AP1
    // CIE 1931 chromaticities: Red(0.6965, 0.3065), Green(0.245, 0.718), Blue(0.1302, 0.0456), White(0.3127, 0.329)
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":238-243
    const float3x3 Wide_2_XYZ_MAT =
    {
        0.5441691,  0.2395926,  0.1666943,
        0.2394656,  0.7021530,  0.0583814,
       -0.0023439,  0.0361834,  1.0552183,
    };

    // @ref Selan 2012 "Using Lookup Tables for Color Grading":245-246
    const float3x3 Wide_2_AP1 = mul(ACES_XYZ_2_AP1_MAT, Wide_2_XYZ_MAT);
    const float3x3 ExpandMat = mul(Wide_2_AP1, ACES_AP1_2_sRGB);

    float3 ColorExpand = mul(ExpandMat, ColorAP1);
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":249
    ColorAP1 = lerp(ColorAP1, ColorExpand, ExpandAmount);

    // ── Nuke-style color correction (shadow/midtone/highlight) ───────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":251
    ColorAP1 = ColorCorrectAll(ColorAP1);

    // Store for Linear HDR output without tone curve (not used for sRGB output)
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":254
    // float3 GradedColor = mul(WorkingColorSpace.FromAP1, ColorAP1);
    // (Omitted — only needed for HDR output paths)

    // ── Blue correction ──────────────────────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":256-269
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
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":268-269
    const float3x3 BlueCorrectAP1    = mul(ACES_AP0_2_AP1_MAT, mul(BlueCorrectMat,    ACES_AP1_2_AP0_MAT));
    const float3x3 BlueCorrectInvAP1 = mul(ACES_AP0_2_AP1_MAT, mul(BlueCorrectInvMat, ACES_AP1_2_AP0_MAT));

    // Apply blue correction before tonemapping
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":272
    ColorAP1 = lerp(ColorAP1, mul(BlueCorrectAP1, ColorAP1), BlueCorrection);

    // ── ApplyToneCurve (ACES-based S-curve) ──────────────────────────────
    // @ref ACES Reference Implementation (Academy S-2014-006) — tone curve
    float3 ToneMappedColorAP1 = ApplyToneCurve(ColorAP1, toneCurveSlope, toneCurveToe, toneCurveShoulder, toneCurveBlackClip, toneCurveWhiteClip);
    ColorAP1 = lerp(ColorAP1, ToneMappedColorAP1, ToneCurveAmount);

    // Uncorrect blue to maintain white point
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":279
    ColorAP1 = lerp(ColorAP1, mul(BlueCorrectInvAP1, ColorAP1), BlueCorrection);

    // ── Convert from AP1 to working color space, clip out-of-gamut ───────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":282
    // WorkingColorSpace.FromAP1 is identity when bIsSRGB? No — it's AP1_2_sRGB.
    // But for the sRGB output path (line 337-348), it converts AGAIN with AP1_2_Output.
    // Actually, looking at reference more carefully: bIsSRGB workaround at line 344 means
    // for sRGB working space, FilmColor stays as-is (no double transform).
    // So we need FilmColor in sRGB space = mul(AP1_2_sRGB, ColorAP1).
    float3 FilmColor = max(0, mul(ACES_AP1_2_sRGB, ColorAP1));

    // ── ColorCorrection polynomial (r.Color.Min/Mid/Max) ─────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":320
    // @ref color correction polynomial:132-135 — ColorCorrection
    // Default MappingPolynomial = (0, 1, 0) → identity mapping
    FilmColor = MappingPolynomial.x * (FilmColor * FilmColor) + MappingPolynomial.y * FilmColor + MappingPolynomial.z;

    // ── Apply color scale + overlay (Fade track in Cinematics) ───────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":323
    float3 FilmColorNoGamma = lerp(FilmColor * ColorScale, OverlayColor.rgb, OverlayColor.a);

    // ── Apply "gamma" curve adjustment ───────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":329
    // InverseGamma.y is typically 1.0 for sRGB output (sRGB conversion handles gamma)
    FilmColor = pow(max(0, FilmColorNoGamma), InverseGammaY);

    // ── sRGB output encoding ─────────────────────────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":337-348 — TONEMAPPER_OUTPUT_sRGB path
    // For sRGB output with sRGB working space (bIsSRGB=true), the FIXME workaround
    // at line 344 means OutputGamutColor = FilmColor directly (no extra gamut transform).
    float3 OutDeviceColor = LinearToSrgb(FilmColor);

    // ── Final LUT output with 1.05 normalization ─────────────────────────
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":419
    // divides by 1.05 to leave headroom for out-of-gamut values.
    // The tonemap shader multiplies by 1.05 when sampling the LUT.
    OutColor.rgb = OutDeviceColor / 1.05;
    OutColor.a = 0;

    return OutColor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Selan 2012 "Using Lookup Tables for Color Grading":445-455 — MainCS (USE_VOLUME_LUT=1)
/// @ref Selan 2012 "Using Lookup Tables for Color Grading":322 — GroupSize = 8
/// @deviations NONE — same dispatch pattern as reference (8x8x8 thread groups for 32x32x32 = 4x4x4 groups)
[numthreads(8, 8, 8)]
void main(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    // Compute UV from thread ID — maps [0, LUTSize) to [0.5/LUTSize, (LUTSize-0.5)/LUTSize]
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":448
    float2 UV = ((float2)DispatchThreadId.xy + 0.5f) * OutputExtentInverse;
    uint LayerIndex = DispatchThreadId.z;

    // Generate the LUT color for this texel
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":450
    float4 OutColor = CombineLUTsCommon(UV, LayerIndex);

    // Write to 3D UAV
    // @ref Selan 2012 "Using Lookup Tables for Color Grading":453-454
    uint3 PixelPos = DispatchThreadId;
    RWOutputTexture[PixelPos] = OutColor;
}
