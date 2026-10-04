/// UzEngine IBL evaluation — SH diffuse + split-sum specular.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — split-sum approximation
/// @ref Ramamoorthi & Hanrahan 2001 — SH irradiance

#ifndef UZ_LIGHTING_IBL_HLSLI
#define UZ_LIGHTING_IBL_HLSLI

#include "../common/constants.hlsli"
#include "../material/brdf.hlsli"

/// Analytical DFG (environment BRDF) approximation — replaces a GPU-baked LUT.
///
/// Returns (A, B) such that the split-sum specular is `F0 * A + B` (or
/// `F0 * A + F90 * B` when the material carries an authored F90).
///
/// @ref Karis 2014 "Physically Based Shading on Mobile" — EnvBRDFApprox
/// @constants c0 = (-1, -0.0275, -0.572, 0.022), c1 = (1, 0.0425, 1.04, -0.04),
/// exponential slope -9.28 and the AB scale (-1.04, 1.04) are the published fit.
float2 EnvironmentBRDFApprox(float roughness, float NoV)
{
    float4 r = roughness * float4(-1.0, -0.0275, -0.572, 0.022)
             + float4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

/// Evaluate L2 spherical harmonics for irradiance.
///
/// Uses compact 7-float4 packing from sh_projection.cpp PackSHToCompactFormat().
/// This is the same evaluation as UE5's GetSkySHDiffuse() — the packing
/// pre-multiplies cosine convolution weights, so evaluation is just dot products.
///
/// @ref Sloan 2008 "Stupid Spherical Harmonics (SH) Tricks"
/// @ref UE5 ReflectionEnvironmentShared.ush — GetSkySHDiffuse()
float3 EvalSH_L2(float3 N, float4 shCoeffs[7])
{
    float4 NormalVector = float4(N, 1.0);

    // L0 + L1: dot(coeffs[channel], (N, 1))
    float3 result;
    result.r = dot(shCoeffs[0], NormalVector);
    result.g = dot(shCoeffs[1], NormalVector);
    result.b = dot(shCoeffs[2], NormalVector);

    // L2: dot(coeffs[channel], N.xyzz * N.yzzx)
    float4 vB = NormalVector.xyzz * NormalVector.yzzx;
    result.r += dot(shCoeffs[3], vB);
    result.g += dot(shCoeffs[4], vB);
    result.b += dot(shCoeffs[5], vB);

    // L2 last term: coeffs[6].xyz * (N.x^2 - N.y^2)
    float vC = N.x * N.x - N.y * N.y;
    result += shCoeffs[6].xyz * vC;

    return max(result, 0.0);
}

bool IsSHZero(float4 shCoeffs[7])
{
    return dot(abs(shCoeffs[0].xyz), 1.0) < 1e-6;
}

/// Specular occlusion from AO — prevents IBL specular light leaking in crevices.
/// @ref Lagarde 2014 "Moving Frostbite to PBR" — specular occlusion approximation
/// @ref Filament — specular occlusion model
///
/// At high roughness the specular lobe is wide and naturally occluded (similar
/// to diffuse), so the occlusion matches AO. At low roughness the lobe is
/// narrow and mainly sees the reflection direction, so occlusion is weaker.
/// The formula uses NdotV to account for grazing angles where the lobe grazes
/// the surface and is more likely to be occluded.
float ComputeSpecularOcclusion(float NdotV, float ao, float roughness)
{
    return saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
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
