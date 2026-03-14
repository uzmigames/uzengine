/// UzEngine Meshlet Rendering — Data structures for GPU-driven mesh shading.
///
/// Meshlets are small clusters of triangles (typically 64 vertices, 124 triangles)
/// that map 1:1 to mesh shader threadgroups. This enables per-cluster GPU culling,
/// LOD selection, and occlusion testing — all without CPU readback.
///
/// # Pipeline
/// ```
/// CPU: Triangle mesh → Meshletizer → Meshlet[] + vertex/triangle indices + bounds
/// GPU: Mesh shader reads buffers → per-meshlet frustum/backface cull → rasterize
/// ```
///
/// # Current State
/// Data structures only (Phase 1). Mesh shader dispatch and meshletizer
/// will be added when SM6.5 mesh shader support is wired through the RHI.

#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Meshlet Core Types
// ─────────────────────────────────────────────────────────────────────────────

/// A meshlet is a small cluster of triangles processed by a single mesh shader
/// threadgroup.
///
/// Each meshlet references a contiguous range in the meshlet vertex index buffer
/// and the meshlet triangle index buffer. The vertex indices map local (per-meshlet)
/// vertex IDs to global vertex buffer indices. The triangle indices are packed
/// 3 bytes per triangle, referencing local vertex IDs (0..vertexCount-1).
///
/// # Size Limits
/// - `vertexCount` max 64 (NVIDIA recommendation for optimal occupancy)
/// - `triangleCount` max 124 (NVIDIA recommendation; 128 minus alignment overhead)
struct Meshlet {
    uint32_t vertexOffset;     ///< Offset into the meshlet vertex index buffer
    uint32_t triangleOffset;   ///< Offset into the meshlet triangle index buffer
    uint32_t vertexCount;      ///< Number of unique vertices in this meshlet (max 64)
    uint32_t triangleCount;    ///< Number of triangles in this meshlet (max 124)
};

/// Bounding information for a meshlet, used for GPU culling.
///
/// Contains a bounding sphere for frustum/occlusion culling and a normal cone
/// for backface cluster culling. If all triangle normals in the meshlet point
/// roughly the same direction, the entire meshlet can be culled when viewed
/// from behind.
///
/// # Normal Cone Culling
/// A meshlet is backface-culled when:
/// `dot(normalize(coneApex - viewPos), coneAxis) >= coneCutoff`
///
/// Set `coneCutoff` to a value < -1.0 to disable cone culling for meshlets
/// with widely varying normals (e.g., double-sided or edge meshlets).
struct MeshletBounds {
    float center[3];           ///< Bounding sphere center (object space)
    float radius;              ///< Bounding sphere radius

    float coneApex[3];         ///< Normal cone apex (for backface cluster culling)
    float coneAxis[3];         ///< Normal cone axis (average normal direction)
    float coneCutoff;          ///< Normal cone cutoff (cos(angle/2)), < -1.0 = skip
};

// ─────────────────────────────────────────────────────────────────────────────
// Meshlet Build Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for meshlet generation from a triangle mesh.
///
/// Controls cluster size and optional post-processing passes.
/// Default values follow NVIDIA recommendations for Turing+ GPUs.
struct MeshletBuildConfig {
    uint32_t maxVertices = 64;       ///< Max unique vertices per meshlet (hardware limit: 256)
    uint32_t maxTriangles = 124;     ///< Max triangles per meshlet (NV recommendation)
    bool generateBounds = true;      ///< Compute bounding spheres + normal cones
    bool optimizeForOverdraw = true; ///< Reorder meshlets to reduce overdraw
};

// ─────────────────────────────────────────────────────────────────────────────
// Meshlet Output Data
// ─────────────────────────────────────────────────────────────────────────────

/// Result of meshlet generation for one mesh.
///
/// Owns all output arrays. The caller is responsible for uploading these
/// buffers to the GPU as structured buffers for mesh shader consumption.
///
/// # Buffer Layout (GPU side)
/// - `meshlets` → StructuredBuffer<Meshlet> (read by mesh shader)
/// - `meshletVertices` → Buffer<uint> (local-to-global vertex index mapping)
/// - `meshletTriangles` → ByteAddressBuffer (3 bytes per triangle, packed)
/// - `meshletBounds` → StructuredBuffer<MeshletBounds> (read by task/amplification shader)
struct MeshletData {
    Meshlet* meshlets = nullptr;            ///< Array of meshlet descriptors
    uint32_t meshletCount = 0;              ///< Number of meshlets

    uint32_t* meshletVertices = nullptr;    ///< Vertex indices (per-meshlet local to global)
    uint32_t meshletVertexCount = 0;        ///< Total vertex index entries across all meshlets

    uint8_t* meshletTriangles = nullptr;    ///< Triangle indices (3 bytes per tri, packed)
    uint32_t meshletTriangleCount = 0;      ///< Total triangle count across all meshlets

    MeshletBounds* meshletBounds = nullptr; ///< One per meshlet (nullptr if not generated)
};

// ─────────────────────────────────────────────────────────────────────────────
// Meshlet Dispatch
// ─────────────────────────────────────────────────────────────────────────────

/// Mesh shader dispatch parameters for a single draw.
///
/// Used with `DispatchMesh(meshletCount, 1, 1)` where each threadgroup
/// processes one meshlet. The `meshletOffset` allows batching multiple
/// meshes into a single meshlet buffer.
struct MeshletDispatchArgs {
    uint32_t meshletCount;     ///< Number of meshlets = number of threadgroups
    uint32_t meshletOffset;    ///< First meshlet index in the buffer
    uint32_t instanceId;       ///< Instance ID for instanced meshlet rendering
};

// ─────────────────────────────────────────────────────────────────────────────
// Capability Query
// ─────────────────────────────────────────────────────────────────────────────

/// Checks whether mesh shader (meshlet) rendering is available on this device.
///
/// Requires SM6.5+ and the mesh shader optional feature. Returns false on
/// hardware/drivers that do not support mesh shaders (e.g., pre-Turing NVIDIA,
/// pre-RDNA2 AMD, all Intel as of 2026).
///
/// # Returns
/// `true` if mesh shaders can be used, `false` otherwise.
[[nodiscard]] bool IsMeshletRenderingSupported();

}  // namespace uz
