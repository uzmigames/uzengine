/// UzEngine reflection-probe capture pixel shader (phase3a 3.37.0, item 1.10).
///
/// Lights the geometry a reflection probe captures the way deferred_lighting_ps.hlsl lights the
/// frame: the sun with the Frostbite diffuse + GGX specular BRDF, through a cascaded shadow map
/// rendered for this capture alone (ShadowDataCB at b4, built around the probe by
/// ReflectionProbeRenderer::RenderCaptureShadowAtlas), the sky light (L2 SH irradiance, the
/// split-sum prefiltered specular with its multi-scatter compensation, AO, horizon and specular
/// occlusion) and the material's emissive. What the frame layers on top of these from screen-space
/// or GI inputs - local lights, the GI solvers, SSR, contact and cloud shadows - is not here: a probe
/// reflection is a secondary term, and those need the frame's screen-space passes or cluster lists.
///
/// Until this shader the capture ran mesh_ps.hlsl, `albedo * (sun N.L + ambient)`: no shadow, no
/// 1 / pi, and an ambient that is zero once the sky IBL is baked. Reflected walls facing the sun came
/// out 8.6-9 x brighter than the frame lit them and every other wall black.
///
/// Input matches mesh_vs output. The capture draws with the frame's own PerFrameCB, per face
/// overriding the camera fields with the face's view-projection and the probe position, so the sun,
/// sky and CVar fields read below are the frame's.

#include "../common/constants.hlsli"
#include "../material/brdf.hlsli"
#include "../material/diffuse.hlsli"
#include "ibl.hlsli"

/// Sky cubemap convention (deferred_lighting_ps.hlsl, skybox_cubemap_ps.hlsl).
float3 WorldToCubemap(float3 dir) { return float3(dir.x, dir.z, dir.y); }

// PerFrameCB (b1): the fields this pass reads, at the byte offsets of PerFrameCB in
// engine/renderer/core/params/constant_buffers.h (deferred_lighting_ps.hlsl lists them all).
// Members are declared in ascending packoffset order: DXC's SPIR-V backend lays the block out in
// declaration order and rejects a packoffset below the previous member's.
cbuffer PerFrameCB : register(b1) {
    float4 cameraPos                 : packoffset(c4);     // (64)   the probe position during a capture
    float4 lightDir                  : packoffset(c6);     // (96)   direction toward the sun
    float4 lightColor                : packoffset(c7);     // (112)  sun colour x intensity
    uint   irradianceSrvIndex        : packoffset(c13.z);  // (216)  sky irradiance cubemap (SH fallback)
    uint   prefilteredSrvIndex       : packoffset(c13.w);  // (220)  sky prefiltered specular cubemap
    uint   prefilteredMipLevels      : packoffset(c14.y);  // (228)  low byte: mips; bit 8: PMREM curve
    float  skyLightIntensity         : packoffset(c14.z);  // (232)
    float  skyLightColorR            : packoffset(c14.w);  // (236)
    float  skyLightColorG            : packoffset(c15.x);  // (240)
    float  skyLightColorB            : packoffset(c15.y);  // (244)
    float  skyLightMinAmbient        : packoffset(c15.z);  // (248)
    float4 skyIrradianceSH[7]        : packoffset(c16);    // (256)  L2 SH, cosine-convolved (irradiance)
    float  mipBias                   : packoffset(c30);    // (480)
    float  shadowPCFPenumbra         : packoffset(c44.y);  // (708)  read by shadow.hlsli
    float  materialDielectricF0      : packoffset(c47.y);  // (756)
    float  materialMinGGXAlpha       : packoffset(c47.z);  // (760)
    float  indirectAOExponent        : packoffset(c53);    // (848)
    uint   materialMapMask           : packoffset(c60.w);  // (972)  diagnostic map switches
    float  materialRoughnessOverride : packoffset(c63.y);  // (1012) diagnostic (-1 = off)
    float  materialMetallicOverride  : packoffset(c63.z);  // (1016) diagnostic (-1 = off)
};

// PerObjectCB (b2): the material, as gbuffer_ps.hlsl reads it.
cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;
    uint     normalIndex;
    uint     roughnessIndex;
    uint     metallicIndex;
    uint     aoIndex;
    uint     emissiveIndex;
    float    alphaClipThreshold;
    uint     opacitySrvIndex;
    row_major float4x4 obj_model;
    row_major float4x4 obj_normalMatrix;
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
    float    displacementScale;
    uint     heightmapSrvIndex;
    float    pomMinLayers;
    float    pomMaxLayers;
    float    pomFadeStart;
    float    pomFadeEnd;
    float    pomSelfShadow;
    uint     materialFlags;
};

// shadow.hlsli declares ShadowDataCB (b4) and the comparison sampler (s1); it reads
// shadowPCFPenumbra from the PerFrameCB above, so it is included after it.
#include "shadow.hlsli"

#ifndef UZ_BINDLESS
// SM5: the material textures at the gbuffer_ps.hlsl registers, the capture's shadow atlas after them.
Texture2D<float4> albedoTexture      : register(t0);
Texture2D<float4> normalTexture      : register(t1);
Texture2D<float4> roughnessTexture   : register(t2);
Texture2D<float4> metallicTexture    : register(t3);
Texture2D<float4> aoTexture          : register(t4);
Texture2D<float4> emissiveTexture    : register(t5);
Texture2D<float>  shadowAtlasTexture : register(t7);
#endif

SamplerState linearSampler : register(s0);

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target {
    const float2 uv = input.texcoord * uvScale + uvOffset;

    // ── Material (the fixed-PBR path of gbuffer_ps.hlsl) ────────────────────────────────────
    float3 albedo = float3(1, 1, 1);
    float  albedoAlpha = 1.0;
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        const float4 albedoSample = tex.SampleBias(linearSampler, uv, mipBias);
        albedo = albedoSample.rgb;
        albedoAlpha = albedoSample.a;
    }
#else
    {
        const float4 albedoSample = albedoTexture.SampleBias(linearSampler, uv, mipBias);
        albedo = albedoSample.rgb;
        albedoAlpha = albedoSample.a;
    }
#endif
    albedo *= baseColorTint;

    float metallic  = 0.0;
    float roughness = 1.0;
    float ao        = 1.0;
    const bool isORM = (materialFlags & 0x4u) != 0;
#ifdef UZ_BINDLESS
    if (isORM && aoIndex > 0) {
        Texture2D<float4> ormTex = ResourceDescriptorHeap[aoIndex];
        const float3 orm = ormTex.SampleBias(linearSampler, uv, mipBias).rgb;
        ao = orm.r; roughness = orm.g; metallic = orm.b;
    } else {
        if (roughnessIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[roughnessIndex];
            roughness = tex.SampleBias(linearSampler, uv, mipBias).r * roughnessMultiplier;
        } else {
            roughness = roughnessMultiplier;
        }
        if (metallicIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[metallicIndex];
            metallic = tex.SampleBias(linearSampler, uv, mipBias).r * metallicMultiplier;
        } else {
            metallic = metallicMultiplier;
        }
        if (aoIndex > 0) {
            Texture2D<float4> tex = ResourceDescriptorHeap[aoIndex];
            ao = tex.SampleBias(linearSampler, uv, mipBias).r;
        }
    }
#else
    if (isORM) {
        const float3 orm = aoTexture.SampleBias(linearSampler, uv, mipBias).rgb;
        ao = orm.r; roughness = orm.g; metallic = orm.b;
    } else {
        roughness = roughnessTexture.SampleBias(linearSampler, uv, mipBias).r * roughnessMultiplier;
        metallic  = metallicTexture.SampleBias(linearSampler, uv, mipBias).r * metallicMultiplier;
        ao        = aoTexture.SampleBias(linearSampler, uv, mipBias).r;
    }
#endif
    ao = lerp(1.0, ao, aoIntensity);

    const float3 geometricNormal = normalize(input.normalWS);
    float3 N = geometricNormal;
#ifdef UZ_BINDLESS
    if (normalIndex > 0) {
#else
    {
#endif
        const float3 T = normalize(input.tangentWS.xyz);
        const float3 B = cross(N, T) * input.tangentWS.w;
        const float3x3 TBN = float3x3(T, B, N);
#ifdef UZ_BINDLESS
        Texture2D<float4> nrmTex = ResourceDescriptorHeap[normalIndex];
        const float3 nSample = nrmTex.SampleBias(linearSampler, uv, mipBias).rgb;
#else
        const float3 nSample = normalTexture.SampleBias(linearSampler, uv, mipBias).rgb;
#endif
        float3 normalTS;
        normalTS.xy = (nSample.rg * 2.0 - 1.0) * normalIntensity;
        normalTS.z  = sqrt(max(1.0 - dot(normalTS.xy, normalTS.xy), 0.0));
        N = normalize(mul(normalTS, TBN));
    }

    float3 emissive = float3(0, 0, 0);
#ifdef UZ_BINDLESS
    if (emissiveIndex > 0) {
        Texture2D<float4> eTex = ResourceDescriptorHeap[emissiveIndex];
        emissive = eTex.SampleBias(linearSampler, uv, mipBias).rgb;
    }
#else
    emissive = emissiveTexture.SampleBias(linearSampler, uv, mipBias).rgb;
#endif
    emissive *= emissiveColor * emissiveIntensity;

    // Masked materials clip as the G-buffer pass does (bits[0:1] == 1; mesh decals, bit 4, are not drawn).
    if ((materialFlags & 0x3u) == 1u && (materialFlags & 0x10u) == 0u) {
        float alphaValue = matOpacity * albedoAlpha;
#ifdef UZ_BINDLESS
        if (opacitySrvIndex > 0) {
            Texture2D<float4> opTex = ResourceDescriptorHeap[opacitySrvIndex];
            alphaValue = matOpacity * opTex.Sample(linearSampler, uv).a;
        }
#endif
        if (alphaValue < alphaClipThreshold) discard;
    }

    // The diagnostic overrides and map switches of the G-buffer pass, so a diagnostic frame and
    // its probe captures describe the same materials.
    if (materialRoughnessOverride >= 0.0) roughness = materialRoughnessOverride;
    if (materialMetallicOverride  >= 0.0) metallic  = materialMetallicOverride;
    if ((materialMapMask & 1u) != 0u) N  = geometricNormal;
    if ((materialMapMask & 2u) != 0u) ao = 1.0;

    // ── Shading inputs (deferred_lighting_ps.hlsl) ───────────────────────────────────────────
    // Roughness floor = sqrt(r.Material.MinGGXAlpha); the authored Specular (materialFlags bits
    // 8..15, 0.5 = the default F0) sets the dielectric reflectance as the G-buffer packs it.
    metallic  = saturate(metallic);
    roughness = max(saturate(roughness), sqrt(materialMinGGXAlpha));
    ao        = max(saturate(ao), skyLightMinAmbient);
    const float  alpha = roughness * roughness;
    const float  materialSpecular = float((materialFlags >> 8u) & 0xFFu) / 255.0;
    const float3 dielectricF0  = (2.0 * materialDielectricF0) * materialSpecular;
    const float  dielectricF90 = saturate(50.0 * dielectricF0.g);
    const float3 F0        = lerp(dielectricF0, albedo, metallic);
    const float3 F90       = lerp(dielectricF90, 1.0, metallic);
    const float3 diffColor = albedo * (1.0 - metallic);

    const float3 worldPos = input.positionWS;
    const float3 V   = normalize(cameraPos.xyz - worldPos);
    const float  NoV = max(dot(N, V), 1e-4);

    // ── Sun through the capture's own cascades ───────────────────────────────────────────────
    // The cascades are centred on the probe, so the receiver's distance to it selects the cascade.
    const float3 sunL   = normalize(lightDir.xyz);
    const float  sunNoL = max(dot(N, sunL), 0.0);
    float3 Lo = float3(0, 0, 0);
    if (sunNoL > 0.0) {
        const float shadow = CascadeShadow(worldPos, length(cameraPos.xyz - worldPos), N, sunNoL);
        const float3 H   = normalize(V + sunL);
        const float  NoH = saturate(dot(N, H));
        const float  VoH = saturate(dot(V, H));
        const float  D   = D_GGX(alpha, NoH);
        const float  Vis = V_SmithGGXCorrelated(alpha, NoV, sunNoL);
        const float3 F   = F_Schlick(F0, F90, VoH);
        const float3 spec = D * Vis * F;
        // Frostbite energy-conserving diffuse (already divided by pi).
        const float3 diff = EvalFrostbiteDiffuse(diffColor, roughness, NoV, sunNoL, VoH);
        Lo = (diff + spec) * lightColor.xyz * sunNoL * shadow;
    }

    // ── Sky light: the split-sum IBL of the deferred pass ────────────────────────────────────
    const float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;

    // The SH is projected in the cubemap's frame, so the normal is converted like every cubemap read.
    float3 irradiance = EvalSH_L2(WorldToCubemap(N), skyIrradianceSH);
#ifdef UZ_BINDLESS
    if (IsSHZero(skyIrradianceSH) && irradianceSrvIndex != kSentinelSrvIndex) {
        TextureCube<float4> irrTex = ResourceDescriptorHeap[irradianceSrvIndex];
        irradiance = irrTex.SampleLevel(linearSampler, WorldToCubemap(N), 0).rgb;
    }
#endif
    irradiance = max(irradiance, skyLightMinAmbient);

    // Dominant-direction correction and the bake's roughness -> mip curve (linear for the engine's
    // own bake, the PMREM curve when bit 8 of prefilteredMipLevels is set).
    float3 R = reflect(-V, N);
    const float r4 = roughness * roughness * roughness * roughness;
    R = normalize(lerp(R, N, r4));
    const float maxMip = float((prefilteredMipLevels & 0xFFu) - 1u);
    const bool  prefilteredIsPmrem = (prefilteredMipLevels & 0x100u) != 0u;
    const float mipLevel = prefilteredIsPmrem
        ? clamp(roughness * (2.0 - roughness) * maxMip * 1.2, 0.0, maxMip)
        : clamp(roughness * maxMip, 0.0, maxMip);
    float3 radiance = float3(0, 0, 0);
#ifdef UZ_BINDLESS
    if (prefilteredSrvIndex != kSentinelSrvIndex) {
        TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
        radiance = prefTex.SampleLevel(linearSampler, WorldToCubemap(R), mipLevel).rgb;
    }
#endif

    // Split dielectric / metallic multi-scattering (Fdez-Aguera 2019), as the deferred pass.
    const float2 envBRDF  = EnvironmentBRDFApprox(roughness, NoV);
    const float3 FssEss_d = dielectricF0 * envBRDF.x + dielectricF90 * envBRDF.y;
    const float  Ess_d    = envBRDF.x + envBRDF.y;
    const float  Ems_d    = 1.0 - Ess_d;
    const float3 Favg_d   = dielectricF0 + (dielectricF90 - dielectricF0) * 0.047619;
    const float3 Fms_d    = FssEss_d * Favg_d / max(1.0 - Ems_d * Favg_d, 0.001);
    const float3 metallicF0 = albedo;
    const float3 FssEss_m = metallicF0 * envBRDF.x + envBRDF.y;
    const float3 Favg_m   = metallicF0 + (1.0 - metallicF0) * 0.047619;
    const float3 Fms_m    = FssEss_m * Favg_m / max(1.0 - Ems_d * Favg_m, 0.001);
    const float3 singleScatter = lerp(FssEss_d, FssEss_m, metallic);
    const float3 multiScatter  = max(float3(0, 0, 0), lerp(Fms_d * Ems_d, Fms_m * Ems_d, metallic));
    float3 specularAmbient = radiance * singleScatter + irradiance * multiScatter;

    // Diffuse energy compensation at NoV = 1 (view-independent), dielectric scattering only.
    const float2 envBRDF_diff  = EnvironmentBRDFApprox(roughness, 1.0);
    const float3 FssEss_d_diff = dielectricF0 * envBRDF_diff.x + dielectricF90 * envBRDF_diff.y;
    const float  Ems_d_diff    = 1.0 - (envBRDF_diff.x + envBRDF_diff.y);
    const float3 Fms_d_diff    = FssEss_d_diff * Favg_d / max(1.0 - Ems_d_diff * Favg_d, 0.001);
    const float3 totalScatterDielectric = FssEss_d_diff + Fms_d_diff * Ems_d_diff;
    const float3 diffuseAmbient = (1.0 - metallic) * irradiance * skyTint * albedo * (1.0 - totalScatterDielectric);

    // Horizon occlusion and specular occlusion (rough dielectrics), AO on the diffuse.
    const float horizon     = saturate(1.0 + dot(R, N));
    const float horizonFade = saturate(roughness * 4.0) * (1.0 - metallic);
    specularAmbient *= lerp(1.0, horizon * horizon, horizonFade);
    const float specOcclusion = (metallic > 0.5)
        ? 1.0
        : saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
    const float aoForDiffuse = pow(saturate(ao), indirectAOExponent);
    const float3 ambient = diffuseAmbient * aoForDiffuse + specularAmbient * specOcclusion * skyTint;

    return float4(Lo + ambient + emissive, 1.0);
}
