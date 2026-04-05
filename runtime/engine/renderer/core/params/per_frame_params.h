/// UzEngine Per-Frame Shader Parameters — metadata-annotated version of PerFrameCB.
///
/// `PerFrameParams` mirrors the scalar layout of `PerFrameCB` byte-for-byte
/// (first 512 bytes are identical), then appends RHI resource handles for IBL
/// textures. The `ShaderParameterBinder` uses the metadata to:
/// 1. Copy the 512-byte scalar section to the CB ring.
/// 2. Bind IBL textures at register slots t5/t6/t7 (SM5 fallback path).
///
/// # Migration Strategy
/// `PerFrameParams` coexists with `PerFrameCB`. Passes that adopt the new
/// binder use `PerFrameParams`; legacy passes continue using `PerFrameCB`.
/// Once all passes are migrated, `PerFrameCB` can be removed.
///
/// # HLSL Register Layout for IBL (SM5)
/// - t0-t4: G-Buffer textures (albedo, normal, depth, shadow, position)
/// - t5: Irradiance cubemap (IBL diffuse)
/// - t6: Prefiltered specular cubemap (IBL specular)
/// - t7: BRDF integration LUT (split-sum)
///
/// # See Also
/// - `constant_buffers.h` — Original `PerFrameCB` definition.
/// - `shader_parameters.h` — Metadata types used by `GetMetadata()`.
/// - `shader_parameter_binder.h` — Binder that consumes this metadata.

#pragma once

#include "shader_parameters.h"
#include "mat4.h"
#include "vector4.h"

#include <cstddef>
#include <cstdint>
#include <climits>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameParams — extends PerFrameCB with IBL resource handles
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame shader parameters with metadata for automatic CB upload and
/// IBL texture binding.
///
/// The scalar section (bytes 0-511) is byte-identical to `PerFrameCB`.
/// Resource handles are appended after the scalar section and are NOT
/// copied to the GPU constant buffer — they are bound via SRV descriptors.
///
/// # Layout
/// ```
/// Offset   Size   Field
/// 0        64     viewProj (Mat4)
/// 64       16     cameraPos (Vec4)
/// 80       16     timeParams (Vec4)
/// 96       16     lightDir (Vec4)
/// 112      16     lightColor (Vec4)
/// 128      16     ambientColor (Vec4)
/// 144      64     invViewProj (Mat4)
/// 208      4      nearPlane
/// 212      4      farPlane
/// 216      4      irradianceSrvIndex
/// 220      4      prefilteredSrvIndex
/// 224      4      brdfLutSrvIndex
/// 228      4      prefilteredMipLevels
/// 232      4      skyLightIntensity
/// 236      4      skyLightColorR
/// 240      4      skyLightColorG
/// 244      4      skyLightColorB
/// 248      4      skyLightMinAmbient
/// 252      4      exposure
/// 256      112    skyIrradianceSH[7] (7 * Vec4)
/// 368      4      shadowDebugMode
/// 372      4      aerialPerspectiveEnabled
/// 376      4      transmittanceLutSrvIndex
/// 380      56     apBottomRadiusKm..apMieScale (14 floats)
/// 436      20     toneCurveSlope..toneCurveWhiteClip (5 floats)
/// 456      4      gtaoSrvIndex
/// 460      4      brdfDebugMode
/// 464      4      heightFogContribution
/// 468      4      filmGrainIntensity
/// 472      4      vignetteIntensity
/// 476      4      chromaAbIntensity
/// 480      4      contactShadowLength
/// 484      36     _pad2[9]
/// ─── 512 bytes (CB boundary) ───
/// 512      8      irradianceCubemap (RHITextureHandle, NOT in CB)
/// 520      8      prefilteredSpecular (RHITextureHandle, NOT in CB)
/// 528      8      brdfLut (RHITextureHandle, NOT in CB)
/// ```
struct PerFrameParams {
    // ── Scalar section (512 bytes, copied to CB) ─────────────────────────
    // Layout MUST match PerFrameCB exactly — same offsets, same padding.

    Mat4     viewProj;                                 ///< Combined view-projection matrix.
    Vec4     cameraPos;                                ///< xyz = world-space camera position, w = unused.
    Vec4     timeParams;                               ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    Vec4     lightDir;                                 ///< xyz = direction toward primary light, w = 0.
    Vec4     lightColor;                               ///< xyz = light color * intensity, w = unused.
    Vec4     ambientColor;                             ///< xyz = ambient color, w = unused.
    Mat4     invViewProj;                              ///< Inverse view-projection for position reconstruction.
    float    nearPlane            = 10.0f;             ///< Near clip distance (cm).
    float    farPlane             = 100000.0f;         ///< Far clip distance (cm).
    uint32_t irradianceSrvIndex   = UINT32_MAX;        ///< Bindless SRV index for irradiance cubemap.
    uint32_t prefilteredSrvIndex  = UINT32_MAX;        ///< Bindless SRV index for prefiltered specular.
    uint32_t brdfLutSrvIndex      = UINT32_MAX;        ///< Bindless SRV index for BRDF LUT.
    uint32_t prefilteredMipLevels = 8;                 ///< Number of prefiltered mip levels (must match IBLBaker::kPrefilteredMipLevels).
    float    skyLightIntensity    = 1.0f;              ///< IBL ambient multiplier.
    float    skyLightColorR       = 1.0f;              ///< Sky light tint (R).
    float    skyLightColorG       = 1.0f;              ///< Sky light tint (G).
    float    skyLightColorB       = 1.0f;              ///< Sky light tint (B).
    float    skyLightMinAmbient   = 0.0f;              ///< Minimum ambient floor (0-1).
    float    exposure             = 1.0f;              ///< Auto-exposure multiplier.
    Vec4     skyIrradianceSH[7]   = {};                ///< L2 SH coefficients for sky diffuse irradiance.
    uint32_t shadowDebugMode      = 0;                 ///< Shadow debug mode (0=off).

    // ── Aerial Perspective parameters (uses former padding) ───────────────
    uint32_t aerialPerspectiveEnabled = 0;             ///< 1 = apply aerial perspective in deferred lighting.
    uint32_t transmittanceLutSrvIndex = UINT32_MAX;    ///< SRV index for transmittance LUT.
    float    apBottomRadiusKm = 6360.0f;               ///< Planet surface radius (km).
    float    apTopRadiusKm    = 6420.0f;               ///< Top of atmosphere radius (km).
    float    apRayleighBetaX  = 5.802e-3f;             ///< Rayleigh scattering X (km^-1).
    float    apRayleighBetaY  = 13.558e-3f;            ///< Rayleigh scattering Y (km^-1).
    float    apRayleighBetaZ  = 33.1e-3f;              ///< Rayleigh scattering Z (km^-1).
    float    apRayleighScaleH = 8.0f;                  ///< Rayleigh scale height (km).
    float    apMieBeta        = 3.996e-3f;             ///< Mie scattering coefficient (km^-1).
    float    apMieScaleH      = 1.2f;                  ///< Mie scale height (km).
    float    apMieAnisotropy  = 0.8f;                  ///< Mie HG anisotropy parameter.
    float    apSunIntensity   = 1.0f;                  ///< Sun illuminance for AP inscattering.
    float    apRayleighScale  = 1.0f;                  ///< Artistic Rayleigh scattering multiplier.
    float    apMieScale       = 1.0f;                  ///< Artistic Mie scattering multiplier.

    // ── ACES tone curve parameters ──────────────────────────────────────
    float    toneCurveSlope      = 0.88f;              ///< Steepness of the linear (straight) segment.
    float    toneCurveToe        = 0.55f;              ///< Size of the toe region (dark rolloff).
    float    toneCurveShoulder   = 0.26f;              ///< Size of the shoulder region (highlight rolloff).
    float    toneCurveBlackClip  = 0.0f;               ///< Lifts black level (adds to min output).
    float    toneCurveWhiteClip  = 0.04f;              ///< Lowers white ceiling (subtracts from max output).

    uint32_t gtaoSrvIndex        = UINT32_MAX;         ///< SRV index for GTAO filtered AO texture (UINT32_MAX = no GTAO).
    uint32_t brdfDebugMode       = 0;                 ///< BRDF debug (0=off, 1=split-screen: left=old, right=new).
    float    heightFogContribution = 0.0f;            ///< AP reduction factor when height fog active (0=none, 1=full).

    // ── Post-process film effects ───────────────────────────────────────
    float    filmGrainIntensity = 0.0f;              ///< Film grain intensity (0=off).
    float    vignetteIntensity  = 0.4f;              ///< Vignette darkening intensity.
    float    chromaAbIntensity  = 0.0f;              ///< Chromatic aberration / scene fringe intensity (0=off).

    /// Screen-space contact shadow ray length. 0.0 = disabled.
    /// Positive = screen-space fraction (0.02 = 2% of depth-scaled screen ray length).
    float    contactShadowLength = 0.0f;             ///< Contact shadow ray length (0=off, 0.02=typical).

    uint32_t ssrSrvIndex = UINT32_MAX;               ///< SRV index for resolved SSR texture (UINT32_MAX = no SSR).

    /// Global mipmap LOD bias for screen percentage compensation.
    /// 0 at 100%, negative below native resolution, positive for supersampling.
    float    mipBias = 0.0f;

    // ── SSGI (Screen-Space Global Illumination) ────────────────────────
    uint32_t ssgiSrvIndex   = UINT32_MAX;             ///< SRV index for upsampled SSGI indirect diffuse (UINT32_MAX = disabled).
    uint32_t ssgiAOSrvIndex = UINT32_MAX;             ///< SRV index for SSGI ambient occlusion mask (UINT32_MAX = disabled).
    uint32_t ssgiEnabled    = 0;                      ///< 1 = SSGI active, 0 = disabled.

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
    uint32_t apVolumeSrvIndex = UINT32_MAX;          ///< SRV for precomputed 32x32x16 AP 3D volume.

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
    uint32_t envSamplerAliasSrv = UINT32_MAX;       ///< SRV for alias table StructuredBuffer (UINT32_MAX = disabled).
    uint32_t envSamplerLumMip0Srv = UINT32_MAX;     ///< SRV for luminance pyramid mip 0 (UINT32_MAX = disabled).
    float    envSamplerTotalLuminance = 0.0f;       ///< Total luminance sum for PDF normalization.
    uint32_t envSamplerTexelCount = 0;              ///< Texel count of luminance mip 0.
    uint32_t envSamplerLumWidth = 0;                ///< Width of luminance mip 0.
    uint32_t envSamplerLumHeight = 0;               ///< Height of luminance mip 0.
    uint32_t envMisAccumulatedSrv = UINT32_MAX;     ///< SRV for temporally-accumulated env MIS specular.

    // ── Shader CVar parameters (Phase 1 — no hardcoded values) ─────────
    float    shadowPCFPenumbra       = 2.0f;           ///< CVar r.Shadow.PCFPenumbraRadius
    float    shadowPCSSMinPenumbra   = 0.5f;           ///< CVar r.Shadow.PCSSMinPenumbra
    float    shadowPCSSMaxPenumbra   = 16.0f;          ///< CVar r.Shadow.PCSSMaxPenumbra
    float    shadowESMExponent       = 80.0f;          ///< CVar r.Shadow.ESMExponentConstant
    uint32_t shadowContactSteps      = 16;             ///< CVar r.Shadow.ContactShadowSteps
    float    shadowContactThickness  = 0.005f;         ///< CVar r.Shadow.ContactShadowThickness
    float    bloomKnee               = 0.5f;           ///< CVar r.Bloom.Knee
    float    exposureKeyValue        = 0.18f;          ///< CVar r.Exposure.KeyValue
    float    exposureSpeedUp         = 3.0f;           ///< CVar r.Exposure.SpeedUp
    float    exposureSpeedDown       = 1.0f;           ///< CVar r.Exposure.SpeedDown
    float    exposureLowPercent      = 10.0f;          ///< CVar r.Exposure.LowPercent
    float    exposureHighPercent     = 90.0f;          ///< CVar r.Exposure.HighPercent
    float    materialDielectricF0    = 0.04f;          ///< CVar r.Material.DielectricF0
    float    materialMinGGXAlpha     = 0.0064f;        ///< CVar r.Material.MinGGXAlpha

    float    _pad2[1]               = {};              ///< Pad scalar section to 768 bytes.

    // ── Resource handles (NOT copied to CB) ──────────────────────────────
    // These are bound by ShaderParameterBinder at their HLSL register slots.

    RHITextureHandle irradianceCubemap;                ///< IBL irradiance cubemap (bound at t5 on SM5).
    RHITextureHandle prefilteredSpecular;              ///< IBL prefiltered specular cubemap (bound at t6 on SM5).
    RHITextureHandle brdfLut;                          ///< BRDF integration LUT (bound at t7 on SM5).

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    ///
    /// The metadata tells `ShaderParameterBinder` which bytes to copy to
    /// the CB (the first 512 bytes) and which resource handles to bind.
    ///
    /// # Returns
    /// Reference to a static `ShaderParameterMetadata` instance.
    /// Valid for the lifetime of the program.
    static const ShaderParameterMetadata& GetMetadata();
};

// Verify scalar section matches PerFrameCB exactly
static_assert(offsetof(PerFrameParams, irradianceCubemap) == 768,
              "PerFrameParams resource section must start at byte 768 (after PerFrameCB-compatible scalar section)");

} // namespace uz
