/// GBuffer debug visualization — stub.
/// Full implementation will be added after the main pipeline is working.

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct DebugIndices {
    uint albedoIdx;
    uint normalIdx;
    uint depthIdx;
    uint velocityIdx;
};
ConstantBuffer<DebugIndices> dbgIndices : register(b0);
cbuffer DebugCB : register(b2) { uint debugMode; };
#else
cbuffer DebugCB : register(b0) {
    uint debugMode;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};
Texture2D<float4> gbufferAlbedo : register(t0);
Texture2D<float4> gbufferNormal : register(t1);
Texture2D<float>  gbufferDepth  : register(t2);
#endif

SamplerState linearSampler : register(s0);

float4 main(FSInput input) : SV_Target {
    int2 px = int2(input.positionCS.xy);
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[dbgIndices.albedoIdx];
    return float4(tex.Load(int3(px, 0)).rgb, 1.0);
#else
    return float4(gbufferAlbedo.Load(int3(px, 0)).rgb, 1.0);
#endif
}
