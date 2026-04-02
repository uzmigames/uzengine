/// UzEngine SVGF À-Trous Wavelet Filter
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
/// @ref Dammertz et al. 2010 "Edge-Avoiding À-Trous Wavelet Transform"
///
/// Pass 3-7 of the SVGF pipeline (5 iterations with doubling step size).
/// Each iteration applies a 5x5 cross-bilateral filter with edge-stopping
/// weights on depth, normal, and luminance variance.
///
/// Step sizes: 1, 2, 4, 8, 16 (covers 33-pixel radius over 5 passes).
///
/// Root constants at b0 (8 DWORDs):
///   [0] inputColorSrv    — input color from previous pass
///   [1] momentsSrv       — filtered moments (μ, σ²) for variance-guided weights
///   [2] depthSrv         — full-res depth
///   [3] normalSrv        — full-res normals (RG16F octahedral)
///   [4] outputColorUav   — filtered color output
///   [5] stepSize         — à-trous step size (1, 2, 4, 8, 16)
///   [6] texWidth
///   [7] texHeight

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_inputColorSrv;
    uint g_momentsSrv;
    uint g_depthSrv;
    uint g_normalSrv;
    uint g_outputColorUav;
#endif
    uint g_stepSize;
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

/// @ref ITU-R BT.709
float Luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

/// Decodes octahedral normal
float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0) ? -t : t;
    return normalize(n);
}

/// @ref Dammertz 2010 — 5x5 à-trous wavelet kernel weights (B3 spline)
/// @constants h[5] = {1/16, 1/4, 3/8, 1/4, 1/16}
static const float kKernel[3] = { 1.0, 2.0/3.0, 1.0/6.0 };

/// @ref Schied 2017 — edge-stopping parameters
/// @constants sigmaDepth=1.0, sigmaNormal=128.0, sigmaLuminance=4.0
static const float kSigmaDepth     = 1.0;
static const float kSigmaNormal    = 128.0;
static const float kSigmaLuminance = 4.0;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_texWidth || DTid.y >= g_texHeight) return;

    int2 pixel = int2(DTid.xy);
    int step = int(g_stepSize);

#ifdef UZ_BINDLESS
    Texture2D<float4>   inputTex  = ResourceDescriptorHeap[g_inputColorSrv];
    Texture2D<float2>   momTex    = ResourceDescriptorHeap[g_momentsSrv];
    Texture2D<float>    depthTex  = ResourceDescriptorHeap[g_depthSrv];
    Texture2D<float4>   normalTex = ResourceDescriptorHeap[g_normalSrv];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_outputColorUav];
#endif

    float4 centerColor = inputTex.Load(int3(pixel, 0));
    float  centerDepth = depthTex.Load(int3(pixel, 0));

    if (IsSkyDepth(centerDepth)) {
        outputTex[pixel] = centerColor;
        return;
    }

    float  centerLinZ   = LinearizeDepthReversedZ(centerDepth, nearPlane, farPlane);
    float3 centerNormal = OctDecode(normalTex.Load(int3(pixel, 0)).rg);
    float  centerLum    = Luminance(centerColor.rgb);

    // Variance from moments texture for luminance edge-stopping
    float2 moments = momTex.Load(int3(pixel, 0));
    float variance = max(moments.y - moments.x * moments.x, 0.0);
    float lumStdDev = sqrt(variance);

    // 5x5 à-trous filter (simplified to 3x3 with step offset for efficiency)
    float3 colorSum = float3(0, 0, 0);
    float  wSum = 0.0;

    for (int ky = -2; ky <= 2; ++ky) {
        for (int kx = -2; kx <= 2; ++kx) {
            int2 np = pixel + int2(kx, ky) * step;
            np = clamp(np, int2(0, 0), int2(g_texWidth - 1, g_texHeight - 1));

            float4 sampleColor = inputTex.Load(int3(np, 0));
            float  sampleDepth = depthTex.Load(int3(np, 0));
            float  sampleLinZ  = LinearizeDepthReversedZ(sampleDepth, nearPlane, farPlane);
            float3 sampleNorm  = OctDecode(normalTex.Load(int3(np, 0)).rg);
            float  sampleLum   = Luminance(sampleColor.rgb);

            // Spatial kernel weight (B3 spline approximation)
            float kernelW = kKernel[abs(kx)] * kKernel[abs(ky)];

            // Depth edge-stopping
            float wDepth = exp(-abs(centerLinZ - sampleLinZ) /
                               (kSigmaDepth * centerLinZ + 1e-6));

            // Normal edge-stopping
            float wNormal = pow(max(dot(centerNormal, sampleNorm), 0.0), kSigmaNormal);

            // Luminance edge-stopping (variance-guided)
            float lumDiff = abs(centerLum - sampleLum);
            float sigmaL  = kSigmaLuminance * lumStdDev + 1e-6;
            float wLum    = exp(-lumDiff / sigmaL);

            float w = kernelW * wDepth * wNormal * wLum;

            colorSum += sampleColor.rgb * w;
            wSum += w;
        }
    }

    float3 result = (wSum > 0.0) ? colorSum / wSum : centerColor.rgb;
    outputTex[pixel] = float4(result, centerColor.a);
}
