/// UzEngine Asset Cache
///
/// Content-addressed disk cache for imported assets. Stores compiled output
/// keyed by xxHash64 derived from importer identity + source content + options.
///
/// # Cache Key Derivation
/// ```
/// key = xxhash64(
///     importer_name       +
///     importer_version    +  (4 bytes LE)
///     source_content_hash +  (8 bytes LE, xxhash64 of source bytes)
///     options_hash           (8 bytes LE, xxhash64 of sorted options)
/// )
/// ```
///
/// # Storage Layout
/// ```
/// .cache/
///   imported/
///     <hex16>.bin    — cached asset bytes
/// ```
///
/// # Invariants
/// - Cache is safe to delete entirely — reimport regenerates everything
/// - No timestamps — content hash only
/// - Entries are immutable — never updated, only created or evicted

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace uz {

/// Content-addressed asset cache backed by flat files on disk.
///
/// Each entry is stored as `{cacheDir}/imported/{hash}.bin`.
/// Keys are xxHash64 values combining importer identity, source content,
/// and import options — ensuring automatic invalidation on any change.
class AssetCache {
public:
    /// Constructs a cache rooted at `cacheDirectory`.
    ///
    /// The `imported/` subdirectory is created on demand when the first
    /// entry is stored. The directory need not exist at construction time.
    ///
    /// # Arguments
    /// * `cacheDirectory` - Absolute or relative path to the cache root.
    ///
    /// # Side Effects
    /// Logs `Asset::Info` with the resolved cache directory path.
    explicit AssetCache(const std::string& cacheDirectory);

    /// Computes the cache key for an import operation.
    ///
    /// Feeds importer name, version (4 bytes LE), source content hash
    /// (8 bytes LE), and options hash (8 bytes LE) into an xxHash64
    /// streaming state to produce a single 64-bit key.
    ///
    /// # Arguments
    /// * `importerName` - Importer identifier (e.g., "shader").
    /// * `importerVersion` - Importer version number.
    /// * `sourceHash` - xxHash64 of the source file content.
    /// * `optionsHash` - xxHash64 of the sorted import options.
    ///
    /// # Returns
    /// A 64-bit cache key. Deterministic for identical inputs.
    [[nodiscard]] static uint64_t ComputeKey(
        const std::string& importerName,
        uint32_t importerVersion,
        uint64_t sourceHash,
        uint64_t optionsHash);

    /// Computes xxHash64 of raw bytes (seed 0).
    ///
    /// # Arguments
    /// * `data` - Pointer to the byte buffer.
    /// * `size` - Number of bytes to hash.
    ///
    /// # Returns
    /// 64-bit hash value. Returns 0 for null/zero-size input.
    [[nodiscard]] static uint64_t HashBytes(const void* data, size_t size);

    /// Computes xxHash64 of a string's byte content (seed 0).
    ///
    /// # Arguments
    /// * `str` - The string to hash.
    ///
    /// # Returns
    /// 64-bit hash value. Returns a consistent hash for empty strings.
    [[nodiscard]] static uint64_t HashString(const std::string& str);

    /// Looks up a cached entry by key.
    ///
    /// Reads the file at `{cacheDir}/imported/{key:016x}.bin`.
    ///
    /// # Arguments
    /// * `key` - Cache key (typically from `ComputeKey()`).
    ///
    /// # Returns
    /// - `std::optional` containing the cached bytes on cache hit.
    /// - `std::nullopt` on cache miss (file not found or empty).
    ///
    /// # Side Effects
    /// Logs `Asset::Debug` on cache hit with key and byte count.
    [[nodiscard]] std::optional<std::vector<uint8_t>> Lookup(uint64_t key) const;

    /// Stores an entry in the cache.
    ///
    /// Writes `data` to `{cacheDir}/imported/{key:016x}.bin`.
    /// Creates the `imported/` directory if it does not exist.
    ///
    /// # Arguments
    /// * `key` - Cache key.
    /// * `data` - Asset bytes to store.
    ///
    /// # Returns
    /// `true` on success, `false` if directory creation or file write fails.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the file path on write failure.
    bool Store(uint64_t key, const std::vector<uint8_t>& data);

    /// Returns `true` if a cache entry exists for the given key.
    ///
    /// Checks file existence only — does not read or validate contents.
    [[nodiscard]] bool Contains(uint64_t key) const;

    /// Removes a single cache entry from disk.
    ///
    /// # Returns
    /// `true` if the file was successfully deleted, `false` if it didn't exist.
    bool Remove(uint64_t key);

    /// Returns the total number of `.bin` files in the `imported/` directory.
    [[nodiscard]] size_t EntryCount() const;

    /// Returns the total size in bytes of all `.bin` files in the `imported/` directory.
    [[nodiscard]] size_t TotalSizeBytes() const;

    /// Removes the entire cache directory and all its contents.
    ///
    /// # Side Effects
    /// Logs `Asset::Info` confirming the deletion.
    void Clear();

    /// Returns the cache root directory path as provided at construction.
    [[nodiscard]] const std::string& CacheDirectory() const { return m_cacheDir; }

private:
    /// Returns the filesystem path for a cache entry.
    [[nodiscard]] std::string EntryPath(uint64_t key) const;

    /// Ensures the imported/ subdirectory exists.
    bool EnsureDirectory() const;

    std::string m_cacheDir;
};

} // namespace uz
