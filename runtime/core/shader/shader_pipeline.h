/// UzEngine Shader Pipeline
///
/// Top-level orchestrator that ties all shader subsystems together:
/// VFS, source manager, dependency graph, cache, compiler, and hot-reload.
///
/// Primary use case: post-switch shader compilation. When the user changes
/// `--rhi` or `--shader-model` at startup, CompileAll() enumerates every
/// shader, expands permutations, checks the cache, and compiles what's missing.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace uz {

class ShaderCache;
class ShaderCompiler;
class ShaderDependencyGraph;
class ShaderHotReload;
class ShaderSourceManager;
class ShaderVFS;

/// Orchestrates the full shader compilation pipeline.
///
/// Owns VFS, source manager, dependency graph, cache, and hot-reload.
/// Does NOT own the compiler — caller manages its lifetime.
class ShaderPipeline {
public:
    /// Configuration for the pipeline.
    struct Config {
        std::string cacheDir = "cache/shaders";    ///< Root cache directory
        std::string targetBackend = "dx12_sm67";   ///< Target backend string for cache subdir
        bool enableHotReload = true;               ///< Enable file watching in dev builds
    };

    /// Statistics returned by CompileAll().
    struct CompileAllResult {
        size_t total = 0;                          ///< Total shader variants enumerated
        size_t cached = 0;                         ///< Already in cache (skipped)
        size_t compiled = 0;                       ///< Successfully compiled this run
        size_t failed = 0;                         ///< Failed to compile
        std::vector<std::string> errors;           ///< Error messages for failures
    };

    ShaderPipeline();
    ~ShaderPipeline();

    ShaderPipeline(const ShaderPipeline&) = delete;
    ShaderPipeline& operator=(const ShaderPipeline&) = delete;

    /// Initialize the pipeline with all subsystems.
    ///
    /// Safe to call multiple times — subsequent calls are no-ops.
    void Init(const Config& config);

    /// Shutdown and release resources.
    ///
    /// Safe to call multiple times or without prior Init.
    void Shutdown();

    /// Mount a shader directory in the VFS.
    void Mount(const std::string& virtualPath, const std::string& realPath);

    /// Compile all shaders that are missing from the cache.
    ///
    /// Enumerates all .hlsl files in VFS mounts, expands permutations,
    /// checks cache, and compiles missing variants.
    [[nodiscard]] CompileAllResult CompileAll();

    /// Compile a single shader file (all permutations).
    ///
    /// Returns true if all variants compiled or were already cached.
    bool CompileShader(const std::string& virtualPath);

    /// Load compiled bytecode for a shader variant.
    ///
    /// Checks dirty flag, recompiles if needed, then returns from cache.
    /// Returns empty vector on failure.
    [[nodiscard]] std::vector<uint8_t> LoadBytecode(
        const std::string& virtualPath,
        const std::string& permutationDefines = "");

    /// Poll for hot-reload changes (call once per frame in dev).
    ///
    /// Returns virtual paths that were recompiled.
    [[nodiscard]] std::vector<std::string> PollHotReload();

    /// Get the VFS for external use.
    [[nodiscard]] ShaderVFS& GetVFS();

    /// Get the source manager.
    [[nodiscard]] ShaderSourceManager& GetSourceManager();

    /// Set the compiler (external, not owned).
    void SetCompiler(ShaderCompiler* compiler);

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    /// Compute cache key for a shader + permutation combo.
    std::string ComputeCacheKey(const std::string& virtualPath,
                                const std::string& permutationDefines);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized = false;
};

} // namespace uz
