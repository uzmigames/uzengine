/// UzEngine GPU Mesh — Uploads CPU-side Mesh to GPU buffers for rendering.
///
/// Creates vertex and index buffer resources on the GPU, optionally with a
/// bindless SRV for SM6.6 structured buffer access. The returned `GPUMesh`
/// struct holds all handles needed to bind and draw the mesh.
///
/// # Usage
/// ```cpp
/// Mesh cpuMesh = MeshBuilder::CreateCube(1.0f);
/// GPUMesh gpu = UploadMesh(device, cmdList, cpuMesh);
/// // ... render with gpu.vertexBuffer, gpu.indexBuffer ...
/// DestroyGPUMesh(device, gpu);
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Call from the thread that owns the command list.

#pragma once

#include "rhi_types.h"
#include "upload_context.h"
#include "gi/mesh_sdf_baker.h"
#include "vector3.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
struct Mesh;

// ─────────────────────────────────────────────────────────────────────────────
// GPUMesh — Handles and metadata for a mesh uploaded to the GPU
// ─────────────────────────────────────────────────────────────────────────────

/// GPU-resident mesh with vertex buffer, optional index buffer, and bindless
/// SRV index for SM6.6 structured buffer access.
///
/// Created by `UploadMesh()`, destroyed by `DestroyGPUMesh()`. All buffer
/// handles are owned by the caller and must be explicitly destroyed.
struct GPUMesh {
    RHIBufferHandle vertexBuffer;                       ///< GPU vertex buffer handle.
    RHIBufferHandle indexBuffer;                        ///< GPU index buffer handle (invalid if non-indexed).
    uint32_t        vbSrvIndex  = UINT32_MAX;           ///< Bindless SRV descriptor index for vertex buffer (SM6.6). UINT32_MAX if not bindless.
    uint32_t        vertexStride = 48;                  ///< Vertex stride in bytes (48 = Vertex, 32 = VertexPacked).
    uint32_t        indexCount  = 0;                    ///< Number of indices for LOD0 (0 = non-indexed mesh).
    uint32_t        vertexCount = 0;                    ///< Number of vertices.
    RHIIndexFormat  indexFormat = RHIIndexFormat::Uint32; ///< Index element format (`Uint16` or `Uint32`).

    /// LOD index data — LOD0 uses indexCount at offset 0.
    /// LOD1..3 use these fields. All LOD indices are in the same index buffer.
    uint32_t lodCount = 1;
    struct { uint32_t indexOffset; uint32_t indexCount; } lodInfo[3] = {};

    /// World-space AABB from the source mesh (used for bounding sphere / LOD radius).
    Vec3 boundsMin;
    Vec3 boundsMax;

    /// Baked signed distance field for this mesh (used by GDF composite for GI).
    /// Valid only after MeshSdfBaker::UploadToGPU succeeds during mesh loading.
    MeshSDF sdf;

    /// Returns true if the mesh has a valid vertex buffer on the GPU.
    [[nodiscard]] bool IsValid() const { return vertexBuffer.IsValid(); }

    /// Returns true if the mesh uses an index buffer.
    [[nodiscard]] bool IsIndexed() const { return indexBuffer.IsValid() && indexCount > 0; }

    /// Returns true if this mesh has a valid baked SDF for GI tracing.
    [[nodiscard]] bool HasSDF() const { return sdf.valid; }
};

// ─────────────────────────────────────────────────────────────────────────────
// DeferredMeshUpload — Holds a GPUMesh and its staging buffers pending flush
// ─────────────────────────────────────────────────────────────────────────────

/// Holds a fully-populated `GPUMesh` alongside the `UploadContext` whose
/// staging buffers are still alive. The GPU copy commands have been recorded
/// on a command list but NOT yet executed — call `FlushDeferredMeshUploads()`
/// to execute all pending copies in a single GPU submission.
///
/// Move-only — the `UploadContext` contains a `std::vector` of staging handles.
struct DeferredMeshUpload {
    GPUMesh       mesh;     ///< Populated mesh metadata (vertex/index count, formats, SRV index).
    UploadContext upload;   ///< Staging buffers kept alive until flush.

    /// Constructs a deferred upload bound to the given device for staging
    /// buffer creation.
    ///
    /// # Arguments
    /// * `device` - RHI device for staging buffer allocation. Must not be null.
    explicit DeferredMeshUpload(IRHIDevice* device) : upload(device) {}

    DeferredMeshUpload(const DeferredMeshUpload&)            = delete;
    DeferredMeshUpload& operator=(const DeferredMeshUpload&) = delete;
    DeferredMeshUpload(DeferredMeshUpload&&)                 = default;
    DeferredMeshUpload& operator=(DeferredMeshUpload&&)      = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Upload / Destroy
// ─────────────────────────────────────────────────────────────────────────────

/// Uploads a CPU-side `Mesh` to GPU default-heap buffers.
///
/// Creates a vertex buffer (and optional index buffer) on the GPU via staging
/// upload. If the device supports bindless (SM6.6), the vertex buffer is
/// created as a `StructuredBuffer` with an SRV, and `GPUMesh::vbSrvIndex`
/// is set to the descriptor index. Otherwise, a plain `VertexBuffer` is used.
///
/// Internally creates an `UploadContext`, records copy commands on `cmdList`,
/// then executes the command list and waits for GPU completion before returning.
/// The command list is left in the recording state on return.
///
/// # Arguments
/// * `device`  - RHI device for buffer creation. Must not be null.
/// * `cmdList` - Command list for recording copy commands. Must be in recording
///   state on entry. Will be ended, executed, waited on, and re-begun internally.
/// * `mesh`    - CPU mesh data to upload. Must pass `Mesh::IsValid()`.
///
/// # Returns
/// A populated `GPUMesh` on success. An invalid `GPUMesh` (where `IsValid()`
/// returns false) on failure — no GPU resources are leaked in this case.
///
/// # Errors
/// Logs `Renderer::Error` on:
/// - Null device or command list
/// - Invalid mesh (fails `Mesh::IsValid()`)
/// - Buffer creation failure
/// - Upload failure
[[nodiscard]] GPUMesh UploadMesh(IRHIDevice* device, IRHICommandList* cmdList, const Mesh& mesh);

/// Uploads a CPU-side `Mesh` to GPU default-heap buffers WITHOUT executing
/// the command list or waiting for the GPU.
///
/// Records copy commands on `cmdList` (which must already be in recording
/// state) and returns a `DeferredMeshUpload` that keeps the staging buffers
/// alive. The command list is NOT ended, executed, or waited on — the caller
/// must batch multiple deferred uploads and then call
/// `FlushDeferredMeshUploads()` once to submit them all in a single GPU
/// submission, avoiding per-mesh GPU stalls during scene loading.
///
/// All metadata fields (`vertexCount`, `indexCount`, `indexFormat`,
/// `vbSrvIndex`) are populated immediately since they don't require GPU
/// completion.
///
/// # Arguments
/// * `device`  - RHI device for buffer creation. Must not be null.
/// * `cmdList` - Command list in recording state. NOT ended by this function.
/// * `mesh`    - CPU mesh data to upload. Must pass `Mesh::IsValid()`.
///
/// # Returns
/// A populated `DeferredMeshUpload` on success. On failure, the returned
/// `DeferredMeshUpload::mesh.IsValid()` returns false — no GPU resources are
/// leaked.
///
/// # Errors
/// Logs `Renderer::Error` on null arguments, invalid mesh, or buffer
/// creation/upload failure.
[[nodiscard]] DeferredMeshUpload UploadMeshDeferred(IRHIDevice* device, IRHICommandList* cmdList, const Mesh& mesh);

/// Executes all pending deferred mesh uploads in a single GPU submission.
///
/// Ends the command list and executes it so the copies are submitted before
/// any draw recorded afterwards (same-queue FIFO guarantees ordering), then
/// re-begins the command list so the caller can continue recording.
///
/// Staging buffer lifetime has two modes:
/// - `deferredRelease == nullptr` (legacy): waits for GPU idle and frees all
///   staging buffers immediately. Introduces a full CPU/GPU stall.
/// - `deferredRelease != nullptr` (stall-free): NO WaitIdle. Ownership of
///   every staging buffer handle is moved into `*deferredRelease`; the caller
///   MUST destroy them only after the GPU has consumed this frame's commands
///   (e.g., a fence-guarded retire queue held for the frames-in-flight depth).
///
/// After this call, all `DeferredMeshUpload::mesh` instances are safe to use
/// for rendering (draws recorded later on the same queue observe the copies).
///
/// # Arguments
/// * `device`  - RHI device. Must not be null.
/// * `cmdList` - Command list with pending copy commands. Will be ended,
///   executed, and re-begun.
/// * `uploads` - Vector of deferred uploads whose staging buffers will be
///   released or handed off. May be empty (still submits/restarts the list).
/// * `deferredRelease` - Optional sink receiving staging buffer handles for
///   fence-guarded destruction. Null selects the legacy WaitIdle path.
///
/// # Errors
/// Logs `Renderer::Error` if device or command list is null.
void FlushDeferredMeshUploads(IRHIDevice* device, IRHICommandList* cmdList,
                              std::vector<DeferredMeshUpload>& uploads,
                              std::vector<RHIBufferHandle>* deferredRelease = nullptr);

/// Destroys GPU buffers owned by a `GPUMesh` and resets it to default state.
///
/// Safe to call on an already-invalid `GPUMesh` (no-op).
/// The GPU must be idle before calling this — the caller is responsible
/// for ensuring no in-flight frames reference these buffers.
///
/// # Arguments
/// * `device`  - RHI device that created the buffers. Must not be null.
/// * `gpuMesh` - Mesh to destroy. Reset to default values on return.
void DestroyGPUMesh(IRHIDevice* device, GPUMesh& gpuMesh);

} // namespace uz
