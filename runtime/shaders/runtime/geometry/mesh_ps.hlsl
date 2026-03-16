/// Standard mesh pixel shader — Cook-Torrance PBR with multi-light support.
///
/// Implements GGX (Trowbridge-Reitz) NDF, Smith geometry function, and
/// Schlick-Fresnel approximation. Uses the metallic-roughness workflow.
/// Supports up to 8 dynamic lights: directional, point, and spot.
///
/// SM6.6 (UZ_BINDLESS): samples textures via ResourceDescriptorHeap[index].
/// SM5 (!UZ_BINDLESS): samples textures via register-bound textures t0-t4.
///
/// # Texture Maps (all optional — 0 index = use default value)
/// - albedoIndex:    BaseColor map (sRGB) → linear albedo
/// - normalIndex:    Tangent-space normal map → perturbed world normal via TBN
/// - roughnessIndex: Roughness map (R channel) → perceptual roughness
/// - metallicIndex:  Metallic map (R channel) → metallic factor
/// - aoIndex:        Ambient occlusion map (R channel) → AO multiplier
///
/// Lighting inputs come from PerFrameCB (lights array, ambientColor).
/// Shadow mapping is applied to the first directional light only.
///
/// Output: linear HDR color. Tonemapping and gamma correction are handled by
/// the tonemap_ps.hlsl fullscreen pass (reads HDR scene color → LDR backbuffer).

#include "../include/pbr_common.hlsli"
#include "../include/ibl.hlsli"
#include "../include/sh_irradiance.hlsli"

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

#ifdef UZ_BINDLESS
// Root constants at b0 (used by VS for vertex fetch; PS reads materialIndex from b2)
struct PushConstants {
    uint vertexBufferIndex;
};
ConstantBuffer<PushConstants> push : register(b0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Constant Buffer — Multi-Light Layout (1024 bytes = 4 x 256)
// ─────────────────────────────────────────────────────────────────────────────

// LightData struct and MAX_LIGHTS are defined here (before cbuffer) because
// lighting.hlsli must be included after shadow_common.hlsli (it calls SampleShadowMap).
#include "../include/light_types.hlsli"

/// Per-frame CB at b1 — must match C++ PerFrameMultiLightCB struct exactly.
///
/// Contains camera transforms, time parameters, ambient color, an array
/// of up to MAX_LIGHTS (8) dynamic lights, IBL descriptor indices,
/// SkyLight parameters, and L2 SH coefficients for diffuse ambient.
/// Total size: 1024 bytes (4 x 256 byte aligned) to satisfy cbuffer packing rules.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;     // camera view-projection matrix         [  0.. 63]
    float4   cameraPos;              // xyz = world position, w = unused       [ 64.. 79]
    float4   timeParams;             // x = totalTime, y = deltaTime, ...      [ 80.. 95]
    float4   ambientColor;           // xyz = ambient radiance, w = unused     [ 96..111]
    uint     lightCount;             // number of active lights [0, MAX_LIGHTS] [112..115]
    uint     _pad0a;                 // pad to 16-byte boundary                [116..119]
    uint     _pad0b;                 //                                         [120..123]
    uint     _pad0c;                 //                                         [124..127]
    LightData lights[MAX_LIGHTS];    // dynamic light array (64B each = 512B)  [128..639]
    uint     irradianceSrvIndex;     // SRV index for irradiance cubemap       [640..643]
    uint     prefilteredSrvIndex;    // SRV index for prefiltered specular     [644..647]
    uint     brdfLutSrvIndex;        // SRV index for BRDF LUT                [648..651]
    uint     prefilteredMipLevels;   // mip count for prefiltered cubemap LOD  [652..655]
    float    skyLightIntensity;      // multiplier for IBL ambient (SkyLight)  [656..659]
    float    skyLightColorR;         // sky light tint R                       [660..663]
    float    skyLightColorG;         // sky light tint G                       [664..667]
    float    skyLightColorB;         // sky light tint B                       [668..671]
    float    skyLightMinAmbient;     // minimum ambient floor (0-1)            [672..675]
    float    _pad1[3];              // align SH array to 16-byte boundary      [676..687]

    // ── Sky Irradiance SH (L2, 7 float4 = 112 bytes) ──
    // Matches UE5 SkyIrradianceEnvironmentMap[0..6].
    float4   skyIrradianceSH[7];     // L2 SH for sky diffuse irradiance       [688..799]

    float    _pad2[56];             // pad to 1024 bytes (4 x 256)             [800..1023]
};

// Per-object CB at b2 — scalars first to avoid SPIRV std140 offset corruption
cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;     // SRV heap index for BaseColor map (0 = white default)
    uint     normalIndex;     // SRV heap index for normal map (0 = flat normal)
    uint     roughnessIndex;  // SRV heap index for roughness map (0 = 0.5 default)
    uint     metallicIndex;   // SRV heap index for metallic map (0 = 0.0 default)
    uint     aoIndex;         // SRV heap index for AO map (0 = 1.0 default)
    float3   _pad;
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
};

/// Cascaded shadow map parameters — must match C++ ShadowDataCB struct exactly.
/// Supports up to 8 cascades (4 main + 4 far shadow cascades).
/// UE5 parity: per-cascade bias, fade starts, receiver bias, distance fade.
cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[8];  // light-space view-projection per cascade
    float    cascadeSplits[8];               // view-space extended far split per cascade
    float    cascadeFadeStarts[8];           // view-space original (un-extended) far split per cascade
    float    cascadeDepthBias[8];            // per-cascade computed depth bias
    float    cascadeNormalBias[8];           // per-cascade computed normal bias
    uint     shadowAtlasIndex;               // SRV heap index for shadow depth atlas (dynamic)
    uint     cascadeCount;                   // number of active cascades (1-8, main + far)
    float    shadowMapSize;                  // per-cascade resolution in texels
    float    receiverBias;                   // shadow receiver bias (UE5 default: 0.9)
    float    shadowFadeStart;                // world distance where last-cascade fade begins
    float    shadowFadeEnd;                  // world distance where shadows fully disappear
    uint     filterMethod;                   // 0=5x5 PCF, 1=Poisson 16-tap, 2=PCSS
    float    tanLightSourceAngle;            // tan(sun disk angle) for PCSS
    float    maxSoftKernelSize;              // max PCSS kernel radius in texels
    uint     atlasColumns;                   // number of columns in the atlas grid (2, 3, or 4)
    uint     staticShadowAtlasIndex;         // SRV index for cached static shadow atlas. 0xFFFFFFFF = no cache.
    uint     useShadowCache;                 // 0 = no cache (single atlas), 1 = composite static + dynamic.
    uint     dfsEnabled;                     // 1 = Distance Field Shadows active, 0 = disabled.
    uint     dfsOutputSrvIndex;              // SRV index for DFS result texture. 0xFFFFFFFF = not bound.
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float>  shadowAtlasTexture : register(t5);
TextureCube<float3> irradianceCubemap  : register(t6);
TextureCube<float3> prefilteredCubemap : register(t7);
Texture2D<float2>  brdfLutTexture     : register(t8);
#endif
SamplerState linearSampler : register(s0);

/// Comparison sampler for percentage-closer filtering on shadow depth maps.
/// Uses LESS comparison — returns 1.0 when the sample passes (fragment is lit).
SamplerComparisonState shadowSampler : register(s1);

#include "../include/shadow_common.hlsli"
#include "../include/lighting.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[earlydepthstencil]
float4 main(PSInput input) : SV_Target {

    // ── Material parameters (defaults when no texture is bound) ──
    float3 albedo    = float3(1.0f, 1.0f, 1.0f);
    float  metallic  = 0.0f;
    float  roughness = 0.5f;
    float  ao        = 1.0f;
    float3 emissive  = float3(0.0f, 0.0f, 0.0f);

    float2 uv = input.texcoord;

    // ── Sample PBR texture maps (index 0 = use default value) ──
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo = tex.Sample(linearSampler, uv).rgb;
    }
    if (roughnessIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[roughnessIndex];
        roughness = tex.Sample(linearSampler, uv).r;
    }
    if (metallicIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[metallicIndex];
        metallic = tex.Sample(linearSampler, uv).r;
    }
    if (aoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[aoIndex];
        ao = tex.Sample(linearSampler, uv).r;
    }
#else
    if (albedoIndex > 0)    albedo    = albedoTexture.Sample(linearSampler, uv).rgb;
    if (roughnessIndex > 0) roughness = roughnessTexture.Sample(linearSampler, uv).r;
    if (metallicIndex > 0)  metallic  = metallicTexture.Sample(linearSampler, uv).r;
    if (aoIndex > 0)        ao        = aoTexture.Sample(linearSampler, uv).r;
#endif

    // ── Surface normal (with optional tangent-space normal mapping) ──
    float3 N = normalize(input.normalWS);

    // Apply normal map via TBN matrix when a normal texture is bound
    if (normalIndex > 0) {
        float3 T = normalize(input.tangentWS.xyz);
        float3 B = cross(N, T) * input.tangentWS.w; // w = handedness
        float3x3 TBN = float3x3(T, B, N);

        float4 normalSample;
#ifdef UZ_BINDLESS
        Texture2D<float4> nrmTex = ResourceDescriptorHeap[normalIndex];
        normalSample = nrmTex.Sample(linearSampler, uv);
#else
        normalSample = normalTexture.Sample(linearSampler, uv);
#endif
        // Remap from [0,1] to [-1,1] tangent space
        float3 normalTS;
        normalTS.xy = normalSample.rg * 2.0f - 1.0f;

        // BC5 two-channel normal maps store only RG; reconstruct Z from unit length
        // constraint. For standard RGB normal maps, the sampled B channel is used.
        normalTS.z = (normalSample.b > 0.0f)
            ? normalSample.b * 2.0f - 1.0f
            : sqrt(max(1.0f - dot(normalTS.xy, normalTS.xy), 0.0f));
        N = normalize(mul(normalTS, TBN));
    }

    float3 V = normalize(cameraPos.xyz - input.positionWS);

    // NdotV depends only on N and V — computed once, shared across all lights
    float NdotV = max(dot(N, V), 0.001f);

    // ── PBR parameter preparation ──

    // Perceptual roughness → alpha: squaring linearises the perceived roughness
    // response and is standard practice (Disney PBR, UE4 Karis 2013).
    float alpha = max(roughness * roughness,
                      MIN_ROUGHNESS * MIN_ROUGHNESS);

    // F0: reflectance at 0° incidence.
    // Dielectrics use 0.04 (typical non-metal IOR ≈ 1.5).
    // Metals use their albedo color as the tinted F0 (no diffuse transmission).
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // ── Multi-light direct lighting ──
    float3 Lo = AccumulateDirectLighting(
        input.positionWS, N, V, NdotV,
        albedo, metallic, alpha, F0,
        lightCount, lights,
        cameraPos.xyz, cascadeCount, input.positionCS.xy);

    // ── Ambient / IBL (SH9 diffuse + split-sum specular) ──
    // Matches deferred_lighting_ps.hlsl ambient quality: L2 SH for diffuse,
    // prefiltered cubemap + BRDF LUT for specular, skyLight tint/intensity.
    float3 ambient;

#ifdef UZ_BINDLESS
    if (irradianceSrvIndex != 0xFFFFFFFF) {
        // Diffuse IBL via L2 Spherical Harmonics (UE5 parity)
        // SH is much higher quality than 32x32 cubemap sampling
        float3 irradiance = GetSkySHDiffuse(N, skyIrradianceSH);

        // Fresnel: metals reflect more at grazing, dielectrics diffuse more
        float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD = (1.0f - F) * (1.0f - metallic);

        float3 diffuseIBL = kD * albedo * irradiance;

        // Specular IBL — sample prefiltered environment map along reflection
        float3 R = reflect(-V, N);
        TextureCube<float4> prefilteredMap = ResourceDescriptorHeap[prefilteredSrvIndex];
        float mipLevel = roughness * float(prefilteredMipLevels - 1);
        float3 prefilteredColor = prefilteredMap.SampleLevel(linearSampler, R, mipLevel).rgb;

        // BRDF LUT: pre-integrated scale and bias terms for split-sum approximation
        Texture2D<float2> brdfLut = ResourceDescriptorHeap[brdfLutSrvIndex];
        float2 envBRDF = brdfLut.SampleLevel(linearSampler, float2(NdotV, roughness), 0);
        float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        float3 skyLightTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
        ambient = (diffuseIBL + specularIBL) * ao * skyLightIntensity * skyLightTint;
    } else
#endif
    {
        // SM5 fallback or no IBL: irradiance cubemap + specular IBL
        float3 irradiance;
        float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD = (1.0f - F) * (1.0f - metallic);

#ifndef UZ_BINDLESS
        if (irradianceSrvIndex != 0xFFFFFFFF) {
            irradiance = irradianceCubemap.Sample(linearSampler, N).rgb;
            float3 diffuseIBL = kD * albedo * irradiance;

            // Specular IBL
            float3 R = reflect(-V, N);
            float mipLevel = roughness * float(prefilteredMipLevels - 1);
            float3 prefilteredColor = prefilteredCubemap.SampleLevel(linearSampler, R, mipLevel).rgb;

            float2 envBRDF = brdfLutTexture.Sample(linearSampler, float2(NdotV, roughness)).rg;
            float3 specularIBL = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

            float3 skyLightTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
            ambient = (diffuseIBL + specularIBL) * ao * skyLightIntensity * skyLightTint;
        } else
#endif
        {
            // Fallback: constant ambient (no IBL data available)
            float3 skyLightTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
            ambient = ambientColor.xyz * albedo * ao * skyLightIntensity * skyLightTint;
        }
    }

    // ── Minimum ambient floor (SkyLight minOcclusion equivalent) ──
    // Ensures shadowed surfaces never go fully black — adds a minimum fill light.
    if (skyLightMinAmbient > 0.0f) {
        float3 minFloor = albedo * skyLightMinAmbient;
        ambient = max(ambient, minFloor);
    }

    // ── Combine ──
    float3 color = ambient + Lo + emissive;

    // Output is linear HDR — tonemap_ps.hlsl handles ACES filmic + sRGB conversion.
    return float4(color, 1.0f);
}
