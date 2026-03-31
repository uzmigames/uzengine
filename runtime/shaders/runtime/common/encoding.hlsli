/// UzEngine common encoding/packing utilities.
///
/// Octahedral normal encoding, UNORM/SNORM packing, R11G11B10F, RGBE
/// shared-exponent encoding, and GBuffer packing helpers.

#ifndef UZ_COMMON_ENCODING_HLSLI
#define UZ_COMMON_ENCODING_HLSLI

#include "constants.hlsli"

// ─── Octahedral normal encoding ───────────────────────────────────────────

/// Encode a unit normal vector to [0,1]^2 octahedral parameterization.
/// @ref Meyer et al. 2010 "On Floating-Point Normal Vectors"
/// @ref Cigolle et al. 2014 "A Survey of Efficient Representations for
///      Independent Unit Vectors"
///
/// Projects the unit sphere onto an octahedron, then unfolds the lower
/// hemisphere by reflecting across the diagonal. Result in [0,1]^2 suitable
/// for UNORM8/UNORM16 storage.
float2 OctEncode(float3 n)
{
    // Project onto octahedron (L1 normalization)
    float2 p = n.xy * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));

    // Reflect lower hemisphere to [0,1]^2
    float2 sign_p = select(p >= 0.0f, float2(1.0f, 1.0f), float2(-1.0f, -1.0f));
    float2 wrapped = (1.0f - abs(p.yx)) * sign_p;
    float2 result = select(n.z >= 0.0f, p, wrapped);

    // Map from [-1,1] to [0,1]
    return result * 0.5f + 0.5f;
}

/// Decode [0,1]^2 octahedral parameterization back to a unit normal.
/// @ref Meyer et al. 2010 "On Floating-Point Normal Vectors"
float3 OctDecode(float2 f)
{
    // Remap from [0,1] to [-1,1]
    f = f * 2.0f - 1.0f;

    // Reconstruct Z from the L1 constraint: |x| + |y| + |z| = 1
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));

    // Unwrap the lower hemisphere
    float2 sign_n = select(n.xy >= 0.0f, float2(1.0f, 1.0f), float2(-1.0f, -1.0f));
    float2 wrapped = (1.0f - abs(n.yx)) * sign_n;
    n.xy = select(n.z >= 0.0f, n.xy, wrapped);

    return normalize(n);
}

// ─── 16-bit packing ──────────────────────────────────────────────────────

/// Pack two [0,1] floats into a single uint (16 bits per component).
uint PackUnorm2x16(float2 v)
{
    uint lo = (uint)(saturate(v.x) * 65535.0f + 0.5f);
    uint hi = (uint)(saturate(v.y) * 65535.0f + 0.5f);
    return lo | (hi << 16u);
}

/// Unpack a uint into two [0,1] floats (16 bits per component).
float2 UnpackUnorm2x16(uint p)
{
    float lo = float(p & 0xFFFFu) / 65535.0f;
    float hi = float(p >> 16u) / 65535.0f;
    return float2(lo, hi);
}

/// Pack two [-1,1] floats into a single uint (16 bits per component, signed).
uint PackSnorm2x16(float2 v)
{
    int2 sn = int2(round(clamp(v, -1.0f, 1.0f) * 32767.0f));
    uint lo = (uint)(sn.x & 0xFFFF);
    uint hi = (uint)(sn.y & 0xFFFF);
    return lo | (hi << 16u);
}

/// Unpack a uint into two [-1,1] floats (16 bits per component, signed).
float2 UnpackSnorm2x16(uint p)
{
    int lo = (int)((p & 0xFFFFu) << 16u) >> 16;  // sign-extend
    int hi = (int)(p & 0xFFFF0000u) >> 16;        // already in high bits
    return clamp(float2(lo, hi) / 32767.0f, -1.0f, 1.0f);
}

// ─── R11G11B10F packing ──────────────────────────────────────────────────

/// Helper: convert a float to an 11-bit float (5-bit exponent, 6-bit mantissa, no sign).
uint FloatToR11(float v)
{
    uint f = asuint(max(v, 0.0f));
    // Extract biased exponent and mantissa from IEEE 754
    uint biasedExp = (f >> 23u) & 0xFFu;
    uint mantissa  = f & 0x7FFFFFu;

    // Clamp to R11F range: exponent bias 15, max biased exp 30
    // IEEE bias 127 -> R11 bias 15, so subtract 112
    if (biasedExp < 112u) return 0u;         // Underflow to zero
    if (biasedExp > 142u) return 0x7BFu;     // Clamp to max (all 1s mantissa, exp 30)

    uint e11 = biasedExp - 112u;
    uint m11 = mantissa >> 17u;  // 23-bit mantissa -> 6-bit
    return (e11 << 6u) | m11;
}

/// Helper: convert a float to a 10-bit float (5-bit exponent, 5-bit mantissa, no sign).
uint FloatToB10(float v)
{
    uint f = asuint(max(v, 0.0f));
    uint biasedExp = (f >> 23u) & 0xFFu;
    uint mantissa  = f & 0x7FFFFFu;

    if (biasedExp < 112u) return 0u;
    if (biasedExp > 142u) return 0x3DFu;

    uint e10 = biasedExp - 112u;
    uint m10 = mantissa >> 18u;  // 23-bit -> 5-bit
    return (e10 << 5u) | m10;
}

/// Helper: convert an 11-bit float back to IEEE 754 float.
float R11ToFloat(uint v)
{
    uint e = (v >> 6u) & 0x1Fu;
    uint m = v & 0x3Fu;
    if (e == 0u && m == 0u) return 0.0f;
    uint f = ((e + 112u) << 23u) | (m << 17u);
    return asfloat(f);
}

/// Helper: convert a 10-bit float back to IEEE 754 float.
float B10ToFloat(uint v)
{
    uint e = (v >> 5u) & 0x1Fu;
    uint m = v & 0x1Fu;
    if (e == 0u && m == 0u) return 0.0f;
    uint f = ((e + 112u) << 23u) | (m << 18u);
    return asfloat(f);
}

/// Pack linear RGB to R11G11B10 float format (no sign bit, HDR-capable).
/// @ref OpenGL 4.6 Specification, Section 8.5.2 "Packed Formats"
uint PackR11G11B10F(float3 rgb)
{
    uint r = FloatToR11(rgb.x);
    uint g = FloatToR11(rgb.y);
    uint b = FloatToB10(rgb.z);
    return r | (g << 11u) | (b << 22u);
}

/// Unpack R11G11B10 float format to linear RGB.
float3 UnpackR11G11B10F(uint packed)
{
    uint r = packed & 0x7FFu;
    uint g = (packed >> 11u) & 0x7FFu;
    uint b = (packed >> 22u) & 0x3FFu;
    return float3(R11ToFloat(r), R11ToFloat(g), B10ToFloat(b));
}

// ─── RGBE shared-exponent encoding ────────────────────────────────────────

/// Pack HDR linear RGB to Radiance RGBE (shared exponent) format.
/// @ref Ward 1991 "Real Pixels" — Graphics Gems II, Radiance HDR format
///
/// Stores RGB with 8-bit mantissas sharing a single 8-bit exponent.
/// Encodes values in approximately [1e-38, 1e+38] range.
uint PackRGBE(float3 rgb)
{
    float maxComp = max(rgb.x, max(rgb.y, rgb.z));
    if (maxComp < 1e-32f) return 0u;

    // frexp: maxComp = mantissa * 2^exponent, mantissa in [0.5, 1.0)
    // We need to scale RGB so the largest component fits in [0, 255].
    // exponent is biased by 128 in the RGBE format.
    int exponent;
    float mantissa = frexp(maxComp, exponent);

    // Scale factor: 256.0 / maxComp * mantissa puts values in [0, 255]
    float scale = mantissa * 256.0f / maxComp;

    uint r = (uint)(rgb.x * scale);
    uint g = (uint)(rgb.y * scale);
    uint b = (uint)(rgb.z * scale);
    uint e = (uint)(exponent + 128);

    return r | (g << 8u) | (b << 16u) | (e << 24u);
}

/// Unpack Radiance RGBE to linear HDR RGB.
/// @ref Ward 1991 "Real Pixels"
float3 UnpackRGBE(uint packed)
{
    float e = float((packed >> 24u) & 0xFFu) - 128.0f;
    float scale = ldexp(1.0f, (int)e) / 256.0f;

    float r = float(packed & 0xFFu) * scale;
    float g = float((packed >> 8u) & 0xFFu) * scale;
    float b = float((packed >> 16u) & 0xFFu) * scale;
    return float3(r, g, b);
}

// ─── GBuffer packing ─────────────────────────────────────────────────────

/// Pack octahedral-encoded normal + metallic + roughness into RGBA8.
/// Layout: RG = octahedral normal [0,1], B = metallic, A = roughness.
/// Used for GBuffer RT1 (normal/material) in RGBA8_UNORM format.
float4 PackNormalMetallicRoughness(float2 octNormal, float metallic, float roughness)
{
    return float4(octNormal.x, octNormal.y, saturate(metallic), saturate(roughness));
}

#endif // UZ_COMMON_ENCODING_HLSLI
