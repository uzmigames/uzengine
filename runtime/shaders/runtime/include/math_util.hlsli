/// Compiler-agnostic math utilities for UzEngine shaders.
///
/// FXC (SM5.0/DXBC) and DXC (SM6.x/DXIL) compile pow() with different
/// instruction patterns — FXC uses multiply chains for small integer
/// exponents while DXC always uses log-mul-exp. This causes visible
/// brightness divergence between DX11 and DX12 on the same GPU.
///
/// This header provides deterministic replacements that produce identical
/// results on both compilers, following the same pattern as UE5's
/// MathUtil.ush (Pow2/Pow4/Pow5) and Common.ush (LinearToSrgb).
///
/// # Usage
/// Include this header in any shader that needs pow-of-N or sRGB conversion:
/// ```hlsl
/// #include "include/math_util.hlsli"
/// float3 F = F0 + (1 - F0) * Pow5(1 - cosTheta);
/// color = LinearToSrgb(color);
/// ```

#ifndef MATH_UTIL_HLSLI
#define MATH_UTIL_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Deterministic Power Functions
// ─────────────────────────────────────────────────────────────────────────────
// Explicit multiply chains — identical codegen on FXC and DXC.

/// x^2 via multiply (avoids pow).
float  Pow2(float  x) { return x * x; }
float2 Pow2(float2 x) { return x * x; }
float3 Pow2(float3 x) { return x * x; }
float4 Pow2(float4 x) { return x * x; }

/// x^4 via two squarings (avoids pow).
float  Pow4(float  x) { float x2 = x * x; return x2 * x2; }
float2 Pow4(float2 x) { float2 x2 = x * x; return x2 * x2; }
float3 Pow4(float3 x) { float3 x2 = x * x; return x2 * x2; }
float4 Pow4(float4 x) { float4 x2 = x * x; return x2 * x2; }

/// x^5 via multiply chain (avoids pow).
/// Primary use: Schlick Fresnel approximation pow(1 - cosTheta, 5).
float  Pow5(float  x) { float x2 = x * x; return x2 * x2 * x; }
float2 Pow5(float2 x) { float2 x2 = x * x; return x2 * x2 * x; }
float3 Pow5(float3 x) { float3 x2 = x * x; return x2 * x2 * x; }
float4 Pow5(float4 x) { float4 x2 = x * x; return x2 * x2 * x; }

// ─────────────────────────────────────────────────────────────────────────────
// sRGB Conversion — IEC 61966-2-1 exact transfer function
// ─────────────────────────────────────────────────────────────────────────────
// Uses pow(x, 1/2.4) exponent from the sRGB spec instead of the approximate
// pow(x, 1/2.2). The 1/2.4 exponent is exact in IEEE754 (0.41666...repeating
// but the spec mandates it), and critically, the two-branch structure with
// step()+lerp() eliminates the divergence-prone pow(x, 1/2.2) entirely for
// the linear segment. This matches UE5 Common.ush LinearToSrgbBranchless().

/// Converts linear-space color to sRGB using the exact IEC 61966-2-1 curve.
///
/// # Parameters
/// - lin: linear-space RGB color (pre-tonemapped, in [0,1] range).
///
/// # Returns
/// sRGB-encoded color suitable for UNORM backbuffer output.
float3 LinearToSrgb(float3 lin) {
    float3 lo = lin * 12.92f;
    float3 hi = 1.055f * pow(max(lin, 0.0f), 1.0f / 2.4f) - 0.055f;
    float3 t  = step(0.0031308f, lin);
    return lerp(lo, hi, t);
}

/// Converts sRGB-encoded color back to linear space.
///
/// # Parameters
/// - srgb: sRGB-encoded RGB color (e.g. from texture with no hardware decode).
///
/// # Returns
/// Linear-space color.
float3 SrgbToLinear(float3 srgb) {
    float3 lo = srgb / 12.92f;
    float3 hi = pow(max((srgb + 0.055f) / 1.055f, 0.0f), 2.4f);
    float3 t  = step(0.04045f, srgb);
    return lerp(lo, hi, t);
}

#endif // MATH_UTIL_HLSLI
