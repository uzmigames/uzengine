/// BLASManager Implementation — Per-mesh BLAS creation and caching.

#include "blas_manager.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "log.h"

namespace uz {

void BLASManager::Init(IRHIDevice* device) {
    m_device = device;
}

RHIBLASHandle BLASManager::GetOrBuildBLAS(uint32_t meshId, const GPUMesh& mesh,
                                           IRHICommandList* cmdList) {
    if (!m_device || !cmdList) return RHIBLASHandle::Invalid();

    // Check cache
    auto it = m_cache.find(meshId);
    if (it != m_cache.end() && it->second.IsValid()) {
        return it->second;
    }

    // Validate mesh has geometry
    if (!mesh.IsValid() || mesh.vertexCount == 0) {
        UZ_LOG(Renderer, Warning, "BLASManager: Mesh {} has no valid geometry, skipping BLAS",
               meshId);
        return RHIBLASHandle::Invalid();
    }

    // Build geometry description from GPUMesh
    RHIRTGeometryDesc geom = {};
    geom.vertexBuffer = mesh.vertexBuffer;
    geom.vertexCount  = mesh.vertexCount;
    geom.vertexStride = 48; // sizeof(Vertex): position(12) + normal(12) + texcoord(8) + tangent(16)
    geom.vertexFormat = RHIFormat::RGB32Float;
    geom.isOpaque     = true;

    if (mesh.IsIndexed()) {
        geom.indexBuffer = mesh.indexBuffer;
        geom.indexCount  = mesh.indexCount;
        geom.indexFormat  = mesh.indexFormat;
    }

    // Create BLAS
    RHIBLASDesc blasDesc = {};
    blasDesc.geometries    = &geom;
    blasDesc.geometryCount = 1;
    blasDesc.buildFlags    = RHIASBuildFlags::PreferFastTrace | RHIASBuildFlags::AllowCompaction;
    blasDesc.debugName     = "MeshBLAS";

    RHIBLASHandle handle = m_device->CreateBLAS(blasDesc);
    if (!handle.IsValid()) {
        UZ_LOG(Renderer, Error, "BLASManager: Failed to create BLAS for mesh {}", meshId);
        return RHIBLASHandle::Invalid();
    }

    // Build the BLAS on the command list (uses internal scratch buffer)
    cmdList->BuildBLAS(handle, RHIBufferHandle::Invalid(), false);

    // UAV barrier after build — BLAS must be visible before TLAS references it
    cmdList->UAVBarrierAS();

    // Cache the handle
    m_cache[meshId] = handle;

    UZ_LOG(Renderer, Info, "BLASManager: Built BLAS for mesh {} (total cached: {})",
           meshId, m_cache.size());
    return handle;
}

void BLASManager::InvalidateMesh(uint32_t meshId) {
    auto it = m_cache.find(meshId);
    if (it != m_cache.end()) {
        if (m_device && it->second.IsValid()) {
            m_device->DestroyBLAS(it->second);
        }
        m_cache.erase(it);
    }
}

void BLASManager::Shutdown() {
    if (m_device) {
        for (auto& [id, handle] : m_cache) {
            if (handle.IsValid()) {
                m_device->DestroyBLAS(handle);
            }
        }
    }
    m_cache.clear();
    m_device = nullptr;
}

} // namespace uz
