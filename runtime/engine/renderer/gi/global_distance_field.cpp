/// UzEngine Global Distance Field — Implementation.
///
/// Creates and manages 4 cascaded clipmap RGBA16F 3D textures for world-space
/// signed distance field tracing. Each frame, dirty clipmaps are recomposited
/// from all mesh SDFs via a compute shader dispatch.
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — cascaded GDF layout.

#include "global_distance_field.h"

#include "mesh_sdf_baker.h"
#include "cb_ring_allocator.h"
#include "log.h"
#include "rhi.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

static auto* cv_GdfResolution = UZ_CVAR_INT("r.GI.GDF.Resolution", 64,
    "Voxels per clipmap axis (requires scene reload)", 32, 256);

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
    const uint32_t resolution = static_cast<uint32_t>(cv_GdfResolution->value);

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];
        clip.extent = kClipmapExtents[i];
        clip.center = Vec3{0.0f, 0.0f, 0.0f};
        clip.dirty  = true;
        clip.hasData = false;

        RHITextureDesc texDesc{};
        texDesc.width            = resolution;
        texDesc.height           = resolution;
        texDesc.depthOrArraySize = static_cast<uint16_t>(resolution);
        texDesc.mipLevels        = 1;
        texDesc.format           = RHIFormat::RGBA16Float;
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
                   "GlobalDistanceField: failed to create clipmap {} texture ({}^3 RGBA16F)",
                   i, resolution);
            DestroyResources();
            return false;
        }

        clip.srvIndex = device->GetTextureSRVIndex(clip.texture);
        clip.uavIndex = device->GetTextureUAVIndex(clip.texture);
        texDesc.format = RHIFormat::R16Float;
        std::snprintf(debugName, sizeof(debugName), "GDF_RawDistance%u", i);
        clip.rawTexture = device->CreateTexture(texDesc);
        if (!clip.rawTexture.IsValid()) {
            UZ_LOG(Renderer, Error, "GlobalDistanceField: failed to create raw clipmap {} ({}^3 R16F)", i, resolution);
            DestroyResources();
            return false;
        }
        clip.rawSrvIndex = device->GetTextureSRVIndex(clip.rawTexture);
        clip.rawUavIndex = device->GetTextureUAVIndex(clip.rawTexture);
        if (clip.srvIndex == UINT32_MAX || clip.uavIndex == UINT32_MAX
            || clip.rawSrvIndex == UINT32_MAX || clip.rawUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GlobalDistanceField: missing descriptor for clipmap pair {}", i);
            DestroyResources();
            return false;
        }

        UZ_LOG(Renderer, Info,
               "GlobalDistanceField: clipmap {} created ({}^3 RGBA16F+R16F, extent={:.0f}cm, SRV={}, UAV={}, rawSRV={}, rawUAV={})",
               i, resolution, clip.extent, clip.srvIndex, clip.uavIndex, clip.rawSrvIndex, clip.rawUavIndex);
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
        if (clip.rawTexture.IsValid()) {
            m_device->DestroyTexture(clip.rawTexture);
            clip.rawTexture = RHITextureHandle{};
        }
        clip.rawSrvIndex = UINT32_MAX;
        clip.rawUavIndex = UINT32_MAX;
        clip.dirty = true;
        clip.hasData = false;
    }

    m_initialized = false;
    m_meshDataHash = 0;
    m_meshDataCount = UINT32_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — recenter clipmaps on camera, mark dirty as needed
// ─────────────────────────────────────────────────────────────────────────────

void GlobalDistanceField::Update(const Vec3& cameraPos) {
    if (!m_initialized) return;

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];

        // Voxel size for this cascade: full extent / resolution
        /// Snap clipmap center to voxel grid to prevent swimming artifacts.
        const float voxelSize = GetVoxelSize(i);

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
    const Mat4*,
    uint32_t meshSdfCount,
    RHIPipelineHandle compositePipeline,
    CBRingAllocator* cbRing,
    RHIBufferHandle,
    uint32_t,
    const void*,
    uint32_t)
{
    if (!m_initialized || !cmdList || !compositePipeline.IsValid() || !cbRing) {
        return;
    }

    if (meshSdfCount > 0 && !meshSDFs) {
        return;
    }

    std::vector<GdfMeshSdfGPU> gpuMeshes;
    gpuMeshes.reserve(meshSdfCount);
    for (uint32_t m = 0; m < meshSdfCount; ++m) {
        const auto& sdf = meshSDFs[m];
        if (!sdf.valid || sdf.srvIndex == UINT32_MAX || sdf.resolution == 0
            || sdf.volumeExtent[0] <= 0 || sdf.volumeExtent[1] <= 0 || sdf.volumeExtent[2] <= 0) continue;

        GdfMeshSdfGPU entry{};
        entry.originX = sdf.volumeOrigin[0];
        entry.originY = sdf.volumeOrigin[1];
        entry.originZ = sdf.volumeOrigin[2];
        entry.extentX = sdf.volumeExtent[0];
        entry.extentY = sdf.volumeExtent[1];
        entry.extentZ = sdf.volumeExtent[2];
        entry.surfaceBias = sdf.twoSided
            ? std::max({entry.extentX, entry.extentY, entry.extentZ}) / sdf.resolution : 0.0f;
        std::memcpy(entry.invTransform, sdf.invTransform, sizeof(entry.invTransform));
        entry.srvIndex   = sdf.srvIndex;
        entry.resolution = sdf.resolution;
        entry.albedoSrv = sdf.albedoSrv;
        entry.metallicSrv = sdf.metallicSrv;
        std::memcpy(entry.baseColorTint, sdf.baseColorTint, sizeof(entry.baseColorTint));
        entry.metallicMultiplier = sdf.metallicMultiplier;
        entry.materialFlags = sdf.materialFlags;

        // Gershgorin bounds the largest eigenvalue of A^T A, where A is the
        // world-to-local linear transform. Its inverse square root bounds
        // distances conservatively even for nonuniform scale and shear.
        double maxRowSum = 0.0;
        for (uint32_t row = 0; row < 3; ++row) {
            double rowSum = 0.0;
            for (uint32_t col = 0; col < 3; ++col) {
                double gram = 0.0;
                for (uint32_t k = 0; k < 3; ++k) {
                    gram += static_cast<double>(sdf.invTransform[k * 4 + row])
                          * sdf.invTransform[k * 4 + col];
                }
                rowSum += std::abs(gram);
            }
            maxRowSum = std::max(maxRowSum, rowSum);
        }
        if (!std::isfinite(maxRowSum) || maxRowSum <= 0.0) continue;
        entry.distanceScale = static_cast<float>(1.0 / std::sqrt(maxRowSum));
        gpuMeshes.push_back(entry);
    }

    const auto meshCount = static_cast<uint32_t>(gpuMeshes.size());
    uint64_t meshHash = 14695981039346656037ull;
    for (const auto& entry : gpuMeshes) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&entry);
        for (size_t b = 0; b < sizeof(entry); ++b) {
            meshHash = (meshHash ^ bytes[b]) * 1099511628211ull;
        }
    }
    bool needsComposite = false;
    for (auto& clip : m_clipmaps) {
        clip.dirty |= meshHash != m_meshDataHash || meshCount != m_meshDataCount;
        needsComposite |= clip.dirty;
    }
    if (!needsComposite) return;

    const uint32_t batchCount = std::max(1u, (meshCount + kGdfMeshesPerBatch - 1) / kGdfMeshesPerBatch);
    std::vector<CBAllocation> batches;
    batches.reserve(batchCount);
    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        auto allocation = cbRing->Allocate(kGdfMeshesPerBatch * sizeof(GdfMeshSdfGPU));
        if (!allocation.cpuPtr) {
            UZ_LOG(Renderer, Error, "GlobalDistanceField: failed to allocate mesh SDF batch {}", batch);
            return;
        }
        std::memset(allocation.cpuPtr, 0, kGdfMeshesPerBatch * sizeof(GdfMeshSdfGPU));
        const uint32_t offset = batch * kGdfMeshesPerBatch;
        const uint32_t count = std::min(kGdfMeshesPerBatch, meshCount - offset);
        if (count > 0) {
            std::memcpy(allocation.cpuPtr, gpuMeshes.data() + offset, count * sizeof(GdfMeshSdfGPU));
        }
        batches.push_back(allocation);
    }

    cmdList->SetPipeline(compositePipeline);
    for (auto& clip : m_clipmaps) {
        if (!clip.dirty) continue;
        RHIBarrierDesc toUav{};
        toUav.texture     = clip.texture;
        toUav.stateBefore = clip.hasData ? RHIResourceState::ShaderResource : RHIResourceState::Common;
        toUav.stateAfter  = RHIResourceState::UnorderedAccess;
        RHIBarrierDesc initialBarriers[2] = {toUav, toUav};
        initialBarriers[1].texture = clip.rawTexture;
        cmdList->ResourceBarrier(initialBarriers, 2);
        GdfCompositeConstants constants{};
        constants.outputUav      = clip.uavIndex;
        constants.rawOutputUav   = clip.rawUavIndex;
        constants.resolution     = m_device->GetTextureDesc(clip.texture).width;
        constants.clipmapCenterX = clip.center.x;
        constants.clipmapCenterY = clip.center.y;
        constants.clipmapCenterZ = clip.center.z;
        constants.clipmapExtent  = clip.extent;

        for (uint32_t batch = 0; batch < batchCount; ++batch) {
            const auto& allocation = batches[batch];
            constants.meshCount = std::min(kGdfMeshesPerBatch, meshCount - batch * kGdfMeshesPerBatch);
            constants.accumulate = batch > 0;
            cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&constants), 9);
            cmdList->SetRootCBV(2, allocation.bufferHandle, allocation.offsetBytes,
                               allocation.cpuPtr, kGdfMeshesPerBatch * sizeof(GdfMeshSdfGPU));
            const uint32_t groupsPerAxis = (constants.resolution + 3) / 4;
            cmdList->Dispatch(groupsPerAxis, groupsPerAxis, groupsPerAxis);
            if (batch + 1 < batchCount) {
                RHIBarrierDesc uavBarrier{};
                uavBarrier.texture = clip.texture;
                uavBarrier.stateBefore = RHIResourceState::UnorderedAccess;
                uavBarrier.stateAfter = RHIResourceState::UnorderedAccess;
                RHIBarrierDesc batchBarriers[2] = {uavBarrier, uavBarrier};
                batchBarriers[1].texture = clip.rawTexture;
                cmdList->ResourceBarrier(batchBarriers, 2);
            }
        }

        RHIBarrierDesc toSrv{};
        toSrv.texture     = clip.texture;
        toSrv.stateBefore = RHIResourceState::UnorderedAccess;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        RHIBarrierDesc finalBarriers[2] = {toSrv, toSrv};
        finalBarriers[1].texture = clip.rawTexture;
        cmdList->ResourceBarrier(finalBarriers, 2);

        clip.dirty = false;
        clip.hasData = true;
    }
    if (m_meshDataHash != meshHash || m_meshDataCount != meshCount) ++m_sceneRevision;
    m_meshDataHash = meshHash;
    m_meshDataCount = meshCount;
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
    if (!m_initialized || cascadeIndex >= kNumClipmaps) return 0.0f;
    const uint32_t resolution = m_device->GetTextureDesc(m_clipmaps[cascadeIndex].texture).width;
    return (m_clipmaps[cascadeIndex].extent * 2.0f) / static_cast<float>(resolution);
}

} // namespace uz
