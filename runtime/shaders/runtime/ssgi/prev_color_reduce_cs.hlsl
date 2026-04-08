/// UzEngine SSGI Previous Frame Color Reduction
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — weighted average
///
/// Downsamples the previous frame's full-res HDR scene color to half-res
/// using a Karis luminance-weighted average (4 samples per output texel).
/// This prevents bright fireflies from dominating the average during
/// SSGI trace hit lookups.
///
/// Root constants at b0 (8 DWORDs):
///   [0] prevColorSrvIndex   — full-res previous frame color SRV
///   [1] outputMip0UavIndex  — half-res output UAV
///   [2..5] reserved (mip 1-4 for future multi-mip)
///   [6] fullWidth           — full-res width
///   [7] fullHeight          — full-res height

#include "../common/constants.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_prevColorSrvIndex;
    uint g_outputMip0UavIndex;
    uint g_outputMip1UavIndex;
    uint g_outputMip2UavIndex;
    uint g_outputMip3UavIndex;
    uint g_outputMip4UavIndex;
#endif
    uint g_fullWidth;
    uint g_fullHeight;
};

/// @ref Karis 2014 — luminance weight to reduce fireflies
/// @constants w = 1 / (1 + luma), where luma = ITU-R BT.709
float KarisWeight(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint halfWidth  = g_fullWidth / 2;
    uint halfHeight = g_fullHeight / 2;
    if (DTid.x >= halfWidth || DTid.y >= halfHeight) return;

    uint2 outPixel = DTid.xy;
    uint2 basePixel = outPixel * 2;

#ifdef UZ_BINDLESS
    Texture2D<float4>   prevColor = ResourceDescriptorHeap[g_prevColorSrvIndex];
    RWTexture2D<float4> output    = ResourceDescriptorHeap[g_outputMip0UavIndex];
#endif

    // Sample 4 full-res texels
    float3 c00 = prevColor.Load(int3(basePixel + uint2(0, 0), 0)).rgb;
    float3 c10 = prevColor.Load(int3(basePixel + uint2(1, 0), 0)).rgb;
    float3 c01 = prevColor.Load(int3(basePixel + uint2(0, 1), 0)).rgb;
    float3 c11 = prevColor.Load(int3(basePixel + uint2(1, 1), 0)).rgb;

    // Karis luminance-weighted average (anti-firefly)
    float w00 = KarisWeight(c00);
    float w10 = KarisWeight(c10);
    float w01 = KarisWeight(c01);
    float w11 = KarisWeight(c11);

    float wSum = w00 + w10 + w01 + w11;
    float3 result = (c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11) / max(wSum, 1e-6);

    output[outPixel] = float4(result, 1.0);
}
