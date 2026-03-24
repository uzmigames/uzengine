/// Deferred lighting — PBR + IBL + CSM shadows + contact shadows.
///
/// @ref Walter 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// @ref Burley 2012 "Physically Based Shading at Disney"
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref Ramamoorthi 2001 "An Efficient Representation for Irradiance Environment Maps"
/// @ref Engel 2006 "Cascaded Shadow Maps" (ShaderX5) — CSM shadow sampling
/// @ref Wihlidal 2017 "4K Checkerboard Rendering in Battlefield 1" — contact shadows

#include "../include/octahedral.hlsli"
#include "../include/pbr_common.hlsli"
#include "../include/sh_irradiance.hlsli"
#include "../include/ibl.hlsli"

// shadow_common.hlsli and contact_shadows.hlsli are included AFTER sampler/texture
// declarations because they reference shadowSampler, shadowAtlasTexture, etc.

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
SamplerState clampSampler : register(s2);

// Shadow/contact includes AFTER samplers+textures (they reference shadowSampler, etc.)
#include "../include/shadow_common.hlsli"
#include "../include/contact_shadows.hlsli"

float3 ReconstructWorldPosition(float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 wp = mul(float4(ndc, depth, 1.0), invViewProj);
    return wp.xyz / wp.w;
}

/// Smith Joint Approximation (Epic). @ref Karis 2013 "Real Shading in Unreal Engine 4"
float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL + 1e-5);
}

/// Specular occlusion from AO. @ref Lagarde 2014
float SpecularOcclusion(float NoV, float ao, float roughness) {
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

/// Charlie sheen NDF — fabric/velvet specular distribution.
/// @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @constants Uses sin(theta) instead of cos(theta) for fabric highlight at grazing
/// @deviations NONE
float D_Charlie(float roughness, float NoH) {
    float sin2h = max(1.0 - NoH * NoH, 0.0078125);  // 2^(-14/2) min to avoid zero
    float sinH  = sqrt(sin2h);
    // Inverted Gaussian: peaks at grazing angles (fabric sheen behavior)
    float invR = 1.0 / max(roughness, 0.001);
    return (2.0 + invR) * pow(sinH, invR) / (2.0 * 3.14159265);
}

/// Neubelt visibility for cloth — simplified geometric term for sheen.
/// @ref Neubelt 2013, Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @deviations NONE
float V_Neubelt(float NoV, float NoL) {
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
}

float4 main(FSInput input) : SV_Target {
    int2 px = int2(input.positionCS.xy);

    // ── GBuffer Load ──────────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> gbAlbedo = ResourceDescriptorHeap[gBufIndices.albedoIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[gBufIndices.normalIdx];
    Texture2D<float>  gbDepth  = ResourceDescriptorHeap[gBufIndices.depthIdx];

    float4 rt0    = gbAlbedo.Load(int3(px, 0));
    float4 rt1    = gbNormal.Load(int3(px, 0));
    float  z      = gbDepth.Load(int3(px, 0)).r;
#else
    float4 rt0    = gbufferAlbedo.Load(int3(px, 0));
    float4 rt1    = gbufferNormal.Load(int3(px, 0));
    float  z      = gbufferDepth.Load(int3(px, 0)).r;
#endif

    // ── Load RT3: emissive + packed shading model ────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> gbEmissive = ResourceDescriptorHeap[gBufIndices.emissiveIdx];
    float4 rt3 = gbEmissive.Load(int3(px, 0));
#else
    float4 rt3 = gbufferEmissive.Load(int3(px, 0));
#endif
    float3 emissive = rt3.rgb;

    // Decode: packedAlpha = specular + smID * 16.0
    // @ref gbuffer_ps.hlsl encoding — specular in [0,1], smID in upper bits
    uint  shadingModelID = uint(floor(rt3.a / 16.0));
    float specular       = rt3.a - float(shadingModelID) * 16.0;

    // Skip sky pixels — reversed-Z: far plane = 0.0 (kDepthFar).
    if (z <= 0.0001) discard;

    // ── Unlit early-out (shadingModelID == 0) ────────────────────────────
    // Unlit materials output emissive + base color only, no lighting.
    if (shadingModelID == 0) {
        return float4(emissive + rt0.rgb, 1.0);
    }

    float3 baseColor = rt0.rgb;
    float  ao        = rt0.a;
    float3 N = OctDecode(rt1.rg);
    float metallic  = rt1.b;
    float roughness = max(rt1.a, 0.04);
    float alpha     = roughness * roughness;
    float a2        = alpha * alpha;

    float3 F0           = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 DiffuseColor = baseColor * (1.0 - metallic);

    // ── World position + view direction ────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> gbPos = ResourceDescriptorHeap[gBufIndices.positionIdx];
    float3 worldPos = gbPos.Load(int3(px, 0)).xyz;
#else
    float3 worldPos = gbufferPositionWS.Load(int3(px, 0)).xyz;
#endif

    // Read RT2.w — custom data channel (used by ClearCoat for coat params)
#ifdef UZ_BINDLESS
    float customData = gbPos.Load(int3(px, 0)).w;
#else
    float customData = gbufferPositionWS.Load(int3(px, 0)).w;
#endif

    float3 V   = normalize(cameraPos.xyz - worldPos);
    float  NoV = max(dot(N, V), 1e-4);

    // ── ClearCoat parameters (decoded from RT2.w when smID == 4) ──────
    // @ref Karis 2013 "Real Shading in Unreal Engine 4" — clear coat layer
    // RT2.w stores clearCoatRoughness (0..1) when smID==4, else 1.0.
    float clearCoatVal   = 0.0;
    float clearCoatRough = 0.04;
    if (shadingModelID == 4) {
        clearCoatVal   = 1.0;
        clearCoatRough = max(customData, 0.01); // customData = RT2.w = ccRoughness from GBuffer
    }

    // ── View depth (linear, positive) ────────────────────────────────
    // Reversed-Z: near=1.0, far=0.0. Linearize using nearPlane/farPlane.
    // viewDepth = distance from camera along the view axis in world units.
    float viewDepth = length(cameraPos.xyz - worldPos);

    // ── Direct Lighting + CSM Shadows ────────────────────────────────
    float3 Lo      = float3(0, 0, 0);
    float  shadow  = 1.0;  // Hoisted — needed by ClearCoat / Subsurface post-processing
    float  contact = 1.0;  // Hoisted — needed by ClearCoat / Subsurface post-processing
    {
        float3 L   = normalize(lightDir.xyz);
        float  NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            float3 H   = normalize(V + L);
            float  NoH = saturate(dot(N, H));
            float  VoH = saturate(dot(V, H));

            float  D   = D_GGX(NoH, alpha);
            float  Vis = Vis_SmithJointApprox(a2, NoV, NoL);
            float3 F   = F_SchlickF90(VoH, F0, saturate(50.0 * F0.g));
            float3 specBRDF = D * Vis * F;
            float3 diffuse  = Fd_Burley(DiffuseColor, roughness, NoV, NoL, VoH);

            float microShadow = saturate(NoL + 2.0 * ao * ao - 1.0);

            // ── CSM shadow sampling ─────────────────────────────────
            // @ref Engel 2006 "Cascaded Shadow Maps" — cascade selection + PCF
            shadow = SampleShadowMapFull(worldPos, N, NoL, viewDepth,
                                         input.positionCS.xy);

            // ── Contact shadows ─────────────────────────────────────
            // @ref Wihlidal 2017 — screen-space ray march for small-scale
            // shadow detail that CSM resolution cannot capture.
            if (contactShadowLength > 0.0) {
                float frameCycle = fmod(timeParams.w, 4.0);
#ifdef UZ_BINDLESS
                Texture2D<float> csDepth = ResourceDescriptorHeap[gBufIndices.depthIdx];
#else
                #define csDepth gbufferDepth
#endif
                contact = ComputeContactShadow(
                    worldPos, L, contactShadowLength, viewDepth,
                    input.positionCS.xy, frameCycle,
                    viewProj, csDepth, linearSampler);
#ifndef UZ_BINDLESS
                #undef csDepth
#endif
            }

            // ── Subsurface wrap diffuse override (smID == 2) ────────
            // @ref Burley 2012 "Physically Based Shading at Disney" — wrap shading
            // approximation for subsurface scattering: replaces standard NdotL
            // falloff with a softer (NdotL + w) / (1 + w)^2 curve.
            if (shadingModelID == 2 || shadingModelID == 5) {
                // @ref Burley 2012 — wrap diffuse for SSS/foliage
                // TwoSidedFoliage (5): wider wrap for thinner leaf surfaces
                float wrap   = (shadingModelID == 5) ? 0.5 : 0.3;
                float rawNoL = dot(N, L);
                float wrapNoL = max((rawNoL + wrap) / ((1.0 + wrap) * (1.0 + wrap)), 0.0);
                Lo = (DiffuseColor / 3.14159265 + specBRDF) * lightColor.rgb
                     * wrapNoL * microShadow * shadow * contact;
            } else if (shadingModelID == 7) {
                // ── Cloth sheen specular (smID == 7) ──────────────────
                // @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
                // Replace GGX specular with Charlie/Ashikhmin sheen NDF for fabric.
                // Cloth uses Lambert diffuse (no Burley roughness correction).
                float3 sheenColor = float3(0.04, 0.04, 0.04);
                float  D_sheen   = D_Charlie(roughness, NoH);
                float  V_sheen   = V_Neubelt(NoV, NoL);
                float3 sheenSpec = sheenColor * D_sheen * V_sheen;
                float3 clothDiffuse = DiffuseColor / 3.14159265;
                Lo = (clothDiffuse + sheenSpec) * lightColor.rgb
                     * NoL * microShadow * shadow * contact;
            } else {
                Lo = (diffuse + specBRDF) * lightColor.rgb * NoL * microShadow
                     * shadow * contact;
            }
        }

        // ── Subsurface back-lighting (smID == 2) ────────────────────
        // @ref Burley 2012 — back-scattering through thin geometry.
        // Applied outside NoL > 0 block because back-lighting occurs when
        // the surface faces away from the light.
        if (shadingModelID == 2 || shadingModelID == 5) {
            // @ref Burley 2012 — back-scattering / leaf transmittance
            // TwoSidedFoliage (5): stronger transmittance for thin leaves
            float3 sssColor        = baseColor;
            float  backNoL         = saturate(-dot(N, normalize(lightDir.xyz)));
            float  transmittance   = (shadingModelID == 5) ? 0.8 : 0.5;
            float3 backLight       = sssColor * backNoL * transmittance * lightColor.rgb;
            Lo += backLight * lerp(0.5, 1.0, shadow);
        }

        // ── Thin Translucent back-lighting (smID == 9) ────────────
        // @ref Beer-Lambert absorption for thin-film translucency.
        // Uses base color as absorption filter — light passing through
        // thin surfaces is tinted by the material color.
        if (shadingModelID == 9) {
            float3 transmittanceColor = baseColor;
            float  backNoL            = saturate(-dot(N, normalize(lightDir.xyz)));
            float3 transmitted        = transmittanceColor * backNoL * 0.6 * lightColor.rgb;
            Lo += transmitted * shadow;
        }
    }

    // ── IBL Ambient ────────────────────────────────────────────────────
    float3 ambient = float3(0, 0, 0);
    {
        float skyInt = skyLightIntensity;

        float3 irradiance = float3(0, 0, 0);
#ifdef UZ_BINDLESS
        if (irradianceSrvIndex > 0) {
            TextureCube<float4> irrTex = ResourceDescriptorHeap[irradianceSrvIndex];
            irradiance = irrTex.SampleLevel(clampSampler, WorldToCubemap(N), 0).rgb;
        } else {
            irradiance = GetSkySHDiffuse(N, skyIrradianceSH);
        }
#else
        irradiance = iblIrradiance.SampleLevel(clampSampler, WorldToCubemap(N), 0).rgb;
#endif

        float3 kS_env = F_SchlickRoughness(NoV, F0, roughness);
        float3 kD_env = (1.0 - kS_env) * (1.0 - metallic);
        float3 diffuseAmbient = kD_env * irradiance * baseColor;

        float3 R = reflect(-V, N);
        float  mipLevel = roughness * float(prefilteredMipLevels - 1);

        float3 prefilteredColor = float3(0, 0, 0);
        float2 envBRDF = float2(0, 0);
#ifdef UZ_BINDLESS
        if (prefilteredSrvIndex > 0) {
            TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
            prefilteredColor = prefTex.SampleLevel(clampSampler, WorldToCubemap(R), mipLevel).rgb;
        }
        if (brdfLutSrvIndex > 0) {
            Texture2D<float2> brdfTex = ResourceDescriptorHeap[brdfLutSrvIndex];
            envBRDF = brdfTex.SampleLevel(clampSampler, float2(NoV, roughness), 0).rg;
        }
#else
        prefilteredColor = iblPrefiltered.SampleLevel(clampSampler, WorldToCubemap(R), mipLevel).rgb;
        envBRDF = iblBrdfLut.SampleLevel(clampSampler, float2(NoV, roughness), 0).rg;
#endif

        /// @ref Karis 2013, UE5 BRDF.ush:EnvBRDF()
        float  F90 = saturate(50.0 * F0.g);
        float3 specularAmbient = prefilteredColor * (F0 * envBRDF.x + F90 * envBRDF.y);

        /// @ref Lagarde 2012 "Horizon Occlusion for Normal Mapped Reflections"
        float horizon = saturate(1.0 + dot(R, N));
        specularAmbient *= horizon * horizon;

        /// @ref Kulla & Conty 2017, Fdez-Aguera 2019
        float3 FssEss = F0 * envBRDF.x + F90 * envBRDF.y;
        float  Ess    = envBRDF.x + envBRDF.y;
        float  Ems    = 1.0 - Ess;
        float3 Favg   = F0 + (1.0 - F0) / 21.0;
        float3 FmsEms = Ems * FssEss * Favg / (1.0 - Favg * Ems);
        specularAmbient += FmsEms * irradiance * 0.5;

        /// @ref Lagarde 2014
        specularAmbient *= SpecularOcclusion(NoV, ao, roughness);

        ambient = (diffuseAmbient * ao + specularAmbient) * skyInt;

        // Fallback: if IBL cubemaps are empty (first frames), metals get F0 base color.
        float iblEnergy = dot(prefilteredColor, float3(0.299, 0.587, 0.114));
        if (iblEnergy < 0.001) {
            ambient = lerp(baseColor * 0.05, F0 * 0.15, metallic) * skyInt;
        }
    }

    // ── ClearCoat dual-layer specular (smID == 4) ──────────────────────
    // @ref Karis 2013 "Real Shading in Unreal Engine 4" — clear coat is a
    // dielectric layer (IOR ~1.5, F0 = 0.04) on top of the base material.
    // Energy conservation: base layer attenuated by coat Fresnel, coat
    // specular added on top.
    if (shadingModelID == 4) {
        // ── Direct light coat lobe ──────────────────────────────────
        float3 L_cc     = normalize(lightDir.xyz);
        float  NdotL_cc = max(dot(N, L_cc), 0.0);
        if (NdotL_cc > 0.0) {
            float3 H_cc     = normalize(V + L_cc);
            float  coatNoH  = saturate(dot(N, H_cc));
            float  coatVoH  = saturate(dot(V, H_cc));

            float  coatAlpha = clearCoatRough * clearCoatRough;
            float  coatA2    = coatAlpha * coatAlpha;

            // GGX NDF for coat layer
            float coatD   = D_GGX(coatNoH, coatAlpha);
            // Schlick Fresnel for dielectric coat (F0 = 0.04)
            float coatF   = 0.04 + 0.96 * pow(1.0 - coatVoH, 5.0);
            // Smith visibility for coat
            float coatVis = Vis_SmithJointApprox(coatA2, NoV, NdotL_cc);

            float  coatSpec = coatD * coatF * coatVis;
            float3 coatLo   = float3(coatSpec, coatSpec, coatSpec)
                              * lightColor.rgb * NdotL_cc * shadow * contact;

            // Attenuate base layer by coat Fresnel, then add coat on top
            Lo = Lo * (1.0 - coatF * clearCoatVal) + coatLo * clearCoatVal;
        }

        // ── IBL coat reflection ─────────────────────────────────────
        // Attenuate base ambient by average coat Fresnel at grazing angle
        float coatFresnelAvg = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
        ambient *= (1.0 - coatFresnelAvg * clearCoatVal);

        // Add coat specular reflection from environment
        float3 R_cc    = reflect(-V, N);
        float  coatMip = clearCoatRough * float(prefilteredMipLevels - 1);
        float3 coatRefl = float3(0, 0, 0);
#ifdef UZ_BINDLESS
        if (prefilteredSrvIndex > 0) {
            TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
            coatRefl = prefTex.SampleLevel(clampSampler, WorldToCubemap(R_cc), coatMip).rgb;
        }
#else
        coatRefl = iblPrefiltered.SampleLevel(clampSampler, WorldToCubemap(R_cc), coatMip).rgb;
#endif
        ambient += coatRefl * coatFresnelAvg * clearCoatVal * skyLightIntensity;
    }

    // ── Final composite: ambient + direct + emissive ────────────────────
    float3 color = ambient + Lo + emissive;
    return float4(max(color, 0.0), 1.0);
}
