/// UzEngine GI Mesh SDF Baker — dense R16F distance fields generated on the GPU.
/// Each voxel tests mesh triangles for the nearest point. Sign uses the face
/// normal, oriented to supplied vertex normals when their offset is provided.
/// The caller must exclusively own the command list while recording the bake.
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields"
/// @ref Ericson 2005 "Real-Time Collision Detection" — closest-point-on-triangle

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
    float            volumeOrigin[3] = {};     ///< Mesh-local center of the SDF volume.
    float            volumeExtent[3] = {};     ///< Mesh-local half-extents of the SDF volume.
    uint32_t         resolution = 0;           ///< Voxels per axis.
    bool             valid = false;            ///< True if texture is created and uploaded.
    bool             twoSided = false;
    uint32_t         albedoSrv = 0;
    uint32_t         metallicSrv = 0;
    float            baseColorTint[3] = {1.0f, 1.0f, 1.0f};
    float            metallicMultiplier = 1.0f;
    uint32_t         materialFlags = 0;

    /// Three float4 dot products mapping world position to mesh-local position,
    /// including translation in each vector's fourth component.
    float            invTransform[12] = {};

    /// Transient triangle buffer from GPU bake — must be destroyed after the
    /// command list that references it has finished executing on the GPU.
    /// Invalid handle means no pending destruction needed.
    RHIBufferHandle  pendingTriBuffer;

    /// The same triangle + BVH buffer, kept after the bake for the radiance cache's probe side
    /// classifier (`probe_side_cs.hlsl`, followup-01): 64-byte records, triangles first, then the
    /// preorder BVH nodes; `triangleCountAndFlags` is the bake's count with the BVH bit. Owned
    /// together with `texture3D` (released in `DestroyGPUMesh`). Invalid when it was not kept.
    RHIBufferHandle  triangleBuffer;
    uint32_t         triangleSrv = UINT32_MAX;
    uint32_t         triangleCountAndFlags = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker — GPU baking
// ─────────────────────────────────────────────────────────────────────────────

/// Bakes mesh triangles into a signed distance field on the GPU.
///
/// Uses a compute shader (mesh_sdf_bake_cs.hlsl) that dispatches one thread
/// per voxel, brute-force iterating all triangles.
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
    /// * `vertexNormalOffset` — Byte offset of float3 vertex normals; UINT32_MAX uses index winding.
    /// * `twoSided` — Store unsigned distance for open surfaces without an interior.
    [[nodiscard]] static MeshSDF BakeMeshGPU(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        RHIPipelineHandle bakePipeline,
        const float* vertices,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t* indices,
        uint32_t indexCount,
        uint32_t resolution = 32,
        uint32_t vertexNormalOffset = UINT32_MAX,
        bool twoSided = false);
};

} // namespace uz
