/// UzEngine GI Mesh SDF Baker — CPU-side distance field generation for GI pipeline.
///
/// Generates a dense signed distance field from mesh triangles for use in the
/// software ray-traced GI system (Lumen-style sphere marching). Unlike the sparse
/// brick-based SDF in `sdf_types.h` (used for shadow cone tracing), this produces
/// a flat dense R16F-compatible grid optimized for direct 3D texture upload.
///
/// The baker wraps the existing `BakeMeshSdf()` from the asset importer pipeline
/// and converts its sparse brick output to a dense float grid suitable for the
/// GI trace shaders.
///
/// # Algorithm
/// 1. Compute mesh AABB, expand by 10% for narrow-band padding.
/// 2. For each voxel in the grid, compute world position at voxel center.
/// 3. Find distance to nearest triangle via spatial hash acceleration.
/// 4. Determine sign via pseudo-normal method (dot with face normal at closest point).
/// 5. Store signed distance (negative inside, positive outside) in world units (cm).
///
/// # References
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — brick SDF layout
/// @ref Ericson 2005 "Real-Time Collision Detection" — closest-point-on-triangle
///
/// # Thread Safety
/// Thread-safe — no shared mutable state. Can be called from worker threads.

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// MeshSDF — GPU-side 3D texture handle
// ─────────────────────────────────────────────────────────────────────────────

/// GPU representation of a mesh signed distance field.
///
/// Holds an R16F 3D texture and its bindless SRV index for shader access.
/// The volume origin/extent are needed by shaders to transform world
/// positions into volume UVs.
struct MeshSDF {
    RHITextureHandle texture3D;                ///< R16F 3D texture of distance values.
    uint32_t         srvIndex = UINT32_MAX;    ///< Bindless SRV index in global descriptor heap.
    float            volumeOrigin[3] = {};     ///< World-space center of the SDF volume.
    float            volumeExtent[3] = {};     ///< World-space half-extents of the SDF volume.
    uint32_t         resolution = 0;           ///< Voxels per axis.
    bool             valid = false;            ///< True if texture is created and uploaded.

    /// Cached world-to-local inverse transform (top 3 rows, row-major).
    /// Precomputed to avoid per-frame Mat4::Inverse() in GDF composite.
    /// Updated when the entity's world transform changes.
    float            invTransform[12] = {};

    /// Transient triangle buffer from GPU bake — must be destroyed after the
    /// command list that references it has finished executing on the GPU.
    /// Invalid handle means no pending destruction needed.
    RHIBufferHandle  pendingTriBuffer;
};

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker — CPU baking + GPU upload
// ─────────────────────────────────────────────────────────────────────────────

/// Bakes mesh triangles into a signed distance field on the GPU.
///
/// Uses a compute shader (mesh_sdf_bake_cs.hlsl) that dispatches one thread
/// per voxel, brute-force iterating all triangles. 2500x faster than CPU path.
///
/// # Usage
/// ```cpp
/// auto pipeline = pipelines->GetMeshSdfBake();
/// MeshSDF sdf = MeshSdfBaker::BakeMeshGPU(device, cmdList, pipeline,
///     positions, vertexCount, stride, indices, indexCount, 32);
/// // Defer destruction: sdf.pendingTriBuffer must be destroyed after GPU completes
/// ```
class MeshSdfBaker {
public:
    /// Bakes a mesh SDF entirely on the GPU via compute shader.
    ///
    /// Uploads triangle data to a StructuredBuffer, dispatches the mesh_sdf_bake_cs
    /// compute shader (one thread per voxel, brute-force closest-point-on-triangle),
    /// and returns the MeshSDF with a populated R16F 3D texture.
    ///
    /// The returned `MeshSDF::pendingTriBuffer` must be destroyed by the caller
    /// after the command list finishes executing (deferred to next frame).
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation.
    /// * `cmdList` — Command list in recording state.
    /// * `bakePipeline` — Compute pipeline from PipelineLibrary::GetMeshSdfBake().
    /// * `vertices` — Pointer to vertex position data (float x, y, z per vertex).
    /// * `vertexCount` — Number of vertices.
    /// * `vertexStride` — Byte stride between vertices.
    /// * `indices` — Triangle index buffer (3 per triangle). Null for non-indexed.
    /// * `indexCount` — Number of indices.
    /// * `resolution` — Voxels per axis (32 typical).
    [[nodiscard]] static MeshSDF BakeMeshGPU(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        RHIPipelineHandle bakePipeline,
        const float* vertices,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t* indices,
        uint32_t indexCount,
        uint32_t resolution = 32);
};

} // namespace uz
