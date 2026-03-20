/// UzEngine Global Distance Field — Implementation.
///
/// Creates and manages 4 cascaded clipmap R16F 3D textures for world-space
/// signed distance field tracing. Each frame, dirty clipmaps are recomposited
/// from all mesh SDFs via a compute shader dispatch.
///
/// @source UE5 GlobalDistanceField.cpp:23-46, 120-126, 383-403, 446-457

#include "global_distance_field.h"

#include "mesh_sdf_baker.h"
#include "cb_ring_allocator.h"
#include "log.h"
#include "rhi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Destruction
// ─────────────────────────────────────────────────────────────────────────────

GlobalDistanceField::~GlobalDistanceField() {
    DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateResources
// ─────────────────────────────────────────────────────────────────────────────

bool GlobalDistanceField::CreateResources(IRHIDevice* device) {
    if (!device) {
        UZ_LOG(Renderer, Error, "GlobalDistanceField::CreateResources: null device");
        return false;
    }

    if (m_initialized) {
        DestroyResources();
    }

    m_device = device;

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];
        clip.extent = kClipmapExtents[i];
        clip.center = Vec3{0.0f, 0.0f, 0.0f};
        clip.dirty  = true;

        RHITextureDesc texDesc{};
        texDesc.width            = kResolution;
        texDesc.height           = kResolution;
        texDesc.depthOrArraySize = static_cast<uint16_t>(kResolution);
        texDesc.mipLevels        = 1;
        texDesc.format           = RHIFormat::R16Float;
        texDesc.usage            = RHITextureUsage::ShaderResource
                                 | RHITextureUsage::UnorderedAccess;
        texDesc.is3D             = true;

        // Build a unique debug name per cascade
        char debugName[64];
        std::snprintf(debugName, sizeof(debugName), "GDF_Clipmap%u", i);
        texDesc.debugName = debugName;

        clip.texture = device->CreateTexture(texDesc);
        if (!clip.texture.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "GlobalDistanceField: failed to create clipmap {} texture ({}^3 R16F)",
                   i, kResolution);
            DestroyResources();
            return false;
        }

        clip.srvIndex = device->GetTextureSRVIndex(clip.texture);
        clip.uavIndex = device->GetTextureUAVIndex(clip.texture);

        UZ_LOG(Renderer, Info,
               "GlobalDistanceField: clipmap {} created ({}^3 R16F, extent={:.0f}cm, SRV={}, UAV={})",
               i, kResolution, clip.extent, clip.srvIndex, clip.uavIndex);
    }

    m_initialized = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyResources
// ─────────────────────────────────────────────────────────────────────────────

void GlobalDistanceField::DestroyResources() {
    if (!m_device) return;

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];
        if (clip.texture.IsValid()) {
            m_device->DestroyTexture(clip.texture);
            clip.texture  = RHITextureHandle{};
            clip.srvIndex = UINT32_MAX;
            clip.uavIndex = UINT32_MAX;
        }
        clip.dirty = true;
    }

    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — recenter clipmaps on camera, mark dirty as needed
// ─────────────────────────────────────────────────────────────────────────────

void GlobalDistanceField::Update(const Vec3& cameraPos) {
    if (!m_initialized) return;

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];

        // Voxel size for this cascade: full extent / resolution
        /// @source UE5 GlobalDistanceField.cpp:383-403 — clipmap snapping
        const float voxelSize = (clip.extent * 2.0f) / static_cast<float>(kResolution);

        // Snap the clipmap center to voxel grid to avoid swimming artifacts.
        // Re-center when camera moves more than half a voxel from the current center.
        Vec3 snappedCenter;
        snappedCenter.x = std::floor(cameraPos.x / voxelSize) * voxelSize;
        snappedCenter.y = std::floor(cameraPos.y / voxelSize) * voxelSize;
        snappedCenter.z = std::floor(cameraPos.z / voxelSize) * voxelSize;

        // Check if the center changed (moved by at least one voxel)
        const float dx = std::abs(snappedCenter.x - clip.center.x);
        const float dy = std::abs(snappedCenter.y - clip.center.y);
        const float dz = std::abs(snappedCenter.z - clip.center.z);

        if (dx >= voxelSize || dy >= voxelSize || dz >= voxelSize) {
            clip.center = snappedCenter;
            clip.dirty  = true;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CompositeFromMeshSDFs — dispatch compute shader for each dirty clipmap
// ─────────────────────────────────────────────────────────────────────────────

void GlobalDistanceField::CompositeFromMeshSDFs(
    IRHICommandList* cmdList,
    const MeshSDF* meshSDFs,
    const Mat4* meshTransforms,
    uint32_t meshSdfCount,
    RHIPipelineHandle compositePipeline,
    CBRingAllocator* cbRing,
    RHIBufferHandle perFrameBuffer,
    uint32_t perFrameOffset,
    const void* perFrameCpuPtr,
    uint32_t perFrameSize)
{
    if (!m_initialized || !cmdList || !compositePipeline.IsValid() || !cbRing) {
        return;
    }

    if (meshSdfCount == 0 || !meshSDFs || !meshTransforms) {
        return;
    }

    // ── Upload mesh SDF data to a StructuredBuffer via CB ring ──────────────
    // Each mesh SDF gets a GdfMeshSdfGPU entry with its world-to-local transform,
    // volume origin/extent, and SRV index.
    const uint32_t meshDataSize = meshSdfCount * sizeof(GdfMeshSdfGPU);
    CBAllocation meshDataAlloc = cbRing->Allocate(meshDataSize);
    if (!meshDataAlloc.cpuPtr) {
        UZ_LOG(Renderer, Error, "GlobalDistanceField: failed to allocate mesh SDF data ({} bytes)", meshDataSize);
        return;
    }

    auto* gpuMeshes = static_cast<GdfMeshSdfGPU*>(meshDataAlloc.cpuPtr);
    for (uint32_t m = 0; m < meshSdfCount; ++m) {
        const auto& sdf = meshSDFs[m];
        const auto& worldTransform = meshTransforms[m];

        auto& entry = gpuMeshes[m];
        entry.originX = sdf.volumeOrigin[0];
        entry.originY = sdf.volumeOrigin[1];
        entry.originZ = sdf.volumeOrigin[2];
        entry.padding0 = 0.0f;

        entry.extentX = sdf.volumeExtent[0];
        entry.extentY = sdf.volumeExtent[1];
        entry.extentZ = sdf.volumeExtent[2];
        entry.padding1 = 0.0f;

        // Use cached inverse transform from MeshSDF (precomputed at collection time).
        // Avoids per-frame Mat4::Inverse() for every mesh.
        std::memcpy(entry.invTransform, sdf.invTransform, sizeof(entry.invTransform));

        entry.srvIndex   = sdf.srvIndex;
        entry.resolution = sdf.resolution;
        entry._pad2[0]   = 0;
        entry._pad2[1]   = 0;
    }

    // The mesh data buffer SRV index is accessible via the CB ring's buffer.
    // For bindless access, we pass the SRV index in root constants.
    // Since the CB ring buffer doesn't have a structured SRV, we'll use
    // a descriptor table binding or pass the buffer offset in constants.
    // For simplicity, we bind the mesh data via a known descriptor index.
    // The composite shader reads MeshSDF data via root constants pointing
    // to the CB ring offset.

    // Set the composite pipeline
    cmdList->SetPipeline(compositePipeline);

    // ── Dispatch per dirty clipmap ──────────────────────────────────────────
    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];
        if (!clip.dirty) continue;

        // Transition clipmap texture to UAV for compute write
        RHIBarrierDesc toUav{};
        toUav.texture     = clip.texture;
        toUav.stateBefore = RHIResourceState::ShaderResource;
        toUav.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&toUav, 1);

        // Build root constants for this clipmap (8 uint32s at root param 0)
        GdfCompositeConstants constants{};
        constants.outputUav      = clip.uavIndex;
        constants.meshCount      = meshSdfCount;
        constants.clipmapIndex   = i;
        constants.resolution     = kResolution;
        constants.clipmapCenterX = clip.center.x;
        constants.clipmapCenterY = clip.center.y;
        constants.clipmapCenterZ = clip.center.z;
        constants.clipmapExtent  = clip.extent;

        // Bind as root constants (slot 0, 8 uint32s)
        cmdList->SetRootConstants(0,
            reinterpret_cast<const uint32_t*>(&constants), 8);

        // Bind mesh SDF data at b2 (or via structured buffer descriptor)
        cmdList->SetRootCBV(2, meshDataAlloc.bufferHandle,
                            meshDataAlloc.offsetBytes,
                            meshDataAlloc.cpuPtr, meshDataSize);

        // Dispatch: 4x4x4 thread groups to cover 64^3 voxels
        const uint32_t groupsPerAxis = (kResolution + 3) / 4;
        cmdList->Dispatch(groupsPerAxis, groupsPerAxis, groupsPerAxis);

        // Transition back to SRV for subsequent trace reads
        RHIBarrierDesc toSrv{};
        toSrv.texture     = clip.texture;
        toSrv.stateBefore = RHIResourceState::UnorderedAccess;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&toSrv, 1);

        clip.dirty = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

const GlobalDistanceField::ClipmapData& GlobalDistanceField::GetClipmap(uint32_t index) const {
    static const ClipmapData kInvalid{};
    if (index >= kNumClipmaps) return kInvalid;
    return m_clipmaps[index];
}

float GlobalDistanceField::GetVoxelSize(uint32_t cascadeIndex) const {
    if (cascadeIndex >= kNumClipmaps) return 0.0f;
    return (m_clipmaps[cascadeIndex].extent * 2.0f) / static_cast<float>(kResolution);
}

} // namespace uz
