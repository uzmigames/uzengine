#ifndef UZ_NRD_SDK_COMMON
#define UZ_NRD_SDK_COMMON
#include "../common/encoding.hlsli"
#include "nrd_sdk_math.hlsli"

// Root constants of the NVIDIA NRD SDK adapter (pack + modulate dispatches).
// The C++ twin is NrdSdkRoot in nrd_sdk_denoiser.h (checked by test:layouts).
// 56 DWORDs: fits the 64-DWORD root budget without a constant buffer.
cbuffer NrdSdkRoot : register(b0) {
    uint rawSrv, diffuseSrv, albedoSrv, specularSrv;
    uint modulationSrv, virtualPositionSrv, guideSrv, velocitySrv;
    uint motionUav, normalRoughnessUav, viewZUav, diffuseInputUav;
    uint specularInputUav, outputUav, width, height;
    uint diffuseOutputSrv, specularOutputSrv, debugSignal, sdkFlags;
    float hitDistA, hitDistB, hitDistC, skyViewZ;
    row_major float4x4 worldToView;
    row_major float4x4 inverseViewProj;
};

// Front-end encoders replicated from NRD.hlsli v4.17.3 (scalar math in
// nrd_sdk_math.hlsli). NRD.dll is built with NRD_NORMAL_ENCODING=4
// (RGBA16_SNORM, FP16 storage allowed) and NRD_ROUGHNESS_ENCODING=1 (linear);
// NrdSdkDenoiser::Init verifies both against nrd::GetLibraryDesc().
/// @ref NRD.hlsli NRD_FrontEnd_PackNormalAndRoughness (best-fit scale, NRD renormalizes)
float4 NrdSdkPackNormalRoughness(float3 N, float roughness) {
    N /= max(abs(N.x), max(abs(N.y), abs(N.z)));
    return float4(N, roughness);
}
/// @ref NRD.hlsli _NRD_LinearToYCoCg / _NRD_YCoCgToLinear (REBLUR radiance channels)
float3 NrdSdkLinearToYCoCg(float3 c) {
    return float3(NrdSdkLumaY(c.r, c.g, c.b), NrdSdkChromaCo(c.r, c.g, c.b), NrdSdkChromaCg(c.r, c.g, c.b));
}
float3 NrdSdkYCoCgToLinear(float3 c) {
    return float3(NrdSdkLinearR(c.x, c.y, c.z), NrdSdkLinearG(c.x, c.y, c.z), NrdSdkLinearB(c.x, c.y, c.z));
}
/// @ref NRD.hlsli REBLUR_FrontEnd_PackRadianceAndNormHitDist sanitize path (NRD_FP16_MAX)
float3 NrdSdkSanitize(float3 radiance) {
    bool invalid = any(isnan(radiance)) || any(isinf(radiance));
    return invalid ? float3(0.0, 0.0, 0.0) : clamp(radiance, 0.0, 65504.0);
}
#endif
