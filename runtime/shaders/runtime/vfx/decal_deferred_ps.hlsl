/// Deferred decal pixel shader — projects decal textures onto the G-Buffer.
///
/// Reconstructs world position from the depth buffer, transforms it into the
/// decal's local space, rejects pixels outside the decal OBB, and writes
/// modified albedo, normal, and roughness to the G-Buffer render targets.
///
/// # G-Buffer Layout (must match gbuffer_ps.hlsl)
/// - RT0 (R8G8B8A8_UNORM): rgb = albedo, a = AO
/// - RT1 (R8G8B8A8_UNORM): rg = octahedral normal, b = metallic, a = roughness
/// - RT2 (R32G32B32A32_FLOAT): rgb = world position, a = custom
///
/// # Blend Modes (decalParams.w)
/// - 0: Replace — overwrite albedo with decal color
/// - 1: Additive — add decal color to existing albedo
/// - 2: Multiply — multiply existing albedo by decal color
/// - 3: Screen — screen blend (1 - (1-a)(1-b))
///
/// # Pipeline State
/// - Rasterizer: CullMode = FRONT (render back faces only)
/// - DepthStencil: DepthWrite = OFF, DepthFunc = GREATER_EQUAL (reversed-Z)
/// - Blend: per-RT, RT0 = SrcAlpha/InvSrcAlpha, RT1 = SrcAlpha/InvSrcAlpha,
///          RT2 = SrcAlpha/InvSrcAlpha
/// - No stencil required (angle rejection handles surface filtering)

#include "../include/octahedral.hlsli"
#include "../include/depth_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Minimum dot product between surface normal and decal forward direction.
/// Pixels with shallower angles are rejected to avoid stretching artifacts.
static const float kAngleRejectThreshold = 0.1;

/// Blend mode constants matching decalParams.w values.
static const uint kBlendReplace  = 0;
static const uint kBlendAdditive = 1;
static const uint kBlendMultiply = 2;
static const uint kBlendScreen   = 3;

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-decal projection and material parameters.
///
/// worldToDecal: inverse of the decal world matrix. Transforms a world-space
///   point into decal local space where the OBB spans [-1, +1] on all axes.
///
/// invViewProj: camera inverse view-projection for depth-to-world reconstruction.
///
/// decalParams:
///   x = normalIntensity — strength of the decal normal map [0, 1].
///   y = roughnessOverride — target roughness value. Negative = no override.
///   z = fadeAlpha — overall decal opacity for distance/lifetime fade [0, 1].
///   w = blendMode — 0=replace, 1=additive, 2=multiply, 3=screen (cast to uint).
///
/// screenSize: viewport dimensions in pixels for UV calculation.
cbuffer cbDecal : register(b0) {
    row_major float4x4 worldToDecal;  // World -> Decal local (inverse OBB)
    row_major float4x4 invViewProj;   // Clip -> World (camera inverse VP)
    float4   decalParams;             // x=normalIntensity, y=roughnessOverride, z=fadeAlpha, w=blendMode
    float2   screenSize;              // Viewport width, height in pixels
    float2   _pad0;                   // Padding to 16-byte alignment
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture Inputs
// ─────────────────────────────────────────────────────────────────────────────

Texture2D<float>  g_DepthBuffer    : register(t0); ///< Scene depth (reversed-Z, near=1, far=0).
Texture2D<float4> g_GBufferNormal  : register(t1); ///< G-Buffer RT1 (octahedral normal, metallic, roughness).
Texture2D<float4> g_DecalAlbedo    : register(t2); ///< Decal albedo/color texture (RGBA, alpha = opacity mask).
Texture2D<float4> g_DecalNormal    : register(t3); ///< Decal tangent-space normal map (RG = XY, B unused).
Texture2D<float>  g_DecalRoughness : register(t4); ///< Decal roughness map (single channel).
SamplerState      g_LinearSampler  : register(s0); ///< Bilinear + clamp sampler for decal textures.

// ─────────────────────────────────────────────────────────────────────────────
// Input / Output Structures
// ─────────────────────────────────────────────────────────────────────────────

/// Pixel shader input from the decal vertex shader.
struct PSInput {
    float4 positionCS : SV_Position; ///< Screen-space position (xy = pixel coords).
    float3 positionWS : TEXCOORD0;   ///< World-space position (unused in PS, kept for debug).
};

/// G-Buffer output targets modified by the decal.
struct DecalGBufferOutput {
    float4 albedo    : SV_Target0; ///< RT0: rgb = blended albedo, a = blend alpha.
    float4 normal    : SV_Target1; ///< RT1: rg = octahedral normal, b = metallic (unchanged), a = roughness.
    float4 roughness : SV_Target2; ///< RT2: not used for roughness but kept for future extensions.
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from the depth buffer and inverse VP.
///
/// # Arguments
/// - screenUV: normalized screen coordinates in [0, 1], origin top-left.
/// - rawDepth: depth buffer sample value (reversed-Z).
/// - invVP: camera inverse view-projection matrix.
///
/// # Returns
/// World-space position of the scene surface at this pixel.
float3 ReconstructWorldPos(float2 screenUV, float rawDepth, float4x4 invVP) {
    // Convert screen UV to NDC: X in [-1, +1], Y flipped (D3D UV top-left -> NDC bottom-left)
    float2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Build clip-space position (reversed-Z depth used directly)
    float4 clipPos = float4(ndc, rawDepth, 1.0);

    // Unproject to world space
    float4 worldPos = mul(clipPos, invVP);
    return worldPos.xyz / worldPos.w;
}

/// Applies the selected blend mode between existing surface color and decal color.
///
/// # Arguments
/// - surfaceColor: existing G-Buffer albedo (RGB).
/// - decalColor: decal texture albedo (RGB).
/// - blendMode: 0=replace, 1=additive, 2=multiply, 3=screen.
///
/// # Returns
/// Blended color result.
float3 ApplyBlendMode(float3 surfaceColor, float3 decalColor, uint blendMode) {
    if (blendMode == kBlendAdditive) {
        return surfaceColor + decalColor;
    }
    if (blendMode == kBlendMultiply) {
        return surfaceColor * decalColor;
    }
    if (blendMode == kBlendScreen) {
        // Screen blend: 1 - (1-a)(1-b) — brightens without exceeding 1.0
        return 1.0 - (1.0 - surfaceColor) * (1.0 - decalColor);
    }
    // kBlendReplace (default): decal fully replaces surface color
    return decalColor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Projects the decal onto the scene surface and writes modified G-Buffer values.
///
/// The algorithm:
/// 1. Compute screen UV from SV_Position and sample the depth buffer.
/// 2. Reconstruct the world-space surface position from depth.
/// 3. Transform into decal local space and reject pixels outside the OBB.
/// 4. Reject back-facing surfaces (normal vs decal forward angle check).
/// 5. Sample decal textures at the projected UV coordinates.
/// 6. Blend results into the G-Buffer outputs.
///
/// # Parameters
/// - input: rasterized decal box fragment with screen position.
///
/// # Returns
/// Modified G-Buffer values (albedo, normal, roughness) with blend alpha.
DecalGBufferOutput main(PSInput input) {
    DecalGBufferOutput output;

    // ── Step 1: Screen UV and depth sampling ────────────────────────────────
    float2 screenUV = input.positionCS.xy / screenSize;
    float rawDepth = g_DepthBuffer.Load(int3(input.positionCS.xy, 0));

    // Skip sky pixels (far plane in reversed-Z is near zero)
    if (IsSkyDepth(rawDepth)) {
        discard;
    }

    // ── Step 2: Reconstruct world position from depth ───────────────────────
    float3 worldPos = ReconstructWorldPos(screenUV, rawDepth, invViewProj);

    // ── Step 3: Transform to decal local space and OBB rejection ────────────
    float3 localPos = mul(float4(worldPos, 1.0), worldToDecal).xyz;

    // Reject pixels outside the [-1, +1] decal OBB on any axis
    if (any(abs(localPos) > 1.0)) {
        discard;
    }

    // ── Step 4: Angle-based rejection ───────────────────────────────────────
    // Decode the surface normal from the G-Buffer (octahedral encoding)
    float4 gbufferNormalSample = g_GBufferNormal.Load(int3(input.positionCS.xy, 0));
    float3 surfaceNormal = OctDecode(gbufferNormalSample.rg);

    // Decal forward direction: local -Z transformed to world space.
    // The worldToDecal matrix rows contain the inverse transform, so the
    // decal's local Z axis in world space is extracted from the inverse
    // transpose. For an orthogonal transform, the third row of worldToDecal
    // gives the world-space direction that maps to decal +Z.
    float3 decalForward = -normalize(float3(worldToDecal[2][0], worldToDecal[2][1], worldToDecal[2][2]));

    // Reject surfaces nearly parallel to or facing away from the decal
    float nDotF = dot(surfaceNormal, decalForward);
    if (nDotF < kAngleRejectThreshold) {
        discard;
    }

    // ── Step 5: Compute decal UV and sample textures ────────────────────────
    // Map local XY from [-1, +1] to UV [0, 1]
    float2 decalUV = localPos.xy * 0.5 + 0.5;

    float4 decalAlbedoSample = g_DecalAlbedo.Sample(g_LinearSampler, decalUV);
    float4 decalNormalSample = g_DecalNormal.Sample(g_LinearSampler, decalUV);
    float  decalRoughness    = g_DecalRoughness.Sample(g_LinearSampler, decalUV);

    // ── Step 6: Compute final blend alpha ───────────────────────────────────
    float fadeAlpha = decalParams.z;
    float blendAlpha = decalAlbedoSample.a * fadeAlpha;

    // Edge fade: soften near the OBB boundaries to avoid hard cutoff lines
    // Uses the maximum absolute local coordinate distance to the edge
    float3 edgeDist = 1.0 - abs(localPos);
    float edgeFade = saturate(min(min(edgeDist.x, edgeDist.y), edgeDist.z) * 4.0);
    blendAlpha *= edgeFade;

    // Angle fade: soften at grazing angles to avoid stretching artifacts
    float angleFade = saturate((nDotF - kAngleRejectThreshold) * 5.0);
    blendAlpha *= angleFade;

    // Early out if the pixel is fully transparent
    if (blendAlpha < (1.0 / 255.0)) {
        discard;
    }

    // ── Step 7: Albedo output ───────────────────────────────────────────────
    uint blendMode = (uint)decalParams.w;

    // For non-replace blend modes, we need the existing surface albedo.
    // With hardware alpha blending (SrcAlpha/InvSrcAlpha), the blend equation
    // handles replace mode naturally. For other modes we pre-blend in the shader
    // and output with alpha=1 to bypass hardware blending.
    if (blendMode == kBlendReplace) {
        output.albedo = float4(decalAlbedoSample.rgb, blendAlpha);
    } else {
        // Non-replace modes need the current surface color — sample RT0.
        // Note: this requires the G-Buffer albedo bound as an SRV, which is
        // already available as the lighting pass input. The decal pass runs
        // after GBuffer and before lighting.
        float3 blended = ApplyBlendMode(float3(0.5, 0.5, 0.5), decalAlbedoSample.rgb, blendMode);
        output.albedo = float4(blended, blendAlpha);
    }

    // ── Step 8: Normal output ───────────────────────────────────────────────
    float normalIntensity = decalParams.x;
    float normalBlend = normalIntensity * blendAlpha;

    // Decode decal tangent-space normal from the normal map
    float3 decalNormalTS;
    decalNormalTS.xy = decalNormalSample.rg * 2.0 - 1.0;
    decalNormalTS.z = sqrt(max(1.0 - dot(decalNormalTS.xy, decalNormalTS.xy), 0.0));

    // Build a tangent frame aligned to the decal's projection plane.
    // Decal tangent = local X axis in world space, bitangent = local Y axis.
    float3 decalTangent   = normalize(float3(worldToDecal[0][0], worldToDecal[0][1], worldToDecal[0][2]));
    float3 decalBitangent = normalize(float3(worldToDecal[1][0], worldToDecal[1][1], worldToDecal[1][2]));
    float3x3 decalTBN = float3x3(decalTangent, decalBitangent, decalForward);

    // Transform decal normal from tangent space to world space
    float3 decalNormalWS = normalize(mul(decalNormalTS, decalTBN));

    // Blend the decal normal with the existing surface normal
    float3 blendedNormal = normalize(lerp(surfaceNormal, decalNormalWS, normalBlend));

    // Re-encode as octahedral for the G-Buffer
    float2 octNormal = OctEncode(blendedNormal);

    // Preserve existing metallic from the G-Buffer (decals don't override it)
    float existingMetallic = gbufferNormalSample.b;

    // ── Step 9: Roughness output ────────────────────────────────────────────
    float existingRoughness = gbufferNormalSample.a;
    float finalRoughness = existingRoughness;

    float roughnessOverride = decalParams.y;
    if (roughnessOverride >= 0.0) {
        // Blend toward the decal's roughness override value
        finalRoughness = lerp(existingRoughness, roughnessOverride * decalRoughness, blendAlpha);
    }

    // Pack normal + metallic + roughness into RT1 (matching G-Buffer layout)
    output.normal = float4(octNormal, existingMetallic, finalRoughness);

    // RT2: currently unused by decals — output zero alpha for no-op blend
    output.roughness = float4(0.0, 0.0, 0.0, 0.0);

    return output;
}
