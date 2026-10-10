#pragma once

/// VFX Procedural Mesh Generation — cube and sphere primitives for mesh particles.
///
/// Generates GPU-resident vertex and index buffers for simple 3D shapes used by
/// MeshRenderModule. These are Upload-heap buffers suitable for immediate use
/// without staging copy (small enough that Upload-heap latency is negligible).
///
/// # Vertex Layout
/// Each vertex is 32 bytes: position (Vec3), normal (Vec3), u, v (2 floats).
/// This layout matches the VS input struct in particle_mesh_vs.hlsl.
///
/// # Coordinate System
/// Z-up, left-handed, centimeters. Half-extent of 0.5 cm for the unit shapes.
///
/// @ref standard procedural geometry generation (unit shapes with per-face normals)

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// VfxMeshVertex — per-vertex layout for VFX mesh particles
// ─────────────────────────────────────────────────────────────────────────────

/// Vertex layout for procedural VFX meshes.
///
/// Matches the VS input struct in particle_mesh_vs.hlsl. Position and normal
/// are in local (object) space; the VS transforms them to world space using
/// the per-instance float4x4 from the instance transform buffer.
///
/// Total stride: 32 bytes (3 + 3 + 1 + 1 floats * 4 bytes).
struct VfxMeshVertex {
    float px, py, pz;   ///< Position in local space (centimeters).
    float nx, ny, nz;   ///< Unit normal in local space.
    float u, v;          ///< Texture coordinates [0, 1].
};

static_assert(sizeof(VfxMeshVertex) == 32, "VfxMeshVertex must be exactly 32 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// VfxProceduralMesh — result of procedural mesh generation
// ─────────────────────────────────────────────────────────────────────────────

/// Holds GPU buffer handles and counts for a procedurally generated mesh.
///
/// The caller is responsible for destroying the buffers via `device->DestroyBuffer()`
/// when the mesh is no longer needed.
struct VfxProceduralMesh {
    RHIBufferHandle vb = RHIBufferHandle::Invalid();  ///< Vertex buffer (VfxMeshVertex stride).
    RHIBufferHandle ib = RHIBufferHandle::Invalid();   ///< Index buffer (uint16_t stride).
    uint32_t vertexCount = 0;                          ///< Number of vertices in the VB.
    uint32_t indexCount = 0;                            ///< Number of indices in the IB.

    /// Returns true if both buffers were allocated successfully.
    [[nodiscard]] bool IsValid() const noexcept {
        return vb.IsValid() && ib.IsValid();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Procedural mesh generators
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a unit cube mesh (half-extent = 0.5 cm).
///
/// 24 vertices (4 per face, 6 faces) with per-face normals and UVs.
/// 36 indices (2 triangles per face, 6 faces). Winding is counter-clockwise
/// for left-handed Z-up (front-face = CW in DX12 default, which matches
/// left-handed conventions).
///
/// # Parameters
/// - `device`: RHI device for buffer creation. Must not be null.
///
/// # Returns
/// Valid `VfxProceduralMesh` on success; invalid (both handles invalid) on failure.
///
/// # Memory
/// VB: 24 * 32 = 768 bytes. IB: 36 * 2 = 72 bytes. Total: 840 bytes.
[[nodiscard]] VfxProceduralMesh GenerateCubeMesh(IRHIDevice* device);

/// Generates a UV sphere mesh (radius = 0.5 cm, 8 stacks x 16 slices).
///
/// Produces (stacks + 1) * (slices + 1) = 153 vertices and
/// stacks * slices * 6 = 768 indices (degenerate triangles at poles are
/// included for simplicity — they have zero area and are culled by the GPU).
///
/// # Parameters
/// - `device`: RHI device for buffer creation. Must not be null.
///
/// # Returns
/// Valid `VfxProceduralMesh` on success; invalid on failure.
///
/// @ref standard UV sphere parametric generation
[[nodiscard]] VfxProceduralMesh GenerateSphereMesh(IRHIDevice* device);

/// Releases the GPU buffers held by a procedural mesh.
///
/// Safe to call with invalid handles (no-op). Resets the mesh to invalid state.
///
/// # Parameters
/// - `device`: RHI device for deallocation. May be null (no-op).
/// - `mesh`: Mesh to destroy. Handles are reset to invalid.
void DestroyProceduralMesh(IRHIDevice* device, VfxProceduralMesh& mesh);

} // namespace uz
