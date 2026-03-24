/// HDRI skybox pixel shader — renders a cubemap as the sky background.
///
/// Used when an external HDRI environment map is active, replacing the
/// procedural sky atmosphere ray-march pass. Samples the HDRI cubemap
/// in the view direction reconstructed from screen UV + inverse view-projection.
///
/// Only fills pixels where depth == 0.0 (reversed-Z far plane = no geometry).
///
/// # Inputs
/// - PerFrameCB at b1: camera, invViewProj, exposure
/// - SkyboxIndices at b0 (bindless): cubemap SRV index, depth SRV index
/// - Depth texture (t0, non-bindless): to skip pixels with geometry
/// - Cubemap texture (t1, non-bindless): HDRI cubemap
///
/// # Output
/// Linear HDR color (pre-tonemap).

#include "../include/pbr_common.hlsli"

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
struct SkyboxIndices {
    uint depthIdx;
    uint cubemapIdx;
};
ConstantBuffer<SkyboxIndices> skyboxIndices : register(b0);
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
    uint     irradianceSrvIdx;
    uint     prefilteredSrvIdx;
    uint     brdfLutSrvIdx;
    uint     prefilteredMipLevels;
    float    skyLightIntensity;
    float    skyLightColorR;
    float    skyLightColorG;
    float    skyLightColorB;
    float    skyLightMinAmbient;
    float    exposure;
};

#ifndef UZ_BINDLESS
Texture2D<float>     sceneDepth : register(t0);
TextureCube<float4>  hdriCubemap : register(t1);
#endif

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── Read depth — skip pixels with geometry ──────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[skyboxIndices.depthIdx];
    float depth = depthTex.Sample(linearSampler, uv).r;
#else
    float depth = sceneDepth.Sample(linearSampler, uv).r;
#endif

    // Only render sky where there is no geometry.
    // Reversed-Z: far plane = 0.0, so sky pixels have depth near 0.
    if (depth > 0.0001f)
        discard;

    // ── Reconstruct world-space ray direction ────────────────────────────────
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y      = -ndc.y;

    // Unproject two points along the same screen ray at different NDC depths.
    // The camera translation cancels in the subtraction, giving a precise
    // ray direction even when cameraPos is thousands of units from the origin.
    float4 pt0    = mul(float4(ndc, 0.25f, 1.0f), invViewProj);
    float4 pt1    = mul(float4(ndc, 0.75f, 1.0f), invViewProj);
    float3 world0 = pt0.xyz / pt0.w;
    float3 world1 = pt1.xyz / pt1.w;
    float3 rayDir = normalize(world0 - world1);

    // ── Sample HDRI cubemap ──────────────────────────────────────────────────
    // The cubemap was built with Y-up DX convention (CubeFaceDirection in ibl.hlsli).
    // UzEngine world space is Z-up. Convert: DX cubemap (X,Y,Z) = world (X,-Z,Y).
    float3 cubemapDir = float3(rayDir.x, -rayDir.z, rayDir.y);

#ifdef UZ_BINDLESS
    TextureCube<float4> cubemap = ResourceDescriptorHeap[skyboxIndices.cubemapIdx];
    float3 color = cubemap.SampleLevel(linearSampler, cubemapDir, 0).rgb;
#else
    float3 color = hdriCubemap.SampleLevel(linearSampler, cubemapDir, 0).rgb;
#endif

    // Output linear HDR — tonemap applied in separate fullscreen pass.
    return float4(color, 1.0f);
}
