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

/// Compiled shader bytecode stored in/retrieved from cache.
struct CachedShader {
    uint64_t identityHash;          ///< Full identity hash (source + defines + target)
    std::vector<uint8_t> bytecode;  ///< Compiled bytecode (DXIL, SPIRV, etc.)
    std::string sourcePath;         ///< Original virtual path (for debugging)
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
class ShaderCache {
public:
    /// Constructs a cache at the given base directory.
    ///
    /// Subdirectories for each backend/SM combination are created on demand.
    explicit ShaderCache(const std::string& cacheDirectory);

    /// Looks up a cached shader by identity hash and subdirectory.
    ///
    /// Returns the bytecode if found, or nullopt if not cached.
    [[nodiscard]] std::optional<std::vector<uint8_t>> Lookup(
        const std::string& subdirectory,
        uint64_t identityHash) const;

    /// Stores compiled bytecode in the cache.
    ///
    /// Creates the subdirectory if it doesn't exist.
    /// Returns true on success.
    bool Store(const std::string& subdirectory,
               uint64_t identityHash,
               const std::vector<uint8_t>& bytecode,
               const std::string& sourcePath);

    /// Returns true if a cache entry exists for the given hash.
    [[nodiscard]] bool Contains(const std::string& subdirectory,
                                 uint64_t identityHash) const;

    /// Removes a single cache entry.
    bool Remove(const std::string& subdirectory, uint64_t identityHash);

    /// Returns the subdirectory name for a backend/shader model combination.
    ///
    /// Examples: "dx12_sm66", "dx12_sm60", "vulkan_spirv16"
    [[nodiscard]] static std::string GetSubdirectory(const std::string& backend,
                                                       const std::string& shaderModel);

    /// Returns the total number of cached entries across all subdirectories.
    [[nodiscard]] size_t GetEntryCount() const;

    /// Returns the total size in bytes of all cached entries.
    [[nodiscard]] size_t GetTotalSizeBytes() const;

    /// Removes all cache entries.
    void Clear();

    /// Returns the cache base directory.
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
