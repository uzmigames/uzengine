/// Environment map importance sampling via alias table.
/// @ref Vose 1991 "A Linear Algorithm for Generating Random Numbers with a Given Distribution"
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" Chapter 14.6 — 2D importance sampling
/// @ref Falcor EnvMapSampler — alias table + hierarchical luminance pyramid
///
/// Provides O(1) importance-weighted direction sampling from an environment map
/// using a precomputed alias table. The alias table is built CPU-side from the
/// luminance pyramid and uploaded as a StructuredBuffer.
///
/// # Usage in deferred lighting
/// ```hlsl
/// #include "envmap_sampling.hlsli"
/// float2 u = Hammersley(sampleIdx, sampleCount); // or blue noise
/// EnvSample s = SampleEnvironment(u, aliasTable, texelCount, lumMip0, totalLum, envMap, sampler);
/// float3 contribution = brdf * s.color * misWeight / s.pdf;
/// ```

#ifndef ENVMAP_SAMPLING_HLSLI
#define ENVMAP_SAMPLING_HLSLI

#include "../common/constants.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Data Structures
// ─────────────────────────────────────────────────────────────────────────────

/// One entry in the alias table. Stored in StructuredBuffer<AliasEntry>.
/// @ref Vose 1991 — alias method for O(1) discrete sampling
struct AliasEntry {
    float prob;     ///< Probability threshold [0,1] for this bin.
    uint  alias;    ///< Index of the alias bin (used when u > prob).
};

/// Result of an environment sample.
struct EnvSample {
    float3 direction;  ///< World-space direction toward the sampled texel.
    float  pdf;        ///< Probability density (per solid angle).
    float3 color;      ///< Environment radiance from the sampled direction.
};

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate Conversions
// ─────────────────────────────────────────────────────────────────────────────

/// Convert equirectangular UV [0,1]² to unit direction (Z-up).
float3 EnvUVToDirection(float2 uv) {
    float phi   = uv.x * 2.0 * PI;
    float theta = uv.y * PI;
    float sinTheta = sin(theta);
    return float3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cos(theta)
    );
}

/// Convert unit direction (Z-up) to equirectangular UV [0,1]².
float2 DirectionToEnvUV(float3 dir) {
    float theta = acos(clamp(dir.z, -1.0, 1.0));
    float phi   = atan2(dir.y, dir.x);
    if (phi < 0.0) phi += 2.0 * PI;
    return float2(phi / (2.0 * PI), theta / PI);
}

// ─────────────────────────────────────────────────────────────────────────────
// Alias Table Sampling (O(1))
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Vose 1991 — O(1) discrete sampling from arbitrary distribution.
///
/// Given two uniform random numbers in [0,1]:
///   u.x selects a bin, u.y accepts/rejects within the bin.
/// Returns the sampled texel index in the equirectangular luminance map.
uint SampleAliasTable(float2 u,
                      StructuredBuffer<AliasEntry> aliasTable,
                      uint texelCount)
{
    // Select bin
    float scaled = u.x * float(texelCount);
    uint  idx    = min(uint(scaled), texelCount - 1);
    float frac   = scaled - float(idx);

    // Accept/reject
    AliasEntry entry = aliasTable[idx];
    return (frac < entry.prob) ? idx : entry.alias;
}

// ─────────────────────────────────────────────────────────────────────────────
// Full Environment Sample
// ─────────────────────────────────────────────────────────────────────────────

/// Sample the environment map using the alias table.
///
/// @param u              Two uniform random numbers in [0,1]².
/// @param aliasTable     StructuredBuffer<AliasEntry> built from luminance pyramid.
/// @param texelCount     Total texel count in mip 0 (width * height).
/// @param lumMip0Srv     SRV index of luminance pyramid mip 0 (equirect R32F).
/// @param totalLuminance Sum of all luminance values (from 1x1 mip).
/// @param envMapSrv      SRV index of the source cubemap.
/// @param lumWidth       Width of luminance mip 0.
/// @param lumHeight      Height of luminance mip 0.
/// @param samp           Linear sampler for cubemap lookup.
EnvSample SampleEnvironment(
    float2 u,
    StructuredBuffer<AliasEntry> aliasTable,
    uint   texelCount,
    uint   lumMip0Srv,
    float  totalLuminance,
    uint   envMapSrv,
    uint   lumWidth,
    uint   lumHeight,
    SamplerState samp)
{
    EnvSample result;

    // O(1) alias table lookup → texel index
    uint texelIdx = SampleAliasTable(u, aliasTable, texelCount);

    // Convert flat index → 2D equirect coordinate
    uint texX = texelIdx % lumWidth;
    uint texY = texelIdx / lumWidth;

    // Add sub-texel jitter for anti-aliasing (use fractional part from alias sampling)
    float2 uv = (float2(texX, texY) + 0.5) / float2(lumWidth, lumHeight);

    // Direction from equirectangular UV
    result.direction = EnvUVToDirection(uv);

    // Fetch environment radiance from cubemap
    TextureCube<float4> envMap = ResourceDescriptorHeap[envMapSrv];
    result.color = envMap.SampleLevel(samp, result.direction, 0).rgb;

    // Compute PDF: p(texel) = luminance(texel) / totalLuminance.
    // Convert to solid angle PDF: p(omega) = p(texel) * texelCount / (4 * PI).
    // But we stored luminance * sin(theta) in the pyramid, so the sin(theta)
    // Jacobian is already baked in. The PDF per solid angle is:
    //   pdf = (lumValue / totalLuminance) * texelCount / (2 * PI * PI)
    // This accounts for the equirectangular → sphere Jacobian: dA = sin(theta) dtheta dphi.
    Texture2D<float> lumTex = ResourceDescriptorHeap[lumMip0Srv];
    float lumValue = lumTex.Load(int3(texX, texY, 0));
    result.pdf = (lumValue / max(totalLuminance, 1e-10)) * float(texelCount) / (2.0 * PI * PI);
    result.pdf = max(result.pdf, 1e-10); // prevent division by zero

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF Evaluation (for MIS weight computation)
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluate the environment sampling PDF for a given direction.
/// Used to compute MIS weight when combining environment sampling with BRDF sampling.
///
/// @param dir            World-space direction to evaluate.
/// @param lumMip0Srv     SRV index of luminance pyramid mip 0.
/// @param totalLuminance Total luminance sum.
/// @param texelCount     texelCount = lumWidth * lumHeight.
float EvaluateEnvironmentPDF(
    float3 dir,
    uint   lumMip0Srv,
    float  totalLuminance,
    uint   texelCount)
{
    float2 uv = DirectionToEnvUV(dir);

    Texture2D<float> lumTex = ResourceDescriptorHeap[lumMip0Srv];
    // Nearest texel lookup (Load, not Sample, for exact PDF match)
    uint2 dims;
    lumTex.GetDimensions(dims.x, dims.y);
    int2 texel = int2(uv * float2(dims));
    texel = clamp(texel, int2(0, 0), int2(dims) - 1);

    float lumValue = lumTex.Load(int3(texel, 0));
    float pdf = (lumValue / max(totalLuminance, 1e-10)) * float(texelCount) / (2.0 * PI * PI);
    return max(pdf, 1e-10);
}

#endif // ENVMAP_SAMPLING_HLSLI
