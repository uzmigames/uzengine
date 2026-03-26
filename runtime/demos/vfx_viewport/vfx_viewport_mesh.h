/// VFX Viewport Demo — Procedural Mesh Generation
///
/// Generates sphere, cube, plane, cylinder, and torus meshes with position,
/// normal, and UV data. Used for the PBR material preview scene.
///
/// Mesh data is returned as CPU-side vectors; the caller uploads to GPU.

#pragma once

#include "rhi_types.h"
#include "vector3.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class UploadContext;

// ─────────────────────────────────────────────────────────────────────────────
// Vertex format for procedural meshes (matches engine Vertex layout)
// ─────────────────────────────────────────────────────────────────────────────

/// Interleaved vertex for procedural meshes: position + normal + UV + tangent.
///
/// Layout matches the engine's standard PBR vertex (48 bytes) used by
/// VertexLayout::Standard() — position(12) + normal(12) + uv(8) + tangent(16).
/// The tangent.w component stores the bitangent sign (+1 or -1) for TBN matrix
/// reconstruction in the pixel shader.
struct ProceduralVertex {
    float position[3]; ///< World-space position (x, y, z).
    float normal[3];   ///< Unit-length surface normal (x, y, z).
    float uv[2];       ///< Texture coordinates (u, v).
    float tangent[4];  ///< Tangent vector (xyz) + bitangent sign (w). For TBN matrix construction.
};
static_assert(sizeof(ProceduralVertex) == 48, "ProceduralVertex must be 48 bytes (matches engine Vertex)");

// ─────────────────────────────────────────────────────────────────────────────
// CPU-side mesh data (before GPU upload)
// ─────────────────────────────────────────────────────────────────────────────

/// Holds CPU-side vertex and index data for a procedural mesh.
struct ProceduralMeshData {
    std::vector<ProceduralVertex> vertices;
    std::vector<uint32_t> indices;
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU-side mesh handle (after upload)
// ─────────────────────────────────────────────────────────────────────────────

/// Holds GPU buffer handles and draw parameters for a procedural mesh.
struct ProceduralMesh {
    RHIBufferHandle vertexBuffer;  ///< GPU vertex buffer handle.
    RHIBufferHandle indexBuffer;   ///< GPU index buffer handle.
    uint32_t indexCount = 0;       ///< Number of indices (for DrawIndexed).
    uint32_t vertexStride = 0;     ///< Vertex stride in bytes (sizeof(ProceduralVertex)).
    const char* name = "";         ///< Human-readable name for debug overlay.
};

// ─────────────────────────────────────────────────────────────────────────────
// Mesh generation functions
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a UV sphere with the given number of slices (longitude segments)
/// and stacks (latitude segments).
///
/// # Arguments
/// * `slices` — Number of longitudinal segments (default 32).
/// * `stacks` — Number of latitudinal segments (default 16).
///
/// # Returns
/// CPU-side mesh data with vertices and triangle indices.
[[nodiscard]] ProceduralMeshData GenerateSphere(uint32_t slices = 32,
                                                 uint32_t stacks = 16);

/// Generates a unit cube centered at the origin (side length = 2, extents +-1).
///
/// Each face has unique normals and UVs (24 vertices, 36 indices).
[[nodiscard]] ProceduralMeshData GenerateCube();

/// Generates a flat XZ plane centered at the origin.
///
/// # Arguments
/// * `halfExtent` — Distance from center to edge (default 5.0).
/// * `divisions` — Grid subdivisions per axis (default 10).
[[nodiscard]] ProceduralMeshData GeneratePlane(float halfExtent = 5.0f,
                                                uint32_t divisions = 10);

/// Generates a cylinder aligned along the Y axis, centered at the origin.
///
/// # Arguments
/// * `radius` — Cylinder radius (default 0.5).
/// * `height` — Total height (default 2.0).
/// * `slices` — Number of radial segments (default 32).
[[nodiscard]] ProceduralMeshData GenerateCylinder(float radius = 0.5f,
                                                   float height = 2.0f,
                                                   uint32_t slices = 32);

/// Generates a torus centered at the origin in the XZ plane.
///
/// # Arguments
/// * `majorRadius` — Distance from center of torus to center of tube (default 1.0).
/// * `minorRadius` — Tube radius (default 0.3).
/// * `majorSegments` — Segments around the main ring (default 32).
/// * `minorSegments` — Segments around the tube cross-section (default 16).
[[nodiscard]] ProceduralMeshData GenerateTorus(float majorRadius = 1.0f,
                                                float minorRadius = 0.3f,
                                                uint32_t majorSegments = 32,
                                                uint32_t minorSegments = 16);

/// Computes tangent vectors for a mesh using the triangle-edge/delta-UV method.
///
/// @ref Lengyel 2001 "Computing Tangent Space Basis Vectors for an Arbitrary Mesh"
///
/// For each triangle, computes tangent and bitangent from edge vectors and UV
/// deltas, accumulates per-vertex, then Gram-Schmidt orthonormalizes against
/// the vertex normal. The bitangent handedness is stored in tangent.w (+1/-1).
///
/// Call this after generating vertices and indices. Handles averaging of shared
/// vertices and degenerate UV triangles (falls back to an arbitrary perpendicular
/// tangent).
void ComputeTangents(ProceduralMeshData& mesh);

/// Uploads CPU mesh data to GPU buffers via the command list.
///
/// Creates default-heap vertex and index buffers, stages the upload via
/// UploadContext. The command list must be in recording state.
///
/// # Arguments
/// * `device` — RHI device for buffer creation.
/// * `cmdList` — Command list in recording state for copy commands.
/// * `upload` — Upload context for staging buffer management.
/// * `data` — CPU-side mesh data to upload.
/// * `name` — Debug name for the GPU buffers.
///
/// # Returns
/// GPU-side mesh handle with buffer handles and draw parameters.
[[nodiscard]] ProceduralMesh UploadProceduralMesh(IRHIDevice* device,
                                                   IRHICommandList* cmdList,
                                                   UploadContext& upload,
                                                   const ProceduralMeshData& data,
                                                   const char* name);

} // namespace uz
