/// Tonemap pixel shader — applies exposure + ACES filmic tonemap + sRGB gamma.
///
/// Reads the HDR scene color, applies auto-exposure multiplier from PerFrameCB,
/// ACES filmic curve, and linear→sRGB. This is the ONLY place tonemapping happens.
///
/// # UE5 Equivalent
/// PostProcessTonemap.usf — applies eye adaptation exposure + ACES RRT+ODT.

#include "../include/pbr_common.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct TonemapIndices {
    uint hdrColorIdx;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};
ConstantBuffer<TonemapIndices> tonemapIndices : register(b0);
#else
Texture2D<float4> hdrColor : register(t0);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    uint     irradianceSrvIdx;
    uint     prefilteredSrvIdx;
    uint     brdfLutSrvIdx;
    uint     prefilteredMipLevels;
    float    skyLightIntensity;
    float    skyLightColorR;
    float    skyLightColorG;
    float    skyLightColorB;
    float    skyLightMinAmbient;
    float    exposure;
};

SamplerState linearSampler : register(s0);

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

#ifdef UZ_BINDLESS
    Texture2D<float4> hdrTex = ResourceDescriptorHeap[tonemapIndices.hdrColorIdx];
    float3 color = hdrTex.Sample(linearSampler, uv).rgb;
#else
    float3 color = hdrColor.Sample(linearSampler, uv).rgb;
#endif

    // Apply auto-exposure
    color *= exposure;

    // ACES filmic tonemap (HDR → LDR)
    color = ACESFilm(color);

    // Linear → sRGB
    color = LinearToSrgb(color);

    return float4(color, 1.0f);
}
