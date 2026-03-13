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

#include <cstdint>

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
    uint32_t        indexCount  = 0;                    ///< Number of indices (0 = non-indexed mesh).
    uint32_t        vertexCount = 0;                    ///< Number of vertices.
    RHIIndexFormat  indexFormat = RHIIndexFormat::Uint32; ///< Index element format (`Uint16` or `Uint32`).

    /// Returns true if the mesh has a valid vertex buffer on the GPU.
    [[nodiscard]] bool IsValid() const { return vertexBuffer.IsValid(); }

    /// Returns true if the mesh uses an index buffer.
    [[nodiscard]] bool IsIndexed() const { return indexBuffer.IsValid() && indexCount > 0; }
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
