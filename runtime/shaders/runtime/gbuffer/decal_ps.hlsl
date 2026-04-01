/// UzEngine Decal Pixel Shader — deferred decal projection onto GBuffer.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer technique
/// @ref Pesce 2014 "Rendering Decals in Screen Space"
///
/// Uses a precomputed SvPositionToDecal matrix that transforms pixel coordinates
/// + scene depth directly to decal local space [-1,1]³ in a single matrix multiply.
/// SvPositionToDecal = ScreenToNDC × InvViewProj × WorldToDecal
///
/// Outputs to 3 GBuffer targets (rendered AFTER GBuffer fill with alpha blending):
///   MRT0: decal albedo RGB + opacity A
///   MRT1: decal normal [0,1] RGB + normal blend A
///   MRT2: unused (roughness would go here in a DBuffer approach)

#include "../common/constants.hlsli"

cbuffer DecalObjectCB : register(b2) {
    row_major float4x4 decalWorld;      // 0:   Decal OBB world transform
    row_major float4x4 svPosToDecal;    // 64:  SvPosition → decal local [-1,1]³
    row_major float4x4 viewProj;        // 128: View-projection (unused in PS)
    float    opacity;          // 192
    float    normalBlend;      // 196
    float    roughness;        // 200
    float    metallic;         // 204
    float    fadeDistance;      // 208
    float    angleFade;        // 212
    uint     albedoSrv;        // 216
    uint     normalSrv;        // 220
    uint     materialSrv;      // 224
    uint     depthSrv;         // 228
    uint     normalGbufSrv;    // 232
    float    _pad0;            // 236
};

SamplerState linearSampler : register(s0);

struct PSOutput {
    float4 target0 : SV_Target0;  // GBuffer albedo (alpha-blended)
    float4 target1 : SV_Target1;  // GBuffer normal (alpha-blended)
    float4 target2 : SV_Target2;  // GBuffer emissive (unused, output zero)
};

PSOutput main(float4 svPosition : SV_Position) {
    PSOutput output;
    output.target0 = float4(0, 0, 0, 0);
    output.target1 = float4(0, 0, 0, 0);
    output.target2 = float4(0, 0, 0, 0);

    // ── Read scene depth at this pixel ───────────────────────────────────
    /// @ref reversed-Z: near=1, far=0
    float sceneDepth = 0.0;
#ifdef UZ_BINDLESS
    if (depthSrv != kSentinelSrvIndex) {
        Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrv];
        sceneDepth = depthTex.Load(int3(svPosition.xy, 0));
    }
#endif

    // ── Project SvPosition + depth → decal local space ───────────────────
    /// @ref UE5 DeferredDecal: single matrix multiply SvPositionToDecal
    /// @constants svPosToDecal = ScreenToNDC × InvViewProj × WorldToDecal
    float4 svPosWithDepth = float4(svPosition.xy, sceneDepth, 1.0);
    float4 decalLocalH = mul(svPosWithDepth, svPosToDecal);
    float3 decalLocal = decalLocalH.xyz / decalLocalH.w;

    // ── Clip to decal OBB [-0.5, 0.5]³ ─────────────────────────────────
    // The unit cube spans [-0.5, 0.5]³ in local space (world matrix includes scale).
    clip(decalLocal + 0.5);
    clip(0.5 - decalLocal);

    // ── UV from local position: [-0.5, 0.5] → [0, 1] ────────────────────
    float2 decalUV = decalLocal.xy + 0.5;

    // ── Edge fade — smooth falloff near OBB boundaries ───────────────────
    /// @ref Valient 2014 — edge attenuation prevents hard cutoff
    float edgeFade = 1.0;
    if (fadeDistance > 0.0) {
        float3 edgeDist = 0.5 - abs(decalLocal);
        float minEdge = min(min(edgeDist.x, edgeDist.y), edgeDist.z);
        edgeFade = saturate(minEdge / fadeDistance);
    }

    float finalAlpha = opacity * edgeFade;
    if (finalAlpha <= 0.001) {
        clip(-1);
    }

    // ── Sample decal albedo texture ──────────────────────────────────────
    float4 albedoSample = float4(1, 1, 1, 1);
#ifdef UZ_BINDLESS
    if (albedoSrv != kSentinelSrvIndex) {
        Texture2D<float4> albedoTex = ResourceDescriptorHeap[albedoSrv];
        albedoSample = albedoTex.Sample(linearSampler, decalUV);
    }
#endif

    // ── Output to GBuffer with alpha blending ────────────────────────────
    output.target0 = float4(albedoSample.rgb, finalAlpha);

    // Normal: pass through (flat normal = no modification) unless texture bound
    float3 decalNormalTS = float3(0.5, 0.5, 1.0);
#ifdef UZ_BINDLESS
    if (normalSrv != kSentinelSrvIndex) {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[normalSrv];
        decalNormalTS = normalTex.Sample(linearSampler, decalUV).xyz;
    }
#endif
    output.target1 = float4(decalNormalTS, normalBlend * finalAlpha);

    // Target2: pass zero alpha (no modification to emissive channel)
    output.target2 = float4(0, 0, 0, 0);

    return output;
}
