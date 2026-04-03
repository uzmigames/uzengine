/// UzEngine SSR Resolve — pass-through with roughness fade.
/// The trace pass already outputs (hitColor * confidence, confidence).
/// Resolve applies additional roughness-based filtering if needed.
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections"

#include "../common/encoding.hlsli"

cbuffer SsrResolveRootCB : register(b0) {
    uint   ssrTraceSrvIdx;     // 0
    uint   normalSrvIdx;       // 1
    uint   depthSrvIdx;        // 2
    uint   reserved;           // 3
    uint   outputWidth;        // 4
    uint   outputHeight;       // 5
    float  intensity;          // 6
    float  roughnessMaskScale; // 7
};

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
};

RWTexture2D<float4> outputSSR : register(u0);
SamplerState linearClamp : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    Texture2D<float4> ssrTraceTex = ResourceDescriptorHeap[ssrTraceSrvIdx];
    Texture2D<float4> normalTex   = ResourceDescriptorHeap[normalSrvIdx];
    Texture2D<float>  depthTex    = ResourceDescriptorHeap[depthSrvIdx];

    float deviceZ = depthTex.SampleLevel(linearClamp, uv, 0).r;
    if (deviceZ <= 0.0001) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 traceResult = ssrTraceTex.SampleLevel(linearClamp, uv, 0);

    // Read roughness from GBuffer (normalMatPbr.a)
    float4 normalData = normalTex.Load(int3(DTid.xy, 0));
    float  roughness  = max(normalData.a, 0.04);

    // Roughness-based blur: sample 4 neighbours for rough surfaces
    // For smooth surfaces (roughness < 0.2), pass through directly
    float3 color = traceResult.rgb;
    float  alpha = traceResult.a;

    if (roughness > 0.2 && alpha > 0.0) {
        // Simple 4-tap cross blur for rough reflections
        float blurRadius = roughness * 2.0; // px
        float3 colorSum = color;
        float  alphaSum = alpha;
        float  weightSum = 1.0;

        static const float2 offsets[4] = {
            float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1)
        };

        [unroll]
        for (uint i = 0; i < 4; i++) {
            float2 sampleUV = uv + offsets[i] * texelSize * blurRadius;
            float4 s = ssrTraceTex.SampleLevel(linearClamp, sampleUV, 0);
            float w = 0.5;
            colorSum += s.rgb * w;
            alphaSum += s.a * w;
            weightSum += w;
        }

        color = colorSum / weightSum;
        alpha = alphaSum / weightSum;
    }

    outputSSR[DTid.xy] = float4(color, alpha);
}
