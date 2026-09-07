/// UzEngine SSR Resolve — roughness-adaptive elliptical bilateral spatial filter.
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" — spatial filter
/// @ref Schied 2017 "SVGF" — bilateral edge-stopping functions
/// @ref AMD 2020 "FidelityFX SSSR" — PREFILTER_NORMAL_SIGMA=512, PREFILTER_DEPTH_SIGMA=4
///
/// Denoises stochastic SSR trace output. The kernel is an ellipse aligned to
/// the projected reflection direction — stretched along the lobe major axis
/// for rough surfaces, circular for smooth surfaces. This matches the GGX
/// specular lobe shape projected to screen space.

#include "../common/encoding.hlsli"

cbuffer SsrResolveRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   ssrTraceSrvIdx;     // 0
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

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
};

RWTexture2D<float4> outputSSR : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float4> t_ssrTrace : register(t0);
Texture2D<float4> t_normal   : register(t1);
Texture2D<float>  t_depth    : register(t2);
#endif
SamplerState linearClamp : register(s0);

/// Reconstruct world position from UV + device-Z.
float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos  = float4(ndc, deviceZ, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

/// Project world position to screen UV.
float2 ProjectToUV(float3 worldPos) {
    float4 clipPos = mul(float4(worldPos, 1.0), viewProj);
    clipPos.xy /= clipPos.w;
    float2 uv = clipPos.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

#ifdef UZ_BINDLESS
    Texture2D<float4> ssrTraceTex = ResourceDescriptorHeap[ssrTraceSrvIdx];
    Texture2D<float4> normalTex   = ResourceDescriptorHeap[normalSrvIdx];
    Texture2D<float>  depthTex    = ResourceDescriptorHeap[depthSrvIdx];
#else
    Texture2D<float4> ssrTraceTex = t_ssrTrace;
    Texture2D<float4> normalTex   = t_normal;
    Texture2D<float>  depthTex    = t_depth;
#endif

    float deviceZ = depthTex.SampleLevel(linearClamp, uv, 0).r;
    if (deviceZ <= 0.0001) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 normalData = normalTex.Load(int3(DTid.xy, 0));
    float3 centerN    = OctDecode(normalData.rg);
    float  roughness  = max(normalData.a, 0.04);
    float  centerZ    = deviceZ;

    // Roughness gate
    float rFade = saturate(roughness * roughnessMaskScale + 2.0);
    if (rFade <= 0.0) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 traceResult = ssrTraceTex.Load(int3(DTid.xy, 0));

    // Perfect mirrors: pass through — no spatial blur
    if (roughness <= 0.04) {
        outputSSR[DTid.xy] = traceResult;
        return;
    }

    // ── Roughness-adaptive elliptical kernel ────────────────────────────
    // @ref Stachowiak 2015 — kernel aligned to projected specular lobe.
    // The GGX lobe projects as an ellipse on screen, stretched along the
    // reflection direction. We compute a 2D anisotropic basis:
    //   major axis = screen-space reflection direction (scaled by roughness)
    //   minor axis = perpendicular (scaled less)

    // Compute screen-space reflection direction for ellipse orientation
    float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    float3 V = normalize(cameraPos.xyz - worldPos);
    float3 R = reflect(-V, centerN);

    // Project a point along R to screen to get the screen-space direction
    float3 offsetWorld = worldPos + R * 10.0; // small offset along R
    float2 offsetUV = ProjectToUV(offsetWorld);
    float2 screenReflDir = offsetUV - uv;
    float screenReflLen = length(screenReflDir);

    // Ellipse axes: major along reflection, minor perpendicular
    // Aspect ratio: rough surfaces → more elongated (2:1), smooth → circular (1:1)
    float aspectRatio = lerp(1.0, 2.5, saturate((roughness - 0.04) / 0.46));
    float kernelRadius = lerp(1.0, 6.0, saturate((roughness - 0.04) / 0.46));

    float2 majorAxis, minorAxis;
    if (screenReflLen > 1e-4) {
        majorAxis = normalize(screenReflDir);
        minorAxis = float2(-majorAxis.y, majorAxis.x);
    } else {
        majorAxis = float2(1, 0);
        minorAxis = float2(0, 1);
    }

    // Karis HDR weight for center pixel
    float centerLum = dot(traceResult.rgb, float3(0.2126, 0.7152, 0.0722));
    float centerKarisW = rcp(1.0 + centerLum);
    float3 colorSum  = traceResult.rgb * centerKarisW;
    float  alphaSum  = traceResult.a * centerKarisW;
    float  weightSum = centerKarisW;

    // 15-sample Halton(2,3) pattern in [-1,1] — matches AMD SSSR DNSR_Prefilter
    // @ref AMD FidelityFX SSSR — 15 Halton samples + center
    static const float2 kHalton[15] = {
        float2( 0.0,  1.0), float2(-0.5,  0.333), float2( 0.5, -0.333),
        float2(-0.75, 0.667), float2( 0.25, -0.667), float2(-0.25, -0.111),
        float2( 0.75, 0.111), float2(-0.875, 0.778), float2( 0.125, -0.778),
        float2(-0.375, 0.444), float2( 0.625, -0.444), float2(-0.625, -0.222),
        float2( 0.375, 0.222), float2(-0.125, 0.889), float2( 0.875, -0.889)
    };

    [unroll]
    for (uint i = 0; i < 15; i++) {
        // Transform Halton sample through elliptical kernel
        // Major axis scaled by aspectRatio, minor axis by 1
        float2 offset = kHalton[i].x * majorAxis * aspectRatio
                      + kHalton[i].y * minorAxis;
        float2 sampleUV = uv + offset * texelSize * kernelRadius;

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

        int2 samplePx = int2(sampleUV * float2(outputWidth, outputHeight));
        float4 sampleSSR  = ssrTraceTex.Load(int3(samplePx, 0));
        float4 sampleNorm = normalTex.Load(int3(samplePx, 0));
        float  sampleZ    = depthTex.SampleLevel(linearClamp, sampleUV, 0).r;

        // Edge-stopping: normal similarity
        // @ref AMD FidelityFX SSSR — PREFILTER_NORMAL_SIGMA = 512
        float3 sN = OctDecode(sampleNorm.rg);
        float wNormal = pow(max(dot(centerN, sN), 0.0), 512.0);

        // Edge-stopping: depth similarity (relative, center-depth scaled)
        // @ref AMD FidelityFX SSSR — PREFILTER_DEPTH_SIGMA = 4.0
        float depthDiff = abs(centerZ - sampleZ) / max(centerZ, 1e-6);
        float wDepth = exp(-depthDiff * centerZ * 4.0);

        // Spatial Gaussian weight on the Halton pattern
        float dist2 = dot(kHalton[i], kHalton[i]);
        float wSpatial = exp(-dist2 * 1.5);

        float w = wNormal * wDepth * wSpatial;

        // Karis weight for HDR firefly suppression
        float sampleLum = dot(sampleSSR.rgb, float3(0.2126, 0.7152, 0.0722));
        float karisW = rcp(1.0 + sampleLum);
        colorSum  += sampleSSR.rgb * karisW * w;
        alphaSum  += sampleSSR.a * karisW * w;
        weightSum += karisW * w;
    }

    float3 color = colorSum / max(weightSum, 0.001);
    float  alpha = alphaSum / max(weightSum, 0.001);

    // The normalized weighted average already returns linear HDR radiance.
    // Applying inverse compression again would amplify constant input light.
    outputSSR[DTid.xy] = float4(color, alpha);
}
