/// UzEngine Constant Buffer Definitions
///
/// Standard constant buffer layouts for per-frame and per-object data.
/// Bound as root CBVs in the global root signature for minimal overhead.
///
/// # Root Signature Layout
/// - `b0`: Root constants (descriptor index — existing)
/// - `b1`: Per-frame CB (ViewProj, camera position, time, lighting)
/// - `b2`: Per-object CB (model matrix, normal matrix, material index)
///
/// # Alignment
/// All CB structs are padded to 256-byte boundaries (D3D12 requirement).
/// Fields use `float4x4` (64 bytes) and `float4` (16 bytes) for HLSL alignment.
///
/// # See Also
/// - `cb_ring_allocator.h` — Ring-buffer allocator for uploading these CBs per frame.
/// - `root_slots.h` — Named constants for root parameter slot indices.

#pragma once

#include "cb_ring_allocator.h"
#include "vector3.h"
#include "mat4.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB — bound at b1 (root CBV, updated once per frame)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer data uploaded once per frame.
///
/// Contains camera transforms, global time values, and scene lighting data.
/// Bound as a root CBV at register `b1` so all shaders can access it without
/// descriptor indirection.
///
/// Matches the HLSL struct:
/// ```hlsl
/// cbuffer PerFrameCB : register(b1) {
///     float4x4 viewProj;
///     float4   cameraPos;     // xyz = position, w = unused
///     float4   timeParams;    // x = totalTime, y = deltaTime, z = frameCount, w = unused
///     float4   lightDir;      // xyz = normalised direction toward light, w = 0
///     float4   lightColor;    // xyz = color * intensity, w = unused
///     float4   ambientColor;  // xyz = ambient light color, w = unused
/// };
/// ```
struct PerFrameCB {
    Mat4  viewProj;             ///< Combined view-projection matrix (column-major).
    Vec4  cameraPos;            ///< xyz = world-space camera position, w = unused.
    Vec4  timeParams;           ///< x = total elapsed seconds, y = delta time, z = frame count (float),
                                ///< w = diagnostic sampling-seed override (`r.GI.Reflections.FreezeSeed`,
                                ///< negative = use the frame count).
    Vec4  lightDir;             ///< xyz = normalised direction toward the primary light source, w = 0.
    Vec4  lightColor;           ///< xyz = primary light color multiplied by intensity, w = unused.
    Vec4  ambientColor;         ///< xyz = ambient light color contribution, w = unused.
    Mat4  invViewProj;          ///< Inverse view-projection matrix for world position reconstruction (deferred lighting).
    float nearPlane  = 10.0f;     ///< Near clip plane distance in world units / cm (for depth linearization).
    float farPlane   = 100000.0f; ///< Far clip plane distance in world units / cm (for depth linearization).
    uint32_t irradianceSrvIndex  = UINT32_MAX; ///< SRV index for irradiance cubemap (UINT32_MAX = no IBL).
    uint32_t prefilteredSrvIndex = UINT32_MAX; ///< SRV index for prefiltered specular cubemap.
    uint32_t brdfLutSrvIndex     = UINT32_MAX; ///< SRV index for BRDF integration LUT.
    uint32_t prefilteredMipLevels = 8;         ///< Number of prefiltered mip levels (must match IBLBaker::kPrefilteredMipLevels).
    float skyLightIntensity = 1.0f;            ///< Multiplier for IBL ambient (SkyLight intensity).
    float skyLightColorR    = 1.0f;            ///< Sky light tint color (R).
    float skyLightColorG    = 1.0f;            ///< Sky light tint color (G).
    float skyLightColorB    = 1.0f;            ///< Sky light tint color (B).
    float skyLightMinAmbient = 0.0f;           ///< Minimum ambient level (0-1). Prevents fully black shadows.
    float exposure = 1.0f;                     ///< Exposure multiplier from auto-exposure (applied before tonemap).

    // ── Sky Irradiance SH (L2, 7 float4 = 112 bytes) ─────────────────────
    // L2 spherical harmonics for sky irradiance (7 float4 = 112 bytes).
    // Evaluated per-pixel with GetSkySHDiffuse(Normal).
    // When all zeros, IBL falls back to cubemap sampling.
    Vec4 skyIrradianceSH[7] = {};               ///< L2 SH coefficients for sky diffuse irradiance.

    /// Shadow debug visualization mode (0=off, see ShadowDebugMode enum).
    /// When non-zero, deferred_lighting_ps outputs debug colors instead of lit result.
    uint32_t shadowDebugMode = 0;

    // ── Aerial Perspective parameters (uses former padding) ───────────────
    // These are populated from SkyAtmosphereComponent by AtmosphereRenderer
    // and consumed by deferred_lighting_ps.hlsl for distance-based atmosphere fog.
    uint32_t aerialPerspectiveEnabled = 0;       ///< 1 = apply aerial perspective in deferred lighting.
    uint32_t transmittanceLutSrvIndex = UINT32_MAX; ///< SRV index for transmittance LUT (256x64 RGBA16F).
    float    apBottomRadiusKm = 6360.0f;         ///< Planet surface radius (km).
    float    apTopRadiusKm    = 6420.0f;         ///< Top of atmosphere radius (km).
    float    apRayleighBetaX  = 5.802e-3f;       ///< Rayleigh scattering X (km^-1).
    float    apRayleighBetaY  = 13.558e-3f;      ///< Rayleigh scattering Y (km^-1).
    float    apRayleighBetaZ  = 33.1e-3f;        ///< Rayleigh scattering Z (km^-1).
    float    apRayleighScaleH = 8.0f;            ///< Rayleigh scale height (km).
    float    apMieBeta        = 3.996e-3f;       ///< Mie scattering coefficient (km^-1).
    float    apMieScaleH      = 1.2f;            ///< Mie scale height (km).
    float    apMieAnisotropy  = 0.8f;            ///< Mie HG anisotropy parameter.
    float    apSunIntensity   = 1.0f;            ///< Sun illuminance for AP inscattering.
    float    apRayleighScale  = 1.0f;            ///< Artistic Rayleigh scattering multiplier.
    float    apMieScale       = 1.0f;            ///< Artistic Mie scattering multiplier.

    // ── ACES tone curve parameters ──────────────────────────────────────
    /// Default artist tone curve parameters (ACES-style S-curve).
    float    toneCurveSlope      = 0.88f;        ///< Steepness of the linear (straight) segment.
    float    toneCurveToe        = 0.55f;        ///< Size of the toe region (dark rolloff).
    float    toneCurveShoulder   = 0.26f;        ///< Size of the shoulder region (highlight rolloff).
    float    toneCurveBlackClip  = 0.0f;         ///< Lifts black level (adds to min output).
    float    toneCurveWhiteClip  = 0.04f;        ///< Lowers white ceiling (subtracts from max output).

    uint32_t gtaoSrvIndex    = UINT32_MAX;       ///< SRV index for GTAO filtered AO texture (UINT32_MAX = no GTAO).
    uint32_t brdfDebugMode   = 0;               ///< BRDF debug (0=off, 1=split-screen: left=old Lambertian+SchlickGGX, right=new Burley+SmithJoint).

    // Controls how much aerial perspective inscattering is reduced when height fog
    // is active, to prevent double-fogging (both AP and height fog adding haze).
    // 0 = AP unaffected by fog, 1 = AP inscattering fully replaced by fog.
    float    heightFogContribution = 0.0f;       ///< AP reduction factor when height fog is active (0=no reduction, 1=full).

    // ── Post-process film effects ───────────────────────────────────────
    float    filmGrainIntensity = 0.0f;         ///< Film grain intensity (0=off).
    float    vignetteIntensity  = 0.4f;         ///< Vignette darkening intensity.
    float    chromaAbIntensity  = 0.0f;         ///< Chromatic aberration / scene fringe intensity (0=off).

    /// Screen-space contact shadow ray length. 0.0 = disabled.
    /// Positive = screen-space fraction (0.02 = 2% of depth-scaled screen ray length).
    float    contactShadowLength = 0.01f;

    uint32_t ssrSrvIndex = UINT32_MAX;             ///< SRV index for resolved SSR texture (UINT32_MAX = no SSR).

    /// Global mipmap LOD bias applied to all material texture samples.
    /// Computed as log2(screenPercentage / 100). At 100% = 0 (no bias).
    /// At 50% = -1.0 (shift down one mip to compensate for lower render resolution).
    /// At 200% = +1.0 (shift up one mip for supersampling).
    float    mipBias = 0.0f;

    // ── SSGI (Screen-Space Global Illumination) ────────────────────────
    uint32_t ssgiSrvIndex   = UINT32_MAX;          ///< SRV index for upsampled SSGI indirect diffuse (UINT32_MAX = disabled).
    uint32_t ssgiAOSrvIndex = UINT32_MAX;          ///< SRV index for SSGI ambient occlusion mask (UINT32_MAX = disabled).
    uint32_t ssgiEnabled    = 0;                   ///< 1 = SSGI active, 0 = disabled.

    // ── Radiance Cache (SDF-based GI) ─────────────────────────────────
    uint32_t radianceCacheEnabled = 0;              ///< 1 = radiance cache GI active, 0 = disabled.
    uint32_t rcProbeSHBufferSrv   = UINT32_MAX;     ///< SRV index for probe SH StructuredBuffer<float4>.
    uint32_t rcProbesPerAxis      = 16;             ///< Probes per axis (16).
    float    rcGridExtent         = 5000.0f;        ///< Half-size of the probe grid in world units (cm).
    float    rcGridOriginX        = 0.0f;           ///< Probe grid origin X (minimum corner).
    float    rcGridOriginY        = 0.0f;           ///< Probe grid origin Y.
    float    rcGridOriginZ        = 0.0f;           ///< Probe grid origin Z.
    float    rcGiIntensity       = 1.0f;            ///< GI intensity multiplier (default 1.0).

    // ── Aerial Perspective Volume ─────────────────────────────────────
    /// SRV index for the precomputed 32x32x16 RGBA16F 3D texture containing
    /// per-voxel inscattering (RGB) + transmittance (A). Sampled in deferred
    /// lighting to replace per-pixel AP ray march. UINT32_MAX = no volume.
    uint32_t apVolumeSrvIndex = UINT32_MAX;

    // ── Reflection Probes ───────────────────────────────────────────────
    /// Number of active reflection probes in the structured buffer (0 = sky IBL only).
    uint32_t reflectionProbeCount = 0;
    /// SRV index for StructuredBuffer<ReflectionProbeGPU> in the bindless heap.
    /// UINT32_MAX = no probes uploaded (fall back to global sky IBL).
    uint32_t reflectionProbeBufferSrvIndex = UINT32_MAX;

    // ── Color grading (at end of scalar section) ──────────────────────
    float    colorSaturation   = 1.1f;              ///< Color saturation multiplier (0=grayscale, 1=unchanged, >1=vivid).
    float    colorContrast     = 1.0f;              ///< Contrast power exponent (1=unchanged, >1=higher contrast).

    // ── Cloud Shadow parameters (from primary DirectionalLightComponent) ──
    float    cloudShadowStrength = 1.0f;            ///< Overall cloud shadow strength multiplier [0,1].
    float    cloudShadowOnAtmosphereStrength = 1.0f; ///< Cloud shadow strength on atmosphere.
    float    cloudShadowOnSurfaceStrength = 1.0f;   ///< Cloud shadow strength on opaque surfaces.
    float    cloudShadowDepthBias = 0.0f;           ///< Depth bias for cloud shadow map sampling.
    float    cloudShadowExtent = 128.0f;            ///< World-space extent of cloud shadow map (km).
    float    cloudShadowMapResolutionScale = 1.0f;  ///< Multiplier for cloud shadow map resolution.
    float    cloudShadowRaySampleCountScale = 1.0f; ///< Multiplier for cloud shadow ray-march samples.
    float    cloudScatteredLuminanceScaleR = 1.0f;  ///< Cloud scattered luminance scale R.
    float    cloudScatteredLuminanceScaleG = 1.0f;  ///< Cloud scattered luminance scale G.
    float    cloudScatteredLuminanceScaleB = 1.0f;  ///< Cloud scattered luminance scale B.
    uint32_t castCloudShadows = 1;                  ///< 1 = light casts shadows through volumetric clouds.
    uint32_t castShadowsOnClouds = 1;               ///< 1 = light casts shadows onto the cloud layer itself.
    uint32_t castShadowsOnAtmosphere = 1;           ///< 1 = cloud shadows darken the atmosphere (aerial perspective).

    // ── Light Shaft Occlusion (from primary DirectionalLightComponent) ──
    uint32_t enableLightShaftOcclusion = 1;         ///< 1 = screen-space light shaft occlusion enabled.
    float    occlusionMaskDarkness = 0.5f;          ///< How dark the occlusion mask can get [0,1].
    float    occlusionDepthRange = 10000.0f;        ///< Max depth range for occlusion (cm).
    float    lightShaftOverrideDirectionX = 0.0f;   ///< Manual light shaft direction override X.
    float    lightShaftOverrideDirectionY = 0.0f;   ///< Manual light shaft direction override Y.
    float    lightShaftOverrideDirectionZ = 0.0f;   ///< Manual light shaft direction override Z.
    float    shadowSourceAngleFactor = 1.0f;        ///< Multiplier for light source angle in shadow penumbra.
    float    shadowCascadeBiasDistribution = 1.0f;  ///< Controls shadow bias distribution across cascades.

    // ── SkyLight Occlusion parameters ─────────────────────────────────
    float    slOcclusionMaxDistance = 1000.0f;      ///< Max influence distance for AO/DFAO occlusion (cm).
    float    slOcclusionExponent = 1.0f;            ///< Exponent for occlusion falloff curve.
    float    slOcclusionTintR = 0.0f;               ///< Tint color for occluded areas R.
    float    slOcclusionTintG = 0.0f;               ///< Tint color for occluded areas G.
    float    slOcclusionTintB = 0.0f;               ///< Tint color for occluded areas B.
    uint32_t slCloudAmbientOcclusion = 1;           ///< 1 = cloud-based ambient occlusion on sky light.
    float    slCloudAOStrength = 0.5f;              ///< Cloud ambient occlusion strength [0,1].
    float    slCloudAOExtent = 64.0f;               ///< World-space extent of cloud AO map (km).
    float    slCloudAOMapResolutionScale = 1.0f;    ///< Cloud AO map resolution multiplier.
    float    slCloudAOApertureScale = 0.0f;         ///< Cone aperture for cloud AO sampling.
    float    slSkyDistanceThreshold = 150000.0f;    ///< Max distance for sky light cubemap capture (cm).
    float    slOcclusionContrast = 0.2f;            ///< Contrast adjustment for sky light occlusion.

    // ── Environment Map Importance Sampling ─────────────────────────────
    /// SRV index for StructuredBuffer<AliasEntry> (O(1) env sampling). UINT32_MAX = disabled.
    uint32_t envSamplerAliasSrv = UINT32_MAX;
    /// SRV index for luminance pyramid mip 0 (equirect R32F, for PDF evaluation). UINT32_MAX = disabled.
    uint32_t envSamplerLumMip0Srv = UINT32_MAX;
    /// Total luminance sum from environment map (for PDF normalization). 0 = disabled.
    float    envSamplerTotalLuminance = 0.0f;
    /// Texel count of luminance mip 0 (width * height). 0 = disabled.
    uint32_t envSamplerTexelCount = 0;
    /// Width of luminance pyramid mip 0 (for texel index → UV conversion).
    uint32_t envSamplerLumWidth = 0;
    /// Height of luminance pyramid mip 0.
    uint32_t envSamplerLumHeight = 0;
    /// SRV index for temporally-accumulated env MIS specular (RGBA16F). UINT32_MAX = disabled.
    uint32_t envMisAccumulatedSrv = UINT32_MAX;

    // ── Shader CVar parameters (Phase 1 — no hardcoded values) ─────────
    float    shadowPCFPenumbra       = 2.0f;           ///< CVar r.Shadow.PCFPenumbraRadius — PCF texel radius.
    float    shadowPCSSMinPenumbra   = 0.5f;           ///< CVar r.Shadow.PCSSMinPenumbra — min penumbra clamp.
    float    shadowPCSSMaxPenumbra   = 16.0f;          ///< CVar r.Shadow.PCSSMaxPenumbra — max penumbra clamp.
    float    shadowESMExponent       = 80.0f;          ///< CVar r.Shadow.ESMExponentConstant — ESM warp constant.
    uint32_t shadowContactSteps      = 16;             ///< CVar r.Shadow.ContactShadowSteps — ray march steps.
    float    shadowContactThickness  = 0.005f;         ///< CVar r.Shadow.ContactShadowThickness — depth comparison.
    float    bloomKnee               = 0.5f;           ///< CVar r.Bloom.Knee — soft threshold multiplier.
    float    exposureKeyValue        = 0.18f;          ///< CVar r.Exposure.KeyValue — Reinhard mid-tone target.
    float    exposureSpeedUp         = 3.0f;           ///< CVar r.Exposure.SpeedUp — adaptation speed (f-stops/sec).
    float    exposureSpeedDown       = 1.0f;           ///< CVar r.Exposure.SpeedDown — adaptation speed (f-stops/sec).
    float    exposureLowPercent      = 10.0f;          ///< CVar r.Exposure.LowPercent — histogram low percentile.
    float    exposureHighPercent     = 90.0f;          ///< CVar r.Exposure.HighPercent — histogram high percentile.
    float    materialDielectricF0    = 0.04f;          ///< CVar r.Material.DielectricF0 — Fresnel reflectance.
    float    materialMinGGXAlpha     = 0.0016f;        ///< CVar r.Material.MinGGXAlpha — prevents specular aliasing.

    // ── Brixelizer GI (AMD FidelityFX) ────────────────────────────────
    /// SRV index for Brixelizer GI diffuse output (RGBA16F). UINT32_MAX = disabled.
    /// Specular GI SRV is at (brixelizerGIDiffuseSrv + 1) by allocation convention.
    uint32_t brixelizerGIDiffuseSrv = UINT32_MAX;

    // ── Light Probe Volume (author-placed irradiance grid) ───────────────
    /// Author-placed L2 SH probe volume. Sampled in deferred lighting via
    /// trilinear blend of 8 surrounding probes for spatially-varying indirect
    /// diffuse in indoor scenes. UINT32_MAX = no volume.
    /// @ref Tatarchuk 2005 "Irradiance Volumes for Games"
    /// @ref Greger 1998 "The Irradiance Volume"
    uint32_t lpvEnabled       = 0;          ///< 1 = light probe volume active.
    uint32_t lpvProbeBufferSrv = UINT32_MAX;///< SRV of probe SH StructuredBuffer<float4>.
    uint32_t lpvDimX          = 0;          ///< Probes along X.
    uint32_t lpvDimY          = 0;          ///< Probes along Y.
    uint32_t lpvDimZ          = 0;          ///< Probes along Z.
    float    lpvOriginX       = 0.0f;       ///< Volume AABB min corner X (cm).
    float    lpvOriginY       = 0.0f;       ///< Volume AABB min corner Y (cm).
    float    lpvOriginZ       = 0.0f;       ///< Volume AABB min corner Z (cm).
    float    lpvInvSizeX      = 0.0f;       ///< 1 / (extentMax.x - extentMin.x).
    float    lpvInvSizeY      = 0.0f;       ///< 1 / (extentMax.y - extentMin.y).
    float    lpvInvSizeZ      = 0.0f;       ///< 1 / (extentMax.z - extentMin.z).
    float    lpvIntensity     = 1.0f;       ///< Intensity multiplier on probe contribution.

    // ── DDGI atlases (Majercik 2019) ──────────────────────────────────
    /// SRVs for the DDGI octahedral irradiance and depth atlases. Sampled
    /// in deferred lighting using Chebyshev visibility against the depth
    /// atlas to suppress light leaks. UINT32_MAX = no DDGI atlas available.
    /// @ref Majercik 2019 "DDGI" — atlas layout & Chebyshev visibility
    uint32_t ddgiIrradianceSrv = UINT32_MAX;
    uint32_t ddgiDepthSrv      = UINT32_MAX;
    uint32_t ddgiProbesPerAxis = 0;          ///< Probes per axis (cube grid).
    float    ddgiProbeSpacing  = 0.0f;       ///< Probe spacing in cm.
    float    ddgiOriginX       = 0.0f;       ///< Probe grid min corner X.
    float    ddgiOriginY       = 0.0f;
    float    ddgiOriginZ       = 0.0f;
    float    ddgiIntensity     = 1.0f;       ///< Intensity multiplier on DDGI contribution.

    // ── Indirect AO parameters ───────────────────────────────────────────
    /// Exponent applied to AO for indirect light attenuation.
    /// Higher values = more aggressive occlusion on indirect (ambient) light.
    /// Direct light uses linear AO; indirect uses pow(ao, exponent).
    /// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR" — §4.9.3
    float    indirectAOExponent = 2.0f;     ///< CVar r.IndirectAO.Exponent — default 2.0.

    uint32_t sdfReflectionsEnabled = 0;
    uint32_t sdfReflectionSkySrv = UINT32_MAX;
    uint32_t sdfReflectionSamples = 4;
    Vec4     sdfReflectionClipmaps[4] = {};
    uint32_t sdfReflectionClipmapSrv[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    float    sdfReflectionTraceDistance = 5000.0f;
    float    sdfReflectionVoxelSize = 0.0f;
    uint32_t sdfReflectionTraceSteps = 96;
    float    sdfReflectionIntensity = 1.0f;
    uint32_t sdfDiffuseSamples = 0;
    uint32_t sdfReflectionPlainMean = 0;   ///< 1 = GatherSdfReflections averages rays plainly (r.GI.Reflections.HdrWeight=0); 0 = Karis weight.
    /// CVar r.GI.FinalGather.SampleRotation — 0 = radical inverse of the frame-strided seed
           /// (shipping), 1 = Kronecker advance in the frame index (candidate, ledger H-31).
    uint32_t sdfSampleRotation = 0;
    uint32_t materialMapMask = 0;  ///< Diagnostic: bit0 = ignore normal maps, bit1 = ignore material AO maps (r.Material.NormalMap / .AOMap = 0).
    uint32_t sdfRawClipmapSrv[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};

    // ── Padding to next 256-byte boundary (1024 bytes) ────────────────
    // Region dump (diagnostics: per-pixel lighting/TAA intermediates for a screen rectangle,
    // see SceneRenderer::SetRegionDump). Carved out of the former tail padding: offsets 992..1011.
    uint32_t debugRegionUav = UINT32_MAX;  ///< Bindless UAV of the region-dump StructuredBuffer<float4>; UINT32_MAX = off.
    uint32_t debugRegionSrv = UINT32_MAX;  ///< Bindless SRV of the rect table (StructuredBuffer<uint4>, 2 elements per rect).
    uint32_t debugRegionCount = 0;         ///< Number of rectangles (0 = off).
    uint32_t debugRegionStamp = 0;         ///< Frame stamp the GPU writes into each rect footer.
    uint32_t debugRegionFields = 0;        ///< Field-group bitmask, see shaders/runtime/common/region_dump.hlsli.
    float    materialRoughnessOverride = -1.0f;  ///< CVar r.Material.RoughnessOverride (-1 = off, diagnostic).
    float    materialMetallicOverride  = -1.0f;  ///< CVar r.Material.MetallicOverride (-1 = off, diagnostic).
    uint32_t componentReplaySrv = UINT32_MAX;  ///< CVar r.GI.ComponentReplayFile buffer (diagnostic).
};

static_assert(offsetof(PerFrameCB, sdfSampleRotation) == 968);
static_assert(offsetof(PerFrameCB, materialMapMask) == 972);
static_assert(offsetof(PerFrameCB, sdfRawClipmapSrv) == 976);
static_assert(offsetof(PerFrameCB, debugRegionUav) == 992);
static_assert(offsetof(PerFrameCB, debugRegionFields) == 1008);
static_assert(offsetof(PerFrameCB, materialRoughnessOverride) == 1012);
static_assert(offsetof(PerFrameCB, materialMetallicOverride) == 1016);
static_assert(offsetof(PerFrameCB, componentReplaySrv) == 1020);
static_assert(sizeof(PerFrameCB) == 1024, "PerFrameCB must be 1024 bytes (4 x 256 D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// PerObjectCB — bound at b2 (root CBV, updated per draw call)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-object constant buffer data uploaded per draw call.
///
/// Contains object transforms and material reference. Bound as a root CBV
/// at register `b2`.
///
/// Matches the HLSL struct:
/// ```hlsl
/// cbuffer PerObjectCB : register(b2) {
///     uint     albedoIndex;
///     uint     normalIndex;
///     uint     roughnessIndex;
///     uint     metallicIndex;
///     uint     aoIndex;
///     uint     emissiveIndex;
///     float    alphaClipThreshold;
///     uint     opacitySrvIndex;
///     float4x4 model;
///     float4x4 normalMatrix;
///     float3   emissiveColor;
///     float    emissiveIntensity;
///     float3   baseColorTint;
///     float    roughnessMultiplier;
///     float    metallicMultiplier;
///     float    normalIntensity;
///     float2   uvScale;
///     float2   uvOffset;
///     float    aoIntensity;
///     float    opacity;
///     // ... POM fields ...
///     uint     blendMode;
/// };
/// ```
///
/// # Layout Note
/// Scalar members (indices) are placed BEFORE the float4x4 matrices to avoid
/// SPIRV std140 offset corruption. In std140, `row_major float4x4` members
/// may shift subsequent uint offsets on some DXC versions. By placing scalars
/// first (at a well-known offset 0), both DX12 (DXIL) and Vulkan (SPIRV) read
/// the correct values regardless of matrix storage qualifier handling.
struct PerObjectCB {
    // ── Scalar members first (offset 0) — immune to std140 matrix padding issues ──
    uint32_t albedoIndex;      ///< SRV heap index for BaseColor map (0 = white default)
    uint32_t normalIndex;      ///< SRV heap index for normal map (0 = flat normal)
    uint32_t roughnessIndex;   ///< SRV heap index for roughness map (0 = 0.5 default)
    uint32_t metallicIndex;    ///< SRV heap index for metallic map (0 = 0.0 default)
    uint32_t aoIndex;          ///< SRV heap index for AO map (0 = 1.0 default)
    uint32_t emissiveIndex;    ///< SRV heap index for emissive map (0 = no emissive texture, use color only)
    float    alphaClipThreshold = 0.3333f; ///< Alpha test threshold for Masked blend mode. Pixels with opacity below this are discarded.
    uint32_t opacitySrvIndex = 0; ///< SRV index for opacity/alpha mask texture (0 = none, use scalar opacity).
    // ── Matrices after scalars (offset 32) ──
    Mat4     model;            ///< Object-to-world transform
    Mat4     normalMatrix;     ///< transpose(inverse(model)) — for lighting
    // ── Emissive material parameters (offset 160) ──
    /// Emissive output = emissiveColor * emissiveIntensity.
    float    emissiveColorR  = 0.0f;  ///< Emissive tint color R (linear). Default 0 = no emission.
    float    emissiveColorG  = 0.0f;  ///< Emissive tint color G (linear).
    float    emissiveColorB  = 0.0f;  ///< Emissive tint color B (linear).
    float    emissiveIntensity = 0.0f; ///< Emissive brightness multiplier. 0 = no emission. Values > 1.0 drive bloom.

    // ── Material parameters (offset 176) ──
    // These multiply/transform texture-sampled values in the GBuffer pass.
    // Default values are neutral (multipliers=1, offsets=0) so existing
    // rendering is unchanged when no Material/MaterialInstance is assigned.

    float    baseColorTintR   = 1.0f;  ///< Albedo color multiplier R. 1.0 = unmodified.
    float    baseColorTintG   = 1.0f;  ///< Albedo color multiplier G.
    float    baseColorTintB   = 1.0f;  ///< Albedo color multiplier B.

    float    roughnessMultiplier = 1.0f; ///< Roughness texture multiplier. 1.0 = unmodified.
    float    metallicMultiplier  = 1.0f; ///< Metallic texture multiplier. 1.0 = unmodified.
    float    normalIntensity     = 1.0f; ///< Normal map strength (0=flat, 1=full).
    float    uvScaleX  = 1.0f;          ///< Texture tiling X. 1.0 = no tiling.
    float    uvScaleY  = 1.0f;          ///< Texture tiling Y.
    float    uvOffsetX = 0.0f;          ///< Texture offset X.
    float    uvOffsetY = 0.0f;          ///< Texture offset Y.

    float    aoIntensity = 1.0f;        ///< AO map strength. 1.0 = full AO effect.

    /// Overall material opacity. 1.0 = fully opaque.
    float    opacity     = 1.0f;        ///< Material opacity (1=opaque).

    // ── POM (Parallax Occlusion Mapping) parameters ─────────────────────


    float    displacementScale = 0.0f;  ///< POM height scale. 0 = POM disabled.
    uint32_t heightmapSrvIndex = 0;     ///< SRV index for heightmap texture (0 = none).
    float    pomMinLayers      = 8.0f;  ///< Minimum ray march steps (head-on view).
    float    pomMaxLayers      = 32.0f; ///< Maximum ray march steps (grazing view).
    float    pomFadeStart      = 1000.0f; ///< Distance (cm) where POM begins fading.
    float    pomFadeEnd        = 2000.0f; ///< Distance (cm) where POM is fully disabled.
    float    pomSelfShadow     = 0.0f;  ///< Self-shadow strength. 0 = disabled, 1 = full.

    /// Material flags — packed bitfield:
    ///   bits [1:0]   = blend mode (0=Opaque, 1=Masked, 2=Translucent, 3=Additive).
    ///   bit  [2]     = ORM packed texture mode (0=separate, 1=packed R=AO G=Rough B=Metal).
    ///   bit  [3]     = twoSided (0=back-face cull, 1=no cull). PSO-level only, not read in shader.
    ///   bits [8:15]  = specular * 255 (dielectric reflectance scale, 0.0-1.0).
    ///   bits [16:23] = envMultiplier * 127.5 (IBL intensity, 0.0-2.0, 128=1.0).
    ///   bits [24:27] = shadingModelID (EShadingModel, 4-bit, 0-15).
    ///   bits [28:31] = clearCoatRoughness * 15 (4-bit, 16 levels, only when smID==4).
    uint32_t materialFlags = 0;
};

static_assert(sizeof(PerObjectCB) == 256, "PerObjectCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// LightingCB — bound at b2 (root CBV slot 2, reused from PerObjectCB slot in fullscreen deferred passes)
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of point lights supported in the deferred lighting pass.
static constexpr uint32_t kMaxPointLights = 64;

/// Maximum number of spot lights supported in the deferred lighting pass.
static constexpr uint32_t kMaxSpotLights = 16;

/// Point light data for the deferred lighting pass.
///
/// Each point light carries position, attenuation, color, and physically-based
/// parameters (source radius for area approximation, falloff, per-light scales).
/// Packed to 48 bytes (three float4s) for efficient GPU access.
///
/// Matches the HLSL struct:
/// ```hlsl
/// struct PointLightData {
///     float3 position;        // world-space position
///     float  radius;          // attenuation radius
///     float3 color;           // color * intensity (linear HDR)
///     float  sourceRadius;    // area light sphere radius (0=point)
///     float  falloffExponent; // custom falloff (when inverse square disabled)
///     float  specularScale;   // 0-1 specular multiplier
///     float  diffuseScale;    // 0-1 diffuse multiplier
///     uint   flags;           // bit0=useInverseSquared
/// };
/// ```
struct PointLightData {
    Vec3     position;         ///< World-space position of the light source.
    float    radius;           ///< Attenuation radius — light contribution falls to zero at this distance.
    Vec3     color;            ///< Light color multiplied by intensity (linear HDR, not clamped).
    float    sourceRadius;     ///< Sphere radius for area light approximation. 0 = point light.
    float    falloffExponent;  ///< Custom falloff exponent (used when inverse square is disabled).
    float    specularScale;    ///< Per-light specular highlight multiplier (0-1). Default: 1.0.
    float    diffuseScale;     ///< Per-light diffuse contribution multiplier (0-1). Default: 1.0.
    uint32_t flags;            ///< Bit flags: bit 0 = useInverseSquaredFalloff.
    /// Index into LocalShadowCB::shadows[] for this light's shadow data.
    /// For point lights, this is the base index — 6 consecutive entries (one per cubemap face).
    /// 0xFFFFFFFF = no shadow (light does not cast shadows).
    uint32_t shadowIndex = UINT32_MAX;
    float    _pointPad[3] = {};  ///< Pad to 64 bytes (4 float4s).
};

/// PointLightData flag bits.
static constexpr uint32_t kLightFlag_InverseSquared = 1u << 0;

static_assert(sizeof(PointLightData) == 64, "PointLightData must be 64 bytes (four float4s)");

/// Spot light data for the deferred lighting pass.
///
/// Extends point light data with cone direction and angular attenuation.
/// Uses precomputed SpotAngles packing: cosOuterAngle and invCosConeDifference
/// are computed on the CPU.
///
/// Packed to 64 bytes (four float4s) for efficient GPU access.
///
/// Matches the HLSL struct:
/// ```hlsl
/// struct SpotLightData {
///     float3 position;              // world-space position
///     float  radius;                // attenuation radius
///     float3 color;                 // color * intensity (linear HDR)
///     float  cosOuterAngle;         // cosine of outer cone half-angle
///     float3 direction;             // normalised world-space cone direction
///     float  invCosConeDifference;  // 1 / (cosInner - cosOuter)
///     float  falloffExponent;       // custom falloff (when inverse square disabled)
///     float  specularScale;         // 0-1 specular multiplier
///     float  diffuseScale;          // 0-1 diffuse multiplier
///     uint   flags;                 // bit0=useInverseSquared
/// };
/// ```
struct SpotLightData {
    Vec3     position;              ///< World-space position of the light source.
    float    radius;                ///< Attenuation radius — light contribution falls to zero at this distance.
    Vec3     color;                 ///< Light color multiplied by intensity (linear HDR, not clamped).
    float    cosOuterAngle;         ///< Cosine of the outer cone half-angle.
    Vec3     direction;             ///< Normalised world-space cone direction.
    float    invCosConeDifference;  ///< 1 / (cosInner - cosOuter).
    float    falloffExponent;       ///< Custom falloff exponent (used when inverse square is disabled).
    float    specularScale;         ///< Per-light specular highlight multiplier (0-1). Default: 1.0.
    float    diffuseScale;          ///< Per-light diffuse contribution multiplier (0-1). Default: 1.0.
    uint32_t flags;                 ///< Bit flags: bit 0 = useInverseSquaredFalloff.
    /// Index into LocalShadowCB::shadows[] for this light's shadow data.
    /// 0xFFFFFFFF = no shadow (light does not cast shadows).
    uint32_t shadowIndex = UINT32_MAX;
    float    _spotPad[3] = {};      ///< Pad to 80 bytes (5 float4s).
};

static_assert(sizeof(SpotLightData) == 80, "SpotLightData must be 80 bytes (five float4s)");

/// Lighting constant buffer for the deferred lighting pass, bound at register `b2`.
///
/// Contains arrays of point and spot lights with their respective counts.
/// The shader iterates each array and accumulates contributions.
///
/// Padded to a 256-byte multiple as required by D3D12 constant buffer alignment.
///
/// # Layout (5632 bytes = 22 * 256)
/// - pointLights[64]:  64 * 64 = 4096 bytes
/// - pointLightCount:  4 bytes
/// - spotLightCount:   4 bytes
/// - _pad:             8 bytes
/// - spotLights[16]:   16 * 80 = 1280 bytes
/// - _pad2:            240 bytes
/// Total: 5632 bytes
struct LightingCB {
    PointLightData pointLights[kMaxPointLights]; ///< Point light array (64 * 64 = 4096 bytes).
    uint32_t       pointLightCount = 0;          ///< Number of active point lights in the array.
    uint32_t       spotLightCount  = 0;          ///< Number of active spot lights in the array.
    uint32_t       _pad[2] = {};                 ///< Align counts to 16 bytes.
    SpotLightData  spotLights[kMaxSpotLights];   ///< Spot light array (16 * 80 = 1280 bytes).
    float          _pad2[60] = {};               ///< Pad to 5632 bytes (256-byte multiple).
};

static_assert(sizeof(LightingCB) % 256 == 0, "LightingCB must be a multiple of 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// LocalLightShadowData — per-light shadow projection data for local lights
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of local light shadows supported per frame.
static constexpr uint32_t kMaxLocalLightShadows = 16;

/// Per-light shadow data for a single local (point/spot) light.
///
/// Contains the light-space view-projection matrix for shadow depth comparison
/// and the atlas UV rect for tile lookup. The `bias` field is applied as a
/// depth offset before comparison.
///
struct LocalLightShadowData {
    Mat4     viewProj;       ///< Light-space view-projection matrix (world -> clip).
    float    atlasOffsetX;   ///< UV offset X in atlas [0,1].
    float    atlasOffsetY;   ///< UV offset Y in atlas [0,1].
    float    atlasScale;     ///< UV scale (tile size / atlas size, e.g., 0.25 for 1024/4096).
    float    bias;           ///< Shadow bias in cm (JSON `shadowBias`, default 0.5): the receiver's distance
                             ///< is reduced by this much before the comparison.
    float    lightPosX;      ///< Light position (world, cm). The tile stores distance / radius
    float    lightPosY;      ///< (shadow_depth_linear_ps.hlsl), so the consumer needs the same
    float    lightPosZ;      ///< origin and range to compare in linear units (block A.1).
    float    radius;         ///< Light range in cm.
};

static_assert(sizeof(LocalLightShadowData) == 96, "LocalLightShadowData must be 96 bytes (Mat4 + 8 floats)");

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB — bound at b4 (root CBV, updated once per frame for shadow sampling)
// ─────────────────────────────────────────────────────────────────────────────

/// Shadow data constant buffer for cascade shadow map sampling in pixel shaders.
///
/// Contains per-cascade light-space view-projection matrices, split distances,
/// per-cascade bias, fade parameters, and shadow atlas SRV index.
/// Supports up to 8 cascades (4 main + 4 far shadow cascades).
/// Bound at register `b4`.
///
/// Padded to a 256-byte multiple as required by D3D12 constant buffer alignment.
///
/// # Layout (2304 bytes = 9 * 256)
/// - cascadeViewProj[8]:                8 * 64 = 512 bytes
/// - cascadeSplits/fades/biases:        128 bytes
/// - scalar params + padding:           128 bytes
/// ── CSM section subtotal:             768 bytes
/// - localShadows[16]:                  16 * 96 = 1536 bytes
/// Total: 2304 bytes
struct ShadowDataCB {
    Mat4     cascadeViewProj[8];    ///< Light-space viewProj per cascade (8 * 64 = 512B).
    float    cascadeSplits[8];      ///< View-space extended far split per cascade.
    float    cascadeFadeStarts[8];  ///< View-space original (un-extended) far split per cascade.
                                    ///< Blending starts at fadeStart, ends at split (extended).
    float    cascadeDepthBias[8];   ///< Per-cascade computed depth bias (normalized + distributed).
    float    cascadeNormalBias[8];  ///< Per-cascade computed normal bias (scaled by texel size).
    uint32_t shadowAtlasIndex;      ///< SRV heap index for shadow atlas depth texture (dynamic).
    uint32_t cascadeCount;          ///< Number of active cascades (1 to 8, main + far).
    float    shadowMapSize;         ///< Per-cascade resolution in texels (e.g., 2048).
    float    receiverBias;          ///< Shadow receiver bias (default: 0.9).
    float    shadowFadeStart;       ///< World distance where last-cascade fade begins.
    float    shadowFadeEnd;         ///< World distance where shadows fully disappear.
    uint32_t filterMethod;          ///< 0=5x5 PCF (default), 1=Poisson 16-tap, 2=PCSS.
    float    tanLightSourceAngle;   ///< tan(sun disk angle) for PCSS penumbra estimation.
    float    maxSoftKernelSize;     ///< Max PCSS kernel radius in texels (default: 40).
    uint32_t atlasColumns;          ///< Number of columns in the atlas grid (2, 3, or 4).
    uint32_t staticShadowAtlasIndex = UINT32_MAX; ///< SRV heap index for cached static shadow atlas. UINT32_MAX = no cache.
    uint32_t useShadowCache = 0;    ///< 0 = no cache (single atlas), 1 = composite static + dynamic.
    uint32_t dfsEnabled = 0;        ///< 1 = Distance Field Shadows active, 0 = disabled.
    uint32_t dfsOutputSrvIndex = UINT32_MAX; ///< SRV index for DFS result texture (R16_FLOAT screen-space). UINT32_MAX = not bound.
    uint32_t localShadowAtlasIndex = UINT32_MAX; ///< SRV heap index for local light shadow atlas. UINT32_MAX = none.
    uint32_t localShadowCount = 0;  ///< Number of active local light shadows (0 to kMaxLocalShadowTiles).
    float    localShadowNormalOffset = 0.005f; ///< r.Shadow.Local.NormalOffset (block A.1).
    float    _pad2[15] = {};        ///< Pad CSM section to 768 bytes.

    // ── Local Light Shadow Data (appended after CSM section) ──────────────
    // Up to 16 local light shadows. Each entry stores the light-space viewProj
    // matrix and the atlas UV rect for shadow sampling.
    //
    LocalLightShadowData localShadows[kMaxLocalLightShadows]; ///< Per-light shadow data (16 * 96 = 1536B).
};

static_assert(sizeof(ShadowDataCB) == 2304, "ShadowDataCB must be 2304 bytes (256-byte multiple)");

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmosphereCB — bound at b2 (reusing per-object slot in fullscreen sky pass)
// ─────────────────────────────────────────────────────────────────────────────

/// Sky atmosphere constant buffer for the sky rendering pass.
///
/// All distances in KILOMETERS, scattering coefficients in km^-1.
/// Scattering coefficients are km^-1. Planet center is placed at
/// (0, -bottomRadiusKm, 0) in sky-space so world Y=0 = sea level.
///
/// Bound at register `b2` (reuses the per-object CBV slot, which is unused
/// in fullscreen passes).
///
/// Padded to 256 bytes as required by D3D12 constant buffer alignment.
struct SkyAtmosphereCB {
    Vec4  rayleighScattering;   ///< xyz = coefficients (km^-1), w = scale height (km).
    Vec4  mieScattering;        ///< x = coefficient (km^-1), y = anisotropy g, z = scale height (km), w = unused.
    Vec4  groundAlbedo;         ///< xyz = ground reflectance, w = unused.
    Vec4  sunIlluminance;       ///< xyz = sun color * illuminance, w = sun angular radius (rad).
    float bottomRadiusKm;       ///< Planet radius in km (center to sea level).
    float atmosphereHeightKm;   ///< Atmosphere thickness in km.
    float sunDiskIntensity;     ///< Multiplier for the sun disk brightness.
    float multiScatteringFactor; ///< Multi-scattering approximation (0=single, 1=full). Default: 1.0.
    float rayleighScatteringScale; ///< Artistic multiplier for Rayleigh (default 1.0).
    float mieScatteringScale;   ///< Artistic multiplier for Mie scattering (default 1.0).
    float mieAbsorptionScale;   ///< Artistic multiplier for Mie absorption (default 1.0).
    float mieAbsorption;        ///< Mie absorption coefficient in km^-1.
    Vec4  skyLuminanceFactor;   ///< Per-channel sky brightness art direction (default 1,1,1,1).

    // ── Ozone (Other Absorption) ────────────────────────────────────────────
    // Tent distribution for ozone density: two piecewise-linear segments.
    // DensityOzo = saturate(h < layerWidth ? linTerm0*h + constTerm0 : linTerm1*h + constTerm1)

    Vec4  ozoneAbsorption;         ///< xyz = ozone absorption extinction (km^-1), w = layerWidth (km).
    float ozoneLinTerm0;           ///< Linear coefficient for h < layerWidth.
    float ozoneConstTerm0;         ///< Constant term for h < layerWidth.
    float ozoneLinTerm1;           ///< Linear coefficient for h >= layerWidth.
    float ozoneConstTerm1;         ///< Constant term for h >= layerWidth.

    float _skyPad[28] = {};        ///< Pad to 256-byte alignment.
};

static_assert(sizeof(SkyAtmosphereCB) == 256, "SkyAtmosphereCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// AtmosphereLutCB — bound at b0 in the transmittance LUT compute pass
// ─────────────────────────────────────────────────────────────────────────────

/// Constant buffer for the transmittance LUT compute shader.
///
/// Contains all parameters needed to integrate optical depth through the
/// atmosphere for each (height, zenith angle) texel. Bound at register `b0`
/// in the LUT compute pass (uses the compute root signature, not the
/// graphics root signature).
///
/// Matches the HLSL struct in `transmittance_lut_cs.hlsl`:
/// ```hlsl
/// cbuffer AtmosphereLutCB : register(b0) {
///     float3 rayleighBeta;     // km^-1
///     float  rayleighScaleH;   // km
///     float  mieBeta;          // km^-1
///     float  mieScaleH;        // km
///     float  bottomRadiusKm;
///     float  topRadiusKm;
///     uint   lutWidth;         // 256
///     uint   lutHeight;        // 64
///     float2 _pad;
/// };
/// ```
///
/// Padded to 256 bytes as required by D3D12 constant buffer alignment.
struct AtmosphereLutCB {
    Vec3     rayleighBeta;      ///< Rayleigh scattering coefficients (km^-1).
    float    rayleighScaleH;    ///< Rayleigh scale height (km).
    float    mieBeta;           ///< Mie scattering coefficient (km^-1).
    float    mieScaleH;         ///< Mie scale height (km).
    float    bottomRadiusKm;    ///< Planet surface radius (km).
    float    topRadiusKm;       ///< Top of atmosphere radius (km).
    uint32_t lutWidth;          ///< LUT width in texels (256).
    uint32_t lutHeight;         ///< LUT height in texels (64).
    float    _pad[2] = {};      ///< Pad to 16-byte boundary.
    float    _pad2[52] = {};    ///< Pad to 256 bytes total.
};

static_assert(sizeof(AtmosphereLutCB) == 256, "AtmosphereLutCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogCB — bound at b2 (reusing per-object slot in fullscreen fog pass)
// ─────────────────────────────────────────────────────────────────────────────

/// Height fog constant buffer for the fog compositing pass.
///
/// Contains exponential height fog parameters. Bound at register `b2`
/// (reuses the per-object CBV slot). The fog pass reads scene depth from
/// the G-Buffer and blends fog color over the backbuffer.
///
/// Padded to 256 bytes as required by D3D12 constant buffer alignment.
struct HeightFogCB {
    // ── Primary fog layer (float4 + float4 + float4 = 48 bytes) ──────────
    Vec4  fogColor;             ///< xyz = fog color (linear), w = unused.
    float fogDensity;           ///< Exponential density factor.
    float fogHeightFalloff;     ///< Height falloff coefficient.
    float fogBaseHeight;        ///< World-space base height (cm).
    float fogMaxOpacity;        ///< Maximum fog opacity (0-1).
    float fogStartDistance;     ///< Minimum distance before fog begins.
    float fogCutoffDistance;    ///< Maximum fog distance. 0 = unlimited.
    float maxObserverHeight;    ///< Clamped max observer Z for shader-side compensation.
    float rawDensity;           ///< Raw (un-precomputed) density for start distance recalculation.

    // ── Second fog layer (float4 = 16 bytes) ────────────────────────────
    float secondFogDensity;         ///< PRE-COMPUTED: second density * exp2(-falloff*(camZ-baseH)). 0 = disabled.
    float secondFogHeightFalloff;   ///< Second layer height falloff coefficient.
    float secondFogRawDensity;      ///< Raw (un-precomputed) second layer density for start distance recalc.
    float secondFogBaseHeight;      ///< Second layer base height (cm).

    // ── Directional inscattering (float4 + float4 = 32 bytes) ──────────
    Vec4  directionalInscatteringColor; ///< xyz = sun-facing fog glow color, w = unused.
    float directionalInscatteringExponent; ///< Cone tightness (higher = tighter halo).
    float directionalInscatteringStartDistance; ///< Start distance for inscattering.
    float _fogPad2[2] = {};     ///< Pad to 16-byte boundary.

    // ── Sky atmosphere injection (float4 + float4 = 32 bytes) ────────
    Vec4  skyAtmContribColorScale = {1.f, 1.f, 1.f, 0.f}; ///< Sky atmosphere ambient contribution color scale (default white).
    float heightFogContribution = 1.0f;   ///< SkyAtmosphere height fog contribution (0-1, default 1.0).
    float skyLightColorR = 0.f;           ///< Sky light diffuse color R (from SH L0).
    float skyLightColorG = 0.f;           ///< Sky light diffuse color G (from SH L0).
    float skyLightColorB = 0.f;           ///< Sky light diffuse color B (from SH L0).

    // ── Sun transmittance injection (SkyAtmosphere -> HeightFog) ───────
    /// Sun color * intensity * atmospheric transmittance at ground level toward the sun.
    /// Used for directional inscattering with sky atmosphere.
    float sunIllumPostTransR = 0.f;       ///< Sun illuminance post-transmittance R.
    float sunIllumPostTransG = 0.f;       ///< Sun illuminance post-transmittance G.
    float sunIllumPostTransB = 0.f;       ///< Sun illuminance post-transmittance B.
    float sunIllumPostTransA = 0.f;       ///< 1.0 = has atmosphere sun light, 0.0 = none.

    /// Direction toward the sun for directional inscattering phase function.
    float sunDirX = 0.f;                  ///< Sun direction X (world-space, toward sun).
    float sunDirY = 0.f;                  ///< Sun direction Y.
    float sunDirZ = 1.f;                  ///< Sun direction Z. Default: straight up.
    float _sunPad = 0.f;                  ///< Pad to 16-byte boundary.

    // ── Volumetric Fog ────────────────────────────────────────────────
    uint32_t enableVolumetricFog = 0;                      ///< 1 = volumetric fog enabled (froxel grid). 0 = disabled.
    float volumetricFogScatteringDistribution = 0.2f;      ///< Phase function asymmetry [-1,1]. Positive = forward scatter.
    float volumetricFogExtinctionScale = 1.0f;             ///< Extinction coefficient multiplier.
    float volumetricFogDistance = 6400.0f;                  ///< Max distance for volumetric fog evaluation (cm).
    float volumetricFogAlbedoR = 1.0f;                     ///< Single-scattering albedo R.
    float volumetricFogAlbedoG = 1.0f;                     ///< Single-scattering albedo G.
    float volumetricFogAlbedoB = 1.0f;                     ///< Single-scattering albedo B.
    float volumetricFogStartDistance = 0.0f;                ///< Min distance before volumetric fog begins (cm).
    float volumetricFogEmissiveR = 0.0f;                   ///< Self-emissive color R of fog medium.
    float volumetricFogEmissiveG = 0.0f;                   ///< Self-emissive color G of fog medium.
    float volumetricFogEmissiveB = 0.0f;                   ///< Self-emissive color B of fog medium.
    float volumetricFogNearFadeInDistance = 300.0f;         ///< Distance over which volumetric fog fades in near camera (cm).
    float volumetricFogStaticLightingScatteringIntensity = 1.0f; ///< Multiplier for static (baked) lighting scattering.
    uint32_t overrideLightColorsWithFogInscatteringColors = 0;   ///< When 1, light colors are overridden by fog inscattering.

    // ── Inscattering Texture ─────────────────────────────────────────
    uint32_t inscatteringColorCubemapId = 0;               ///< Cubemap texture ID for inscattering color lookup. 0 = none.
    float inscatteringColorCubemapAngle = 0.0f;            ///< Rotation angle for inscattering cubemap (degrees).
    float inscatteringTextureTintR = 1.0f;                 ///< Inscattering cubemap tint R.
    float inscatteringTextureTintG = 1.0f;                 ///< Inscattering cubemap tint G.
    float inscatteringTextureTintB = 1.0f;                 ///< Inscattering cubemap tint B.
    float fullyDirectionalInscatteringColorDistance = 100000.0f; ///< Distance for fully directional inscattering (cm).
    float nonDirectionalInscatteringColorDistance = 50000.0f;    ///< Distance for non-directional inscattering (cm).

    // ── Remaining padding ──────────────────────────────────────────────
    float _fogPad3[3] = {};     ///< Pad to 256-byte alignment.
};

static_assert(sizeof(HeightFogCB) == 256, "HeightFogCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// ReflectionProbeGPU — per-probe data uploaded as StructuredBuffer
// ─────────────────────────────────────────────────────────────────────────────

/// Per-probe GPU data for reflection probe blending in the deferred lighting pass.
///
/// Uploaded as `StructuredBuffer<ReflectionProbeGPU>` accessible via the bindless
/// SRV heap (index stored in `PerFrameCB::reflectionProbeBufferSrvIndex`).
/// The deferred lighting shader iterates active probes, tests the pixel's
/// world position against each influence sphere, and blends the probe's
/// cubemap contribution weighted by distance falloff and priority order.
///
/// # Layout
/// 32 bytes (two float4s) — tightly packed for GPU structured buffer access.
/// No 256-byte alignment required (this is not a constant buffer).
///
/// @ref Lagarde 2012 "Local Image-based Lighting With Parallax-corrected Cubemaps"
struct ReflectionProbeGPU {
    float    posX;                     ///< Probe world position X (cm).
    float    posY;                     ///< Probe world position Y (cm).
    float    posZ;                     ///< Probe world position Z (cm).
    float    influenceRadius;          ///< Influence sphere radius (cm).
    float    blendDistance;            ///< Edge fade distance (cm).
    float    _pad0 = 0.0f;            ///< Padding to align SRV indices to float4 boundary.
    uint32_t irradianceSrvIndex;       ///< Bindless SRV index for irradiance cubemap.
    uint32_t prefilteredSrvIndex;      ///< Bindless SRV index for prefiltered specular cubemap.
};

static_assert(sizeof(ReflectionProbeGPU) == 32, "ReflectionProbeGPU must be 32 bytes (two float4s)");

} // namespace uz
