/// Distance Field Shadow — Object Culling Compute Shader
///
/// Frustum-culls SDF objects against the shadow convex hull (directional light
/// shadow frustum expanded by DFS distance). Objects that pass the test are
/// appended to a compacted index buffer consumed by the cone tracing shader.
///
/// uses a convex hull test with up to 12 planes (the shadow frustum).
/// We implement the same approach: test each object's bounding sphere against
/// all hull planes. If the sphere is entirely behind any plane, it is culled.
///
/// # Inputs
/// - Object data buffer (t0): StructuredBuffer<float4> of SdfObjectGpuData
///   (only worldBoundsMin/Max and sphere radius are needed for culling)
///
/// # Outputs
/// - Culled object indices (u0): AppendStructuredBuffer<uint>
/// - Indirect arguments (u1): RWByteAddressBuffer for indirect dispatch arg
///
/// # Thread Group
/// [64, 1, 1] — one thread per scene SDF object
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — shadow object culling
/// @ref Gribb & Hartmann 2001 — convex hull sphere/box intersection tests

#include "../include/dfs_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, compute root signature)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Wright 2015 "DFS" — cull dispatch:34-37,82-97 — CullObjectsForShadowCS parameters
/// @constants UPDATEOBJECTS_THREADGROUP_SIZE=64 (reference default in C++, DistanceFieldShadowing.cpp:196)
///            NumShadowHullPlanes=up to 12 (DistanceFieldShadowing.usf:36)
/// @deviations Combined convex hull + bounding sphere into single cbuffer.
///             Bound at register(b1) via SetRootCBV (kSlotPerFrame) because the data
///             exceeds the 8-DWORD root constant limit at b0.
cbuffer CullConstants : register(b1) {
    /// Shadow frustum convex hull planes (up to 12).
    /// Each plane: float4(nx, ny, nz, d) where dot(n, p) - d < 0 means inside.
    /// @ref Wright 2015 "DFS" — cull dispatch:34 — ShadowConvexHull[12]
    float4 g_ShadowConvexHull[12];

    /// Shadow bounding sphere: xyz = center, w = radius.
    /// When w > 0, objects are culled against this sphere instead of the convex hull.
    /// Used for point/spot lights.
    /// @ref Wright 2015 "DFS" — cull dispatch:35 — ShadowBoundingSphere
    float4 g_ShadowBoundingSphere;

    /// Number of valid planes in g_ShadowConvexHull.
    /// @ref Wright 2015 "DFS" — cull dispatch:36 — NumShadowHullPlanes
    uint   g_NumShadowHullPlanes;

    /// Total number of SDF objects in the scene (not yet culled).
    uint   g_TotalObjectCount;

    uint2  _pad0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

StructuredBuffer<float4>     g_ObjectData          : register(t0);
AppendStructuredBuffer<uint> g_CulledObjectIndices : register(u0);
RWByteAddressBuffer          g_IndirectArgs        : register(u1);

// ─────────────────────────────────────────────────────────────────────────────
// Convex Hull Test
// ─────────────────────────────────────────────────────────────────────────────

/// Tests a bounding sphere against the shadow frustum convex hull.
///
/// Returns true if the sphere intersects or is inside the frustum.
/// Uses the same separating-axis test as reference engines.
///
/// @ref Wright 2015 "DFS" — cull dispatch:40-54 — ShadowConvexHullIntersectSphere
/// @constants None — planes from cbuffer
/// @deviations NONE
bool ShadowConvexHullIntersectSphere(float3 sphereCenter, float sphereRadius) {
    for (uint i = 0; i < g_NumShadowHullPlanes; i++) {
        float4 plane = g_ShadowConvexHull[i];
        float planeDist = dot(plane.xyz, sphereCenter) - plane.w;

        if (planeDist > sphereRadius) {
            return false;
        }
    }
    return true;
}

/// Tests an AABB against the shadow frustum convex hull.
///
/// Projects the AABB extent onto each plane normal and checks if the box
/// is entirely behind any plane.
///
/// @ref Wright 2015 "DFS" — cull dispatch:56-71 — ShadowConvexHullIntersectBox
/// @constants None
/// @deviations NONE
bool ShadowConvexHullIntersectBox(float3 boxCenter, float3 boxExtent) {
    for (uint i = 0; i < g_NumShadowHullPlanes; i++) {
        float4 plane = g_ShadowConvexHull[i];
        float planeDist = dot(plane.xyz, boxCenter) - plane.w;
        float pushOut = dot(abs(plane.xyz), boxExtent);

        if (planeDist > pushOut) {
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Wright 2015 "DFS" — cull dispatch:82-156 — CullObjectsForShadowCS
/// @constants UPDATEOBJECTS_THREADGROUP_SIZE=64 (DistanceFieldShadowing.cpp:196)
/// @deviations Simplified: no Nanite mesh filtering, no draw distance culling,
///             no bounding geometry index count. Uses AABB test instead of
///             sphere test (more conservative, catches elongated objects better).
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint objectIndex = DTid.x;
    if (objectIndex >= g_TotalObjectCount) {
        return;
    }

    // Load object world-space AABB from the SdfObjectGpuData buffer
    // World bounds are at rows 6-7 (float4 indices 6 and 7 per 12-float4 stride)
    uint base = objectIndex * 12;
    float4 v6 = g_ObjectData[base + 6]; // worldBoundsMin + pad
    float4 v7 = g_ObjectData[base + 7]; // worldBoundsMax + atlasOffset

    float3 worldBoundsMin = v6.xyz;
    float3 worldBoundsMax = v7.xyz;

    float3 boxCenter = (worldBoundsMin + worldBoundsMax) * 0.5f;
    float3 boxExtent = (worldBoundsMax - worldBoundsMin) * 0.5f;
    float sphereRadius = length(boxExtent);

    bool intersects = false;

    // Choose culling method based on shadow bounding sphere
    /// @ref Wright 2015 "DFS" — cull dispatch:108-114 — convex hull vs bounding sphere test
    if (g_ShadowBoundingSphere.w > 0.0f) {
        // Point/spot light: sphere-sphere test
        float3 centerToSphere = g_ShadowBoundingSphere.xyz - boxCenter;
        float distSq = dot(centerToSphere, centerToSphere);
        float radiusSum = g_ShadowBoundingSphere.w + sphereRadius;
        intersects = distSq < (radiusSum * radiusSum);
    } else {
        // Directional light: convex hull test (box version for tighter culling)
        intersects = ShadowConvexHullIntersectBox(boxCenter, boxExtent);
    }

    if (intersects) {
        g_CulledObjectIndices.Append(objectIndex);

        // Atomically increment the visible object count in the indirect dispatch args.
        // The cone tracing shader reads this count to know how many objects to process.
        // Indirect args layout: [threadGroupCountX, threadGroupCountY, threadGroupCountZ]
        // We store the culled object count at byte offset 0 (repurposed for the cone tracer).
        g_IndirectArgs.InterlockedAdd(0, 1);
    }
}
