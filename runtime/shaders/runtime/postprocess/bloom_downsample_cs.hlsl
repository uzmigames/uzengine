/// UzEngine bloom downsample — 5x5 binomial filter (Falcor pattern).
/// @ref Falcor SimplePostFX.cs.slang blurFilter5x5 — 10x10 effective footprint
/// @ref Binomial(9) kernel: 1 9 36 84 126 126 84 36 9 1

cbuffer BloomDownsampleParams : register(b0) {
    uint  g_InputSrvIndex;
    uint  g_OutputUavIndex;
    uint  g_OutputWidth;
    uint  g_OutputHeight;
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4>   g_InputTex  : register(t0);
RWTexture2D<float4> g_OutputTex : register(u0);
#endif

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_OutputWidth || dtid.y >= g_OutputHeight) return;

#ifdef UZ_BINDLESS
    Texture2D<float4> src = ResourceDescriptorHeap[g_InputSrvIndex];
    RWTexture2D<float4> dst = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    Texture2D<float4> src = g_InputTex;
    RWTexture2D<float4> dst = g_OutputTex;
#endif

    // Falcor 5x5 bilinear filter — binomial(9) weights: 1 9 36 84 126 126 84 36 9 1
    // 5x5 bilinear taps cover 10x10 effective texels
    float2 invRes = 1.0 / float2(g_OutputWidth * 2, g_OutputHeight * 2);
    float2 uv = (float2(dtid.xy) * 2.0 + 1.0) * invRes;
    float uscale = invRes.x * 0.5;
    float vscale = invRes.y * 0.5;

    // Weights from binomial(9) / 512
    static const float w0 = (126.0 + 126.0) / 512.0;  // center
    static const float x1 = 1.5 + 36.0 / (36.0 + 84.0);
    static const float w1 = (36.0 + 84.0) / 512.0;
    static const float x2 = 3.5 + 1.0 / (1.0 + 9.0);
    static const float w2 = (1.0 + 10.0) / 512.0;

    float x1u = x1 * uscale, x2u = x2 * uscale;
    float x1v = x1 * vscale, x2v = x2 * vscale;

    // Row 0 (y = -x2v, -x1v offsets)
    float4 s0 =
        (src.SampleLevel(linearSampler, uv + float2(-x2u, -x2v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x2u, -x2v), 0) +
         src.SampleLevel(linearSampler, uv + float2(-x2u,  x2v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x2u,  x2v), 0)) * (w2 * w2) +
        (src.SampleLevel(linearSampler, uv + float2(-x1u, -x2v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x1u, -x2v), 0) +
         src.SampleLevel(linearSampler, uv + float2(-x1u,  x2v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x1u,  x2v), 0)) * (w1 * w2) +
        (src.SampleLevel(linearSampler, uv + float2(0, -x2v), 0) +
         src.SampleLevel(linearSampler, uv + float2(0,  x2v), 0)) * (w0 * w2);

    // Row 1 (y = -x1v, x1v offsets)
    float4 s1 =
        (src.SampleLevel(linearSampler, uv + float2(-x2u, -x1v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x2u, -x1v), 0) +
         src.SampleLevel(linearSampler, uv + float2(-x2u,  x1v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x2u,  x1v), 0)) * (w2 * w1) +
        (src.SampleLevel(linearSampler, uv + float2(-x1u, -x1v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x1u, -x1v), 0) +
         src.SampleLevel(linearSampler, uv + float2(-x1u,  x1v), 0) +
         src.SampleLevel(linearSampler, uv + float2( x1u,  x1v), 0)) * (w1 * w1) +
        (src.SampleLevel(linearSampler, uv + float2(0, -x1v), 0) +
         src.SampleLevel(linearSampler, uv + float2(0,  x1v), 0)) * (w0 * w1);

    // Row 2 (y = 0)
    float4 s2 =
        (src.SampleLevel(linearSampler, uv + float2(-x2u, 0), 0) +
         src.SampleLevel(linearSampler, uv + float2( x2u, 0), 0)) * (w2 * w0) +
        (src.SampleLevel(linearSampler, uv + float2(-x1u, 0), 0) +
         src.SampleLevel(linearSampler, uv + float2( x1u, 0), 0)) * (w1 * w0) +
        src.SampleLevel(linearSampler, uv, 0) * (w0 * w0);

    dst[dtid.xy] = s0 + s1 + s2;
}
