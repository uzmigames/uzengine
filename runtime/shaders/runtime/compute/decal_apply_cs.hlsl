/// DBuffer application compute shader — merges DBuffer into GBuffer.
///
/// Runs as a fullscreen compute dispatch after the DBuffer has been filled by
/// all decal passes and before the deferred lighting pass. Reads the three
/// DBuffer render targets and modifies the GBuffer albedo, normal, and
/// roughness/metallic/AO channels via alpha-weighted lerp.
///
/// When the DBuffer alpha is 0 for a pixel, the GBuffer is untouched.
///
/// # Thread Group Size
/// 8x8x1 — 64 threads per group. Dispatch ceil(width/8) x ceil(height/8) x 1.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer application
/// @ref Pesce 2014 "Rendering Decals in Screen Space"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Push constants at b0 — bindless indices for DBuffer + GBuffer textures.
cbuffer DecalApplyConstants : register(b0) {
    uint dbufferASrvIndex;      ///< DBuffer A: albedo + opacity (RGBA8Unorm SRV)
    uint dbufferBSrvIndex;      ///< DBuffer B: normal + blend (RGBA8Unorm SRV)
    uint dbufferCSrvIndex;      ///< DBuffer C: roughness/metallic/AO + blend (RGBA8Unorm SRV)
    uint gbufferAlbedoUavIndex; ///< GBuffer albedo+AO UAV (RGBA8Unorm)
    uint gbufferNormalUavIndex; ///< GBuffer packed normal+metallic+roughness UAV (RGBA8Unorm)
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint texWidth;              ///< Render target width in texels.
    uint texHeight;             ///< Render target height in texels.
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // Bounds check — skip threads outside the render target.
    if (DTid.x >= texWidth || DTid.y >= texHeight) return;

    uint2 coord = DTid.xy;

    // ── Load DBuffer values ──────────────────────────────────────────────
    Texture2D<float4> dbufferA = ResourceDescriptorHeap[dbufferASrvIndex];
    Texture2D<float4> dbufferB = ResourceDescriptorHeap[dbufferBSrvIndex];
    Texture2D<float4> dbufferC = ResourceDescriptorHeap[dbufferCSrvIndex];

    float4 dA = dbufferA.Load(int3(coord, 0));
    float4 dB = dbufferB.Load(int3(coord, 0));
    float4 dC = dbufferC.Load(int3(coord, 0));

    // ── Early out if no decal coverage ───────────────────────────────────
    // If all three alpha channels are near zero, skip this pixel.
    float maxAlpha = max(dA.a, max(dB.a, dC.a));
    if (maxAlpha < 0.001) return;

    // ── Load and modify GBuffer ──────────────────────────────────────────
    RWTexture2D<float4> gbufferAlbedo = ResourceDescriptorHeap[gbufferAlbedoUavIndex];
    RWTexture2D<float4> gbufferNormal = ResourceDescriptorHeap[gbufferNormalUavIndex];

    float4 gAlbedo = gbufferAlbedo[coord];
    float4 gNormal = gbufferNormal[coord];

    // ── Apply DBuffer A: albedo modification ─────────────────────────────
    // lerp(gbuffer.albedo.rgb, decal.albedo.rgb, decal.alpha)
    // AO channel (alpha of GBuffer albedo) is preserved.
    if (dA.a > 0.001) {
        gAlbedo.rgb = lerp(gAlbedo.rgb, dA.rgb, dA.a);
    }

    // ── Apply DBuffer B: normal modification ─────────────────────────────
    // The decal normal is stored in [0,1] range (same as GBuffer octahedral encoding).
    // We blend the encoded values directly — this is an approximation but visually
    // correct for small normal perturbations typical of decals.
    if (dB.a > 0.001) {
        gNormal.rg = lerp(gNormal.rg, dB.rg, dB.a);
    }

    // ── Apply DBuffer C: material modification ───────────────────────────
    // GBuffer normal texture packing: RG = octahedral normal, B = metallic, A = roughness
    if (dC.a > 0.001) {
        gNormal.b = lerp(gNormal.b, dC.g, dC.a);    // metallic
        gNormal.a = lerp(gNormal.a, dC.r, dC.a);    // roughness
        gAlbedo.a = lerp(gAlbedo.a, dC.b, dC.a);    // AO (stored in albedo alpha)
    }

    // ── Write back modified GBuffer ──────────────────────────────────────
    gbufferAlbedo[coord] = gAlbedo;
    gbufferNormal[coord] = gNormal;
}
