// mesh_ps.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kMeshPS[] = R"hlsl(
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

// ── begin include: include/pbr_common.hlsli ──
/// Shared PBR functions for Cook-Torrance BRDF evaluation.
///
/// Used by both the forward pass (mesh_ps.hlsl) and deferred lighting pass
/// (deferred_lighting_ps.hlsl). Implements GGX NDF, Smith geometry, and
/// Schlick-Fresnel from the metallic-roughness workflow.
///
/// # References
/// - Karis 2013, "Real Shading in Unreal Engine 4"
/// - Walter et al. 2007, "Microfacet Models for Refraction"

#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

static const float PI = 3.14159265359f;

/// Minimum roughness clamped before squaring to alpha, preventing a perfectly
/// smooth (singular) specular lobe that collapses to a delta function on GPU.
static const float MIN_ROUGHNESS = 0.04f;

// ─────────────────────────────────────────────────────────────────────────────
// ACES Filmic Tonemapping
// ─────────────────────────────────────────────────────────────────────────────

/// ACES filmic tonemapping curve (Narkowicz 2015 fit).
///
/// Maps HDR linear radiance to [0,1] LDR with a filmic S-curve that provides
/// better highlight rolloff and color preservation than per-channel Reinhard.
///
/// # Parameters
/// - x: linear HDR color (unbounded positive).
///
/// # Returns
/// Tonemapped LDR color in [0, 1].
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ─────────────────────────────────────────────────────────────────────────────
// PBR Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// GGX / Trowbridge-Reitz Normal Distribution Function.
///
/// Describes the statistical distribution of microfacet normals relative to
/// the macrosurface normal. A higher alpha (rougher surface) produces a wider,
/// dimmer highlight; a lower alpha produces a tight, bright specular peak.
///
/// D(h) = alpha^2 / (PI * ((N.H)^2 * (alpha^2 - 1) + 1)^2)
///
/// # Parameters
/// - NdotH: dot product of macrosurface normal N and half-vector H, in [0,1].
/// - alpha: perceptual roughness squared (r^2), in [MIN_ROUGHNESS^2, 1].
///
/// # Returns
/// Specular lobe density in [0, +inf). Integrates to 1 over the hemisphere.
float DistributionGGX(float NdotH, float alpha) {
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

/// Schlick-GGX single-direction geometry masking/shadowing term.
///
/// Approximates the fraction of microfacets visible from direction v that
/// are not shadowed or masked by other microfacets.
///
/// G1(v) = N.V / (N.V * (1 - k) + k)   where k = alpha / 2 for direct lighting
///
/// # Parameters
/// - NdotV: saturated dot product of N and the view or light direction.
/// - alpha: perceptual roughness squared (r^2).
///
/// # Returns
/// Scalar in (0, 1].
float GeometrySchlickGGX(float NdotV, float alpha) {
    // k remapping for direct (analytic) lights per Karis 2013 Real Shading in UE4
    float k = alpha / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

/// Smith combined geometry function for both view and light directions.
///
/// Multiplies two independent Schlick-GGX terms: one for the view direction
/// (masking) and one for the light direction (shadowing).
///
/// # Parameters
/// - NdotV: saturated dot(N, V).
/// - NdotL: saturated dot(N, L).
/// - alpha: perceptual roughness squared (r^2).
///
/// # Returns
/// Scalar geometry attenuation in (0, 1].
float GeometrySmith(float NdotV, float NdotL, float alpha) {
    return GeometrySchlickGGX(NdotV, alpha) * GeometrySchlickGGX(NdotL, alpha);
}

/// Schlick Fresnel approximation.
///
/// Computes the fraction of light reflected (vs. refracted) at a microfacet
/// surface. At grazing angles (cosTheta -> 0) the reflectance approaches 1
/// regardless of material.
///
/// F(v, h) = F0 + (1 - F0) * (1 - V.H)^5
///
/// # Parameters
/// - cosTheta: dot(V, H) — cosine of angle between view and half-vector.
/// - F0:       reflectance at normal incidence (0 deg angle of incidence).
///             Dielectric typical value: (0.04, 0.04, 0.04).
///             Metals: use the albedo color as F0.
///
/// # Returns
/// float3 Fresnel reflectance in [F0, 1].
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

/// Evaluates Cook-Torrance BRDF for a single light source.
///
/// Computes the full outgoing radiance from a single analytic light, including
/// both diffuse (Lambertian) and specular (GGX) lobes with energy conservation.
///
/// # Parameters
/// - N:         world-space surface normal (normalised).
/// - V:         world-space view direction (normalised, toward camera).
/// - L:         world-space light direction (normalised, toward light).
/// - radiance:  incoming light radiance (color * intensity * attenuation).
/// - albedo:    surface base color (linear).
/// - metallic:  metallic factor [0,1].
/// - alpha:     roughness^2 (PBR alpha parameter).
/// - F0:        reflectance at normal incidence.
///
/// # Returns
/// Outgoing radiance contribution from this light.
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float metallic, float alpha, float3 F0) {
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.001f);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // Cook-Torrance specular BRDF
    float  D = DistributionGGX(NdotH, alpha);
    float  G = GeometrySmith(NdotV, NdotL, alpha);
    float3 F = FresnelSchlick(VdotH, F0);

    // D*G*F / (4 * N.V * N.L) — the 4 is the half-vector Jacobian
    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

    // Energy conservation: kD zeroed for metals (no diffuse lobe)
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    // Lambertian diffuse normalised by PI
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

#endif // PBR_COMMON_HLSLI
// ── end include: include/pbr_common.hlsli ──

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
// ── begin include: include/light_types.hlsli ──
/// Light type definitions for multi-light shading.
///
/// Separated from lighting.hlsli so that cbuffer declarations can reference
/// LightData without pulling in SampleShadowMap and other dependencies.

#ifndef LIGHT_TYPES_HLSLI
#define LIGHT_TYPES_HLSLI

/// Maximum number of dynamic lights supported per frame.
static const uint MAX_LIGHTS = 8;

/// Per-light parameters packed to 64 bytes for cbuffer alignment.
///
/// lightType encoding uses float for cbuffer compatibility:
///   0.0 = directional, 1.0 = point, 2.0 = spot.
struct LightData {
    float3 positionOrDir;   // world position (point/spot) or direction toward light (directional)
    float  range;           // attenuation radius in world units (0 = directional)
    float3 color;           // linear HDR color * intensity
    float  lightType;       // 0 = directional, 1 = point, 2 = spot
    float3 spotDirection;   // normalized cone axis (spot only)
    float  innerConeCos;    // cos(inner half-angle)
    float  outerConeCos;    // cos(outer half-angle)
    float  _pad0;           // pad to 64 bytes
    float  _pad1;
    float  _pad2;
};

#endif // LIGHT_TYPES_HLSLI
// ── end include: include/light_types.hlsli ──

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

// ── begin include: include/shadow_common.hlsli ──
/// Shared cascaded shadow map sampling functions.
///
/// Used by both the forward pass (mesh_ps.hlsl) and deferred lighting pass
/// (deferred_lighting_ps.hlsl). Implements cascade selection, 3x3 PCF with
/// atlas boundary clamping, and inter-cascade blending.
///
/// # Prerequisites
/// The including shader must declare:
/// - `ShadowDataCB` cbuffer with: cascadeViewProj[4], cascadeSplits, shadowAtlasIndex,
///   cascadeCount, normalBias, depthBias, shadowMapSize
/// - `SamplerComparisonState shadowSampler : register(s1)`
/// - For UZ_BINDLESS: nothing extra (uses ResourceDescriptorHeap)
/// - For SM5: `Texture2D<float> shadowAtlasTexture : register(t4 or t5)`

#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Fraction of each cascade's range used for blending with the next cascade.
/// At 10%, a cascade covering [0, 100] units begins blending at 90 units.
static const float CASCADE_BLEND_BAND = 0.1f;

/// Atlas quadrant offsets for 2x2 cascade layout.
/// 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
static const float2 kCascadeOffsets[4] = {
    float2(0.0f, 0.0f), float2(0.5f, 0.0f),
    float2(0.0f, 0.5f), float2(0.5f, 0.5f)
};

// ─────────────────────────────────────────────────────────────────────────────
// PCF Shadow Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the shadow atlas with 3x3 PCF at the given atlas UV and depth.
///
/// Performs 9 taps in a 3x3 grid around the sample point, each offset by one
/// texel. The comparison sampler returns 1.0 for lit and 0.0 for shadowed per
/// tap, and the results are averaged for a soft penumbra.
///
/// PCF kernel UVs are clamped to the current cascade's atlas quadrant to
/// prevent sampling from adjacent cascades at quadrant boundaries.
///
/// # Parameters
/// - atlasUV:       UV coordinates within the full shadow atlas, in [0,1].
/// - depth:         light-space depth to compare against, in [0,1].
/// - texelSize:     1.0 / shadow atlas width in texels.
/// - cascadeOffset: atlas quadrant origin for the current cascade (from kCascadeOffsets).
///
/// # Returns
/// Shadow factor in [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowPCF(float2 atlasUV, float depth, float texelSize, float2 cascadeOffset) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    // SM5 path uses register-bound texture
    #define shadowAtlas shadowAtlasTexture
#endif

    // Cascade quadrant bounds: each quadrant spans [offset, offset + 0.5]
    // Inset by half a texel to avoid bilinear bleed at the boundary
    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + float2(0.5f, 0.5f) - halfTexel;

    float shadow = 0.0f;

    // 3x3 PCF kernel — 9 taps centered on the sample point
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            float2 offset = float2((float)x, (float)y) * texelSize;
            float2 sampleUV = clamp(atlasUV + offset, atlasMin, atlasMax);
            shadow += shadowAtlas.SampleCmpLevelZero(
                shadowSampler, sampleUV, depth);
        }
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow / 9.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cascade Shadow Map Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the shadow factor for a world-space position using cascaded shadow maps.
///
/// Selects the tightest cascade that contains the fragment based on view-space
/// depth, projects into light space, applies normal and depth bias, then samples
/// with 3x3 PCF. In the last 10% of each cascade's range, the result is blended
/// with the next cascade to eliminate visible seams.
///
/// # Parameters
/// - positionWS: fragment world-space position.
/// - normalWS:   fragment world-space surface normal (normalized).
/// - viewDepth:  distance from camera to fragment along the view axis.
///
/// # Returns
/// Shadow factor in [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowMap(float3 positionWS, float3 normalWS, float viewDepth) {
    // Per-cascade atlas texel size: atlas is 2x the cascade resolution
    float texelSize = 1.0f / (shadowMapSize * 2.0f);

    // Access cascade split distances as an array
    float splits[4] = { cascadeSplits.x, cascadeSplits.y,
                         cascadeSplits.z, cascadeSplits.w };

    // Select the first cascade whose far split exceeds the fragment depth.
    // Default to the last cascade for fragments beyond all split distances.
    uint cascadeIdx = cascadeCount - 1;
    for (uint i = 0; i < cascadeCount; i++) {
        if (viewDepth <= splits[i]) {
            cascadeIdx = i;
            break;
        }
    }

    // Offset position along surface normal to reduce shadow acne on slopes
    float3 biasedPos = positionWS + normalWS * normalBias;

    // Project into light clip space for the selected cascade
    float4 shadowClip = mul(float4(biasedPos, 1.0f), cascadeViewProj[cascadeIdx]);
    float3 shadowNDC  = shadowClip.xyz / shadowClip.w;

    // Convert from NDC [-1,1] to UV [0,1], flipping Y for texture space
    float2 shadowUV = float2(shadowNDC.x * 0.5f + 0.5f,
                             -shadowNDC.y * 0.5f + 0.5f);

    // Map UV into the cascade's atlas quadrant (each quadrant is 0.5 x 0.5)
    float2 atlasUV = shadowUV * 0.5f + kCascadeOffsets[cascadeIdx];

    // Apply depth bias before comparison
    float shadowDepth = shadowNDC.z - depthBias;

    float shadow = SampleShadowPCF(atlasUV, shadowDepth, texelSize, kCascadeOffsets[cascadeIdx]);

    // ── Cascade blending ──
    // In the last CASCADE_BLEND_BAND fraction of the cascade range, lerp
    // toward the next cascade's shadow to avoid hard seams between cascades.
    if (cascadeIdx < cascadeCount - 1) {
        float cascadeNear = (cascadeIdx == 0) ? 0.0f : splits[cascadeIdx - 1];
        float cascadeFar  = splits[cascadeIdx];
        float cascadeRange = cascadeFar - cascadeNear;
        float blendStart  = cascadeFar - cascadeRange * CASCADE_BLEND_BAND;

        if (viewDepth > blendStart) {
            // Compute next cascade's shadow
            uint nextIdx = cascadeIdx + 1;
            float3 biasedPosNext = positionWS + normalWS * normalBias;
            float4 nextClip = mul(float4(biasedPosNext, 1.0f), cascadeViewProj[nextIdx]);
            float3 nextNDC  = nextClip.xyz / nextClip.w;

            float2 nextUV = float2(nextNDC.x * 0.5f + 0.5f,
                                   -nextNDC.y * 0.5f + 0.5f);
            float2 nextAtlasUV = nextUV * 0.5f + kCascadeOffsets[nextIdx];
            float nextDepth = nextNDC.z - depthBias;

            float nextShadow = SampleShadowPCF(nextAtlasUV, nextDepth, texelSize, kCascadeOffsets[nextIdx]);

            // Blend factor: 0 at blendStart, 1 at cascadeFar
            float blendFactor = saturate((viewDepth - blendStart) /
                                         (cascadeFar - blendStart));
            shadow = lerp(shadow, nextShadow, blendFactor);
        }
    }

    return shadow;
}

#endif // SHADOW_COMMON_HLSLI
// ── end include: include/shadow_common.hlsli ──
// ── begin include: include/lighting.hlsli ──
/// Multi-light evaluation helpers for forward PBR rendering.
///
/// Defines the LightData struct and attenuation functions for directional,
/// point, and spot lights. Supports up to MAX_LIGHTS (8) dynamic lights
/// per frame.
///
/// # Light Types
/// - 0 = Directional: infinite distance, no attenuation, positionOrDir is direction toward light.
/// - 1 = Point:       omnidirectional, smooth distance attenuation with finite range.
/// - 2 = Spot:        cone-shaped, combines distance attenuation with angular falloff.
///
/// # Prerequisites
/// The including shader must declare pbr_common.hlsli (for BRDF functions)
/// and shadow_common.hlsli (for SampleShadowMap).

#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

// LightData and MAX_LIGHTS must be defined before including this file.
// See light_types.hlsli (included by the shader before this header).

// ─────────────────────────────────────────────────────────────────────────────
// Attenuation Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Smooth distance attenuation for point and spot lights.
///
/// Uses UE4-style inverse-square falloff with a smooth window function that
/// reaches exactly zero at the light's range. This avoids popping artifacts
/// when lights enter/exit the visible set.
///
///   window = saturate(1 - (d/r)^2)^2
///   atten  = window / (d^2 + 1)
///
/// The +1 in the denominator prevents the singularity at d=0 and provides
/// a natural intensity cap at the light center.
///
/// # Parameters
/// - dist:  distance from the shading point to the light source, in world units.
/// - range: light's attenuation radius. Must be > 0 for meaningful results.
///
/// # Returns
/// Attenuation factor in [0, 1]. Returns 0 if range <= 0.
float ComputePointAttenuation(float dist, float range) {
    if (range <= 0.0f) return 0.0f;

    float ratio  = dist / range;
    float window = saturate(1.0f - ratio * ratio);
    window *= window; // squared window for smooth falloff at range boundary

    // Inverse-square with +1 bias to avoid singularity at dist=0
    return window / (dist * dist + 1.0f);
}

/// Angular attenuation for spot lights (cone falloff).
///
/// Returns 1.0 inside the inner cone, 0.0 outside the outer cone, and a
/// smooth quadratic interpolation in the penumbra region between them.
///
/// # Parameters
/// - L:        normalized direction from shading point toward the light.
/// - spotDir:  normalized spot cone axis (direction the spot is pointing).
/// - innerCos: cosine of the inner half-angle (full intensity boundary).
/// - outerCos: cosine of the outer half-angle (zero intensity boundary).
///             Must satisfy outerCos <= innerCos (wider angle = smaller cosine).
///
/// # Returns
/// Spot factor in [0, 1].
float ComputeSpotFactor(float3 L, float3 spotDir, float innerCos, float outerCos) {
    // dot(-L, spotDir): angle between the reversed light direction and cone axis
    float cosAngle = dot(-L, spotDir);

    if (cosAngle >= innerCos) return 1.0f;  // inside inner cone — full intensity
    if (cosAngle <= outerCos) return 0.0f;  // outside outer cone — no light

    // Smooth penumbra: quadratic falloff between outer and inner cone edges
    float t = saturate((cosAngle - outerCos) / (innerCos - outerCos));
    return t * t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Light Accumulation
// ─────────────────────────────────────────────────────────────────────────────

/// Accumulates direct lighting from all active lights using Cook-Torrance BRDF.
///
/// Iterates over the light array, computes per-light direction and attenuation
/// (distance + spot cone), evaluates the full Cook-Torrance BRDF, and sums the
/// outgoing radiance. Shadow mapping is applied only to the first directional
/// light (index 0) when cascadeCount > 0.
///
/// # Parameters
/// - positionWS:  fragment world-space position.
/// - N:           world-space surface normal (normalized).
/// - V:           world-space view direction toward camera (normalized).
/// - NdotV:       pre-computed max(dot(N, V), 0.001) — constant across lights.
/// - albedo:      surface base color (linear).
/// - metallic:    metallic factor [0, 1].
/// - alpha:       roughness squared (PBR alpha parameter).
/// - F0:          reflectance at normal incidence.
/// - lightCount:  number of active lights (clamped to MAX_LIGHTS internally).
/// - lights:      array of LightData structs.
/// - cameraPos:   world-space camera position (for shadow view depth).
/// - cascadeCount: number of active shadow cascades (0 = shadows disabled).
///
/// # Returns
/// Total outgoing radiance from all direct light sources (before ambient/emissive).
float3 AccumulateDirectLighting(
    float3 positionWS, float3 N, float3 V, float NdotV,
    float3 albedo, float metallic, float alpha, float3 F0,
    uint lightCount, LightData lights[MAX_LIGHTS],
    float3 cameraPos, uint cascadeCount)
{
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        LightData light = lights[i];

        float3 L;
        float attenuation = 1.0f;

        if (light.lightType < 0.5f) {
            // ── Directional light ──
            // positionOrDir stores the direction TOWARD the light (pre-normalized)
            L = normalize(light.positionOrDir);

            // Shadow mapping only for the first directional light
            if (i == 0 && cascadeCount > 0) {
                float viewDepth = length(positionWS - cameraPos);
                attenuation = SampleShadowMap(positionWS, N, viewDepth);
            }
        } else {
            // ── Point or Spot light ──
            float3 toLight = light.positionOrDir - positionWS;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001f); // safe normalize

            attenuation = ComputePointAttenuation(dist, light.range);

            if (light.lightType > 1.5f) {
                // Spot light — multiply distance atten by cone falloff
                attenuation *= ComputeSpotFactor(L, light.spotDirection,
                                                  light.innerConeCos, light.outerConeCos);
            }
        }

        // ── Per-light BRDF evaluation ──
        float3 H     = normalize(V + L);
        float  NdotL = max(dot(N, L), 0.001f);
        float  NdotH = saturate(dot(N, H));
        float  VdotH = saturate(dot(V, H));

        float  D = DistributionGGX(NdotH, alpha);
        float  G = GeometrySmith(NdotV, NdotL, alpha);
        float3 F = FresnelSchlick(VdotH, F0);

        // Cook-Torrance specular: D*G*F / (4 * N.V * N.L)
        float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

        // Energy conservation: metals have no diffuse lobe
        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - metallic);
        float3 diffuse = kD * albedo / PI;

        Lo += (diffuse + specular) * light.color * NdotL * attenuation;
    }

    return Lo;
}

#endif // LIGHTING_HLSLI
// ── end include: include/lighting.hlsli ──

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
)hlsl";
} // namespace uz::generated
