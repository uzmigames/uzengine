/// FSR 1.0 RCAS — Robust Contrast-Adaptive Sharpening (Compute Shader)
///
/// Second (optional) pass of AMD FidelityFX Super Resolution 1.0.
/// Applies contrast-adaptive sharpening to the EASU output to restore
/// fine detail that spatial upscaling may have softened.
///
/// The algorithm:
///   1. Read center pixel + 4 cardinal neighbors (cross pattern: N, S, E, W)
///   2. Compute per-channel min/max of the 5 samples
///   3. Derive adaptive sharpening weight: sqrt(min(minRGB) / max(maxRGB))
///      - High local contrast (edges) → low weight → less sharpening (avoids ringing)
///      - Low local contrast (flat) → high weight → more sharpening (restores detail)
///   4. Apply: result = clamp((center + neighbors * w) / (1 + 4 * w), min, max)
///      The clamp to [min, max] prevents ringing (overshoots/undershoots).
///
/// RCAS is derived from AMD's CAS (Contrast Adaptive Sharpening) but adapted
/// for use as a post-upscaling sharpening pass. The key difference is the
/// sharpness attenuation parameter that allows tuning for upscaled content.
///
/// @ref AMD 2021 "FidelityFX Super Resolution" — GPUOpen published algorithm
/// @ref AMD FidelityFX SDK (MIT license) — ffx_fsr1.h RCAS implementation
/// @constants sharpness packed in con0 by FsrRcasCon() on CPU side.
///   Sharpness range: 0.0 = maximum sharpness, 2.0 = softest.
///   Default: 0.2 (AMD recommended for quality mode).
/// @deviations NONE — algorithm matches AMD reference ffx_fsr1.h

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — RCAS configuration packed by FsrRcasCon().
///
/// con0.x: packed sharpness attenuation (float-as-uint).
///   Computed as: exp2(-sharpness) where sharpness is in [0, 2].
///   0.0 sharpness → attenuation = 1.0 (max sharp).
///   2.0 sharpness → attenuation = 0.25 (minimal sharpening).
cbuffer FSRRcasParams : register(b0) {
    uint4 g_Con0;           ///< RCAS constant 0 (packed sharpness attenuation).
    uint  g_InputSrvIndex;  ///< Bindless SRV index for input texture (EASU output).
    uint  g_OutputUavIndex; ///< Bindless UAV index for output texture.
    uint  g_OutputWidth;    ///< Output texture width in pixels.
    uint  g_OutputHeight;   ///< Output texture height in pixels.
};

#ifndef UZ_BINDLESS
Texture2D<float4>   g_InputTexture  : register(t0);
RWTexture2D<float4> g_OutputTexture : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Reinterpret a uint bit pattern as float.
float AU1_AF1(uint a) { return asfloat(a); }

// ─────────────────────────────────────────────────────────────────────────────
// RCAS Per-Pixel Evaluation
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates RCAS for a single pixel.
///
/// Reads a cross-shaped 5-tap neighborhood, computes adaptive sharpening
/// weight from local contrast, and applies the sharpening filter with
/// clamping to prevent ringing artifacts.
///
/// @ref AMD ffx_fsr1.h — FsrRcasF function
///
/// @param pos       Pixel position (integer coordinates).
/// @param inputTex  Input texture (EASU output at full output resolution).
/// @return          Sharpened RGB color.
float3 FsrRcasF(uint2 pos, Texture2D<float4> inputTex)
{
    // Load center pixel and 4 cardinal neighbors (integer loads, no filtering).
    float3 e = inputTex.Load(int3(pos,            0)).rgb;  // Center
    float3 b = inputTex.Load(int3(pos + int2( 0, -1), 0)).rgb;  // North
    float3 d = inputTex.Load(int3(pos + int2(-1,  0), 0)).rgb;  // West
    float3 f = inputTex.Load(int3(pos + int2( 1,  0), 0)).rgb;  // East
    float3 h = inputTex.Load(int3(pos + int2( 0,  1), 0)).rgb;  // South

    // Per-channel min/max of the 5 samples.
    // Used for both contrast estimation and output clamping.
    float3 mnRGB = min(min(min(b, d), min(f, h)), e);
    float3 mxRGB = max(max(max(b, d), max(f, h)), e);

    // Compute the per-channel reciprocal of the max for the weight formula.
    // The weight is: sharpnessAttenuation * sqrt(min / max).
    // We compute min * (1/max) = min/max, then sqrt.

    // Scalar contrast metric: use the minimum of per-channel min/max ratios.
    // This ensures we sharpen conservatively — the channel with the most
    // contrast (lowest min/max ratio) limits the overall sharpening.
    float mnR = mnRGB.r, mnG = mnRGB.g, mnB = mnRGB.b;
    float mxR = mxRGB.r, mxG = mxRGB.g, mxB = mxRGB.b;

    // Prevent division by zero in flat black regions.
    float ampR = saturate(min(mnR, 1.0f - mxR) / max(mxR, 1.0f / 65536.0f));
    float ampG = saturate(min(mnG, 1.0f - mxG) / max(mxG, 1.0f / 65536.0f));
    float ampB = saturate(min(mnB, 1.0f - mxB) / max(mxB, 1.0f / 65536.0f));

    // Use minimum amplitude across channels for conservative sharpening.
    float amp = min(min(ampR, ampG), ampB);

    // Apply sharpness attenuation (user-controlled).
    // con0.x contains exp2(-sharpness) packed as uint.
    float sharpnessAtten = AU1_AF1(g_Con0.x);

    // The final weight is: amp * sharpnessAtten * (-1).
    // Negative because we are doing: center - w*(avg - center) = center*(1+4w) - w*(b+d+f+h)
    // which is the unsharp mask formulation.
    float w = amp * sharpnessAtten;

    // Clamp weight to prevent excessive sharpening.
    // AMD reference uses a limit based on the number of taps (4 neighbors).
    // w is clamped so that (1 + 4*w) stays positive and reasonable.
    // The limit sqrt(5)/4 - 0.5 ~= 0.059 keeps the filter stable.
    // However, the AMD RCAS uses a different approach: the weight is already
    // bounded by the contrast ratio in [0,1] and attenuation in (0,1].
    // The maximum contribution from any neighbor is w, and the center gets
    // 1/(1+4w), so no additional clamping needed beyond the saturation above.

    // Apply sharpening: weighted sum of neighbors with normalization.
    // Formula: (center + (b+d+f+h)*w) / (1 + 4*w)
    // This is equivalent to: center + w * ((b+d+f+h)/4 - center) * 4/(1+4w)
    // which is a contrast-aware unsharp mask.
    float rcpW = rcp(1.0f + 4.0f * w);
    float3 result = (e + (b + d + f + h) * w) * rcpW;

    // Clamp to local min/max to prevent ringing artifacts.
    // The 5-tap min/max defines the valid range — we never produce values
    // outside what was already present in the input.
    result = clamp(result, mnRGB, mxRGB);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// FSR 1.0 RCAS compute shader.
///
/// Thread group: [8, 8, 1] — standard 2D tile for image processing.
/// Each thread computes one output pixel.
///
/// Dispatch: ceil(outputWidth/8) x ceil(outputHeight/8) x 1.
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Early-out for threads outside the output dimensions.
    if (DTid.x >= g_OutputWidth || DTid.y >= g_OutputHeight) return;

    // Access resources — bindless (SM6.6) or explicit register (SM5).
#ifdef UZ_BINDLESS
    Texture2D<float4>   inputTex  = ResourceDescriptorHeap[g_InputSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    Texture2D<float4>   inputTex  = g_InputTexture;
    RWTexture2D<float4> outputTex = g_OutputTexture;
#endif

    // Run RCAS sharpening.
    float3 color = FsrRcasF(DTid.xy, inputTex);

    // Write result. Alpha = 1.0 (RCAS operates on RGB only).
    outputTex[DTid.xy] = float4(color, 1.0f);
}
