/// UzEngine SDF Common — Signed distance field sampling and ray marching utilities.
///
/// Provides world-space SDF sampling, ray-box intersection, and sphere marching
/// for the GI pipeline's mesh SDF tracing system.
///
/// @source UE5 Common.ush:1883-1903 — LineBoxIntersect
/// @source UE5 MeshDistanceFieldCommon.ush:9-38 — DistanceToNearestSurfaceForObject
/// @source UE5 LumenSoftwareRayTracing.ush:124-259 — RayTraceSingleMeshSDF

#ifndef SDF_COMMON_HLSLI
#define SDF_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// SampleMeshSDF — sample a dense mesh SDF 3D texture at a world position
// ─────────────────────────────────────────────────────────────────────────────

/// Samples a mesh SDF 3D texture at a world position.
/// Returns the signed distance in world units (cm).
/// Negative = inside mesh, positive = outside mesh.
///
/// The volume is axis-aligned with center at volumeOrigin and half-extents
/// volumeExtent. The 3D texture covers the full volume with trilinear filtering.
///
/// @source UE5 MeshDistanceFieldCommon.ush:9-38 — DistanceToNearestSurfaceForObject
/// @constants volumeOrigin, volumeExtent from MeshSDF struct
/// @deviations NONE — direct world-space sampling (UE5 uses volume-space transform,
///   but for a single dense SDF the math is equivalent)
float SampleMeshSDF(
    Texture3D<float> sdfTexture,
    SamplerState sdfSampler,
    float3 worldPos,
    float3 volumeOrigin,
    float3 volumeExtent,
    uint resolution)
{
    // Transform world position to normalized [0,1]^3 UV space within the SDF volume.
    // volumeOrigin is the center; volumeExtent is half-extents.
    // localPos maps from [-extent, +extent] to [0, 1].
    float3 localPos = (worldPos - volumeOrigin) / (volumeExtent * 2.0f) + 0.5f;

    // Points outside the volume return a large positive distance (outside).
    if (any(localPos < 0.0f) || any(localPos > 1.0f))
    {
        return 1e6f;
    }

    return sdfTexture.SampleLevel(sdfSampler, localPos, 0).r;
}

// ─────────────────────────────────────────────────────────────────────────────
// LineBoxIntersect — ray vs axis-aligned box intersection
// ─────────────────────────────────────────────────────────────────────────────

/// Computes parametric ray-box intersection times.
///
/// Returns float2(enterT, exitT) where enterT is the parametric distance
/// along the ray where it enters the box, and exitT where it exits.
/// If enterT > exitT, the ray misses the box entirely.
/// enterT is clamped to >= 0 (ray starts at origin, not behind it).
///
/// Note: UE5's LineBoxIntersect takes RayOrigin and RayEnd and returns times
/// in [0,1] (saturated). This version takes RayOrigin and RayDir and returns
/// raw parametric distances for more flexible use in sphere marching.
///
/// @source UE5 Common.ush:1883-1903 — LineBoxIntersect
/// @constants NONE
/// @deviations Uses ray direction instead of ray endpoint; no saturate (raw t values)
float2 LineBoxIntersect(
    float3 rayOrigin,
    float3 rayDir,
    float3 boxMin,
    float3 boxMax)
{
    float3 invDir = 1.0f / rayDir;
    float3 t0 = (boxMin - rayOrigin) * invDir;
    float3 t1 = (boxMax - rayOrigin) * invDir;
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);

    // Furthest near intersection
    float enterT = max(tmin.x, max(tmin.y, tmin.z));
    // Closest far intersection
    float exitT = min(tmax.x, min(tmax.y, tmax.z));

    return float2(max(enterT, 0.0f), exitT);
}

// ─────────────────────────────────────────────────────────────────────────────
// SphereMarchMeshSDF — trace a ray through a mesh SDF volume
// ─────────────────────────────────────────────────────────────────────────────

/// Sphere-marches through a mesh SDF 3D texture to find the nearest hit.
///
/// The algorithm clips the ray to the SDF volume's AABB, then iteratively
/// steps along the ray by the sampled SDF distance. A hit is detected when
/// the distance drops below a fraction of the voxel size.
///
/// # Parameters
/// - sdfTexture: R16F 3D texture of signed distances in world units (cm).
/// - sdfSampler: Trilinear clamp sampler.
/// - volumeOrigin: World-space center of the SDF volume.
/// - volumeExtent: World-space half-extents of the SDF volume.
/// - resolution: Voxels per axis of the 3D texture.
/// - rayOrigin: World-space ray start position.
/// - rayDir: Normalized world-space ray direction.
/// - maxDist: Maximum trace distance in world units (cm).
/// - hitDist: [out] Distance along ray to hit point (maxDist if no hit).
/// - hitPos: [out] World-space hit position.
///
/// # Returns
/// true if a surface was hit, false if the ray exited the volume or exceeded maxDist.
///
/// @source UE5 LumenSoftwareRayTracing.ush:124-259 — RayTraceSingleMeshSDF
/// @constants MaxSteps=64, MinStepSize=1/(16*64), ExpandSurfaceFalloff=2*voxelSize
/// @deviations Simplified: no MIP traversal, no dithered transparency, no surface
///   expansion scale (Coverage always 1.0). These features will be added in Phase B
///   when the global distance field is available for Coverage data.
bool SphereMarchMeshSDF(
    Texture3D<float> sdfTexture,
    SamplerState sdfSampler,
    float3 volumeOrigin,
    float3 volumeExtent,
    uint resolution,
    float3 rayOrigin,
    float3 rayDir,
    float maxDist,
    out float hitDist,
    out float3 hitPos)
{
    hitDist = maxDist;
    hitPos = rayOrigin + rayDir * maxDist;

    // Clip ray to SDF volume AABB
    float3 boxMin = volumeOrigin - volumeExtent;
    float3 boxMax = volumeOrigin + volumeExtent;
    float2 boxT = LineBoxIntersect(rayOrigin, rayDir, boxMin, boxMax);

    // Miss: ray does not intersect the volume
    if (boxT.x > boxT.y)
    {
        return false;
    }

    // Compute voxel size for hit threshold and minimum step size.
    // The voxel size is the extent of the volume divided by the resolution,
    // using the maximum axis extent for a conservative estimate.
    /// @source UE5 LumenSoftwareRayTracing.ush:182 — MinStepSize = 1/(16*MaxSteps)
    float voxelSize = max(volumeExtent.x, max(volumeExtent.y, volumeExtent.z)) * 2.0f / float(resolution);

    // Surface expansion: detect hits within ExpandSurfaceAmount of the zero-crossing.
    // The expansion ramps up from 0 at the trace start to full at 2*ExpandSurfaceDistance,
    // preventing incorrect self-occlusion near the ray origin.
    /// @source UE5 LumenSoftwareRayTracing.ush:198-200
    float expandSurfaceDistance = voxelSize;
    float expandSurfaceFalloff = 2.0f * expandSurfaceDistance;

    float t = boxT.x;
    float maxT = min(boxT.y, maxDist);

    /// @source UE5 LumenSoftwareRayTracing.ush:181 — MaxSteps = 64
    static const uint kMaxSteps = 64;
    /// @source UE5 LumenSoftwareRayTracing.ush:182 — MinStepSize = 1/(16*MaxSteps)
    float minStepSize = voxelSize / (16.0f * float(kMaxSteps));

    for (uint step = 0; step < kMaxSteps; step++)
    {
        float3 samplePos = rayOrigin + rayDir * t;
        float dist = SampleMeshSDF(sdfTexture, sdfSampler, samplePos,
                                    volumeOrigin, volumeExtent, resolution);

        // Surface expansion amount ramps up with distance from trace start
        // to avoid self-intersection at grazing angles near the origin.
        /// @source UE5 LumenSoftwareRayTracing.ush:199-200
        float expandAmount = expandSurfaceDistance * saturate(t / expandSurfaceFalloff);

        if (dist < expandAmount)
        {
            // Hit: pull back by expand amount for better gradient at hit point
            /// @source UE5 LumenSoftwareRayTracing.ush:229
            hitDist = clamp(t + dist - expandAmount, boxT.x, maxT);
            hitPos = rayOrigin + rayDir * hitDist;
            return true;
        }

        // Step forward by the SDF distance, with a minimum step size
        // to guarantee progress and prevent infinite loops in flat regions.
        /// @source UE5 LumenSoftwareRayTracing.ush:235-236
        float stepDist = max(dist, minStepSize);
        t += stepDist;

        if (t > maxT + expandAmount)
        {
            break;
        }
    }

    return false;
}

#endif // SDF_COMMON_HLSLI
