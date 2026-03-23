/// Deferred lighting — Step 2: albedo + NdotL directional.

#include "../include/octahedral.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct GBufferIndices {
    uint albedoIdx;
    uint normalIdx;
    uint depthIdx;
    uint positionIdx;
    uint emissiveIdx;
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);
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
    uint     irradianceSrvIndex;
    uint     prefilteredSrvIndex;
    uint     brdfLutSrvIndex;
    uint     prefilteredMipLevels;
    float    skyLightIntensity;
    float    skyLightColorR;
    float    skyLightColorG;
    float    skyLightColorB;
    float    skyLightMinAmbient;
    float    exposure;
    float4   skyIrradianceSH[7];
    uint     shadowDebugMode;
    uint     aerialPerspectiveEnabled;
    uint     transmittanceLutSrvIndex;
    float    apBottomRadiusKm;
    float    apTopRadiusKm;
    float    apRayleighBetaX;
    float    apRayleighBetaY;
    float    apRayleighBetaZ;
    float    apRayleighScaleH;
    float    apMieBeta;
    float    apMieScaleH;
    float    apMieAnisotropy;
    float    apSunIntensity;
    float    apRayleighScale;
    float    apMieScale;
    float    filmSlope;
    float    filmToe;
    float    filmShoulder;
    float    filmBlackClip;
    float    filmWhiteClip;
    uint     gtaoSrvIndex;
    uint     brdfDebugMode;
    float    heightFogContribution;
    float    filmGrainIntensity;
    float    vignetteIntensity;
    float    chromaAbIntensity;
    float    contactShadowLength;
    uint     ssrSrvIndex;
    float    mipBias;
    uint     ssgiSrvIndex;
    uint     ssgiAOSrvIndex;
    uint     ssgiEnabled;
    uint     radianceCacheEnabled;
    uint     rcProbeSHBufferSrv;
    uint     rcProbesPerAxis;
    float    rcGridExtent;
    float    rcGridOriginX;
    float    rcGridOriginY;
    float    rcGridOriginZ;
    float    rcGiIntensity;
    uint     apVolumeSrvIndex;
    uint     reflectionProbeCount;
    uint     reflectionProbeBufferSrvIndex;
    float    _framePad2[57];
};

#ifndef UZ_BINDLESS
Texture2D<float4> gbufferAlbedo   : register(t0);
Texture2D<float4> gbufferNormal   : register(t1);
Texture2D<float>  gbufferDepth    : register(t2);
Texture2D<float>  shadowAtlasTexture : register(t3);
Texture2D<float4> gbufferPositionWS : register(t4);
TextureCube<float4> iblIrradiance : register(t5);
TextureCube<float4> iblPrefiltered : register(t6);
Texture2D<float2>  iblBrdfLut    : register(t7);
Texture2D<float4>  transmittanceLutTex : register(t8);
Texture2D<float>   gtaoTexture   : register(t9);
Texture2D<float4>  gbufferEmissive : register(t10);
Texture2D<float>   localShadowAtlasTexture : register(t11);
#endif

SamplerState linearSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);

float3 ReconstructWorldPosition(float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 wp = mul(float4(ndc, depth, 1.0), invViewProj);
    return wp.xyz / wp.w;
}

float4 main(FSInput input) : SV_Target {
    int2 px = int2(input.positionCS.xy);

    // ── GBuffer Load ──────────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> gbAlbedo = ResourceDescriptorHeap[gBufIndices.albedoIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[gBufIndices.normalIdx];
    Texture2D<float>  gbDepth  = ResourceDescriptorHeap[gBufIndices.depthIdx];

    float3 albedo = gbAlbedo.Load(int3(px, 0)).rgb;
    float4 rt1    = gbNormal.Load(int3(px, 0));
    float  z      = gbDepth.Load(int3(px, 0)).r;
#else
    float3 albedo = gbufferAlbedo.Load(int3(px, 0)).rgb;
    float4 rt1    = gbufferNormal.Load(int3(px, 0));
    float  z      = gbufferDepth.Load(int3(px, 0)).r;
#endif

    // ── Decode normal ─────────────────────────────────────────────────────
    float3 N = OctDecode(rt1.rg);

    // ── Reconstruct world position ────────────────────────────────────────
    float3 worldPos = ReconstructWorldPosition(input.texcoord, z);

    // ── NdotL directional light ───────────────────────────────────────────
    float3 L = normalize(lightDir.xyz);
    float  NdotL = max(dot(N, L), 0.0);

    // DEBUG: visualize NdotL as grayscale
    float3 L = normalize(float3(0.5, -0.5, 0.7));
    float  NdotL = dot(N, L); // NOT clamped — see if negative
    float3 color = float3(max(NdotL, 0.0), max(-NdotL, 0.0), 0.0); // R=lit, G=backlit

    return float4(color, 1.0);
}
