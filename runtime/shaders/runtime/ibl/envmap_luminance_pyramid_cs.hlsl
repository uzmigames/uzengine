/// Environment map luminance pyramid — hierarchical importance weights.
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" Chapter 14.6 — 2D importance sampling
/// @ref Falcor EnvMapSampler — hierarchical luminance pyramid for O(1) env sampling
///
/// Pass 0 (PYRAMID_MIP0): Cube → equirectangular luminance.
///   Input: TextureCube<float4> env map (bindless SRV).
///   Output: RWTexture2D<float> mip0 of luminance pyramid (equirect layout).
///   Each texel stores luminance * sin(theta) for correct spherical PDF.
///
/// Pass 1+ (PYRAMID_REDUCE): 2x2 block sum reduction.
///   Input: Texture2D<float> previous mip (SRV).
///   Output: RWTexture2D<float> current mip (UAV).
///   Each texel stores the sum of 4 parent texels.
///
/// Root constants at b0: { envSrvIndex, outputUavIdx, inputSrvIdx, mipWidth, mipHeight, colorUavIndex, sampleMip }
/// (sampleMip: the source cubemap mip pass 0 samples — 0 for the pyramid, any level for the diagnostics dump of
/// a prefiltered cubemap's mips.)
/// (colorUavIndex: pass 0 also writes the sampled radiance to this RGBA32F equirect when it is not the
/// sentinel — the diagnostics dump of the environment the sampler saw, block B.5b.)
/// s0: linear sampler

#include "../common/constants.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_envSrvIndex;     ///< SRV index of source cubemap (pass 0) or previous mip (pass 1+).
    uint g_outputUavIndex;  ///< UAV index of output mip level.
    uint g_inputSrvIndex;   ///< SRV index of previous mip (pass 1+ only, unused in pass 0).
#endif
    uint g_mipWidth;        ///< Output mip width in texels.
    uint g_mipHeight;       ///< Output mip height in texels.
#ifdef UZ_BINDLESS
    uint g_colorUavIndex;   ///< UAV of the optional equirect radiance output (0xFFFFFFFF = none).
    uint g_sampleMip;       ///< Source cubemap mip sampled by pass 0 (0 for the pyramid).
#endif
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
TextureCube<float4>  t_envMap    : register(t0);
Texture2D<float>     t_prevMip   : register(t0);
RWTexture2D<float>   u_outputMip : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Pass 0: Cubemap → equirectangular luminance (mip 0)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef PYRAMID_MIP0

/// @ref BT.709 luminance coefficients
float Luminance(float3 rgb) {
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

/// Convert equirectangular UV [0,1]² to unit direction vector.
/// U maps to phi [0, 2*PI], V maps to theta [0, PI].
float3 EquirectToDir(float2 uv) {
    float phi   = uv.x * 2.0 * PI;
    float theta = uv.y * PI;
    float sinTheta = sin(theta);
    return float3(
        sinTheta * cos(phi),   // X
        sinTheta * sin(phi),   // Y
        cos(theta)             // Z (up)
    );
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_mipWidth || DTid.y >= g_mipHeight) return;

    // Texel center in [0,1]²
    float2 uv = (float2(DTid.xy) + 0.5) / float2(g_mipWidth, g_mipHeight);

    // Direction for this texel
    float3 dir = EquirectToDir(uv);

    // Sample environment cubemap
#ifdef UZ_BINDLESS
    TextureCube<float4> envMap = ResourceDescriptorHeap[g_envSrvIndex];
    float3 rgb = envMap.SampleLevel(linearSampler, dir, float(g_sampleMip)).rgb;
#else
    TextureCube<float4> envMap = t_envMap;
    float3 rgb = envMap.SampleLevel(linearSampler, dir, 0).rgb;
#endif

    // Luminance weighted by sin(theta) for correct solid angle measure.
    // The Jacobian of equirectangular projection includes sin(theta),
    // so we bake it into the importance weight to get a proper PDF.
    float theta = uv.y * PI;
    float sinTheta = sin(theta);
    float weight = Luminance(rgb) * sinTheta;

    // Write to mip 0
#ifdef UZ_BINDLESS
    RWTexture2D<float> outputMip = ResourceDescriptorHeap[g_outputUavIndex];
    if (g_colorUavIndex != 0xFFFFFFFFu) {
        RWTexture2D<float4> colorOut = ResourceDescriptorHeap[g_colorUavIndex];
        colorOut[DTid.xy] = float4(rgb, 1.0);
    }
#else
    RWTexture2D<float> outputMip = u_outputMip;
#endif
    outputMip[DTid.xy] = weight;
}

#endif // PYRAMID_MIP0

// ─────────────────────────────────────────────────────────────────────────────
// Pass 1+: Hierarchical 2×2 sum reduction
// ─────────────────────────────────────────────────────────────────────────────

#ifdef PYRAMID_REDUCE

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_mipWidth || DTid.y >= g_mipHeight) return;

    // Read 2x2 block from previous mip
#ifdef UZ_BINDLESS
    Texture2D<float> prevMip = ResourceDescriptorHeap[g_inputSrvIndex];
#else
    Texture2D<float> prevMip = t_prevMip;
#endif
    uint2 srcBase = DTid.xy * 2;

    float a = prevMip.Load(int3(srcBase + uint2(0, 0), 0));
    float b = prevMip.Load(int3(srcBase + uint2(1, 0), 0));
    float c = prevMip.Load(int3(srcBase + uint2(0, 1), 0));
    float d = prevMip.Load(int3(srcBase + uint2(1, 1), 0));

    // Sum (NOT average) — preserves total luminance for PDF normalization.
    float sum = a + b + c + d;

#ifdef UZ_BINDLESS
    RWTexture2D<float> outputMip2 = ResourceDescriptorHeap[g_outputUavIndex];
#else
    RWTexture2D<float> outputMip2 = u_outputMip;
#endif
    outputMip2[DTid.xy] = sum;
}

#endif // PYRAMID_REDUCE
