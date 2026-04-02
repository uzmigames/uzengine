/// UzEngine SSGI Spatial Denoise — Cross-Bilateral Filter
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering" (SVGF)
///
/// 3x3 cross-bilateral spatial filter with edge-stopping weights on:
/// - Depth discontinuity (exponential falloff)
/// - Normal divergence (cosine power)
/// - Luminance variance (prevents over-blurring high-frequency detail)
///
/// Processes half-res trace output → half-res denoised output.
///
/// Root constants at b0 (8 DWORDs):
///   [0] inputSrvIndex     — half-res trace indirect diffuse SRV
///   [1] aoInputSrvIndex   — half-res trace AO SRV
///   [2] depthSrvIndex     — full-res depth buffer SRV
///   [3] normalSrvIndex    — full-res world-normal SRV
///   [4] outputUavIndex    — half-res denoised indirect diffuse UAV
///   [5] aoOutputUavIndex  — half-res denoised AO UAV
///   [6] texWidth
///   [7] texHeight

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_inputSrvIndex;
    uint g_aoInputSrvIndex;
    uint g_depthSrvIndex;
    uint g_normalSrvIndex;
    uint g_outputUavIndex;
    uint g_aoOutputUavIndex;
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

/// @ref Schied 2017 — edge-stopping function parameters
/// @constants sigmaDepth=1.0, sigmaNormal=128.0, sigmaLuminance=4.0
static const float kSigmaDepth     = 1.0;
static const float kSigmaNormal    = 128.0;
static const float kSigmaLuminance = 4.0;

/// @ref ITU-R BT.709 luminance weights
float Luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

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
    if (DTid.x >= g_texWidth || DTid.y >= g_texHeight) return;

    int2 pixel = int2(DTid.xy);

#ifdef UZ_BINDLESS
    Texture2D<float4>   inputTex   = ResourceDescriptorHeap[g_inputSrvIndex];
    Texture2D<float>    aoInputTex = ResourceDescriptorHeap[g_aoInputSrvIndex];
    Texture2D<float>    depthTex   = ResourceDescriptorHeap[g_depthSrvIndex];
    Texture2D<float4>   normalTex  = ResourceDescriptorHeap[g_normalSrvIndex];
    RWTexture2D<float4> outputTex  = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float>  aoOutput   = ResourceDescriptorHeap[g_aoOutputUavIndex];
#endif

    // Center sample
    float4 centerColor = inputTex.Load(int3(pixel, 0));
    float  centerAO    = aoInputTex.Load(int3(pixel, 0));

    // Full-res depth and normal at center (2x mapping from half-res)
    int2 fullPx = pixel * 2;
    float centerDepth   = depthTex.Load(int3(fullPx, 0));
    float centerLinZ    = LinearizeDepthReversedZ(centerDepth, nearPlane, farPlane);
    float3 centerNormal = OctDecode(normalTex.Load(int3(fullPx, 0)).rg);
    float centerLum     = Luminance(centerColor.rgb);

    // Skip sky pixels
    if (IsSkyDepth(centerDepth)) {
        outputTex[pixel] = centerColor;
        aoOutput[pixel]  = centerAO;
        return;
    }

    // ── 3x3 cross-bilateral filter ────────────────────────────────────────
    float3 colorSum = float3(0, 0, 0);
    float  aoSum    = 0.0;
    float  wSum     = 0.0;

    // Compute local luminance variance for adaptive edge-stopping
    /// @ref Schied 2017 — luminance variance edge-stopping weight
    float lumMean  = 0.0;
    float lumMean2 = 0.0;
    float varCount = 0.0;

    // Pre-pass: compute luminance statistics in 3x3 neighborhood
    for (int vy = -1; vy <= 1; ++vy) {
        for (int vx = -1; vx <= 1; ++vx) {
            int2 np = clamp(pixel + int2(vx, vy), int2(0, 0),
                            int2(g_texWidth - 1, g_texHeight - 1));
            float lum = Luminance(inputTex.Load(int3(np, 0)).rgb);
            lumMean  += lum;
            lumMean2 += lum * lum;
            varCount += 1.0;
        }
    }
    lumMean  /= varCount;
    lumMean2 /= varCount;
    float lumVariance = max(lumMean2 - lumMean * lumMean, 0.0);
    float lumStdDev   = sqrt(lumVariance);

    // Main filter pass
    for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
            int2 np = clamp(pixel + int2(kx, ky), int2(0, 0),
                            int2(g_texWidth - 1, g_texHeight - 1));

            float4 sampleColor = inputTex.Load(int3(np, 0));
            float  sampleAO    = aoInputTex.Load(int3(np, 0));

            // Full-res depth and normal at neighbor
            int2 sFullPx = np * 2;
            float sampleDepth  = depthTex.Load(int3(sFullPx, 0));
            float sampleLinZ   = LinearizeDepthReversedZ(sampleDepth, nearPlane, farPlane);
            float3 sampleNorm  = OctDecode(normalTex.Load(int3(sFullPx, 0)).rg);
            float sampleLum    = Luminance(sampleColor.rgb);

            // Depth edge-stopping weight
            /// @ref Schied 2017 Eq. 3 — depth weight
            float wDepth = exp(-abs(centerLinZ - sampleLinZ) /
                               (kSigmaDepth * centerLinZ + 1e-6));

            // Normal edge-stopping weight
            /// @ref Schied 2017 Eq. 4 — normal weight
            float wNormal = pow(max(dot(centerNormal, sampleNorm), 0.0), kSigmaNormal);

            // Luminance edge-stopping weight (variance-guided)
            /// @ref Schied 2017 Eq. 5 — luminance weight with variance
            float lumDiff = abs(centerLum - sampleLum);
            float sigmaL  = kSigmaLuminance * lumStdDev + 1e-6;
            float wLum    = exp(-lumDiff / sigmaL);

            float w = wDepth * wNormal * wLum;

            colorSum += sampleColor.rgb * w;
            aoSum    += sampleAO * w;
            wSum     += w;
        }
    }

    float3 filteredColor = (wSum > 0.0) ? colorSum / wSum : centerColor.rgb;
    float  filteredAO    = (wSum > 0.0) ? aoSum / wSum : centerAO;

    outputTex[pixel] = float4(filteredColor, 1.0);
    aoOutput[pixel]  = filteredAO;
}
