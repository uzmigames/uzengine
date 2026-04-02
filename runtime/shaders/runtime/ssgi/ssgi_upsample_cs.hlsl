/// UzEngine SSGI Bilateral Upsample — Half-Res to Full-Res
///
/// @ref Tomasi & Manduchi 1998 "Bilateral Filtering for Gray and Color Images"
///
/// Joint bilateral upsample from half-res SSGI to full-res using full-res
/// depth and normals as guide signals. Each full-res pixel samples the 4
/// nearest half-res texels, weighted by depth and normal similarity.
///
/// Root constants at b0 (8 DWORDs):
///   [0] ssgiHalfSrvIndex     — half-res temporal output SRV
///   [1] ssgiAOHalfSrvIndex   — half-res temporal AO output SRV
///   [2] depthFullSrvIndex    — full-res depth buffer SRV
///   [3] normalFullSrvIndex   — full-res world-normal SRV
///   [4] outputUavIndex       — full-res result UAV
///   [5] aoOutputUavIndex     — full-res AO result UAV
///   [6] fullWidth
///   [7] fullHeight

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_ssgiHalfSrvIndex;
    uint g_ssgiAOHalfSrvIndex;
    uint g_depthFullSrvIndex;
    uint g_normalFullSrvIndex;
    uint g_outputUavIndex;
    uint g_aoOutputUavIndex;
#endif
    uint g_fullWidth;
    uint g_fullHeight;
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

/// Decodes a unit normal from RG16F octahedral encoding.
float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0) ? -t : t;
    return normalize(n);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_fullWidth || DTid.y >= g_fullHeight) return;

    int2 fullPx = int2(DTid.xy);
    uint halfWidth  = g_fullWidth / 2;
    uint halfHeight = g_fullHeight / 2;

#ifdef UZ_BINDLESS
    Texture2D<float4>   ssgiHalf   = ResourceDescriptorHeap[g_ssgiHalfSrvIndex];
    Texture2D<float>    aoHalf     = ResourceDescriptorHeap[g_ssgiAOHalfSrvIndex];
    Texture2D<float>    depthTex   = ResourceDescriptorHeap[g_depthFullSrvIndex];
    Texture2D<float4>   normalTex  = ResourceDescriptorHeap[g_normalFullSrvIndex];
    RWTexture2D<float4> outputTex  = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float>  aoOutput   = ResourceDescriptorHeap[g_aoOutputUavIndex];
#endif

    // Full-res depth and normal at this pixel
    float centerDepth = depthTex.Load(int3(fullPx, 0));

    // Sky: output zero (no indirect light in sky)
    if (IsSkyDepth(centerDepth)) {
        outputTex[fullPx] = float4(0, 0, 0, 0);
        aoOutput[fullPx]  = 1.0;
        return;
    }

    float centerLinZ    = LinearizeDepthReversedZ(centerDepth, nearPlane, farPlane);
    float3 centerNormal = OctDecode(normalTex.Load(int3(fullPx, 0)).rg);

    // Map full-res pixel to half-res neighborhood (2x2 bilinear)
    float2 halfUV   = (float2(fullPx) + 0.5) * 0.5;
    int2   halfBase  = int2(floor(halfUV - 0.5));
    float2 frac2     = halfUV - 0.5 - float2(halfBase);

    // Sample 4 nearest half-res texels with bilateral weights
    float3 colorSum = float3(0, 0, 0);
    float  aoSum    = 0.0;
    float  wSum     = 0.0;

    for (int ky = 0; ky <= 1; ++ky) {
        for (int kx = 0; kx <= 1; ++kx) {
            int2 hp = clamp(halfBase + int2(kx, ky), int2(0, 0),
                            int2(halfWidth - 1, halfHeight - 1));

            float3 sampleColor = ssgiHalf.Load(int3(hp, 0)).rgb;
            float  sampleAO    = aoHalf.Load(int3(hp, 0));

            // Get depth/normal at the corresponding full-res position
            int2 sp = hp * 2;
            float sampleDepth = depthTex.Load(int3(sp, 0));
            float sampleLinZ  = LinearizeDepthReversedZ(sampleDepth, nearPlane, farPlane);
            float3 sampleNorm = OctDecode(normalTex.Load(int3(sp, 0)).rg);

            // Bilinear weight
            float bx = (kx == 0) ? (1.0 - frac2.x) : frac2.x;
            float by = (ky == 0) ? (1.0 - frac2.y) : frac2.y;
            float wBilinear = bx * by;

            // Depth weight (exponential falloff)
            float wDepth = exp(-abs(centerLinZ - sampleLinZ) /
                               (0.5 * centerLinZ + 1e-6));

            // Normal weight (cosine power)
            float wNormal = pow(max(dot(centerNormal, sampleNorm), 0.0), 32.0);

            float w = wBilinear * wDepth * wNormal;

            colorSum += sampleColor * w;
            aoSum    += sampleAO * w;
            wSum     += w;
        }
    }

    float3 result   = (wSum > 0.0) ? colorSum / wSum : float3(0, 0, 0);
    float  resultAO = (wSum > 0.0) ? aoSum / wSum : 1.0;

    outputTex[fullPx] = float4(result, 1.0);
    aoOutput[fullPx]  = resultAO;
}
