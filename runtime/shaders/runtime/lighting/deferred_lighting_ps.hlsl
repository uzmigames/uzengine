/// Deferred lighting pixel shader — Cook-Torrance PBR with directional + point lights
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
/// # G-Buffer Inputs (2-RT packed layout)
/// - AlbedoAO     (R8G8B8A8_UNORM) — rgb = base color (linear), a = AO
/// - NormalMatPbr  (R8G8B8A8_UNORM) — rg = octahedral normal, b = metallic, a = roughness
/// - Depth         (R32_FLOAT SRV)  — hardware depth sampled as float
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
/// 4 indices for the 3-RT G-Buffer layout:
/// albedo+AO in RT0, octahedral normal+metallic+roughness in RT1, depth, world-space position in RT2.
struct GBufferIndices {
    uint albedoIdx;     ///< Descriptor index for albedo+AO G-Buffer (R8G8B8A8_UNORM).
    uint normalIdx;     ///< Descriptor index for normal+metallic+roughness G-Buffer (R8G8B8A8_UNORM).
    uint depthIdx;      ///< Descriptor index for depth buffer SRV (R32_FLOAT).
    uint positionIdx;   ///< Descriptor index for world-space position G-Buffer (R32G32B32A32_FLOAT).
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
    // Matches UE5 SkyIrradianceEnvironmentMap[0..6].
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
    float    _framePad2[15];     ///< Padding to 512 bytes.
};

/// Single point light for the deferred lighting pass (48 bytes = three float4s).
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
};

/// Maximum number of point lights evaluated per pixel.
static const uint MAX_POINT_LIGHTS = 64;

/// Lighting CB at b2 — point light array and count.
///
/// Moved from b3 to b2 because fullscreen deferred passes do not use
/// per-object data, and the SM6.6 root signature only has 3 root params
/// (b0 root constants, b1 per-frame CBV, b2 per-object/lighting CBV).
cbuffer LightingCB : register(b2) {
    PointLight pointLights[MAX_POINT_LIGHTS];
    uint       pointLightCount;
    uint3      _lightPad;
};

/// Cascaded shadow map parameters — must match C++ ShadowDataCB struct exactly.
/// Supports up to 8 cascades (4 main + 4 far shadow cascades).
/// UE5 parity: per-cascade bias, fade starts, receiver bias, distance fade.
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
    float    receiverBias;                   ///< Shadow receiver bias (UE5 default: 0.9).
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
    if (gtaoSrvIndex != 0xFFFFFFFF) {
#ifdef UZ_BINDLESS
        Texture2D<float> gtaoTex = ResourceDescriptorHeap[gtaoSrvIndex];
        float ssao = gtaoTex.Sample(linearSampler, uv).r;
#else
        // SM5 path: GTAO bound at t9
        float ssao = gtaoTexture.Sample(linearSampler, uv).r;
#endif
        ao *= ssao;
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

    // ── Directional light (shadowed) ──
    float3 L_dir = normalize(lightDir.xyz);
    float3 Lo = EvaluateBRDF(N, V, L_dir, lightColor.xyz,
                             albedo, metallic, alpha, F0) * shadow;

    // ── Point lights ──
    for (uint i = 0; i < pointLightCount; ++i) {
        float3 lightVec  = pointLights[i].position - worldPos;
        float  dist      = length(lightVec);
        float  radius    = pointLights[i].radius;

        // Skip lights beyond their attenuation radius
        if (dist > radius)
            continue;

        float3 L_point = lightVec / dist; // normalise

        // Attenuation: inverse square (UE5 default) or custom exponent
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
            // Custom falloff exponent (non-physical, UE5 legacy)
            attenuation = pow(saturate(1.0f - distOverRadius), pointLights[i].falloffExponent);
        }

        float3 radiance = pointLights[i].color * attenuation;

        // Evaluate BRDF
        Lo += EvaluateBRDF(N, V, L_point, radiance,
                           albedo, metallic, alpha, F0);
    }

    // ── Ambient: IBL or constant fallback ──────────────────────────────
    float  NdotV = max(dot(N, V), 0.0f);
    float3 ambient;

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

        // BRDF LUT: pre-integrated scale and bias terms for split-sum approximation
        float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        // Karis 2013 multi-scatter energy compensation
        // @source Karis 2013 "Real Shading in Unreal Engine 4" — multi-scatter approximation
        // Compensates for energy lost to single-scatter assumption in split-sum IBL.
        // Ems = fraction of energy NOT accounted for by single scatter.
        // Favg = average Fresnel reflectance over the hemisphere.
        float Ems = 1.0f - (envBRDF.x + envBRDF.y);
        float3 Favg = F0 + (1.0f - F0) / 21.0f;
        specularIBL *= 1.0f + Favg * Ems / (1.0f - Favg * Ems);

        float3 skyLightTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
        ambient = (diffuseIBL + specularIBL) * ao * skyLightIntensity * skyLightTint;
    } else {
        // Fallback: constant ambient (no IBL data available)
        float3 skyLightTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
        ambient = ambientColor.xyz * albedo * ao * skyLightIntensity * skyLightTint;
    }

    // ── Minimum ambient floor (SkyLight minOcclusion equivalent) ──
    // Ensures shadowed surfaces never go fully black — adds a minimum fill light.
    if (skyLightMinAmbient > 0.0f) {
        float3 minFloor = albedo * skyLightMinAmbient;
        ambient = max(ambient, minFloor);
    }

    // ── Combine ──
    float3 color = ambient + Lo;

    // ── Aerial Perspective ──
    // Per-pixel ray-march through the atmosphere from camera to pixel.
    // Adds distance-dependent inscattered luminance and applies transmittance
    // to simulate atmospheric haze on distant geometry.
    // @source UE5 SkyAtmosphereCommon.ush:42 — GetAerialPerspectiveLuminanceTransmittance
    // @source UE5 SkyAtmosphere.usf:1466 — RenderCameraAerialPerspectiveVolumeCS
    if (aerialPerspectiveEnabled != 0 && transmittanceLutSrvIndex != 0xFFFFFFFF) {
        float3 apRayleighBeta3 = float3(apRayleighBetaX, apRayleighBetaY, apRayleighBetaZ);

        // Fetch transmittance LUT
#ifdef UZ_BINDLESS
        Texture2D<float4> apTransLut = ResourceDescriptorHeap[transmittanceLutSrvIndex];
#else
        Texture2D<float4> apTransLut = transmittanceLutTex;
#endif

        // Sun direction reused from the directional light
        float3 apSunDir = normalize(lightDir.xyz);

        float4 ap = ComputeAerialPerspective(
            worldPos,
            cameraPos.xyz,
            apSunDir,
            apSunIntensity,
            apBottomRadiusKm,
            apTopRadiusKm,
            apRayleighBeta3,
            apRayleighScaleH,
            apMieBeta,
            apMieScaleH,
            apMieAnisotropy,
            apRayleighScale,
            apMieScale,
            apTransLut,
            linearSampler);

        // Apply: transmittance dims the scene, inscattering adds atmosphere glow
        // @source UE5 SkyAtmosphereCommon.ush:107-108 — AP.rgb *= Weight; AP.a = 1-(Weight*(1-AP.a))
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

    // Output linear HDR — tonemap applied in separate fullscreen pass.
    return float4(color, 1.0f);
}
