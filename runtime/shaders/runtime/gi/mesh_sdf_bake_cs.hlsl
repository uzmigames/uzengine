/// UzEngine Mesh SDF Bake Compute Shader
///
/// Bakes a signed distance field for a single mesh on the GPU.
/// Each thread computes the signed distance for ONE voxel in a 3D grid by
/// iterating all triangles, finding the closest point on each, and writing
/// the minimum signed distance to a RWTexture3D<float>.
///
/// Brute-force O(V*T) per-voxel where V = voxels, T = triangles.
/// Acceptable for small grids (32^3 = 32768 voxels). For large meshes with
/// thousands of triangles, the GPU parallelism across voxels keeps wall time
/// reasonable (each thread independently scans all triangles).
///
/// @source Real-Time Collision Detection, Ch 5.1.5 — ClosestPointOnTriangle
/// @ref Wright 2015 "Dynamic Occlusion with SDFs" — mesh distance field generation
/// @deviations Brute-force per-voxel O(V*T) — acceptable for 32^3 grids on GPU.
///   the reference uses a spatial acceleration structure (BVH) for the triangle query;
///   we trade that complexity for simplicity at small grid sizes.

// ─────────────────────────────────────────────────────────────────────────────
// Root constants — bake parameters (bound at b0, root param [0])
// Exactly 8 uint32s = 32 bytes to match the root signature layout.
// ─────────────────────────────────────────────────────────────────────────────

cbuffer RootConstants : register(b0)
{
    uint  g_outputUav;      // [0] Bindless UAV index for output RWTexture3D<float>
    uint  g_trianglesSrv;   // [1] Bindless SRV index for StructuredBuffer<GPUTriangle>
    uint  g_triangleCount;  // [2] Number of triangles in the mesh
    uint  g_resolution;     // [3] Voxels per axis (e.g. 32)
    float g_volumeMinX;     // [4] Volume AABB minimum X (world space, cm)
    float g_volumeMinY;     // [5] Volume AABB minimum Y (world space, cm)
    float g_volumeMinZ;     // [6] Volume AABB minimum Z (world space, cm)
    float g_voxelSize;      // [7] Size of one voxel in world units (cm)
};

// ─────────────────────────────────────────────────────────────────────────────
// Triangle data — accessed via bindless ResourceDescriptorHeap
// ─────────────────────────────────────────────────────────────────────────────

/// Per-triangle data uploaded from CPU. Must match GpuTriangle in mesh_sdf_baker.h.
/// Each vertex is padded to float4 for GPU alignment (16-byte stride per row).
/// The face normal is precomputed on the CPU for sign determination.
/// Total stride: 4 x float4 = 64 bytes per triangle.
struct GPUTriangle
{
    float3 v0;
    float  _pad0;
    float3 v1;
    float  _pad1;
    float3 v2;
    float  _pad2;
    float3 normal;  // precomputed outward-facing face normal
    float  _pad3;
};

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
StructuredBuffer<GPUTriangle> g_Triangles : register(t0);
RWTexture3D<float>            g_OutputTex : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Closest point on triangle — Ericson's Voronoi region test
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the closest point on triangle (a, b, c) to point p.
/// Uses the Voronoi region decomposition from "Real-Time Collision Detection"
/// by Christer Ericson, Section 5.1.5 (pages 141-146).
///
/// The algorithm classifies the query point into one of 7 Voronoi regions
/// (3 vertex, 3 edge, 1 face) using barycentric coordinate signs, then
/// projects onto the corresponding feature. All 7 regions are tested
/// exhaustively with no early-out shortcuts that could miss edge cases.
///
/// @source Real-Time Collision Detection, Ch 5.1.5 — ClosestPtPointTriangle
/// @deviations NONE
float3 ClosestPointOnTriangle(float3 p, float3 a, float3 b, float3 c)
{
    float3 ab = b - a;
    float3 ac = c - a;
    float3 ap = p - a;

    float d1 = dot(ab, ap);
    float d2 = dot(ac, ap);

    // Region A: vertex a — closest when projection onto both edges is negative
    if (d1 <= 0.0f && d2 <= 0.0f)
    {
        return a;
    }

    float3 bp = p - b;
    float d3 = dot(ab, bp);
    float d4 = dot(ac, bp);

    // Region B: vertex b — closest when past b along ab and ac projection confirms
    if (d3 >= 0.0f && d4 <= d3)
    {
        return b;
    }

    // Region AB: edge ab — barycentric coordinate vc is negative
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        float v = d1 / (d1 - d3);
        return a + ab * v;
    }

    float3 cp = p - c;
    float d5 = dot(ab, cp);
    float d6 = dot(ac, cp);

    // Region C: vertex c — closest when past c along ac
    if (d6 >= 0.0f && d5 <= d6)
    {
        return c;
    }

    // Region AC: edge ac — barycentric coordinate vb is negative
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        float w = d2 / (d2 - d6);
        return a + ac * w;
    }

    // Region BC: edge bc — barycentric coordinate va is negative
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;
    }

    // Region ABC: interior of triangle — project onto face using barycentric coords
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main compute entry point
// ─────────────────────────────────────────────────────────────────────────────

/// Bakes the signed distance field for a single mesh.
/// Each thread processes one voxel in the output 3D texture.
/// Thread group size: 4x4x4 = 64 threads per group, matching the 3D grid.
///
/// @ref Wright 2015 — GPU mesh distance field generation (brute-force variant)
/// @deviations Brute-force O(V*T) instead of the reference engine's BVH-accelerated query.
///   For 32^3 grids with <10K triangles, GPU parallelism compensates.
[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Bounds check: skip threads outside the volume resolution
    if (any(dispatchThreadId >= g_resolution))
    {
        return;
    }

    // ── Compute world position for this voxel ────────────────────────────
    // Each voxel is centered at: volumeMin + (voxelIndex + 0.5) * voxelSize
    // The +0.5 offset places the sample at the voxel center, not the corner.
    float3 volumeMin = float3(g_volumeMinX, g_volumeMinY, g_volumeMinZ);
    float3 voxelCenter = volumeMin + (float3(dispatchThreadId) + 0.5f) * g_voxelSize;

    // ── Iterate all triangles, find closest surface point ────────────────
    // Bindless access to the triangle buffer via descriptor heap index.
#ifdef UZ_BINDLESS
    StructuredBuffer<GPUTriangle> triangles = ResourceDescriptorHeap[g_trianglesSrv];
#else
    StructuredBuffer<GPUTriangle> triangles = g_Triangles;
#endif

    // Track minimum squared distance, closest triangle index, and its face normal.
    // Using squared distance in the inner loop avoids a sqrt per triangle.
    float minDistSq      = 1e30f;
    uint  closestTriIdx  = 0u;
    float3 closestNormal = float3(0.0f, 0.0f, 1.0f);

    for (uint i = 0; i < g_triangleCount; i++)
    {
        GPUTriangle tri = triangles[i];

        // Find the closest point on this triangle to the voxel center
        float3 closest = ClosestPointOnTriangle(voxelCenter, tri.v0, tri.v1, tri.v2);

        // Squared distance comparison avoids sqrt in the hot loop
        float3 diff = voxelCenter - closest;
        float distSq = dot(diff, diff);

        // Update minimum if this triangle is closer
        if (distSq < minDistSq)
        {
            minDistSq     = distSq;
            closestTriIdx = i;
            closestNormal = tri.normal;
        }
    }

    // ── Compute signed distance ──────────────────────────────────────────
    // Unsigned distance from the closest surface point
    float unsignedDist = sqrt(minDistSq);

    // Recover the closest point on the winning triangle for sign computation.
    // We stored the triangle index during the loop so we only recompute the
    // closest point once, after the loop, rather than storing it (which would
    // add 3 registers of live state across the entire inner loop).
    GPUTriangle winTri = triangles[closestTriIdx];
    float3 closestPoint = ClosestPointOnTriangle(voxelCenter, winTri.v0, winTri.v1, winTri.v2);

    // Sign determination using the pseudonormal method:
    // The sign is positive outside the mesh (voxel on the same side as the
    // outward face normal) and negative inside. We compute:
    //   sign = dot(voxelCenter - closestPoint, faceNormal) >= 0 ? +1 : -1
    //
    // For watertight meshes with consistent winding order (counter-clockwise
    // front faces), this produces correct inside/outside classification.
    // For non-manifold or open meshes, a ray-casting parity test would be
    // more robust, but the pseudonormal method avoids the cost of casting
    // rays through the triangle set on every voxel.
    float signDot = dot(voxelCenter - closestPoint, closestNormal);
    float signVal = (signDot >= 0.0f) ? 1.0f : -1.0f;

    float signedDist = signVal * unsignedDist;

    // ── Write result ─────────────────────────────────────────────────────
    // Output texture accessed via bindless descriptor heap.
    // The texture format (R16F or R32F) handles the signed float encoding.
#ifdef UZ_BINDLESS
    RWTexture3D<float> outputTex = ResourceDescriptorHeap[g_outputUav];
#else
    RWTexture3D<float> outputTex = g_OutputTex;
#endif
    outputTex[dispatchThreadId] = signedDist;
}
