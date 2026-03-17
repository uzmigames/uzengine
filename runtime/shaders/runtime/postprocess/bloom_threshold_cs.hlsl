/// Bloom Threshold Pass — Extract Bright Pixels with Karis Average
///
/// Extracts pixels above the bloom threshold from the HDR scene color and
/// writes them to a half-resolution texture. Uses the Karis average (weighted
/// by 1/(1+luma)) to prevent firefly artifacts from extremely bright pixels
/// dominating the bloom result.
///
/// # UE5 Equivalent
/// PostProcessBloom.usf — BloomSetupPS/BloomSetupCS
/// The Karis average prevents single bright pixels from producing distracting
/// bloom artifacts (the "firefly" problem common with HDR bloom).
///
/// @source UE5 PostProcessBloom.usf:37-67 — BloomSetupCommon
/// @constants BloomThreshold default = 1.0 (PostProcessSettings.h)
/// @deviations UE5 uses exposure scale from eye adaptation buffer; we use
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
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // Output is half-resolution
    uint outputWidth  = g_InputWidth / 2;
    uint outputHeight = g_InputHeight / 2;

    if (dtid.x >= outputWidth || dtid.y >= outputHeight)
        return;

    Texture2D<float4> hdrTex = ResourceDescriptorHeap[g_HdrSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_OutputUavIndex];

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

    // Apply threshold: subtract threshold from luminance, scale remaining contribution
    // @source UE5 PostProcessBloom.usf:59-63 — threshold and saturate pattern
    float totalLuminance = Luminance(color) * g_Exposure;
    float bloomLuminance = totalLuminance - g_BloomThreshold;
    float bloomAmount = saturate(bloomLuminance * 0.5f);

    outputTex[dtid.xy] = float4(bloomAmount * color, 1.0f);
}
