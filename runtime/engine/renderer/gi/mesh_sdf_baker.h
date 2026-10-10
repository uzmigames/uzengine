/// UzEngine GI Mesh SDF Baker — dense R16F distance fields generated on the GPU.
/// Each voxel tests mesh triangles for the nearest point. Sign uses the face
/// normal, oriented to supplied vertex normals when their offset is provided.
/// The caller must exclusively own the command list while recording the bake.
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields"
/// @ref Ericson 2005 "Real-Time Collision Detection" — closest-point-on-triangle

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;

// ───────────────────────────────────────
// MeshSDF — GPU-side 3D texture handle
// ───────────────────────────────────────

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
    /// Identity of the instance this copy describes (the draw's entity id): the global distance
    /// field diffs instances by it from frame to frame (block B.2).
    uint32_t         instanceKey = UINT32_MAX;

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

// ───────────────────────────────────────
// MeshSdfCpuBake — the CPU half of a bake (block B.3)
// ───────────────────────────────────────

/// Everything a bake dispatch needs that does not touch the device: the volume placement and the
/// 64-byte triangle + preorder BVH records. Built by `MeshSdfBaker::PrepareCPU` on any thread
/// (it only reads the mesh bytes), consumed on the main thread by `CreateResources`; the mesh
/// SDF cache stores it next to the baked volume.
struct MeshSdfCpuBake {
    bool                 ok = false;
    float                volumeCenter[3] = {};     ///< Mesh-local centre of the cubic volume.
    float                volumeHalfExtent[3] = {}; ///< Half-extent, equal on the three axes.
    float                volumeMin[3] = {};        ///< Corner of voxel (0, 0, 0) (the shader's volumeMin).
    float                voxelSize = 0.0f;
    uint32_t             resolution = 0;
    uint32_t             triangleCount = 0;
    uint32_t             nodeCount = 0;
    uint32_t             orientedTriangleCount = 0; ///< Face normals flipped to agree with the vertex normals.
    std::vector<uint8_t> records;                  ///< Triangles then BVH nodes, 64 bytes each.
};

/// Root constants of mesh_sdf_bake_cs.hlsl (9 DWORDs at root parameter 0). `sliceOffset` lets a
/// bake be recorded in Z slabs across frames (block B.3): thread z + sliceOffset is the voxel z.
struct MeshSdfBakeRootConstants {
    uint32_t outputUav;
    uint32_t trianglesSrv;
    uint32_t triangleCount;      ///< With kMeshSdfBvhFlag.
    uint32_t resolution;         ///< Bit 31 = two-sided (unsigned distance).
    float    volumeMinX, volumeMinY, volumeMinZ;
    float    voxelSize;
    uint32_t sliceOffset;
};
static_assert(sizeof(MeshSdfBakeRootConstants) == 36, "9 root DWORDs");

// ───────────────────────────────────────
// MeshSdfBaker — GPU baking
// ───────────────────────────────────────

/// Bakes mesh triangles into a signed distance field on the GPU.
///
/// Uses a compute shader (mesh_sdf_bake_cs.hlsl) that dispatches one thread
/// per voxel and searches a BVH for the closest triangle.
///
/// Two ways to use it. `BakeMeshGPU` does everything on the calling thread and marks the SDF
/// valid when the commands are recorded (tools and tests). The scene renderer instead runs
/// `PrepareCPU` on a worker, `CreateResources` + `RecordBakeSlices` over one or more frames and
/// marks the SDF valid once a fence says the GPU finished (`MeshSdfBakeQueue`, block B.3).
class MeshSdfBaker {
public:
    static constexpr uint32_t kRootConstantCount = sizeof(MeshSdfBakeRootConstants) / sizeof(uint32_t);

    /// CPU half: validates the mesh, places the cubic volume (AABB padded by 10 %, at least one
    /// unit), builds the triangle records (face normals oriented to the vertex normals when
    /// `vertexNormalOffset` is given) and the BVH. Thread-safe: reads only its arguments.
    /// `indices` may be null for a non-indexed mesh (`indexCount` 0 = vertexCount).
    [[nodiscard]] static MeshSdfCpuBake PrepareCPU(
        const float* vertices,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t* indices,
        uint32_t indexCount,
        uint32_t resolution,
        uint32_t vertexNormalOffset = UINT32_MAX);

    /// Main thread: creates the triangle/BVH buffer (upload heap, records copied in; returned in
    /// `pendingTriBuffer`) and the R16F volume (shader resource, UAV and copy source/destination).
    /// The returned SDF is not valid yet; `texture3D` is invalid on failure (nothing leaks).
    [[nodiscard]] static MeshSDF CreateResources(IRHIDevice* device, const MeshSdfCpuBake& cpu, bool twoSided);

    /// Records the bake of voxel slices [firstSlice, firstSlice + sliceCount) — rounded up to the
    /// 4-slice thread group, re-writing identical values past the end — on a volume that is in
    /// UnorderedAccess state. Nothing else: the caller owns the barriers.
    static void RecordBakeSlices(
        IRHICommandList* cmdList,
        RHIPipelineHandle bakePipeline,
        IRHIDevice* device,
        const MeshSDF& sdf,
        const MeshSdfCpuBake& cpu,
        uint32_t firstSlice,
        uint32_t sliceCount);

    /// Row pitch of one row of the R16F volume in a copy buffer (256-byte aligned, as D3D12 wants).
    [[nodiscard]] static uint32_t VolumeRowPitch(uint32_t resolution) {
        return (resolution * 2u + 255u) & ~255u;
    }

    /// Bakes a mesh SDF entirely on the GPU via compute shader.
    ///
    /// Uploads triangle data to a StructuredBuffer, dispatches the mesh_sdf_bake_cs
    /// compute shader (one thread per voxel, closest-point-on-triangle through the BVH),
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
