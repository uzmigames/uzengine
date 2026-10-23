/// @ref Wright et al. 2022 "Lumen" — surface final gather backed by a world radiance cache.
/// @ref Pharr, Jakob and Humphreys 2023 "Physically Based Rendering", cosine-weighted hemisphere sampling.
#ifndef UZ_SDF_DIFFUSE_GATHER_HLSLI
#define UZ_SDF_DIFFUSE_GATHER_HLSLI

#include "sdf_reflections.hlsli"

/// @constants Cosine PDF=cos(theta)/pi cancels E/pi's cosine and pi, giving mean radiance.
/// Samples are bounded to [1,32]. The 0.999 pole threshold keeps the tangent basis nondegenerate.
/// The shared origin function preserves the raster surface; dynamic expansion handles tracing.
/// Hash multipliers 1664525,1013904223,747796405 and xor 0x9e3779b9 decorrelate pixel/frame samples.
/// How the sample set is rotated from frame to frame.
///
/// Mode 0 is the shipping path: pixel and frame are hashed into one seed and the seed goes through
/// the base-2 radical inverse. The frame stride 747796405 is odd, so the seed's low bit alternates
/// every frame, and a base-2 radical inverse is a bit reversal that moves that low bit into the
/// result's high bit. Which half of the turn the rotation lands in is therefore locked to frame
/// parity (ledger H-31, measured on recorded seeds).
///
/// Mode 2 is a reference generator: an independent hashed rotation per frame, unbiased by
/// construction, for estimating what the other two approximate. It is not a shipping candidate.
///
/// Mode 1 is the pre-registered candidate: the pixel keeps a hashed offset and the frame advances by
/// the R2 Kronecker constants (Roberts 2018, the plastic number's reciprocals) in exact 32-bit fixed
/// point, so nothing degrades at large frame indices. Both modes are Cranley-Patterson rotations of
/// the same Hammersley set, so each frame's point set, the ray count and the estimator's expectation
/// are identical; only the frame-to-frame sequence differs.
/// @ref Roberts 2018 "The Unreasonable Effectiveness of Quasirandom Sequences".
/// @ref Cranley and Patterson 1976 "Randomization of Number Theoretic Methods".
float2 SdfDiffuseRotation(uint2 pixel, uint frame, uint mode) {
    mode &= 0x7fffffffu; // PerFrameCB packs diffuse sample-validity rejection in bit 31.
    if (mode == 0u) {
        uint seed = pixel.x * 1664525u + pixel.y * 1013904223u + frame * 747796405u;
        return float2(RadicalInverseVdC(seed), RadicalInverseVdC(seed ^ 0x9e3779b9u));
    }
    if (mode == 1u) {
        uint px = pixel.x * 1664525u + pixel.y * 1013904223u;
        px ^= px >> 16; px *= 0x85ebca6bu; px ^= px >> 13;
        uint py = px ^ 0x9e3779b9u;
        py *= 0xc2b2ae35u; py ^= py >> 16;
        return float2(float(px + frame * 0xC13FA9A9u), float(py + frame * 0x91E10DA6u))
             * 2.3283064365386963e-10;
    }
    // Mode 2: a reference generator, not a candidate. An independent hashed rotation per (pixel,
    // frame) is an unbiased Monte Carlo Cranley-Patterson rotation, so averaging it over many frames
    // estimates the integral both other modes approximate. It maximises temporal noise by design and
    // must never ship.
    uint h = pixel.x * 73856093u ^ pixel.y * 19349663u ^ frame * 83492791u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    uint h2 = h ^ 0x9e3779b9u;
    h2 *= 0x85ebca6bu; h2 ^= h2 >> 13; h2 *= 0xc2b2ae35u; h2 ^= h2 >> 16;
    return float2(float(h), float(h2)) * 2.3283064365386963e-10;
}

/// Rays reported individually for diagnostics. Eight is the shipping sample count
/// (`r.GI.FinalGather.Samples`); a larger count records the first eight and says so.
#define UZ_GATHER_MAX_REPORTED_RAYS 8

/// The gather, with the identity of what it sampled exposed for diagnostics. `GatherSdfDiffuse`
/// is a thin wrapper on this so there is one implementation, not two that can drift apart.
///
/// `rays[i]` is the world-space direction (xyz) and that ray's radiance contribution as luminance.
/// Every ray carries the same weight, 1/count: the cosine PDF cancels the cosine and pi, so the
/// estimator is a plain mean. That is what makes the contributions checkable against the result.
float3 GatherSdfDiffuseIdentified(SdfReflectionScene scene, float3 position, float3 normal, float3 geometricNormal,
                                  uint2 pixel, uint frame, uint samples, uint rotationMode,
                                  out float3 origin, out uint seed, out float3 firstDirection,
                                  out bool resolved, out float4 rays[UZ_GATHER_MAX_REPORTED_RAYS],
                                  out uint reportedRays, out float2 rotationUsed,
                                  out float4 cascadeStats) {
    // Diagnostic (followup-13): x = fraction of hits inside clipmap 0, y = fraction of hits in a
    // coarser cascade, z = min distance from a hit to the nearest clipmap-0 face in voxels (negative
    // outside), w = fraction of rays that escaped. Says where the rays ENDED with respect to the
    // finest clipmap, which is the only sampling quantity a voxel-aligned recentre can change.
    cascadeStats = float4(0.0, 0.0, 1e9, 0.0);
    origin = position;
    seed = pixel.x * 1664525u + pixel.y * 1013904223u + frame * 747796405u;
    rotationUsed = SdfDiffuseRotation(pixel, frame, rotationMode);
    firstDirection = float3(0,0,0);
    reportedRays = 0u;
    [unroll] for (uint r = 0u; r < UZ_GATHER_MAX_REPORTED_RAYS; ++r) rays[r] = float4(0,0,0,0);
    resolved = ResolveSdfRayOrigin(scene, position, geometricNormal, origin);
    if (!resolved) return 0.0;
    float3 up = abs(normal.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    uint count = clamp(samples, 1u, 32u);
    float2 rotation = rotationUsed;
    float3 irradianceOverPi = 0.0;
    const float invCount = 1.0 / float(count);
    const float4 clip0 = scene.clipmaps[0];
    const float voxel = max(scene.voxelSize, 1e-3);
    [loop] for (uint i = 0; i < count; ++i) {
        float pdf;
        float3 sampleDirection = SampleCosineHemisphere(frac(Hammersley(i, count) + rotation), pdf);
        float3 direction = tangent * sampleDirection.x + bitangent * sampleDirection.y + normal * sampleDirection.z;
        if (i == 0u) firstDirection = direction;
        float hitT;
        float3 contribution = TraceSdfSurfaceRadianceIdentified(scene, origin, direction, 0u, geometricNormal, hitT);
        irradianceOverPi += contribution;
        if (hitT >= 0.0) {
            const float3 hitPos = origin + direction * hitT;
            // Distance to the nearest face of clipmap 0, in voxels: positive inside, negative outside.
            const float3 local = abs(hitPos - clip0.xyz);
            const float faceVoxels = (clip0.w - max(local.x, max(local.y, local.z))) / voxel;
            if (clip0.w > 0.0 && faceVoxels >= 0.0) cascadeStats.x += invCount;
            else                                    cascadeStats.y += invCount;
            cascadeStats.z = min(cascadeStats.z, faceVoxels);
        } else if (hitT == -1.0) {
            cascadeStats.w += invCount;
        }
        if (i < UZ_GATHER_MAX_REPORTED_RAYS) {
            rays[i] = float4(direction, dot(contribution, float3(0.2126, 0.7152, 0.0722)));
            reportedRays = i + 1u;
        }
    }
    return irradianceOverPi / float(count);
}

float3 GatherSdfDiffuse(SdfReflectionScene scene, float3 position, float3 normal, float3 geometricNormal,
                        uint2 pixel, uint frame, uint samples, uint rotationMode) {
    float3 origin, firstDirection;
    float4 rays[UZ_GATHER_MAX_REPORTED_RAYS];
    float2 rotationUsed;
    uint seed, reportedRays;
    bool resolved;
    float4 cascadeStats;
    return GatherSdfDiffuseIdentified(scene, position, normal, geometricNormal, pixel, frame, samples,
                                      rotationMode, origin, seed, firstDirection, resolved, rays,
                                      reportedRays, rotationUsed, cascadeStats);
}

#endif
