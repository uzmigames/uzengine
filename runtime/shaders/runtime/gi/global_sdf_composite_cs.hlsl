/// UzEngine Global SDF Composite Compute Shader
///
/// Composites mesh SDFs into a single global distance field clipmap volume.
/// Each thread handles one voxel: computes world position from the voxel grid,
/// iterates all mesh SDFs, transforms the world position into each mesh's local
/// space, samples the mesh SDF, and stores the global minimum distance.
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — global distance field composition
/// @ref Hart 1996 "Sphere Tracing" — SDF distance encoding/decoding
/// @constants kMaxMeshSDFs=256, kResolution=64
/// @deviations Dense 64^3 volume per clipmap instead of the reference engine's sparse page-based
///   128^3 system. Functionally equivalent for GI probe tracing; reduces complexity
///   at the cost of ~2MB GPU memory per clipmap (vs the reference engine's ~32MB paged atlas).

#include "../include/sdf_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root constants — composite parameters (bound at b0, root param [0])
// Exactly 8 uint32s = 32 bytes to match the root signature layout.
// ─────────────────────────────────────────────────────────────────────────────
/// @deviations Packed from 12 to 8 uint32s to fit root param [0] (8x uint32).
///   Removed g_meshSdfBufferSrv (unused), g_clipmapIndex (not consumed by shader
///   logic), and 3x padding uint32s. clipmapCenter split into 3 scalar floats to
///   avoid float3 16-byte alignment padding within root constants.

cbuffer RootConstants : register(b0)
{
    uint   g_outputUav;       // [0] Bindless UAV index for target clipmap 3D texture
    uint   g_meshCount;       // [1] Number of mesh SDFs to composite
    uint   g_clipmapIndex;    // [2] Index of the clipmap being composited (0-3)
    uint   g_resolution;      // [3] Voxels per axis (64)
    float  g_clipmapCenterX;  // [4] World-space clipmap center X (cm)
    float  g_clipmapCenterY;  // [5] World-space clipmap center Y (cm)
    float  g_clipmapCenterZ;  // [6] World-space clipmap center Z (cm)
    float  g_clipmapExtent;   // [7] Clipmap half-size in world units (cm)
};

// ─────────────────────────────────────────────────────────────────────────────
// Mesh SDF data (bound at b2 as a cbuffer array)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-mesh SDF descriptor. Must match GdfMeshSdfGPU in global_distance_field.h.
struct MeshSdfDesc
{
    float3 origin;
    float  _pad0;
    float3 extent;
    float  _pad1;
    float4 invTransformRow0;   // World-to-local row 0 (x, y, z, tx)
    float4 invTransformRow1;   // World-to-local row 1
    float4 invTransformRow2;   // World-to-local row 2
    uint   srvIndex;
    uint   resolution;
    uint2  _pad2;
};

// Maximum mesh SDFs per composite pass.
// Bounded by cbuffer size: 256 * 96 bytes = 24 KB < 64 KB cbuffer limit.
#define MAX_MESH_SDFS 256

cbuffer MeshSdfData : register(b2)
{
    MeshSdfDesc g_meshSDFs[MAX_MESH_SDFS];
};

// ─────────────────────────────────────────────────────────────────────────────
// UAV output — accessed via bindless ResourceDescriptorHeap[g_outputUav]
// No explicit register binding; the descriptor index comes from root constants.
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Sampler for mesh SDF trilinear sampling
// ─────────────────────────────────────────────────────────────────────────────

SamplerState g_linearClamp : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Helper: transform world position to mesh local space and sample SDF
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a world position into a mesh's local SDF volume space and samples
/// the signed distance. Returns a large positive value if the point is outside
/// the mesh SDF volume.
///
/// @ref Wright 2015 — mesh SDF distance query for object
float SampleMeshSdfLocal(
    Texture3D<float> sdfTex,
    MeshSdfDesc mesh,
    float3 worldPos)
{
    // Transform world position to mesh local space via the 3x4 inverse transform
    float3 localPos;
    localPos.x = dot(mesh.invTransformRow0.xyz, worldPos) + mesh.invTransformRow0.w;
    localPos.y = dot(mesh.invTransformRow1.xyz, worldPos) + mesh.invTransformRow1.w;
    localPos.z = dot(mesh.invTransformRow2.xyz, worldPos) + mesh.invTransformRow2.w;

    // Transform local position to normalized [0,1]^3 UV space within the SDF volume.
    // origin is the center; extent is half-extents.
    float3 uvPos = (localPos - mesh.origin) / (mesh.extent * 2.0f) + 0.5f;

    // Points outside the volume return a large positive distance
    if (any(uvPos < 0.0f) || any(uvPos > 1.0f))
    {
        return 1e6f;
    }

    // Sample the mesh SDF 3D texture with trilinear filtering
    return sdfTex.SampleLevel(g_linearClamp, uvPos, 0).r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main compute entry point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Wright 2015 — GDF composite dispatch entry point
/// Each thread processes one voxel in the clipmap volume.
/// Thread group size: 4x4x4 = 64 threads per group.
[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Bounds check: skip threads outside the volume
    if (any(dispatchThreadId >= g_resolution))
    {
        return;
    }

    // ── Compute world position for this voxel ──────────────────────────
    // The clipmap covers [center - extent, center + extent] in each axis.
    // Each voxel is centered at: center - extent + (voxelIdx + 0.5) * voxelSize
    float3 clipmapCenter = float3(g_clipmapCenterX, g_clipmapCenterY, g_clipmapCenterZ);
    float3 voxelCoord = float3(dispatchThreadId) + 0.5f;
    float voxelSize = (g_clipmapExtent * 2.0f) / float(g_resolution);
    float3 worldPos = clipmapCenter - g_clipmapExtent + voxelCoord * voxelSize;

    // ── Find minimum distance across all mesh SDFs ─────────────────────
    /// @ref Wright 2015 — min distance composite across all mesh SDFs
    float minDist = g_clipmapExtent; // Initialize to maximum possible distance

    uint meshCount = min(g_meshCount, MAX_MESH_SDFS);

    for (uint m = 0; m < meshCount; m++)
    {
        MeshSdfDesc mesh = g_meshSDFs[m];

        // Quick AABB rejection: skip meshes whose volume doesn't overlap
        // the voxel's neighborhood. The overlap test uses the mesh volume
        // expanded by the current minimum distance (early-out optimization).
        float3 meshMin = mesh.origin - mesh.extent;
        float3 meshMax = mesh.origin + mesh.extent;

        // Transform world position to mesh local for sampling
        // (the local-space AABB test is done inside SampleMeshSdfLocal
        // via UV range check — returns 1e6 for out-of-volume points)

        // Coarse world-space AABB culling: skip if voxel is far from mesh
        float3 closestOnBox = clamp(worldPos, meshMin, meshMax);
        float boxDist = length(worldPos - closestOnBox);
        if (boxDist > minDist)
        {
            continue;
        }

        // Bindless mesh SDF sampling: fetch the per-mesh 3D texture from the
        // descriptor heap using the SRV index stored in the mesh descriptor.
        Texture3D<float> sdfTex = ResourceDescriptorHeap[mesh.srvIndex];
        float meshDist = SampleMeshSdfLocal(sdfTex, mesh, worldPos);

        minDist = min(minDist, meshDist);
    }

    // ── Write result ───────────────────────────────────────────────────
    /// @ref Wright 2015 — GDF distance encoding to UNORM
    /// We store raw float16 distance (the R16F texture handles encoding).
    /// Output texture accessed via bindless descriptor heap.
    RWTexture3D<float> outputTex = ResourceDescriptorHeap[g_outputUav];
    outputTex[dispatchThreadId] = minDist;
}
