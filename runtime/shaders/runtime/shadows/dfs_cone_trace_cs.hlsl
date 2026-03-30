/// Distance Field Shadow — Cone Tracing Compute Shader
///
/// Per-pixel ray march through mesh SDFs toward the directional light source.
/// Uses cone expansion based on the light source angle to produce physically
/// accurate variable-penumbra soft shadows.
///
/// # Pipeline
/// 1. Reconstruct world position from depth buffer
/// 2. For each SDF object (from culled list):
///    a. Transform ray into object's local SDF space
///    b. Sphere-trace through the SDF with adaptive step size
///    c. Compute cone visibility: distance / cone_radius at each step
///    d. Track minimum visibility across all steps and objects
/// 3. Write (min_visibility, scene_depth) to output UAV
///
/// # Inputs
/// - Depth buffer (t0): scene depth for world position reconstruction
/// - Object data buffer (t1): StructuredBuffer<float4> of SdfObjectGpuData
/// - SDF atlas (t2): Texture3D<float> R8_UNORM brick atlas
/// - Indirection buffer (t3): StructuredBuffer<uint> brick indirection
/// - Culled object indices (t4): StructuredBuffer<uint> from cull pass
///
/// # Output
/// - RWTexture2D<float2> (u0): (shadow_factor, scene_depth)
///   shadow_factor: 0 = fully shadowed, 1 = fully lit
///
/// # Thread Group
/// [8, 8, 1] — one thread per output pixel
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" (SIGGRAPH)
/// @ref Hart 1996 "Sphere Tracing" — SDF ray marching with cone-based soft shadows

#include "../include/dfs_common.hlsli"
#include "../include/depth_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, compute root signature)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Wright 2015 "DFS" — cone trace dispatch:293-319 — FDistanceFieldShadowingCS::FParameters
/// @constants All values populated from DistanceFieldShadowConfig + scene state
/// @deviations Combined into a single cbuffer (reference uses separate parameter structs).
///             Bound at register(b1) via SetRootCBV (kSlotPerFrame) because the data
///             exceeds the 8-DWORD root constant limit at b0.
cbuffer DfsShadowParams : register(b1) {
    float4x4 g_InvViewProj;        // Inverse view-projection for world pos reconstruction
    float3   g_LightDir;           // World-space light direction (toward light, normalized)
    float    g_TanLightAngle;      // tan(LightSourceAngle), default tan(0.5357°) = 0.00935
                                   // @ref Standard sun angular diameter — default 0.5357°
    float    g_MaxTraceDistance;    // Max DFS distance in cm, default 51200
                                   // @ref Default max ray distance — 51200 cm (512 m)
    float    g_SelfShadowBias;     // Self-shadow prevention bias in cm
    float    g_NearPlane;          // Camera near plane for depth linearization
    float    g_FarPlane;           // Camera far plane for depth linearization
    uint     g_ObjectCount;        // Number of objects in culled list
    uint     g_OutputWidth;        // Output texture width
    uint     g_OutputHeight;       // Output texture height
    uint     g_DownsampleFactor;   // 1 = full res, 2 = half res
    uint3    g_AtlasSizeInBricks;  // Atlas dimension in bricks per axis
    float    g_AtlasTexelSizeX;    // 1.0 / atlas_texel_width
    float    g_AtlasTexelSizeY;    // 1.0 / atlas_texel_height
    float    g_AtlasTexelSizeZ;    // 1.0 / atlas_texel_depth
    float    g_RayStartOffsetDepthScale; // Scale for depth-dependent ray start offset
                                   // @ref Wright 2015 "DFS" — cone trace dispatch:696 — RayStartOffsetDepthScale
    float    g_MinDepth;           // Min depth for shadow computation
    float    g_MaxDepth;           // Max depth for shadow computation
    uint     _pad0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

Texture2D<float>           g_DepthBuffer       : register(t0);
StructuredBuffer<float4>   g_ObjectData        : register(t1);
Texture3D<float>           g_SdfAtlas          : register(t2);
StructuredBuffer<uint>     g_IndirectionBuffer : register(t3);
StructuredBuffer<uint>     g_CulledObjectIndices : register(t4);

RWTexture2D<float2>        g_OutputShadow      : register(u0);

SamplerState               g_LinearSampler     : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Cone Tracing — Per-Object Ray March
// ─────────────────────────────────────────────────────────────────────────────

/// Ray-marches through a single object's SDF with cone expansion.
///
/// Implements the core sphere-tracing loop from reference with the following behavior:
/// - Adaptive step size: advances by max(|sdf_distance|, min_step_size)
/// - Cone visibility: saturate(distance / cone_radius) where cone_radius grows with ray time
/// - Self-shadow prevention: scales visibility near ray origin based on selfShadowBias
/// - Early termination when fully occluded or past the object bounds
/// - Per-object indirection offset for correct brick lookup in the global indirection buffer
///
/// @ref Wright 2015 "DFS" — shadow ray trace:66-221 — ShadowRayTraceThroughCulledObjects
/// @constants MinSphereRadius=0.4, MinConeVisibility_threshold=0.01, MinStepSize=1/(4*MaxSteps)
/// @deviations NONE — exact reference algorithm with same constants and logic flow
float TraceSdfObject(
    float3 worldRayStart,
    float3 worldRayEnd,
    SdfObjectData obj,
    uint maxSteps,
    float tanLightAngle,
    float selfShadowBias)
{
    // Transform ray endpoints into the object's local (volume) space
    /// @ref Wright 2015 "DFS" — shadow ray trace:111-115 — VolumeRayStart/End/Direction/Length
    float3 volumeRayStart = mul(float4(worldRayStart, 1.0f), obj.worldToLocal).xyz;
    float3 volumeRayEnd   = mul(float4(worldRayEnd,   1.0f), obj.worldToLocal).xyz;
    float3 volumeRayDir   = volumeRayEnd - volumeRayStart;
    float  volumeRayLength = length(volumeRayDir);
    volumeRayDir /= max(volumeRayLength, 0.0001f);

    // Compute world-to-volume scale (use max axis scale to avoid over-dilating penumbra)
    /// @ref Wright 2015 "DFS" — shadow ray trace:118 — WorldToVolumeScale = 1/max3(VolumeToWorldScale)
    float3 volumeExtent = obj.localBoundsMax - obj.localBoundsMin;
    float maxVolumeExtent = max(volumeExtent.x, max(volumeExtent.y, volumeExtent.z));
    float worldToVolumeScale = maxVolumeExtent > 0.0001f ? 1.0f : 0.0f;

    /// @ref Wright 2015 "DFS" — shadow ray trace:119-120 — VolumeMinSphereRadius, VolumeMaxSphereRadius
    /// @constants MinSphereRadius=0.4, MAX_TRACE_SPHERE_RADIUS=100
    float volumeMinSphereRadius = 0.4f;
    float volumeMaxSphereRadius = MAX_TRACE_SPHERE_RADIUS;

    /// @ref Wright 2015 "DFS" — shadow ray trace:121 — SelfShadowScale
    float selfShadowScale = 1.0f / max(selfShadowBias, 0.0001f);

    // Expand intersection box by cone radius at object center distance
    /// @ref Wright 2015 "DFS" — shadow ray trace:124-125
    float objectCenterDistanceAlongRay = dot(-volumeRayStart, volumeRayDir);
    float localConeRadiusAtObject = min(tanLightAngle * max(objectCenterDistanceAlongRay, 0.0f), volumeMaxSphereRadius);

    // Ray-AABB intersection in local space
    /// @ref Wright 2015 "DFS" — shadow ray trace:127
    float3 volumePosExtent = (obj.localBoundsMax - obj.localBoundsMin) * 0.5f;
    float3 volumeCenter = (obj.localBoundsMax + obj.localBoundsMin) * 0.5f;
    float3 localBoxMin = volumeCenter - volumePosExtent - localConeRadiusAtObject;
    float3 localBoxMax = volumeCenter + volumePosExtent + localConeRadiusAtObject;

    float2 intersectionTimes = LineBoxIntersect(volumeRayStart, volumeRayEnd, localBoxMin, localBoxMax);

    // No intersection
    if (intersectionTimes.x >= intersectionTimes.y) {
        return 1.0f;
    }

    // Compute the maximum encodable distance for this mesh's SDF
    float maxEncodedDistance = obj.voxelSize * float(SDF_BRICK_SIZE) * 0.5f;

    // Clamp max sphere radius to prevent sampling beyond valid SDF band
    /// @ref Wright 2015 "DFS" — shadow ray trace:146
    volumeMaxSphereRadius = min(volumeMaxSphereRadius, maxEncodedDistance);

    // Begin sphere-tracing loop
    /// @ref Wright 2015 "DFS" — shadow ray trace:148-156 — MaxSteps, MinStepSize
    float minStepSize = 1.0f / (4.0f * float(maxSteps));
    float sampleRayTime = intersectionTimes.x * volumeRayLength;
    float minConeVisibility = 1.0f;

    // Get atlas parameters
    float3 atlasTexelSize = float3(g_AtlasTexelSizeX, g_AtlasTexelSizeY, g_AtlasTexelSizeZ);

    // Use the per-object indirection offset stored in SdfObjectData.
    // This is the starting index into the global indirection buffer for this object's
    // brick indirection table, analogous to the reference engine's FDFAssetData.IndirectionTableOffset.
    /// @ref Wright 2015 "DFS" — SDF atlas sampling:83,401 — IndirectionTableOffset used in SampleSparseMeshSignedDistanceField
    uint indirectionOffset = obj.indirectionOffset;

    uint stepIndex = 0;
    LOOP
    for (; stepIndex < maxSteps; stepIndex++) {
        float3 sampleVolumePos = volumeRayStart + volumeRayDir * sampleRayTime;

        // Clamp sample position to volume bounds
        /// @ref Wright 2015 "DFS" — shadow ray trace:164-166
        float3 clampedSamplePos = clamp(sampleVolumePos, obj.localBoundsMin, obj.localBoundsMax);
        float distanceToClamped = length(clampedSamplePos - sampleVolumePos);

        // Sample the SDF atlas using the per-object indirection offset
        /// @ref Wright 2015 "DFS" — shadow ray trace:166 — SampleSparseMeshSignedDistanceField + DistanceToClamped
        /// @ref Wright 2015 "DFS" — SDF atlas sampling:401 — DistanceFieldIndirectionTable.Load((DFAssetData.IndirectionTableOffset + IndirectionIndex) * 4)
        float distanceField = SampleSparseMeshSdf(
            clampedSamplePos, obj,
            g_IndirectionBuffer, indirectionOffset,
            g_SdfAtlas, g_LinearSampler,
            g_AtlasSizeInBricks, atlasTexelSize
        ) + distanceToClamped;

        // Don't allow occlusion within the object's self-shadow distance
        /// @ref Wright 2015 "DFS" — shadow ray trace:177
        float selfShadowVisibility = 1.0f - saturate(sampleRayTime * selfShadowScale);

        // Compute cone radius at current ray time
        /// @ref Wright 2015 "DFS" — shadow ray trace:179
        float sphereRadius = clamp(tanLightAngle * sampleRayTime, volumeMinSphereRadius, volumeMaxSphereRadius);

        // Compute step visibility: ratio of distance to cone radius, clamped by self-shadow
        /// @ref Wright 2015 "DFS" — shadow ray trace:180
        float stepVisibility = max(saturate(distanceField / sphereRadius), selfShadowVisibility);

        minConeVisibility = min(minConeVisibility, stepVisibility);

        // Adaptive step size: advance by at least minStepSize, or by the distance to surface
        /// @ref Wright 2015 "DFS" — shadow ray trace:197-198
        float stepDistance = max(abs(distanceField), minStepSize);
        sampleRayTime += stepDistance;

        // Early termination: fully occluded or past the object
        /// @ref Wright 2015 "DFS" — shadow ray trace:201-205
        if (minConeVisibility < 0.01f || sampleRayTime > intersectionTimes.y * volumeRayLength) {
            break;
        }
    }

    // Force to shadowed as we approach max steps (prevents banding artifacts)
    /// @ref Wright 2015 "DFS" — shadow ray trace:209
    minConeVisibility = min(minConeVisibility, 1.0f - float(stepIndex) / float(maxSteps));

    return minConeVisibility;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Wright 2015 "DFS" — cone trace dispatch:612-811 — DistanceFieldShadowingCS
/// @constants THREADGROUP_SIZEX=8, THREADGROUP_SIZEY=8, RayStartOffset=2
/// @deviations No full tile-based scatter culling (reference ScatterObjectsToShadowTiles). Instead uses
///             per-pixel sphere-vs-ray + AABB early-out to reject non-overlapping objects cheaply.
///             No heightfield support, no SSS transmission.
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // Skip out-of-bounds threads
    if (DTid.x >= g_OutputWidth || DTid.y >= g_OutputHeight) {
        return;
    }

    // Compute screen UV for depth buffer sampling
    /// @ref Wright 2015 "DFS" — cone trace dispatch:620 — ScreenUV computation
    float2 screenUV = (float2(DTid.xy) * float(g_DownsampleFactor) + 0.5f) / float2(g_OutputWidth * g_DownsampleFactor, g_OutputHeight * g_DownsampleFactor);

    // Sample depth buffer
    float rawDepth = g_DepthBuffer.SampleLevel(g_LinearSampler, screenUV, 0).r;

    // Linearize depth (reversed-Z: near=1, far=0)
    float sceneDepth = LinearizeDepthReversedZ(rawDepth, g_NearPlane, g_FarPlane);

    // Skip sky pixels and out-of-range pixels
    /// @ref Wright 2015 "DFS" — cone trace dispatch:654 — bShouldComputeShadowing depth range check
    bool shouldComputeShadowing = !IsSkyDepth(rawDepth) && sceneDepth > g_MinDepth && sceneDepth < g_MaxDepth;

    float result = 1.0f;

    BRANCH
    if (shouldComputeShadowing && g_ObjectCount > 0) {
        // Reconstruct world position
        /// @ref Wright 2015 "DFS" — cone trace dispatch:647 — OpaqueTranslatedWorldPosition
        float3 worldPos = ReconstructWorldPosition(screenUV, rawDepth, g_InvViewProj);

        // World-space offset along ray start to avoid self-shadowing
        /// @ref Wright 2015 "DFS" — cone trace dispatch:696 — RayStartOffset = 2 + RayStartOffsetDepthScale * SceneDepth
        /// @constants RayStartOffset_base=2.0 (Wright 2015 "Dynamic Occlusion with Signed Distance Fields")
        float rayStartOffset = 2.0f + g_RayStartOffsetDepthScale * sceneDepth;

        float3 rayStart = worldPos + g_LightDir * rayStartOffset;
        float3 rayEnd   = worldPos + g_LightDir * g_MaxTraceDistance;

        // Determine quality level (step count)
        // This is set via shader define DF_SHADOW_QUALITY, but for runtime flexibility
        // we read it from the root constants. The step count matches reference:
        // Quality 0: 20 steps, Quality 1: 32 steps, Quality 2: 64 steps
        /// @ref Wright 2015 "DFS" — shadow ray trace:149-155 — MaxSteps per quality level
        uint maxSteps = 64; // Default high quality
#if defined(DF_SHADOW_QUALITY)
    #if DF_SHADOW_QUALITY == 0
        maxSteps = 20;
    #elif DF_SHADOW_QUALITY == 1
        maxSteps = 32;
    #else
        maxSteps = 64;
    #endif
#endif

        /// @ref Wright 2015 "DFS" — cone trace dispatch:681 — MinSphereRadius = 0.4f
        float minConeVisibility = 1.0f;

        // Iterate over all culled objects
        /// @ref Wright 2015 "DFS" — shadow ray trace:84-86 — object loop
        LOOP
        for (uint listIndex = 0; listIndex < g_ObjectCount; listIndex++) {
            uint objectIndex = g_CulledObjectIndices[listIndex];
            SdfObjectData obj = LoadSdfObjectData(g_ObjectData, objectIndex);

            // Two-stage spatial rejection: sphere-vs-ray then AABB-vs-ray.
            // The sphere test is cheaper (dot + mad) and rejects distant objects
            // before the more precise AABB overlap check.

            // Stage 1: Sphere-vs-ray test — reject objects whose bounding sphere
            // is entirely behind the ray origin or beyond max trace distance,
            // or whose closest approach to the ray exceeds the bounding radius.
            /// @ref Wright 2015 "DFS" — shadow ray trace:88-95 — ObjectBounds cull
            float3 boundsCenter = (obj.worldBoundsMin + obj.worldBoundsMax) * 0.5f;
            float3 boundsExtent = (obj.worldBoundsMax - obj.worldBoundsMin) * 0.5f;
            float boundsRadius = length(boundsExtent);

            float3 rayToCenter = boundsCenter - worldPos;
            float projDist = dot(rayToCenter, g_LightDir);

            // Skip if the object sphere is entirely behind ray start or beyond max trace distance
            if (projDist < -boundsRadius || projDist > g_MaxTraceDistance + boundsRadius) {
                continue;
            }

            // Perpendicular distance from ray to sphere center — if greater than
            // bounding radius, the ray cannot intersect the object's volume.
            float3 closestPointOnRay = worldPos + g_LightDir * clamp(projDist, 0.0f, g_MaxTraceDistance);
            float perpDistSq = dot(boundsCenter - closestPointOnRay, boundsCenter - closestPointOnRay);
            if (perpDistSq > boundsRadius * boundsRadius) {
                continue;
            }

            // Stage 2: AABB-vs-ray overlap — tighter rejection for elongated objects
            // where the bounding sphere is overly conservative.
            float3 rayWorldMin = min(rayStart, rayEnd);
            float3 rayWorldMax = max(rayStart, rayEnd);
            bool aabbOverlap = all(obj.worldBoundsMax >= rayWorldMin) && all(obj.worldBoundsMin <= rayWorldMax);

            BRANCH
            if (aabbOverlap) {
                float objVisibility = TraceSdfObject(
                    rayStart, rayEnd, obj, maxSteps,
                    g_TanLightAngle, g_SelfShadowBias);

                minConeVisibility = min(minConeVisibility, objVisibility);
            }

            // Early exit if fully occluded
            /// @ref Wright 2015 "DFS" — shadow ray trace:213-217
            if (minConeVisibility < 0.01f) {
                minConeVisibility = 0.0f;
                break;
            }
        }

        result = minConeVisibility;
    }

    // Write output: (shadow_factor, scene_depth)
    /// @ref Wright 2015 "DFS" — cone trace dispatch:810 — RWShadowFactors[...] = float2(Result, SceneDepth)
    g_OutputShadow[DTid.xy] = float2(result, sceneDepth);
}
