/// UzEngine PSO Disk Cache — Persistent storage for compiled pipeline state blobs.
///
/// Stores compiled PSO blobs on disk so subsequent application runs can skip
/// GPU pipeline compilation. Cache entries are keyed by the 64-bit FNV-1a hash
/// from `PsoCacheKey`.
///
/// Cache layout: `{cacheDir}/pso/{hex_hash}.bin`
///
/// # Backend Notes
/// - **DX12**: Blobs come from `ID3D12PipelineState::GetCachedBlob()`.
/// - **Vulkan**: `VkPipelineCache` handles disk persistence natively (not needed here).
/// - **Metal**: Binary archives (future).
///
/// # Thread Safety
/// Reads are safe from multiple threads (file I/O is independent per entry).
/// Writes (Store/Remove/Clear) must be serialized by the caller — the pipeline
/// library ensures single-writer semantics.
///
/// # Lifetime
/// Standalone. Does not hold references to other subsystems.
///
/// # Examples
/// ```cpp
/// PsoDiskCache cache("cache/pso");
///
/// // Store a blob after PSO creation
/// cache.Store(key.hash, blob.data(), blob.size());
///
/// // Lookup on next run
/// auto cached = cache.Lookup(key.hash);
/// if (cached) { /* pass to CachedPSO field */ }
/// ```

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace uz {

/// Disk-based PSO blob cache.
///
/// Stores compiled pipeline state blobs keyed by PsoCacheKey hash.
/// On DX12, blobs come from `ID3D12PipelineState::GetCachedBlob()`.
/// On Vulkan, `VkPipelineCache` handles this natively (not needed here).
/// On Metal, binary archives (future).
class PsoDiskCache {
public:
    /// Constructs a cache rooted at the given base directory.
    ///
    /// The directory is NOT created immediately — it is created on demand
    /// when `Store()` is first called.
    ///
    /// # Arguments
    /// * `cacheDirectory` — Base path for the cache (e.g., "cache/pso").
    explicit PsoDiskCache(const std::string& cacheDirectory);

    /// Looks up a cached PSO blob by key hash.
    ///
    /// # Arguments
    /// * `keyHash` — The 64-bit content hash from `PsoCacheKey`.
    ///
    /// # Returns
    /// The blob as a byte vector if found, or `std::nullopt` if not cached.
    [[nodiscard]] std::optional<std::vector<uint8_t>> Lookup(uint64_t keyHash) const;

    /// Stores a PSO blob to disk.
    ///
    /// Creates the cache directory if it doesn't exist.
    ///
    /// # Arguments
    /// * `keyHash` — Content hash (used as filename).
    /// * `data` — Pointer to the compiled PSO blob.
    /// * `sizeBytes` — Size of the blob in bytes.
    ///
    /// # Returns
    /// `true` on success, `false` if directory creation or file write fails.
    bool Store(uint64_t keyHash, const void* data, size_t sizeBytes);

    /// Returns true if a cache entry exists for the given hash.
    ///
    /// # Arguments
    /// * `keyHash` — Content hash to check.
    [[nodiscard]] bool Contains(uint64_t keyHash) const;

    /// Removes a single cache entry from disk.
    ///
    /// # Arguments
    /// * `keyHash` — Content hash of the entry to remove.
    ///
    /// # Returns
    /// `true` if the file was deleted, `false` if it didn't exist or deletion failed.
    bool Remove(uint64_t keyHash);

    /// Removes all cached entries.
    void Clear();

    /// Returns the total number of cached `.bin` entries on disk.
    [[nodiscard]] size_t GetEntryCount() const;

    /// Returns the total size in bytes of all cached entries on disk.
    [[nodiscard]] size_t GetTotalSizeBytes() const;

    /// Returns the cache directory path.
    [[nodiscard]] const std::string& GetCacheDirectory() const { return m_cacheDir; }

private:
    /// Returns the full filesystem path for a cache entry.
    [[nodiscard]] std::string GetEntryPath(uint64_t keyHash) const;

    /// Ensures the cache directory exists.
    [[nodiscard]] bool EnsureDirectory() const;

    std::string m_cacheDir;
};

} // namespace uz
