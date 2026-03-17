/// Bloom Downsample Chain — 13-Tap Downsampling (Jimenez 2014)
///
/// Progressive 2x downsampling with a 13-tap filter that samples a 4x4
/// neighborhood using bilinear hardware filtering for effective 4x4 coverage
/// with only 13 taps. This pattern minimizes aliasing and preserves energy
/// better than a simple box filter.
///
/// The 13-tap pattern (Jimenez 2014, "Next Generation Post-Processing in
/// Call of Duty: Advanced Warfare") samples 5 overlapping 2x2 blocks:
///
/// ```
///   +--+--+--+--+
///   |  a  |  b  |
///   +--e--+--f--+
///   |  c  |  d  |
///   +--+--+--+--+
///
///   Center (e): weight 0.500 (4 overlapping taps at center)
///   Corners (a,b,c,d): weight 0.125 each (one tap per corner 2x2)
///   Edges: 4 taps at edge midpoints, weight 0.250 total
///
///   13 bilinear taps effectively cover a 4x4 texel region.
/// ```
///
/// # Usage
/// Dispatch once per downsample level:
/// - Level 0: fullRes/2 → /4
/// - Level 1: /4 → /8
/// - Level 2: /8 → /16
/// - Level 3: /16 → /32
/// - Level 4: /32 → /64
///
/// @source Jimenez 2014, "Next Generation Post-Processing in Call of Duty: AW"
///   slide 162 — 13-tap downsample filter
/// @source UE5 PostProcessDownsample.usf:34-68 — DownsampleCommon
///   (UE5 uses simpler 4-tap; we use the 13-tap for better quality)
/// @deviations UE5's PostProcessDownsample uses a 4-tap bilinear (4 offset taps).
///   We use the 13-tap Jimenez pattern for better anti-aliasing, which is the
///   standard approach in modern engines (Call of Duty, Unity HDRP).

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

cbuffer BloomDownsampleParams : register(b0) {
    uint  g_InputSrvIndex;     ///< SRV index for input texture (previous mip level).
    uint  g_OutputUavIndex;    ///< UAV index for output texture (next mip level).
    uint  g_OutputWidth;       ///< Output texture width in pixels.
    uint  g_OutputHeight;      ///< Output texture height in pixels.
};

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// 13-tap downsample filter (Jimenez 2014).
///
/// Each thread produces one output pixel by sampling the input texture at 13
/// bilinear-filtered positions covering a 4x4 input texel region.
///
/// The filter samples 5 groups:
/// - 4 corner 2x2 blocks (bilinear filtered, each 1 tap)
/// - 4 edge 2x2 blocks (bilinear filtered, each 1 tap)
/// - 1 center 2x2 block (bilinear filtered, 1 tap) counted with double weight
/// - 4 additional axis-aligned taps at 2-texel distance
///
/// Weights are chosen to be energy-preserving (sum = 1.0).
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_OutputWidth || dtid.y >= g_OutputHeight)
        return;

    Texture2D<float4> inputTex = ResourceDescriptorHeap[g_InputSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_OutputUavIndex];

    // Input is 2x the output resolution
    float2 inputSize = float2(g_OutputWidth * 2, g_OutputHeight * 2);
    float2 texelSize = 1.0f / inputSize;

    // Center UV of the 4x4 source region for this output pixel
    float2 uv = (float2(dtid.xy) * 2.0f + 1.0f) * texelSize;

    // 13-tap filter pattern (Jimenez 2014)
    // Sample positions relative to center, in texel units:
    //
    //   A . B . C
    //   . D . E .
    //   F . G . H
    //   . I . J .
    //   K . L . M
    //
    // Using bilinear sampling, we can cover 4 texels per tap.
    // The filter decomposes into 5 overlapping 2x2 blocks plus cross taps.

    // Corner samples (each covers a 2x2 block via bilinear)
    float3 a = inputTex.SampleLevel(linearSampler, uv + float2(-2, -2) * texelSize, 0).rgb;
    float3 c = inputTex.SampleLevel(linearSampler, uv + float2( 2, -2) * texelSize, 0).rgb;
    float3 k = inputTex.SampleLevel(linearSampler, uv + float2(-2,  2) * texelSize, 0).rgb;
    float3 m = inputTex.SampleLevel(linearSampler, uv + float2( 2,  2) * texelSize, 0).rgb;

    // Edge midpoint samples
    float3 b = inputTex.SampleLevel(linearSampler, uv + float2( 0, -2) * texelSize, 0).rgb;
    float3 f = inputTex.SampleLevel(linearSampler, uv + float2(-2,  0) * texelSize, 0).rgb;
    float3 h = inputTex.SampleLevel(linearSampler, uv + float2( 2,  0) * texelSize, 0).rgb;
    float3 l = inputTex.SampleLevel(linearSampler, uv + float2( 0,  2) * texelSize, 0).rgb;

    // Inner cross samples (covers the center 2x2 region more densely)
    float3 d = inputTex.SampleLevel(linearSampler, uv + float2(-1, -1) * texelSize, 0).rgb;
    float3 e = inputTex.SampleLevel(linearSampler, uv + float2( 1, -1) * texelSize, 0).rgb;
    float3 i = inputTex.SampleLevel(linearSampler, uv + float2(-1,  1) * texelSize, 0).rgb;
    float3 j = inputTex.SampleLevel(linearSampler, uv + float2( 1,  1) * texelSize, 0).rgb;

    // Center sample
    float3 g = inputTex.SampleLevel(linearSampler, uv, 0).rgb;

    // Weighted combination (energy preserving, weights sum to 1.0)
    // Weights from Jimenez 2014:
    // - Center (g): 4/32 = 0.125 (counted in 4 overlapping blocks)
    // - Inner cross (d,e,i,j): 4/32 each = 0.125 (counted in 2 blocks each)
    // - Edge (b,f,h,l): 2/32 each = 0.0625
    // - Corner (a,c,k,m): 1/32 each = 0.03125
    //
    // Simplified grouping (5 blocks of 4 samples each):
    // Block center:     g * 4
    // Block top-left:   a+b+d+g -> but g is shared, so decompose differently.
    //
    // Standard 13-tap weights (Jimenez):
    float3 color = float3(0, 0, 0);
    color += (a + c + k + m) * 0.03125f;       // 4 corners: 1/32 each
    color += (b + f + h + l) * 0.0625f;         // 4 edges: 2/32 each
    color += (d + e + i + j) * 0.125f;          // 4 inner: 4/32 each
    color += g * 0.125f;                         // center: 4/32
    // Total: 4*0.03125 + 4*0.0625 + 4*0.125 + 0.125
    //      = 0.125 + 0.25 + 0.5 + 0.125 = 1.0

    outputTex[dtid.xy] = float4(max(0, color), 1.0f);
}
