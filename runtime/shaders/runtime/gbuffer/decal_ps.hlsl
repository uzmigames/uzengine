/// UzEngine Decal Pixel Shader — deferred decal projection onto GBuffer.
///
/// @ref UE5 DeferredDecal.usf — exact projection pattern
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
///
/// Key steps (matching UE5 exactly):
/// 1. Replace SvPosition.z with scene depth from depth buffer
/// 2. Transform (SvPosition.xy, sceneDepth, 1) → decal local space via SvPositionToDecal
/// 3. Clip to [-1,1]³
/// 4. UV = (OSPosition * 0.5 + 0.5).zyx.xy — swizzle so XY are perpendicular to projection
/// 5. Fade along projection axis (Z)
/// 6. Sample material textures at DecalUVs
/// 7. Output with opacity * fade

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
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
};

PSOutput main(float4 svPosition : SV_Position) {
    PSOutput output;
    output.target0 = float4(0, 0, 0, 0);
    output.target1 = float4(0, 0, 0, 0);
    output.target2 = float4(0, 0, 0, 0);

    // ── Step 1: Replace SvPosition.z with scene depth ────────────────────
    /// @ref UE5 DeferredDecal.usf line 134: In.SvPosition.z = LookupDeviceZ(ScreenUV)
    float sceneDepth = svPosition.z;
#ifdef UZ_BINDLESS
    if (depthSrv != kSentinelSrvIndex) {
        Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrv];
        sceneDepth = depthTex.Load(int3(svPosition.xy, 0));
    }
#endif

    // ── Step 2: SvPosition → decal local space [-1,1]³ ───────────────────
    /// @ref UE5 line 139: mul(float4(In.SvPosition.xyz, 1), SvPositionToDecal)
    float4 DecalVectorHom = mul(float4(svPosition.xy, sceneDepth, 1.0), svPosToDecal);
    float3 OSPosition = DecalVectorHom.xyz / DecalVectorHom.w;

    // ── Step 3: Clip outside [-1,1]³ ─────────────────────────────────────
    /// @ref UE5 line 144-145
    clip(OSPosition.xyz + 1.0);
    clip(1.0 - OSPosition.xyz);

    // ── Step 4: UV mapping ──────────────────────────────────────────────
    /// @ref UE5 line 152-158: DecalVector.zyx swizzle — adapted for Z-up.
    /// UE5 (Y-up): projects along X, UVs from ZY → swizzle .zyx
    /// UzEngine (Z-up): projects along Z, UVs from XY → no swizzle needed.
    float3 DecalVector = OSPosition * 0.5 + 0.5;
    float2 DecalUVs = DecalVector.xy;

    // ── Step 5: Fade along projection axis (Z in our Z-up system) ────────
    /// @ref UE5 line 185: saturate(4 - 4 * abs(axis * 2 - 1))
    float DecalFading = saturate(4.0 - 4.0 * abs(DecalVector.z * 2.0 - 1.0));

    // ── Step 6: Sample decal albedo texture ──────────────────────────────
    float3 BaseColor = float3(1, 1, 1);
    float TexOpacity = 1.0;
#ifdef UZ_BINDLESS
    if (albedoSrv != kSentinelSrvIndex) {
        Texture2D<float4> albedoTex = ResourceDescriptorHeap[albedoSrv];
        float4 sample = albedoTex.Sample(linearSampler, DecalUVs);
        BaseColor = sample.rgb;
        // Use texture alpha as opacity mask when it has meaningful values (> 0).
        // Grayscale PNGs loaded as RGBA may have alpha=0 — treat as "no mask".
        if (sample.a > 0.001) {
            TexOpacity = sample.a;
        }
    } else {
        // No albedo texture = no visible decal
        clip(-1);
    }
#else
    clip(-1); // Decals require bindless
#endif

    // ── Step 7: Final opacity = material opacity × texture mask × fade ───
    /// @ref UE5 line 266: Opacity = GetMaterialOpacity() * DecalFading
    float FinalOpacity = opacity * TexOpacity * DecalFading;
    if (FinalOpacity <= 0.001) {
        clip(-1);
    }

    // ── Output to GBuffer with alpha blending ────────────────────────────
    output.target0 = float4(BaseColor, FinalOpacity);

    // Normal channel
    float3 decalNormal = float3(0.5, 0.5, 1.0);
#ifdef UZ_BINDLESS
    if (normalSrv != kSentinelSrvIndex) {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[normalSrv];
        decalNormal = normalTex.Sample(linearSampler, DecalUVs).xyz;
    }
#endif
    output.target1 = float4(decalNormal, normalBlend * FinalOpacity);

    output.target2 = float4(0, 0, 0, 0);

    return output;
}
