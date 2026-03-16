/// UzEngine SDF Atlas Manager — Implementation.

#include "sdf_atlas_manager.h"

#include "rhi.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Shadow);

namespace uz {

bool SdfAtlasManager::Init(IRHIDevice* device, uint32_t initialSize) {
    if (!device) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::Init: null device");
        return false;
    }

    // Atlas size must be a multiple of brick size (8) for clean tiling.
    if (initialSize == 0 || (initialSize % SdfBrick::kSize) != 0) {
        UZ_LOG(Shadow, Error,
               "SdfAtlasManager::Init: initialSize={} must be a positive multiple of {} (brick size)",
               initialSize, SdfBrick::kSize);
        return false;
    }

    m_device    = device;
    m_atlasSize = initialSize;

    // Compute brick capacity: (atlasSize / 8)^3
    const uint32_t bricksPerAxis = initialSize / SdfBrick::kSize;
    m_totalBricks = bricksPerAxis * bricksPerAxis * bricksPerAxis;

    // Create the 3D texture (R8_UNORM, one byte per voxel).
    RHITextureDesc desc{};
    desc.width            = initialSize;
    desc.height           = initialSize;
    desc.depthOrArraySize = static_cast<uint16_t>(initialSize);
    desc.mipLevels        = 1;
    desc.format           = RHIFormat::R8Unorm;
    desc.usage            = RHITextureUsage::ShaderResource;
    desc.debugName        = "SdfAtlas3D";

    m_atlasTexture = device->CreateTexture(desc);
    if (!m_atlasTexture.IsValid()) {
        UZ_LOG(Shadow, Error,
               "SdfAtlasManager::Init: failed to create {}x{}x{} R8Unorm 3D texture",
               initialSize, initialSize, initialSize);
        m_device      = nullptr;
        m_atlasSize   = 0;
        m_totalBricks = 0;
        return false;
    }

    m_atlasSrvIndex = device->GetTextureSRVIndex(m_atlasTexture);
    m_initialized   = true;
    m_nextFreeBrick = 0;

    const uint64_t vramBytes = GetMemoryUsage();
    UZ_LOG(Shadow, Info,
           "SdfAtlasManager: created {}x{}x{} R8Unorm atlas (srv={}, capacity={} bricks, {:.1f} MB)",
           initialSize, initialSize, initialSize,
           m_atlasSrvIndex, m_totalBricks,
           static_cast<double>(vramBytes) / (1024.0 * 1024.0));

    return true;
}

void SdfAtlasManager::Shutdown() {
    if (m_device && m_atlasTexture.IsValid()) {
        m_device->DestroyTexture(m_atlasTexture);
        UZ_LOG(Shadow, Info, "SdfAtlasManager: shutdown (freed {} mesh registrations)",
               m_meshAllocations.size());
    }

    m_atlasTexture   = RHITextureHandle{};
    m_atlasSrvIndex  = UINT32_MAX;
    m_atlasSize      = 0;
    m_totalBricks    = 0;
    m_nextFreeBrick  = 0;
    m_initialized    = false;
    m_device         = nullptr;
    m_meshAllocations.clear();
}

uint32_t SdfAtlasManager::RegisterMesh(uint32_t meshId, const MeshSdfAsset& asset) {
    if (!m_initialized) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::RegisterMesh: atlas not initialized");
        return UINT32_MAX;
    }

    if (!asset.IsValid()) {
        UZ_LOG(Shadow, Error, "SdfAtlasManager::RegisterMesh: invalid SDF asset for meshId={}",
               meshId);
        return UINT32_MAX;
    }

    // Reject duplicate registration.
    if (m_meshAllocations.count(meshId) > 0) {
        UZ_LOG(Shadow, Warning,
               "SdfAtlasManager::RegisterMesh: meshId={} already registered (offset={})",
               meshId, m_meshAllocations[meshId].offset);
        return m_meshAllocations[meshId].offset;
    }

    const auto brickCount = static_cast<uint32_t>(asset.bricks.size());

    // Check if there is enough space in the atlas.
    if (m_nextFreeBrick + brickCount > m_totalBricks) {
        UZ_LOG(Shadow, Error,
               "SdfAtlasManager::RegisterMesh: atlas full — need {} bricks, have {} free (total={})",
               brickCount, m_totalBricks - m_nextFreeBrick, m_totalBricks);
        return UINT32_MAX;
    }

    // Linear allocate: reserve a contiguous range of brick slots.
    const uint32_t offset = m_nextFreeBrick;
    m_nextFreeBrick += brickCount;

    m_meshAllocations[meshId] = MeshAllocation{offset, brickCount};

    // Actual GPU upload of brick voxel data will happen when the staging/upload
    // infrastructure is connected. The atlas offset is valid — the cone tracing
    // shader will read uninitialized (zero) voxels until upload is implemented,
    // which produces no shadow (all distances read as deep-inside = no surface hit).

    UZ_LOG(Shadow, Info,
           "SdfAtlasManager::RegisterMesh: meshId={} allocated {} bricks at offset={} ({}/{} used)",
           meshId, brickCount, offset, m_nextFreeBrick, m_totalBricks);

    return offset;
}

void SdfAtlasManager::UnregisterMesh(uint32_t meshId) {
    const auto it = m_meshAllocations.find(meshId);
    if (it == m_meshAllocations.end()) {
        UZ_LOG(Shadow, Warning,
               "SdfAtlasManager::UnregisterMesh: meshId={} not registered", meshId);
        return;
    }

    // Mark the allocation as freed. With the linear allocator, these slots are
    // not immediately reusable — they become dead space until the atlas is
    // compacted or re-created. This is acceptable for the current use case
    // (static meshes rarely unloaded).
    const auto brickCount = it->second.brickCount;
    const auto offset     = it->second.offset;
    m_meshAllocations.erase(it);

    UZ_LOG(Shadow, Info,
           "SdfAtlasManager::UnregisterMesh: meshId={} freed {} bricks at offset={} "
           "({} meshes remaining, {}/{} slots used)",
           meshId, brickCount, offset,
           m_meshAllocations.size(), m_nextFreeBrick, m_totalBricks);
}

uint64_t SdfAtlasManager::GetMemoryUsage() const noexcept {
    // R8_UNORM = 1 byte per texel. Atlas is atlasSize^3 texels.
    return static_cast<uint64_t>(m_atlasSize)
         * static_cast<uint64_t>(m_atlasSize)
         * static_cast<uint64_t>(m_atlasSize);
}

} // namespace uz
