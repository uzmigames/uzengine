/// ShaderPipeline Implementation
///
/// Orchestrates VFS → source loading → permutation expansion → cache check →
/// compilation → cache store. Uses PIMPL to hide subsystem headers from
/// consumers.
///
/// CompileAll() supports parallel compilation via the job system when available.
/// Phase 1 (sequential): enumerate shaders, expand permutations, check cache.
/// Phase 2 (parallel): compile uncached variants using per-worker DXC instances.
/// Phase 3 (sequential): store results in cache, aggregate stats.
///
/// Windows-only: depends on DXC (ShaderCompiler) and FXC (ShaderCompilerFXC)
/// which are Windows-only shader compilers. macOS uses Slang via slang_compiler.h.

#ifdef _WIN32

#include "shader_pipeline_internal.h"

#include "shader_permutation.h"

#include <sstream>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace uz {

UZ_DEFINE_STAT(RHI, ShaderCompiles);
UZ_DEFINE_STAT(RHI, CacheHits);
UZ_DEFINE_STAT(RHI, CacheMisses);

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

ShaderPipeline::ShaderPipeline() = default;
ShaderPipeline::~ShaderPipeline() { Shutdown(); }

void ShaderPipeline::Init(const Config& config) {
    UZ_PROFILE_FUNCTION();
    if (m_initialized) {
        return;
    }

    m_impl = std::make_unique<Impl>(config);
    m_initialized = true;

    // Auto-detect Vulkan backend: force SPIRV output and disable bindless
    // (ResourceDescriptorHeap is DX12-only, not supported with -spirv)
    if (GetActiveRHIBackend() == RHIBackend::Vulkan) {
        m_impl->config.targetBackend = "vulkan_spirv";
        m_impl->config.bindless = false;
        UZ_LOG(IO, Info, "ShaderPipeline: auto-detected Vulkan backend, using SPIRV output (bindless disabled)");
    }

    UZ_LOG(IO, Info, "ShaderPipeline: initialized (backend='{}', cache='{}', bindless={})",
           m_impl->config.targetBackend, config.cacheDir, config.bindless ? "true" : "false");

    // Start shader hot-reload if enabled. Watches shader source directories
    // for changes (timestamp polling). Dirty shaders are recompiled lazily
    // on the next PollHotReload() call from the render loop.
    if (config.enableHotReload) {
        std::vector<std::filesystem::path> watchDirs;
        for (const auto& mount : m_impl->vfs.GetMounts()) {
            watchDirs.push_back(mount.realDirectory);
        }
        if (!watchDirs.empty()) {
            m_impl->hotReload.Start(watchDirs);
            UZ_LOG(IO, Info, "ShaderPipeline: hot-reload watching {} director{}",
                   watchDirs.size(), watchDirs.size() == 1 ? "y" : "ies");
        }
    }
}

void ShaderPipeline::Shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_impl) {
        m_impl->hotReload.Stop();
    }

    m_impl.reset();
    m_initialized = false;

    UZ_LOG(IO, Info, "ShaderPipeline: shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// VFS operations
// ─────────────────────────────────────────────────────────────────────────────

void ShaderPipeline::Mount(const std::string& virtualPath, const std::string& realPath) {
    UZ_PROFILE_FUNCTION();
    if (!m_initialized) {
        UZ_LOG(IO, Warning, "ShaderPipeline::Mount called before Init");
        return;
    }
    m_impl->vfs.Mount(virtualPath, realPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-shader compilation
// ─────────────────────────────────────────────────────────────────────────────

bool ShaderPipeline::CompileShader(const std::string& virtualPath) {
    UZ_PROFILE_FUNCTION();
    if (!m_initialized) {
        return false;
    }

    if (!m_impl->vfs.Exists(virtualPath)) {
        UZ_LOG(IO, Warning, "ShaderPipeline::CompileShader: '{}' not found", virtualPath);
        return false;
    }

    auto sourceInfo = m_impl->sourceManager.LoadSource(virtualPath);
    if (!sourceInfo) {
        return false;
    }

    m_impl->depGraph.SetDependencies(virtualPath, sourceInfo->includes);

    auto perms = ParsePermutations(sourceInfo->source);
    auto expanded = ExpandPermutations(perms);
    if (!expanded.Ok()) {
        return false;
    }

    bool allOk = true;
    for (const auto& variant : expanded.variants) {
        std::vector<std::string> defineStrings;
        for (const auto& [name, value] : variant) {
            defineStrings.push_back(name + "=" + value);
        }

        const auto identityHash = m_impl->sourceManager.ComputeIdentityHash(
            virtualPath, defineStrings, ShaderTarget::DXIL_SM6_6, 0);

        if (m_impl->cache.Contains(m_impl->config.targetBackend, identityHash)) {
            UZ_STAT_INCREMENT(RHI, CacheHits);
            continue;
        }
        UZ_STAT_INCREMENT(RHI, CacheMisses);

        ShaderCompileDesc desc;
        desc.source = sourceInfo->source;
        desc.sourceName = virtualPath;
        desc.defines = defineStrings;
        desc.stage = InferStageFromPath(virtualPath);
        ApplyBindlessConfig(desc, m_impl->config.bindless);
        desc.generateDebugInfo = m_impl->config.generateDebugInfo;

        const bool useDXC = RequiresDXC(desc.model);
        if ((useDXC && !m_impl->compiler) || (!useDXC && !m_impl->compilerFXC)) {
            allOk = false;
            continue;
        }

        auto compileResult = useDXC
            ? m_impl->compiler->Compile(desc)
            : m_impl->compilerFXC->Compile(desc);
        if (!compileResult.success) {
            allOk = false;
            continue;
        }
        UZ_STAT_INCREMENT(RHI, ShaderCompiles);

        m_impl->cache.Store(m_impl->config.targetBackend, identityHash,
                            compileResult.bytecode, virtualPath);
    }

    return allOk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bytecode loading
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> ShaderPipeline::LoadBytecode(
    const std::string& virtualPath,
    const std::string& permutationDefines)
{
    UZ_PROFILE_FUNCTION();
    if (!m_initialized) {
        return {};
    }

    if (!m_impl->vfs.Exists(virtualPath)) {
        return {};
    }

    // If dirty from hot-reload, recompile first
    if (m_impl->hotReload.IsDirty(virtualPath)) {
        m_impl->sourceManager.Invalidate(virtualPath);
        CompileShader(virtualPath);
        m_impl->hotReload.ClearDirty(virtualPath);
    }

    auto sourceInfo = m_impl->sourceManager.LoadSource(virtualPath);
    if (!sourceInfo) {
        return {};
    }

    // Parse the permutation defines string into individual defines
    std::vector<std::string> defineStrings;
    if (!permutationDefines.empty()) {
        std::istringstream ss(permutationDefines);
        std::string token;
        while (std::getline(ss, token, ';')) {
            if (!token.empty()) {
                defineStrings.push_back(token);
            }
        }
    }

    const auto identityHash = m_impl->sourceManager.ComputeIdentityHash(
        virtualPath, defineStrings, ShaderTarget::DXIL_SM6_6, 0);

    auto cached = m_impl->cache.Lookup(m_impl->config.targetBackend, identityHash);
    if (cached) {
        UZ_STAT_INCREMENT(RHI, CacheHits);
        return std::move(*cached);
    }
    UZ_STAT_INCREMENT(RHI, CacheMisses);

    // Not in cache — try to compile on-demand
    ShaderCompileDesc desc;
    desc.source = sourceInfo->source;
    desc.sourceName = virtualPath;
    desc.defines = defineStrings;
    desc.stage = InferStageFromPath(virtualPath);
    ApplyBindlessConfig(desc, m_impl->config.bindless);
    desc.generateDebugInfo = m_impl->config.generateDebugInfo;

    const bool useDXC = RequiresDXC(desc.model);
    if ((useDXC && !m_impl->compiler) || (!useDXC && !m_impl->compilerFXC)) {
        return {};
    }

    auto compileResult = useDXC
        ? m_impl->compiler->Compile(desc)
        : m_impl->compilerFXC->Compile(desc);
    if (!compileResult.success) {
        UZ_LOG(IO, Error, "ShaderPipeline::LoadBytecode: compile failed for '{}': {}",
               virtualPath, compileResult.errors);
        return {};
    }
    UZ_STAT_INCREMENT(RHI, ShaderCompiles);

    m_impl->cache.Store(m_impl->config.targetBackend, identityHash,
                        compileResult.bytecode, virtualPath);
    return compileResult.bytecode;
}

std::vector<uint8_t> ShaderPipeline::LoadBytecodeFromSource(
    const std::string& name,
    const std::string& hlslSource,
    const std::string& permutationDefines)
{
    UZ_PROFILE_FUNCTION();
    if (!m_initialized) {
        UZ_LOG(IO, Error, "ShaderPipeline::LoadBytecodeFromSource: not initialized (name='{}')", name);
        return {};
    }

    // Parse defines
    std::vector<std::string> defineStrings;
    if (!permutationDefines.empty()) {
        std::istringstream ss(permutationDefines);
        std::string token;
        while (std::getline(ss, token, ';')) {
            if (!token.empty()) {
                defineStrings.push_back(token);
            }
        }
    }

    // Compute identity hash from raw source text + defines + backend + bindless flag.
    // Mirrors ShaderSourceManager::ComputeIdentityHash but operates on raw source
    // instead of a VFS path, so inline shaders also benefit from the bytecode cache.
    const uint64_t identityHash = [&]() {
        XXH64_state_t* state = XXH64_createState();
        XXH64_reset(state, 0);

        // Source text
        XXH64_update(state, hlslSource.data(), hlslSource.size());

        // Defines (sorted for determinism, matching ComputeIdentityHash)
        std::vector<std::string> sortedDefines = defineStrings;
        std::sort(sortedDefines.begin(), sortedDefines.end());
        for (const auto& def : sortedDefines) {
            XXH64_update(state, def.data(), def.size());
        }

        // Target backend string
        const auto& backend = m_impl->config.targetBackend;
        XXH64_update(state, backend.data(), backend.size());

        // Bindless flag
        const uint8_t bindless = m_impl->config.bindless ? 1 : 0;
        XXH64_update(state, &bindless, sizeof(bindless));

        const uint64_t hash = XXH64_digest(state);
        XXH64_freeState(state);
        return hash;
    }();

    // Check the bytecode cache before compiling
    auto cached = m_impl->cache.Lookup(m_impl->config.targetBackend, identityHash);
    if (cached) {
        UZ_STAT_INCREMENT(RHI, CacheHits);
        return std::move(*cached);
    }
    UZ_STAT_INCREMENT(RHI, CacheMisses);

    ShaderCompileDesc desc;
    desc.source = hlslSource;
    desc.sourceName = name;
    desc.defines = defineStrings;
    desc.stage = InferStageFromPath(name);
    ApplyBindlessConfig(desc, m_impl->config.bindless);
    desc.generateDebugInfo = m_impl->config.generateDebugInfo;

    const bool useDXC = RequiresDXC(desc.model);
    if ((useDXC && !m_impl->compiler) || (!useDXC && !m_impl->compilerFXC)) {
        UZ_LOG(IO, Error, "ShaderPipeline::LoadBytecodeFromSource: no {} compiler set (name='{}')",
               useDXC ? "DXC" : "FXC", name);
        return {};
    }

    auto compileResult = useDXC
        ? m_impl->compiler->Compile(desc)
        : m_impl->compilerFXC->Compile(desc);
    if (!compileResult.success) {
        UZ_LOG(IO, Error, "ShaderPipeline::LoadBytecodeFromSource: compile failed for '{}': {}",
               name, compileResult.errors);
        return {};
    }
    UZ_STAT_INCREMENT(RHI, ShaderCompiles);

    m_impl->cache.Store(m_impl->config.targetBackend, identityHash,
                        compileResult.bytecode, name);
    return compileResult.bytecode;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hot-reload
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> ShaderPipeline::PollHotReload() {
    UZ_PROFILE_FUNCTION();
    if (!m_initialized) {
        return {};
    }

    auto dirtyPaths = m_impl->hotReload.Poll();
    if (dirtyPaths.empty()) {
        return {};
    }

    std::vector<std::string> recompiled;
    for (const auto& path : dirtyPaths) {
        m_impl->sourceManager.Invalidate(path);
        if (CompileShader(path)) {
            recompiled.push_back(path);
        }
        m_impl->hotReload.ClearDirty(path);
    }

    return recompiled;
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

ShaderVFS& ShaderPipeline::GetVFS() {
    return m_impl->vfs;
}

ShaderSourceManager& ShaderPipeline::GetSourceManager() {
    return m_impl->sourceManager;
}

void ShaderPipeline::SetCompiler(ShaderCompiler* compiler) {
    if (m_initialized) {
        m_impl->compiler = compiler;
    }
}

void ShaderPipeline::SetCompilerFXC(ShaderCompilerFXC* compiler) {
    if (m_initialized) {
        m_impl->compilerFXC = compiler;
    }
}

bool ShaderPipeline::IsInitialized() const noexcept {
    return m_initialized;
}

std::string ShaderPipeline::ComputeCacheKey(
    const std::string& virtualPath,
    const std::string& permutationDefines)
{
    return virtualPath + "|" + permutationDefines + "|" + m_impl->config.targetBackend;
}

} // namespace uz

#endif // _WIN32
