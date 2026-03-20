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
///     -> Upload brick voxel data to Texture3D via staging buffer
///     -> Populate indirection table StructuredBuffer
///     -> Return atlas offset for SdfObjectGpuData::atlasOffset
///
/// Per-frame:
///   -> RegisterObject() / UpdateObjectTransform()
///     -> Populate SdfObjectGpuData StructuredBuffer
///     -> Upload dirty entries to GPU
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
/// # Reference
/// - DistanceFieldAtlas.h — global atlas management
/// - DistanceFieldLightingShared.h:78-94 — atlas upload and defragmentation

#pragma once

#include "sdf_types.h"
#include "rhi_types.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;

/// Maximum number of SDF objects that can be registered simultaneously.
/// Determines the size of the GPU StructuredBuffer for SdfObjectGpuData.
/// MAX_SDF_OBJECTS = 2048 — maximum registered distance field objects
static constexpr uint32_t kMaxSdfObjects = 2048;

/// Maximum indirection table entries across all registered meshes.
/// Each entry is a uint2 (8 bytes): (atlasOffsetLinear, brickResolution).
static constexpr uint32_t kMaxIndirectionEntries = 65536;

// ─────────────────────────────────────────────────────────────────────────────
// SdfIndirectionEntry — GPU layout for indirection table
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a brick grid cell to its location in the 3D atlas texture.
///
/// Each entry corresponds to one cell in a mesh's brick indirection grid.
/// The atlas offset is a linear brick index that the shader converts to a
/// 3D atlas coordinate using the atlas dimensions.
struct SdfIndirectionEntry {
    uint32_t atlasOffsetLinear;  ///< Linear brick index in atlas (UINT32_MAX = empty brick).
    uint32_t brickResolution;   ///< Packed: resX | (resY << 8) | (resZ << 16) | (mipLevel << 24).
};

static_assert(sizeof(SdfIndirectionEntry) == 8, "SdfIndirectionEntry must be 8 bytes (uint2 on GPU)");

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

    /// Initializes the atlas 3D texture, indirection table, and object buffer.
    ///
    /// Creates:
    /// - R8_UNORM Texture3D atlas (`initialSize^3` texels)
    /// - StructuredBuffer<SdfIndirectionEntry> for the indirection table
    /// - StructuredBuffer<SdfObjectGpuData> for per-object instance data
    ///
    /// # Parameters
    /// - device: RHI device for resource creation. Must not be null.
    /// - initialSize: Atlas dimension per axis in texels (default 256 = 16 MB).
    ///   Must be a multiple of `SdfBrick::kSize` (8).
    ///
    /// # Returns
    /// True if all resources were created successfully.
    bool Init(IRHIDevice* device, uint32_t initialSize = 256);

    /// Shuts down and releases all GPU resources.
    void Shutdown();

    /// Registers a mesh SDF asset: allocates atlas bricks, uploads voxel data,
    /// and populates the indirection table.
    ///
    /// # Parameters
    /// - meshId: Unique mesh identifier.
    /// - asset: SDF data to register. Must pass `MeshSdfAsset::IsValid()`.
    /// - cmdList: Command list for staging buffer -> atlas texture copy.
    ///            If null, upload is deferred (atlas offset is reserved but data
    ///            is not uploaded).
    ///
    /// # Returns
    /// Atlas offset (first brick index) for `SdfObjectGpuData::atlasOffset`.
    /// Returns `UINT32_MAX` if the atlas is full or the asset is invalid.
    [[nodiscard]] uint32_t RegisterMesh(uint32_t meshId, const MeshSdfAsset& asset,
                                         IRHICommandList* cmdList = nullptr);

    /// Unregisters a mesh and marks its atlas brick slots as freed.
    void UnregisterMesh(uint32_t meshId);

    // ── Object instance management (items 3.2-3.4) ──────────────────────

    /// Registers an SDF object instance in the scene.
    ///
    /// Creates a slot in the per-frame SdfObjectGpuData buffer. The object
    /// starts with identity transform — call `UpdateObjectTransform()` to set
    /// the actual world-to-local matrix and world AABB.
    ///
    /// # Parameters
    /// - objectId: Unique object instance ID.
    /// - meshId: The mesh ID previously passed to RegisterMesh().
    ///
    /// # Returns
    /// Object index for use in tile culling dispatch, or UINT32_MAX on failure.
    [[nodiscard]] uint32_t RegisterObject(uint32_t objectId, uint32_t meshId);

    /// Unregisters an SDF object instance.
    void UnregisterObject(uint32_t objectId);

    /// Updates the world transform and bounds for an SDF object.
    ///
    /// Marks the object as dirty — the GPU buffer will be updated on the
    /// next call to `UploadObjectBuffer()`.
    ///
    /// # Parameters
    /// - objectId: Object instance ID previously passed to RegisterObject().
    /// - worldToLocal: 4x4 row-major world-to-local transform matrix.
    /// - worldBoundsMin: World-space AABB minimum (precomputed by caller).
    /// - worldBoundsMax: World-space AABB maximum (precomputed by caller).
    void UpdateObjectTransform(uint32_t objectId,
                                const float worldToLocal[16],
                                const float worldBoundsMin[3],
                                const float worldBoundsMax[3]);

    /// Uploads dirty object data to the GPU StructuredBuffer.
    ///
    /// Call once per frame before the SDF culling/tracing dispatches.
    /// Only re-uploads entries whose transform changed since last upload.
    void UploadObjectBuffer();

    // ── Accessors ────────────────────────────────────────────────────────

    /// Returns the atlas 3D texture SRV descriptor index.
    [[nodiscard]] uint32_t GetAtlasSrvIndex() const noexcept { return m_atlasSrvIndex; }

    /// Returns the indirection table SRV descriptor index.
    [[nodiscard]] uint32_t GetIndirectionSrvIndex() const noexcept { return m_indirectionSrvIndex; }

    /// Returns the object data SRV descriptor index.
    [[nodiscard]] uint32_t GetObjectBufferSrvIndex() const noexcept { return m_objectBufferSrvIndex; }

    /// Returns true if the atlas has been initialized.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Returns total VRAM used by the atlas texture in bytes.
    [[nodiscard]] uint64_t GetMemoryUsage() const noexcept;

    /// Returns the number of brick slots currently allocated.
    [[nodiscard]] uint32_t GetAllocatedBrickCount() const noexcept { return m_nextFreeBrick; }

    /// Returns the total capacity of the atlas in brick slots.
    [[nodiscard]] uint32_t GetTotalBrickCapacity() const noexcept { return m_totalBricks; }

    /// Returns the number of registered SDF object instances with valid data.
    /// Used for dispatch sizing in culling/tracing compute shaders.
    [[nodiscard]] uint32_t GetRegisteredObjectCount() const noexcept {
        return static_cast<uint32_t>(m_objectSlots.size());
    }

private:
    IRHIDevice*      m_device         = nullptr;

    // ── Atlas 3D texture ─────────────────────────────────────────────────
    RHITextureHandle m_atlasTexture;
    uint32_t         m_atlasSrvIndex  = UINT32_MAX;
    uint32_t         m_atlasSize      = 0;
    bool             m_initialized    = false;

    // ── Indirection table (StructuredBuffer<SdfIndirectionEntry>) ────────
    RHIBufferHandle  m_indirectionBuffer;
    uint32_t         m_indirectionSrvIndex = UINT32_MAX;
    uint32_t         m_nextFreeIndirection = 0;

    // CPU mirror of the indirection table for staging uploads
    std::vector<SdfIndirectionEntry> m_indirectionCpu;

    // ── Object data (StructuredBuffer<SdfObjectGpuData>) ─────────────────
    RHIBufferHandle  m_objectBuffer;
    RHIBufferHandle  m_objectStagingBuffer;
    uint32_t         m_objectBufferSrvIndex = UINT32_MAX;

    // CPU mirror of object data
    std::vector<SdfObjectGpuData> m_objectDataCpu;

    // Maps objectId -> slot index in m_objectDataCpu
    std::unordered_map<uint32_t, uint32_t> m_objectSlots;

    // Dirty flags per object slot (bitset would be better for large counts,
    // but a simple vector<bool> suffices for kMaxSdfObjects = 2048)
    std::vector<bool> m_objectDirty;

    // Next free slot for object allocation (linear, reuses freed slots)
    std::vector<uint32_t> m_freeObjectSlots;

    // ── Mesh registration tracking ───────────────────────────────────────
    struct MeshAllocation {
        uint32_t atlasOffset;         ///< First brick index in atlas.
        uint32_t brickCount;          ///< Number of bricks in atlas.
        uint32_t indirectionOffset;   ///< First entry in the indirection table.
        uint32_t indirectionCount;    ///< Number of indirection entries.
        MeshSdfAsset asset;           ///< CPU-side asset data (for object registration).
    };
    std::unordered_map<uint32_t, MeshAllocation> m_meshAllocations;

    // ── Brick allocator ──────────────────────────────────────────────────
    uint32_t m_nextFreeBrick = 0;
    uint32_t m_totalBricks   = 0;

    // ── Staging buffer for atlas uploads ─────────────────────────────────
    RHIBufferHandle m_stagingBuffer;
    uint64_t        m_stagingBufferSize = 0;

    /// Helper: uploads brick data from a MeshSdfAsset to the atlas texture.
    void UploadBricksToAtlas(const MeshSdfAsset& asset, uint32_t atlasOffset,
                             IRHICommandList* cmdList);

    /// Helper: populates indirection table entries for a mesh.
    void PopulateIndirectionTable(const MeshSdfAsset& asset, uint32_t atlasOffset,
                                  uint32_t indirectionOffset);

    /// Helper: converts a linear brick index to 3D atlas coordinates.
    void BrickIndexTo3D(uint32_t linearIndex, uint32_t& outX, uint32_t& outY, uint32_t& outZ) const;
};

} // namespace uz
