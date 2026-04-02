/// UzEngine SVGF Filter Moments — Spatial Variance Filter
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
///
/// Pass 2 of the SVGF pipeline. Applies a 3x3 cross-bilateral filter
/// on the variance/moments texture using depth + normal as guide signals.
/// This stabilizes the per-pixel variance estimate before it is used as
/// edge-stopping weights in the subsequent à-trous passes.
///
/// Root constants at b0 (8 DWORDs):
///   [0] momentsSrv      — input moments (RG16F: μ, σ²)
///   [1] depthSrv        — full-res depth
///   [2] normalSrv       — full-res normals (RG16F octahedral)
///   [3] outputMomentsUav — filtered moments output (RG16F)
///   [4-5] unused
///   [6] texWidth
///   [7] texHeight

#include "../common/constants.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_momentsSrv;
    uint g_depthSrv;
    uint g_normalSrv;
    uint g_outputMomentsUav;
    uint _pad0;
    uint _pad1;
#endif
    uint g_texWidth;
    uint g_texHeight;
};

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float nearPlane, farPlane;
};

/// Decodes octahedral normal
float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0) ? -t : t;
    return normalize(n);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_texWidth || DTid.y >= g_texHeight) return;

    int2 pixel = int2(DTid.xy);

#ifdef UZ_BINDLESS
    Texture2D<float2>   momentsTex = ResourceDescriptorHeap[g_momentsSrv];
    Texture2D<float>    depthTex   = ResourceDescriptorHeap[g_depthSrv];
    Texture2D<float4>   normalTex  = ResourceDescriptorHeap[g_normalSrv];
    RWTexture2D<float2> outputTex  = ResourceDescriptorHeap[g_outputMomentsUav];
#endif

    float centerDepth = depthTex.Load(int3(pixel, 0));
    if (IsSkyDepth(centerDepth)) {
        outputTex[pixel] = momentsTex.Load(int3(pixel, 0));
        return;
    }

    float  centerLinZ   = LinearizeDepthReversedZ(centerDepth, nearPlane, farPlane);
    float3 centerNormal = OctDecode(normalTex.Load(int3(pixel, 0)).rg);

    float2 momSum = float2(0, 0);
    float  wSum   = 0.0;

    /// @ref Schied 2017 — 3x3 bilateral filter on moments
    for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
            int2 np = clamp(pixel + int2(kx, ky), int2(0, 0),
                            int2(g_texWidth - 1, g_texHeight - 1));

            float sDepth = depthTex.Load(int3(np, 0));
            float sLinZ  = LinearizeDepthReversedZ(sDepth, nearPlane, farPlane);
            float3 sNorm = OctDecode(normalTex.Load(int3(np, 0)).rg);
            float2 sMom  = momentsTex.Load(int3(np, 0));

            float wDepth  = exp(-abs(centerLinZ - sLinZ) / (centerLinZ + 1e-6));
            float wNormal = pow(max(dot(centerNormal, sNorm), 0.0), 32.0);
            float w = wDepth * wNormal;

            momSum += sMom * w;
            wSum   += w;
        }
    }

    outputTex[pixel] = (wSum > 0.0) ? momSum / wSum : momentsTex.Load(int3(pixel, 0));
}
