#include "nrd_sdk_common.hlsli"

/// Modulate-illumination pass after the NVIDIA NRD SDK dispatches:
///   result = diffuseDenoised * albedo + specularDenoised * F0 + residual
/// where residual = raw - diffuse - specular keeps primary emission, the
/// background and hair untouched (the stage-2 contract of nrd_atrous_cs.hlsl).
/// REBLUR returns YCoCg radiance (REBLUR_BackEnd_UnpackRadianceAndNormHitDist),
/// RELAX returns linear radiance (RELAX_BackEnd_UnpackRadiance).
/// debugSignal mirrors r.NRD.DebugSignal: 1 raw, 2 residual, 3 specular, 4 diffuse,
/// 5 denoised diffuse remodulated, 6 denoised specular remodulated (so 5/6 are
/// directly comparable with the raw 4/3 channels in expectation).
/// @constants 8 = tile; 1e-3 = remodulation floor (nrd_atrous_cs.hlsl)
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= width || tid.y >= height) return;
    int2 p = int2(tid.xy);
    Texture2D<float4> raw = ResourceDescriptorHeap[rawSrv];
    Texture2D<float4> diffuse = ResourceDescriptorHeap[diffuseSrv];
    Texture2D<float4> albedo = ResourceDescriptorHeap[albedoSrv];
    Texture2D<float4> specular = ResourceDescriptorHeap[specularSrv];
    Texture2D<float4> modulation = ResourceDescriptorHeap[modulationSrv];
    Texture2D<float4> guides = ResourceDescriptorHeap[guideSrv];
    Texture2D<float4> diffuseOutput = ResourceDescriptorHeap[diffuseOutputSrv];
    Texture2D<float4> specularOutput = ResourceDescriptorHeap[specularOutputSrv];
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUav];

    float3 rawRadiance = raw.Load(int3(p, 0)).rgb;
    float3 d = diffuse.Load(int3(p, 0)).rgb;
    float3 s = specular.Load(int3(p, 0)).rgb;
    float3 residual = rawRadiance - d - s;
    float3 diffDenoised = NrdSdkYCoCgToLinear(diffuseOutput.Load(int3(p, 0)).rgb)
        * max(albedo.Load(int3(p, 0)).rgb, 1e-3);
    float3 specDenoised = specularOutput.Load(int3(p, 0)).rgb
        * max(modulation.Load(int3(p, 0)).rgb, 1e-3);
    float3 result = rawRadiance;
    if (guides.Load(int3(p, 0)).z > 0.0) result = diffDenoised + specDenoised + residual;
    if (debugSignal == 1u) result = rawRadiance;
    if (debugSignal == 2u) result = residual;
    if (debugSignal == 3u) result = s;
    if (debugSignal == 4u) result = d;
    if (debugSignal == 5u) result = diffDenoised;
    if (debugSignal == 6u) result = specDenoised;
    output[p] = float4(max(result, 0.0), 1.0);
}
