/// Deferred lighting pixel shader — Cook-Torrance PBR with directional + point + spot lights
/// and cascaded shadow maps (CSM).
///
/// Reads G-Buffer render targets (albedo+AO, octahedral normal+metallic+roughness)
/// and the depth buffer to reconstruct world-space position. Evaluates a single
/// directional light (with CSM shadow) and up to 64 point lights using the same
/// GGX-based Cook-Torrance BRDF as the forward pass (mesh_ps.hlsl).
///
/// SM6.6 (UZ_BINDLESS): fetches G-Buffer textures via ResourceDescriptorHeap[index]
/// using SRV descriptor indices passed as root constants at b0.
/// SM5 (!UZ_BINDLESS): uses register-bound Texture2D at t0-t2, shadow atlas at t3.
///
/// # G-Buffer Inputs (4-RT packed layout)
/// - AlbedoAO     (R8G8B8A8_UNORM) — rgb = base color (linear), a = AO
/// - NormalMatPbr  (R8G8B8A8_UNORM) — rg = octahedral normal, b = metallic, a = roughness
/// - Depth         (R32_FLOAT SRV)  — hardware depth sampled as float
/// - Emissive      (R16G16B16A16_FLOAT) — rgb = HDR emissive, a = unused
///
/// # Constant Buffers
/// - b0: Root constants (UZ_BINDLESS only) — 3 x uint32 SRV descriptor indices
/// - b1: PerFrameCB (viewProj, invViewProj, camera, lighting globals)
/// - b2: LightingCB (point light array + count)
/// - b4: ShadowDataCB (cascade view-proj matrices, splits, bias, atlas index)
///
/// # Output
/// Linear HDR color tonemapped via ACES filmic and gamma-corrected to sRGB for
/// the R8G8B8A8_UNORM backbuffer.

#include "../include/pbr_common.hlsli"
#include "../include/sh_irradiance.hlsli"
#include "../include/octahedral.hlsli"
#include "../include/math_util.hlsli"
#include "../include/aerial_perspective.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the fullscreen triangle vertex shader (fullscreen_vs.hlsl).
struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV descriptor heap indices for G-Buffer textures.
///
/// In SM6.6 bindless mode, the root signature has no SRV descriptor table.
/// Instead, each texture is accessed via ResourceDescriptorHeap[index] using
/// indices pushed as 32-bit root constants.
///
/// 5 indices for the 4-RT G-Buffer layout:
/// albedo+AO in RT0, octahedral normal+metallic+roughness in RT1, depth,
/// world-space position in RT2, HDR emissive in RT3.
struct GBufferIndices {
    uint albedoIdx;     ///< Descriptor index for albedo+AO G-Buffer (R8G8B8A8_UNORM).
    uint normalIdx;     ///< Descriptor index for normal+metallic+roughness G-Buffer (R8G8B8A8_UNORM).
    uint depthIdx;      ///< Descriptor index for depth buffer SRV (R32_FLOAT).
    uint positionIdx;   ///< Descriptor index for world-space position G-Buffer (R32G32B32A32_FLOAT).
    uint emissiveIdx;   ///< Descriptor index for emissive G-Buffer (R16G16B16A16_FLOAT).
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);
#endif

/// Per-frame CB at b1 — extended with invViewProj in the padding area.
///
/// The original PerFrameCB has 28 floats (112 bytes) of padding after ambientColor.
/// We place invViewProj (16 floats = 64 bytes) at the start of that padding,
/// leaving 12 floats (48 bytes) still available for future use.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;      ///< xyz = world position, w = unused
    float4   timeParams;     ///< x = totalTime, y = deltaTime, z = frameCount, w = unused
    float4   lightDir;       ///< xyz = direction TOWARD the light (normalised), w = unused
    float4   lightColor;     ///< xyz = color * intensity (radiance), w = unused
    float4   ambientColor;   ///< xyz = ambient radiance, w = unused
    row_major float4x4 invViewProj; ///< Inverse view-projection for world position reconstruction
    float    nearPlane;          ///< Near clip plane distance in world units.
    float    farPlane;           ///< Far clip plane distance in world units.
    uint     irradianceSrvIndex;    ///< SRV index for irradiance cubemap (0xFFFFFFFF = no IBL).
    uint     prefilteredSrvIndex;   ///< SRV index for prefiltered specular cubemap.
    uint     brdfLutSrvIndex;       ///< SRV index for BRDF integration LUT.
    uint     prefilteredMipLevels;  ///< Number of prefiltered mip levels.
    float    skyLightIntensity;    ///< Multiplier for IBL ambient (SkyLight).
    float    skyLightColorR;       ///< Sky light tint R.
    float    skyLightColorG;       ///< Sky light tint G.
    float    skyLightColorB;       ///< Sky light tint B.
    float    skyLightMinAmbient;   ///< Minimum ambient floor (0-1). Prevents fully black shadows.
    float    exposure;           ///< Exposure multiplier from auto-exposure (applied before tonemap).

    // ── Sky Irradiance SH (L2, 7 float4 = 112 bytes) ──
    // Standard SkyIrradianceEnvironmentMap[0..6].
    float4   skyIrradianceSH[7]; ///< L2 SH coefficients for sky diffuse irradiance.
    uint     shadowDebugMode;    ///< Shadow debug visualization (0=off, 1-8=debug view).

    // ── Aerial Perspective parameters ──
    uint     aerialPerspectiveEnabled; ///< 1 = apply aerial perspective after lighting.
    uint     transmittanceLutSrvIndex; ///< SRV index for transmittance LUT (256x64 RGBA16F).
    float    apBottomRadiusKm;   ///< Planet surface radius (km).
    float    apTopRadiusKm;      ///< Top of atmosphere radius (km).
    float    apRayleighBetaX;    ///< Rayleigh scattering X (km^-1).
    float    apRayleighBetaY;    ///< Rayleigh scattering Y (km^-1).
    float    apRayleighBetaZ;    ///< Rayleigh scattering Z (km^-1).
    float    apRayleighScaleH;   ///< Rayleigh scale height (km).
    float    apMieBeta;          ///< Mie scattering coefficient (km^-1).
    float    apMieScaleH;        ///< Mie scale height (km).
    float    apMieAnisotropy;    ///< Mie HG anisotropy parameter.
    float    apSunIntensity;     ///< Sun illuminance for AP inscattering.
    float    apRayleighScale;    ///< Artistic Rayleigh scattering multiplier.
    float    apMieScale;         ///< Artistic Mie scattering multiplier.
    // Film tonemap params (unused by this shader, consumed by tonemap_ps)
    float    filmSlope;
    float    filmToe;
    float    filmShoulder;
    float    filmBlackClip;
    float    filmWhiteClip;
    uint     gtaoSrvIndex;       ///< SRV index for GTAO filtered AO (0xFFFFFFFF = no GTAO).
    uint     brdfDebugMode;     ///< BRDF debug (0=off, 1=split-screen: left=old, right=new).

    /// Reduces AP inscattering when height fog is active to prevent double-fogging.
    /// Reduces AP inscattering when height fog is active to prevent double-fogging.
    /// 0 = AP unaffected, 1 = AP inscattering fully suppressed (height fog handles haze).
    float    heightFogContribution;

    // ── Post-process film effects (unused by this shader) ──
    float    filmGrainIntensity;  ///< Film grain intensity (consumed by film_effects_ps).
    float    vignetteIntensity;   ///< Vignette intensity (consumed by film_effects_ps).
    float    chromaAbIntensity;   ///< Chromatic aberration intensity (consumed by film_effects_ps).

    /// Screen-space contact shadow ray length. 0.0 = disabled.
    /// Positive = screen-space fraction (0.02 = 2% of depth-scaled screen ray).
    /// Screen-space contact shadow ray length. 0.0 = disabled.
    /// Positive = screen-space fraction (0.02 = 2% of depth-scaled screen ray).
    float    contactShadowLength;

    uint     ssrSrvIndex;        ///< SRV index for resolved SSR texture (0xFFFFFFFF = no SSR).
    float    mipBias;            ///< Global mipmap LOD bias for screen percentage compensation.

    // ── SSGI (Screen-Space Global Illumination) ────────────────────────
    /// SRV index for upsampled SSGI indirect diffuse (0xFFFFFFFF = disabled).
    uint     ssgiSrvIndex;       ///< SRV index for upsampled SSGI indirect diffuse (0xFFFFFFFF = disabled).
    uint     ssgiAOSrvIndex;     ///< SRV index for SSGI ambient occlusion mask (0xFFFFFFFF = disabled).
    uint     ssgiEnabled;        ///< 1 = SSGI active, 0 = disabled.

    // ── Radiance Cache (SDF-based GI) ─────────────────────────────────
    /// Radiance cache probe interpolation for SDF-based GI.
    uint     radianceCacheEnabled; ///< 1 = radiance cache GI active, 0 = disabled.
    uint     rcProbeSHBufferSrv;   ///< SRV index for probe SH StructuredBuffer<float4>.
    uint     rcProbesPerAxis;      ///< Probes per axis (16).
    float    rcGridExtent;         ///< Half-size of the probe grid in world units (cm).
    float    rcGridOriginX;        ///< Probe grid origin X (minimum corner).
    float    rcGridOriginY;        ///< Probe grid origin Y.
    float    rcGridOriginZ;        ///< Probe grid origin Z.
    float    rcGiIntensity;       ///< GI intensity multiplier (default 1.0).

    // ── Aerial Perspective Volume ────────────────────────────────────
    /// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere" — AP volume
    uint     apVolumeSrvIndex;    ///< SRV for precomputed 32x32x16 AP 3D volume (0xFFFFFFFF = none).

    float    _framePad2[59];       ///< Padding to 768 bytes.
};

/// Single point light for the deferred lighting pass (64 bytes = four float4s).
/// Must match C++ PointLightData struct exactly.
static const uint LIGHT_FLAG_INVERSE_SQUARED = 1u;

struct PointLight {
    float3 position;        ///< World-space position.
    float  radius;          ///< Attenuation radius.
    float3 color;           ///< Pre-multiplied color * intensity (radiance).
    float  sourceRadius;    ///< Area light sphere radius (0 = point light).
    float  falloffExponent; ///< Custom falloff (when inverse square disabled).
    float  specularScale;   ///< Per-light specular multiplier (0-1).
    float  diffuseScale;    ///< Per-light diffuse multiplier (0-1).
    uint   flags;           ///< Bit 0 = useInverseSquaredFalloff.
    uint   shadowIndex;     ///< Base index into localShadowData[] (6 consecutive face entries). 0xFFFFFFFF = no shadow.
    float3 _pointPad;       ///< Pad to 64 bytes (4 float4s).
};

/// Single spot light for the deferred lighting pass (80 bytes = five float4s).
/// Must match C++ SpotLightData struct exactly.
///
/// Spot light struct: cone angles, falloff, shadow index.
/// SpotAngles = (CosOuterCone, InvCosConeDifference).
struct SpotLight {
    float3 position;              ///< World-space position.
    float  radius;                ///< Attenuation radius.
    float3 color;                 ///< Pre-multiplied color * intensity (radiance).
    float  cosOuterAngle;         ///< Cosine of outer cone half-angle.
    float3 direction;             ///< Normalised world-space cone direction.
    float  invCosConeDifference;  ///< 1 / (cosInner - cosOuter).
    float  falloffExponent;       ///< Custom falloff (when inverse square disabled).
    float  specularScale;         ///< Per-light specular multiplier (0-1).
    float  diffuseScale;          ///< Per-light diffuse multiplier (0-1).
    uint   flags;                 ///< Bit 0 = useInverseSquaredFalloff.
    uint   shadowIndex;           ///< Index into localShadows[]. 0xFFFFFFFF = no shadow.
    float3 _spotPad;              ///< Pad to 80 bytes (5 float4s).
};

/// Maximum number of point lights evaluated per pixel.
static const uint MAX_POINT_LIGHTS = 64;

/// Maximum number of spot lights evaluated per pixel.
static const uint MAX_SPOT_LIGHTS = 16;

/// Lighting CB at b2 — point and spot light arrays with counts.
///
/// Moved from b3 to b2 because fullscreen deferred passes do not use
/// per-object data, and the SM6.6 root signature only has 3 root params
/// (b0 root constants, b1 per-frame CBV, b2 per-object/lighting CBV).
cbuffer LightingCB : register(b2) {
    PointLight pointLights[MAX_POINT_LIGHTS];
    uint       pointLightCount;
    uint       spotLightCount;
    uint2      _lightPad;
    SpotLight  spotLights[MAX_SPOT_LIGHTS];
};

/// Cascaded shadow map parameters — must match C++ ShadowDataCB struct exactly.
/// Supports up to 8 cascades (4 main + 4 far shadow cascades).
/// standard: per-cascade bias, fade starts, receiver bias, distance fade.
/// Shadow data CB — MUST match C++ ShadowDataCB layout exactly.
/// HLSL cbuffer packing: float arrays use float4 pairs to match C++ tight packing.
/// C++ float[8] = 32 bytes contiguous. HLSL float[8] = 128 bytes (16B per element).
/// Solution: pack as float4[2] and index with helper macros.
cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[8];  ///< Light-space view-projection per cascade (512B).
    float4   cascadeSplitsPacked[2];         ///< View-space extended far split [0-3], [4-7] (32B).
    float4   cascadeFadeStartsPacked[2];     ///< View-space original far split [0-3], [4-7] (32B).
    float4   cascadeDepthBiasPacked[2];      ///< Per-cascade depth bias [0-3], [4-7] (32B).
    float4   cascadeNormalBiasPacked[2];     ///< Per-cascade normal bias [0-3], [4-7] (32B).
    uint     shadowAtlasIndex;               ///< SRV heap index for shadow depth atlas (dynamic).
    uint     cascadeCount;                   ///< Number of active cascades (1-8, main + far).
    float    shadowMapSize;                  ///< Per-cascade resolution in texels.
    float    receiverBias;                   ///< Shadow receiver bias (Default: 0.9).
    float    shadowFadeStart;                ///< World distance where last-cascade fade begins.
    float    shadowFadeEnd;                  ///< World distance where shadows fully disappear.
    uint     filterMethod;                   ///< 0=5x5 PCF, 1=Poisson 16-tap, 2=PCSS.
    float    tanLightSourceAngle;            ///< tan(sun disk angle) for PCSS.
    float    maxSoftKernelSize;              ///< Max PCSS kernel radius in texels.
    uint     atlasColumns;                   ///< Number of columns in the atlas grid (2, 3, or 4).
    uint     staticShadowAtlasIndex;         ///< SRV heap index for cached static shadow atlas. 0xFFFFFFFF = no cache.
    uint     useShadowCache;                 ///< 0 = no cache (single atlas), 1 = composite static + dynamic.
    uint     dfsEnabled;                     ///< 1 = Distance Field Shadows active, 0 = disabled.
    uint     dfsOutputSrvIndex;              ///< SRV index for DFS result texture (R16_FLOAT screen-space). 0xFFFFFFFF = not bound.
    uint     localShadowAtlasIndex;          ///< SRV heap index for local light shadow atlas. 0xFFFFFFFF = none.
    uint     localShadowCount;               ///< Number of active local light shadows.
    float4   _csmPad[4];                     ///< Pad CSM section to 768 bytes.

    // ── Local Light Shadow Data (16 entries, 80 bytes each = 1280 bytes) ──
    // C++ struct: { Mat4 viewProj; float atlasOffsetX, atlasOffsetY, atlasScale, bias; }
    // In HLSL cbuffer, the struct is naturally 80 bytes (64B mat + 16B float4) = 5 float4s.
    // We use raw float4 arrays indexed by shadow index to avoid HLSL struct alignment surprises.
    //
    // localShadowData[i*5 + 0..3] = viewProj rows 0-3
    // localShadowData[i*5 + 4]    = (atlasOffsetX, atlasOffsetY, atlasScale, bias)
    //
    // Per-light shadow data (viewProj + atlas params)
    float4   localShadowData[80];            ///< 16 shadows * 5 float4s each = 80 float4s (1280 bytes).
    float4   _localShadowPad[16];            ///< Pad to 2304 total bytes (9 * 256).
};

// Cascade array accessors defined in shadow_common.hlsli (included below).

// ─────────────────────────────────────────────────────────────────────────────
// G-Buffer Textures & Sampler
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// SM5 path: G-Buffer textures bound via SRV descriptor table at t0-t4.
Texture2D<float4> gbufferAlbedo      : register(t0); ///< rgb = linear base color, a = AO
Texture2D<float4> gbufferNormal      : register(t1); ///< rg = octahedral normal, b = metallic, a = roughness
Texture2D<float>  gbufferDepth       : register(t2); ///< Hardware depth as R32_FLOAT SRV
Texture2D<float>  shadowAtlasTexture : register(t3); ///< CSM shadow depth atlas
Texture2D<float4> gbufferPositionWS  : register(t4); ///< xyz = world-space position (RGBA32Float), a = 1

/// SM5 IBL textures bound by ShaderParameterBinder at t5-t7.
/// Only sampled when irradianceSrvIndex != 0xFFFFFFFF (IBL bake completed).
TextureCube<float4> iblIrradiance    : register(t5); ///< Irradiance cubemap for diffuse IBL.
TextureCube<float4> iblPrefiltered   : register(t6); ///< Prefiltered specular cubemap for specular IBL.
Texture2D<float2>   iblBrdfLut      : register(t7); ///< BRDF integration LUT (split-sum approximation).

/// SM5 transmittance LUT for aerial perspective at t8.
/// Only sampled when aerialPerspectiveEnabled == 1.
Texture2D<float4>  transmittanceLutTex : register(t8); ///< Transmittance LUT (256x64 RGBA16F).

/// SM5 GTAO filtered AO at t9.
/// Only sampled when gtaoSrvIndex != 0xFFFFFFFF.
Texture2D<float>   gtaoTexture : register(t9); ///< GTAO filtered AO (R8_UNORM).

/// SM5 emissive G-Buffer at t10.
/// SM5 emissive G-Buffer (additive self-illumination).
Texture2D<float4>  gbufferEmissive : register(t10); ///< HDR emissive color (R16G16B16A16_FLOAT).

/// SM5 local light shadow atlas at t11.
/// Used for spot light shadow sampling when localShadowAtlasIndex != 0xFFFFFFFF.
Texture2D<float>   localShadowAtlasTexture : register(t11); ///< Local light shadow depth atlas (D32Float).
#endif

/// Linear (bilinear wrap) sampler at s0 — standard engine-wide sampler.
SamplerState linearSampler : register(s0);

/// Comparison sampler for percentage-closer filtering on shadow depth maps.
/// Uses LESS comparison — returns 1.0 when the sample passes (fragment is lit).
SamplerComparisonState shadowSampler : register(s1);

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from screen UV and hardware depth.
///
/// Transforms the screen-space coordinate back through the inverse view-projection
/// matrix. The depth value is used as-is (assumes standard [0,1] depth range).
///
/// # Parameters
/// - uv:    screen UV in [0,1] (top-left origin).
/// - depth: hardware depth value from the depth buffer [0,1].
///
/// # Returns
/// World-space position (float3).
float3 ReconstructWorldPosition(float2 uv, float depth) {
    // Convert UV to NDC: x [-1,1], y [-1,1] (flip Y), z [0,1]
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3D UV origin is top-left, NDC Y points up

    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);

    // Perspective divide
    return worldPos.xyz / worldPos.w;
}

#include "../include/shadow_common.hlsli"
#include "../include/contact_shadows.hlsli"
#include "../include/shadow_debug.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// IBL Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Fresnel-Schlick approximation with roughness for IBL ambient term.
///
/// Unlike the standard FresnelSchlick used in direct lighting, this variant
/// accounts for surface roughness to reduce Fresnel reflections on rough surfaces
/// at grazing angles. Prevents overly bright rims on rough dielectrics.
///
/// # Parameters
/// - cosTheta: dot(N, V) clamped to [0,1]
/// - F0: reflectance at normal incidence
/// - roughness: perceptual roughness [0,1]
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float smoothness = 1.0f - roughness;
    return F0 + (max(float3(smoothness, smoothness, smoothness), F0) - F0) * Pow5(1.0f - cosTheta);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Bounce AO Approximation
// ─────────────────────────────────────────────────────────────────────────────

/// Approximates multi-bounce indirect occlusion using albedo-dependent curves.
/// Single-scatter AO over-darkens colored surfaces because it ignores light
/// that bounces multiple times within occluded crevices. This polynomial
/// fit compensates by lifting the AO term toward the surface color.
///
/// @ref Jimenez et al. 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion"
/// @constants a = 2.0404 * BaseColor - 0.3324 (ush:1222)
///            b = -4.7951 * BaseColor + 0.6417 (ush:1223)
///            c = 2.7552 * BaseColor + 0.6903 (ush:1224)
/// @deviations NONE
/// @reference Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"
float3 AOMultiBounce(float ao, float3 baseColor) {
    float3 a = 2.0404f * baseColor - 0.3324f;
    float3 b = -4.7951f * baseColor + 0.6417f;
    float3 c = 2.7552f * baseColor + 0.6903f;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}

// ─────────────────────────────────────────────────────────────────────────────
// Spot Light Attenuation
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the spot cone angular attenuation factor.
///
/// Uses the reference engine's squared smooth-step falloff from full intensity inside the
/// inner cone to zero at the outer cone edge.
///
/// Squared smooth-step spot cone falloff.
/// @constants SpotAngles.x = CosOuterCone
///            SpotAngles.y = InvCosConeDifference
/// @deviations NONE
///
/// # Parameters
/// - L:         normalised direction FROM surface TO light.
/// - spotDir:   normalised world-space direction the spot cone points.
/// - spotAngles: x = cosOuterAngle, y = invCosConeDifference.
///
/// # Returns
/// [0, 1] attenuation factor: 1 inside inner cone, 0 outside outer cone.
float SpotAttenuation(float3 L, float3 spotDir, float2 spotAngles)
{
    // saturate((dot(L, -SpotDirection) - SpotAngles.x) * SpotAngles.y)
    // saturate((dot(L, -SpotDirection) - SpotAngles.x) * SpotAngles.y)
    float ConeAngleFalloff = saturate((dot(L, -spotDir) - spotAngles.x) * spotAngles.y);

    // Square the linear ramp for smoother visual falloff
    // Square the linear ramp for a smoother visual falloff
    return ConeAngleFalloff * ConeAngleFalloff;
}

// ─────────────────────────────────────────────────────────────────────────────
// Old BRDF Functions (for split-screen debug comparison)
// ─────────────────────────────────────────────────────────────────────────────

/// Old BRDF: Lambertian diffuse (constant 1/PI).
///
/// The simplest physically-based diffuse model. Does not account for
/// roughness-dependent retroreflection (unlike Burley). Used as the
/// "before" reference in the split-screen BRDF comparison debug mode.
///
/// # Parameters
/// - albedo: surface base color (linear).
///
/// # Returns
/// Diffuse BRDF value (includes 1/PI normalization).
float3 DiffuseLambertOld(float3 albedo) {
    return albedo / PI;
}

/// Old BRDF: Schlick-GGX geometry/visibility term.
///
/// Uses the Schlick approximation to Smith's masking function with a
/// remapped roughness parameter (k = alpha/2). This is the older visibility
/// formulation that does NOT correlate the masking and shadowing terms
/// (uncorrelated Smith). The newer VisibilitySmithGGX (height-correlated)
/// provides better energy conservation at grazing angles.
///
/// # Parameters
/// - NdotV: saturated dot(N, V).
/// - NdotL: saturated dot(N, L).
/// - a2:    alpha squared (roughness^4 or roughness^2 depending on convention).
///
/// # Returns
/// Combined visibility term including the 1/(4*NdotV*NdotL) denominator.
float Vis_SchlickGGXOld(float NdotV, float NdotL, float a2) {
    float k = sqrt(a2) * 0.5f; // Schlick approximation: k = alpha/2
    float gv = NdotV / (NdotV * (1.0f - k) + k);
    float gl = NdotL / (NdotL * (1.0f - k) + k);
    return gv * gl * 0.25f; // 0.25 = 1/(4) from Cook-Torrance denominator
}

// ─────────────────────────────────────────────────────────────────────────────
// Local Light Shadow Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the local light shadow atlas for a given spot light shadow.
///
/// Projects the world position through the light's view-projection matrix,
/// transforms to atlas UV space, and performs a single hardware PCF sample
/// using the comparison sampler.
///
/// Atlas tile sampling for local light shadows.
/// @constants atlasParams.x = atlasOffsetX, .y = atlasOffsetY, .z = atlasScale, .w = bias
/// @deviations NONE — single-tap PCF
///
/// # Parameters
/// - worldPos:     world-space fragment position.
/// - shadowIdx:    index into localShadowData[] (0 to localShadowCount-1).
///
/// # Returns
/// Shadow factor [0, 1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleLocalLightShadow(float3 worldPos, uint shadowIdx)
{
    // Decode the interleaved localShadowData array:
    // [idx*5+0..3] = viewProj rows, [idx*5+4] = (offsetX, offsetY, scale, bias)
    uint base = shadowIdx * 5;
    float4 row0 = localShadowData[base + 0];
    float4 row1 = localShadowData[base + 1];
    float4 row2 = localShadowData[base + 2];
    float4 row3 = localShadowData[base + 3];
    float4 atlasParams = localShadowData[base + 4];

    // Build view-projection matrix from rows
    float4x4 lightViewProj = float4x4(row0, row1, row2, row3);

    // Project world position to light clip space
    float4 lightClip = mul(float4(worldPos, 1.0f), lightViewProj);
    float3 lightNdc = lightClip.xyz / lightClip.w;

    // Convert from NDC [-1,1] to UV [0,1], flip Y (D3D convention)
    float2 shadowUV = lightNdc.xy * 0.5f + 0.5f;
    shadowUV.y = 1.0f - shadowUV.y;

    // Skip samples outside the shadow map frustum
    if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) || lightNdc.z < 0.0f || lightNdc.z > 1.0f) {
        return 1.0f; // Outside shadow frustum — lit
    }

    // Transform UV from [0,1] local space to atlas tile UV space
    float atlasScale = atlasParams.z;
    float2 atlasOffset = atlasParams.xy;
    shadowUV = shadowUV * atlasScale + atlasOffset;

    // Apply depth bias
    float bias = atlasParams.w;
    float compareDepth = lightNdc.z - bias * 0.001f;

    // Sample shadow atlas with hardware PCF comparison
#ifdef UZ_BINDLESS
    Texture2D<float> localAtlas = ResourceDescriptorHeap[localShadowAtlasIndex];
    return localAtlas.SampleCmpLevelZero(shadowSampler, shadowUV, compareDepth);
#else
    // SM5: local shadow atlas bound at t11
    return localShadowAtlasTexture.SampleCmpLevelZero(shadowSampler, shadowUV, compareDepth);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Point Light Cubemap Shadow Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Selects the cubemap face index for a given direction vector.
///
/// Determines which of the 6 cube faces the direction vector points into
/// by finding the dominant axis component and its sign.
///
/// Face indices: +X=0, -X=1, +Y=2, -Y=3, +Z=4, -Z=5.
///
/// Selects cubemap face by dominant axis component.
/// @constants Face indices: +X=0,-X=1,+Y=2,-Y=3,+Z=4,-Z=5
/// @deviations NONE
///
/// # Parameters
/// - dir: unnormalised direction vector (e.g., light-to-pixel or pixel-to-light).
///
/// # Returns
/// Cubemap face index [0-5].
uint SelectCubeFace(float3 dir)
{
    // AbsLightVector = abs(WorldSampleToLightVec)
    float3 absDir = abs(dir);
    // MaxCoordinate = max(abs.x, max(abs.y, abs.z))
    float maxCoord = max(absDir.x, max(absDir.y, absDir.z));

    // if/else chain on MaxCoordinate to select face
    if (maxCoord == absDir.x)
    {
        return absDir.x == dir.x ? 0 : 1;
    }
    else if (maxCoord == absDir.y)
    {
        return absDir.y == dir.y ? 2 : 3;
    }
    else
    {
        return absDir.z == dir.z ? 4 : 5;
    }
}

/// Samples point light shadow from the atlas using cubemap face selection.
///
/// For point lights, the shadow atlas stores 6 tiles (one per cubemap face)
/// at consecutive indices in localShadowData[]. The function selects the
/// correct face based on the light-to-pixel direction, then projects
/// through that face's view-projection matrix and samples the atlas.
///
/// Point light cubemap shadow via atlas face tiles.
/// @constants Face selection per SelectCubeFace, projection through per-face VP matrix.
/// @deviations Atlas-based 2D sampling instead of TextureCube — face selection
///   and depth comparison logic are identical.
///
/// # Parameters
/// - worldPos:      world-space position of the shaded pixel.
/// - lightPos:      point light world-space position.
/// - lightInvRadius: 1.0 / light attenuation radius (for early-out).
/// - shadowBaseIdx: base index into localShadowData[] (6 consecutive entries, one per face).
///
/// # Returns
/// Shadow factor [0, 1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SamplePointLightShadow(float3 worldPos, float3 lightPos, float lightInvRadius, uint shadowBaseIdx)
{
    // WorldSampleToLightVec = LightPos - WorldPos
    float3 worldSampleToLightVec = lightPos - worldPos;
    float dist = length(worldSampleToLightVec);

    // Skip pixels outside light influence
    if (dist * lightInvRadius >= 1.0f)
        return 1.0f;

    // Face selection on pixel-to-light vector
    uint face = SelectCubeFace(worldSampleToLightVec);
    uint shadowIdx = shadowBaseIdx + face;

    // Bounds check: ensure we don't read past the localShadowData array
    if (shadowIdx >= localShadowCount)
        return 1.0f;

    // Decode the interleaved localShadowData array for this face:
    // [idx*5+0..3] = viewProj rows, [idx*5+4] = (offsetX, offsetY, scale, bias)
    uint base = shadowIdx * 5;
    float4 row0 = localShadowData[base + 0];
    float4 row1 = localShadowData[base + 1];
    float4 row2 = localShadowData[base + 2];
    float4 row3 = localShadowData[base + 3];
    float4 atlasParams = localShadowData[base + 4];

    // Build view-projection matrix from rows
    float4x4 lightViewProj = float4x4(row0, row1, row2, row3);

    // Project world position through per-face VP matrix
    // uses -WorldSampleToLightVec (light-to-pixel direction) as input to the face VP matrix.
    // Our localShadowData stores face VP matrices that expect world-space input directly,
    // so we project the world position through the face VP matrix (same as spot lights).
    float4 lightClip = mul(float4(worldPos, 1.0f), lightViewProj);
    float3 lightNdc = lightClip.xyz / lightClip.w;

    // Convert from NDC [-1,1] to UV [0,1], flip Y (D3D convention)
    float2 shadowUV = lightNdc.xy * 0.5f + 0.5f;
    shadowUV.y = 1.0f - shadowUV.y;

    // Skip samples outside the shadow map frustum
    if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) || lightNdc.z < 0.0f || lightNdc.z > 1.0f) {
        return 1.0f; // Outside shadow frustum — lit
    }

    // Transform UV from [0,1] local space to atlas tile UV space
    float atlasScale = atlasParams.z;
    float2 atlasOffset = atlasParams.xy;
    shadowUV = shadowUV * atlasScale + atlasOffset;

    // Apply depth bias
    // Apply depth bias to avoid self-shadowing artifacts
    float bias = atlasParams.w;
    float compareDepth = lightNdc.z - bias * 0.001f;

    // Sample shadow atlas with hardware PCF comparison
    // Single-tap hardware PCF comparison
#ifdef UZ_BINDLESS
    Texture2D<float> localAtlas = ResourceDescriptorHeap[localShadowAtlasIndex];
    return localAtlas.SampleCmpLevelZero(shadowSampler, shadowUV, compareDepth);
#else
    // SM5: local shadow atlas bound at t11
    return localShadowAtlasTexture.SampleCmpLevelZero(shadowSampler, shadowUV, compareDepth);
#endif
}

/// Evaluates the OLD Cook-Torrance BRDF (Lambertian diffuse + Schlick-GGX).
///
/// Used in split-screen BRDF debug mode (left half). Does NOT include
/// multiscatter energy compensation — uses the simpler (1-F)*(1-metallic)
/// energy conservation that was standard before Kulla-Conty.
///
/// # Parameters
/// - N, V, L:   world-space normal, view, and light directions (normalised).
/// - radiance:  incoming light radiance.
/// - albedo:    surface base color (linear).
/// - metallic:  metallic factor [0,1].
/// - alpha:     roughness^2 (PBR alpha).
/// - F0:        reflectance at normal incidence.
///
/// # Returns
/// Outgoing radiance from this light using the old BRDF.
float3 EvaluateBRDFOld(float3 N, float3 V, float3 L, float3 radiance,
                       float3 albedo, float metallic, float alpha, float3 F0) {
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.001f);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // Specular: D * Vis * F (old Schlick-GGX visibility)
    float  D   = DistributionGGX(NdotH, alpha);
    float  Vis = Vis_SchlickGGXOld(NdotV, NdotL, alpha);
    float3 F   = FresnelSchlick(VdotH, F0);

    float3 specular = D * Vis * F;

    // Old energy conservation: (1-F)*(1-metallic), no multiscatter
    float3 kD = (1.0f - F) * (1.0f - metallic);

    // Lambertian diffuse (constant 1/PI)
    float3 diffuse = kD * DiffuseLambertOld(albedo);

    return (diffuse + specular) * radiance * NdotL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates deferred PBR lighting from the G-Buffer.
///
/// Reads surface attributes from the G-Buffer textures, reconstructs the
/// world-space position from depth, and evaluates Cook-Torrance BRDF for
/// one directional light and up to 64 point lights. Applies ACES filmic
/// tonemapping and sRGB gamma correction.
///
/// # Returns
/// float4 color for the R8G8B8A8_UNORM backbuffer.
float4 main(FSInput input) : SV_Target {

    float2 uv = input.texcoord;

    // ── Buffer Visualization Grid ──
    // When active, remap UV per-tile so each tile shows the full scene in miniature.
    uint gridTileIdx = 4; // center = LIT (default)
    bool gridActive = (shadowDebugMode != 0);
    bool gridIsLabel = false;
    if (gridActive) {
        // Border check
        float3 border = GetBorderColor(uv);
        if (border.x >= 0.0f) return float4(border, 1.0f);

        float2 tileUV;
        GetBufferGridTile(uv, tileUV, gridTileIdx, gridIsLabel);
        if (gridIsLabel) {
            return float4(GetTileLabelColor(gridTileIdx), 1.0f);
        }
        // Remap UV so the G-Buffer is sampled for the full scene within this tile
        uv = tileUV;
    }

    // ── Read G-Buffer (2-RT packed layout) ──
#ifdef UZ_BINDLESS
    // SM6.6 bindless: fetch G-Buffer textures from the descriptor heap
    Texture2D<float4> gbAlbedo = ResourceDescriptorHeap[gBufIndices.albedoIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[gBufIndices.normalIdx];
    Texture2D<float>  gbDepth  = ResourceDescriptorHeap[gBufIndices.depthIdx];

    float4 albedoAO     = gbAlbedo.Sample(linearSampler, uv);
    float4 normalPacked = gbNormal.Sample(linearSampler, uv);
    float  depth        = gbDepth.Sample(linearSampler, uv).r;
#else
    // SM5 path: register-bound textures at t0-t2
    float4 albedoAO     = gbufferAlbedo.Sample(linearSampler, uv);
    float4 normalPacked = gbufferNormal.Sample(linearSampler, uv);
    float  depth        = gbufferDepth.Sample(linearSampler, uv).r;
#endif

    float3 albedo    = albedoAO.rgb;
    float  ao        = albedoAO.a;
    float3 N         = OctDecode(normalPacked.rg);
    float  metallic  = normalPacked.b;
    float  roughness = normalPacked.a;

    // ── GTAO screen-space ambient occlusion ──
    // Multiply material AO with GTAO result when available.
    // GTAO texture stores visibility (1.0 = fully lit, 0.0 = fully occluded).
    float gtaoValue = 1.0f;
    if (gtaoSrvIndex != 0xFFFFFFFF) {
#ifdef UZ_BINDLESS
        Texture2D<float> gtaoTex = ResourceDescriptorHeap[gtaoSrvIndex];
        gtaoValue = gtaoTex.Sample(linearSampler, uv).r;
#else
        // SM5 path: GTAO bound at t9
        gtaoValue = gtaoTexture.Sample(linearSampler, uv).r;
#endif
        ao *= gtaoValue;
    }

    // ── World-space position ──
    // UZ_WORLDPOS_GBUFFER: read directly from the position G-Buffer RT2 (debug / Vulkan verification).
    // Default: reconstruct from depth + invViewProj (avoids extra 32bpp bandwidth in production).
#ifdef UZ_WORLDPOS_GBUFFER
#ifdef UZ_BINDLESS
    Texture2D<float4> gbPosition = ResourceDescriptorHeap[gBufIndices.positionIdx];
    float3 worldPos = gbPosition.Sample(linearSampler, uv).xyz;
#else
    float3 worldPos = gbufferPositionWS.Sample(linearSampler, uv).xyz;
#endif
#else
    float3 worldPos = ReconstructWorldPosition(uv, depth);
#endif

    // ── PBR parameter preparation ──
    float3 V = normalize(cameraPos.xyz - worldPos);

    // Perceptual roughness -> alpha: squaring linearises the perceived response
    float alpha = max(roughness * roughness, MIN_ROUGHNESS * MIN_ROUGHNESS);

    // F0: dielectrics use 0.04 (IOR ~1.5), metals use albedo as tinted reflectance
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // ── Cascaded shadow map ──
    float viewDepth = length(worldPos - cameraPos.xyz);
    float shadow = 1.0f;
    if (cascadeCount > 0) {
        shadow = SampleShadowMap(worldPos, N, viewDepth, input.positionCS.xy);
    }

    // ── Contact shadows ──
    // Screen-space ray march along light direction for fine-detail shadows
    // at object contact points that CSM resolution cannot capture.
    // Screen-space contact shadow ray march along light direction
    // Contact shadow multiplies with CSM shadow (min compositing).
    {
#ifdef UZ_BINDLESS
        Texture2D<float> csDepthTex = ResourceDescriptorHeap[gBufIndices.depthIdx];
#else
        Texture2D<float> csDepthTex = gbufferDepth;
#endif
        float contactShadow = ComputeContactShadow(
            worldPos,
            normalize(lightDir.xyz),
            contactShadowLength,
            viewDepth,
            input.positionCS.xy,
            timeParams.z, // frameCount
            viewProj,
            csDepthTex,
            linearSampler);

        // Multiply contact shadow with CSM shadow (min compositing)
        shadow *= contactShadow;
    }

    // ── BRDF debug split-screen ──
    // When brdfDebugMode == 1, the left half of the screen uses the old BRDF
    // (Lambertian diffuse + Schlick-GGX) and the right half uses the current
    // BRDF (Disney Burley diffuse + VisibilitySmithGGX + multiscatter). This allows
    // direct visual comparison of the BRDF upgrade.
    // Use the original (pre-grid-remap) texcoord for the split test.
    bool useOldBRDF = (brdfDebugMode == 1 && input.texcoord.x < 0.5f);

    // ── Directional light (shadowed) ──
    float3 L_dir = normalize(lightDir.xyz);

    // Micro-shadow disabled — causes visual artifacts when sampling GTAO texture
    // in deferred lighting pass (texture may be in wrong state or SRV conflict).
    // TODO: investigate GTAO texture state at this point in the pipeline.

    float3 Lo;
    if (useOldBRDF) {
        Lo = EvaluateBRDFOld(N, V, L_dir, lightColor.xyz,
                              albedo, metallic, alpha, F0) * shadow;
    } else {
        Lo = EvaluateBRDF(N, V, L_dir, lightColor.xyz,
                          albedo, metallic, alpha, F0) * shadow;
    }

    // ── Point lights ──
    for (uint i = 0; i < pointLightCount; ++i) {
        float3 lightVec  = pointLights[i].position - worldPos;
        float  dist      = length(lightVec);
        float  radius    = pointLights[i].radius;

        // Skip lights beyond their attenuation radius
        if (dist > radius)
            continue;

        float3 L_point = lightVec / dist; // normalise

        // Attenuation: inverse square (reference default) or custom exponent
        float attenuation;
        bool useInvSq = (pointLights[i].flags & LIGHT_FLAG_INVERSE_SQUARED) != 0;
        float distOverRadius = dist / radius;

        if (useInvSq) {
            // Smooth inverse-square with windowing function:
            // attenuation = saturate(1 - (d/r)^4)^2 / (d^2 + 1)
            float distRatio4 = distOverRadius * distOverRadius * distOverRadius * distOverRadius;
            float windowing  = saturate(1.0f - distRatio4);
            attenuation      = (windowing * windowing) / (dist * dist + 1.0f);
        } else {
            // Custom falloff exponent (non-physical, reference legacy)
            attenuation = pow(saturate(1.0f - distOverRadius), pointLights[i].falloffExponent);
        }

        // ── Point light shadow sampling ──────────────────────────────
        // If this point light casts shadows (shadowIndex != 0xFFFFFFFF),
        // sample the local shadow atlas using cubemap face selection and
        // multiply attenuation by the shadow factor.
        // Point light cubemap shadow via atlas face tiles
        float pointShadow = 1.0f;
        if (pointLights[i].shadowIndex != 0xFFFFFFFF
            && localShadowAtlasIndex != 0xFFFFFFFF
            && pointLights[i].shadowIndex < localShadowCount) {
            float lightInvRadius = 1.0f / radius;
            pointShadow = SamplePointLightShadow(worldPos, pointLights[i].position, lightInvRadius, pointLights[i].shadowIndex);
        }
        attenuation *= pointShadow;

        // Early-out if fully attenuated (distance + shadow)
        if (attenuation <= 0.0f)
            continue;

        // Per-light specular/diffuse scaling
        float3 radiance = pointLights[i].color * attenuation;
        float pSpecScale = pointLights[i].specularScale;
        float pDiffScale = pointLights[i].diffuseScale;

        // Evaluate BRDF — old or new depending on split-screen side
        // Apply per-light SpecularScale/DiffuseScale via radiance splitting
        if (useOldBRDF) {
            Lo += EvaluateBRDFOld(N, V, L_point, radiance,
                                   albedo, metallic, alpha, F0);
        } else {
            Lo += EvaluateBRDFScaled(N, V, L_point, radiance,
                               albedo, metallic, alpha, F0, pSpecScale, pDiffScale);
        }
    }

    // ── Spot lights ──
    // Spot lights: distance attenuation + cone falloff
    // @ref Karis 2013 — inverse-square windowed attenuation + squared smooth-step cone
    for (uint si = 0; si < spotLightCount; ++si) {
        float3 lightVec  = spotLights[si].position - worldPos;
        float  dist      = length(lightVec);
        float  radius    = spotLights[si].radius;

        // Skip lights beyond their attenuation radius
        if (dist > radius)
            continue;

        float3 L_spot = lightVec / dist; // normalise

        // Distance attenuation: same as point lights
        // Inverse squared windowing: saturate(1 - (d/r)^4)^2 / (d^2 + 1)
        float distAttenuation;
        bool useInvSq = (spotLights[si].flags & LIGHT_FLAG_INVERSE_SQUARED) != 0;
        float distOverRadius = dist / radius;

        if (useInvSq) {
            float distRatio4 = distOverRadius * distOverRadius * distOverRadius * distOverRadius;
            float windowing  = saturate(1.0f - distRatio4);
            distAttenuation  = (windowing * windowing) / (dist * dist + 1.0f);
        } else {
            distAttenuation = pow(saturate(1.0f - distOverRadius), spotLights[si].falloffExponent);
        }

        // Cone attenuation: squared smooth-step falloff
        // Cone attenuation: squared smooth-step falloff
        float2 spotAngles = float2(spotLights[si].cosOuterAngle, spotLights[si].invCosConeDifference);
        float coneAtten = SpotAttenuation(L_spot, spotLights[si].direction, spotAngles);

        // Total attenuation = distance * cone
        float attenuation = distAttenuation * coneAtten;

        // Early-out if fully attenuated
        if (attenuation <= 0.0f)
            continue;

        // ── Spot light shadow sampling ──────────────────────────────
        // If this spot light casts shadows (shadowIndex != 0xFFFFFFFF),
        // sample the local shadow atlas and multiply attenuation by the shadow factor.
        // Spot light shadow: project through light VP, sample atlas tile
        float spotShadow = 1.0f;
        if (spotLights[si].shadowIndex != 0xFFFFFFFF
            && localShadowAtlasIndex != 0xFFFFFFFF
            && spotLights[si].shadowIndex < localShadowCount) {
            spotShadow = SampleLocalLightShadow(worldPos, spotLights[si].shadowIndex);
        }
        attenuation *= spotShadow;

        // Re-check after shadow application
        if (attenuation <= 0.0f)
            continue;

        // Per-light specular/diffuse scaling
        float3 radiance = spotLights[si].color * attenuation;
        float sSpecScale = spotLights[si].specularScale;
        float sDiffScale = spotLights[si].diffuseScale;

        // Evaluate BRDF — old or new depending on split-screen side
        if (useOldBRDF) {
            Lo += EvaluateBRDFOld(N, V, L_spot, radiance,
                                   albedo, metallic, alpha, F0);
        } else {
            Lo += EvaluateBRDFScaled(N, V, L_spot, radiance,
                               albedo, metallic, alpha, F0, sSpecScale, sDiffScale);
        }
    }

    // ── Ambient: IBL or constant fallback ──────────────────────────────
    float  NdotV = max(dot(N, V), 0.0f);
    float3 ambient;

    if (irradianceSrvIndex != 0xFFFFFFFF) {
        // Diffuse IBL via L2 Spherical Harmonics (standard)
        // SH is much higher quality than 32x32 cubemap sampling
        float3 irradiance = GetSkySHDiffuse(N, skyIrradianceSH);

        // Fresnel: metals reflect more at grazing, dielectrics diffuse more
        float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD = (1.0f - F) * (1.0f - metallic);

        float3 diffuseIBL = kD * albedo * irradiance;

        // Specular IBL — sample prefiltered environment map along reflection
        float3 R = reflect(-V, N);
        float mipLevel = roughness * float(prefilteredMipLevels - 1);

#ifdef UZ_BINDLESS
        TextureCube<float4> prefilteredMap = ResourceDescriptorHeap[prefilteredSrvIndex];
        float3 prefilteredColor = prefilteredMap.SampleLevel(linearSampler, R, mipLevel).rgb;

        Texture2D<float2> brdfLutTex = ResourceDescriptorHeap[brdfLutSrvIndex];
        float2 envBRDF = brdfLutTex.SampleLevel(linearSampler, float2(NdotV, roughness), 0);
#else
        // SM5: sample register-bound IBL textures at t5-t7
        float3 prefilteredColor = iblPrefiltered.SampleLevel(linearSampler, R, mipLevel).rgb;
        float2 envBRDF = iblBrdfLut.SampleLevel(linearSampler, float2(NdotV, roughness), 0);
#endif

        // ── Lower hemisphere compensation for metals ──
        // Without scene-captured reflection probes, the sky-only cubemap has a dark
        // lower hemisphere. Metals reflecting downward appear black instead of picking
        // up indirect ground bounce. Blend toward diffuse irradiance for directions
        // below the horizon to approximate missing ground reflections.
        float belowHorizon = saturate(-R.z); // 0=above horizon, 1=straight down (Z-up)
        float3 groundFallback = irradiance * 0.5f; // Use diffuse irradiance as proxy
        float3 correctedPrefiltered = lerp(prefilteredColor, max(prefilteredColor, groundFallback), belowHorizon * metallic);

        // ── SSR + IBL compositing ──
        float3 combinedSpecular = correctedPrefiltered;

        [branch]
        if (ssrSrvIndex != 0xFFFFFFFF) {
#ifdef UZ_BINDLESS
            Texture2D<float4> ssrTex = ResourceDescriptorHeap[ssrSrvIndex];
            float4 ssrData = ssrTex.SampleLevel(linearSampler, input.texcoord, 0);
#else
            float4 ssrData = float4(0, 0, 0, 0);
#endif
            // ssrData.rgb = unweighted SSR hit color (no Fresnel, roughnessFade+intensity baked in trace)
            // ssrData.a   = blend weight (confidence from trace: vignette * roughnessFade * intensity)
            // Blend between IBL prefiltered color and SSR color BEFORE EnvBRDF
            combinedSpecular = lerp(prefilteredColor, ssrData.rgb, saturate(ssrData.a));
        }

        // Apply EnvBRDF (split-sum Fresnel integration) to combined reflection
        // @ref Lagarde 2014 — F90 micro-occlusion for EnvBRDF bias term
        // saturate(50*F0.g) for AB.y
        float F90_ibl = saturate(50.0f * F0.g);
        float3 specularIBL = combinedSpecular * (F0 * envBRDF.x + F90_ibl * envBRDF.y);

        // Karis 2013 multi-scatter energy compensation
        // @source Karis 2013 "Real Shading in Unreal Engine 4" — multi-scatter approximation
        float Ems = 1.0f - (envBRDF.x + envBRDF.y);
        float3 Favg = F0 + (1.0f - F0) / 21.0f;
        specularIBL *= 1.0f + Favg * Ems / (1.0f - Favg * Ems);

        // ── Horizon occlusion — softer falloff for metals ──
        // Linear instead of squared: metals (gold, chrome) need visible
        // reflections even when R points below the surface plane.
        float horizon = saturate(1.0f + dot(R, N));
        specularIBL *= horizon;

        // ── TEST 4: Specular occlusion ENABLED ──
        float specOcc = saturate(pow(NdotV + ao, exp2(-16.0f * roughness - 1.0f)) - 1.0f + ao);

        // ── SSGI compositing ──────────────────────────────────────────
        // SSGI indirect diffuse compositing
        // When SSGI is active, add screen-space indirect diffuse bounce to
        // the SH-based IBL diffuse. The SSGI output already includes the
        // bouncing surface's albedo and lighting, so we apply the RECEIVING
        // surface's kD (energy conservation) but NOT albedo again.
        // SSGI AO modulates the existing AO — use min to combine.
        [branch]
        if (ssgiEnabled != 0 && ssgiSrvIndex != 0xFFFFFFFF) {
#ifdef UZ_BINDLESS
            Texture2D<float4> ssgiTex   = ResourceDescriptorHeap[ssgiSrvIndex];
            float3 ssgiIndirect = ssgiTex.SampleLevel(linearSampler, input.texcoord, 0).rgb;

            // Add SSGI indirect to SH-based diffuse IBL, modulated by AO
            // to prevent bright haze at contact points (sphere-ground junction)
            diffuseIBL += ssgiIndirect * kD * ao;

            if (ssgiAOSrvIndex != 0xFFFFFFFF) {
                Texture2D<float> ssgiAOTex = ResourceDescriptorHeap[ssgiAOSrvIndex];
                float ssgiAO = ssgiAOTex.SampleLevel(linearSampler, input.texcoord, 0);
                ao = min(ao, ssgiAO);
            }
#endif
        }

        // ── Radiance Cache blending ──────────────────────────────────
        // Radiance cache probe trilinear interpolation for SDF-based GI
        // When radiance cache is active, blend probe-based GI with SH IBL.
        // Use distance from probe grid center to control blend weight:
        // probes provide better GI in the near/mid field, SH IBL covers far field.
        [branch]
        if (radianceCacheEnabled != 0 && rcProbeSHBufferSrv != 0xFFFFFFFF) {
#ifdef UZ_BINDLESS
            StructuredBuffer<float4> probeSH = ResourceDescriptorHeap[rcProbeSHBufferSrv];
            float3 rcGridOrigin = float3(rcGridOriginX, rcGridOriginY, rcGridOriginZ);

            // Compute fractional grid coordinates
            float probeSpacing = (rcGridExtent * 2.0f) / float(rcProbesPerAxis);
            float3 localPos = (worldPos - rcGridOrigin) / probeSpacing - 0.5f;
            float3 maxCoord = float3(rcProbesPerAxis - 1, rcProbesPerAxis - 1, rcProbesPerAxis - 1);
            localPos = clamp(localPos, 0.0f, maxCoord);

            uint3 cell = uint3(floor(localPos));
            float3 frac3 = localPos - float3(cell);
            uint3 cellMax = min(cell + 1, uint3(rcProbesPerAxis - 1, rcProbesPerAxis - 1, rcProbesPerAxis - 1));

            // Trilinear interpolation of probe SH at 8 corner probes
            float3 rcIrradiance = (float3)0;
            for (uint dz = 0; dz < 2; dz++) {
                float wz = (dz == 0) ? (1.0f - frac3.z) : frac3.z;
                uint pz = (dz == 0) ? cell.z : cellMax.z;
                for (uint dy = 0; dy < 2; dy++) {
                    float wy = (dy == 0) ? (1.0f - frac3.y) : frac3.y;
                    uint py = (dy == 0) ? cell.y : cellMax.y;
                    for (uint dx = 0; dx < 2; dx++) {
                        float wx = (dx == 0) ? (1.0f - frac3.x) : frac3.x;
                        uint px = (dx == 0) ? cell.x : cellMax.x;
                        float weight = wx * wy * wz;
                        if (weight <= 0.0f) continue;

                        uint probeIdx = pz * rcProbesPerAxis * rcProbesPerAxis
                                      + py * rcProbesPerAxis + px;
                        uint base = probeIdx * 7;

                        // Evaluate L0+L1 SH with cosine lobe convolution for irradiance.
                        // Raw SH stores radiance coefficients. To get irradiance (integral
                        // of radiance * cos(theta) over hemisphere), apply ZH convolution:
                        //   L0 band: multiply by PI (A0 = π)
                        //   L1 band: multiply by 2π/3 (A1 = 2π/3)
                        // @ref Sloan "Stupid Spherical Harmonics Tricks" — cosine lobe convolution factors
                        // @source "Stupid Spherical Harmonics Tricks" — Sloan, Table 1
                        float4 rSH = probeSH[base + 0];
                        float4 gSH = probeSH[base + 1];
                        float4 bSH = probeSH[base + 2];
                        // nDir: L0 basis = 1.0 (scaled by A0=π), L1 basis = N.xyz (scaled by A1=2π/3)
                        static const float kA0 = 3.14159265f;       // π
                        static const float kA1 = 2.09439510f;       // 2π/3
                        float4 nDir = float4(kA0, kA1 * N.x, kA1 * N.z, kA1 * N.y);
                        float3 probeIrr = max(float3(dot(rSH, nDir), dot(gSH, nDir), dot(bSH, nDir)), 0.0f);
                        rcIrradiance += probeIrr * weight;
                    }
                }
            }

            // Weight = 1.0 inside the probe grid, fading to 0.0 at the boundary.
            // Uses per-axis box distance so the entire grid interior gets full GI,
            // with a smooth fade only in the last 10% near each face.
            float3 rcGridCenter = rcGridOrigin + float3(rcGridExtent, rcGridExtent, rcGridExtent);
            float3 rcLocalPos = abs(worldPos - rcGridCenter) / rcGridExtent;
            // Per-axis fade: 1.0 when inside 90% of the grid, fades to 0 at the edge
            float fadeX = saturate((1.0f - rcLocalPos.x) * 10.0f);
            float fadeY = saturate((1.0f - rcLocalPos.y) * 10.0f);
            float fadeZ = saturate((1.0f - rcLocalPos.z) * 10.0f);
            float rcWeight = fadeX * fadeY * fadeZ;

            // Radiance cache diffuse: modulate by AO and direct shadow to prevent
            // light injection in occluded/shadowed areas. Additive blend with IBL
            // instead of max() which was losing energy and ignoring shadows.
            float3 rcDiffuse = kD * albedo * rcIrradiance * rcGiIntensity * ao * shadow;
            diffuseIBL += rcDiffuse * rcWeight;
#endif
        }

        float3 skyLightTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
        float3 diffuseAO = (gtaoSrvIndex != 0xFFFFFFFF) ? AOMultiBounce(ao, albedo) : float3(ao, ao, ao);
        ambient = (diffuseIBL * diffuseAO + specularIBL * specOcc) * skyLightIntensity * skyLightTint;
    } else {
        // Fallback: constant ambient (no IBL data available)
        float3 skyLightTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
        float3 aoTerm = (gtaoSrvIndex != 0xFFFFFFFF) ? AOMultiBounce(ao, albedo) : float3(ao, ao, ao);
        ambient = ambientColor.xyz * albedo * aoTerm * skyLightIntensity * skyLightTint;
    }

    // ── Minimum ambient floor (SkyLight minOcclusion equivalent) ──
    // Ensures shadowed surfaces never go fully black — adds a minimum fill light.
    if (skyLightMinAmbient > 0.0f) {
        float3 minFloor = albedo * skyLightMinAmbient;
        ambient = max(ambient, minFloor);
    }

    // ── Emissive ──
    // Emissive: pure self-illumination, bypasses all lighting.
    // HDR values > 1.0 expected and drive bloom post-process.
    // adds emissive directly to SceneColor in the base pass. In our deferred
    // pipeline, emissive was written to GBuffer RT3 (RGBA16Float) and is read here.
    // Emissive bypasses all lighting — it is pure self-illumination.
    // HDR values > 1.0 are expected and will drive the bloom post-process.
    // @deviations Read from GBuffer RT3 instead of direct base pass SceneColor write.
    float3 emissiveContrib = float3(0.0f, 0.0f, 0.0f);
#ifdef UZ_BINDLESS
    {
        Texture2D<float4> gbEmissive = ResourceDescriptorHeap[gBufIndices.emissiveIdx];
        emissiveContrib = gbEmissive.Sample(linearSampler, uv).rgb;
    }
#else
    emissiveContrib = gbufferEmissive.Sample(linearSampler, uv).rgb;
#endif

    // ── Combine ──
    float3 color = ambient + Lo + emissiveContrib;

    // ── Aerial Perspective ──
    // @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere"
    // Aerial perspective: inscattering + transmittance from precomputed 3D volume or ray march
    //
    // When the precomputed AP volume is available (apVolumeSrvIndex != 0xFFFFFFFF),
    // sample the 32x32x16 3D texture for inscattering + transmittance. This replaces
    // the per-pixel 8-step ray march with a single trilinear texture fetch.
    //
    // When the volume is not available, fall back to per-pixel ray march.
    if (aerialPerspectiveEnabled != 0) {
        float4 ap = float4(0, 0, 0, 1);

#ifdef UZ_BINDLESS
        [branch]
        if (apVolumeSrvIndex != 0xFFFFFFFF) {
            // ── AP Volume path (standard) ──────────────────────────────
            // @ref Hillaire 2020 — AP volume lookup with squared depth distribution
            Texture3D<float4> apVolume = ResourceDescriptorHeap[apVolumeSrvIndex];

            // Screen UV from pixel position
            float2 apScreenUV = input.texcoord;

            // Linearize depth: world distance from camera to pixel (km)
            float linearDepthKm = viewDepth * 0.00001f; // cm → km

            // Squared depth distribution for better near-field precision
            // Forward: normalizedSlice = ((z+0.5)/16)^2, depth = normalizedSlice * totalDepthKm
            // Inverse: normalizedSlice = sqrt(depth / totalDepthKm), w = normalizedSlice
            float totalDepthKm = 100.0f; // Must match compute shader
            float depthSlices  = 16.0f;  // Must match compute shader
            float linearW = saturate(linearDepthKm / totalDepthKm);
            float nonLinW = sqrt(linearW); // Inverse of squared distribution

            float3 uvw = float3(apScreenUV, nonLinW);
            ap = apVolume.SampleLevel(linearSampler, uvw, 0);

            // Near-fade: avoid AP artifacts at near plane
            // Fade out AP for very close pixels to avoid artifacts at near plane.
            // uses AerialPerspectiveStartDepthKm (typically 0.0001 km = 10 cm).
            float fadeWeight = saturate(linearDepthKm * 10000.0f); // 0→1 over first 0.0001 km
            ap.rgb *= fadeWeight;
            ap.a    = lerp(1.0f, ap.a, fadeWeight);
        } else
#endif
        if (transmittanceLutSrvIndex != 0xFFFFFFFF) {
            // ── Per-pixel ray march fallback ─────────────────────────────
            float3 apRayleighBeta3 = float3(apRayleighBetaX, apRayleighBetaY, apRayleighBetaZ);
#ifdef UZ_BINDLESS
            Texture2D<float4> apTransLut = ResourceDescriptorHeap[transmittanceLutSrvIndex];
#else
            Texture2D<float4> apTransLut = transmittanceLutTex;
#endif
            float3 apSunDir = normalize(lightDir.xyz);
            ap = ComputeAerialPerspective(
                worldPos, cameraPos.xyz, apSunDir, apSunIntensity,
                apBottomRadiusKm, apTopRadiusKm, apRayleighBeta3,
                apRayleighScaleH, apMieBeta, apMieScaleH, apMieAnisotropy,
                apRayleighScale, apMieScale, apTransLut, linearSampler);
        }

        // Standard AP compositing: finalColor = sceneColor * transmittance + inscattering
        ///   finalColor = sceneColor * transmittance + inscattering
        color = color * ap.a + ap.rgb;
    }

    // ── Buffer Visualization Grid — tile dispatch ──
    if (gridActive) {
        float3 tileColor;
        if (gridTileIdx == 2) {
            // Tile 2: RAW SHADOW ATLAS — display atlas content directly.
            // Maps the tile UV to atlas UV [0,1] to show the entire atlas as-is.
            // This bypasses all projection math — just shows what's in the depth texture.
            float2 atlasDirectUV = uv; // uv is already remapped to [0,1] per tile
#ifdef UZ_BINDLESS
            Texture2D<float> dbgAtl = ResourceDescriptorHeap[shadowAtlasIndex];
            float rawD = dbgAtl.SampleLevel(linearSampler, atlasDirectUV, 0).r;
#else
            float rawD = 0.0f;
#endif
            // Amplify to see depth variation: cleared=1.0→white, geometry<1.0→darker
            tileColor = float3(rawD, rawD, rawD);
        } else {
            tileColor = GetTileColor(
                gridTileIdx, albedo, N, metallic, roughness, ao,
                shadow, viewDepth, depth, color,
                nearPlane, farPlane, cascadeCount);
        }
        return float4(tileColor, 1.0f);
    }

    // ── BRDF debug split line ──
    // Draw a thin (2-pixel) vertical white line at the screen center to visually
    // delineate old BRDF (left) from new BRDF (right) in split-screen mode.
    if (brdfDebugMode == 1) {
        float pixelX = input.positionCS.x;
        // SV_Position is pixel center (e.g., 640.5 for pixel 640 on a 1280-wide RT).
        // We don't have explicit outputWidth in the cbuffer, but we can use
        // input.texcoord.x == 0.5 ± half-pixel tolerance for the divider.
        float halfPixelU = fwidth(input.texcoord.x);
        if (abs(input.texcoord.x - 0.5f) < halfPixelU) {
            return float4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    // Output linear HDR — tonemap applied in separate fullscreen pass.
    return float4(color, 1.0f);
}
