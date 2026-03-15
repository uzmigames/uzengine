// height_fog_ps.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kHeightFogPS[] = R"hlsl(
/// Height fog pixel shader — exponential height fog via alpha blending.
///
/// Reads scene depth from the G-Buffer, reconstructs world position,
/// computes exponential height fog density along the view ray, and
/// outputs fog color with fog opacity as alpha. The PSO uses SrcAlpha /
/// InvSrcAlpha blending to composite fog over the existing backbuffer.
///
/// # Inputs
/// - PerFrameCB at b1: camera, invViewProj
/// - HeightFogCB at b2: fog parameters
/// - Depth texture (SRV at t0 / bindless)
///
/// # Output
/// float4(fogColor.rgb, fogFactor) — alpha-blended over the backbuffer by the PSO.

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
struct FogIndices {
    uint depthIdx;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};
ConstantBuffer<FogIndices> fogIndices : register(b0);
#endif

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
    float    _framePad[10];
};

cbuffer HeightFogCB : register(b2) {
    float4 fogColor;
    float  fogDensity;
    float  fogHeightFalloff;
    float  fogBaseHeight;
    float  fogMaxOpacity;
    float  fogStartDistance;
};

#ifndef UZ_BINDLESS
Texture2D<float> sceneDepth : register(t0);
#endif

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from screen UV and hardware depth.
float3 ReconstructWorldPosition(float2 uv, float depth) {
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

/// Computes exponential height fog factor.
///
/// Integrates fog density along the view ray from camera to world position.
///
/// # Returns
/// Fog factor in [0, maxOpacity]. 0 = no fog, maxOpacity = maximum fog.
float ComputeHeightFog(float3 worldPos) {
    float3 camPos = cameraPos.xyz;
    float dist = length(worldPos - camPos);

    // No fog before start distance
    if (dist < fogStartDistance)
        return 0.0f;

    float effectiveDistance = dist - fogStartDistance;

    // Height-based density
    float camHeight  = camPos.y - fogBaseHeight;
    float fragHeight = worldPos.y - fogBaseHeight;
    float heightDiff = fragHeight - camHeight;
    float fogAmount;

    if (abs(fogHeightFalloff * heightDiff) > 0.001f) {
        // Analytical integration of exp(-falloff * h) along the ray
        float expCam  = exp(-fogHeightFalloff * camHeight);
        float expFrag = exp(-fogHeightFalloff * fragHeight);
        fogAmount = fogDensity * effectiveDistance * (expCam - expFrag) /
                    (fogHeightFalloff * heightDiff);
    } else {
        // Nearly horizontal ray — use density at camera height
        fogAmount = fogDensity * effectiveDistance * exp(-fogHeightFalloff * camHeight);
    }

    float fog = 1.0f - exp(-max(fogAmount, 0.0f));
    return min(fog, fogMaxOpacity);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── Read depth ──
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[fogIndices.depthIdx];
    float depth = depthTex.Sample(pointSampler, uv).r;
#else
    float depth = sceneDepth.Sample(pointSampler, uv).r;
#endif

    // Skip sky pixels — fog does not affect the sky
    if (depth >= 0.9999f)
        discard;

    // ── Reconstruct world position ──
    float3 worldPos = ReconstructWorldPosition(uv, depth);

    // ── Compute fog factor ──
    float fog = ComputeHeightFog(worldPos);

    // Discard if no fog to avoid unnecessary blending
    if (fog < 0.001f)
        discard;

    // Output fog color with fog opacity as alpha.
    // The PSO uses SrcAlpha/InvSrcAlpha blending to composite over the scene.
    return float4(fogColor.xyz, fog);
}
)hlsl";
} // namespace uz::generated
