/// Previous Frame Color Reduction — Downsample HDR scene color to half-res mip 0.
///
/// Takes the full-resolution previous frame HDR scene color and produces a
/// half-resolution (mip 0) output with Karis luminance weighting for firefly
/// suppression. Each output texel is the weighted average of the corresponding
/// 2x2 block from the full-res input.
///
/// This is the first stage of the SSGI pipeline — the reduced color pyramid
/// provides the radiance source for screen-space ray hit shading.
///
/// # Resources
/// - t0: Previous frame full-res HDR scene color (R11G11B10F or RGBA16F)
/// - u0: Output half-res mip 0 (R11G11B10F or RGBA16F)
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB
///
/// @source UE5 SSRTPrevFrameReduction.usf:1-130 — ReduceCS
/// @source UE5 ScreenSpaceRayTracing.cpp:745-800 — SetupPrevSceneColorReduction
/// @constants Karis weight = rcp(1 + Luminance(color)) (SSRTPrevFrameReduction.usf:56)
///            Luminance = (0.2126, 0.7152, 0.0722) (Common.ush:405)
/// @deviations NONE — single mip 0 generation only; additional mips deferred to
///   future single-pass downsampler. UE5 generates 5 mips via SPD in one dispatch;
///   we generate mip 0 here (sufficient for initial SSGI trace with SampleLevel).

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ScreenSpaceRayTracing.cpp:745-800 — PrevSceneColorReduction setup
cbuffer PrevColorReduceRootCB : register(b0) {
    uint  prevColorSrvIndex;   // DWORD 0 — full-res prev scene color SRV (bindless)
    uint  outputMip0UavIndex;  // DWORD 1 — half-res mip 0 UAV (bindless)
    uint  outputMip1UavIndex;  // DWORD 2 — reserved for future mip 1 UAV
    uint  outputMip2UavIndex;  // DWORD 3 — reserved for future mip 2 UAV
    uint  outputMip3UavIndex;  // DWORD 4 — reserved for future mip 3 UAV
    uint  outputMip4UavIndex;  // DWORD 5 — reserved for future mip 4 UAV
    uint  fullWidth;           // DWORD 6 — full-res width
    uint  fullHeight;          // DWORD 7 — full-res height
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame CB (b1)
// ─────────────────────────────────────────────────────────────────────────────

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
    float    _pad_pf[14];
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: textures accessed via ResourceDescriptorHeap[]
#else
Texture2D<float4> prevColorTex : register(t0);
#endif

#ifndef UZ_BINDLESS
RWTexture2D<float4> outputMip0 : register(u0);
#endif

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Luminance & Karis Weight
// ─────────────────────────────────────────────────────────────────────────────

/// Rec. 709 luminance.
/// @source UE5 Common.ush:405 — Luminance(float3 LinearColor)
/// @constants (0.2126, 0.7152, 0.0722)
/// @deviations NONE
float Luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

/// Karis luminance weight for firefly suppression.
/// Weights bright pixels less to prevent single bright texels from
/// dominating the downsampled result.
/// @source UE5 SSRTPrevFrameReduction.usf:56 — Weight = rcp(1 + Luminance)
/// @constants rcp(1 + L)
/// @deviations NONE
float KarisWeight(float3 color) {
    return rcp(1.0f + Luminance(color));
}

/// Sanitize HDR color — clamp NaN/negative to zero.
/// @source UE5 SSRTRayCast.ush:733 — OutColor.rgb = -min(-OutColor.rgb, 0.0)
/// @deviations NONE
float3 SanitizeColor(float3 c) {
    return -min(-c, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Sample previous frame scene color at given UV.
float3 SamplePrevColor(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[prevColorSrvIndex];
    return SanitizeColor(tex.SampleLevel(linearSampler, uv, 0).rgb);
#else
    return SanitizeColor(prevColorTex.SampleLevel(linearSampler, uv, 0).rgb);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader — Mip 0 (half-res from full-res)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SSRTPrevFrameReduction.usf:40-90 — ReduceCS
/// Each thread produces one half-res texel by averaging the 2x2 block from
/// full-res with Karis luminance weighting for firefly suppression.
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    // Half-res dimensions
    uint halfWidth  = (fullWidth  + 1) / 2;
    uint halfHeight = (fullHeight + 1) / 2;

    // Bounds check — dispatch covers half-res output
    if (dispatchThreadId.x >= halfWidth || dispatchThreadId.y >= halfHeight) {
        return;
    }

    // Compute UV coordinates for the center of each of the 4 full-res texels
    // in the 2x2 block corresponding to this half-res texel.
    // @source UE5 SSRTPrevFrameReduction.usf:48-55 — 2x2 gather
    float2 invFullSize = 1.0f / float2((float)fullWidth, (float)fullHeight);

    // Base full-res pixel coordinate (top-left of the 2x2 block)
    uint2 basePixel = dispatchThreadId.xy * 2;

    // Sample all 4 texels with pixel-center offsets (+0.5)
    float2 uv00 = ((float2)basePixel + float2(0.5f, 0.5f)) * invFullSize;
    float2 uv10 = ((float2)basePixel + float2(1.5f, 0.5f)) * invFullSize;
    float2 uv01 = ((float2)basePixel + float2(0.5f, 1.5f)) * invFullSize;
    float2 uv11 = ((float2)basePixel + float2(1.5f, 1.5f)) * invFullSize;

    float3 c00 = SamplePrevColor(uv00);
    float3 c10 = SamplePrevColor(uv10);
    float3 c01 = SamplePrevColor(uv01);
    float3 c11 = SamplePrevColor(uv11);

    // Karis-weighted average — suppresses fireflies from bright HDR pixels.
    // @source UE5 SSRTPrevFrameReduction.usf:56-64
    // weight_i = 1 / (1 + Luminance(color_i))
    // result = sum(color_i * weight_i) / sum(weight_i)
    float w00 = KarisWeight(c00);
    float w10 = KarisWeight(c10);
    float w01 = KarisWeight(c01);
    float w11 = KarisWeight(c11);

    float totalWeight = w00 + w10 + w01 + w11;
    float invTotalWeight = (totalWeight > 0.0f) ? rcp(totalWeight) : 0.0f;

    float3 result = (c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11) * invTotalWeight;

    // Write half-res mip 0
#ifdef UZ_BINDLESS
    RWTexture2D<float4> outMip0 = ResourceDescriptorHeap[outputMip0UavIndex];
    outMip0[dispatchThreadId.xy] = float4(result, 1.0f);
#else
    outputMip0[dispatchThreadId.xy] = float4(result, 1.0f);
#endif
}
