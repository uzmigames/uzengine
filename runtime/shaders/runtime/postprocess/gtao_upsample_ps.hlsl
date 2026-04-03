/// UzEngine GTAO Bilateral Upsample — half-res AO → full-res with depth-aware filtering.
/// @ref Jimenez 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion"
/// @constants depthSimilarity threshold = 0.003

cbuffer GTAOUpsampleRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   halfResAOSrvIndex;     // 0
    uint   halfResDepthSrvIndex;  // 1
    uint   fullResDepthSrvIndex;  // 2
#else
    uint   _srvPad0;              // 0
    uint   _srvPad1;              // 1
    uint   _srvPad2;              // 2
#endif
    uint   _pad0;                 // 3
    float  halfResTexelSizeX;     // 4
    float  halfResTexelSizeY;     // 5
    float  fullResTexelSizeX;     // 6
    float  fullResTexelSizeY;     // 7
};

#ifndef UZ_BINDLESS
Texture2D<float> t_halfAO    : register(t0);
Texture2D<float> t_halfDepth : register(t1);
Texture2D<float> t_fullDepth : register(t2);
#endif
SamplerState linearClamp : register(s0);
SamplerState pointClamp  : register(s1);

float DepthSimilarity(float dA, float dB) {
    return saturate(1.0 - abs(dA - dB) * 0.003);
}

float4 main(float4 posCS : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
#ifdef UZ_BINDLESS
    Texture2D<float> halfAOTex    = ResourceDescriptorHeap[halfResAOSrvIndex];
    Texture2D<float> halfDepthTex = ResourceDescriptorHeap[halfResDepthSrvIndex];
    Texture2D<float> fullDepthTex = ResourceDescriptorHeap[fullResDepthSrvIndex];
#else
    Texture2D<float> halfAOTex    = t_halfAO;
    Texture2D<float> halfDepthTex = t_halfDepth;
    Texture2D<float> fullDepthTex = t_fullDepth;
#endif

    float2 halfTexelSize = float2(halfResTexelSizeX, halfResTexelSizeY);

    float fullDepth = fullDepthTex.SampleLevel(pointClamp, uv, 0).r;

    static const float2 offsets[4] = {
        float2(-0.25, -0.25), float2(0.25, -0.25),
        float2(-0.25,  0.25), float2(0.25,  0.25)
    };

    float aoSum    = 0.0;
    float weightSum = 0.0;

    [unroll]
    for (uint i = 0; i < 4; i++) {
        float2 sampleUV = uv + offsets[i] * halfTexelSize;
        float  sampleAO    = halfAOTex.SampleLevel(linearClamp, sampleUV, 0).r;
        float  sampleDepth = halfDepthTex.SampleLevel(pointClamp, sampleUV, 0).r;

        float w = DepthSimilarity(fullDepth, sampleDepth);
        aoSum    += sampleAO * w;
        weightSum += w;
    }

    float ao;
    if (weightSum > 0.0001) {
        ao = aoSum / weightSum;
    } else {
        ao = 1.0;
        [unroll]
        for (uint j = 0; j < 4; j++) {
            float2 sampleUV = uv + offsets[j] * halfTexelSize;
            ao = min(ao, halfAOTex.SampleLevel(linearClamp, sampleUV, 0).r);
        }
    }

    return float4(ao, ao, ao, 1.0);
}
