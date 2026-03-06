#pragma once

// mesh.h -- GPU mesh representation and management.
//
// GpuMesh holds RHI vertex/index buffer handles and submesh data.
// MeshData holds CPU-side vertex/index arrays for upload.
//
// RULE RHI-1: No backend types above rhi.h

#include <cstdint>
#include <vector>

#include "engine/core/math.h"
#include "engine/renderer/rhi/rhi.h"

namespace uz {

// ---------------------------------------------------------------------------
// Standard vertex format for PBR rendering.
// 48 bytes/vertex: POSITION(12) + NORMAL(12) + TEXCOORD(8) + TANGENT(16)
// ---------------------------------------------------------------------------
struct MeshVertex {
    Float3 position;        // 12 bytes
    Float3 normal;          // 12 bytes
    float  texcoord[2];     //  8 bytes (u, v)
    float  tangent[4];      // 16 bytes (xyz + w sign for bitangent)
};

static_assert(sizeof(MeshVertex) == 48, "MeshVertex must be 48 bytes");

// ---------------------------------------------------------------------------
// SubMesh — a range within the index buffer with a material assignment.
// ---------------------------------------------------------------------------
struct SubMesh {
    uint32_t index_offset   = 0;
    uint32_t index_count    = 0;
    uint32_t material_index = 0;
};

// ---------------------------------------------------------------------------
// MeshData — CPU-side mesh data for upload.
// ---------------------------------------------------------------------------
struct MeshData {
    std::vector<MeshVertex>  vertices;
    std::vector<uint32_t>    indices;
    std::vector<SubMesh>     submeshes;
    Aabb                     bounds;
};

// ---------------------------------------------------------------------------
// GpuMesh — GPU-resident mesh with RHI buffer handles.
// ---------------------------------------------------------------------------
struct GpuMesh {
    RhiBuffer  vertex_buffer = RHI_NULL;
    RhiBuffer  index_buffer  = RHI_NULL;
    uint32_t   vertex_count  = 0;
    uint32_t   index_count   = 0;
    uint32_t   vertex_stride = sizeof(MeshVertex);
    std::vector<SubMesh> submeshes;
    Aabb       bounds;
};

/// Create GPU buffers from CPU mesh data. Returns a GpuMesh with valid handles
/// on success, or with RHI_NULL handles on failure.
[[nodiscard]] GpuMesh create_gpu_mesh(const MeshData& data);

/// Destroy GPU buffers. Resets handles to RHI_NULL.
void destroy_gpu_mesh(GpuMesh& mesh);

} // namespace uz
