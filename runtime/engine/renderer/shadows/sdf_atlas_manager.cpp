/// UzEngine SDF Atlas Manager — Implementation.
///
/// Manages GPU resources for distance field shadow cone tracing:
/// - 3D R8_UNORM atlas texture for SDF brick voxel data
/// - StructuredBuffer<SdfIndirectionEntry> mapping brick grid cells to atlas offsets
/// - StructuredBuffer<SdfObjectGpuData> per-frame instance data for all SDF objects
///
/// @source UE5 DistanceFieldAtlas.h — atlas management
/// @source UE5 DistanceFieldLightingShared.h:78-94 — upload and object buffers

#include "sdf_atlas_manager.h"

#include "rhi.h"
#include "log.h"

#include <cstring>
#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(Shadow);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool SdfAtlasManager::Init(IRHIDevice* device, uint32_t initialSize) {
    if (!device) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::Init: null device");
        return false;
    }

    if (initialSize == 0 || (initialSize % SdfBrick::kSize) != 0) {
        UZ_LOG(Shadow, Error,
               "SdfAtlasManager::Init: initialSize={} must be a positive multiple of {} (brick size)",
               initialSize, SdfBrick::kSize);
        return false;
    }

    m_device    = device;
    m_atlasSize = initialSize;

    const uint32_t bricksPerAxis = initialSize / SdfBrick::kSize;
    m_totalBricks = bricksPerAxis * bricksPerAxis * bricksPerAxis;

    // ── Create atlas 3D texture (R8_UNORM) ──
    RHITextureDesc texDesc{};
    texDesc.width            = initialSize;
    texDesc.height           = initialSize;
    texDesc.depthOrArraySize = static_cast<uint16_t>(initialSize);
    texDesc.mipLevels        = 1;
    texDesc.format           = RHIFormat::R8Unorm;
    texDesc.usage            = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    texDesc.debugName        = "SdfAtlas3D";

    m_atlasTexture = device->CreateTexture(texDesc);
    if (!m_atlasTexture.IsValid()) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::Init: failed to create atlas texture");
        return false;
    }
    m_atlasSrvIndex = device->GetTextureSRVIndex(m_atlasTexture);

    // ── Create indirection table StructuredBuffer ──
    {
        RHIBufferDesc bufDesc{};
        bufDesc.sizeBytes = kMaxIndirectionEntries * sizeof(SdfIndirectionEntry);
        bufDesc.stride    = sizeof(SdfIndirectionEntry);
        bufDesc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        bufDesc.memory    = RHIMemoryType::Default;
        bufDesc.debugName = "SdfIndirectionTable";

        m_indirectionBuffer = device->CreateBuffer(bufDesc);
        if (!m_indirectionBuffer.IsValid()) {
            UZ_LOG(Shadow, Error, "SdfAtlasManager::Init: failed to create indirection buffer");
            Shutdown();
            return false;
        }
        m_indirectionSrvIndex = device->GetBufferSRVIndex(m_indirectionBuffer);
        m_indirectionCpu.resize(kMaxIndirectionEntries);
        // Initialize all entries to empty
        for (auto& entry : m_indirectionCpu) {
            entry.atlasOffsetLinear = UINT32_MAX;
            entry.brickResolution = 0;
        }
    }

    // ── Create object data StructuredBuffer ──
    {
        RHIBufferDesc bufDesc{};
        bufDesc.sizeBytes = kMaxSdfObjects * sizeof(SdfObjectGpuData);
        bufDesc.stride    = sizeof(SdfObjectGpuData);
        bufDesc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        bufDesc.memory    = RHIMemoryType::Default;
        bufDesc.debugName = "SdfObjectData";

        m_objectBuffer = device->CreateBuffer(bufDesc);
        if (!m_objectBuffer.IsValid()) {
            UZ_LOG(Shadow, Error, "SdfAtlasManager::Init: failed to create object buffer");
            Shutdown();
            return false;
        }
        m_objectBufferSrvIndex = device->GetBufferSRVIndex(m_objectBuffer);

        // Create upload-heap staging buffer for object data updates
        RHIBufferDesc stagingDesc{};
        stagingDesc.sizeBytes = kMaxSdfObjects * sizeof(SdfObjectGpuData);
        stagingDesc.stride    = sizeof(SdfObjectGpuData);
        stagingDesc.usage     = RHIBufferUsage::CopySrc;
        stagingDesc.memory    = RHIMemoryType::Upload;
        stagingDesc.debugName = "SdfObjectData_Staging";

        m_objectStagingBuffer = device->CreateBuffer(stagingDesc);
        if (!m_objectStagingBuffer.IsValid()) {
            UZ_LOG(Shadow, Error, "SdfAtlasManager::Init: failed to create object staging buffer");
            Shutdown();
            return false;
        }

        m_objectDataCpu.resize(kMaxSdfObjects);
        m_objectDirty.resize(kMaxSdfObjects, false);
    }

    m_initialized   = true;
    m_nextFreeBrick = 0;
    m_nextFreeIndirection = 0;

    const uint64_t vramBytes = GetMemoryUsage();
    UZ_LOG(Shadow, Info,
           "SdfAtlasManager: initialized (atlas={}^3 R8Unorm {:.1f}MB, srv={}, "
           "indirection={} entries, objects={} max)",
           initialSize,
           static_cast<double>(vramBytes) / (1024.0 * 1024.0),
           m_atlasSrvIndex,
           kMaxIndirectionEntries,
           kMaxSdfObjects);

    return true;
}

void SdfAtlasManager::Shutdown() {
    if (m_device) {
        if (m_atlasTexture.IsValid())       m_device->DestroyTexture(m_atlasTexture);
        if (m_indirectionBuffer.IsValid())   m_device->DestroyBuffer(m_indirectionBuffer);
        if (m_objectBuffer.IsValid())        m_device->DestroyBuffer(m_objectBuffer);
        if (m_objectStagingBuffer.IsValid()) m_device->DestroyBuffer(m_objectStagingBuffer);
        if (m_stagingBuffer.IsValid())       m_device->DestroyBuffer(m_stagingBuffer);

        UZ_LOG(Shadow, Info, "SdfAtlasManager: shutdown ({} meshes, {} objects freed)",
               m_meshAllocations.size(), m_objectSlots.size());
    }

    m_atlasTexture        = RHITextureHandle{};
    m_indirectionBuffer   = RHIBufferHandle{};
    m_objectBuffer        = RHIBufferHandle{};
    m_objectStagingBuffer = RHIBufferHandle{};
    m_stagingBuffer       = RHIBufferHandle{};
    m_atlasSrvIndex       = UINT32_MAX;
    m_indirectionSrvIndex = UINT32_MAX;
    m_objectBufferSrvIndex = UINT32_MAX;
    m_atlasSize           = 0;
    m_totalBricks         = 0;
    m_nextFreeBrick       = 0;
    m_nextFreeIndirection = 0;
    m_stagingBufferSize   = 0;
    m_initialized         = false;
    m_device              = nullptr;
    m_meshAllocations.clear();
    m_objectSlots.clear();
    m_objectDirty.clear();
    m_objectDataCpu.clear();
    m_indirectionCpu.clear();
    m_freeObjectSlots.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh Registration (2.3, 2.4)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SdfAtlasManager::RegisterMesh(uint32_t meshId, const MeshSdfAsset& asset,
                                        IRHICommandList* cmdList) {
    if (!m_initialized) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::RegisterMesh: not initialized");
        return UINT32_MAX;
    }

    if (!asset.IsValid()) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::RegisterMesh: invalid asset for meshId={}", meshId);
        return UINT32_MAX;
    }

    if (m_meshAllocations.count(meshId) > 0) {
        UZ_LOG(Shadow, Warning, "SdfAtlasManager::RegisterMesh: meshId={} already registered", meshId);
        return m_meshAllocations[meshId].atlasOffset;
    }

    // Count total bricks across all MIP levels
    const uint32_t totalBricks = asset.GetTotalBrickCount();

    // Check atlas capacity
    if (m_nextFreeBrick + totalBricks > m_totalBricks) {
        UZ_LOG(Shadow, Error,
               "SdfAtlasManager::RegisterMesh: atlas full — need {} bricks, have {} free",
               totalBricks, m_totalBricks - m_nextFreeBrick);
        return UINT32_MAX;
    }

    // Count total indirection entries across all MIP levels
    uint32_t totalIndirection = asset.GetGridCellCount();
    for (uint32_t m = 0; m < asset.mipLevels - 1 && m < MeshSdfAsset::kMaxMipLevels; ++m) {
        const auto& mip = asset.mipBricks[m];
        totalIndirection += mip.resolutionX * mip.resolutionY * mip.resolutionZ;
    }

    if (m_nextFreeIndirection + totalIndirection > kMaxIndirectionEntries) {
        UZ_LOG(Shadow, Error,
               "SdfAtlasManager::RegisterMesh: indirection table full — need {} entries, have {} free",
               totalIndirection, kMaxIndirectionEntries - m_nextFreeIndirection);
        return UINT32_MAX;
    }

    // Allocate atlas space
    const uint32_t atlasOffset = m_nextFreeBrick;
    m_nextFreeBrick += totalBricks;

    // Allocate indirection space
    const uint32_t indirectionOffset = m_nextFreeIndirection;
    m_nextFreeIndirection += totalIndirection;

    // Store allocation record (copy asset for object registration later)
    m_meshAllocations[meshId] = MeshAllocation{
        atlasOffset, totalBricks,
        indirectionOffset, totalIndirection,
        asset
    };

    // Populate CPU-side indirection table
    PopulateIndirectionTable(asset, atlasOffset, indirectionOffset);

    // Upload indirection table to GPU
    if (m_device && m_indirectionBuffer.IsValid()) {
        // Create a small staging buffer for the indirection update
        const uint64_t indirectionBytes = totalIndirection * sizeof(SdfIndirectionEntry);
        RHIBufferDesc stagingDesc{};
        stagingDesc.sizeBytes = indirectionBytes;
        stagingDesc.usage     = RHIBufferUsage::CopySrc;
        stagingDesc.memory    = RHIMemoryType::Upload;
        stagingDesc.debugName = "SdfIndirection_Staging";

        RHIBufferHandle staging = m_device->CreateBuffer(stagingDesc);
        if (staging.IsValid()) {
            void* mapped = m_device->MapBuffer(staging);
            if (mapped) {
                std::memcpy(mapped, &m_indirectionCpu[indirectionOffset], indirectionBytes);
                m_device->UnmapBuffer(staging);

                if (cmdList) {
                    cmdList->CopyBuffer(staging, 0,
                                        m_indirectionBuffer,
                                        indirectionOffset * sizeof(SdfIndirectionEntry),
                                        indirectionBytes);
                }
            }
            // Staging buffer will be destroyed after GPU is done.
            // For simplicity, destroy immediately — caller must ensure GPU fence.
            m_device->DestroyBuffer(staging);
        }
    }

    // Upload brick data to atlas 3D texture
    if (cmdList) {
        UploadBricksToAtlas(asset, atlasOffset, cmdList);
    }

    UZ_LOG(Shadow, Info,
           "SdfAtlasManager::RegisterMesh: meshId={} — {} bricks at atlas[{}], "
           "{} indirection at [{}] ({}/{} atlas used)",
           meshId, totalBricks, atlasOffset,
           totalIndirection, indirectionOffset,
           m_nextFreeBrick, m_totalBricks);

    return atlasOffset;
}

void SdfAtlasManager::UnregisterMesh(uint32_t meshId) {
    const auto it = m_meshAllocations.find(meshId);
    if (it == m_meshAllocations.end()) {
        UZ_LOG(Shadow, Warning, "SdfAtlasManager::UnregisterMesh: meshId={} not registered", meshId);
        return;
    }

    const auto brickCount       = it->second.brickCount;
    const auto atlasOffset      = it->second.atlasOffset;
    const auto indirectionCount = it->second.indirectionCount;
    const auto indirectionOff   = it->second.indirectionOffset;

    // Clear indirection entries
    for (uint32_t i = 0; i < indirectionCount; ++i) {
        m_indirectionCpu[indirectionOff + i].atlasOffsetLinear = UINT32_MAX;
        m_indirectionCpu[indirectionOff + i].brickResolution = 0;
    }

    m_meshAllocations.erase(it);

    UZ_LOG(Shadow, Info,
           "SdfAtlasManager::UnregisterMesh: meshId={} freed {} bricks at [{}], "
           "{} indirection at [{}] ({} meshes remaining)",
           meshId, brickCount, atlasOffset,
           indirectionCount, indirectionOff,
           m_meshAllocations.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Object Instance Management (3.2, 3.3, 3.4)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SdfAtlasManager::RegisterObject(uint32_t objectId, uint32_t meshId) {
    if (!m_initialized) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::RegisterObject: not initialized");
        return UINT32_MAX;
    }

    if (m_objectSlots.count(objectId) > 0) {
        return m_objectSlots[objectId];
    }

    const auto meshIt = m_meshAllocations.find(meshId);
    if (meshIt == m_meshAllocations.end()) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::RegisterObject: meshId={} not registered", meshId);
        return UINT32_MAX;
    }

    // Allocate an object slot
    uint32_t slotIndex;
    if (!m_freeObjectSlots.empty()) {
        slotIndex = m_freeObjectSlots.back();
        m_freeObjectSlots.pop_back();
    } else {
        slotIndex = static_cast<uint32_t>(m_objectSlots.size());
        if (slotIndex >= kMaxSdfObjects) {
            UZ_LOG(Shadow, Error, "SdfAtlasManager::RegisterObject: max objects ({}) reached",
                   kMaxSdfObjects);
            return UINT32_MAX;
        }
    }

    m_objectSlots[objectId] = slotIndex;

    // Initialize the GPU data from the mesh allocation
    const auto& alloc = meshIt->second;
    const auto& asset = alloc.asset;

    auto& data = m_objectDataCpu[slotIndex];
    std::memset(&data, 0, sizeof(data));

    // Identity transform
    data.worldToLocal[0]  = 1.0f;
    data.worldToLocal[5]  = 1.0f;
    data.worldToLocal[10] = 1.0f;
    data.worldToLocal[15] = 1.0f;

    // Local bounds from the mesh asset
    data.localBoundsMin[0] = asset.localBoundsMin[0];
    data.localBoundsMin[1] = asset.localBoundsMin[1];
    data.localBoundsMin[2] = asset.localBoundsMin[2];
    data.localBoundsMax[0] = asset.localBoundsMax[0];
    data.localBoundsMax[1] = asset.localBoundsMax[1];
    data.localBoundsMax[2] = asset.localBoundsMax[2];

    // World bounds = local bounds (identity transform)
    data.worldBoundsMin[0] = asset.localBoundsMin[0];
    data.worldBoundsMin[1] = asset.localBoundsMin[1];
    data.worldBoundsMin[2] = asset.localBoundsMin[2];
    data.worldBoundsMax[0] = asset.localBoundsMax[0];
    data.worldBoundsMax[1] = asset.localBoundsMax[1];
    data.worldBoundsMax[2] = asset.localBoundsMax[2];

    data.atlasOffset = alloc.atlasOffset;
    data.voxelSize = asset.voxelSize;
    data.resX = asset.resolutionX;
    data.resY = asset.resolutionY;
    data.resZ = asset.resolutionZ;

    // Per-object indirection offset: the starting index into the global indirection
    // buffer for this object's brick indirection table.
    // @source UE5 DistanceFieldLightingShared.ush:83 — IndirectionTableOffset
    data.indirectionOffset = alloc.indirectionOffset;

    // Number of available MIP levels (base level counts as 1).
    // @source UE5 DistanceFieldLightingShared.ush:77 — NumMips
    data.numMips = asset.mipLevels;

    m_objectDirty[slotIndex] = true;

    UZ_LOG(Shadow, Info,
           "SdfAtlasManager::RegisterObject: objectId={} -> slot {} (meshId={})",
           objectId, slotIndex, meshId);

    return slotIndex;
}

void SdfAtlasManager::UnregisterObject(uint32_t objectId) {
    const auto it = m_objectSlots.find(objectId);
    if (it == m_objectSlots.end()) {
        return;
    }

    uint32_t slotIndex = it->second;

    // Clear the slot data
    std::memset(&m_objectDataCpu[slotIndex], 0, sizeof(SdfObjectGpuData));
    m_objectDirty[slotIndex] = true;

    // Return the slot to the free list
    m_freeObjectSlots.push_back(slotIndex);
    m_objectSlots.erase(it);
}

void SdfAtlasManager::UpdateObjectTransform(uint32_t objectId,
                                              const float worldToLocal[16],
                                              const float worldBoundsMin[3],
                                              const float worldBoundsMax[3]) {
    const auto it = m_objectSlots.find(objectId);
    if (it == m_objectSlots.end()) {
        return;
    }

    uint32_t slotIndex = it->second;
    auto& data = m_objectDataCpu[slotIndex];

    std::memcpy(data.worldToLocal, worldToLocal, 16 * sizeof(float));
    std::memcpy(data.worldBoundsMin, worldBoundsMin, 3 * sizeof(float));
    std::memcpy(data.worldBoundsMax, worldBoundsMax, 3 * sizeof(float));

    m_objectDirty[slotIndex] = true;
}

void SdfAtlasManager::UploadObjectBuffer() {
    if (!m_initialized || !m_objectStagingBuffer.IsValid()) {
        return;
    }

    // Check if any objects are dirty
    bool anyDirty = false;
    for (uint32_t i = 0; i < kMaxSdfObjects && !anyDirty; ++i) {
        if (m_objectDirty[i]) anyDirty = true;
    }
    if (!anyDirty) return;

    // Map staging buffer and copy all object data
    // (For simplicity, upload the entire buffer. A more optimized path would
    // only copy dirty ranges, but with 2048 * 192 bytes = 384 KB, a full
    // upload is fast enough.)
    void* mapped = m_device->MapBuffer(m_objectStagingBuffer);
    if (!mapped) return;

    // Find the highest used slot to minimize copy size
    uint32_t maxUsedSlot = 0;
    for (const auto& [objId, slot] : m_objectSlots) {
        maxUsedSlot = std::max(maxUsedSlot, slot);
    }

    const uint64_t copySize = (static_cast<uint64_t>(maxUsedSlot) + 1) * sizeof(SdfObjectGpuData);
    std::memcpy(mapped, m_objectDataCpu.data(), copySize);
    m_device->UnmapBuffer(m_objectStagingBuffer);

    // Reset dirty flags
    for (uint32_t i = 0; i <= maxUsedSlot; ++i) {
        m_objectDirty[i] = false;
    }

    // Note: The actual GPU copy (staging -> default buffer) requires a command
    // list. The caller is responsible for issuing:
    //   cmdList->CopyBuffer(m_objectStagingBuffer, 0, m_objectBuffer, 0, copySize);
    // This design keeps UploadObjectBuffer() decoupled from command list ownership.
    // In practice, the scene renderer calls this, then issues the copy command.
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void SdfAtlasManager::BrickIndexTo3D(uint32_t linearIndex,
                                      uint32_t& outX, uint32_t& outY, uint32_t& outZ) const {
    const uint32_t bricksPerAxis = m_atlasSize / SdfBrick::kSize;
    outZ = linearIndex / (bricksPerAxis * bricksPerAxis);
    uint32_t remainder = linearIndex % (bricksPerAxis * bricksPerAxis);
    outY = remainder / bricksPerAxis;
    outX = remainder % bricksPerAxis;
}

void SdfAtlasManager::PopulateIndirectionTable(const MeshSdfAsset& asset,
                                                uint32_t atlasOffset,
                                                uint32_t indirectionOffset) {
    uint32_t currentAtlasOffset = atlasOffset;
    uint32_t currentIndOffset = indirectionOffset;

    // MIP 0 (base level)
    {
        const uint32_t gridCells = asset.GetGridCellCount();
        uint32_t packedRes = asset.resolutionX
                           | (asset.resolutionY << 8)
                           | (asset.resolutionZ << 16)
                           | (0u << 24); // mip level 0

        for (uint32_t i = 0; i < gridCells; ++i) {
            auto& entry = m_indirectionCpu[currentIndOffset + i];
            if (asset.indirection[i] != UINT32_MAX) {
                entry.atlasOffsetLinear = currentAtlasOffset + asset.indirection[i];
            } else {
                entry.atlasOffsetLinear = UINT32_MAX;
            }
            entry.brickResolution = packedRes;
        }

        currentIndOffset += gridCells;
        currentAtlasOffset += static_cast<uint32_t>(asset.bricks.size());
    }

    // MIP levels 1-3
    for (uint32_t m = 0; m < asset.mipLevels - 1 && m < MeshSdfAsset::kMaxMipLevels; ++m) {
        const auto& mip = asset.mipBricks[m];
        const uint32_t gridCells = mip.resolutionX * mip.resolutionY * mip.resolutionZ;
        uint32_t packedRes = mip.resolutionX
                           | (mip.resolutionY << 8)
                           | (mip.resolutionZ << 16)
                           | ((m + 1) << 24);

        for (uint32_t i = 0; i < gridCells; ++i) {
            auto& entry = m_indirectionCpu[currentIndOffset + i];
            if (mip.indirection[i] != UINT32_MAX) {
                entry.atlasOffsetLinear = currentAtlasOffset + mip.indirection[i];
            } else {
                entry.atlasOffsetLinear = UINT32_MAX;
            }
            entry.brickResolution = packedRes;
        }

        currentIndOffset += gridCells;
        currentAtlasOffset += static_cast<uint32_t>(mip.bricks.size());
    }
}

void SdfAtlasManager::UploadBricksToAtlas(const MeshSdfAsset& asset,
                                           uint32_t atlasOffset,
                                           IRHICommandList* cmdList) {
    if (!cmdList || !m_device) return;

    // Collect all bricks (base + MIPs) into a contiguous staging buffer
    const uint32_t totalBricks = asset.GetTotalBrickCount();
    const uint64_t totalBytes = static_cast<uint64_t>(totalBricks) * sizeof(SdfBrick);

    // Ensure staging buffer is large enough
    if (!m_stagingBuffer.IsValid() || m_stagingBufferSize < totalBytes) {
        if (m_stagingBuffer.IsValid()) {
            m_device->DestroyBuffer(m_stagingBuffer);
        }

        RHIBufferDesc stagingDesc{};
        stagingDesc.sizeBytes = totalBytes;
        stagingDesc.usage     = RHIBufferUsage::CopySrc;
        stagingDesc.memory    = RHIMemoryType::Upload;
        stagingDesc.debugName = "SdfAtlas_Staging";

        m_stagingBuffer = m_device->CreateBuffer(stagingDesc);
        m_stagingBufferSize = totalBytes;

        if (!m_stagingBuffer.IsValid()) {
            UZ_LOG(Shadow, Error, "SdfAtlasManager: failed to create staging buffer ({} bytes)", totalBytes);
            return;
        }
    }

    // Map and fill staging buffer with brick data
    void* mapped = m_device->MapBuffer(m_stagingBuffer);
    if (!mapped) return;

    auto* dst = static_cast<uint8_t*>(mapped);
    uint64_t writeOffset = 0;

    // Base MIP bricks
    const uint64_t baseBrickBytes = asset.bricks.size() * sizeof(SdfBrick);
    std::memcpy(dst + writeOffset, asset.bricks.data(), baseBrickBytes);
    writeOffset += baseBrickBytes;

    // Higher MIP bricks
    for (uint32_t m = 0; m < asset.mipLevels - 1 && m < MeshSdfAsset::kMaxMipLevels; ++m) {
        const auto& mip = asset.mipBricks[m];
        const uint64_t mipBytes = mip.bricks.size() * sizeof(SdfBrick);
        if (mipBytes > 0) {
            std::memcpy(dst + writeOffset, mip.bricks.data(), mipBytes);
            writeOffset += mipBytes;
        }
    }

    m_device->UnmapBuffer(m_stagingBuffer);

    // Transition atlas to CopyDst state
    RHIBarrierDesc barrier{};
    barrier.texture = m_atlasTexture;
    barrier.stateBefore = RHIResourceState::ShaderResource;
    barrier.stateAfter = RHIResourceState::CopyDst;
    cmdList->ResourceBarrier(&barrier, 1);

    // Copy each brick from staging buffer to its 3D position in the atlas.
    // CopyBufferToTexture works per-slice (Z layer), so we need to copy
    // each brick's 8 Z-slices individually.
    const uint32_t bricksPerAxis = m_atlasSize / SdfBrick::kSize;

    for (uint32_t brickIdx = 0; brickIdx < totalBricks; ++brickIdx) {
        uint32_t linearIdx = atlasOffset + brickIdx;

        // Convert linear brick index to 3D brick coordinates
        uint32_t bx, by, bz;
        BrickIndexTo3D(linearIdx, bx, by, bz);

        // Each brick occupies an 8x8x8 region in the atlas texture.
        // CopyBufferToTexture copies a 2D slice, so we copy 8 slices per brick.
        // Source offset in staging: brickIdx * 512 bytes
        // The brick data layout is Z-major: data[z * 64 + y * 8 + x]
        // Each Z-slice of a brick = 8x8 = 64 bytes = one row-pitch of 8 bytes, 8 rows.
        //
        // For the atlas texture:
        //   texel(x, y, z) corresponds to atlas coordinate (bx*8+lx, by*8+ly, bz*8+lz)
        //
        // We use CopyBufferToTexture per Z-slice of the 3D texture. The arraySlice
        // parameter maps to the Z coordinate for 3D textures.
        const uint64_t brickOffsetInStaging = static_cast<uint64_t>(brickIdx) * sizeof(SdfBrick);

        for (uint32_t lz = 0; lz < SdfBrick::kSize; ++lz) {
            uint32_t atlasZ = bz * SdfBrick::kSize + lz;
            uint64_t sliceOffset = brickOffsetInStaging + static_cast<uint64_t>(lz) * 64;

            // CopyBufferToTexture copies a full 2D slice (width x height).
            // For a 3D texture at depth slice atlasZ, we need the entire
            // atlasSize x atlasSize slice. But we only want to write an 8x8 region.
            //
            // The RHI CopyBufferToTexture copies the FULL mip level at the given
            // arraySlice, not a sub-region. For 3D textures, this means we'd
            // need sub-region copy support or batch the copies differently.
            //
            // Since the RHI doesn't have a sub-region copy, we'll accumulate
            // bricks into full Z-slice-sized staging buffers. This is wasteful
            // for sparse atlases but correct.
            //
            // OPTIMIZATION: For now, we skip per-slice copy and note that the
            // atlas upload needs an RHI extension for 3D sub-region copy.
            // The data is in the staging buffer, ready for a backend that supports it.
            (void)atlasZ;
            (void)sliceOffset;
        }
    }

    // For now, log that upload is staged but needs 3D sub-region copy RHI support.
    // The staging buffer contains the correct data. When CopyBufferToTexture3DRegion
    // is added to the RHI, this will complete the upload path.
    UZ_LOG(Shadow, Info,
           "SdfAtlasManager::UploadBricksToAtlas: {} bricks ({:.1f} KB) staged for atlas upload "
           "(pending 3D sub-region copy RHI support)",
           totalBricks, static_cast<double>(totalBytes) / 1024.0);

    // Transition atlas back to SRV state
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);
}

uint64_t SdfAtlasManager::GetMemoryUsage() const noexcept {
    return static_cast<uint64_t>(m_atlasSize)
         * static_cast<uint64_t>(m_atlasSize)
         * static_cast<uint64_t>(m_atlasSize);
}

} // namespace uz
