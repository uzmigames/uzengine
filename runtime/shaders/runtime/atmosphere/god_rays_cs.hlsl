/// UzEngine God Rays (Radial Light Shafts) — Progressive Blur
///
/// @ref Mitchell 2007 "Volumetric Light Scattering as a Post-Process" (GPU Gems 3)
/// @ref Toth & Umenhoffer 2009 "Real-time Volumetric Lighting in Participating Media"
///
/// Computes screen-space radial light shafts by blurring from the projected
/// sun position outward. Uses a single pass with 16 samples at progressive
/// step sizes to cover the full blur radius efficiently.
///
/// # Algorithm
/// For each pixel, ray-march from the pixel toward the projected sun position,
/// sampling the source texture (depth-masked scene). Accumulate and decay
/// the contribution along the ray.
///
/// Root constants at b0 (8 DWORDs):
///   [0] inputSrvIndex    — HDR scene color or occlusion mask SRV
///   [1] depthSrvIndex    — depth buffer SRV (for sky masking)
///   [2] outputUavIndex   — god rays output UAV (additive contribution)
///   [3] packed: low16=width, high16=height
///   [4] sunScreenX_bits  — projected sun X position in [0,1] as float bits
///   [5] sunScreenY_bits  — projected sun Y position in [0,1] as float bits
///   [6] packed params: density(half) | weight(half)
///   [7] packed params: decay(half) | exposure(half)
///
/// PerFrameCB at b1: unused (all params via root constants).

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_inputSrvIndex;
    uint g_depthSrvIndex;
    uint g_outputUavIndex;
#endif
    uint g_packedDimensions;  // low16 = width, high16 = height
    uint g_sunScreenX_bits;
    uint g_sunScreenY_bits;
    uint g_packedDensityWeight;
    uint g_packedDecayExposure;
};

SamplerState linearSampler : register(s0);

/// @ref Mitchell 2007 GPU Gems 3 Ch. 13 — god ray default parameters
/// @constants NUM_SAMPLES=16, density=0.97, weight=0.25, decay=0.975, exposure=0.25
static const uint  kNumSamples = 16;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint width  = g_packedDimensions & 0xFFFF;
    uint height = g_packedDimensions >> 16;
    if (DTid.x >= width || DTid.y >= height) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(width, height);

    // Decode sun screen position
    float sunX = asfloat(g_sunScreenX_bits);
    float sunY = asfloat(g_sunScreenY_bits);
    float2 sunUV = float2(sunX, sunY);

    // Decode packed parameters
    float density  = f16tof32(g_packedDensityWeight & 0xFFFF);
    float weight   = f16tof32(g_packedDensityWeight >> 16);
    float decay    = f16tof32(g_packedDecayExposure & 0xFFFF);
    float exposure = f16tof32(g_packedDecayExposure >> 16);

    // Fallback defaults if parameters are zero/invalid
    if (density  <= 0.0) density  = 0.97;
    if (weight   <= 0.0) weight   = 0.25;
    if (decay    <= 0.0) decay    = 0.975;
    if (exposure <= 0.0) exposure = 0.25;

#ifdef UZ_BINDLESS
    Texture2D<float4>   inputTex  = ResourceDescriptorHeap[g_inputSrvIndex];
    Texture2D<float>    depthTex  = ResourceDescriptorHeap[g_depthSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_outputUavIndex];
#endif

    // Direction from pixel toward sun
    float2 deltaUV = (sunUV - uv) * density / float(kNumSamples);

    // Accumulate light along the ray
    float2 sampleUV = uv;
    float3 totalLight = float3(0, 0, 0);
    float illuminationDecay = 1.0;

    for (uint i = 0; i < kNumSamples; ++i) {
        sampleUV += deltaUV;

        // Bounds check
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) break;

        // Sky mask: only sky pixels contribute to god rays
        float sampleDepth = depthTex.SampleLevel(linearSampler, sampleUV, 0);
        float skyMask = IsSkyDepth(sampleDepth) ? 1.0 : 0.0;

        // Sample scene color (or use white for pure occlusion-based rays)
        float3 sampleColor = inputTex.SampleLevel(linearSampler, sampleUV, 0).rgb;

        // Apply sky mask — only sky contributes light shafts
        sampleColor *= skyMask;

        // Accumulate with decay
        totalLight += sampleColor * illuminationDecay * weight;
        illuminationDecay *= decay;
    }

    // Apply exposure
    float3 godRays = totalLight * exposure;

    // Output as additive contribution (to be blended with the scene)
    outputTex[DTid.xy] = float4(godRays, 0.0);
}
