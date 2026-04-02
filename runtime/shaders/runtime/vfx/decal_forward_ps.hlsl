/// Forward decal pixel shader — projects a decal onto the scene surface and
/// outputs a single blended RGBA color to the HDR render target.
///
/// This is the forward-rendering variant of the DBuffer decal system. Unlike the
/// deferred path (geometry/decal_ps.hlsl) which writes to 3 G-Buffer RTs, this
/// shader outputs a single float4 to SV_Target0 (RGBA16Float HDR).
///
/// # Differences from Deferred Path
/// - Single render target (no normal or roughness output)
/// - No angle rejection (no G-Buffer normals available in forward)
/// - No normal/material texture sampling (single RT = albedo only)
/// - Shares the same vertex shader (geometry/decal_vs.hlsl / EngineShader::DecalVS)
///
/// # Pipeline State
/// - Rasterizer: CullMode = FRONT (render back faces of the OBB box)
/// - DepthStencil: DepthWrite = OFF, DepthFunc = GREATER_EQUAL (reversed-Z)
/// - Blend: SrcAlpha / InvSrcAlpha on RT0
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" -- DBuffer decals
/// @ref Pesce 2014 "Rendering Decals in Screen Space" -- screen-space projection

#include "../include/depth_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame CB at b1.
/// Contains camera matrices and lighting data. Only invViewProj is used by the
/// pixel shader for depth-to-world reconstruction.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
};

/// Per-object CB at b2 — decal projection and material parameters.
/// Layout must match geometry/decal_vs.hlsl exactly.
cbuffer DecalObjectCB : register(b2) {
    row_major float4x4 decalWorld;      ///< Decal world transform (position + rotation + extents).
    row_major float4x4 decalInvWorld;   ///< Inverse of decalWorld (world -> local projection).
    float    decalOpacity;              ///< Global opacity [0, 1].
    float    decalNormalBlend;          ///< Normal blend intensity (unused in forward path).
    float    decalRoughness;            ///< Roughness value (unused in forward path -- single RT).
    float    decalMetallic;             ///< Metallic value (unused in forward path -- single RT).
    float    decalFadeDistance;         ///< Edge fade distance in centimeters.
    float    decalAngleFade;            ///< Angle fade threshold in degrees (unused -- no GBuffer normals).
    uint     decalAlbedoSrv;            ///< Bindless SRV index for albedo texture (UINT32_MAX = no texture).
    uint     decalNormalSrv;            ///< Normal SRV index (unused in forward path).
    uint     decalMaterialSrv;          ///< Material SRV index (unused in forward path).
    uint     decalDepthSrv;             ///< Depth buffer SRV index for world position reconstruction.
    uint     decalNormalGbufSrv;        ///< GBuffer normal SRV (unused in forward path).
    float    _pad0;                     ///< Padding to 16-byte alignment.
};

// ─────────────────────────────────────────────────────────────────────────────
// Samplers & Textures
// ─────────────────────────────────────────────────────────────────────────────

/// Non-bindless texture declarations for the SM5 fallback path.
/// When UZ_BINDLESS is defined, textures are fetched from the descriptor heap
/// using the SRV indices in DecalObjectCB.
#ifndef UZ_BINDLESS
Texture2D<float>  t_DepthBuffer  : register(t0); ///< Scene depth (reversed-Z).
Texture2D<float4> t_DecalAlbedo  : register(t2); ///< Decal albedo/color texture.
#endif

/// Bilinear clamp sampler (static sampler s2 in the shared root signature).
SamplerState linearClamp : register(s2);

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Must match DecalVSOutput from geometry/decal_vs.hlsl.
struct DecalPSInput {
    float4 positionCS : SV_Position;   ///< Screen-space position (pixel center).
    float4 screenPos  : TEXCOORD0;     ///< Clip-space position for UV reconstruction.
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Reconstruct World Position from Depth
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs the world-space position of the scene surface at this pixel.
///
/// Uses the clip-space interpolant from the vertex shader to compute screen UV,
/// samples the depth buffer, and unprojects through the inverse view-projection.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" -- depth reconstruction
///
/// # Arguments
/// - screenUV: normalized screen coordinates in [0, 1], origin top-left.
/// - rawDepth: depth buffer value (reversed-Z: near=1.0, far=0.0).
///
/// # Returns
/// World-space position of the scene surface.
float3 ReconstructWorldPos(float2 screenUV, float rawDepth) {
    // Convert screen UV [0,1] to NDC [-1,+1], with Y flipped (D3D UV top-left vs NDC bottom-left)
    float2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Build clip-space position using reversed-Z depth directly
    float4 clipPos = float4(ndc, rawDepth, 1.0);

    // Unproject to world space via the camera's inverse view-projection
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Edge Fade
// ─────────────────────────────────────────────────────────────────────────────

/// Computes a smooth fade factor based on distance from the decal OBB boundary.
///
/// Returns 1.0 at the center of the decal volume, fading smoothly to 0.0 near
/// the edges. The fade width is controlled by fadeNorm, which is the fade distance
/// normalized to [0, 1] relative to the decal extents.
///
/// @ref Pesce 2014 "Rendering Decals in Screen Space" -- edge attenuation
///
/// # Arguments
/// - uvw: the pixel's position in decal UV space [0, 1]^3.
/// - fadeNorm: normalized fade width (decalFadeDistance / average decal extent).
///
/// # Returns
/// Edge fade factor in [0, 1]. 1.0 = no fade, 0.0 = fully faded.
float ComputeEdgeFade(float3 uvw, float fadeNorm) {
    // If fade distance is zero or negative, no fading -- return full intensity
    if (fadeNorm <= 0.0) return 1.0;

    // Compute distance from each of the six faces of the [0,1]^3 unit cube
    float3 distFromMin = uvw;
    float3 distFromMax = 1.0 - uvw;

    // The minimum distance to any face determines how close we are to the boundary
    float minDist = min(
        min(distFromMin.x, distFromMin.y),
        min(min(distFromMin.z, distFromMax.x),
            min(distFromMax.y, distFromMax.z))
    );

    // Normalize by the fade width and clamp to [0, 1]
    return saturate(minDist / fadeNorm);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Forward decal pixel shader entry point.
///
/// Projects the scene surface into the decal's local space, samples the decal
/// albedo texture, applies edge fading, and outputs premultiplied-alpha color
/// to a single HDR render target.
///
/// The algorithm:
/// 1. Compute screen UV from the clip-space interpolant (perspective divide).
/// 2. Sample the depth buffer and discard sky pixels.
/// 3. Reconstruct world-space position via inverse view-projection.
/// 4. Transform world position into decal local space via decalInvWorld.
/// 5. Remap local [-0.5, 0.5] to UV [0, 1] and discard outside the volume.
/// 6. Compute smooth edge fade near OBB boundaries.
/// 7. Sample albedo texture (or use white if no texture bound).
/// 8. Output blended color with combined opacity for alpha blending.
///
/// # Parameters
/// - input: rasterized decal box fragment from the shared DecalVS.
///
/// # Returns
/// float4 color written to SV_Target0 (RGBA16Float HDR render target).
float4 main(DecalPSInput input) : SV_Target0 {

    // ── Step 1: Compute screen UV from clip-space interpolant ────────────
    // The VS passes the clip-space position in screenPos. Perspective divide
    // gives normalized device coordinates, which we remap to [0,1] UV.
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV = screenUV * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;  // D3D: UV origin top-left, NDC origin bottom-left

    // ── Step 2: Sample depth buffer and discard sky ──────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[decalDepthSrv];
    float rawDepth = depthTex.SampleLevel(linearClamp, screenUV, 0);
#else
    float rawDepth = t_DepthBuffer.SampleLevel(linearClamp, screenUV, 0);
#endif

    // Sky pixels in reversed-Z have depth near 0.0 -- no surface to project onto
    if (IsSkyDepth(rawDepth)) {
        discard;
    }

    // ── Step 3: Reconstruct world-space position ─────────────────────────
    float3 worldPos = ReconstructWorldPos(screenUV, rawDepth);

    // ── Step 4: Transform to decal local space ───────────────────────────
    // decalInvWorld maps world space back to the unit cube [-0.5, 0.5]^3
    // that the vertex shader uses for the decal OBB.
    float4 localPos4 = mul(float4(worldPos, 1.0), decalInvWorld);
    float3 localPos = localPos4.xyz;

    // ── Step 5: Remap to UV space and discard outside volume ─────────────
    // The VS cube spans [-0.5, 0.5], so add 0.5 to get [0, 1] UV coordinates.
    float3 uvw = localPos + 0.5;

    // Discard pixels that fall outside the decal OBB on any axis
    if (any(uvw < 0.0) || any(uvw > 1.0)) {
        discard;
    }

    // Decal projects along local Z; XY become the texture UV
    float2 decalUV = uvw.xy;

    // ── Step 6: Edge fade ────────────────────────────────────────────────
    // Convert fade distance from centimeters to normalized [0,1] range relative
    // to the decal's average world-space extent. The decal world matrix columns
    // encode the three axes with their extents baked into the scale.
    float avgExtent = max(
        (length(decalWorld[0].xyz) + length(decalWorld[1].xyz)) * 0.5,
        0.001  // Guard against degenerate zero-scale decals
    );
    float fadeNorm = decalFadeDistance / avgExtent;
    float edgeFade = ComputeEdgeFade(uvw, fadeNorm);

    // ── Step 7: Sample decal albedo texture ──────────────────────────────
    float4 albedo;

#ifdef UZ_BINDLESS
    if (decalAlbedoSrv != 0xFFFFFFFF) {
        // Bindless path: fetch texture from descriptor heap using the SRV index
        Texture2D<float4> albedoTex = ResourceDescriptorHeap[decalAlbedoSrv];
        albedo = albedoTex.SampleLevel(linearClamp, decalUV, 0);
    } else {
        // No albedo texture bound: use opaque white (decal acts as a tint/mask)
        albedo = float4(1.0, 1.0, 1.0, 1.0);
    }
#else
    // SM5 non-bindless path: texture is always bound at t2
    albedo = t_DecalAlbedo.SampleLevel(linearClamp, decalUV, 0);
#endif

    // ── Step 8: Compute final opacity and output ─────────────────────────
    // Combined opacity = texture alpha * global decal opacity * edge fade
    float finalAlpha = albedo.a * decalOpacity * edgeFade;

    // Early discard for fully transparent pixels to avoid blend overhead
    if (finalAlpha < (1.0 / 255.0)) {
        discard;
    }

    // Output to the single HDR render target with alpha for SrcAlpha/InvSrcAlpha blending
    return float4(albedo.rgb, finalAlpha);
}
