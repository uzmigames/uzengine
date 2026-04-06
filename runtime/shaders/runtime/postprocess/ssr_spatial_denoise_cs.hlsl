/// UzEngine SSR Spatial Bilateral Denoise — post-temporal cleanup filter.
/// @ref AMD FidelityFX SSSR "DNSR_Prefilter" — bilateral spatial denoise
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" — spatial filter
/// @ref Schied 2017 "SVGF" — edge-stopping bilateral weights
///
/// Runs AFTER the SSR temporal accumulation pass and BEFORE the deferred
/// lighting composite. Removes residual per-pixel noise from the temporally
/// accumulated SSR output using depth + normal edge-stopping weights.
/// This prevents SSR noise from entering the HDR scene color buffer where
/// TAA's 3x3 neighborhood sampling would spread it to adjacent surfaces.

#include "../common/encoding.hlsli"

cbuffer SsrDenoiseRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   ssrInputSrvIdx;     // 0 — temporal output to denoise
    uint   normalSrvIdx;       // 1
    uint   depthSrvIdx;        // 2
#else
    uint   _srvPad0;           // 0
    uint   _srvPad1;           // 1
    uint   _srvPad2;           // 2
#endif
    uint   reserved;           // 3
    uint   outputWidth;        // 4
    uint   outputHeight;       // 5
    float  intensity;          // 6
    float  roughnessMaskScale; // 7
};

RWTexture2D<float4> outputSSR : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float4> t_ssrInput : register(t0);
Texture2D<float4> t_normal   : register(t1);
Texture2D<float>  t_depth    : register(t2);
#endif
SamplerState pointClamp : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

#ifdef UZ_BINDLESS
    Texture2D<float4> ssrTex   = ResourceDescriptorHeap[ssrInputSrvIdx];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[normalSrvIdx];
    Texture2D<float>  depthTex  = ResourceDescriptorHeap[depthSrvIdx];
#else
    Texture2D<float4> ssrTex   = t_ssrInput;
    Texture2D<float4> normalTex = t_normal;
    Texture2D<float>  depthTex  = t_depth;
#endif

    float4 centerSSR = ssrTex.Load(int3(DTid.xy, 0));

    // Early out: no SSR data → pass through zero
    if (centerSSR.a <= 0.001) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Read center pixel properties for edge-stopping
    float4 normalData = normalTex.Load(int3(DTid.xy, 0));
    float3 centerN    = OctDecode(normalData.rg);
    float  roughness  = max(normalData.a, 0.04);
    float  centerZ    = depthTex.Load(int3(DTid.xy, 0)).r;

    // Perfect mirrors: skip spatial denoise (preserve sharp reflections)
    if (roughness < 0.04) {
        outputSSR[DTid.xy] = centerSSR;
        return;
    }

    // @ref AMD SSSR DNSR_Prefilter — 15 samples in 7x7, bilateral weights
    // Kernel radius scales with roughness: smooth = tight, rough = wide
    float kernelRadius = lerp(1.0, 3.0, saturate(roughness / 0.35));

    float3 colorSum  = centerSSR.rgb;
    float  alphaSum  = centerSSR.a;
    float  weightSum = 1.0;

    // 3x3 cross + diagonal pattern (9 samples) for post-temporal cleanup
    // This is lighter than the resolve pass's 12-tap Poisson because the
    // temporal pass has already reduced most of the noise.
    static const int2 kOffsets[8] = {
        int2(-1, -1), int2( 0, -1), int2( 1, -1),
        int2(-1,  0),               int2( 1,  0),
        int2(-1,  1), int2( 0,  1), int2( 1,  1)
    };

    [unroll]
    for (uint i = 0; i < 8; i++) {
        int2 samplePx = int2(DTid.xy) + kOffsets[i] * int(kernelRadius);

        // Bounds check
        if (samplePx.x < 0 || samplePx.y < 0 ||
            uint(samplePx.x) >= outputWidth || uint(samplePx.y) >= outputHeight)
            continue;

        float4 sampleSSR  = ssrTex.Load(int3(samplePx, 0));

        // Skip zero-alpha neighbors (no SSR data)
        if (sampleSSR.a <= 0.001) continue;

        float4 sampleNorm = normalTex.Load(int3(samplePx, 0));
        float  sampleZ    = depthTex.Load(int3(samplePx, 0)).r;

        // Edge-stopping: normal similarity
        // @ref Schied 2017 SVGF — sigma_n = 128
        float3 sN = OctDecode(sampleNorm.rg);
        float wNormal = pow(max(dot(centerN, sN), 0.0), 64.0);

        // Edge-stopping: depth similarity (relative)
        // @ref Schied 2017 SVGF — sigma_z = 1.0 (normalized)
        float depthDiff = abs(centerZ - sampleZ) / max(centerZ, 1e-6);
        float wDepth = exp(-depthDiff * 500.0);

        // Spatial Gaussian weight
        float dist2 = float(kOffsets[i].x * kOffsets[i].x + kOffsets[i].y * kOffsets[i].y);
        float wSpatial = exp(-dist2 * 0.5);

        float w = wNormal * wDepth * wSpatial;

        colorSum  += sampleSSR.rgb * w;
        alphaSum  += sampleSSR.a * w;
        weightSum += w;
    }

    float3 color = colorSum / max(weightSum, 0.001);
    float  alpha = alphaSum / max(weightSum, 0.001);

    outputSSR[DTid.xy] = float4(color, alpha);
}
