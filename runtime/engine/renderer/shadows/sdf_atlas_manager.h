/// UzEngine SDF Atlas Manager — Global GPU 3D Texture Atlas for Mesh SDFs
///
/// Manages allocation and lifecycle of SDF brick data in a single global
/// Texture3D (R8_UNORM). Each registered mesh occupies a contiguous range of
/// brick slots in the atlas. The atlas is shared across all active mesh SDFs
/// in the scene.
///
/// # Architecture
/// ```
/// MeshSdfAsset (CPU)
///   -> SdfAtlasManager::RegisterMesh()
///     -> Linear allocate brick slots in atlas
///     -> Upload brick voxel data to Texture3D (deferred until upload infra exists)
///     -> Return atlas offset for SdfObjectGpuData::atlasOffset
/// ```
///
/// # Memory
/// The atlas is a cube Texture3D of `atlasSize^3` R8_UNORM texels. At 256^3 that
/// is 16 MB. Each SDF brick occupies 8^3 = 512 texels = 512 bytes in the atlas.
/// A 256^3 atlas holds `(256/8)^3 = 32768` bricks.
///
/// # Allocation Strategy
/// Currently uses a simple linear (bump) allocator. Freed bricks are tracked but
/// not compacted — the atlas may fragment over time. A free-list allocator will
/// replace this when dynamic mesh loading/unloading becomes frequent.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.
///
/// # UE5 Reference
/// - DistanceFieldAtlas.h — global atlas management
/// - DistanceFieldLightingShared.h:78-94 — atlas upload and defragmentation

#pragma once

#include "sdf_types.h"
#include "rhi_types.h"

#include <cstdint>
#include <unordered_map>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// SdfAtlasManager
// ─────────────────────────────────────────────────────────────────────────────

/// Manages the global GPU 3D texture atlas for mesh SDF bricks.
///
/// Call `Init()` once during renderer startup, `RegisterMesh()`/`UnregisterMesh()`
/// as meshes are loaded/unloaded, and `Shutdown()` during renderer teardown.
/// The atlas SRV index (`GetAtlasSrvIndex()`) is bound to shaders that perform
/// SDF cone tracing.
class SdfAtlasManager {
public:
    SdfAtlasManager() = default;
    ~SdfAtlasManager() = default;

    // Non-copyable, non-movable (owns GPU resource via handle).
    SdfAtlasManager(const SdfAtlasManager&)            = delete;
    SdfAtlasManager& operator=(const SdfAtlasManager&) = delete;
    SdfAtlasManager(SdfAtlasManager&&)                 = delete;
    SdfAtlasManager& operator=(SdfAtlasManager&&)      = delete;

    /// Initializes the atlas 3D texture on the GPU.
    ///
    /// Creates an R8_UNORM Texture3D of `initialSize^3` texels. The atlas holds
    /// `(initialSize / SdfBrick::kSize)^3` bricks.
    ///
    /// # Parameters
    /// - device: RHI device for texture creation. Must not be null.
    /// - initialSize: Atlas dimension per axis in texels (default 256 = 16 MB).
    ///   Must be a multiple of `SdfBrick::kSize` (8).
    ///
    /// # Returns
    /// True if the atlas was created successfully.
    bool Init(IRHIDevice* device, uint32_t initialSize = 256);

    /// Shuts down and releases the atlas GPU texture.
    ///
    /// All mesh registrations become invalid after this call. Safe to call
    /// multiple times or on an uninitialized manager (no-op).
    void Shutdown();

    /// Registers a mesh SDF asset and reserves brick slots in the atlas.
    ///
    /// The actual voxel data upload to the GPU texture is deferred until the
    /// upload infrastructure (staging buffer + copy command list) is connected.
    /// For now, this reserves the slots and logs the allocation.
    ///
    /// # Parameters
    /// - meshId: Unique mesh identifier (e.g., asset hash or registry index).
    /// - asset: SDF data to register. Must pass `MeshSdfAsset::IsValid()`.
    ///
    /// # Returns
    /// Atlas offset (first brick index) for use in `SdfObjectGpuData::atlasOffset`.
    /// Returns `UINT32_MAX` if the atlas is full or the asset is invalid.
    [[nodiscard]] uint32_t RegisterMesh(uint32_t meshId, const MeshSdfAsset& asset);

    /// Unregisters a mesh and marks its atlas brick slots as freed.
    ///
    /// The freed slots are not immediately reused (linear allocator). They will
    /// be reclaimed when the atlas is compacted or re-created.
    ///
    /// # Parameters
    /// - meshId: Mesh identifier previously passed to `RegisterMesh()`.
    ///   No-op if the mesh was not registered.
    void UnregisterMesh(uint32_t meshId);

    /// Returns the atlas 3D texture SRV descriptor index for shader binding.
    ///
    /// # Returns
    /// SRV index into the global CBV_SRV_UAV descriptor heap, or `UINT32_MAX`
    /// if the atlas is not initialized.
    [[nodiscard]] uint32_t GetAtlasSrvIndex() const noexcept { return m_atlasSrvIndex; }

    /// Returns true if the atlas has been initialized and the GPU texture is valid.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Returns total VRAM used by the atlas texture in bytes.
    ///
    /// This is the full atlas allocation (`atlasSize^3 * 1 byte`), not the
    /// amount actually filled with brick data.
    [[nodiscard]] uint64_t GetMemoryUsage() const noexcept;

    /// Returns the number of brick slots currently allocated (across all meshes).
    [[nodiscard]] uint32_t GetAllocatedBrickCount() const noexcept { return m_nextFreeBrick; }

    /// Returns the total capacity of the atlas in brick slots.
    [[nodiscard]] uint32_t GetTotalBrickCapacity() const noexcept { return m_totalBricks; }

private:
    IRHIDevice*      m_device         = nullptr;
    RHITextureHandle m_atlasTexture;
    uint32_t         m_atlasSrvIndex  = UINT32_MAX;
    uint32_t         m_atlasSize      = 0;      ///< Atlas dimension per axis in texels.
    bool             m_initialized    = false;

    /// Tracks allocated brick regions per registered mesh.
    struct MeshAllocation {
        uint32_t offset;      ///< First brick index in atlas.
        uint32_t brickCount;  ///< Number of bricks allocated.
    };
    std::unordered_map<uint32_t, MeshAllocation> m_meshAllocations;

    /// Linear (bump) allocator state for brick slots.
    /// Freed bricks are not reclaimed until atlas compaction.
    uint32_t m_nextFreeBrick = 0;
    uint32_t m_totalBricks   = 0;
};

} // namespace uz
