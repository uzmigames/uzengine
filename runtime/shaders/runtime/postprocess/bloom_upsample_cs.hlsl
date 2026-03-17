/// Bloom Upsample Chain — 9-Tap Tent Filter with Additive Blend
///
/// Progressive 2x upsampling with a 3x3 tent filter. Each upsample level
/// reads the result of the previous (lower-resolution) level and blends it
/// additively with the current level's downsample result.
///
/// The 9-tap tent filter provides smooth upsampling with minimal ringing:
/// ```
///   1  2  1
///   2  4  2   / 16
///   1  2  1
/// ```
///
/// # Usage
/// Dispatch once per upsample level (reverse of downsample):
/// - Level 0: /64 + /32 → /32
/// - Level 1: /32 + /16 → /16
/// - Level 2: /16 + /8  → /8
/// - Level 3: /8  + /4  → /4
/// - Level 4: /4  + /2  → /2
///
/// At each level, the output is: downsample[level] + upsample(previousResult)
/// This produces a smooth multi-scale bloom with contributions from all levels.
///
/// @source Jimenez 2014, "Next Generation Post-Processing in Call of Duty: AW"
///   slide 167 — 9-tap tent filter upsample
/// @source UE5 PostProcessBloomSetup.cpp:268 — per-stage tint applied to each bloom level
/// @source UE5 Scene.cpp:426-437 — default tint values per stage
/// @deviations NONE — standard tent filter with UE5 per-stage tint weights

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

cbuffer BloomUpsampleParams : register(b0) {
    uint  g_LowerMipSrvIndex;  ///< SRV index for the lower-res input (upsampled from previous level).
    uint  g_OutputUavIndex;    ///< UAV index for output texture (this level's result, also read for blend).
    uint  g_OutputWidth;       ///< Output texture width in pixels.
    uint  g_OutputHeight;      ///< Output texture height in pixels.
    float g_BloomRadius;       ///< Filter radius scale (default 1.0, increases blur spread).
    float g_StageTint;         ///< Per-stage tint weight from UE5 bloom settings (Scene.cpp:426-437).
    float _pad1;
    float _pad2;
};

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// 9-tap tent filter upsample with additive blend.
///
/// Reads the lower-resolution bloom result and upsamples it using a 3x3
/// tent filter (bilinear-weighted), then additively blends with the
/// downsample result at this resolution level.
///
/// The tent filter weights (normalized):
///   1/16  2/16  1/16
///   2/16  4/16  2/16
///   1/16  2/16  1/16
///
/// Using bilinear hardware sampling, this requires only 9 taps.
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_OutputWidth || dtid.y >= g_OutputHeight)
        return;

    Texture2D<float4> lowerMipTex  = ResourceDescriptorHeap[g_LowerMipSrvIndex];
    RWTexture2D<float4> outputTex  = ResourceDescriptorHeap[g_OutputUavIndex];

    float2 texelSize = g_BloomRadius / float2(g_OutputWidth, g_OutputHeight);

    // UV for this output pixel
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(g_OutputWidth, g_OutputHeight);

    // 9-tap tent filter on the lower-resolution bloom input
    // Weights: 1/16, 2/16, 4/16 for corners, edges, center
    float3 upsampled = float3(0, 0, 0);

    // Corners (weight 1/16 each)
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2(-1, -1) * texelSize, 0).rgb;
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2( 1, -1) * texelSize, 0).rgb;
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2(-1,  1) * texelSize, 0).rgb;
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2( 1,  1) * texelSize, 0).rgb;

    // Edges (weight 2/16 each)
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2( 0, -1) * texelSize, 0).rgb * 2.0f;
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2(-1,  0) * texelSize, 0).rgb * 2.0f;
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2( 1,  0) * texelSize, 0).rgb * 2.0f;
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv + float2( 0,  1) * texelSize, 0).rgb * 2.0f;

    // Center (weight 4/16)
    upsampled += lowerMipTex.SampleLevel(linearSampler, uv, 0).rgb * 4.0f;

    // Normalize: total weights = 4*1 + 4*2 + 1*4 = 16
    upsampled *= (1.0f / 16.0f);

    // Apply per-stage tint weight before accumulation.
    // UE5 multiplies each bloom stage's contribution by its tint color during upsample.
    // @source UE5 PostProcessBloomSetup.cpp:268 — PassInputs.TintColor = BloomStage.Tint * TintScale
    upsampled *= g_StageTint;

    // Read the downsample result at this level via UAV load (same resource
    // as output — per-pixel 1:1 read-modify-write avoids SRV/UAV conflict)
    float3 currentLevel = outputTex[dtid.xy].rgb;

    float3 result = currentLevel + upsampled;

    outputTex[dtid.xy] = float4(max(0, result), 1.0f);
}
