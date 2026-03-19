/// G-Buffer pixel shader for the deferred rendering geometry pass.
///
/// Writes surface attributes to render targets (MRT) for later
/// consumption by the deferred lighting pass. No lighting is computed here.
///
/// SM6.6 (UZ_BINDLESS): samples PBR textures via ResourceDescriptorHeap[index].
/// SM5 (!UZ_BINDLESS): samples PBR textures via register-bound textures t0-t5.
///
/// # Render Targets (optimized UE5-style packed layout)
/// - SV_Target0: AlbedoAO     (R8G8B8A8_UNORM)  — rgb = base color (linear), a = AO
/// - SV_Target1: NormalMatPbr  (R8G8B8A8_UNORM)  — rg = octahedral normal, b = metallic, a = roughness
/// - SV_Target2: PositionWS    (R32G32B32A32_FLOAT) — xyz = world pos, w = 1
/// - SV_Target3: Emissive      (R16G16B16A16_FLOAT) — rgb = HDR emissive color, a = unused
///
/// # Texture Maps (all optional — index 0 = use default value)
/// - albedoIndex:    BaseColor map (sRGB) -> linear albedo
/// - normalIndex:    Tangent-space normal map -> perturbed world normal via TBN
/// - roughnessIndex: Roughness map (R channel) -> perceptual roughness
/// - metallicIndex:  Metallic map (R channel) -> metallic factor
/// - aoIndex:        Ambient occlusion map (R channel) -> AO multiplier
/// - emissiveIndex:  Emissive map (RGB) -> HDR emissive contribution

#include "../include/octahedral.hlsli"

#ifdef UZ_POM
#include "../include/pom.hlsli"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Input / Output Structures
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolated vertex attributes from the vertex shader (mesh_vs.hlsl).
struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

/// G-Buffer MRT output — 4-RT layout.
struct GBufferOutput {
    float4 albedoAO     : SV_Target0; ///< rgb = base color (linear), a = ambient occlusion
    float4 normalMatPbr : SV_Target1; ///< rg = octahedral-encoded normal, b = metallic, a = roughness
    float4 positionWS   : SV_Target2; ///< xyz = world-space position (debug / reconstruction verification), w = 1
    float4 emissiveHDR  : SV_Target3; ///< rgb = HDR emissive color (can be > 1.0 for bloom), a = unused
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers & Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 (used by VS for vertex fetch; PS reads materialIndex from b2).
struct PushConstants {
    uint vertexBufferIndex;
};
ConstantBuffer<PushConstants> push : register(b0);
#endif

/// Per-frame CB at b1 — selective fields from C++ PerFrameCB using packoffset.
/// Only the fields needed by the GBuffer PS are declared; the rest are
/// accessed by lighting/post-process shaders that declare the full layout.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj  : packoffset(c0);   // offset 0
    float4   cameraPos           : packoffset(c4);    // offset 64
    float4   timeParams          : packoffset(c5);    // offset 80
    float4   lightDir            : packoffset(c6);    // offset 96
    float4   lightColor          : packoffset(c7);    // offset 112
    float4   ambientColor        : packoffset(c8);    // offset 128
    /// @source UE5 ViewUniformShaderParameters.h — MaterialTextureMipBias
    /// Global mip LOD bias for screen percentage compensation.
    /// 0 at 100%, negative when rendering below native resolution.
    float    mipBias             : packoffset(c30);   // offset 480
};

/// Per-object CB at b2 — must match C++ PerObjectCB struct exactly.
///
/// Scalar indices are placed FIRST (offset 0) to avoid SPIRV std140 offset
/// corruption when float4x4 members precede uint members.
cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;
    uint     normalIndex;
    uint     roughnessIndex;
    uint     metallicIndex;
    uint     aoIndex;
    uint     emissiveIndex;
    float2   _pad;           // align to 16 bytes before matrices
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    // ── Emissive parameters (offset 160) ──
    /// @source UE5 BasePassPixelShader.usf:1480 — GetMaterialEmissive(PixelMaterialInputs)
    float3   emissiveColor;      ///< Emissive tint color (linear RGB). (0,0,0) = no emission.
    float    emissiveIntensity;  ///< Emissive brightness multiplier. 0 = no emission. >1 drives bloom.
    // ── Material parameters (offset 176) ──
    /// @source UE5 MaterialTemplate.ush — BaseColor output * tint, TexCoord * scale + offset
    float3   baseColorTint;      ///< Albedo color multiplier (1,1,1 = unmodified).
    float    roughnessMultiplier; ///< Roughness texture multiplier (1 = unmodified).
    float    metallicMultiplier; ///< Metallic texture multiplier (1 = unmodified).
    float    normalIntensity;    ///< Normal map strength (0=flat, 1=full).
    float2   uvScale;            ///< Texture tiling (1,1 = no tiling).
    float2   uvOffset;           ///< Texture offset (0,0 = no offset).
    float    aoIntensity;        ///< AO map strength (1 = full AO effect).
    float    matOpacity;         ///< Material opacity (1 = opaque).
#ifdef UZ_POM
    // ── POM parameters (offset 200) ──
    /// @source UE5 MaterialExpressionBumpOffset.h:28 — HeightRatio = 0.05f
    /// @source UE5 ParallaxOcclusionMapping.uasset — MinSteps, MaxSteps, HeightScale
    uint     heightmapIndex;     ///< Heightmap texture descriptor index (0 = no POM).
    float    pomScale;           ///< Displacement scale in UV space. Default 0.05. Range [0.01, 0.1].
    int      pomMinLayers;       ///< Minimum ray march steps (head-on view). Default 8.
    int      pomMaxLayers;       ///< Maximum ray march steps (grazing view). Default 32.
    float    pomFadeStart;       ///< Distance (cm) where POM begins fading. Default 1000.
    float    pomFadeEnd;         ///< Distance (cm) where POM is fully disabled. Default 2000.
    float    pomSelfShadow;      ///< Self-shadow strength (0 = off, 1 = full). Default 0.
    float    _pomPad;            ///< Padding to 16-byte alignment.
#endif
};

#ifdef UZ_MATERIAL_GRAPH
/// Material graph parameter slots (16 x float4 = 256 bytes).
/// Populated by MaterialInstance runtime overrides — each float4 slot
/// maps to an exposed parameter in the visual shader graph.
/// @source UE5 MaterialTemplate.ush — Material.PreshaderBuffer
/// @constants 16 slots (UE5 uses PreshaderBuffer with variable count)
/// @deviations Fixed 16-slot count vs UE5 variable-length PreshaderBuffer
cbuffer MaterialParamsCB : register(b3) {
    float4 materialParams[16];
};

/// Generated material graph body — provides ComputeMaterialOutputs().
/// This file is emitted per-material by HlslMaterialTranslator and placed
/// alongside the compiled shader variant.
#include "material_body.hlsli"
#endif

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float4> emissiveTexture  : register(t5);
#ifdef UZ_POM
Texture2D<float>  heightmapTexture : register(t6);
#endif
#endif
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Writes surface attributes to the G-Buffer MRT.
///
/// Samples PBR texture maps (or uses defaults when index == 0), applies
/// tangent-space normal mapping when a normal map is bound, and outputs
/// the raw material data with no lighting computation.
[earlydepthstencil]
GBufferOutput main(PSInput input) {

#ifdef UZ_MATERIAL_GRAPH
    // ── Material Graph Mode ──────────────────────────────────────────
    // Generated HLSL provides ComputeMaterialOutputs() that evaluates
    // the full material graph and writes all PBR channels.
    //
    // materialParams[] in MaterialParamsCB (b3) provides runtime parameter
    // overrides set by MaterialInstance. Texture slots are accessed via
    // ResourceDescriptorHeap[texIndex_N] inside the generated body.
    //
    // @source UE5 MaterialTemplate.ush — CalcMaterialParameters / GetMaterialXxx()
    // @constants defaults: baseColor=(1,1,1), metallic=0, roughness=0.5, ao=1, emissive=(0,0,0), opacity=1
    // @deviations NONE

    float2 uv       = input.texcoord;
    float3 worldPos  = input.positionWS;
    float3 N         = normalize(input.normalWS);

    // Material graph outputs — initialized to physically plausible defaults.
    // ComputeMaterialOutputs() overwrites only the channels the graph connects.
    float3 out_baseColor = float3(1.0f, 1.0f, 1.0f);
    float  out_metallic  = 0.0f;
    float  out_roughness = 0.5f;
    float3 out_normal    = N;
    float3 out_emissive  = float3(0.0f, 0.0f, 0.0f);
    float  out_ao        = 1.0f;
    float  out_opacity   = 1.0f;

    // Invoke the generated material graph evaluation function.
    // The function signature is fixed by HlslMaterialTranslator:
    //   void ComputeMaterialOutputs(
    //       float2 uv, float3 worldPos, float3 geometryNormal,
    //       inout float3 baseColor, inout float metallic, inout float roughness,
    //       inout float3 normal, inout float3 emissive, inout float ao,
    //       inout float opacity);
    ComputeMaterialOutputs(uv, worldPos, N,
                           out_baseColor, out_metallic, out_roughness,
                           out_normal, out_emissive, out_ao, out_opacity);

    float3 albedo    = out_baseColor;
    float  metallic  = out_metallic;
    float  roughness = out_roughness;
    float  ao        = out_ao;
    float3 emissive  = out_emissive;
    N = out_normal;

#else
    // ── Fixed Texture Mode (default) ─────────────────────────────────
    // Traditional PBR texture sampling with per-material multipliers.
    // Used when no material graph is assigned to the mesh.

    // ── Material defaults (when no texture is bound) ──
    float3 albedo    = float3(1.0f, 1.0f, 1.0f);
    float  metallic  = 0.0f;
    float  roughness = 0.5f;
    float  ao        = 1.0f;

    // ── UV transform (apply material tiling and offset) ──
    // @source UE5 MaterialTemplate.ush — TexCoord * TextureScale + TextureOffset
    float2 uv = input.texcoord * uvScale + uvOffset;

#ifdef UZ_POM
    // ── Parallax Occlusion Mapping ──────────────────────────────────
    // Offsets UVs based on heightmap ray marching before all material
    // texture sampling, so that albedo/normal/roughness/etc. read from
    // the correct displaced location.
    //
    // @source UE5 ParallaxOcclusionMapping.uasset — UV offset applied
    //         before all material texture reads in CalcPixelMaterialInputs.
    // @deviations NONE

    // Save the original flat UV for distance fading.
    float2 originalUV = uv;

    // POM self-shadow factor (1 = fully lit, applied during emissive/shadow).
    float pomShadowFactor = 1.0f;

    // Build TBN matrix for tangent-space view/light direction computation.
    float3 pomN = normalize(input.normalWS);
    float3 pomT = normalize(input.tangentWS.xyz);
    float3 pomB = cross(pomN, pomT) * input.tangentWS.w;
    float3x3 TBN_pom = float3x3(pomT, pomB, pomN);

    // Compute tangent-space view direction.
    // CameraVector = normalize(cameraPos - surfacePos), pointing from surface to camera.
    // @source UE5 MaterialExpressions.cpp:5830 — TransformVector(World, Tangent, CameraVector)
    float3 viewDirWS = normalize(cameraPos.xyz - input.positionWS);
    float3 viewDirTS = normalize(mul(TBN_pom, viewDirWS));

    // Distance from camera to surface for LOD fading.
    float camDist = length(cameraPos.xyz - input.positionWS);

    // Only apply POM if we have a heightmap bound.
#ifdef UZ_BINDLESS
    if (heightmapIndex > 0)
    {
        Texture2D<float> pomHeightmap = ResourceDescriptorHeap[heightmapIndex];

        // Ray march to find displaced UV.
        float2 pomUV = ParallaxOcclusionMapping(
            pomHeightmap, linearSampler, uv, viewDirTS,
            pomScale, pomMinLayers, pomMaxLayers);

        // Blend with flat UV based on camera distance.
        uv = POMDistanceFade(pomUV, originalUV, camDist, pomFadeStart, pomFadeEnd);

        // Self-shadow computation (if enabled).
        if (pomSelfShadow > 0.0f)
        {
            // Transform light direction to tangent space.
            float3 lightDirTS = normalize(mul(TBN_pom, normalize(-lightDir.xyz)));

            // Compute current depth at the displaced UV.
            float currentDepth = POMComputeCurrentDepth(pomHeightmap, linearSampler, uv);

            // Trace shadow ray through heightmap.
            float rawShadow = POMSelfShadow(
                pomHeightmap, linearSampler, uv, lightDirTS,
                currentDepth, pomScale);

            // Blend shadow with distance fade: no self-shadow at fadeEnd.
            float fadeFactor = 1.0f - saturate((camDist - pomFadeStart) / max(pomFadeEnd - pomFadeStart, 0.001f));
            pomShadowFactor = lerp(1.0f, rawShadow, pomSelfShadow * fadeFactor);
        }
    }
#else
    // SM5 path: heightmapTexture is always bound (real map or fallback black).
    // When the heightmap is all-black (height=0), the ray march returns the
    // original UV because the ray immediately goes below height 0 at the first step.
    // However, to avoid wasted work, we check pomScale > 0.
    if (pomScale > 0.0f)
    {
        // Ray march to find displaced UV.
        float2 pomUV = ParallaxOcclusionMapping(
            heightmapTexture, linearSampler, uv, viewDirTS,
            pomScale, pomMinLayers, pomMaxLayers);

        // Blend with flat UV based on camera distance.
        uv = POMDistanceFade(pomUV, originalUV, camDist, pomFadeStart, pomFadeEnd);

        // Self-shadow computation (if enabled).
        if (pomSelfShadow > 0.0f)
        {
            // Transform light direction to tangent space.
            float3 lightDirTS = normalize(mul(TBN_pom, normalize(-lightDir.xyz)));

            // Compute current depth at the displaced UV.
            float currentDepth = POMComputeCurrentDepth(heightmapTexture, linearSampler, uv);

            // Trace shadow ray through heightmap.
            float rawShadow = POMSelfShadow(
                heightmapTexture, linearSampler, uv, lightDirTS,
                currentDepth, pomScale);

            // Blend shadow with distance fade: no self-shadow at fadeEnd.
            float fadeFactor = 1.0f - saturate((camDist - pomFadeStart) / max(pomFadeEnd - pomFadeStart, 0.001f));
            pomShadowFactor = lerp(1.0f, rawShadow, pomSelfShadow * fadeFactor);
        }
    }
#endif // UZ_BINDLESS
#endif // UZ_POM

    // ── Sample PBR texture maps (index 0 = use default value) ──
    // SampleBias applies the global mipBias to compensate for screen percentage.
    // At 100% screen percentage, mipBias = 0 and SampleBias is identical to Sample.
    // @source UE5 ViewUniformShaderParameters.h — MaterialTextureMipBias
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo = tex.SampleBias(linearSampler, uv, mipBias).rgb;
    }
    if (roughnessIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[roughnessIndex];
        roughness = tex.SampleBias(linearSampler, uv, mipBias).r;
    }
    if (metallicIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[metallicIndex];
        metallic = tex.SampleBias(linearSampler, uv, mipBias).r;
    }
    if (aoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[aoIndex];
        ao = tex.SampleBias(linearSampler, uv, mipBias).r;
    }
#else
    // SM5 (Vulkan non-bindless): textures are always bound via SetFragmentTexture
    // with fallback white/flat-normal when no material texture is assigned.
    // Sample unconditionally — index-based guards fail in Vulkan due to cbuffer
    // std140 layout differences for row_major float4x4 structs.
    albedo    = albedoTexture.SampleBias(linearSampler, uv, mipBias).rgb;
    roughness = roughnessTexture.SampleBias(linearSampler, uv, mipBias).r;
    metallic  = metallicTexture.SampleBias(linearSampler, uv, mipBias).r;
    ao        = aoTexture.SampleBias(linearSampler, uv, mipBias).r;
#endif

    // ── Surface normal (with optional tangent-space normal mapping) ──
    float3 N = normalize(input.normalWS);

#ifdef UZ_BINDLESS
    // SM6.6 bindless: guard on normalIndex — 0 means no normal map bound.
    if (normalIndex > 0) {
        float3 T = normalize(input.tangentWS.xyz);
        float3 B = cross(N, T) * input.tangentWS.w;
        float3x3 TBN = float3x3(T, B, N);

        Texture2D<float4> nrmTex = ResourceDescriptorHeap[normalIndex];
        float4 normalSample = nrmTex.SampleBias(linearSampler, uv, mipBias);

        float3 normalTS;
        normalTS.xy = normalSample.rg * 2.0f - 1.0f;
        // Apply normal intensity: 0 = flat (no normal map effect), 1 = full effect.
        // @source UE5 Material.FlattenNormalMap — scales normalTS.xy by intensity
        normalTS.xy *= normalIntensity;
        normalTS.z = sqrt(max(1.0f - dot(normalTS.xy, normalTS.xy), 0.0f));
        N = normalize(mul(normalTS, TBN));
    }
#else
    // SM5 path: normalTexture is always bound (real map or fallback flat-normal 128,128,255).
    // Sampling the fallback yields normalTS≈(0,0,1) which leaves N unchanged.
    // Do NOT guard on normalIndex — it may be unreliable in SPIRV std140 layout.
    {
        float3 T = normalize(input.tangentWS.xyz);
        float3 B = cross(N, T) * input.tangentWS.w;
        float3x3 TBN = float3x3(T, B, N);

        float4 normalSample = normalTexture.SampleBias(linearSampler, uv, mipBias);

        float3 normalTS;
        normalTS.xy = normalSample.rg * 2.0f - 1.0f;
        // Apply normal intensity: 0 = flat (no normal map effect), 1 = full effect.
        // @source UE5 Material.FlattenNormalMap — scales normalTS.xy by intensity
        normalTS.xy *= normalIntensity;
        normalTS.z = sqrt(max(1.0f - dot(normalTS.xy, normalTS.xy), 0.0f));
        N = normalize(mul(normalTS, TBN));
    }
#endif

    // ── Apply material parameter multipliers ──
    // All multipliers default to 1.0, so existing rendering is unchanged
    // when no Material/MaterialInstance assigns overrides.

    // @source UE5 MaterialTemplate.ush — BaseColor * BaseColorTint
    albedo *= baseColorTint;

    // @source UE5 Material defaults — Roughness/Metallic outputs multiplied in graph
    roughness *= roughnessMultiplier;
    metallic  *= metallicMultiplier;

    // @source UE5 Material defaults — AO blended with strength
    // ao_final = lerp(1.0, ao_sampled, aoIntensity)
    ao = lerp(1.0f, ao, aoIntensity);

#ifdef UZ_POM
    // ── Apply POM self-shadow to AO ──
    // In a deferred pipeline, POM self-shadow is stored in the AO channel
    // so it modulates both direct and indirect lighting during the lighting pass.
    // pomShadowFactor is 1.0 when no heightmap is bound or self-shadow is off.
    ao *= pomShadowFactor;
#endif

    // ── Emissive: texture * color * intensity ──
    // @source UE5 BasePassPixelShader.usf:1480 — Emissive = GetMaterialEmissive(PixelMaterialInputs)
    // @source UE5 BasePassPixelShader.usf:1541 — Color += Emissive
    // UE5 computes emissive in the material graph and adds it directly to SceneColor.
    // In our deferred pipeline, we write it to a separate GBuffer RT (RGBA16Float)
    // and add it during the deferred lighting pass.
    // @constants emissiveColor default=(0,0,0), emissiveIntensity default=0 (no emission)
    // @deviations Stored in GBuffer RT3 instead of direct SceneColor write (deferred pipeline)
    float3 emissive = float3(0.0f, 0.0f, 0.0f);

#ifdef UZ_BINDLESS
    if (emissiveIndex > 0) {
        // Sample emissive texture map (RGB, linear)
        Texture2D<float4> emissiveTex = ResourceDescriptorHeap[emissiveIndex];
        emissive = emissiveTex.SampleBias(linearSampler, uv, mipBias).rgb;
    }
#else
    // SM5 path: emissiveTexture is always bound (real map or fallback black).
    // Fallback black (0,0,0) produces zero emissive contribution — correct default.
    emissive = emissiveTexture.SampleBias(linearSampler, uv, mipBias).rgb;
#endif

    // Apply emissive color tint and intensity multiplier.
    // When emissiveIntensity is 0 or emissiveColor is (0,0,0), result is zero.
    // HDR values > 1.0 are expected and will drive the bloom pass.
    emissive *= emissiveColor * emissiveIntensity;

#endif // UZ_MATERIAL_GRAPH

    // ── Write G-Buffer (4-RT layout) ──
    GBufferOutput output;
    output.albedoAO     = float4(albedo, ao);
    output.normalMatPbr = float4(OctEncode(N), metallic, roughness);
    output.positionWS   = float4(input.positionWS, 1.0f);
    output.emissiveHDR  = float4(emissive, 0.0f);

    return output;
}
