/// UzEngine bloom upsample — 3x3 binomial filter + additive blend (Falcor pattern).
/// @ref Falcor SimplePostFX.cs.slang blurFilter3x3 — 6x6 effective footprint
/// @ref Binomial(5) kernel: 1 5 10 10 5 1

cbuffer BloomUpsampleParams : register(b0) {
    uint  g_LowerMipSrvIndex;
    uint  g_OutputUavIndex;
    uint  g_OutputWidth;
    uint  g_OutputHeight;
    float g_BloomRadius;
    float g_StageTintR;
    float g_StageTintG;
    float g_StageTintB;
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4>   g_LowerMipTex : register(t0);
RWTexture2D<float4> g_OutputTex   : register(u0);
#endif

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_OutputWidth || dtid.y >= g_OutputHeight) return;

#ifdef UZ_BINDLESS
    Texture2D<float4> lowerMip = ResourceDescriptorHeap[g_LowerMipSrvIndex];
    RWTexture2D<float4> dst = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    Texture2D<float4> lowerMip = g_LowerMipTex;
    RWTexture2D<float4> dst = g_OutputTex;
#endif

    float2 invRes = float2(1.0 / g_OutputWidth, 1.0 / g_OutputHeight);
    float2 uv = (float2(dtid.xy) + 0.5) * invRes;
    float uscale = invRes.x * g_BloomRadius;
    float vscale = invRes.y * g_BloomRadius;

    // Falcor 3x3 binomial filter — binomial(5): 1 5 10 10 5 1
    static const float w0 = (10.0 + 10.0) / 32.0;  // center weight
    static const float x1 = 1.5 + 1.0 / (1.0 + 5.0);
    static const float w1 = (1.0 + 5.0) / 32.0;
    float x1u = x1 * uscale;
    float x1v = x1 * vscale;

    float3 upsampled =
        (lowerMip.SampleLevel(linearSampler, uv + float2(-x1u, -x1v), 0).rgb +
         lowerMip.SampleLevel(linearSampler, uv + float2( x1u, -x1v), 0).rgb +
         lowerMip.SampleLevel(linearSampler, uv + float2(-x1u,  x1v), 0).rgb +
         lowerMip.SampleLevel(linearSampler, uv + float2( x1u,  x1v), 0).rgb) * (w1 * w1) +
        (lowerMip.SampleLevel(linearSampler, uv + float2(0, -x1v), 0).rgb +
         lowerMip.SampleLevel(linearSampler, uv + float2(0,  x1v), 0).rgb +
         lowerMip.SampleLevel(linearSampler, uv + float2(-x1u, 0), 0).rgb +
         lowerMip.SampleLevel(linearSampler, uv + float2( x1u, 0), 0).rgb) * (w1 * w0) +
        lowerMip.SampleLevel(linearSampler, uv, 0).rgb * (w0 * w0);

    // Per-stage tint
    upsampled *= float3(g_StageTintR, g_StageTintG, g_StageTintB);

    // Additive blend with current level
    float3 currentLevel = dst[dtid.xy].rgb;
    dst[dtid.xy] = float4(max(0, currentLevel + upsampled), 1.0);
}
