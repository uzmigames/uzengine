// ssao_blur.ps.hlsl -- Bilateral blur for SSAO
//
// 4x4 depth-aware blur that smooths SSAO noise while preserving edges.
// Samples in a 4x4 area with depth-based weight rejection.

cbuffer BlurParams : register(b0) {
    float2 texelSize;     // 1.0 / ssaoRT resolution
    float  depthThreshold; // depth similarity threshold (default 0.05)
    float  _blurPad;
};

Texture2D<float> ssaoTex  : register(t0);
Texture2D<float> depthTex : register(t1);
SamplerState     linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float centerDepth = depthTex.SampleLevel(linearClamp, input.texcoord, 0).r;
    float totalAO     = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            float2 offset = float2(x, y) * texelSize;
            float2 sampleUV = input.texcoord + offset;

            float ao    = ssaoTex.SampleLevel(linearClamp, sampleUV, 0).r;
            float depth = depthTex.SampleLevel(linearClamp, sampleUV, 0).r;

            // Bilateral weight: reject samples with very different depth
            float depthDiff = abs(depth - centerDepth);
            float weight = exp(-depthDiff / max(depthThreshold, 0.001));

            // Spatial gaussian weight (simple box approximation)
            weight *= 1.0;

            totalAO     += ao * weight;
            totalWeight += weight;
        }
    }

    float result = totalAO / max(totalWeight, 0.001);
    return float4(result, result, result, 1.0);
}
