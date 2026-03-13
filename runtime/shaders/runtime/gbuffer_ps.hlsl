/// G-Buffer pixel shader for the deferred rendering geometry pass.
///
/// Writes surface attributes to three render targets (MRT) for later
/// consumption by the deferred lighting pass. No lighting is computed here.
///
/// SM6.6 (UZ_BINDLESS): samples PBR textures via ResourceDescriptorHeap[index].
/// SM5 (!UZ_BINDLESS): samples PBR textures via register-bound textures t0-t4.
///
/// # Render Targets
/// - SV_Target0: Albedo   (R8G8B8A8_UNORM) — rgb = base color, a = 1.0
/// - SV_Target1: Normal   (R16G16B16A16_FLOAT) — rgb = world-space normal [-1,1], a = 1.0
/// - SV_Target2: MetalRough (R8G8B8A8_UNORM) — r = metallic, g = roughness, b = ao, a = 1.0
///
/// # Texture Maps (all optional — index 0 = use default value)
/// - albedoIndex:    BaseColor map (sRGB) -> linear albedo
/// - normalIndex:    Tangent-space normal map -> perturbed world normal via TBN
/// - roughnessIndex: Roughness map (R channel) -> perceptual roughness
/// - metallicIndex:  Metallic map (R channel) -> metallic factor
/// - aoIndex:        Ambient occlusion map (R channel) -> AO multiplier

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

/// G-Buffer MRT output. Each field maps to a separate render target.
struct GBufferOutput {
    float4 albedo     : SV_Target0; ///< rgb = base color (linear), a = 1.0
    float4 normal     : SV_Target1; ///< rgb = world normal [-1,1], a = 1.0
    float4 metalRough : SV_Target2; ///< r = metallic, g = roughness, b = ao, a = 1.0
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
cbuffer PerObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    uint     albedoIndex;
    uint     normalIndex;
    uint     roughnessIndex;
    uint     metallicIndex;
    uint     aoIndex;
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
    if (albedoIndex > 0)    albedo    = albedoTexture.Sample(linearSampler, uv).rgb;
    if (roughnessIndex > 0) roughness = roughnessTexture.Sample(linearSampler, uv).r;
    if (metallicIndex > 0)  metallic  = metallicTexture.Sample(linearSampler, uv).r;
    if (aoIndex > 0)        ao        = aoTexture.Sample(linearSampler, uv).r;
#endif

    // ── Surface normal (with optional tangent-space normal mapping) ──
    float3 N = normalize(input.normalWS);

    if (normalIndex > 0) {
        float3 T = normalize(input.tangentWS.xyz);
        float3 B = cross(N, T) * input.tangentWS.w; // w = handedness
        float3x3 TBN = float3x3(T, B, N);

        float3 normalTS;
#ifdef UZ_BINDLESS
        Texture2D<float4> nrmTex = ResourceDescriptorHeap[normalIndex];
        normalTS = nrmTex.Sample(linearSampler, uv).rgb;
#else
        normalTS = normalTexture.Sample(linearSampler, uv).rgb;
#endif
        // Remap from [0,1] to [-1,1] tangent space
        normalTS = normalTS * 2.0f - 1.0f;
        N = normalize(mul(normalTS, TBN));
    }

    // ── Write G-Buffer ──
    GBufferOutput output;
    output.albedo     = float4(albedo, 1.0f);
    output.normal     = float4(N, 1.0f);      // RGBA16F — no packing needed
    output.metalRough = float4(metallic, roughness, ao, 1.0f);

    return output;
}
