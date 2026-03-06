// ---------------------------------------------------------------------------
// mesh.cpp -- GPU mesh creation and destruction via RHI
//
// RULE RHI-1: No backend types above rhi.h
// RULE CS-7:  No exceptions
// ---------------------------------------------------------------------------

#include "mesh.h"

#include "engine/core/diag.h"

namespace uz {

GpuMesh create_gpu_mesh(const MeshData& data) {
    GpuMesh mesh;

    if (data.vertices.empty()) {
        DIAG("create_gpu_mesh: empty vertex data");
        return mesh;
    }

    // Create vertex buffer.
    const size_t vb_size = data.vertices.size() * sizeof(MeshVertex);
    RhiBufferDesc vb_desc{};
    vb_desc.size  = vb_size;
    vb_desc.usage = RHI_BUFFER_USAGE_VERTEX;
    vb_desc.debug_name = "MeshVertexBuffer";

    mesh.vertex_buffer = rhi()->buffer_create(&vb_desc);
    if (mesh.vertex_buffer == RHI_NULL) {
        DIAG("create_gpu_mesh: vertex buffer creation failed");
        return mesh;
    }
    rhi()->buffer_upload(mesh.vertex_buffer, data.vertices.data(), vb_size);

    // Create index buffer (if indices exist).
    if (!data.indices.empty()) {
        const size_t ib_size = data.indices.size() * sizeof(uint32_t);
        RhiBufferDesc ib_desc{};
        ib_desc.size  = ib_size;
        ib_desc.usage = RHI_BUFFER_USAGE_INDEX;
        ib_desc.debug_name = "MeshIndexBuffer";

        mesh.index_buffer = rhi()->buffer_create(&ib_desc);
        if (mesh.index_buffer == RHI_NULL) {
            DIAG("create_gpu_mesh: index buffer creation failed");
            rhi()->buffer_destroy(mesh.vertex_buffer);
            mesh.vertex_buffer = RHI_NULL;
            return mesh;
        }
        rhi()->buffer_upload(mesh.index_buffer, data.indices.data(), ib_size);
        mesh.index_count = static_cast<uint32_t>(data.indices.size());
    }

    mesh.vertex_count  = static_cast<uint32_t>(data.vertices.size());
    mesh.vertex_stride = sizeof(MeshVertex);
    mesh.submeshes     = data.submeshes;
    mesh.bounds        = data.bounds;

    DIAG_FMT("create_gpu_mesh: %u verts, %u indices, %zu submeshes",
             mesh.vertex_count, mesh.index_count, mesh.submeshes.size());
    return mesh;
}

void destroy_gpu_mesh(GpuMesh& mesh) {
    if (mesh.vertex_buffer != RHI_NULL) {
        rhi()->buffer_destroy(mesh.vertex_buffer);
        mesh.vertex_buffer = RHI_NULL;
    }
    if (mesh.index_buffer != RHI_NULL) {
        rhi()->buffer_destroy(mesh.index_buffer);
        mesh.index_buffer = RHI_NULL;
    }
    mesh.vertex_count = 0;
    mesh.index_count  = 0;
    mesh.submeshes.clear();
}

} // namespace uz
