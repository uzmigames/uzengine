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

/// Compilation target backend for hash differentiation.
///
/// Each target produces a different identity hash even for the same source,
/// ensuring that DXIL and SPIRV bytecode never collide in the cache.
enum class ShaderTarget : uint8_t {
    DXIL_SM6_6,   ///< DX12 Shader Model 6.6 (bindless, wave intrinsics)
    DXIL_SM6_0,   ///< DX12 Shader Model 6.0 (wave ops, no bindless)
    DXBC_SM5_1,   ///< DX12 Shader Model 5.1 (FXC legacy bytecode)
    SPIRV_1_6,    ///< Vulkan SPIR-V 1.6 (mesh shaders, ray tracing)
};

/// Result of loading and processing a shader source file.
///
/// Contains the fully resolved source text, all transitive include paths,
/// and the content hash used as the basis for cache identity keys.
struct ShaderSourceInfo {
    std::string virtualPath;             ///< Original virtual path requested by caller
    std::string resolvedPath;            ///< Real filesystem path after VFS resolution
    std::string source;                  ///< Full source content (as read from disk)
    std::vector<std::string> includes;   ///< All resolved include paths (recursive, deduplicated, sorted)
    uint64_t contentHash;                ///< xxHash64 of source + all include contents
};

/// Manages shader source loading, include resolution, and content hashing.
///
/// Caches loaded sources and their hashes. Invalidation is explicit via
/// `Invalidate()` (called by hot-reload on file change).
///
/// # Thread Safety
/// NOT thread-safe. All operations must be called from the same thread
/// (typically the render/compile thread).
///
/// # Lifetime
/// Holds a const reference to `ShaderVFS` — the VFS must outlive this object.
///
/// # Examples
/// ```cpp
/// ShaderVFS vfs;
/// vfs.Mount("/engine", "shaders/engine");
///
/// ShaderSourceManager mgr(vfs);
/// auto info = mgr.LoadSource("/engine/pbr.hlsl");
/// if (info) {
///     uint64_t hash = mgr.ComputeIdentityHash(
///         "/engine/pbr.hlsl", {"USE_PBR=1"}, ShaderTarget::DXIL_SM6_6, 1);
/// }
/// ```
class ShaderSourceManager {
public:
    /// Constructs a source manager backed by the given VFS.
    ///
    /// # Arguments
    /// * `vfs` — The virtual filesystem used to resolve and read shader files.
    ///   Must outlive this `ShaderSourceManager` instance.
    explicit ShaderSourceManager(const ShaderVFS& vfs);

    /// Loads a shader source file and resolves all includes recursively.
    ///
    /// Results are cached — repeated calls with the same path return the cached
    /// version without re-reading from disk. Call `Invalidate()` to force a reload.
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to the shader file (e.g., "/engine/pbr.hlsl").
    ///
    /// # Returns
    /// `ShaderSourceInfo` with resolved source, includes, and content hash.
    /// Returns `std::nullopt` if the path cannot be resolved or read, or if
    /// include resolution exceeds `kMaxIncludeDepth`.
    [[nodiscard]] std::optional<ShaderSourceInfo> LoadSource(const std::string& virtualPath);

    /// Computes the full identity hash for a shader with specific compilation parameters.
    ///
    /// The identity hash uniquely identifies a compiled artifact:
    /// `hash = xxHash64(source_hash + sorted_define_hashes + target + compiler_version)`
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to the shader file.
    /// * `defines` — Preprocessor defines (e.g., `{"USE_PBR=1", "QUALITY=HIGH"}`).
    /// * `target` — Compilation target backend.
    /// * `compilerVersion` — Compiler version number (changes invalidate cache).
    ///
    /// # Returns
    /// 64-bit identity hash. Returns 0 if the source cannot be loaded.
    [[nodiscard]] uint64_t ComputeIdentityHash(
        const std::string& virtualPath,
        const std::vector<std::string>& defines,
        ShaderTarget target,
        uint32_t compilerVersion);

    /// Invalidates the cached source for a file (virtual path).
    ///
    /// Called by hot-reload when a file changes on disk. The next `LoadSource()`
    /// call for this path will re-read from disk and recompute the hash.
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to invalidate.
    void Invalidate(const std::string& virtualPath);

    /// Invalidates all cached sources, forcing a full reload on next access.
    void InvalidateAll();

    /// Returns the number of cached source entries.
    [[nodiscard]] size_t GetCacheSize() const { return m_cache.size(); }

    /// Maximum include depth before aborting (prevents infinite recursion from
    /// circular `#include` chains).
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
