// gbuffer_ps.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kGbufferPS[] = R"hlsl(
/// G-Buffer pixel shader for the deferred rendering geometry pass.
///
/// Writes surface attributes to two render targets (MRT) for later
/// consumption by the deferred lighting pass. No lighting is computed here.
///
/// SM6.6 (UZ_BINDLESS): samples PBR textures via ResourceDescriptorHeap[index].
/// SM5 (!UZ_BINDLESS): samples PBR textures via register-bound textures t0-t4.
///
/// # Render Targets (optimized UE5-style packed layout)
/// - SV_Target0: AlbedoAO     (R8G8B8A8_UNORM) — rgb = base color (linear), a = AO
/// - SV_Target1: NormalMatPbr  (R8G8B8A8_UNORM) — rg = octahedral normal, b = metallic, a = roughness
///
/// # Texture Maps (all optional — index 0 = use default value)
/// - albedoIndex:    BaseColor map (sRGB) -> linear albedo
/// - normalIndex:    Tangent-space normal map -> perturbed world normal via TBN
/// - roughnessIndex: Roughness map (R channel) -> perceptual roughness
/// - metallicIndex:  Metallic map (R channel) -> metallic factor
/// - aoIndex:        Ambient occlusion map (R channel) -> AO multiplier

// ── begin include: ../include/octahedral.hlsli ──
#ifndef OCTAHEDRAL_HLSLI
#define OCTAHEDRAL_HLSLI

/// Encode a unit normal to octahedral [0,1]² representation.
///
/// Projects the normal onto the L1 unit sphere, then folds the lower
/// hemisphere into the outer edges of the [−1,1]² square. The result is
/// remapped to [0,1]² for storage in an R8G8 render target.
///
/// Reference: "Survey of Efficient Representations for Independent Unit Vectors"
/// (Cigolle, Donow, Evangelakos, Mara, McGuire, Meyer — JCGT 2014)
///
/// # Parameters
/// - n: unit-length world-space normal.
///
/// # Returns
/// float2 in [0,1]² suitable for R8G8_UNORM storage.
float2 OctEncode(float3 n)
{
    // Project onto L1 norm sphere: sum(|xyz|) = 1
    float3 p = n / (abs(n.x) + abs(n.y) + abs(n.z));

    // Wrap lower hemisphere — fold the four triangular flaps outward.
    // sign(0) == 0 in HLSL, which would collapse the edge to zero.
    // Use the conditional form to map zero to +1, preserving the octahedron seam.
    float2 wrap = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0,
                                               p.y >= 0.0 ? 1.0 : -1.0);
    float2 oct = (n.z >= 0.0) ? p.xy : wrap;

    // Remap [-1,1] to [0,1]
    return oct * 0.5 + 0.5;
}

/// Decode octahedral [0,1]² back to a unit normal.
///
/// Reverses the encoding: remaps to [−1,1]², reconstructs Z from the L1
/// constraint, unfolds the lower hemisphere, and normalizes.
///
/// # Parameters
/// - f: float2 in [0,1]² read from the G-Buffer RG channels.
///
/// # Returns
/// Unit-length world-space normal (float3).
float3 OctDecode(float2 f)
{
    // Remap [0,1] to [-1,1]
    f = f * 2.0 - 1.0;

    // Reverse octahedral projection — Z from L1 constraint
    float3 n = float3(f.xy, 1.0 - abs(f.x) - abs(f.y));

    // Unwrap lower hemisphere (same conditional sign as encode)
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0 ? -t : t,
                    n.y >= 0.0 ? -t : t);

    return normalize(n);
}

#endif // OCTAHEDRAL_HLSLI
// ── end include: ../include/octahedral.hlsli ──

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

/// G-Buffer MRT output — 3-RT layout.
struct GBufferOutput {
    float4 albedoAO     : SV_Target0; ///< rgb = base color (linear), a = ambient occlusion
    float4 normalMatPbr : SV_Target1; ///< rg = octahedral-encoded normal, b = metallic, a = roughness
    float4 positionWS   : SV_Target2; ///< xyz = world-space position (debug / reconstruction verification), w = 1
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
    float3   _pad;           // align to 16 bytes before matrices
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
};

#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
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
        normalTS.z = (normalSample.b > 0.0f)
            ? normalSample.b * 2.0f - 1.0f
            : sqrt(max(1.0f - dot(normalTS.xy, normalTS.xy), 0.0f));
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
        normalTS.z = (normalSample.b > 0.0f)
            ? normalSample.b * 2.0f - 1.0f
            : sqrt(max(1.0f - dot(normalTS.xy, normalTS.xy), 0.0f));
        N = normalize(mul(normalTS, TBN));
    }
#endif

    // ── Write G-Buffer (3-RT layout) ──
    GBufferOutput output;
    output.albedoAO     = float4(albedo, ao);
    output.normalMatPbr = float4(OctEncode(N), metallic, roughness);
    output.positionWS   = float4(input.positionWS, 1.0f);

    return output;
}
)hlsl";
} // namespace uz::generated
