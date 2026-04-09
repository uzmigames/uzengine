/// UzEngine bloom threshold — extract bright pixels with Karis average.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — firefly suppression
/// @ref Jimenez 2014 "Next Generation Post-Processing in CoD:AW" — soft threshold

cbuffer BloomThresholdParams : register(b0) {
    uint  g_HdrSrvIndex;
    uint  g_OutputUavIndex;
    uint  g_InputWidth;
    uint  g_InputHeight;
    float g_BloomThreshold;
    float g_Exposure;
    float _pad0, _pad1;
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4>   g_HdrTex    : register(t0);
RWTexture2D<float4> g_OutputTex : register(u0);
#endif

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float KarisW(float3 c) { return 1.0 / (1.0 + Luma(c)); }

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint outW = g_InputWidth / 2, outH = g_InputHeight / 2;
    if (dtid.x >= outW || dtid.y >= outH) return;

#ifdef UZ_BINDLESS
    Texture2D<float4> hdr = ResourceDescriptorHeap[g_HdrSrvIndex];
    RWTexture2D<float4> outTex = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    Texture2D<float4> hdr = g_HdrTex;
    RWTexture2D<float4> outTex = g_OutputTex;
#endif

    float2 invSz = 1.0 / float2(g_InputWidth, g_InputHeight);
    float2 uv = (float2(dtid.xy) * 2.0 + 1.0) * invSz;
    float2 off = 0.5 * invSz;

    float3 s0 = hdr.SampleLevel(linearSampler, uv + float2(-off.x, -off.y), 0).rgb;
    float3 s1 = hdr.SampleLevel(linearSampler, uv + float2( off.x, -off.y), 0).rgb;
    float3 s2 = hdr.SampleLevel(linearSampler, uv + float2(-off.x,  off.y), 0).rgb;
    float3 s3 = hdr.SampleLevel(linearSampler, uv + float2( off.x,  off.y), 0).rgb;

    float w0 = KarisW(s0), w1 = KarisW(s1), w2 = KarisW(s2), w3 = KarisW(s3);
    float3 color = (s0*w0 + s1*w1 + s2*w2 + s3*w3) / (w0+w1+w2+w3);

    float totalLuma = Luma(color) * g_Exposure;
    float knee = g_BloomThreshold * 0.5;
    float soft = max(0.0, totalLuma - g_BloomThreshold + knee);
    float bloomAmt = (knee > 1e-5) ? (soft*soft) / (4.0*knee + 1e-5) : max(0.0, totalLuma - g_BloomThreshold);
    bloomAmt = min(bloomAmt, totalLuma);
    float scale = (totalLuma > 1e-5) ? (bloomAmt / totalLuma) : 0.0;

    outTex[dtid.xy] = float4(color * scale, 1.0);
}
