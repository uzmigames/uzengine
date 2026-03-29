/// UzEngine GPU Mesh — Implementation

#include "gpu_mesh.h"
#include "rhi.h"
#include "mesh.h"
#include "upload_context.h"
#include "log.h"

// Log category declared at global scope (matching log.cpp definition)
UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// UploadMesh
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a vertex buffer desc appropriate for the device's bindless capability.
static RHIBufferDesc MakeVertexBufferDesc(IRHIDevice* device, const Mesh& mesh) {
    RHIBufferDesc desc{};
    desc.sizeBytes = mesh.GetVertexDataSize();
    desc.memory    = RHIMemoryType::Default;

    if (device->IsBindless()) {
        // SM6.6: structured buffer with SRV for bindless vertex fetch
        desc.usage  = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        desc.stride = mesh.header.vertexStride;
        desc.debugName = "GPUMesh_VB_Structured";
    } else {
        desc.usage  = RHIBufferUsage::VertexBuffer | RHIBufferUsage::CopyDst;
        desc.stride = mesh.header.vertexStride;
        desc.debugName = "GPUMesh_VB";
    }

    return desc;
}

/// Creates an index buffer desc sized for the mesh's index data.
static RHIBufferDesc MakeIndexBufferDesc(const Mesh& mesh) {
    RHIBufferDesc desc{};
    desc.sizeBytes = mesh.GetIndexDataSize();
    desc.stride    = (mesh.header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    desc.usage     = RHIBufferUsage::IndexBuffer | RHIBufferUsage::CopyDst;
    desc.memory    = RHIMemoryType::Default;
    desc.debugName = "GPUMesh_IB";
    return desc;
}

/// Destroys a single buffer handle if valid, then invalidates it.
static void SafeDestroyBuffer(IRHIDevice* device, RHIBufferHandle& handle) {
    if (handle.IsValid()) {
        device->DestroyBuffer(handle);
        handle = {};
    }
}

GPUMesh UploadMesh(IRHIDevice* device, IRHICommandList* cmdList, const Mesh& mesh) {
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "UploadMesh: null device or command list");
        return {};
    }

    if (!mesh.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadMesh: invalid mesh (failed IsValid check)");
        return {};
    }

    GPUMesh result{};

    // -- Create vertex buffer --
    const RHIBufferDesc vbDesc = MakeVertexBufferDesc(device, mesh);
    result.vertexBuffer = device->CreateBuffer(vbDesc);
    if (!result.vertexBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadMesh: failed to create vertex buffer ({} bytes)", vbDesc.sizeBytes);
        return {};
    }

    // -- Create index buffer (optional) --
    const bool hasIndices = mesh.GetIndexCount() > 0;
    if (hasIndices) {
        const RHIBufferDesc ibDesc = MakeIndexBufferDesc(mesh);
        result.indexBuffer = device->CreateBuffer(ibDesc);
        if (!result.indexBuffer.IsValid()) {
            UZ_LOG(Renderer, Error, "UploadMesh: failed to create index buffer ({} bytes)", ibDesc.sizeBytes);
            SafeDestroyBuffer(device, result.vertexBuffer);
            return {};
        }
    }

    // -- Upload via staging buffers --
    UploadContext upload(device);

    if (!upload.UploadBuffer(cmdList, result.vertexBuffer, mesh.vertexData.data(), mesh.GetVertexDataSize())) {
        UZ_LOG(Renderer, Error, "UploadMesh: failed to upload vertex data ({} bytes)", mesh.GetVertexDataSize());
        SafeDestroyBuffer(device, result.vertexBuffer);
        SafeDestroyBuffer(device, result.indexBuffer);
        return {};
    }

    if (hasIndices) {
        if (!upload.UploadBuffer(cmdList, result.indexBuffer, mesh.indexData.data(), mesh.GetIndexDataSize())) {
            UZ_LOG(Renderer, Error, "UploadMesh: failed to upload index data ({} bytes)", mesh.GetIndexDataSize());
            SafeDestroyBuffer(device, result.vertexBuffer);
            SafeDestroyBuffer(device, result.indexBuffer);
            return {};
        }
    }

    // -- Execute copies and wait for GPU --
    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();
    upload.ReleaseStaging();
    cmdList->Begin();

    // -- Populate metadata --
    result.vertexCount = mesh.GetVertexCount();
    result.indexCount  = mesh.header.indexCount; // LOD0 index count
    result.indexFormat  = (mesh.header.indexFormat == MeshIndexFormat::UInt16)
                             ? RHIIndexFormat::Uint16
                             : RHIIndexFormat::Uint32;

    // -- LOD info --
    result.lodCount = mesh.GetLODCount();
    for (uint32_t i = 0; i < 3; ++i) {
        result.lodInfo[i].indexOffset = mesh.GetLODIndexOffset(i + 1);
        result.lodInfo[i].indexCount  = mesh.GetLODIndexCount(i + 1);
    }

    // -- Bindless SRV index --
    if (device->IsBindless()) {
        result.vbSrvIndex = device->GetBufferSRVIndex(result.vertexBuffer);
    }

    UZ_LOG(Renderer, Info, "UploadMesh: uploaded {} vertices, {} indices (bindless SRV={})",
           result.vertexCount, result.indexCount,
           result.vbSrvIndex != UINT32_MAX ? static_cast<int>(result.vbSrvIndex) : -1);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// UploadMeshDeferred
// ─────────────────────────────────────────────────────────────────────────────

DeferredMeshUpload UploadMeshDeferred(IRHIDevice* device, IRHICommandList* cmdList, const Mesh& mesh) {
    DeferredMeshUpload result(device);

    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "UploadMeshDeferred: null device or command list");
        return result;
    }

    if (!mesh.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadMeshDeferred: invalid mesh (failed IsValid check)");
        return result;
    }

    // -- Create vertex buffer --
    const RHIBufferDesc vbDesc = MakeVertexBufferDesc(device, mesh);
    result.mesh.vertexBuffer = device->CreateBuffer(vbDesc);
    if (!result.mesh.vertexBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadMeshDeferred: failed to create vertex buffer ({} bytes)", vbDesc.sizeBytes);
        return result;
    }

    // -- Create index buffer (optional) --
    const bool hasIndices = mesh.GetIndexCount() > 0;
    if (hasIndices) {
        const RHIBufferDesc ibDesc = MakeIndexBufferDesc(mesh);
        result.mesh.indexBuffer = device->CreateBuffer(ibDesc);
        if (!result.mesh.indexBuffer.IsValid()) {
            UZ_LOG(Renderer, Error, "UploadMeshDeferred: failed to create index buffer ({} bytes)", ibDesc.sizeBytes);
            SafeDestroyBuffer(device, result.mesh.vertexBuffer);
            return result;
        }
    }

    // -- Upload via staging buffers (commands recorded, NOT executed) --
    if (!result.upload.UploadBuffer(cmdList, result.mesh.vertexBuffer, mesh.vertexData.data(), mesh.GetVertexDataSize())) {
        UZ_LOG(Renderer, Error, "UploadMeshDeferred: failed to upload vertex data ({} bytes)", mesh.GetVertexDataSize());
        SafeDestroyBuffer(device, result.mesh.vertexBuffer);
        SafeDestroyBuffer(device, result.mesh.indexBuffer);
        return result;
    }

    if (hasIndices) {
        if (!result.upload.UploadBuffer(cmdList, result.mesh.indexBuffer, mesh.indexData.data(), mesh.GetIndexDataSize())) {
            UZ_LOG(Renderer, Error, "UploadMeshDeferred: failed to upload index data ({} bytes)", mesh.GetIndexDataSize());
            SafeDestroyBuffer(device, result.mesh.vertexBuffer);
            SafeDestroyBuffer(device, result.mesh.indexBuffer);
            return result;
        }
    }

    // -- Populate metadata (no GPU completion needed) --
    result.mesh.vertexCount = mesh.GetVertexCount();
    result.mesh.indexCount  = mesh.GetIndexCount();
    result.mesh.indexFormat  = (mesh.header.indexFormat == MeshIndexFormat::UInt16)
                                  ? RHIIndexFormat::Uint16
                                  : RHIIndexFormat::Uint32;

    // -- Bindless SRV index --
    if (device->IsBindless()) {
        result.mesh.vbSrvIndex = device->GetBufferSRVIndex(result.mesh.vertexBuffer);
    }

    UZ_LOG(Renderer, Info, "UploadMeshDeferred: queued {} vertices, {} indices (bindless SRV={})",
           result.mesh.vertexCount, result.mesh.indexCount,
           result.mesh.vbSrvIndex != UINT32_MAX ? static_cast<int>(result.mesh.vbSrvIndex) : -1);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// FlushDeferredMeshUploads
// ─────────────────────────────────────────────────────────────────────────────

void FlushDeferredMeshUploads(IRHIDevice* device, IRHICommandList* cmdList,
                              std::vector<DeferredMeshUpload>& uploads) {
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "FlushDeferredMeshUploads: null device or command list");
        return;
    }

    // Single GPU submission for all batched copy commands.
    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();

    // All copies are complete — safe to release staging buffers.
    for (auto& upload : uploads) {
        upload.upload.ReleaseStaging();
    }

    // Resume recording so the caller can continue using the command list.
    cmdList->Begin();

    UZ_LOG(Renderer, Info, "FlushDeferredMeshUploads: flushed {} deferred mesh uploads", uploads.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyGPUMesh
// ─────────────────────────────────────────────────────────────────────────────

void DestroyGPUMesh(IRHIDevice* device, GPUMesh& gpuMesh) {
    if (!device) {
        UZ_LOG(Renderer, Error, "DestroyGPUMesh: null device");
        return;
    }

    SafeDestroyBuffer(device, gpuMesh.vertexBuffer);
    SafeDestroyBuffer(device, gpuMesh.indexBuffer);

    // Destroy the SDF 3D texture if it was baked for this mesh.
    if (gpuMesh.sdf.texture3D.IsValid()) {
        device->DestroyTexture(gpuMesh.sdf.texture3D);
    }
    gpuMesh.sdf = MeshSDF{};

    gpuMesh.vbSrvIndex  = UINT32_MAX;
    gpuMesh.indexCount   = 0;
    gpuMesh.vertexCount  = 0;
    gpuMesh.indexFormat   = RHIIndexFormat::Uint32;
}

} // namespace uz
