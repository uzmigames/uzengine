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
/// Does NOT own the compiler — caller manages its lifetime and passes it
/// via `SetCompiler()`.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render/init thread.
/// Hot-reload polling (`PollHotReload()`) must also be on the same thread.
///
/// # Lifetime
/// Owns all subsystems via PIMPL. The external `ShaderCompiler*` set via
/// `SetCompiler()` must outlive the pipeline (or be set to `nullptr` before
/// the compiler is destroyed).
///
/// # Examples
/// ```cpp
/// ShaderPipeline pipeline;
/// pipeline.Init({.cacheDir = "cache/shaders", .targetBackend = "dx12_sm66"});
/// pipeline.Mount("/engine", "shaders/engine");
/// pipeline.SetCompiler(&compiler);
///
/// auto result = pipeline.CompileAll();
/// UZ_LOG_INFO("Compiled {}/{} shaders ({} cached, {} failed)",
///             result.compiled, result.total, result.cached, result.failed);
///
/// // Per-frame in dev builds:
/// auto reloaded = pipeline.PollHotReload();
/// ```
class ShaderPipeline {
public:
    /// Configuration for the pipeline.
    struct Config {
        std::string cacheDir = "cache/shaders";    ///< Root cache directory on disk
        std::string targetBackend = "dx12_sm67";   ///< Target backend string for cache subdirectory naming
        bool enableHotReload = true;               ///< Enable file watching for dev builds
    };

    /// Statistics returned by `CompileAll()`.
    ///
    /// `total = cached + compiled + failed`
    struct CompileAllResult {
        size_t total = 0;                          ///< Total shader variants enumerated
        size_t cached = 0;                         ///< Already in cache (skipped compilation)
        size_t compiled = 0;                       ///< Successfully compiled this run
        size_t failed = 0;                         ///< Failed to compile (see `errors`)
        std::vector<std::string> errors;           ///< One error message per failed variant
    };

    ShaderPipeline();
    ~ShaderPipeline();

    ShaderPipeline(const ShaderPipeline&) = delete;
    ShaderPipeline& operator=(const ShaderPipeline&) = delete;

    /// Initializes the pipeline with all subsystems (VFS, cache, source manager, etc.).
    ///
    /// Safe to call multiple times — subsequent calls are no-ops.
    ///
    /// # Arguments
    /// * `config` — Pipeline configuration (cache dir, backend, hot-reload toggle).
    void Init(const Config& config);

    /// Shuts down and releases all subsystem resources.
    ///
    /// Safe to call multiple times or without prior `Init()`.
    void Shutdown();

    /// Mounts a shader directory in the VFS.
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual prefix (e.g., "/engine").
    /// * `realPath` — Real filesystem directory (e.g., "shaders/engine").
    void Mount(const std::string& virtualPath, const std::string& realPath);

    /// Compiles all shaders that are missing from the cache.
    ///
    /// Enumerates all `.hlsl` files in VFS mounts, expands permutations via
    /// `#pragma uz_permutation`, checks the cache, and compiles missing variants.
    ///
    /// # Returns
    /// `CompileAllResult` with counts and error messages.
    [[nodiscard]] CompileAllResult CompileAll();

    /// Compiles a single shader file (all permutations).
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to the `.hlsl` file.
    ///
    /// # Returns
    /// `true` if all variants compiled successfully or were already cached.
    bool CompileShader(const std::string& virtualPath);

    /// Loads compiled bytecode for a shader variant from the VFS.
    ///
    /// Checks the hot-reload dirty flag, recompiles if needed, then returns
    /// bytecode from the cache.
    ///
    /// # Arguments
    /// * `virtualPath` — Virtual path to the `.hlsl` file.
    /// * `permutationDefines` — Semicolon-separated defines string (e.g.,
    ///   "HAS_NORMAL_MAP=1;SHADOW_QUALITY=HIGH"). Empty for the base variant.
    ///
    /// # Returns
    /// Compiled bytecode, or an empty vector on failure.
    [[nodiscard]] std::vector<uint8_t> LoadBytecode(
        const std::string& virtualPath,
        const std::string& permutationDefines = "");

    /// Compiles an in-memory HLSL source string and returns bytecode.
    ///
    /// Used for embedded engine shaders whose source is baked into C++ headers
    /// at build time (via uzembed). Bypasses VFS, source manager, and hot-reload
    /// entirely — the source is compiled directly via the shader compiler.
    ///
    /// The shader stage is inferred from `name` using the standard suffix
    /// convention (`_vs`, `_ps`, `_cs`, etc.).
    ///
    /// # Arguments
    /// * `name` — Logical name for error messages and stage inference
    ///   (e.g., "overlay_vs.hlsl").
    /// * `hlslSource` — Complete HLSL source code string.
    /// * `permutationDefines` — Semicolon-separated defines, or empty.
    ///
    /// # Returns
    /// Compiled bytecode, or an empty vector on failure.
    [[nodiscard]] std::vector<uint8_t> LoadBytecodeFromSource(
        const std::string& name,
        const std::string& hlslSource,
        const std::string& permutationDefines = "");

    /// Polls for hot-reload file changes (call once per frame in dev builds).
    ///
    /// # Returns
    /// Virtual paths of shaders that were marked dirty and recompiled this poll.
    [[nodiscard]] std::vector<std::string> PollHotReload();

    /// Returns a reference to the internal VFS for external mount/resolve operations.
    [[nodiscard]] ShaderVFS& GetVFS();

    /// Returns a reference to the internal source manager.
    [[nodiscard]] ShaderSourceManager& GetSourceManager();

    /// Sets the external shader compiler (not owned by the pipeline).
    ///
    /// # Arguments
    /// * `compiler` — Pointer to an initialized `ShaderCompiler`. Must outlive the
    ///   pipeline, or be set to `nullptr` before the compiler is destroyed.
    void SetCompiler(ShaderCompiler* compiler);

    /// Returns true if `Init()` has been called and `Shutdown()` has not.
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
