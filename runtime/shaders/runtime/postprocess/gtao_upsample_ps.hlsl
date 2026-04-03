/// UzEngine GTAO Bilateral Upsample — half-res AO → full-res with depth-aware filtering.
/// @ref Jimenez 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion"
/// @constants depthSimilarity threshold = 0.003

cbuffer GTAOUpsampleRootCB : register(b0) {
    uint   halfResAOSrvIndex;     // 0
    uint   halfResDepthSrvIndex;  // 1
    uint   fullResDepthSrvIndex;  // 2
    uint   _pad0;                 // 3
    float  halfResTexelSizeX;     // 4
    float  halfResTexelSizeY;     // 5
    float  fullResTexelSizeX;     // 6
    float  fullResTexelSizeY;     // 7
};

SamplerState linearClamp : register(s0);
SamplerState pointClamp  : register(s1);

float DepthSimilarity(float dA, float dB) {
    return saturate(1.0 - abs(dA - dB) * 0.003);
}

float4 main(float4 posCS : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    Texture2D<float> halfAOTex    = ResourceDescriptorHeap[halfResAOSrvIndex];
    Texture2D<float> halfDepthTex = ResourceDescriptorHeap[halfResDepthSrvIndex];
    Texture2D<float> fullDepthTex = ResourceDescriptorHeap[fullResDepthSrvIndex];

    float2 halfTexelSize = float2(halfResTexelSizeX, halfResTexelSizeY);

    // Full-res depth at this pixel
    float fullDepth = fullDepthTex.SampleLevel(pointClamp, uv, 0).r;

    // 4 diagonal corners in half-res space at ±0.25 texel offset
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
        // Fallback: conservative min of all 4 samples (prevents halo lightening)
        ao = 1.0;
        [unroll]
        for (uint j = 0; j < 4; j++) {
            float2 sampleUV = uv + offsets[j] * halfTexelSize;
            ao = min(ao, halfAOTex.SampleLevel(linearClamp, sampleUV, 0).r);
        }
    }

    return float4(ao, ao, ao, 1.0);
}
