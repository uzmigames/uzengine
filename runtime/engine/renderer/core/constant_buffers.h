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
    Vec4  timeParams;           ///< x = total elapsed seconds, y = delta time, z = frame count (float), w = unused.
    Vec4  lightDir;             ///< xyz = normalised direction toward the primary light source, w = 0.
    Vec4  lightColor;           ///< xyz = primary light color multiplied by intensity, w = unused.
    Vec4  ambientColor;         ///< xyz = ambient light color contribution, w = unused.
    Mat4  invViewProj;          ///< Inverse view-projection matrix for world position reconstruction (deferred lighting).
    float nearPlane  = 10.0f;     ///< Near clip plane distance in world units / cm (for depth linearization).
    float farPlane   = 100000.0f; ///< Far clip plane distance in world units / cm (for depth linearization).
    uint32_t irradianceSrvIndex  = UINT32_MAX; ///< SRV index for irradiance cubemap (UINT32_MAX = no IBL).
    uint32_t prefilteredSrvIndex = UINT32_MAX; ///< SRV index for prefiltered specular cubemap.
    uint32_t brdfLutSrvIndex     = UINT32_MAX; ///< SRV index for BRDF integration LUT.
    uint32_t prefilteredMipLevels = 5;         ///< Number of prefiltered mip levels (for LOD calculation).
    float skyLightIntensity = 1.0f;            ///< Multiplier for IBL ambient (SkyLight intensity).
    float skyLightColorR    = 1.0f;            ///< Sky light tint color (R).
    float skyLightColorG    = 1.0f;            ///< Sky light tint color (G).
    float skyLightColorB    = 1.0f;            ///< Sky light tint color (B).
    float skyLightMinAmbient = 0.0f;           ///< Minimum ambient level (0-1). Prevents fully black shadows.
    float exposure = 1.0f;                     ///< Exposure multiplier from auto-exposure (applied before tonemap).

    // ── Sky Irradiance SH (L2, 7 float4 = 112 bytes) ─────────────────────
    // Matches UE5 SkyIrradianceEnvironmentMap[0..6].
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

    // ── Film Tonemap parameters (UE5 parity) ─────────────────────────────
    /// @source UE5 PostProcessSettings.h — default values for FilmToneMap curve
    float    filmSlope      = 0.88f;             ///< Steepness of the linear (straight) segment.
    float    filmToe        = 0.55f;             ///< Size of the toe region (dark rolloff).
    float    filmShoulder   = 0.26f;             ///< Size of the shoulder region (highlight rolloff).
    float    filmBlackClip  = 0.0f;              ///< Lifts black level (adds to min output).
    float    filmWhiteClip  = 0.04f;             ///< Lowers white ceiling (subtracts from max output).

    uint32_t gtaoSrvIndex    = UINT32_MAX;       ///< SRV index for GTAO filtered AO texture (UINT32_MAX = no GTAO).
    uint32_t brdfDebugMode   = 0;               ///< BRDF debug (0=off, 1=split-screen: left=old Lambertian+SchlickGGX, right=new Burley+SmithJoint).

    // @source UE5 SceneRendering.cpp:1440 — SkyAtmosphereHeightFogContribution
    // Controls how much aerial perspective inscattering is reduced when height fog
    // is active, to prevent double-fogging (both AP and height fog adding haze).
    // 0 = AP unaffected by fog, 1 = AP inscattering fully replaced by fog.
    float    heightFogContribution = 0.0f;       ///< AP reduction factor when height fog is active (0=no reduction, 1=full).

    float    _pad2[13] = {};                     ///< Pad to 512 bytes.
};

static_assert(sizeof(PerFrameCB) == 512, "PerFrameCB must be 512 bytes (D3D12 CB alignment)");

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
///     float4x4 model;
///     float4x4 normalMatrix;
///     uint     albedoIndex;
///     uint     normalIndex;
///     uint     roughnessIndex;
///     uint     metallicIndex;
///     uint     aoIndex;
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
    float    _pad[3] = {};     ///< Align to 16-byte boundary before matrices
    // ── Matrices after scalars (offset 32) ──
    Mat4     model;            ///< Object-to-world transform
    Mat4     normalMatrix;     ///< transpose(inverse(model)) — for lighting
    float    _pad2[24] = {};   ///< Padding to 256-byte alignment
};

static_assert(sizeof(PerObjectCB) == 256, "PerObjectCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// LightingCB — bound at b2 (root CBV slot 2, reused from PerObjectCB slot in fullscreen deferred passes)
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of point lights supported in the deferred lighting pass.
static constexpr uint32_t kMaxPointLights = 64;

/// Point light data for the deferred lighting pass.
///
/// Each point light carries position, attenuation, color, and UE5-parity
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
};

/// PointLightData flag bits.
static constexpr uint32_t kLightFlag_InverseSquared = 1u << 0;

static_assert(sizeof(PointLightData) == 48, "PointLightData must be 48 bytes (three float4s)");

/// Lighting constant buffer for the deferred lighting pass, bound at register `b2`.
///
/// Contains an array of point lights and a count of active lights. The shader
/// iterates `pointLightCount` entries and accumulates their contributions.
///
/// Padded to a 256-byte multiple as required by D3D12 constant buffer alignment.
///
/// Matches the HLSL struct:
/// ```hlsl
/// cbuffer LightingCB : register(b2) {
///     PointLightData pointLights[64];
///     uint           pointLightCount;
///     uint3          _pad;
/// };
/// ```
struct LightingCB {
    PointLightData pointLights[kMaxPointLights]; ///< Point light array (64 * 48 = 3072 bytes).
    uint32_t       pointLightCount = 0;          ///< Number of active point lights in the array.
    uint32_t       _pad[3] = {};                 ///< Align count field to 16 bytes.
    float          _pad2[60] = {};               ///< Pad to 3328 bytes (256-byte multiple).
};

static_assert(sizeof(LightingCB) % 256 == 0, "LightingCB must be a multiple of 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB — bound at b4 (root CBV, updated once per frame for shadow sampling)
// ─────────────────────────────────────────────────────────────────────────────

/// Shadow data constant buffer for cascade shadow map sampling in pixel shaders.
///
/// Contains per-cascade light-space view-projection matrices, split distances,
/// per-cascade bias, fade parameters, and shadow atlas SRV index.
/// Supports up to 8 cascades (4 main + 4 far shadow cascades).
/// Bound at register `b4`. UE5 parity — all parameters match UE5 5.6 defaults.
///
/// Padded to a 256-byte multiple as required by D3D12 constant buffer alignment.
///
/// # Layout (768 bytes = 3 * 256)
/// - cascadeViewProj[8]:    8 * 64 = 512 bytes
/// - cascadeSplits[8]:      8 * 4  = 32 bytes
/// - cascadeFadeStarts[8]:  8 * 4  = 32 bytes
/// - cascadeDepthBias[8]:   8 * 4  = 32 bytes
/// - cascadeNormalBias[8]:  8 * 4  = 32 bytes
/// - scalar params:                  56 bytes
/// - padding:                        72 bytes
/// Total: 768 bytes
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
    float    receiverBias;          ///< Shadow receiver bias (UE5 default: 0.9).
    float    shadowFadeStart;       ///< World distance where last-cascade fade begins.
    float    shadowFadeEnd;         ///< World distance where shadows fully disappear.
    uint32_t filterMethod;          ///< 0=5x5 PCF (default), 1=Poisson 16-tap, 2=PCSS.
    float    tanLightSourceAngle;   ///< tan(sun disk angle) for PCSS penumbra estimation.
    float    maxSoftKernelSize;     ///< Max PCSS kernel radius in texels (UE5 default: 40).
    uint32_t atlasColumns;          ///< Number of columns in the atlas grid (2, 3, or 4).
    uint32_t staticShadowAtlasIndex = UINT32_MAX; ///< SRV heap index for cached static shadow atlas. UINT32_MAX = no cache.
    uint32_t useShadowCache = 0;    ///< 0 = no cache (single atlas), 1 = composite static + dynamic.
    uint32_t dfsEnabled = 0;        ///< 1 = Distance Field Shadows active, 0 = disabled.
    uint32_t dfsOutputSrvIndex = UINT32_MAX; ///< SRV index for DFS result texture (R16_FLOAT screen-space). UINT32_MAX = not bound.
    float    _pad2[18] = {};        ///< Pad to 768 bytes (256-byte multiple).
};

static_assert(sizeof(ShadowDataCB) == 768, "ShadowDataCB must be 768 bytes (256-byte multiple)");

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmosphereCB — bound at b2 (reusing per-object slot in fullscreen sky pass)
// ─────────────────────────────────────────────────────────────────────────────

/// Sky atmosphere constant buffer for the sky rendering pass.
///
/// Follows UE5 SkyAtmosphereComponent conventions: all distances in KILOMETERS.
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
    float multiScatteringFactor; ///< Multi-scattering approximation (0=single, 1=full). UE5 default: 1.0.
    float rayleighScatteringScale; ///< Artistic multiplier for Rayleigh (default 1.0).
    float mieScatteringScale;   ///< Artistic multiplier for Mie scattering (default 1.0).
    float mieAbsorptionScale;   ///< Artistic multiplier for Mie absorption (default 1.0).
    float mieAbsorption;        ///< Mie absorption coefficient in km^-1.
    Vec4  skyLuminanceFactor;   ///< Per-channel sky brightness art direction (default 1,1,1,1).

    // ── Ozone (Other Absorption) ────────────────────────────────────────────
    // @source UE5 SkyAtmosphereCommonData.h:38-42, SkyAtmosphereCommon.ush:316-318
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
    float fogBaseHeight;        ///< World-space base height (cm, UE5 parity).
    float fogMaxOpacity;        ///< Maximum fog opacity (0-1).
    float fogStartDistance;     ///< Minimum distance before fog begins.
    float fogCutoffDistance;    ///< Maximum fog distance. 0 = unlimited.
    float _fogPad0[2] = {};     ///< Pad to 16-byte boundary.

    // ── Second fog layer (float4 = 16 bytes) ────────────────────────────
    // @source UE5 FogRendering.cpp:257 — ExponentialFogParameters2
    // .x = precomputed (density * pow(2, -falloff*(camZ-baseH)))
    // .y = height falloff
    // .z = raw density (for start distance recalculation)
    // .w = base height (for start distance recalculation)
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
    /// @source UE5 HeightFogCommon.ush:174-180 — Sky ambient fog injection
    /// @source UE5 FogRendering.cpp:85 — SkyAtmosphereAmbientContributionColorScale
    Vec4  skyAtmContribColorScale = {1.f, 1.f, 1.f, 0.f}; ///< UE5: SkyAtmosphereAmbientContributionColorScale (default white).
    float heightFogContribution = 1.0f;   ///< UE5: SkyAtmosphere.HeightFogContribution (0-1, default 1.0).
    float skyLightColorR = 0.f;           ///< Sky light diffuse color R (from SH L0).
    float skyLightColorG = 0.f;           ///< Sky light diffuse color G (from SH L0).
    float skyLightColorB = 0.f;           ///< Sky light diffuse color B (from SH L0).

    // ── Remaining padding ──────────────────────────────────────────────
    float _fogPad3[32] = {};    ///< Pad to 256-byte alignment.
};

static_assert(sizeof(HeightFogCB) == 256, "HeightFogCB must be 256 bytes (D3D12 CB alignment)");

} // namespace uz
