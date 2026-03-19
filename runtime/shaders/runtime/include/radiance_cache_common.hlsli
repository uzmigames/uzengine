/// UzEngine Radiance Cache Common — Probe lookup and interpolation utilities.
///
/// Provides functions for the deferred lighting pass to sample diffuse irradiance
/// from the world-space radiance cache probe grid via trilinear interpolation
/// of L2 SH coefficients.
///
/// @source UE5 LumenRadianceCacheInterpolation.ush:114-128 — GetRadianceProbeCoordFloat
/// @source UE5 LumenRadianceCacheInterpolation.ush:372-395 — SampleRadianceCacheInterpolated
/// @deviations Single uniform grid instead of UE5's 6 clipmap levels. No sphere
///   parallax correction (Lumen feature for specular). No octahedral atlas —
///   uses direct SH evaluation which integrates with UzEngine's existing SH path.

#ifndef RADIANCE_CACHE_COMMON_HLSLI
#define RADIANCE_CACHE_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// SH evaluation from packed probe buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates L2 SH for a given normal direction from a single probe's packed
/// 7-float4 data. Returns diffuse irradiance (RGB).
///
/// The packing matches UE5 SkyIrradianceEnvironmentMap:
///   [0] = (R_L00, R_L1x, R_L1z, R_L1y)
///   [1] = (G_L00, G_L1x, G_L1z, G_L1y)
///   [2] = (B_L00, B_L1x, B_L1z, B_L1y)
///   [3] = (R_L20, R_L21, R_L22, R_L2_1)
///   [4] = (G_L20, G_L21, G_L22, G_L2_1)
///   [5] = (B_L20, B_L21, B_L22, B_L2_1)
///   [6] = (R_L2xy, G_L2xy, B_L2xy, 0)
///
/// @source UE5 ReflectionEnvironmentShaders.usf:62-92 — GetSkySHDiffuse
float3 EvaluateProbeSH(
    StructuredBuffer<float4> probeSHBuffer,
    uint probeBaseIndex,
    float3 normal)
{
    float4 rSH0 = probeSHBuffer[probeBaseIndex + 0];
    float4 gSH0 = probeSHBuffer[probeBaseIndex + 1];
    float4 bSH0 = probeSHBuffer[probeBaseIndex + 2];
    float4 rSH1 = probeSHBuffer[probeBaseIndex + 3];
    float4 gSH1 = probeSHBuffer[probeBaseIndex + 4];
    float4 bSH1 = probeSHBuffer[probeBaseIndex + 5];
    float4 sh6  = probeSHBuffer[probeBaseIndex + 6];

    // L0 + L1 evaluation: dot(coefficients, float4(1, x, z, y))
    float4 nDir = float4(1.0f, normal.x, normal.z, normal.y);

    float3 result;
    result.r = dot(rSH0, nDir);
    result.g = dot(gSH0, nDir);
    result.b = dot(bSH0, nDir);

    // L2 evaluation
    // basis[4] = xy, basis[5] = yz, basis[6] = 3z^2-1, basis[7] = xz, basis[8] = x^2-y^2
    float4 l2Dir = float4(
        3.0f * normal.z * normal.z - 1.0f,  // L20
        normal.x * normal.z,                  // L21
        normal.x * normal.x - normal.y * normal.y, // L22
        normal.y * normal.z                   // L2-1
    );

    result.r += dot(rSH1, l2Dir) + sh6.x * normal.x * normal.y;
    result.g += dot(gSH1, l2Dir) + sh6.y * normal.x * normal.y;
    result.b += dot(bSH1, l2Dir) + sh6.z * normal.x * normal.y;

    return max(result, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Trilinear probe interpolation
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolates radiance cache probes at a world position via trilinear blending.
///
/// Returns diffuse irradiance for the given surface normal by evaluating the
/// SH at 8 corner probes of the grid cell containing the query point, weighted
/// by trilinear interpolation weights.
///
/// @source UE5 LumenRadianceCacheInterpolation.ush:372-395 — SampleRadianceCacheInterpolated
/// @constants NONE (grid parameters passed as arguments)
/// @deviations No sphere parallax correction, no dithered clipmap fade.
float3 SampleRadianceCache(
    float3 worldPos,
    float3 normal,
    StructuredBuffer<float4> probeSHBuffer,
    float3 gridOrigin,
    float  gridExtent,
    uint   probesPerAxis)
{
    // Compute fractional grid coordinates
    float probeSpacing = (gridExtent * 2.0f) / float(probesPerAxis);
    float3 localPos = (worldPos - gridOrigin) / probeSpacing;

    // Clamp to grid bounds (half-texel inset to prevent sampling beyond edges)
    float3 maxCoord = float3(probesPerAxis - 1, probesPerAxis - 1, probesPerAxis - 1);
    localPos = clamp(localPos - 0.5f, 0.0f, maxCoord);

    // Integer grid cell and fractional weights
    uint3 cell = uint3(floor(localPos));
    float3 frac3 = localPos - float3(cell);

    // Clamp upper corner to grid bounds
    uint3 cellMax = min(cell + 1, uint3(probesPerAxis - 1, probesPerAxis - 1, probesPerAxis - 1));

    // ── Evaluate SH at 8 corner probes and blend ──────────────────────
    float3 result = (float3)0;

    // Trilinear: iterate 2x2x2 corners
    for (uint dz = 0; dz < 2; dz++)
    {
        float wz = (dz == 0) ? (1.0f - frac3.z) : frac3.z;
        uint pz = (dz == 0) ? cell.z : cellMax.z;

        for (uint dy = 0; dy < 2; dy++)
        {
            float wy = (dy == 0) ? (1.0f - frac3.y) : frac3.y;
            uint py = (dy == 0) ? cell.y : cellMax.y;

            for (uint dx = 0; dx < 2; dx++)
            {
                float wx = (dx == 0) ? (1.0f - frac3.x) : frac3.x;
                uint px = (dx == 0) ? cell.x : cellMax.x;

                float weight = wx * wy * wz;
                if (weight <= 0.0f) continue;

                // Linear probe index from 3D coordinates
                uint probeIdx = pz * probesPerAxis * probesPerAxis
                              + py * probesPerAxis
                              + px;

                // Evaluate SH at this probe for the given normal direction
                float3 probeIrradiance = EvaluateProbeSH(probeSHBuffer, probeIdx * 7, normal);

                result += probeIrradiance * weight;
            }
        }
    }

    return result;
}

#endif // RADIANCE_CACHE_COMMON_HLSLI
