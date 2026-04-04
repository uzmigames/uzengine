/// UzEngine common color utilities.
///
/// sRGB transfer functions, luminance, tone mapping operators, and white
/// balance. All functions operate in linear RGB unless noted otherwise.

#ifndef UZ_COMMON_COLOR_HLSLI
#define UZ_COMMON_COLOR_HLSLI

#include "constants.hlsli"

// ─── sRGB transfer functions ──────────────────────────────────────────────

/// Exact sRGB electro-optical transfer function (sRGB -> linear).
/// @ref IEC 61966-2-1:1999 "Multimedia systems and equipment — Colour
///      measurement and management — Part 2-1: Default RGB colour space"
///
/// Applies the piecewise function per component:
///   c <= 0.04045  =>  c / 12.92
///   c >  0.04045  =>  ((c + 0.055) / 1.055)^2.4
float3 SRGBToLinear(float3 srgb)
{
    float3 lo = srgb / 12.92f;
    float3 hi = pow((srgb + 0.055f) / 1.055f, 2.4f);
    return select(srgb <= 0.04045f, lo, hi);
}

/// Exact sRGB opto-electronic transfer function (linear -> sRGB).
/// @ref IEC 61966-2-1:1999
///
/// Applies the piecewise function per component:
///   c <= 0.0031308  =>  c * 12.92
///   c >  0.0031308  =>  1.055 * c^(1/2.4) - 0.055
float3 LinearToSRGB(float3 linearColor)
{
    float3 lo = linearColor * 12.92f;
    float3 hi = 1.055f * pow(linearColor, 1.0f / 2.4f) - 0.055f;
    return select(linearColor <= 0.0031308f, lo, hi);
}

/// Fast sRGB -> linear approximation using gamma 2.2 power curve.
float3 SRGBToLinearFast(float3 srgb)
{
    return pow(max(srgb, 0.0f), 2.2f);
}

/// Fast linear -> sRGB approximation using gamma 1/2.2 power curve.
float3 LinearToSRGBFast(float3 linearColor)
{
    return pow(max(linearColor, 0.0f), 1.0f / 2.2f);
}

// ─── Luminance ────────────────────────────────────────────────────────────

/// Perceptual luminance from linear RGB.
/// @ref ITU-R BT.709 "Parameter values for the HDTV standards"
///
/// Uses the standard BT.709 luminance coefficients:
///   Y = 0.2126 R + 0.7152 G + 0.0722 B
float Luminance(float3 linearColor)
{
    return dot(linearColor, float3(0.2126f, 0.7152f, 0.0722f));
}

// ─── Tone mapping operators ───────────────────────────────────────────────

/// ACES filmic tone mapping curve (fitted approximation).
/// @ref Narkowicz 2015 "ACES Filmic Tone Mapping Curve"
///
/// Simple rational fit to the ACES Reference Rendering Transform (RRT)
/// combined with the Output Device Transform (ODT) for sRGB display.
/// Input and output are both in linear space.
float3 ACESFilmic(float3 x)
{
    float3 a = 2.51f;
    float3 b = 0.03f;
    float3 c = 2.43f;
    float3 d = 0.59f;
    float3 e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

/// ACES filmic tone mapping (Stephen Hill fitted RRT+ODT matrices).
/// @ref Hill 2016 "ACES Fitted Curves"
///
/// Higher-quality fit than Narkowicz — uses input/output 3x3 matrices to
/// approximate the full ACES RRT+ODT pipeline. Better hue preservation in
/// saturated highlights (orange stays orange instead of shifting to yellow).
/// Input is linear HDR; output is linear [0,1] for sRGB display.
float3 ACESHillFit(float3 x)
{
    // sRGB -> AP1 (ACEScg working space) input matrix
    static const float3x3 ACESInputMat = float3x3(
        0.59719f, 0.35458f, 0.04823f,
        0.07600f, 0.90834f, 0.01566f,
        0.02840f, 0.13383f, 0.83777f
    );

    // AP1 -> sRGB output matrix
    static const float3x3 ACESOutputMat = float3x3(
         1.60475f, -0.53108f, -0.07367f,
        -0.10208f,  1.10813f, -0.00605f,
        -0.00327f, -0.07276f,  1.07602f
    );

    x = mul(ACESInputMat, x);

    // RRT+ODT fit (Hill)
    float3 a = x * (x + 0.0245786f) - 0.000090537f;
    float3 b = x * (0.983729f * x + 0.4329510f) + 0.238081f;
    x = a / b;

    return saturate(mul(ACESOutputMat, x));
}

/// Reinhard simple tone mapping: x / (1 + x).
/// @ref Reinhard et al. 2002 "Photographic Tone Reproduction for Digital
///      Images"
float3 ReinhardTonemap(float3 x)
{
    return x / (1.0f + x);
}

/// Extended Reinhard with white point control.
/// @ref Reinhard et al. 2002 "Photographic Tone Reproduction for Digital
///      Images"
///
/// maxWhite controls the luminance level that maps to pure white.
/// When maxWhite -> infinity, this reduces to the simple Reinhard operator.
float3 ReinhardExtended(float3 x, float maxWhite)
{
    float3 numerator = x * (1.0f + x / (maxWhite * maxWhite));
    return numerator / (1.0f + x);
}

/// Hable (Uncharted 2) filmic tone mapping operator.
/// @ref Hable 2010 "Filmic Tonemapping Operators" (GDC)
///
/// Uses the piecewise rational function with shoulder and toe:
///   f(x) = ((x*(A*x+C*B)+D*E) / (x*(A*x+B)+D*F)) - E/F
/// Constants tuned for cinematic look by Hable for Uncharted 2.
float3 HableUncharted2(float3 x)
{
    static const float A = 0.15f; // Shoulder Strength
    static const float B = 0.50f; // Linear Strength
    static const float C = 0.10f; // Linear Angle
    static const float D = 0.20f; // Toe Strength
    static const float E = 0.02f; // Toe Numerator
    static const float F = 0.30f; // Toe Denominator

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// ─── White balance ────────────────────────────────────────────────────────

/// White balance via Bradford chromatic adaptation.
/// @ref Lam 1985 "Metamerism and Colour Constancy" — Bradford transform
/// @ref Lindbloom "Chromatic Adaptation" — Bradford matrix derivation
///
/// temperature: shift in Kelvin from D65 (positive = warmer / yellow,
///              negative = cooler / blue). Typical range [-1.0, 1.0] mapped
///              to meaningful color temperature shift.
/// tint: green-magenta shift. Typical range [-1.0, 1.0].
///
/// Implementation uses a simplified linear blend in LMS space derived from
/// the Bradford transform. The full CIE illuminant computation is expensive;
/// this approximation matches standard grading tools at typical artist ranges.
float3 WhiteBalance(float3 linearColor, float temperature, float tint)
{
    // Approximate D65 -> target illuminant shift in LMS space.
    // These coefficients map the temperature/tint artist controls to
    // per-channel LMS scale factors via the Bradford transform.
    // @ref Wyszecki & Stiles 2000 "Color Science" — CIE daylight locus
    float t1 = temperature * 0.0366f;
    float t2 = tint * 0.0229f;

    // LMS scale factors for the white point shift
    float x = 0.3127f - t1;
    float y = 0.3290f - t2;

    // Bradford transform: RGB -> LMS -> scale -> LMS -> RGB
    // Simplified: compute the diagonal adaptation matrix entries
    // from the xy chromaticity shift relative to D65 (0.3127, 0.3290)
    float X = x / y;
    float Z = (1.0f - x - y) / y;

    // D65 reference white
    static const float D65_X = 0.95047f;
    static const float D65_Z = 1.08883f;

    // Scale factors (source / destination in XYZ, mapped through Bradford)
    float scaleX = X / D65_X;
    float scaleZ = Z / D65_Z;

    // Simplified Bradford: approximate the RGB correction as a diagonal
    // transform in a linearized LMS-like space
    static const float3x3 kBradfordToRGB = float3x3(
         1.0479f, 0.0229f, -0.0502f,
        -0.0296f, 0.9904f, -0.0171f,
        -0.0092f, 0.0150f,  0.7522f
    );
    static const float3x3 kRGBToBradford = float3x3(
         0.8951f, 0.2664f, -0.1614f,
        -0.7502f, 1.7135f,  0.0367f,
         0.0389f, -0.0686f, 1.0296f
    );

    float3 lms = mul(kRGBToBradford, linearColor);
    lms.x *= scaleX;
    lms.z *= scaleZ;
    return mul(kBradfordToRGB, lms);
}

#endif // UZ_COMMON_COLOR_HLSLI
