/// Standard mesh pixel shader — Cook-Torrance PBR with directional lighting.
///
/// Implements GGX (Trowbridge-Reitz) NDF, Smith geometry function, and
/// Schlick-Fresnel approximation. Uses the metallic-roughness workflow.
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
/// Lighting inputs come from PerFrameCB (lightDir, lightColor, ambientColor).
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

// Per-frame CB at b1 — must match C++ PerFrameCB struct exactly
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;     // xyz = world position, w = unused
    float4   timeParams;    // x = totalTime, y = deltaTime, z = frameCount, w = unused
    float4   lightDir;      // xyz = direction TOWARD the light (normalized), w = unused
    float4   lightColor;    // xyz = color * intensity (radiance), w = unused
    float4   ambientColor;  // xyz = ambient radiance, w = unused
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
    float3 L = normalize(lightDir.xyz);     // already points TOWARD the light
    float3 H = normalize(V + L);

    // Clamp dot products away from zero to avoid division artifacts
    float NdotL = max(dot(N, L), 0.001f);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // ── PBR parameter preparation ──

    // Perceptual roughness → alpha: squaring linearises the perceived roughness
    // response and is standard practice (Disney PBR, UE4 Karis 2013).
    float alpha = max(roughness * roughness,
                      MIN_ROUGHNESS * MIN_ROUGHNESS);

    // F0: reflectance at 0° incidence.
    // Dielectrics use 0.04 (typical non-metal IOR ≈ 1.5).
    // Metals use their albedo color as the tinted F0 (no diffuse transmission).
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // ── Cook-Torrance specular BRDF ──
    float  D = DistributionGGX(NdotH, alpha);
    float  G = GeometrySmith(NdotV, NdotL, alpha);
    float3 F = FresnelSchlick(VdotH, F0);

    // Specular term: D*G*F / (4 * N·V * N·L)
    // The 4 in the denominator is the Jacobian of the half-vector transformation.
    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

    // ── Energy conservation ──
    // kS is the specular reflectance fraction (Fresnel).
    // kD is the remaining diffuse fraction, zeroed for metals (no diffuse lobe).
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    // Lambertian diffuse: albedo / PI normalises for energy conservation.
    float3 diffuse = kD * albedo / PI;

    // ── Shadow ──
    // Skip shadow sampling when no cascades are active (shadows disabled).
    float shadow = 1.0f;
    if (cascadeCount > 0) {
        float viewDepth = length(input.positionWS - cameraPos.xyz);
        shadow = SampleShadowMap(input.positionWS, N, viewDepth);
    }

    // ── Direct lighting ──
    // Outgoing radiance Lo = BRDF * radiance * N·L, attenuated by shadow
    float3 Lo = (diffuse + specular) * lightColor.xyz * NdotL * shadow;

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
