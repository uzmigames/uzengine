/// UzEngine SDF Common — Signed distance field sampling and ray marching utilities.
///
/// Provides world-space SDF sampling, ray-box intersection, and sphere marching
/// for the GI pipeline's mesh SDF tracing system.
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" (SIGGRAPH)
/// @ref Hart 1996 "Sphere Tracing" — fundamental SDF ray marching algorithm

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
/// @ref Wright 2015 "Dynamic Occlusion with SDFs" — mesh SDF distance query
/// @constants volumeOrigin, volumeExtent from MeshSDF struct
/// @deviations NONE — direct world-space sampling (reference uses volume-space transform,
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
/// Note: the reference engine's LineBoxIntersect takes RayOrigin and RayEnd and returns times
/// in [0,1] (saturated). This version takes RayOrigin and RayDir and returns
/// raw parametric distances for more flexible use in sphere marching.
///
/// @ref Standard ray-AABB slab intersection (Williams et al. 2005)
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
/// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —124-259 — RayTraceSingleMeshSDF
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
    /// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —182 — MinStepSize = 1/(16*MaxSteps)
    float voxelSize = max(volumeExtent.x, max(volumeExtent.y, volumeExtent.z)) * 2.0f / float(resolution);

    // Surface expansion: detect hits within ExpandSurfaceAmount of the zero-crossing.
    // The expansion ramps up from 0 at the trace start to full at 2*ExpandSurfaceDistance,
    // preventing incorrect self-occlusion near the ray origin.
    /// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —198-200
    float expandSurfaceDistance = voxelSize;
    float expandSurfaceFalloff = 2.0f * expandSurfaceDistance;

    float t = boxT.x;
    float maxT = min(boxT.y, maxDist);

    /// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —181 — MaxSteps = 64
    static const uint kMaxSteps = 64;
    /// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —182 — MinStepSize = 1/(16*MaxSteps)
    float minStepSize = voxelSize / (16.0f * float(kMaxSteps));

    for (uint step = 0; step < kMaxSteps; step++)
    {
        float3 samplePos = rayOrigin + rayDir * t;
        float dist = SampleMeshSDF(sdfTexture, sdfSampler, samplePos,
                                    volumeOrigin, volumeExtent, resolution);

        // Surface expansion amount ramps up with distance from trace start
        // to avoid self-intersection at grazing angles near the origin.
        /// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —199-200
        float expandAmount = expandSurfaceDistance * saturate(t / expandSurfaceFalloff);

        if (dist < expandAmount)
        {
            // Hit: pull back by expand amount for better gradient at hit point
            /// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —229
            hitDist = clamp(t + dist - expandAmount, boxT.x, maxT);
            hitPos = rayOrigin + rayDir * hitDist;
            return true;
        }

        // Step forward by the SDF distance, with a minimum step size
        // to guarantee progress and prevent infinite loops in flat regions.
        /// @ref Hart 1996 "Sphere Tracing" / Wright 2015 —235-236
        float stepDist = max(dist, minStepSize);
        t += stepDist;

        if (t > maxT + expandAmount)
        {
            break;
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Distance Field Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples a single global distance field clipmap at a world position.
///
/// Returns the signed distance in world units (cm) from the nearest surface
/// in the given clipmap. Returns a large positive distance if the query point
/// is outside the clipmap volume.
///
/// @ref Wright 2015 — global distance field sampling
/// @constants NONE (volume parameters passed as arguments)
/// @deviations Dense volume lookup instead of the reference engine's page-table indirection.
float SampleGlobalSdfClipmap(
    Texture3D<float> clipmapTexture,
    SamplerState     clipmapSampler,
    float3           worldPos,
    float3           clipmapCenter,
    float            clipmapExtent)
{
    // Transform world position to normalized [0,1]^3 UV space
    float3 uvPos = (worldPos - clipmapCenter + clipmapExtent) / (clipmapExtent * 2.0f);

    // Out-of-volume: return large distance
    if (any(uvPos < 0.0f) || any(uvPos > 1.0f))
    {
        return 1e6f;
    }

    return clipmapTexture.SampleLevel(clipmapSampler, uvPos, 0).r;
}

/// Estimates the surface normal at a point in the global SDF via central differences.
///
/// @ref Wright 2015 — GDF utility:60-85 — GetDistanceFieldGradient
/// @constants kGradientEpsilon = 1.0 (1 cm offset for finite differences)
/// @deviations Uses a single clipmap instead of cascaded lookup.
float3 EstimateGlobalSdfNormal(
    Texture3D<float> clipmapTexture,
    SamplerState     clipmapSampler,
    float3           worldPos,
    float3           clipmapCenter,
    float            clipmapExtent)
{
    /// @ref Wright 2015 — GDF utility:62 — TexelOffset for gradient
    float eps = clipmapExtent * 2.0f / 64.0f; // One voxel step

    float3 gradient;
    gradient.x = SampleGlobalSdfClipmap(clipmapTexture, clipmapSampler,
                     worldPos + float3(eps, 0, 0), clipmapCenter, clipmapExtent)
               - SampleGlobalSdfClipmap(clipmapTexture, clipmapSampler,
                     worldPos - float3(eps, 0, 0), clipmapCenter, clipmapExtent);

    gradient.y = SampleGlobalSdfClipmap(clipmapTexture, clipmapSampler,
                     worldPos + float3(0, eps, 0), clipmapCenter, clipmapExtent)
               - SampleGlobalSdfClipmap(clipmapTexture, clipmapSampler,
                     worldPos - float3(0, eps, 0), clipmapCenter, clipmapExtent);

    gradient.z = SampleGlobalSdfClipmap(clipmapTexture, clipmapSampler,
                     worldPos + float3(0, 0, eps), clipmapCenter, clipmapExtent)
               - SampleGlobalSdfClipmap(clipmapTexture, clipmapSampler,
                     worldPos - float3(0, 0, eps), clipmapCenter, clipmapExtent);

    float len = length(gradient);
    return len > 1e-8f ? gradient / len : float3(0, 0, 1);
}

/// Sphere-marches a ray through the cascaded global distance field.
///
/// Traces from the finest cascade outward. For each cascade, clips the ray to
/// the cascade volume, then sphere-marches. Falls through to coarser cascades
/// when the ray exits the current cascade's volume.
///
/// # Arguments
/// - clipmapTextures[4]: R16F 3D textures, one per cascade.
/// - clipmapSampler: Trilinear clamp sampler.
/// - clipmapCenters[4]: World-space center of each cascade.
/// - clipmapExtents[4]: Half-size of each cascade in world units (cm).
/// - numClipmaps: Number of cascades (1-4).
/// - rayOrigin: World-space ray start.
/// - rayDir: Normalized world-space ray direction.
/// - maxDist: Maximum trace distance in world units (cm).
/// - hitDist: [out] Distance along ray to hit (maxDist if no hit).
/// - hitNormal: [out] Estimated surface normal at hit point.
///
/// # Returns
/// true if a surface was hit, false otherwise.
///
/// @ref Wright 2015 — GDF utility:90-200 — RayTraceGlobalDistanceField
/// @constants MaxSteps=128, StepFactor=1.0, MinStepFactor=1.0
/// @deviations Dense clipmap instead of paged; no coverage atlas; no mip early-out
bool SphereMarchGlobalSDF(
    Texture3D<float> clipmapTextures[4],
    SamplerState     clipmapSampler,
    float3           clipmapCenters[4],
    float            clipmapExtents[4],
    uint             numClipmaps,
    float3           rayOrigin,
    float3           rayDir,
    float            maxDist,
    out float        hitDist,
    out float3       hitNormal)
{
    hitDist   = maxDist;
    hitNormal = float3(0, 0, 1);

    /// @ref Wright 2015 — GDF utility:92 — MaxSteps = 256
    /// We use 128 steps as a balance between quality and performance for 64^3 resolution.
    static const uint kMaxSteps = 128;

    float t = 0.0f;

    // Try each clipmap from finest (0) to coarsest (numClipmaps-1)
    for (uint clipIdx = 0; clipIdx < numClipmaps; clipIdx++)
    {
        float3 center = clipmapCenters[clipIdx];
        float  extent = clipmapExtents[clipIdx];

        /// @ref Wright 2015 — GDF utility:108 — VoxelExtent = Extent * TexelSize
        float voxelExtent = extent * 2.0f / 64.0f;

        /// @ref Wright 2015 — GDF utility:111 — InfluenceRange = 4 * 2 * VoxelExtent
        float influenceRange = 4.0f * 2.0f * voxelExtent;

        // Clip ray to this cascade's AABB
        float3 boxMin = center - extent;
        float3 boxMax = center + extent;
        float2 boxT = LineBoxIntersect(rayOrigin, rayDir, boxMin, boxMax);

        // Skip this cascade if ray doesn't intersect
        if (boxT.x > boxT.y) continue;

        // Start from either the current t or the box entry, whichever is later
        float startT = max(t, boxT.x);
        float endT   = min(boxT.y, maxDist);
        float marchT = startT;

        for (uint step = 0; step < kMaxSteps; step++)
        {
            if (marchT > endT) break;

            float3 samplePos = rayOrigin + rayDir * marchT;

            float dist = SampleGlobalSdfClipmap(
                clipmapTextures[clipIdx], clipmapSampler,
                samplePos, center, extent);

            /// @ref Wright 2015 — GDF utility:157-162 — surface expansion
            float expandAmount = voxelExtent * saturate(marchT / (2.0f * voxelExtent));

            if (dist < expandAmount)
            {
                // Surface hit
                /// @ref Wright 2015 — GDF utility:165
                hitDist = max(marchT + dist - expandAmount, 0.0f);
                float3 hitPos = rayOrigin + rayDir * hitDist;

                // Estimate normal via central differences at the hit point
                hitNormal = EstimateGlobalSdfNormal(
                    clipmapTextures[clipIdx], clipmapSampler,
                    hitPos, center, extent);

                return true;
            }

            /// @ref Wright 2015 — GDF utility:170-175 — step size
            float minStep = voxelExtent; // MinStepFactor * VoxelExtent
            marchT += max(dist, minStep);
        }

        // Continue to the next (coarser) cascade from where we left off
        t = marchT;
    }

    return false;
}

#endif // SDF_COMMON_HLSLI
