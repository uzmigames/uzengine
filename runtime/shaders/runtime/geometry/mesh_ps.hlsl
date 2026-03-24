/// Forward mesh pixel shader — Step 1: albedo only, no lighting.
/// Placeholder until deferred pipeline is complete.

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

#ifdef UZ_BINDLESS
struct PushConstants { uint vertexBufferIndex; };
ConstantBuffer<PushConstants> push : register(b0);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj  : packoffset(c0);
    float4   cameraPos           : packoffset(c4);
    float4   timeParams          : packoffset(c5);
    float4   lightDir            : packoffset(c6);
    float4   lightColor          : packoffset(c7);
    float4   ambientColor        : packoffset(c8);
};

cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;
    uint     normalIndex;
    uint     roughnessIndex;
    uint     metallicIndex;
    uint     aoIndex;
    uint     emissiveIndex;
    float    alphaClipThreshold;
    uint     opacitySrvIndex;
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    float3   emissiveColor;
    float    emissiveIntensity;
    float3   baseColorTint;
    float    roughnessMultiplier;
    float    metallicMultiplier;
    float    normalIntensity;
    float2   uvScale;
    float2   uvOffset;
    float    aoIntensity;
    float    matOpacity;
    float    _pomPad[7];
    uint     materialFlags;
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture : register(t0);
#endif
SamplerState linearSampler : register(s0);

float4 main(PSInput input) : SV_Target {
    float2 uv = input.texcoord * uvScale + uvOffset;

    float3 albedo = float3(1.0, 1.0, 1.0);
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo = tex.Sample(linearSampler, uv).rgb;
    }
#else
    albedo = albedoTexture.Sample(linearSampler, uv).rgb;
#endif
    albedo *= baseColorTint;

    // Simple NdotL for minimum visibility
    float3 N = normalize(input.normalWS);
    float3 L = normalize(lightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    float3 color = albedo * (NdotL * lightColor.xyz + ambientColor.xyz);

    // ── Opacity texture modulation (forward/translucent path) ──────────
    float finalOpacity = matOpacity;
    if (opacitySrvIndex > 0) {
        Texture2D<float4> opacityTex = ResourceDescriptorHeap[opacitySrvIndex];
        finalOpacity *= opacityTex.Sample(linearSampler, uv).r;
    }

    return float4(color, finalOpacity);
}
