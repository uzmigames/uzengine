/// UzEngine Pipeline State Object Cache — Deduplication by content hash.
///
/// In-memory cache for PSOs keyed by content hash of their descriptor.
/// Multiple calls to `CreateGraphicsPipeline(desc1)` and `CreateGraphicsPipeline(desc2)`
/// with identical descriptors will return the same cached handle, reducing GPU memory
/// waste and compilation time.
///
/// # Usage
/// ```cpp
/// PsoCache cache;
/// cache.Init(256);  // Initial capacity
///
/// // Compute a deterministic key from the descriptor
/// PsoCacheKey key = ComputePsoCacheKey(desc);
///
/// // Lookup in cache (no creation cost on hit)
/// auto cached = cache.Lookup(key);
/// if (cached.IsValid()) {
///     UZ_STAT_INCREMENT(RHI, PsoCacheHits);
///     return cached;
/// }
///
/// // Miss — create the PSO the usual way
/// auto handle = device->CreateGraphicsPipeline(desc);
/// if (handle.IsValid()) {
///     cache.Insert(key, handle);
///     UZ_STAT_INCREMENT(RHI, PsoCacheMisses);
/// }
/// return handle;
/// ```
///
/// # Hot-Reload
/// When a shader is recompiled, call `InvalidateByShader(shaderHandle)` to evict
/// all cached PSOs that depend on that shader. The cache will be repopulated on
/// the next `CreateGraphicsPipeline` call.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread (same constraint
/// as `IRHIDevice::CreateGraphicsPipeline`).

#pragma once

#include "rhi_types.h"
#include <cstdint>

namespace uz {

// Forward declaration — defined in rhi_types.h
struct RHIGraphicsPipelineDesc;

// ─────────────────────────────────────────────────────────────────────────────
// PsoCacheKey — Content-hash key for pipeline descriptors
// ─────────────────────────────────────────────────────────────────────────────

/// Content-hash key for PSO deduplication.
///
/// Computed via FNV-1a mixing of all descriptor fields that affect the compiled
/// PSO (shader handles, blend state, depth state, rasterizer state, topology,
/// render target formats, input layout). Excludes debug names and pointer values.
///
/// Two identical pipeline descriptors always produce identical keys, enabling
/// safe deduplication across runs.
struct PsoCacheKey {
    uint64_t hash;  ///< FNV-1a 64-bit hash of descriptor canonical bytes

    /// Comparison operators for use in hash maps
    bool operator==(const PsoCacheKey& other) const { return hash == other.hash; }
    bool operator!=(const PsoCacheKey& other) const { return hash != other.hash; }

    /// Creates an invalid key (hash = 0). Used as a sentinel.
    [[nodiscard]] static PsoCacheKey Invalid() { return {0}; }

    /// Returns true if this key was never computed (hash = 0).
    bool IsValid() const { return hash != 0; }
};

/// Computes a content-hash key for a graphics pipeline descriptor.
///
/// Hashes all descriptor fields that affect the compiled PSO: shader handles
/// (index + generation), topology, rasterizer state, depth-stencil state,
/// blend state per render target, render target formats, depth-stencil format,
/// and input layout (if SM5 variant).
///
/// Debug names are deliberately excluded to allow the same PSO to be created
/// with different debug names.
///
/// # Arguments
/// * `desc` — Graphics pipeline descriptor to hash
///
/// # Returns
/// A `PsoCacheKey` with the computed hash. Always valid (hash != 0).
[[nodiscard]] PsoCacheKey ComputePsoCacheKey(const RHIGraphicsPipelineDesc& desc);

// ─────────────────────────────────────────────────────────────────────────────
// PsoCache — Open-addressing hash map for deduplication
// ─────────────────────────────────────────────────────────────────────────────

/// In-memory cache of pipeline state objects keyed by content hash.
///
/// Uses open-addressing with Robin Hood probing on a power-of-2 capacity array.
/// All allocations happen during `Init()` from the permanent arena. No per-frame
/// or per-lookup allocations.
///
/// Capacity doubles automatically (with rehash) when load factor exceeds 70%.
/// Initial capacity defaults to 256, supporting ~180 unique PSOs before rehash.
class PsoCache {
public:
    PsoCache() = default;
    ~PsoCache() = default;

    PsoCache(const PsoCache&) = delete;
    PsoCache& operator=(const PsoCache&) = delete;
    PsoCache(PsoCache&&) = delete;
    PsoCache& operator=(PsoCache&&) = delete;

    /// Initializes the cache with a given capacity (must be power-of-2).
    ///
    /// Allocates the slot array from the permanent arena. Safe to call multiple
    /// times; subsequent calls are no-ops if already initialized.
    ///
    /// # Arguments
    /// * `capacity` — Initial slot count. Must be power-of-2. Defaults to 256.
    void Init(uint32_t capacity = 256);

    /// Shuts down and deallocates the cache.
    ///
    /// Does NOT destroy the pipeline handles stored in the cache — the caller
    /// is responsible for destroying GPU objects. The `InvalidateByShader`
    /// or `Clear` methods can be used to evict handles before shutdown.
    void Shutdown();

    /// Looks up a PSO by key. Returns an invalid handle on miss.
    ///
    /// O(1) average case, O(n) worst case (load factor = 1.0, pathological hash).
    /// In practice, with Robin Hood probing and < 70% load factor, always O(1).
    ///
    /// # Arguments
    /// * `key` — Content-hash key to look up
    ///
    /// # Returns
    /// Valid handle if found, invalid handle otherwise.
    [[nodiscard]] RHIPipelineHandle Lookup(const PsoCacheKey& key) const;

    /// Inserts a PSO into the cache at the given key.
    ///
    /// If the key already exists, overwrites the old handle with the new one.
    /// This is useful for hot-reload scenarios where a shader is recompiled
    /// and the PSO needs to be updated.
    ///
    /// If the cache reaches 70% load factor, automatically rehashes to 2x capacity.
    ///
    /// # Arguments
    /// * `key` — Content-hash key
    /// * `pipeline` — Handle to the compiled PSO
    void Insert(const PsoCacheKey& key, RHIPipelineHandle pipeline);

    /// Evicts all cache entries whose shader handle matches.
    ///
    /// Called during shader hot-reload to invalidate all dependent PSOs.
    /// The caller is responsible for destroying the evicted GPU objects.
    ///
    /// # Arguments
    /// * `shader` — Shader handle to match (vertex, pixel, compute, etc.)
    ///
    /// # Returns
    /// Number of cache entries evicted.
    uint32_t InvalidateByShader(RHIShaderHandle shader);

    /// Evicts all cache entries.
    ///
    /// Does NOT destroy the pipeline handles — the caller must do that.
    /// Useful for scene transitions or full cache invalidation on backend change.
    void Clear();

    /// Returns the current number of occupied slots.
    [[nodiscard]] uint32_t GetEntryCount() const { return m_count; }

    /// Returns the total slot capacity.
    [[nodiscard]] uint32_t GetCapacity() const { return m_capacity; }

    /// Returns the current load factor (entries / capacity).
    [[nodiscard]] float GetLoadFactor() const {
        return m_capacity > 0 ? static_cast<float>(m_count) / static_cast<float>(m_capacity) : 0.0F;
    }

    /// Returns true if initialized and ready for use.
    [[nodiscard]] bool IsInitialized() const { return m_slots != nullptr; }

private:
    struct Slot {
        PsoCacheKey key = {0};
        RHIPipelineHandle pipeline = RHIPipelineHandle::Invalid();
        bool occupied = false;
    };

    /// Inserts into the given slots array (used for rehashing).
    void InsertIntoSlots(Slot* slots, uint32_t mask, const PsoCacheKey& key,
                         RHIPipelineHandle pipeline);

    /// Rehashes to 2x capacity when load factor exceeds 70%.
    void Rehash();

    Slot* m_slots = nullptr;        ///< Open-addressing slot array (power-of-2)
    uint32_t m_capacity = 0;         ///< Total slot capacity
    uint32_t m_count = 0;            ///< Number of occupied slots
    uint32_t m_mask = 0;             ///< Capacity - 1 (for modulo via bitwise AND)
};

}  // namespace uz
