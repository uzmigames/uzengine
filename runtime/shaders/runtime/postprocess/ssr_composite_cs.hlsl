/// UzEngine SSR Post-TAA Composite — applies SSR reflections after TAA resolve.
/// @ref AMD FidelityFX SSSR — denoised SSR composited after temporal pipeline
///
/// By applying SSR AFTER TAA, the TAA neighborhood sampling never sees
/// SSR-modified pixels, eliminating cross-surface noise contamination.
/// TAA operates on IBL-only specular, producing a clean temporal result.
/// SSR is then blended onto the clean TAA output.

#include "../common/encoding.hlsli"

cbuffer SsrCompositeRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   ssrSrvIdx;        // 0 — denoised SSR (spatial denoise output)
    uint   normalSrvIdx;     // 1 — GBuffer normals + roughness
    uint   depthSrvIdx;      // 2 — depth buffer
#else
    uint   _srvPad0;
    uint   _srvPad1;
    uint   _srvPad2;
#endif
    uint   reserved;         // 3
    uint   outputWidth;      // 4
    uint   outputHeight;     // 5
    float  intensity;        // 6
    float  _pad;             // 7
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

RWTexture2D<float4> outputColor : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float4> t_ssr    : register(t0);
Texture2D<float4> t_normal : register(t1);
Texture2D<float>  t_depth  : register(t2);
#endif

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

#ifdef UZ_BINDLESS
    Texture2D<float4> ssrTex    = ResourceDescriptorHeap[ssrSrvIdx];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[normalSrvIdx];
#else
    Texture2D<float4> ssrTex    = t_ssr;
    Texture2D<float4> normalTex = t_normal;
#endif

    float4 ssrData = ssrTex.Load(int3(DTid.xy, 0));

    // Early out: no SSR data
    if (ssrData.a <= 0.01) return;

    // Read roughness for smooth fade
    float4 normalData = normalTex.Load(int3(DTid.xy, 0));
    float  roughness  = max(normalData.a, 0.04);
    float  metallic   = normalData.b;

    // Smooth roughness fade — same as what was in deferred lighting
    float ssrRoughnessMask = 1.0 - smoothstep(0.15, 0.45, roughness);
    float ssrAlpha = ssrData.a * ssrRoughnessMask;

    if (ssrAlpha <= 0.01) return;

    // Squared confidence for smooth edge transition
    float ssrWeight = ssrAlpha * ssrAlpha;

    // Read current TAA-resolved color
    float4 taaColor = outputColor[DTid.xy];

    // Simple additive blend: SSR reflection adds to the existing specular.
    // The SSR color already contains the hit radiance; we scale by confidence.
    float3 result = taaColor.rgb + ssrData.rgb * ssrWeight * intensity;

    outputColor[DTid.xy] = float4(result, taaColor.a);
}
