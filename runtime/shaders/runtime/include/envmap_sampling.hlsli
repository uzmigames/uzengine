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
    float3 direction;  ///< World-space (Z-up) direction toward the sampled texel.
    float  pdf;        ///< Probability density (per solid angle).
    float3 color;      ///< Environment radiance from the sampled direction.
};

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate Conversions
// ─────────────────────────────────────────────────────────────────────────────

/// The engine's cubemaps are in the DX convention with +Y up; world space is Z-up. The luminance pyramid
/// and the alias table live in cubemap space (the equirect's pole is the cube's +Y); every direction that
/// crosses this interface is converted here, the same swizzle the lighting and the skybox apply
/// (`WorldToCubemap` in deferred_lighting_ps.hlsl / skybox_cubemap_ps.hlsl). The swap is its own inverse.
float3 EnvWorldToCubemap(float3 dir) { return float3(dir.x, dir.z, dir.y); }
float3 EnvCubemapToWorld(float3 dir) { return float3(dir.x, dir.z, dir.y); }

/// Convert equirectangular UV [0,1]² to unit direction (cubemap space: the equirect's pole is +Z here,
/// which the cube stores as its +Y).
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

/// Convert unit direction (cubemap space) to equirectangular UV [0,1]².
float2 DirectionToEnvUV(float3 dir) {
    float theta = acos(clamp(dir.z, -1.0, 1.0));
    float phi   = atan2(dir.y, dir.x);
    if (phi < 0.0) phi += 2.0 * PI;
    return float2(phi / (2.0 * PI), theta / PI);
}

// ─────────────────────────────────────────────────────────────────────────────
// Solid-angle pdf (block B.5b)
// ─────────────────────────────────────────────────────────────────────────────

/// Solid-angle pdf of an equirect texel with pyramid weight `lumValue` (luminance * sin(theta)) whose
/// centre sits at the equirect row `v` in [0, 1] (theta = v * PI).
float EnvTexelPdf(float lumValue, float totalLuminance, uint texelCount, float v)
{
    float sinTheta = max(sin(v * PI), 1e-4);
    float pdf = (lumValue / max(totalLuminance, 1e-10)) * float(texelCount) / (2.0 * PI * PI * sinTheta);
    return max(pdf, 1e-10);
}

// ─────────────────────────────────────────────────────────────────────────────
// Alias Table Sampling (O(1))
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Vose 1991 — O(1) discrete sampling from arbitrary distribution.
///
/// Given two uniform random numbers in [0,1]:
///   u.x selects a bin, u.y accepts/rejects within the bin.
/// Returns the sampled texel index in the equirectangular luminance map.
///
/// The accept test needs a uniform of its own: reusing the fractional part of u.x * texelCount
/// (the classic trick) requires far more than the table's 17 bits of precision in u.x. With the
/// 16-bit randoms this shader used, that fraction was always 0 (block B.5b): every sample accepted its
/// own even texel, the alias was never taken, and the environment was sampled uniformly while the
/// estimator divided by the luminance pdf (+11 % at roughness 0.3, +25 % at 0.5 on the reference).
uint SampleAliasTable(float2 u,
                      StructuredBuffer<AliasEntry> aliasTable,
                      uint texelCount)
{
    // Select bin
    uint idx = min(uint(u.x * float(texelCount)), texelCount - 1);

    // Accept/reject with the second uniform
    AliasEntry entry = aliasTable[idx];
    return (u.y < entry.prob) ? idx : entry.alias;
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

    // Direction from equirectangular UV (cubemap space); the caller gets it in world space
    float3 dirCube = EnvUVToDirection(uv);
    result.direction = EnvCubemapToWorld(dirCube);

    // Fetch environment radiance from cubemap
    TextureCube<float4> envMap = ResourceDescriptorHeap[envMapSrv];
    result.color = envMap.SampleLevel(samp, dirCube, 0).rgb;

    // PDF per solid angle. The pyramid stores w = luminance * sin(theta) per texel, so the discrete
    // probability is p(texel) = w / total. A texel covers (2 PI / W) * (PI / H) * sin(theta) steradians,
    // i.e. 2 PI^2 sin(theta) / texelCount, so
    //   p(omega) = p(texel) / dOmega = (w / total) * texelCount / (2 PI^2 sin(theta)).
    // (Block B.5b: the sin(theta) of the solid angle was missing, which left the pdf too small by
    // sin(theta) and the estimate too bright towards the zenith and the nadir.)
    Texture2D<float> lumTex = ResourceDescriptorHeap[lumMip0Srv];
    float lumValue = lumTex.Load(int3(texX, texY, 0));
    result.pdf = EnvTexelPdf(lumValue, totalLuminance, texelCount, uv.y);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF Evaluation (for MIS weight computation)
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluate the environment sampling PDF for a given direction.
/// Used to compute MIS weight when combining environment sampling with BRDF sampling.
///
/// @param dir            World-space (Z-up) direction to evaluate.
/// @param lumMip0Srv     SRV index of luminance pyramid mip 0.
/// @param totalLuminance Total luminance sum.
/// @param texelCount     texelCount = lumWidth * lumHeight.
float EvaluateEnvironmentPDF(
    float3 dir,
    uint   lumMip0Srv,
    float  totalLuminance,
    uint   texelCount)
{
    float2 uv = DirectionToEnvUV(EnvWorldToCubemap(dir));

    Texture2D<float> lumTex = ResourceDescriptorHeap[lumMip0Srv];
    // Nearest texel lookup (Load, not Sample, for exact PDF match)
    uint2 dims;
    lumTex.GetDimensions(dims.x, dims.y);
    int2 texel = int2(uv * float2(dims));
    texel = clamp(texel, int2(0, 0), int2(dims) - 1);

    float lumValue = lumTex.Load(int3(texel, 0));
    return EnvTexelPdf(lumValue, totalLuminance, texelCount, (float(texel.y) + 0.5) / float(dims.y));
}

#endif // ENVMAP_SAMPLING_HLSLI
