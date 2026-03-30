/// Previous Frame Color Reduction — Downsample HDR scene color to half-res mip chain (mips 0-3).
///
/// Takes the full-resolution previous frame HDR scene color and produces a
/// 4-level mip chain (mips 0-3) in a single dispatch using cooperative
/// downsample via groupshared memory (LDS). Each 8x8 thread group generates
/// one 8x8 tile of mip 0, then cooperatively reduces to 4x4 (mip 1),
/// 2x2 (mip 2), and 1x1 (mip 3) within the same dispatch.
///
/// All levels use Karis luminance weighting for firefly suppression.
///
/// This is the first stage of the SSGI pipeline — the reduced color pyramid
/// provides the radiance source for screen-space ray hit shading.
///
/// # Resources
/// - t0: Previous frame full-res HDR scene color (R11G11B10F or RGBA16F)
/// - u0: Output half-res mip 0 (R11G11B10F or RGBA16F)
/// - u1: Output quarter-res mip 1
/// - u2: Output eighth-res mip 2
/// - u3: Output sixteenth-res mip 3
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB
///
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" — previous frame reduction
/// @ref Karis 2014 — luminance weighting for firefly suppression during downsample
/// @ref Pettineo 2016 "A Closer Look At GPUs" — single-pass mip generation via LDS
/// @ref Wihlidal 2016 "Optimizing the Graphics Pipeline with Compute" GDC — cooperative downsample
/// @constants Karis weight = rcp(1 + Luminance(color))
///            Luminance = (0.2126, 0.7152, 0.0722) per ITU-R BT.709
/// @deviations NONE

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Previous frame color reduction dispatch parameters.
cbuffer PrevColorReduceRootCB : register(b0) {
    uint  prevColorSrvIndex;   // DWORD 0 — full-res prev scene color SRV (bindless)
    uint  outputMip0UavIndex;  // DWORD 1 — half-res mip 0 UAV (bindless)
    uint  outputMip1UavIndex;  // DWORD 2 — quarter-res mip 1 UAV (bindless)
    uint  outputMip2UavIndex;  // DWORD 3 — eighth-res mip 2 UAV (bindless)
    uint  outputMip3UavIndex;  // DWORD 4 — sixteenth-res mip 3 UAV (bindless)
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
RWTexture2D<float4> outputMip1 : register(u1);
RWTexture2D<float4> outputMip2 : register(u2);
RWTexture2D<float4> outputMip3 : register(u3);
#endif

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Luminance & Karis Weight
// ─────────────────────────────────────────────────────────────────────────────

/// Rec. 709 luminance.
/// @ref ITU-R BT.709 — standard luminance coefficients (0.2126, 0.7152, 0.0722)
float Luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

/// Karis luminance weight for firefly suppression.
/// Weights bright pixels less to prevent single bright texels from
/// dominating the downsampled result.
/// @ref Karis 2014 — Weight = rcp(1 + Luminance)
float KarisWeight(float3 color) {
    return rcp(1.0f + Luminance(color));
}

/// Sanitize HDR color — clamp NaN/negative to zero (standard NaN-safe clamp pattern).
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
// Groupshared Memory for Cooperative Mip Chain
// ─────────────────────────────────────────────────────────────────────────────

/// LDS tile for cooperative downsample within 8x8 thread group.
/// Each thread stores its mip 0 result, then subsets of threads read 2x2
/// neighborhoods to produce subsequent mip levels.
/// @ref Wihlidal 2016 "Optimizing the Graphics Pipeline with Compute" GDC
groupshared float3 gs_color[8][8];

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader — Mip 0-3 (single-pass cooperative downsample)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Stachowiak 2015 — previous frame color reduction
/// @ref Wihlidal 2016 — single-pass mip generation via LDS cooperative downsample
/// Each thread produces one half-res texel by averaging the 2x2 block from
/// full-res with Karis luminance weighting for firefly suppression. Then the
/// group cooperatively reduces mips 1-3 using groupshared memory.
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID,
          uint3 groupThreadId    : SV_GroupThreadID,
          uint3 groupId          : SV_GroupID) {
    // Half-res dimensions
    uint halfWidth  = (fullWidth  + 1) / 2;
    uint halfHeight = (fullHeight + 1) / 2;

    // Bounds check — cannot early-return because all threads in the group must
    // participate in GroupMemoryBarrierWithGroupSync() calls below.
    bool inBounds = (dispatchThreadId.x < halfWidth && dispatchThreadId.y < halfHeight);

    float3 result = float3(0.0f, 0.0f, 0.0f);

    if (inBounds) {
        // Compute UV coordinates for the center of each of the 4 full-res texels
        // in the 2x2 block corresponding to this half-res texel.
        // 2x2 gather from full-res to half-res
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
        // weight_i = 1 / (1 + Luminance(color_i))
        // result = sum(color_i * weight_i) / sum(weight_i)
        float w00 = KarisWeight(c00);
        float w10 = KarisWeight(c10);
        float w01 = KarisWeight(c01);
        float w11 = KarisWeight(c11);

        float totalWeight = w00 + w10 + w01 + w11;
        float invTotalWeight = (totalWeight > 0.0f) ? rcp(totalWeight) : 0.0f;

        result = (c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11) * invTotalWeight;

        // Write half-res mip 0
#ifdef UZ_BINDLESS
        RWTexture2D<float4> outMip0 = ResourceDescriptorHeap[outputMip0UavIndex];
        outMip0[dispatchThreadId.xy] = float4(result, 1.0f);
#else
        outputMip0[dispatchThreadId.xy] = float4(result, 1.0f);
#endif
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Store mip 0 result into groupshared memory for cooperative downsample.
    // Out-of-bounds threads store black (zero Karis weight contribution).
    // All threads must participate in barriers — no early return allowed.
    // ─────────────────────────────────────────────────────────────────────────
    gs_color[groupThreadId.y][groupThreadId.x] = result;

    GroupMemoryBarrierWithGroupSync();

    // ─────────────────────────────────────────────────────────────────────────
    // Mip 1 — quarter-res (4x4 threads active per group)
    // Each of the first 4x4 threads reads a 2x2 neighborhood from LDS,
    // applies Karis-weighted average, writes mip 1, stores back to LDS.
    // @ref Wihlidal 2016 — cooperative downsample pattern
    // ─────────────────────────────────────────────────────────────────────────
    if (groupThreadId.x < 4 && groupThreadId.y < 4) {
        uint2 base = groupThreadId.xy * 2;
        float3 m1_00 = gs_color[base.y    ][base.x    ];
        float3 m1_10 = gs_color[base.y    ][base.x + 1];
        float3 m1_01 = gs_color[base.y + 1][base.x    ];
        float3 m1_11 = gs_color[base.y + 1][base.x + 1];

        float mw00 = KarisWeight(m1_00);
        float mw10 = KarisWeight(m1_10);
        float mw01 = KarisWeight(m1_01);
        float mw11 = KarisWeight(m1_11);

        float mipTotalWeight = mw00 + mw10 + mw01 + mw11;
        float mipInvWeight = (mipTotalWeight > 0.0f) ? rcp(mipTotalWeight) : 0.0f;

        float3 mip1Result = (m1_00 * mw00 + m1_10 * mw10 + m1_01 * mw01 + m1_11 * mw11) * mipInvWeight;

        // Write mip 1 output — coordinate is group tile origin (in mip 1 space) + local offset
        uint2 mip1Coord = groupId.xy * 4 + groupThreadId.xy;
#ifdef UZ_BINDLESS
        RWTexture2D<float4> outMip1 = ResourceDescriptorHeap[outputMip1UavIndex];
        outMip1[mip1Coord] = float4(mip1Result, 1.0f);
#else
        outputMip1[mip1Coord] = float4(mip1Result, 1.0f);
#endif

        // Store mip 1 result back into LDS for next reduction level
        gs_color[groupThreadId.y][groupThreadId.x] = mip1Result;
    }

    GroupMemoryBarrierWithGroupSync();

    // ─────────────────────────────────────────────────────────────────────────
    // Mip 2 — eighth-res (2x2 threads active per group)
    // @ref Wihlidal 2016 — cooperative downsample pattern
    // ─────────────────────────────────────────────────────────────────────────
    if (groupThreadId.x < 2 && groupThreadId.y < 2) {
        uint2 base2 = groupThreadId.xy * 2;
        float3 m2_00 = gs_color[base2.y    ][base2.x    ];
        float3 m2_10 = gs_color[base2.y    ][base2.x + 1];
        float3 m2_01 = gs_color[base2.y + 1][base2.x    ];
        float3 m2_11 = gs_color[base2.y + 1][base2.x + 1];

        float m2w00 = KarisWeight(m2_00);
        float m2w10 = KarisWeight(m2_10);
        float m2w01 = KarisWeight(m2_01);
        float m2w11 = KarisWeight(m2_11);

        float mip2TotalWeight = m2w00 + m2w10 + m2w01 + m2w11;
        float mip2InvWeight = (mip2TotalWeight > 0.0f) ? rcp(mip2TotalWeight) : 0.0f;

        float3 mip2Result = (m2_00 * m2w00 + m2_10 * m2w10 + m2_01 * m2w01 + m2_11 * m2w11) * mip2InvWeight;

        // Write mip 2 output
        uint2 mip2Coord = groupId.xy * 2 + groupThreadId.xy;
#ifdef UZ_BINDLESS
        RWTexture2D<float4> outMip2 = ResourceDescriptorHeap[outputMip2UavIndex];
        outMip2[mip2Coord] = float4(mip2Result, 1.0f);
#else
        outputMip2[mip2Coord] = float4(mip2Result, 1.0f);
#endif

        // Store mip 2 result back into LDS for final reduction
        gs_color[groupThreadId.y][groupThreadId.x] = mip2Result;
    }

    GroupMemoryBarrierWithGroupSync();

    // ─────────────────────────────────────────────────────────────────────────
    // Mip 3 — sixteenth-res (thread 0 only)
    // @ref Wihlidal 2016 — cooperative downsample pattern
    // ─────────────────────────────────────────────────────────────────────────
    if (groupThreadId.x == 0 && groupThreadId.y == 0) {
        float3 m3_00 = gs_color[0][0];
        float3 m3_10 = gs_color[0][1];
        float3 m3_01 = gs_color[1][0];
        float3 m3_11 = gs_color[1][1];

        float m3w00 = KarisWeight(m3_00);
        float m3w10 = KarisWeight(m3_10);
        float m3w01 = KarisWeight(m3_01);
        float m3w11 = KarisWeight(m3_11);

        float mip3TotalWeight = m3w00 + m3w10 + m3w01 + m3w11;
        float mip3InvWeight = (mip3TotalWeight > 0.0f) ? rcp(mip3TotalWeight) : 0.0f;

        float3 mip3Result = (m3_00 * m3w00 + m3_10 * m3w10 + m3_01 * m3w01 + m3_11 * m3w11) * mip3InvWeight;

        // Write mip 3 output — one texel per group
        uint2 mip3Coord = groupId.xy;
#ifdef UZ_BINDLESS
        RWTexture2D<float4> outMip3 = ResourceDescriptorHeap[outputMip3UavIndex];
        outMip3[mip3Coord] = float4(mip3Result, 1.0f);
#else
        outputMip3[mip3Coord] = float4(mip3Result, 1.0f);
#endif
    }
}
