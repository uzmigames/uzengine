/// UzEngine GPU Streaming Pool — Mip-level streaming with LRU eviction.
///
/// Manages a pool of streamable textures where individual mip levels can be
/// loaded/evicted based on screen-space importance and memory pressure.
///
/// # Design
/// - Each streamable texture has a "desired mip count" (set by the renderer
///   based on screen-space size) and a "resident mip count" (actually loaded).
/// - Mips are loaded from coarsest to finest. The coarsest mip (index
///   `totalMipCount - 1`) is always resident; finer mips stream in on demand.
/// - When memory pressure occurs, the pool evicts the least-recently-used
///   mip levels starting from the finest (mip 0) until within budget.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
///
/// # Usage
/// ```cpp
/// GpuStreamingPool pool;
/// pool.Init(device, 256 * 1024 * 1024); // 256 MB budget
///
/// uint64_t mipSizes[] = {4*1024*1024, 1*1024*1024, 256*1024, 64*1024};
/// auto id = pool.Register(texHandle, 4, mipSizes, "brick_albedo");
///
/// pool.SetDesiredMips(id, 3);     // renderer wants 3 mips loaded
/// pool.Touch(id, frameNumber);    // mark as used this frame
/// pool.Update(frameNumber);       // process loads/evictions
/// ```

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class IRHIDevice;

/// Unique identifier for a streamable texture in the pool.
using StreamingTextureId = uint32_t;

/// Sentinel value indicating an invalid or unregistered streaming texture.
constexpr StreamingTextureId kInvalidStreamingTexture = UINT32_MAX;

// ─────────────────────────────────────────────────────────────────────────────
// MipResidencyState — Per-mip loading state
// ─────────────────────────────────────────────────────────────────────────────

/// Residency state of a single mip level in the streaming pool.
///
/// Transitions follow this order:
/// `NotLoaded -> Loading -> Resident` (load path)
/// `Resident -> Evicting -> NotLoaded` (eviction path)
enum class MipResidencyState : uint8_t {
    NotLoaded, ///< Mip data not in GPU memory.
    Loading,   ///< Upload in progress (will become Resident on completion).
    Resident,  ///< Fully loaded and usable for rendering.
    Evicting,  ///< Being removed from GPU memory (will become NotLoaded).
};

/// Returns a human-readable name for a mip residency state.
///
/// # Returns
/// A null-terminated string literal. Returns "Unknown" for out-of-range values.
[[nodiscard]] const char* MipResidencyStateName(MipResidencyState state);

// ─────────────────────────────────────────────────────────────────────────────
// MipLevelInfo — Per-mip metadata
// ─────────────────────────────────────────────────────────────────────────────

/// Metadata for a single mip level within a streamable texture.
///
/// Index 0 is the finest (largest) mip level, index `totalMipCount - 1` is the
/// coarsest (smallest, typically 1x1 or 4x4).
struct MipLevelInfo {
    MipResidencyState state = MipResidencyState::NotLoaded;
    uint64_t sizeBytes = 0;     ///< Size of this mip level in GPU memory.
    uint64_t lastUsedFrame = 0; ///< Frame number when this mip was last sampled.
};

// ─────────────────────────────────────────────────────────────────────────────
// StreamingTextureEntry — Per-texture tracking data
// ─────────────────────────────────────────────────────────────────────────────

/// A streamable texture entry managed by the pool.
///
/// Tracks both the desired and actual mip residency, total memory footprint,
/// and LRU frame timestamp for eviction ordering.
struct StreamingTextureEntry {
    RHITextureHandle texture = RHITextureHandle::Invalid();
    std::string debugName;
    uint32_t totalMipCount = 0;    ///< Total mip levels available on disk/source.
    uint32_t residentMipCount = 0; ///< Number of mips currently loaded (counted from coarsest).
    uint32_t desiredMipCount = 0;  ///< Number of mips the renderer wants loaded.
    uint64_t totalSizeBytes = 0;   ///< Total size if all mips were loaded.
    uint64_t residentSizeBytes = 0;///< Current resident GPU memory for this texture.
    uint64_t lastUsedFrame = 0;    ///< Last frame this texture was referenced via Touch().
    std::vector<MipLevelInfo> mips;///< Per-mip info, index 0 = finest.
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuStreamingPool — Main streaming pool class
// ─────────────────────────────────────────────────────────────────────────────

/// GPU streaming pool with LRU mip-level eviction and memory budget enforcement.
///
/// Manages a set of streamable textures, tracking which mip levels are resident
/// and evicting least-recently-used fine mips when memory pressure occurs.
///
/// # Lifecycle
/// 1. Construct, call `Init(device, budgetBytes)`.
/// 2. `Register()` textures as they become streamable.
/// 3. Each frame: `SetDesiredMips()` + `Touch()` for visible textures, then `Update()`.
/// 4. `Shutdown()` before destroying the device.
///
/// # Eviction Policy
/// When resident bytes exceed the budget, `EvictLRU()` sorts all textures by
/// `lastUsedFrame` (ascending) and evicts the finest resident mip of the
/// least-recently-used texture first, repeating until enough memory is freed.
class GpuStreamingPool {
public:
    GpuStreamingPool() = default;
    ~GpuStreamingPool() = default;

    // Non-copyable, non-movable.
    GpuStreamingPool(const GpuStreamingPool&) = delete;
    GpuStreamingPool& operator=(const GpuStreamingPool&) = delete;
    GpuStreamingPool(GpuStreamingPool&&) = delete;
    GpuStreamingPool& operator=(GpuStreamingPool&&) = delete;

    /// Initializes the streaming pool.
    ///
    /// # Parameters
    /// - `device` — Non-null RHI device pointer. Must outlive the pool.
    /// - `budgetBytes` — Total memory budget for streaming textures in bytes.
    ///   Must be > 0.
    ///
    /// # Returns
    /// `true` on success, `false` if device is null or budget is zero.
    [[nodiscard]] bool Init(IRHIDevice* device, uint64_t budgetBytes);

    /// Shuts down the pool and clears all tracked entries.
    ///
    /// Does NOT destroy the underlying RHI textures — the caller retains
    /// ownership. Logs warnings for any textures still registered.
    void Shutdown();

    /// Registers a texture for streaming management.
    ///
    /// # Parameters
    /// - `texture` — Valid RHI texture handle. Ownership stays with caller.
    /// - `totalMipCount` — Total number of mip levels. Must be >= 1.
    /// - `mipSizes` — Array of `totalMipCount` sizes in bytes. Index 0 = finest mip.
    ///   Must not be null.
    /// - `debugName` — Optional name for logging. May be null.
    ///
    /// # Returns
    /// A `StreamingTextureId` for future reference, or `kInvalidStreamingTexture`
    /// on invalid input.
    [[nodiscard]] StreamingTextureId Register(RHITextureHandle texture,
                                              uint32_t totalMipCount,
                                              const uint64_t* mipSizes,
                                              const char* debugName = nullptr);

    /// Unregisters a texture from the pool.
    ///
    /// Subtracts any resident bytes from the pool total. Does NOT destroy
    /// the RHI texture. Logs a warning if `id` is not found.
    ///
    /// # Parameters
    /// - `id` — Previously returned by `Register()`.
    void Unregister(StreamingTextureId id);

    /// Sets the desired mip count for a texture.
    ///
    /// Called by the renderer each frame based on screen-space size.
    /// Clamped to `[1, totalMipCount]`.
    ///
    /// # Parameters
    /// - `id` — Registered texture ID.
    /// - `desiredMipCount` — How many mips (from coarsest) should be resident.
    void SetDesiredMips(StreamingTextureId id, uint32_t desiredMipCount);

    /// Marks a texture as used this frame, updating LRU timestamps.
    ///
    /// Must be called for every texture sampled during rendering to prevent
    /// premature eviction.
    ///
    /// # Parameters
    /// - `id` — Registered texture ID.
    /// - `frameNumber` — Current frame number.
    void Touch(StreamingTextureId id, uint64_t frameNumber);

    /// Notifies the pool that a mip level has finished uploading.
    ///
    /// Transitions the mip from `Loading` to `Resident` and updates
    /// resident byte counters.
    ///
    /// # Parameters
    /// - `id` — Registered texture ID.
    /// - `mipLevel` — Mip index (0 = finest) that completed upload.
    void NotifyMipLoaded(StreamingTextureId id, uint32_t mipLevel);

    /// Processes streaming requests and enforces the memory budget.
    ///
    /// Call once per frame. Performs two passes:
    /// 1. **Load pass** — For textures needing more mips, marks the next coarsest
    ///    unloaded mip as `Loading` (up to `maxUploadsPerFrame`).
    /// 2. **Eviction pass** — If `residentBytes > budgetBytes`, calls `EvictLRU()`
    ///    to free the excess.
    ///
    /// # Parameters
    /// - `frameNumber` — Current frame number for LRU tracking.
    /// - `maxUploadsPerFrame` — Maximum mip uploads to initiate this frame.
    void Update(uint64_t frameNumber, uint32_t maxUploadsPerFrame = 4);

    /// Evicts mip levels to free at least `bytesToFree` bytes.
    ///
    /// Uses LRU ordering: sorts textures by `lastUsedFrame` ascending,
    /// then evicts the finest resident mip of each LRU texture until enough
    /// memory is freed or no more mips can be evicted.
    ///
    /// # Parameters
    /// - `bytesToFree` — Minimum bytes to free. May free more if mip granularity
    ///   causes overshoot.
    ///
    /// # Returns
    /// Actual bytes freed. May be less than `bytesToFree` if all evictable mips
    /// have been removed (only coarsest mips remain).
    [[nodiscard]] uint64_t EvictLRU(uint64_t bytesToFree);

    /// Returns current resident bytes across all tracked textures.
    [[nodiscard]] uint64_t GetResidentBytes() const noexcept { return m_residentBytes; }

    /// Returns the configured memory budget in bytes.
    [[nodiscard]] uint64_t GetBudgetBytes() const noexcept { return m_budgetBytes; }

    /// Returns the number of registered textures.
    [[nodiscard]] uint32_t GetTextureCount() const noexcept {
        return static_cast<uint32_t>(m_textures.size());
    }

    /// Returns true if the pool has been initialized via `Init()`.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }

    /// Returns a const reference to a texture entry, or nullptr if not found.
    ///
    /// # Parameters
    /// - `id` — Registered texture ID.
    ///
    /// # Returns
    /// Pointer to the entry, or `nullptr` if `id` is not registered.
    [[nodiscard]] const StreamingTextureEntry* GetEntry(StreamingTextureId id) const;

    /// Logs a summary of pool status (budget, resident bytes, texture count).
    void LogSummary() const;

private:
    /// Collects all texture IDs sorted by lastUsedFrame ascending (LRU first).
    [[nodiscard]] std::vector<StreamingTextureId> GetLRUSortedTextures() const;

    IRHIDevice* m_device = nullptr;
    uint64_t m_budgetBytes = 0;
    uint64_t m_residentBytes = 0;
    StreamingTextureId m_nextId = 0;
    std::unordered_map<StreamingTextureId, StreamingTextureEntry> m_textures;
};

} // namespace uz
