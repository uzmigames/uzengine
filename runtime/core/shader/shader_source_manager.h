/// UzEngine Shader Source Manager
///
/// Loads shader source files via ShaderVFS, recursively resolves `#include`
/// directives, and computes content-based identity hashes (xxHash64).
///
/// # Content-Addressed Identity
/// The hash key for a shader is:
///   xxHash64(source + sorted(include_hashes) + defines + target + compiler_version)
///
/// This means:
/// - Changing ANY included file changes the root shader's hash
/// - Identical source with different defines produces different hashes
/// - No timestamps, no filenames in the hash — pure content identity
///
/// # Include Resolution
/// Supports `#include "path"` with virtual paths resolved through ShaderVFS.
/// Relative includes are resolved relative to the including file's directory.
/// Maximum include depth: 32 (prevents infinite recursion).

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace uz {

class ShaderVFS;

/// Compilation target for hash differentiation.
enum class ShaderTarget : uint8_t {
    DXIL_SM6_6,   ///< DX12 Shader Model 6.6
    DXIL_SM6_0,   ///< DX12 Shader Model 6.0
    DXBC_SM5_1,   ///< DX12 Shader Model 5.1 (FXC)
    SPIRV_1_6,    ///< Vulkan SPIR-V 1.6
};

/// Result of loading and processing a shader source file.
struct ShaderSourceInfo {
    std::string virtualPath;             ///< Original virtual path
    std::string resolvedPath;            ///< Real filesystem path
    std::string source;                  ///< Full source content
    std::vector<std::string> includes;   ///< All resolved include paths (recursive, deduplicated)
    uint64_t contentHash;                ///< xxHash64 of source + all includes
};

/// Manages shader source loading, include resolution, and content hashing.
///
/// Caches loaded sources and their hashes. Invalidation is explicit via
/// `Invalidate()` (called by hot-reload on file change).
class ShaderSourceManager {
public:
    /// Constructs a source manager backed by the given VFS.
    explicit ShaderSourceManager(const ShaderVFS& vfs);

    /// Loads a shader source file and resolves all includes.
    ///
    /// Returns the source info with content hash, or nullopt on failure.
    /// Results are cached — repeated calls return the cached version.
    [[nodiscard]] std::optional<ShaderSourceInfo> LoadSource(const std::string& virtualPath);

    /// Computes the full identity hash for a shader with specific compilation parameters.
    ///
    /// hash = xxHash64(source_hash + sorted_define_hashes + target + compiler_version)
    [[nodiscard]] uint64_t ComputeIdentityHash(
        const std::string& virtualPath,
        const std::vector<std::string>& defines,
        ShaderTarget target,
        uint32_t compilerVersion);

    /// Invalidates the cached source for a file (virtual path).
    /// Called by hot-reload when a file changes on disk.
    void Invalidate(const std::string& virtualPath);

    /// Invalidates all cached sources.
    void InvalidateAll();

    /// Returns the number of cached source entries.
    [[nodiscard]] size_t GetCacheSize() const { return m_cache.size(); }

    /// Maximum include depth before aborting (prevents infinite recursion).
    static constexpr uint32_t kMaxIncludeDepth = 32;

private:
    /// Recursively resolves includes for a shader source.
    bool ResolveIncludes(
        const std::string& virtualPath,
        const std::string& source,
        std::vector<std::string>& outIncludes,
        std::unordered_set<std::string>& visited,
        uint32_t depth);

    /// Computes xxHash64 over the source content and all include contents.
    uint64_t ComputeContentHash(const std::string& source,
                                 const std::vector<std::string>& includes);

    /// Extracts the directory portion of a virtual path.
    static std::string GetDirectory(const std::string& path);

    /// Resolves a potentially relative include path against a base directory.
    static std::string ResolveIncludePath(const std::string& baseDir,
                                           const std::string& includePath);

    const ShaderVFS& m_vfs;
    std::unordered_map<std::string, ShaderSourceInfo> m_cache;
};

} // namespace uz
