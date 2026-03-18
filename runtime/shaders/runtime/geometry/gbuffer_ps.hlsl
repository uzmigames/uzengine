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

/// Per-frame CB at b1 — must match C++ PerFrameCB struct exactly.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
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
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float4> emissiveTexture  : register(t5);
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

    // ── Material defaults (when no texture is bound) ──
    float3 albedo    = float3(1.0f, 1.0f, 1.0f);
    float  metallic  = 0.0f;
    float  roughness = 0.5f;
    float  ao        = 1.0f;

    // ── UV transform (apply material tiling and offset) ──
    // @source UE5 MaterialTemplate.ush — TexCoord * TextureScale + TextureOffset
    float2 uv = input.texcoord * uvScale + uvOffset;

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
    // SM5 (Vulkan non-bindless): textures are always bound via SetFragmentTexture
    // with fallback white/flat-normal when no material texture is assigned.
    // Sample unconditionally — index-based guards fail in Vulkan due to cbuffer
    // std140 layout differences for row_major float4x4 structs.
    albedo    = albedoTexture.Sample(linearSampler, uv).rgb;
    roughness = roughnessTexture.Sample(linearSampler, uv).r;
    metallic  = metallicTexture.Sample(linearSampler, uv).r;
    ao        = aoTexture.Sample(linearSampler, uv).r;
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
        float4 normalSample = nrmTex.Sample(linearSampler, uv);

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

        float4 normalSample = normalTexture.Sample(linearSampler, uv);

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
        emissive = emissiveTex.Sample(linearSampler, uv).rgb;
    }
#else
    // SM5 path: emissiveTexture is always bound (real map or fallback black).
    // Fallback black (0,0,0) produces zero emissive contribution — correct default.
    emissive = emissiveTexture.Sample(linearSampler, uv).rgb;
#endif

    // Apply emissive color tint and intensity multiplier.
    // When emissiveIntensity is 0 or emissiveColor is (0,0,0), result is zero.
    // HDR values > 1.0 are expected and will drive the bloom pass.
    emissive *= emissiveColor * emissiveIntensity;

    // ── Write G-Buffer (4-RT layout) ──
    GBufferOutput output;
    output.albedoAO     = float4(albedo, ao);
    output.normalMatPbr = float4(OctEncode(N), metallic, roughness);
    output.positionWS   = float4(input.positionWS, 1.0f);
    output.emissiveHDR  = float4(emissive, 0.0f);

    return output;
}
