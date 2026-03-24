/// UzEngine Shader Permutation Cache
///
/// Maps `ShaderPermutationKey` (64-bit packed material+feature flags) to cached
/// `RHIPipelineHandle` values. On cache hit, returns the pre-compiled PSO without
/// any shader compilation or pipeline creation overhead.
///
/// # Relationship to PsoCache
/// `PsoCache` (rhi/optimization/) deduplicates PSOs by content-hashing the full
/// `RHIGraphicsPipelineDesc`. `ShaderPermutationCache` operates one level above:
/// it maps a high-level permutation key (shading model + features) to the PSO
/// handle, bypassing descriptor construction entirely on cache hit.
///
/// # Hot-Reload
/// Call `InvalidateAll()` when any shader source changes. The cache will be
/// repopulated lazily on the next frame's draw calls.
///
/// # Thread Safety
/// NOT thread-safe. All access must happen on the render thread.

#pragma once

#include "shader_permutation_key.h"

#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <unordered_map>

namespace uz {

class IRHIDevice;
class ShaderManager;

// ---------------------------------------------------------------------------
// ShaderPermutationCache
// ---------------------------------------------------------------------------

/// Caches compiled PSOs keyed by `ShaderPermutationKey`.
///
/// On cache miss, the caller is responsible for compiling the shader variant
/// and creating the PSO through the normal pipeline. Once created, the handle
/// is inserted into this cache for future lookups.
///
/// The current implementation uses `std::unordered_map` with the key's
/// splitmix64 hash. For the expected permutation count (< 1000), this provides
/// excellent lookup performance with minimal memory overhead.
class ShaderPermutationCache {
public:
    ShaderPermutationCache() = default;
    ~ShaderPermutationCache() = default;

    // Non-copyable, movable.
    ShaderPermutationCache(const ShaderPermutationCache&) = delete;
    ShaderPermutationCache& operator=(const ShaderPermutationCache&) = delete;
    ShaderPermutationCache(ShaderPermutationCache&&) noexcept = default;
    ShaderPermutationCache& operator=(ShaderPermutationCache&&) noexcept = default;

    /// Looks up a cached PSO for the given permutation key.
    ///
    /// On cache hit, returns the previously stored `RHIPipelineHandle`.
    /// On cache miss, logs a warning and returns an invalid handle. The caller
    /// should then create the PSO through the normal pipeline and call `Insert()`
    /// to populate the cache.
    ///
    /// # Parameters
    /// - `key`: Permutation key to look up.
    ///
    /// # Returns
    /// Valid handle on cache hit; invalid handle on cache miss.
    [[nodiscard]] RHIPipelineHandle Lookup(const ShaderPermutationKey& key) const;

    /// Inserts a compiled PSO into the cache for the given permutation key.
    ///
    /// If the key already exists, the old handle is overwritten. This is the
    /// expected behavior during hot-reload: invalidate, then re-insert as
    /// permutations are encountered.
    ///
    /// # Parameters
    /// - `key`: Permutation key.
    /// - `pipeline`: Compiled PSO handle to cache.
    void Insert(const ShaderPermutationKey& key, RHIPipelineHandle pipeline);

    /// Attempts to find a cached PSO; on miss, returns an invalid handle and
    /// logs a diagnostic warning.
    ///
    /// This is the primary entry point for the renderer's draw loop. Full PSO
    /// creation on miss will be integrated in a future task (0.11) when the
    /// `PipelineLibrary` is connected to the permutation system.
    ///
    /// # Parameters
    /// - `key`: Permutation key describing the desired shader variant.
    /// - `device`: RHI device (reserved for future PSO creation on miss).
    /// - `shaderMgr`: Shader manager (reserved for future compilation on miss).
    ///
    /// # Returns
    /// Valid handle on cache hit; invalid handle on cache miss.
    [[nodiscard]] RHIPipelineHandle GetOrCreatePSO(
        const ShaderPermutationKey& key,
        IRHIDevice* device,
        ShaderManager* shaderMgr);

    /// Clears all cached entries.
    ///
    /// Does NOT destroy the pipeline handles -- the caller (typically the
    /// `PipelineLibrary` or `PsoCache`) owns GPU resource lifetimes.
    /// Call this when shaders are hot-reloaded or the rendering backend changes.
    void InvalidateAll();

    /// Returns the number of cached permutations.
    [[nodiscard]] uint32_t GetEntryCount() const;

private:
    /// Internal map from permutation key hash to cached PSO handle.
    /// Key: `ShaderPermutationKey::GetHash()` (splitmix64 of the packed bits).
    /// We store the full key alongside the hash to detect collisions.
    struct CacheEntry {
        ShaderPermutationKey key;       ///< Original key for collision detection.
        RHIPipelineHandle    pipeline;  ///< Cached PSO handle.
    };

    std::unordered_map<uint64_t, CacheEntry> m_cache;
};

} // namespace uz
