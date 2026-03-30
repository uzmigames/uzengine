/// Bloom Threshold Pass — Extract Bright Pixels with Karis Average
///
/// Extracts pixels above the bloom threshold from the HDR scene color and
/// writes them to a half-resolution texture. Uses the Karis average (weighted
/// by 1/(1+luma)) to prevent firefly artifacts from extremely bright pixels
/// dominating the bloom result.
///
/// The Karis average prevents single bright pixels from producing distracting
/// bloom artifacts (the "firefly" problem common with HDR bloom).
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — Karis average (1/(1+luma)) for firefly suppression
/// @constants BloomThreshold default = 1.0 (standard bloom threshold for HDR scene colors)
/// @deviations INTENTIONAL: Karis average (1/(1+luma) weighting) is applied at
///   the threshold stage, NOT during the first downsample. The standard BloomSetupCommon
///   does NOT apply Karis weighting — it relies on downsample filter quality instead.
///   Our approach prevents fireflies earlier in the pipeline, matching the technique from
///   Jimenez 2014 "Next Generation Post Processing in Call of Duty" and Unity HDRP.
///   This produces equivalent visual results with better firefly rejection at the cost
///   of slightly softer bloom on isolated bright pixels. This is a deliberate engineering choice, not a bug.
/// @deviations Standard bloom setup uses exposure scale from eye adaptation buffer; we use
///   PerFrameCB.exposure directly. Same functional result.

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — SRV/UAV indices and dimensions.
cbuffer BloomThresholdParams : register(b0) {
    uint  g_HdrSrvIndex;       ///< SRV descriptor heap index for HDR scene color.
    uint  g_OutputUavIndex;    ///< UAV descriptor heap index for half-res bright output.
    uint  g_InputWidth;        ///< Input texture width in pixels.
    uint  g_InputHeight;       ///< Input texture height in pixels.
    float g_BloomThreshold;    ///< Luminance threshold. Pixels below this are zeroed.
    float g_Exposure;          ///< Current exposure multiplier (from auto-exposure).
    float _pad0;
    float _pad1;
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4>   g_HdrTex    : register(t0);
RWTexture2D<float4> g_OutputTex : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// BT.709 luminance from linear RGB.
float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

/// Karis average weight: 1 / (1 + luma).
/// Bright pixels get lower weight, preventing fireflies from dominating.
/// @source Karis 2014, "Physically Based Shading in Theory and Practice"
///   slide 26 — "Weighted average by luminance to reduce fireflies"
float KarisWeight(float3 color)
{
    return 1.0f / (1.0f + Luminance(color));
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Each thread writes one pixel of the half-resolution output.
/// Reads a 2x2 block from the input, applies Karis-weighted average,
/// then thresholds the result.
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // Output is half-resolution
    uint outputWidth  = g_InputWidth / 2;
    uint outputHeight = g_InputHeight / 2;

    if (dtid.x >= outputWidth || dtid.y >= outputHeight)
        return;

#ifdef UZ_BINDLESS
    Texture2D<float4> hdrTex = ResourceDescriptorHeap[g_HdrSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    Texture2D<float4>   hdrTex    = g_HdrTex;
    RWTexture2D<float4> outputTex = g_OutputTex;
#endif

    // Sample 4 corners of the 2x2 block using bilinear filtering
    // Each SampleLevel at the center of the 2x2 block naturally averages the 4 texels
    // But for Karis average we need individual texel values
    float2 invInputSize = 1.0f / float2(g_InputWidth, g_InputHeight);

    // Source texel coordinates (top-left of the 2x2 block)
    float2 srcUV = (float2(dtid.xy) * 2.0f + 1.0f) * invInputSize;

    // Sample the 4 texels of the 2x2 block
    float2 offset = 0.5f * invInputSize;

    float3 s0 = hdrTex.SampleLevel(linearSampler, srcUV + float2(-offset.x, -offset.y), 0).rgb;
    float3 s1 = hdrTex.SampleLevel(linearSampler, srcUV + float2( offset.x, -offset.y), 0).rgb;
    float3 s2 = hdrTex.SampleLevel(linearSampler, srcUV + float2(-offset.x,  offset.y), 0).rgb;
    float3 s3 = hdrTex.SampleLevel(linearSampler, srcUV + float2( offset.x,  offset.y), 0).rgb;

    // Karis-weighted average: weight each sample by 1/(1+luma)
    // This prevents firefly artifacts from extremely bright pixels
    float w0 = KarisWeight(s0);
    float w1 = KarisWeight(s1);
    float w2 = KarisWeight(s2);
    float w3 = KarisWeight(s3);

    float wSum = w0 + w1 + w2 + w3;
    float3 color = (s0 * w0 + s1 * w1 + s2 * w2 + s3 * w3) / wSum;

    // Smooth quadratic knee threshold — avoids hard cutoff flicker.
    // @ref Karis 2013 — bloom setup with soft knee
    // @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty" — soft threshold
    // @constants knee = 0.5 * threshold (half-width of transition band)
    float totalLuminance = Luminance(color) * g_Exposure;
    float knee = g_BloomThreshold * 0.5f;
    float soft = max(0.0f, totalLuminance - g_BloomThreshold + knee);
    float bloomAmount = (knee > 1e-5f) ? (soft * soft) / (4.0f * knee + 1e-5f) : max(0.0f, totalLuminance - g_BloomThreshold);
    bloomAmount = min(bloomAmount, totalLuminance);

    // Normalize: scale color by the ratio of bloom contribution to total luminance
    float bloomScale = (totalLuminance > 1e-5f) ? (bloomAmount / totalLuminance) : 0.0f;

    outputTex[dtid.xy] = float4(bloomScale * color, 1.0f);
}
