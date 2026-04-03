/// UzEngine GTAO Denoise — 4-tap bilateral spatial filter.
/// @ref Jimenez 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion"
/// @constants depthSimilarity threshold = 0.003 (inverse)

cbuffer DenoiseParams : register(b0) {
    float  texelSizeX;       // 0
    float  texelSizeY;       // 1
    float  nearPlane;        // 2
    float  farPlane;         // 3
#ifdef UZ_BINDLESS
    uint   rawAOSrvIndex;    // 4
    uint   depthSrvIndex;    // 5
#else
    uint   _srvPad0;         // 4
    uint   _srvPad1;         // 5
#endif
    uint   _pad0;            // 6
    uint   _pad1;            // 7
};

RWTexture2D<float> filteredAO : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float> t_rawAO : register(t0);
Texture2D<float> t_depth : register(t1);
#endif
SamplerState linearClamp : register(s0);

float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (nearPlane + deviceZ * (farPlane - nearPlane));
}

/// Bilateral depth similarity weight.
/// @ref Jimenez 2016 — threshold 0.003 → weight drops to 0 when |dA-dB| > ~333 cm
float DepthSimilarity(float dA, float dB) {
    return saturate(1.0 - abs(dA - dB) * 0.003);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint w, h;
    filteredAO.GetDimensions(w, h);
    if (DTid.x >= w || DTid.y >= h) return;

    float2 texelSize = float2(texelSizeX, texelSizeY);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

#ifdef UZ_BINDLESS
    Texture2D<float> rawAOTex = ResourceDescriptorHeap[rawAOSrvIndex];
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIndex];
#else
    Texture2D<float> rawAOTex = t_rawAO;
    Texture2D<float> depthTex = t_depth;
#endif

    float centerAO    = rawAOTex.SampleLevel(linearClamp, uv, 0).r;
    float centerDepth = LinearizeDepth(depthTex.SampleLevel(linearClamp, uv, 0).r);

    // 4 diagonal offsets at ±1 texel — covers 4×4 block via bilinear
    static const float2 offsets[4] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0)
    };

    float aoSum    = centerAO;
    float weightSum = 1.0;

    [unroll]
    for (uint i = 0; i < 4; i++) {
        float2 sampleUV    = uv + offsets[i] * texelSize;
        float  sampleAO    = rawAOTex.SampleLevel(linearClamp, sampleUV, 0).r;
        float  sampleDepth = LinearizeDepth(depthTex.SampleLevel(linearClamp, sampleUV, 0).r);

        float dw = DepthSimilarity(centerDepth, sampleDepth);
        aoSum    += sampleAO * dw;
        weightSum += dw;
    }

    filteredAO[DTid.xy] = aoSum / max(weightSum, 0.0001);
}
