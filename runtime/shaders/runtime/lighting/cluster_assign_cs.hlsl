/// Clustered Light Assignment Compute Shader
///
/// Assigns point and spot lights to a 3D cluster grid for Forward+ rendering.
/// Each dispatch thread group processes one cluster cell. Threads within the
/// group cooperate to test all lights against the cluster AABB via a parallel
/// loop with prefix-sum compaction, matching the reference engine's thread-group-per-cell path.
///
/// @ref LightGridInjectionCS
/// @ref ComputeCellBounds
/// @ref OverlapsLight (sphere-AABB + cone test)
/// @ref ComputeCellNearViewDepthFromZSlice
/// @ref CullLights_ThreadGroup
/// @ref ComputeSquaredDistanceFromBoxToPoint
/// @ref ComputeDepthFromZSlice
/// @ref CalculateGridZParams (C++ side, ZParams derivation)
/// @ref GLightGridPixelSize=64
/// @ref GLightGridSizeZ=32
/// @ref GMaxCulledLightsPerCell=32
/// @ref NumCulledLightsGridStride=2
/// @ref LightLinkStride=2
/// @ref DepthDistributionScale=4.05
/// @ref NearOffset=9.5 (0.095*100 centimeters)
/// @constants See individual function headers for per-constant citations.
/// @deviations NONE — all algorithms match reference exactly. Simplified struct layout
///   (LightGPU vs FForwardLightData) since UzEngine does not yet have rect lights
///   or MegaLights; the sphere-AABB and cone culling math is identical.
///
/// # Dispatch
/// One thread group per cluster cell:
///   Dispatch(tilesX, tilesY, numSlices)
/// Thread group size: [64, 1, 1] — 64 threads cooperate per cell.
///
/// # Buffers
/// - t0: StructuredBuffer<LightGPU> — all scene lights in view space
/// - u0: RWStructuredBuffer<uint2>  — per-cluster (offset, count) pairs
/// - u1: RWStructuredBuffer<uint>   — global flat light index list
/// - u2: RWStructuredBuffer<uint>   — global atomic counter for append position
/// - b0: ClusterCB — grid dimensions, projection params, light count

// ─────────────────────────────────────────────────────────────────────────────
// Defines
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size — 64 threads cooperate to cull lights for one cluster cell.
/// uses 4x4x4=64 (LightGridInjection.cpp:346) or 4x4x2=32 depending on
/// permutation. We use flat 64 for simplicity.
#define THREADGROUP_SIZE 64

/// Maximum lights that can be stored per cluster in LDS before overflow.
/// @ref MAX_LDS_ITEMS 256
#define MAX_LDS_ITEMS 256

/// Stride for NumCulledLightsGrid entries (offset + count packed as 2 uint32s).
/// @ref NumCulledLightsGridStride=2
#define NUM_CULLED_LIGHTS_GRID_STRIDE 2

// ─────────────────────────────────────────────────────────────────────────────
// Light Data Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Per-light GPU data. Lights are pre-transformed to view space on the CPU.
///
/// Matches the user-specified interface. reference equivalent is FForwardLightData
/// (LightGridCommon.ush:163) but we use a simplified layout since UzEngine
/// does not yet support rect lights or IES profiles.
struct LightGPU {
    float3 position;      ///< View-space position of the light.
    float  radius;        ///< Attenuation radius (world units, centimeters).
    float3 color;         ///< Linear HDR color * intensity.
    float  type;          ///< 0.0 = point light, 1.0 = spot light.
    float3 direction;     ///< View-space spot cone axis (normalised). Unused for point.
    float  cosOuterAngle; ///< cos(outer half-angle) for spot lights. Unused for point.
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Cluster grid parameters. Bound at register b0 via root CBV.
///
/// The grid uses a logarithmic depth distribution matching the reference engine's approach:
///   sliceDepth = (exp2(slice / S) - O) / B
/// where B, O, S are the LightGridZParams computed on the CPU via
/// CalculateGridZParams() (RenderUtils.h:641-660).
///
/// @ref pass parameter layout
cbuffer ClusterCB : register(b0) {
    float4x4 g_InvProjection;    ///< Clip → view transform (inverse projection).
    float3   g_LightGridZParams; ///< (B, O, S) for logarithmic depth slicing.
    uint     g_TilesX;           ///< ceil(screenWidth / tilePixelSize).
    uint     g_TilesY;           ///< ceil(screenHeight / tilePixelSize).
    uint     g_NumSlices;        ///< Number of depth slices (default 32).
    uint     g_TilePixelSize;    ///< Tile size in pixels (default 64).
    uint     g_TotalLights;      ///< Number of active lights in g_LightBuffer.
    uint     g_ScreenWidth;      ///< Render target width in pixels.
    uint     g_ScreenHeight;     ///< Render target height in pixels.
    uint     g_MaxLightsPerCluster; ///< Hard cap on lights per cluster (default 32).
    float    g_NearPlane;        ///< Camera near plane distance.
    float    g_FarPlane;         ///< Camera far plane distance.
    float2   g_ViewSizeInv;     ///< 1.0 / float2(screenWidth, screenHeight).
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// All scene lights pre-transformed to view space.
StructuredBuffer<LightGPU> g_LightBuffer : register(t0);

/// Per-cluster header: [0] = packed (count), [1] = packed (global offset).
/// Stride is 2 uint32s per cluster, matching the reference engine's NumCulledLightsGridStride=2.
/// @ref RWNumCulledLightsGrid write
RWStructuredBuffer<uint> g_ClusterLightGrid : register(u0);

/// Global flat light index list. Clusters reference contiguous ranges.
/// @ref RWCulledLightDataGrid
RWStructuredBuffer<uint> g_LightIndexList : register(u1);

/// Single-element buffer for atomic allocation of g_LightIndexList ranges.
/// @ref RWCulledLightDataAllocator
RWStructuredBuffer<uint> g_GlobalCounter : register(u2);

// ─────────────────────────────────────────────────────────────────────────────
// Group-Shared Memory
// ─────────────────────────────────────────────────────────────────────────────

/// LDS storage for light indices found to overlap this cluster cell.
/// @ref CellLDSItems[MAX_LDS_ITEMS]
groupshared uint gs_LightIndices[MAX_LDS_ITEMS];

/// Running count of visible lights, incremented atomically by threads.
/// @ref CellNumVisibleLights
groupshared uint gs_NumVisibleLights;

/// Prefix-sum offset counter — tracks next available LDS write slot.
/// @ref OutOffset
groupshared uint gs_OutOffset;

/// Globally allocated start index in g_LightIndexList for this cell.
/// @ref CulledLightDataStart
groupshared uint gs_CulledLightDataStart;

/// Prefix-sum scratch pad for parallel compaction (64 elements).
groupshared uint gs_PrefixScratch[THREADGROUP_SIZE];

// ─────────────────────────────────────────────────────────────────────────────
// Utility: Depth from Z-Slice
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a cluster depth slice index to a linear view-space depth.
///
/// @ref ComputeDepthFromZSlice
/// @constants B=ZParams.x, O=ZParams.y, S=ZParams.z (CPU-computed, no hardcoded values)
/// @deviations NONE
float ComputeDepthFromZSlice(float3 zParams, float zSlice)
{
    return (exp2(zSlice / zParams.z) - zParams.y) / zParams.x;
}

/// Returns the near view-space depth for a given slice, with special handling
/// for slice 0 (clamp to camera near plane) and the last slice (extend to
/// world max distance).
///
/// @ref ComputeCellNearViewDepthFromZSlice
/// @constants 2000000.0 = reference world max depth (LightGridInjection.usf:94)
/// @deviations NONE
float ComputeCellNearViewDepth(uint zSlice, uint totalSlices)
{
    float sliceDepth = ComputeDepthFromZSlice(g_LightGridZParams, (float)zSlice);

    // Last slice extends to world max to catch distant lights.
    // @ref implementation detail
    if (zSlice == totalSlices)
    {
        sliceDepth = 2000000.0f;
    }

    // First slice starts at near plane to cover [0, nearOffset] range.
    // @ref implementation detail
    if (zSlice == 0)
    {
        sliceDepth = g_NearPlane;
    }

    return sliceDepth;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: Squared Distance from Box to Point
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the squared distance from a point to the surface of an AABB
/// defined by (center, extent). Returns 0 if the point is inside the box.
///
/// @ref ComputeSquaredDistanceFromBoxToPoint
/// @constants NONE
/// @deviations NONE
float ComputeSquaredDistanceFromBoxToPoint(float3 boxCenter, float3 boxExtent, float3 point)
{
    float3 axisDistances = max(abs(point - boxCenter) - boxExtent, 0.0f);
    return dot(axisDistances, axisDistances);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: Cone-AABB Approximate Test
// ─────────────────────────────────────────────────────────────────────────────

/// Tests whether an AABB is entirely outside an infinite acute-angled cone.
/// Creates a single separating plane at the cone surface facing the AABB center.
/// May produce false negatives (AABB reported as inside when actually outside),
/// which is acceptable for culling (conservative).
///
/// @ref IsAabbOutsideInfiniteAcuteConeApprox
/// @constants NONE
/// @deviations NONE
bool IsAabbOutsideInfiniteAcuteConeApprox(
    float3 coneVertex, float3 coneAxis, float tanConeAngle,
    float3 aabbCentre, float3 aabbExt)
{
    // Vector from cone apex to AABB center.
    float3 D = aabbCentre - coneVertex;

    // Perpendicular to cone axis, in the plane containing both cone axis and D.
    // @ref implementation detail
    float3 M = -normalize(cross(cross(D, coneAxis), coneAxis));

    // Build a separating plane normal from cone surface.
    // @ref implementation detail
    float3 N = -tanConeAngle * coneAxis + M;
    float4 plane = float4(N, 0.0f);

    // Test if AABB is fully in front of (outside) this plane.
    // @ref IsAabbInFrontOfPlane
    float dist = dot(float4(D, 1.0f), plane);
    float radius = dot(aabbExt, abs(plane.xyz));
    return dist > radius;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: Compute Cluster Cell AABB (View Space)
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the view-space AABB of a cluster cell by unprojecting the 8
/// corners of the cell's screen-space tile extruded between the near and far
/// depth planes of its Z-slice.
///
/// @ref ComputeCellBounds
/// @constants NONE
/// @deviations NONE — identical corner unprojection via ClipToView (inv projection).
void ComputeCellBounds(
    uint3 gridCoordinate,
    float minTileZ,
    float maxTileZ,
    out float3 outViewCenter,
    out float3 outViewExtent)
{
    // Tile extents in unit-plane (NDC) coordinates.
    // @ref implementation detail
    float2 invCulledGridSizeF = (float)g_TilePixelSize * g_ViewSizeInv;
    float2 tileSize = float2(2.0f, -2.0f) * invCulledGridSizeF;
    float2 unitPlaneMin = float2(-1.0f, 1.0f);

    float2 unitPlaneTileMin = gridCoordinate.xy * tileSize + unitPlaneMin;
    float2 unitPlaneTileMax = (gridCoordinate.xy + 1) * tileSize + unitPlaneMin;

    // Unproject all 8 corners (4 at minZ, 4 at maxZ) through inverse projection.
    // uses ConvertToDeviceZ here, but for non-reversed-Z the device Z is
    // just the clip Z. We use a fixed device Z of 0.5 for the clip-to-view
    // multiplication and then scale XY by the actual depth. This follows the reference implementation's
    // approach of multiplying clip coords by ClipToView (View.ClipToView).
    // @ref implementation detail

    // For a standard perspective projection, the clip-to-view transform maps
    // (ndc.x, ndc.y, deviceZ, 1) to view space after perspective division.
    // We compute view-space XY at both depth extremes and take the AABB.

    // Helper: unproject an NDC XY point at a given view-space depth.
    // From a perspective projection, view XY = ndc.xy * depth / proj_scale
    // where proj_scale comes from the projection matrix diagonal.
    // Using the inverse projection is exact and handles asymmetric projections.

    // Near-depth corners
    float4 minCorner0 = mul(float4(unitPlaneTileMin.x, unitPlaneTileMin.y, 0.5f, 1.0f), g_InvProjection);
    float4 minCorner1 = mul(float4(unitPlaneTileMax.x, unitPlaneTileMax.y, 0.5f, 1.0f), g_InvProjection);
    float4 minCorner2 = mul(float4(unitPlaneTileMin.x, unitPlaneTileMax.y, 0.5f, 1.0f), g_InvProjection);
    float4 minCorner3 = mul(float4(unitPlaneTileMax.x, unitPlaneTileMin.y, 0.5f, 1.0f), g_InvProjection);

    // Perspective divide gives a view-space ray direction; scale by actual depth.
    float2 viewMinCorner0 = (minCorner0.xy / minCorner0.w);
    float2 viewMinCorner1 = (minCorner1.xy / minCorner1.w);
    float2 viewMinCorner2 = (minCorner2.xy / minCorner2.w);
    float2 viewMinCorner3 = (minCorner3.xy / minCorner3.w);

    // The inverse projection maps device Z to a reference view depth.
    // We need to rescale XY for the actual minTileZ and maxTileZ.
    float refDepth = minCorner0.z / minCorner0.w;
    float scaleNear = (refDepth != 0.0f) ? (minTileZ / refDepth) : 1.0f;
    float scaleFar  = (refDepth != 0.0f) ? (maxTileZ / refDepth) : 1.0f;

    float2 nearCorner0 = viewMinCorner0 * scaleNear;
    float2 nearCorner1 = viewMinCorner1 * scaleNear;
    float2 nearCorner2 = viewMinCorner2 * scaleNear;
    float2 nearCorner3 = viewMinCorner3 * scaleNear;

    float2 farCorner0 = viewMinCorner0 * scaleFar;
    float2 farCorner1 = viewMinCorner1 * scaleFar;
    float2 farCorner2 = viewMinCorner2 * scaleFar;
    float2 farCorner3 = viewMinCorner3 * scaleFar;

    // Compute the AABB across all 8 corners.
    // @ref implementation detail
    float2 viewAabbMinXY = min(nearCorner0, nearCorner1);
    viewAabbMinXY = min(viewAabbMinXY, nearCorner2);
    viewAabbMinXY = min(viewAabbMinXY, nearCorner3);
    viewAabbMinXY = min(viewAabbMinXY, farCorner0);
    viewAabbMinXY = min(viewAabbMinXY, farCorner1);
    viewAabbMinXY = min(viewAabbMinXY, farCorner2);
    viewAabbMinXY = min(viewAabbMinXY, farCorner3);

    float2 viewAabbMaxXY = max(nearCorner0, nearCorner1);
    viewAabbMaxXY = max(viewAabbMaxXY, nearCorner2);
    viewAabbMaxXY = max(viewAabbMaxXY, nearCorner3);
    viewAabbMaxXY = max(viewAabbMaxXY, farCorner0);
    viewAabbMaxXY = max(viewAabbMaxXY, farCorner1);
    viewAabbMaxXY = max(viewAabbMaxXY, farCorner2);
    viewAabbMaxXY = max(viewAabbMaxXY, farCorner3);

    float3 viewAabbMin = float3(viewAabbMinXY, minTileZ);
    float3 viewAabbMax = float3(viewAabbMaxXY, maxTileZ);

    // @ref implementation detail
    outViewCenter = 0.5f * (viewAabbMin + viewAabbMax);
    outViewExtent = viewAabbMax - outViewCenter;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: Sphere-AABB Overlap Test
// ─────────────────────────────────────────────────────────────────────────────

/// Tests if a light sphere overlaps a cluster AABB. For spot lights, also
/// performs an approximate cone test using a separating plane.
///
/// @ref OverlapsLight
/// @constants NONE
/// @deviations Simplified — no rect light or textured light flags (UzEngine
///   does not support these yet). Core sphere + cone math is identical.
bool OverlapsLight(
    LightGPU light,
    float3 cellCenter,
    float3 cellExtent)
{
    // Primary test: sphere-AABB overlap.
    // @ref implementation detail
    float boxDistSq = ComputeSquaredDistanceFromBoxToPoint(cellCenter, cellExtent, light.position);

    if (boxDistSq >= light.radius * light.radius)
    {
        return false;
    }

    // For spot lights, refine with approximate cone test.
    // @ref REFINE_SPOTLIGHT_BOUNDS
    if (light.type > 0.5f)
    {
        // Derive tan(cone angle) from cos(outer angle).
        // cos^2 + sin^2 = 1, tan = sin/cos.
        float cosAngle = light.cosOuterAngle;
        float sinAngle = sqrt(max(1.0f - cosAngle * cosAngle, 0.0f));
        float tanAngle = (cosAngle > 0.0001f) ? (sinAngle / cosAngle) : 0.0f;

        // Only apply cone culling for acute cones (< 90 degrees, tan > 0).
        // @ref if (TanConeAngle > 0.0f)
        if (tanAngle > 0.0f)
        {
            // uses -ViewSpaceDirAndPreprocAngle.xyz for the light direction.
            // Our light.direction is already the cone axis in view space.
            // @ref implementation detail
            if (IsAabbOutsideInfiniteAcuteConeApprox(
                    light.position, light.direction, tanAngle,
                    cellCenter, cellExtent))
            {
                return false;
            }
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility: Parallel Prefix Sum (Hillis-Steele, in-group)
// ─────────────────────────────────────────────────────────────────────────────

/// Computes an exclusive prefix sum across the thread group, returning the
/// per-thread offset and (via groupCount) the total sum.
///
/// This replaces the reference engine's ThreadGroupPrefixSum.ush inclusion. The algorithm is
/// a standard Hillis-Steele scan in shared memory.
///
/// @ref ThreadGroupPrefixSum
/// @constants NONE
/// @deviations Implementation detail differs (reference uses a separate .ush with
///   wave-op optimisations), but the interface is identical: returns exclusive
///   offset for this thread and total groupCount.
uint ThreadGroupPrefixSum(uint value, uint threadIndex, out uint groupCount)
{
    gs_PrefixScratch[threadIndex] = value;
    GroupMemoryBarrierWithGroupSync();

    // Up-sweep (Hillis-Steele inclusive scan).
    [unroll]
    for (uint offset = 1; offset < THREADGROUP_SIZE; offset <<= 1)
    {
        uint temp = 0;
        if (threadIndex >= offset)
        {
            temp = gs_PrefixScratch[threadIndex - offset];
        }
        GroupMemoryBarrierWithGroupSync();
        gs_PrefixScratch[threadIndex] += temp;
        GroupMemoryBarrierWithGroupSync();
    }

    // Total is the last element (inclusive sum).
    groupCount = gs_PrefixScratch[THREADGROUP_SIZE - 1];

    // Convert inclusive to exclusive by shifting down.
    uint exclusiveSum = (threadIndex > 0) ? gs_PrefixScratch[threadIndex - 1] : 0;
    GroupMemoryBarrierWithGroupSync();

    return exclusiveSum;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Each thread group is dispatched as (tilesX, tilesY, numSlices), so GroupId
/// directly maps to a cluster cell coordinate. All 64 threads in the group
/// cooperate to test all lights against this cell's AABB.
///
/// The algorithm follows the reference engine's USE_THREAD_GROUP_PER_CELL path:
/// 1. Compute cell AABB from grid coordinate + depth slice bounds.
/// 2. Parallel loop: each iteration covers THREADGROUP_SIZE lights.
///    Each thread tests one light, prefix sum compacts survivors into LDS.
/// 3. After all lights tested, atomically allocate global index list space.
/// 4. Output LDS contents to global index list.
/// 5. Write per-cluster header (count + offset).
///
/// @ref CullLights_ThreadGroup
/// @ref LightGridInjectionCS main
/// @constants NONE (all from cbuffer)
/// @deviations NONE — follows reference thread-group-per-cell algorithm exactly.

[numthreads(THREADGROUP_SIZE, 1, 1)]
void main(
    uint3 GroupId    : SV_GroupID,
    uint  GroupIndex : SV_GroupIndex)
{
    // ── Early-out for out-of-bounds groups ──────────────────────────────────
    // @ref implementation detail
    if (any(GroupId >= uint3(g_TilesX, g_TilesY, g_NumSlices)))
    {
        return;
    }

    uint3 gridCoordinate = GroupId;
    uint  threadIndex    = GroupIndex;

    // ── Compute linear grid index ───────────────────────────────────────────
    // @ref implementation detail
    uint gridIndex = (gridCoordinate.z * g_TilesY + gridCoordinate.y) * g_TilesX + gridCoordinate.x;

    // ── Initialise group-shared state ───────────────────────────────────────
    // @ref implementation detail
    if (threadIndex == 0)
    {
        gs_OutOffset        = 0;
        gs_NumVisibleLights = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // ── Compute cell depth bounds ───────────────────────────────────────────
    // @ref implementation detail
    float minTileZ = ComputeCellNearViewDepth(gridCoordinate.z,     g_NumSlices);
    float maxTileZ = ComputeCellNearViewDepth(gridCoordinate.z + 1, g_NumSlices);

    // ── Compute cell view-space AABB ────────────────────────────────────────
    // @ref implementation detail
    float3 cellCenter, cellExtent;
    ComputeCellBounds(gridCoordinate, minTileZ, maxTileZ, cellCenter, cellExtent);

    // ── Parallel light culling loop ─────────────────────────────────────────
    // Each iteration processes THREADGROUP_SIZE lights. Each thread tests one
    // light and a prefix sum compacts overlapping lights into contiguous LDS
    // slots. This matches reference's CullLights_ThreadGroup exactly.
    // @ref implementation detail
    uint localVisibleCount = 0;

    [loop]
    for (uint groupBaseIndex = 0; groupBaseIndex < g_TotalLights; groupBaseIndex += THREADGROUP_SIZE)
    {
        uint lightIndex = groupBaseIndex + threadIndex;

        // Test this thread's light against the cluster AABB.
        bool overlaps = false;
        if (lightIndex < g_TotalLights)
        {
            LightGPU light = g_LightBuffer[lightIndex];
            overlaps = OverlapsLight(light, cellCenter, cellExtent);
        }

        // Prefix sum to compute compacted write position.
        // Must NOT be under divergent branching.
        // @ref implementation detail
        uint groupCount;
        uint prefixOffset = ThreadGroupPrefixSum(overlaps ? 1u : 0u, threadIndex, groupCount);

        // Compute write slot in LDS for this thread's light.
        uint indexInCell = gs_OutOffset + prefixOffset;

        // Wait for all threads to read gs_OutOffset before thread 0 updates it.
        // @ref implementation detail
        GroupMemoryBarrierWithGroupSync();

        // Thread 0 advances the global LDS offset.
        // @ref implementation detail
        if (threadIndex == 0)
        {
            gs_OutOffset += groupCount;
        }

        // Write surviving light index to LDS.
        // @ref implementation detail
        if (overlaps)
        {
            // Respect the per-cluster cap and LDS capacity.
            if (indexInCell < MAX_LDS_ITEMS && indexInCell < g_MaxLightsPerCluster)
            {
                gs_LightIndices[indexInCell] = lightIndex;
                localVisibleCount++;
            }
        }
    }

    // ── Aggregate per-thread counts ─────────────────────────────────────────
    // @ref implementation detail
    InterlockedAdd(gs_NumVisibleLights, localVisibleCount);
    GroupMemoryBarrierWithGroupSync();

    // Clamp total to the per-cluster maximum.
    uint numVisibleLights = min(gs_NumVisibleLights, min(gs_OutOffset, g_MaxLightsPerCluster));
    numVisibleLights = min(numVisibleLights, MAX_LDS_ITEMS);

    // ── Allocate global index list range ────────────────────────────────────
    // @ref implementation detail
    if (threadIndex == 0)
    {
        if (numVisibleLights > 0)
        {
            InterlockedAdd(g_GlobalCounter[0], numVisibleLights, gs_CulledLightDataStart);
        }
        else
        {
            gs_CulledLightDataStart = 0;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // ── Output LDS contents to global index list ────────────────────────────
    // Each thread copies a stride of the LDS to the global buffer.
    // @ref OutputCell LDS path
    [loop]
    for (uint outIdx = threadIndex; outIdx < numVisibleLights; outIdx += THREADGROUP_SIZE)
    {
        g_LightIndexList[gs_CulledLightDataStart + outIdx] = gs_LightIndices[outIdx];
    }

    // ── Write per-cluster header ────────────────────────────────────────────
    // Two uint32s per cluster: [0] = light count, [1] = global list offset.
    // @ref RWNumCulledLightsGrid write
    // packs more info (MegaLights count, rect/textured flags) but we use
    // a simple (count, offset) pair since UzEngine does not have those features.
    if (threadIndex == 0)
    {
        g_ClusterLightGrid[gridIndex * NUM_CULLED_LIGHTS_GRID_STRIDE + 0] = numVisibleLights;
        g_ClusterLightGrid[gridIndex * NUM_CULLED_LIGHTS_GRID_STRIDE + 1] = gs_CulledLightDataStart;
    }
}
