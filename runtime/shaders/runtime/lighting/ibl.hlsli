/// UzEngine IBL evaluation — SH diffuse + split-sum specular.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — split-sum approximation
/// @ref Ramamoorthi & Hanrahan 2001 — SH irradiance

#ifndef UZ_LIGHTING_IBL_HLSLI
#define UZ_LIGHTING_IBL_HLSLI

#include "../common/constants.hlsli"
#include "../material/brdf.hlsli"

/// Evaluate L2 spherical harmonics for irradiance.
/// Matches sh_projection.cpp compact 7-float4 packing.
float3 EvalSH_L2(float3 N, float4 shCoeffs[7])
{
    // L0
    float3 result = shCoeffs[0].xyz * 0.282095;
    // L1
    result += shCoeffs[1].xyz * 0.488603 * N.y;
    result += shCoeffs[2].xyz * 0.488603 * N.z;
    result += shCoeffs[3].xyz * 0.488603 * N.x;
    // L2
    result += shCoeffs[4].xyz * 1.092548 * (N.x * N.y);
    result += shCoeffs[5].xyz * 1.092548 * (N.y * N.z);
    result += shCoeffs[6].xyz * 0.315392 * (3.0 * N.z * N.z - 1.0);
    return max(result, 0.0);
}

bool IsSHZero(float4 shCoeffs[7])
{
    return dot(abs(shCoeffs[0].xyz), 1.0) < 1e-6;
}

/// Evaluate IBL: SH diffuse + split-sum specular
float3 EvalIBL(
    float3 N, float3 V, float3 f0, float3 diffuseColor, float roughness, float NdotV,
    float4 shCoeffs[7], float skyIntensity, float3 skyColor, float minAmbient,
    uint irradianceSrv, uint prefilteredSrv, uint brdfLutSrv, uint prefilteredMips,
    SamplerState samp)
{
    // Diffuse from SH
    float3 irradiance = EvalSH_L2(N, shCoeffs);

    // Cubemap fallback if SH is zero
#ifdef UZ_BINDLESS
    if (IsSHZero(shCoeffs) && irradianceSrv != kSentinelSrvIndex)
    {
        TextureCube<float4> irrMap = ResourceDescriptorHeap[irradianceSrv];
        irradiance = irrMap.SampleLevel(samp, N, 0).rgb;
    }
#endif

    // Min ambient floor
    irradiance = max(irradiance, minAmbient);

    float3 diffuse = diffuseColor * irradiance;

    // Specular: prefiltered env + BRDF LUT
    float3 R = reflect(-V, N);
    float mipLevel = roughness * float(prefilteredMips - 1);
    float3 prefilteredColor = float3(0, 0, 0);
    float2 brdfLut = float2(1, 0);

#ifdef UZ_BINDLESS
    if (prefilteredSrv != kSentinelSrvIndex)
    {
        TextureCube<float4> prefMap = ResourceDescriptorHeap[prefilteredSrv];
        prefilteredColor = prefMap.SampleLevel(samp, R, mipLevel).rgb;
    }
    if (brdfLutSrv != kSentinelSrvIndex)
    {
        Texture2D<float4> lutTex = ResourceDescriptorHeap[brdfLutSrv];
        brdfLut = lutTex.SampleLevel(samp, float2(NdotV, roughness), 0).rg;
    }
#endif

    float3 specular = prefilteredColor * (f0 * brdfLut.x + brdfLut.y);

    return (diffuse + specular) * skyIntensity * skyColor;
}

#endif
