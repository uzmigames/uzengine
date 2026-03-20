/// UzEngine SH Irradiance — L2 Spherical Harmonics evaluation for sky lighting.
///
/// Evaluates packed L2 SH coefficients at a surface normal to produce
/// diffuse irradiance. Standard GetSkySHDiffuse() exactly.
///
/// The 7-float4 coefficient array is produced by sh_projection.cpp on the CPU
/// and uploaded as part of the per-frame constant buffer.

#ifndef SH_IRRADIANCE_HLSLI
#define SH_IRRADIANCE_HLSLI

/// Evaluates L2 Spherical Harmonics irradiance at a surface normal.
///
/// The SH coefficients must be in reference implementations-compatible packed format (7 float4),
/// with diffuse convolution (cosine lobe) already baked in.
///
/// Reconstruction uses 3 dot products for L0+L1 (one per channel),
/// 3 dot products for L2 quadratic terms, and 1 multiply for Y22.
/// Total: 7 dot products + 1 multiply + 1 max = ~20 ALU ops.
///
/// # Arguments
/// * `Normal` — World-space surface normal (must be unit length).
/// * `SkyIrradianceMap` — 7 float4 SH coefficients from sh_projection.cpp.
///
/// # Returns
/// Diffuse irradiance in linear HDR space, clamped to non-negative.
///
/// # Packing Format
/// ```
/// [0..2] = per-channel (R,G,B) L0+L1 terms:
///          dot(coeffs[i], float4(nx, ny, nz, 1.0))
/// [3..5] = per-channel (R,G,B) L2 quadratic terms:
///          dot(coeffs[i], float4(nx*ny, ny*nz, nz*nz, nx*nz))
/// [6]    = Y22 term: coeffs[6].xyz * (nx*nx - ny*ny)
/// ```
float3 GetSkySHDiffuse(float3 Normal, float4 SkyIrradianceMap[7])
{
    float4 NormalVector = float4(Normal, 1.0f);

    // L0 + L1: linear terms (constant + directional)
    // Each dot evaluates: c1*nx + c2*ny + c3*nz + c4*1.0
    float3 Intermediate0;
    Intermediate0.x = dot(SkyIrradianceMap[0], NormalVector);
    Intermediate0.y = dot(SkyIrradianceMap[1], NormalVector);
    Intermediate0.z = dot(SkyIrradianceMap[2], NormalVector);

    // L2: quadratic terms (xy, yz, zz, xz products)
    float4 vB = NormalVector.xyzz * NormalVector.yzzx;
    float3 Intermediate1;
    Intermediate1.x = dot(SkyIrradianceMap[3], vB);
    Intermediate1.y = dot(SkyIrradianceMap[4], vB);
    Intermediate1.z = dot(SkyIrradianceMap[5], vB);

    // L2: Y22 term (x^2 - y^2)
    float vC = NormalVector.x * NormalVector.x - NormalVector.y * NormalVector.y;
    float3 Intermediate2 = SkyIrradianceMap[6].xyz * vC;

    // Sum all bands and clamp to non-negative (physically no negative irradiance)
    return max(0.0f, Intermediate0 + Intermediate1 + Intermediate2);
}

#endif // SH_IRRADIANCE_HLSLI
