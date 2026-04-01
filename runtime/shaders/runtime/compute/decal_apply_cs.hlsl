/// UzEngine Decal Apply Compute Shader — merges DBuffer into GBuffer.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer merge
///
/// Reads the three DBuffer textures (albedo, normal, material) and blends
/// them into the GBuffer albedo and normal textures using alpha blending.
///
/// Root constants (b0): DBuffer SRV indices, GBuffer UAV indices, dimensions.

cbuffer DecalApplyConstants : register(b0) {
    uint dbufferASrvIndex;       // DBuffer A (albedo + opacity)
    uint dbufferBSrvIndex;       // DBuffer B (normal + normal blend)
    uint dbufferCSrvIndex;       // DBuffer C (roughness + metallic + blend)
    uint gbufferAlbedoUavIndex;  // GBuffer albedo UAV
    uint gbufferNormalUavIndex;  // GBuffer normal UAV
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint texWidth;               // Render target width
    uint texHeight;              // Render target height
};

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= texWidth || DTid.y >= texHeight) return;

    int2 coord = int2(DTid.xy);

    // ── Load DBuffer values ──────────────────────────────────────────────
    Texture2D<float4> dbufA = ResourceDescriptorHeap[dbufferASrvIndex];
    Texture2D<float4> dbufB = ResourceDescriptorHeap[dbufferBSrvIndex];
    Texture2D<float4> dbufC = ResourceDescriptorHeap[dbufferCSrvIndex];

    float4 dA = dbufA.Load(int3(coord, 0));
    float4 dB = dbufB.Load(int3(coord, 0));
    float4 dC = dbufC.Load(int3(coord, 0));

    // Skip pixels with no decal contribution (alpha ≈ 0).
    if (dA.a < 0.001 && dB.a < 0.001 && dC.a < 0.001) return;

    // ── Load current GBuffer values ──────────────────────────────────────
    RWTexture2D<float4> gbufAlbedo = ResourceDescriptorHeap[gbufferAlbedoUavIndex];
    RWTexture2D<float4> gbufNormal = ResourceDescriptorHeap[gbufferNormalUavIndex];

    float4 curAlbedo = gbufAlbedo[coord];
    float4 curNormal = gbufNormal[coord];

    // ── Blend DBuffer into GBuffer (alpha blend) ─────────────────────────
    // DBuffer A → GBuffer albedo: lerp by decal opacity.
    float albedoAlpha = dA.a;
    curAlbedo.rgb = lerp(curAlbedo.rgb, dA.rgb, albedoAlpha);

    // DBuffer B → GBuffer normal: lerp by normal blend factor.
    // DBuffer B stores tangent-space normal in [0,1] encoding.
    float normalAlpha = dB.a;
    if (normalAlpha > 0.001) {
        curNormal.rgb = lerp(curNormal.rgb, dB.rgb, normalAlpha);
    }

    // DBuffer C → GBuffer normal alpha (roughness is packed in normal.a
    // in UzEngine's GBuffer layout).
    float materialAlpha = dC.a;
    if (materialAlpha > 0.001) {
        curNormal.a = lerp(curNormal.a, dC.r, materialAlpha); // roughness → normal.a
    }

    // ── Write back ───────────────────────────────────────────────────────
    gbufAlbedo[coord] = curAlbedo;
    gbufNormal[coord] = curNormal;
}
