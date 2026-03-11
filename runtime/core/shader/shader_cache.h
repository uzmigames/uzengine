/// UzEngine Shader Cache
///
/// Stores compiled shader bytecode on disk, keyed by xxHash64 identity hash.
/// Cache layout: `{cacheDir}/{backend}_{sm}/{hash}.bin`
///
/// # Design
/// - Content-addressed: hash changes when source, includes, defines, or target change
/// - No explicit invalidation needed — stale entries are simply never referenced
/// - Manifest.json for debugging only (maps hash → source path for inspection)
/// - Thread-safe reads, single-writer (compilation is serialized per shader)

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace uz {

/// Compiled shader bytecode stored in or retrieved from the disk cache.
///
/// Used as the transfer type between the cache and the compilation pipeline.
/// The `sourcePath` is stored for debugging/manifest purposes only and does
/// not affect cache lookup.
struct CachedShader {
    uint64_t identityHash;          ///< Full identity hash (source + defines + target)
    std::vector<uint8_t> bytecode;  ///< Compiled bytecode (DXIL, SPIRV, etc.)
    std::string sourcePath;         ///< Original virtual path (for debugging/manifest only)
};

/// Disk-based shader bytecode cache.
///
/// Cache directory layout:
/// ```
/// cache/shaders/
///   dx12_sm66/
///     <hash>.bin        — compiled bytecode
///   dx12_sm60/
///     <hash>.bin
///   vulkan_spirv16/
///     <hash>.bin
///   manifest.json       — debug-only hash→path mapping
/// ```
/// Disk-based shader bytecode cache.
///
/// # Thread Safety
/// Reads are safe from multiple threads (file I/O is independent per entry).
/// Writes (Store/Remove/Clear) must be serialized by the caller — the shader
/// compilation pipeline ensures single-writer semantics per shader variant.
///
/// # Lifetime
/// Standalone. Does not hold references to other subsystems.
///
/// # Examples
/// ```cpp
/// ShaderCache cache("cache/shaders");
/// std::string subdir = ShaderCache::GetSubdirectory("dx12", "sm66");
///
/// // Store compiled bytecode
/// cache.Store(subdir, identityHash, bytecode, "/engine/pbr.hlsl");
///
/// // Lookup on next run
/// auto cached = cache.Lookup(subdir, identityHash);
/// if (cached) { /* use *cached as bytecode */ }
/// ```
class ShaderCache {
public:
    /// Constructs a cache rooted at the given base directory.
    ///
    /// The directory is NOT created immediately — subdirectories are created
    /// on demand when `Store()` is first called for a given backend.
    ///
    /// # Arguments
    /// * `cacheDirectory` — Base path for the cache (e.g., "cache/shaders").
    explicit ShaderCache(const std::string& cacheDirectory);

    /// Looks up a cached shader by identity hash and subdirectory.
    ///
    /// # Arguments
    /// * `subdirectory` — Backend subdirectory (e.g., "dx12_sm66").
    /// * `identityHash` — The content identity hash from `ShaderSourceManager`.
    ///
    /// # Returns
    /// The bytecode as a byte vector if found, or `std::nullopt` if not cached.
    [[nodiscard]] std::optional<std::vector<uint8_t>> Lookup(
        const std::string& subdirectory,
        uint64_t identityHash) const;

    /// Stores compiled bytecode in the cache.
    ///
    /// Creates the subdirectory if it doesn't exist.
    ///
    /// # Arguments
    /// * `subdirectory` — Backend subdirectory (e.g., "dx12_sm66").
    /// * `identityHash` — Content identity hash (used as filename).
    /// * `bytecode` — Compiled shader bytecode to store.
    /// * `sourcePath` — Original virtual path (written to manifest.json for debugging).
    ///
    /// # Returns
    /// `true` on success, `false` if directory creation or file write fails.
    bool Store(const std::string& subdirectory,
               uint64_t identityHash,
               const std::vector<uint8_t>& bytecode,
               const std::string& sourcePath);

    /// Returns true if a cache entry exists for the given hash.
    ///
    /// # Arguments
    /// * `subdirectory` — Backend subdirectory.
    /// * `identityHash` — Content identity hash to check.
    [[nodiscard]] bool Contains(const std::string& subdirectory,
                                 uint64_t identityHash) const;

    /// Removes a single cache entry from disk.
    ///
    /// # Arguments
    /// * `subdirectory` — Backend subdirectory.
    /// * `identityHash` — Content identity hash of the entry to remove.
    ///
    /// # Returns
    /// `true` if the file was deleted, `false` if it didn't exist or deletion failed.
    bool Remove(const std::string& subdirectory, uint64_t identityHash);

    /// Returns the subdirectory name for a backend/shader model combination.
    ///
    /// # Arguments
    /// * `backend` — Backend name (e.g., "dx12", "vulkan").
    /// * `shaderModel` — Shader model string (e.g., "sm66", "spirv16").
    ///
    /// # Returns
    /// Formatted subdirectory name (e.g., "dx12_sm66", "vulkan_spirv16").
    [[nodiscard]] static std::string GetSubdirectory(const std::string& backend,
                                                       const std::string& shaderModel);

    /// Returns the total number of cached entries across all subdirectories.
    [[nodiscard]] size_t GetEntryCount() const;

    /// Returns the total size in bytes of all cached entries on disk.
    [[nodiscard]] size_t GetTotalSizeBytes() const;

    /// Removes all cache entries and subdirectories.
    void Clear();

    /// Returns the cache base directory path.
    [[nodiscard]] const std::string& GetCacheDirectory() const { return m_cacheDir; }

private:
    /// Returns the full filesystem path for a cache entry.
    [[nodiscard]] std::string GetEntryPath(const std::string& subdirectory,
                                             uint64_t identityHash) const;

    /// Ensures a subdirectory exists under the cache root.
    bool EnsureDirectory(const std::string& subdirectory) const;

    std::string m_cacheDir;
};

} // namespace uz
