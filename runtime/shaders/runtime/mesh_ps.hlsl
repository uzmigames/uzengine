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
/// Output: linear HDR color tonemapped via ACES filmic and gamma-corrected to
/// sRGB for the R8G8B8A8_UNORM swapchain backbuffer.

#include "include/pbr_common.hlsli"

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
// Per-Frame Constant Buffer — Multi-Light Layout (768 bytes = 3 x 256)
// ─────────────────────────────────────────────────────────────────────────────

// LightData struct and MAX_LIGHTS are defined here (before cbuffer) because
// lighting.hlsli must be included after shadow_common.hlsli (it calls SampleShadowMap).
#include "include/light_types.hlsli"

/// Per-frame CB at b1 — must match C++ PerFrameMultiLightCB struct exactly.
///
/// Contains camera transforms, time parameters, ambient color, and an array
/// of up to MAX_LIGHTS (8) dynamic lights. Total size: 768 bytes (3 x 256
/// byte aligned) to satisfy cbuffer packing rules.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;     // camera view-projection matrix
    float4   cameraPos;              // xyz = world position, w = unused
    float4   timeParams;             // x = totalTime, y = deltaTime, z = frameCount, w = unused
    float4   ambientColor;           // xyz = ambient radiance, w = unused
    uint     lightCount;             // number of active lights [0, MAX_LIGHTS]
    uint     _pad0a;                 // pad to 16-byte boundary
    uint     _pad0b;
    uint     _pad0c;
    LightData lights[MAX_LIGHTS];    // dynamic light array (64 bytes each = 512 bytes)
    float4   _pad1[8];              // pad to 768 bytes total (8 x 16 = 128 bytes)
};

// Per-object CB at b2 — must match C++ PerObjectCB struct exactly
cbuffer PerObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    uint     albedoIndex;     // SRV heap index for BaseColor map (0 = white default)
    uint     normalIndex;     // SRV heap index for normal map (0 = flat normal)
    uint     roughnessIndex;  // SRV heap index for roughness map (0 = 0.5 default)
    uint     metallicIndex;   // SRV heap index for metallic map (0 = 0.0 default)
    uint     aoIndex;         // SRV heap index for AO map (0 = 1.0 default)
};

/// Cascaded shadow map parameters — must match C++ ShadowDataCB struct exactly.
///
/// Supports 1-4 cascades arranged in a 2x2 atlas layout. Each cascade occupies
/// one quadrant of the shadow depth atlas texture.
cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[4];  // light-space view-projection per cascade
    float4   cascadeSplits;                  // view-space far distance per cascade (x=C0, y=C1, z=C2, w=C3)
    uint     shadowAtlasIndex;               // SRV heap index for shadow depth atlas
    uint     cascadeCount;                   // number of active cascades (1-4)
    float    normalBias;                     // world-space offset along surface normal
    float    depthBias;                      // bias subtracted from shadow depth comparison
    float    shadowMapSize;                  // per-cascade resolution in texels
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float>  shadowAtlasTexture : register(t5);
#endif
SamplerState linearSampler : register(s0);

/// Comparison sampler for percentage-closer filtering on shadow depth maps.
/// Uses LESS comparison — returns 1.0 when the sample passes (fragment is lit).
SamplerComparisonState shadowSampler : register(s1);

#include "include/shadow_common.hlsli"
#include "include/lighting.hlsli"

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
        cameraPos.xyz, cascadeCount);

    // ── Ambient (constant approximation; will be replaced with IBL) ──
    float3 ambient = ambientColor.xyz * albedo * ao;

    // ── Combine ──
    float3 color = ambient + Lo + emissive;

    // ── Tonemap (ACES filmic) + gamma correction ──
    // ACES filmic curve: better highlight rolloff than Reinhard, preserves
    // color saturation in bright areas.
    color = ACESFilm(color);
    // Convert linear to sRGB (approximate gamma 2.2 for UNORM backbuffer).
    color = pow(color, 1.0f / 2.2f);

    return float4(color, 1.0f);
}
