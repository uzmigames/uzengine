/// DBuffer decal pixel shader — projects world position into decal space,
/// samples decal textures, and outputs to three DBuffer render targets.
///
/// # DBuffer Layout
/// - RT0 (DBuffer A): RGBA8Unorm — decal albedo (RGB) + opacity (A)
/// - RT1 (DBuffer B): RGBA8Unorm — decal normal encoded [0,1] (RGB) + normal blend (A)
/// - RT2 (DBuffer C): RGBA8Unorm — roughness (R) + metallic (G) + AO (B) + material blend (A)
///
/// # Projection
/// 1. Reconstruct world position from GBuffer depth + inverse viewProj
/// 2. Transform world position into decal local space via inverse decal matrix
/// 3. Clip: if outside [0,1]^3 in local space, discard (not inside decal volume)
/// 4. Use local XY as UV to sample decal textures
/// 5. Apply edge fade and angle fade
/// 6. Output to DBuffer with alpha blending
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decals
/// @ref Pesce 2014 "Rendering Decals in Screen Space"

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame CB at b1.
/// Only invViewProj and screen dimensions are needed for world pos reconstruction.
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

/// Per-object CB at b2 — decal parameters (must match decal_vs.hlsl layout).
cbuffer DecalObjectCB : register(b2) {
    row_major float4x4 decalWorld;
    row_major float4x4 decalInvWorld;
    float    decalOpacity;
    float    decalNormalBlend;
    float    decalRoughness;
    float    decalMetallic;
    float    decalFadeDistance;
    float    decalAngleFade;
    uint     decalAlbedoSrv;
    uint     decalNormalSrv;
    uint     decalMaterialSrv;
    uint     decalDepthSrv;
    uint     decalNormalGbufSrv;
    float    _pad0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Samplers & Textures
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float>  decalDepthTexture     : register(t0);
Texture2D<float4> decalNormalGbufTexture: register(t1);
Texture2D<float4> decalAlbedoTexture    : register(t2);
Texture2D<float4> decalNormalTexture    : register(t3);
Texture2D<float4> decalMaterialTexture  : register(t4);
#endif

SamplerState linearClamp : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Input / Output
// ─────────────────────────────────────────────────────────────────────────────

struct DecalPSInput {
    float4 positionCS : SV_Position;
    float4 screenPos  : TEXCOORD0;
};

/// Three-target output for the DBuffer.
struct DecalPSOutput {
    float4 dbufferA : SV_Target0;   ///< Albedo (RGB) + opacity (A)
    float4 dbufferB : SV_Target1;   ///< Normal encoded [0,1] (RGB) + normal blend (A)
    float4 dbufferC : SV_Target2;   ///< Roughness (R) + metallic (G) + AO (B) + blend (A)
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Reconstruct world position from depth
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from screen-space position and depth buffer.
///
/// @ref Reversed-Z depth buffer: near=1.0, far=0.0.
/// The depth texture is sampled at the pixel's screen UV.
float3 ReconstructWorldPos(float2 screenUV, float depth) {
    // Reversed-Z: depth=1 is near plane, depth=0 is far plane.
    // Build NDC position: xy in [-1,1], z = depth (already in NDC for reversed-Z).
    float2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y;  // D3D UV convention: y flipped from NDC

    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Edge fade
// ─────────────────────────────────────────────────────────────────────────────

/// Computes a smooth fade factor based on distance from the decal volume boundary.
/// Returns 1.0 at the center, fading to 0.0 at the edges within fadeDistance.
float ComputeEdgeFade(float3 localPos, float fadeNorm) {
    if (fadeNorm <= 0.0) return 1.0;

    // Distance from each face of the unit cube [0,1]^3
    float3 distFromMin = localPos;
    float3 distFromMax = 1.0 - localPos;
    float minDist = min(min(distFromMin.x, distFromMin.y),
                    min(min(distFromMin.z, distFromMax.x),
                    min(distFromMax.y, distFromMax.z)));

    return saturate(minDist / fadeNorm);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

DecalPSOutput main(DecalPSInput input) {
    DecalPSOutput output;

    // ── Screen UV from SV_Position ───────────────────────────────────────
    // SV_Position gives the pixel center in screen space.
    // We need to convert to [0,1] UV using the viewport dimensions.
    // The viewport dimensions are passed implicitly — we use the
    // perspective divide of the screenPos interpolant from the VS.
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV = screenUV * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;

    // ── Sample depth buffer ──────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[decalDepthSrv];
    float depth = depthTex.SampleLevel(linearClamp, screenUV, 0);
#else
    float depth = decalDepthTexture.SampleLevel(linearClamp, screenUV, 0);
#endif

    // Skip sky pixels (reversed-Z: depth=0 is far plane/sky)
    if (depth <= 0.0) discard;

    // ── Reconstruct world position ───────────────────────────────────────
    float3 worldPos = ReconstructWorldPos(screenUV, depth);

    // ── Project into decal local space ───────────────────────────────────
    float4 localPos4 = mul(float4(worldPos, 1.0), decalInvWorld);
    float3 localPos = localPos4.xyz;

    // Clip: decal volume is [0,1]^3 in local space
    // (centered at origin in the VS cube, but the inverse matrix maps to [0,1])
    // Actually the unit cube is [-0.5, 0.5], so after inv transform the result
    // is in [-0.5, 0.5]. We remap to [0,1] for UV sampling.
    float3 uvw = localPos + 0.5;

    // Discard pixels outside the decal volume
    if (any(uvw < 0.0) || any(uvw > 1.0)) discard;

    // Decal UV = local XY (projects along Z axis)
    float2 decalUV = uvw.xy;

    // ── Angle fade ───────────────────────────────────────────────────────
    float angleFade = 1.0;
    if (decalAngleFade < 90.0
#ifdef UZ_BINDLESS
        && decalNormalGbufSrv != 0xFFFFFFFF
#endif
    ) {
#ifdef UZ_BINDLESS
        Texture2D<float4> normalGbufTex = ResourceDescriptorHeap[decalNormalGbufSrv];
        float4 normalSample = normalGbufTex.SampleLevel(linearClamp, screenUV, 0);
#else
        float4 normalSample = decalNormalGbufTexture.SampleLevel(linearClamp, screenUV, 0);
#endif

        // Decode octahedral normal from GBuffer (RG channels, [0,1] range)
        float2 octEnc = normalSample.rg * 2.0 - 1.0;
        float z = 1.0 - abs(octEnc.x) - abs(octEnc.y);
        float t = saturate(-z);
        float3 surfNormal = normalize(float3(
            octEnc.x + (octEnc.x >= 0.0 ? -t : t),
            octEnc.y + (octEnc.y >= 0.0 ? -t : t),
            z));

        // Decal projects along its local Z axis
        float3 decalDir = normalize(float3(decalWorld[2].xyz));

        float cosAngle = abs(dot(surfNormal, decalDir));
        float cosThreshold = cos(decalAngleFade * PI / 180.0);
        angleFade = saturate((cosAngle - cosThreshold) / max(1.0 - cosThreshold, 0.001));
    }

    // ── Edge fade ────────────────────────────────────────────────────────
    // fadeDistance is in cm; convert to [0,1] range relative to decal extents.
    // The decal world matrix encodes extents in its scale, so the local space
    // is already normalized to [-0.5, 0.5]. fadeDistance / averageExtent gives
    // the normalized fade width.
    float avgExtent = max(length(decalWorld[0].xyz) + length(decalWorld[1].xyz), 0.001) * 0.5;
    float fadeNorm = decalFadeDistance / avgExtent;
    float edgeFade = ComputeEdgeFade(uvw, fadeNorm);

    float totalFade = decalOpacity * angleFade * edgeFade;

    // Early out if fully transparent
    if (totalFade <= 0.001) discard;

    // ── Sample decal textures ────────────────────────────────────────────
    float4 albedo = float4(0.0, 0.0, 0.0, 0.0);
    float albedoBlend = 0.0;
#ifdef UZ_BINDLESS
    if (decalAlbedoSrv != 0xFFFFFFFF) {
        Texture2D<float4> albedoTex = ResourceDescriptorHeap[decalAlbedoSrv];
        albedo = albedoTex.SampleLevel(linearClamp, decalUV, 0);
        albedoBlend = albedo.a * totalFade;
    }
#else
    {
        albedo = decalAlbedoTexture.SampleLevel(linearClamp, decalUV, 0);
        albedoBlend = albedo.a * totalFade;
    }
#endif

    float normalBlendFinal = 0.0;
    float3 decalNormal = float3(0.5, 0.5, 1.0);  // Neutral normal (no modification)
#ifdef UZ_BINDLESS
    if (decalNormalSrv != 0xFFFFFFFF) {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[decalNormalSrv];
        float4 normalSample = normalTex.SampleLevel(linearClamp, decalUV, 0);
        decalNormal = normalSample.rgb;
        normalBlendFinal = decalNormalBlend * totalFade;
    }
#else
    {
        float4 normalSample = decalNormalTexture.SampleLevel(linearClamp, decalUV, 0);
        decalNormal = normalSample.rgb;
        normalBlendFinal = decalNormalBlend * totalFade;
    }
#endif

    float materialBlend = 0.0;
    float3 materialValues = float3(decalRoughness, decalMetallic, 1.0);
#ifdef UZ_BINDLESS
    if (decalMaterialSrv != 0xFFFFFFFF) {
        Texture2D<float4> matTex = ResourceDescriptorHeap[decalMaterialSrv];
        float4 matSample = matTex.SampleLevel(linearClamp, decalUV, 0);
        materialValues = float3(
            matSample.r * decalRoughness,
            matSample.g * decalMetallic,
            matSample.b);  // AO
        materialBlend = totalFade;
    } else if (totalFade > 0.001) {
        // No material texture but decal has roughness/metallic overrides
        materialBlend = totalFade;
    }
#else
    {
        float4 matSample = decalMaterialTexture.SampleLevel(linearClamp, decalUV, 0);
        materialValues = float3(
            matSample.r * decalRoughness,
            matSample.g * decalMetallic,
            matSample.b);  // AO
        materialBlend = totalFade;
    }
#endif

    // ── Write DBuffer outputs ────────────────────────────────────────────
    output.dbufferA = float4(albedo.rgb, albedoBlend);
    output.dbufferB = float4(decalNormal, normalBlendFinal);
    output.dbufferC = float4(materialValues, materialBlend);

    return output;
}
