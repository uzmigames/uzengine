/// UzEngine SSR Resolve — edge-aware bilateral spatial filter.
/// Denoises stochastic SSR trace output using a depth/normal-aware
/// bilateral kernel that widens with roughness. Smooth surfaces get
/// minimal filtering; rough surfaces get a wider kernel to reduce
/// single-sample noise from GGX importance sampling.
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections"
/// @ref Schied 2017 "SVGF" — bilateral edge-stopping functions

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

    // Read center pixel normal + roughness + depth for edge-stopping
    float4 normalData = normalTex.Load(int3(DTid.xy, 0));
    float3 centerN    = OctDecode(normalData.rg);
    float  roughness  = max(normalData.a, 0.04);
    float  centerZ    = deviceZ;

    // Roughness gate — if this pixel's roughness exceeds the trace pass threshold,
    // output zero immediately. Without this, the Poisson disk spatial filter can
    // bleed SSR data from neighboring mirror-surface pixels onto rough surfaces
    // at silhouette boundaries, causing persistent stipple noise.
    float rFade = saturate(roughness * roughnessMaskScale + 2.0);
    if (rFade <= 0.0) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 traceResult = ssrTraceTex.Load(int3(DTid.xy, 0));

    // Perfect mirrors (roughness < 0.04): pass through directly — no spatial blur
    // @ref Karis 2013 — only truly specular surfaces skip filtering
    if (roughness < 0.04) {
        outputSSR[DTid.xy] = traceResult;
        return;
    }

    // Bilateral filter with roughness-driven kernel radius
    // @ref Schied 2017 "SVGF" — edge-stopping weights: depth + normal
    // Wider kernel for rough surfaces — roughness 0.5 gets radius ~6px
    float kernelRadius = lerp(1.0, 8.0, saturate((roughness - 0.04) / 0.46));

    // @ref Karis 2013 "Real Shading in Unreal Engine 4" — inverse Reinhard tonemap
    // Compress HDR values before accumulation to prevent bright fireflies from
    // dominating the kernel. Apply inverse after normalization.
    // Karis weight: rcp(1 + Luminance(color))
    float centerLum = dot(traceResult.rgb, float3(0.2126, 0.7152, 0.0722));
    float centerKarisW = rcp(1.0 + centerLum);
    float3 colorSum  = traceResult.rgb * centerKarisW;
    float  alphaSum  = traceResult.a;
    float  weightSum = centerKarisW;

    // 12-tap Poisson disk (better coverage than cross/grid at same cost)
    // @ref Jimenez 2016 — screen-space sampling patterns
    static const float2 kPoisson[12] = {
        float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696,  0.457),
        float2(-0.203,  0.621), float2( 0.962, -0.195), float2( 0.473, -0.480),
        float2( 0.519,  0.767), float2( 0.185, -0.893), float2( 0.507,  0.064),
        float2(-0.321, -0.932), float2(-0.797, -0.580), float2( 0.114,  0.370)
    };

    [unroll]
    for (uint i = 0; i < 12; i++) {
        float2 sampleUV = uv + kPoisson[i] * texelSize * kernelRadius;

        // Bounds check
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

        int2 samplePx = int2(sampleUV * float2(outputWidth, outputHeight));
        float4 sampleSSR  = ssrTraceTex.Load(int3(samplePx, 0));
        float4 sampleNorm = normalTex.Load(int3(samplePx, 0));
        float  sampleZ    = depthTex.SampleLevel(linearClamp, sampleUV, 0).r;

        // Edge-stopping weight: normal similarity
        float3 sN = OctDecode(sampleNorm.rg);
        float wNormal = pow(max(dot(centerN, sN), 0.0), 32.0);

        // Edge-stopping weight: depth similarity (relative)
        float depthDiff = abs(centerZ - sampleZ) / max(centerZ, 1e-6);
        float wDepth = exp(-depthDiff * 100.0);

        // Spatial Gaussian weight
        float dist2 = dot(kPoisson[i], kPoisson[i]);
        float wSpatial = exp(-dist2 * 2.0);

        float w = wNormal * wDepth * wSpatial;

        // Karis weight: compress HDR before accumulation
        float sampleLum = dot(sampleSSR.rgb, float3(0.2126, 0.7152, 0.0722));
        float karisW = rcp(1.0 + sampleLum);
        colorSum  += sampleSSR.rgb * karisW * w;
        alphaSum  += sampleSSR.a * w;
        weightSum += karisW * w;
    }

    float3 color = colorSum / max(weightSum, 0.001);
    float  alpha = alphaSum / max(weightSum, 0.001);

    // Inverse Karis: decompress HDR after normalization
    // @ref Karis 2013 — rcp(1 - Luminance(tonemapped))
    float finalLum = dot(color, float3(0.2126, 0.7152, 0.0722));
    color *= rcp(max(1.0 - finalLum, 0.001));

    outputSSR[DTid.xy] = float4(color, alpha);
}
