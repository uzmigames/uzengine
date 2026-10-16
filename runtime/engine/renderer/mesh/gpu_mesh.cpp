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

    // GPU-skinned output: compute shader writes each frame via UAV. Also
    // keep the structured-buffer SRV so bindless meshes can still read it
    // as a VB. The initial state is CopyDst so the bind-pose UploadBuffer
    // below succeeds before the per-frame UAV→VertexBuffer transitions.
    if (mesh.gpuSkinned) {
        desc.usage = desc.usage |
                     RHIBufferUsage::StructuredBuffer |
                     RHIBufferUsage::UnorderedAccess;
        desc.debugName = "GPUMesh_VB_Skinned";
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

static bool UploadColors(IRHIDevice* device, IRHICommandList* cmdList,
                         UploadContext& upload, const Mesh& mesh, GPUMesh& gpu) {
    // SM5 uses the documented white fallback; no side buffer is needed.
    if (mesh.vertexColors.empty() || !device->IsBindless()) return true;
    RHIBufferDesc desc{};
    desc.sizeBytes = mesh.vertexColors.size() * sizeof(uint32_t);
    desc.stride = sizeof(uint32_t);
    desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
    desc.memory = RHIMemoryType::Default;
    desc.debugName = "GPUMesh_Colors";
    gpu.colorBuffer = device->CreateBuffer(desc);
    if (!gpu.colorBuffer.IsValid()) return false;
    if (!upload.UploadBuffer(cmdList, gpu.colorBuffer, mesh.vertexColors.data(), desc.sizeBytes)) {
        SafeDestroyBuffer(device, gpu.colorBuffer);
        return false;
    }
    gpu.colorSrvIndex = device->GetBufferSRVIndex(gpu.colorBuffer);
    return true;
}

GPUMesh UploadMesh(IRHIDevice* device, IRHICommandList* cmdList, const Mesh& mesh) {
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "UploadMesh: null device or command list");
        return {};
    }

    if (!mesh.IsValid() || (!mesh.vertexColors.empty() && mesh.vertexColors.size() != mesh.GetVertexCount())) {
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

    if (!UploadColors(device, cmdList, upload, mesh, result)) {
        UZ_LOG(Renderer, Error, "UploadMesh: failed to upload color stream");
        SafeDestroyBuffer(device, result.vertexBuffer);
        SafeDestroyBuffer(device, result.indexBuffer);
        return {};
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

    // -- Mesh AABB bounds (used for bounding sphere / LOD screen-size computation) --
    result.boundsMin = mesh.header.boundsMin;
    result.boundsMax = mesh.header.boundsMax;

    // -- Vertex stride (48 = Vertex, 32 = VertexPacked) --
    result.vertexStride = mesh.header.vertexStride;

    // -- Bindless SRV index --
    if (device->IsBindless()) {
        result.vbSrvIndex = device->GetBufferSRVIndex(result.vertexBuffer);
    }

    UZ_LOG(Renderer, Info, "UploadMesh: uploaded {} vertices, {} indices, stride={} (bindless SRV={})",
           result.vertexCount, result.indexCount, result.vertexStride,
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

    if (!mesh.IsValid() || (!mesh.vertexColors.empty() && mesh.vertexColors.size() != mesh.GetVertexCount())) {
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

    if (!UploadColors(device, cmdList, result.upload, mesh, result.mesh)) {
        UZ_LOG(Renderer, Error, "UploadMeshDeferred: failed to upload color stream");
        SafeDestroyBuffer(device, result.mesh.vertexBuffer);
        SafeDestroyBuffer(device, result.mesh.indexBuffer);
        return result;
    }

    // -- Populate metadata (no GPU completion needed) --
    result.mesh.vertexCount = mesh.GetVertexCount();
    result.mesh.vertexStride = mesh.header.vertexStride;
    result.mesh.indexCount  = mesh.GetIndexCount();
    result.mesh.indexFormat  = (mesh.header.indexFormat == MeshIndexFormat::UInt16)
                                  ? RHIIndexFormat::Uint16
                                  : RHIIndexFormat::Uint32;

    // -- LOD metadata --
    result.mesh.lodCount = mesh.GetLODCount();
    for (uint32_t i = 0; i < 3; ++i) {
        result.mesh.lodInfo[i].indexOffset = mesh.GetLODIndexOffset(i + 1);
        result.mesh.lodInfo[i].indexCount  = mesh.GetLODIndexCount(i + 1);
    }

    // -- Mesh AABB bounds (used for bounding sphere / LOD screen-size computation) --
    result.mesh.boundsMin = mesh.header.boundsMin;
    result.mesh.boundsMax = mesh.header.boundsMax;

    // -- Bindless SRV index --
    if (device->IsBindless()) {
        result.mesh.vbSrvIndex = device->GetBufferSRVIndex(result.mesh.vertexBuffer);
    }

    UZ_LOG(Renderer, Info, "UploadMeshDeferred: queued {} vertices, {} indices, {} LODs (bindless SRV={})",
           result.mesh.vertexCount, result.mesh.indexCount, result.mesh.lodCount,
           result.mesh.vbSrvIndex != UINT32_MAX ? static_cast<int>(result.mesh.vbSrvIndex) : -1);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// FlushDeferredMeshUploads
// ─────────────────────────────────────────────────────────────────────────────

void FlushDeferredMeshUploads(IRHIDevice* device, IRHICommandList* cmdList,
                              std::vector<DeferredMeshUpload>& uploads,
                              std::vector<RHIBufferHandle>* deferredRelease) {
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "FlushDeferredMeshUploads: null device or command list");
        return;
    }

    // Single GPU submission for all batched copy commands. Submitting here
    // (before the caller records draws on the re-begun list) keeps the D3D12
    // implicit state promotion/decay semantics: buffers decay to COMMON at the
    // ExecuteCommandLists boundary, so later draws read-promote them without
    // explicit barriers, and same-queue FIFO ordering guarantees the copies
    // complete before any subsequent draw executes.
    cmdList->End();
    cmdList->Execute();

    if (deferredRelease) {
        // Stall-free path: hand staging buffers to the caller's retire queue.
        // The GPU may still be reading them — the caller destroys them only
        // after the frames-in-flight fence guarantees completion.
        for (auto& upload : uploads) {
            for (RHIBufferHandle handle : upload.upload.TakeStagingBuffers()) {
                deferredRelease->push_back(handle);
            }
        }
    } else {
        // Legacy path: full device drain, then immediate release.
        device->WaitIdle();
        for (auto& upload : uploads) {
            upload.upload.ReleaseStaging();
        }
    }

    // Resume recording so the caller can continue using the command list.
    cmdList->Begin();

    UZ_LOG(Renderer, Info, "FlushDeferredMeshUploads: flushed {} deferred mesh uploads{}",
           uploads.size(), deferredRelease ? " (stall-free)" : "");
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
    SafeDestroyBuffer(device, gpuMesh.colorBuffer);
    gpuMesh.colorSrvIndex = 0;

    // Destroy the SDF 3D texture if it was baked for this mesh.
    if (gpuMesh.sdf.texture3D.IsValid()) {
        device->DestroyTexture(gpuMesh.sdf.texture3D);
    }
    SafeDestroyBuffer(device, gpuMesh.sdf.triangleBuffer);
    gpuMesh.sdf = MeshSDF{};

    gpuMesh.vbSrvIndex  = UINT32_MAX;
    gpuMesh.indexCount   = 0;
    gpuMesh.vertexCount  = 0;
    gpuMesh.indexFormat   = RHIIndexFormat::Uint32;
}

} // namespace uz
