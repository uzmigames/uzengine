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

#include "shader_pipeline.h"

#include "shader_cache.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "shader_dependency_graph.h"
#include "shader_hot_reload.h"
#include "shader_permutation.h"
#include "shader_source_manager.h"
#include "shader_vfs.h"
#include "log.h"
#include "profiler.h"
#include "../job/job_system.h"
#include "../job/counter.h"
#include "../rhi/rhi_backend.h"
#include "../rhi/rhi_config.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <sstream>

namespace uz {

UZ_DEFINE_STAT(RHI, ShaderCompiles);
UZ_DEFINE_STAT(RHI, CacheHits);
UZ_DEFINE_STAT(RHI, CacheMisses);

// ─────────────────────────────────────────────────────────────────────────────
// Stage inference from filename convention
// ─────────────────────────────────────────────────────────────────────────────

/// Infers the shader stage from the virtual path using suffix conventions:
/// `_vs.hlsl` → Vertex, `_ps.hlsl` → Pixel, `_cs.hlsl` → Compute, etc.
static ShaderStageCompile InferStageFromPath(const std::string& path) {
    auto stem = std::filesystem::path(path).stem().string();
    if (stem.size() >= 3) {
        auto suffix = stem.substr(stem.size() - 3);
        if (suffix == "_vs") return ShaderStageCompile::Vertex;
        if (suffix == "_ps") return ShaderStageCompile::Pixel;
        if (suffix == "_cs") return ShaderStageCompile::Compute;
        if (suffix == "_gs") return ShaderStageCompile::Geometry;
        if (suffix == "_hs") return ShaderStageCompile::Hull;
        if (suffix == "_ds") return ShaderStageCompile::Domain;
        if (suffix == "_as") return ShaderStageCompile::Amplification;
        if (suffix == "_ms") return ShaderStageCompile::Mesh;
    }
    // Fallback: check for common naming patterns
    if (stem.find("vert") != std::string::npos) return ShaderStageCompile::Vertex;
    if (stem.find("pixel") != std::string::npos || stem.find("frag") != std::string::npos)
        return ShaderStageCompile::Pixel;
    if (stem.find("comp") != std::string::npos) return ShaderStageCompile::Compute;
    return ShaderStageCompile::Vertex; // Default
}

/// Maps the device ShaderModel to the compiler ShaderModelCompile enum.
static ShaderModelCompile DeviceSMToCompileSM(ShaderModel sm) {
    switch (sm) {
        case ShaderModel::SM5_0: return ShaderModelCompile::SM5_0;
        case ShaderModel::SM5_1: return ShaderModelCompile::SM5_1;
        case ShaderModel::SM6_0: return ShaderModelCompile::SM6_0;
        case ShaderModel::SM6_1: return ShaderModelCompile::SM6_1;
        case ShaderModel::SM6_2: return ShaderModelCompile::SM6_2;
        case ShaderModel::SM6_3: return ShaderModelCompile::SM6_3;
        case ShaderModel::SM6_4: return ShaderModelCompile::SM6_4;
        case ShaderModel::SM6_5: return ShaderModelCompile::SM6_5;
        case ShaderModel::SM6_6: return ShaderModelCompile::SM6_6;
        case ShaderModel::SM6_7: return ShaderModelCompile::SM6_7;
        case ShaderModel::SM6_8: return ShaderModelCompile::SM6_8;
        case ShaderModel::SM6_9: return ShaderModelCompile::SM6_9;
    }
    return ShaderModelCompile::SM6_6;
}

/// Applies bindless/SM5 configuration and backend-specific output format.
///
/// When bindless is true: injects UZ_BINDLESS=1 define, targets the device's
/// negotiated shader model (SM6.6+).
/// When bindless is false: targets SM5_0 (FXC) for DX backends, SM6_0 (DXC) for Vulkan.
/// When Vulkan backend is active: forces SPIRV output and always uses DXC
/// (FXC cannot produce SPIR-V).
static void ApplyBindlessConfig(ShaderCompileDesc& desc, bool bindless) {
    const bool isVulkan = (GetActiveRHIBackend() == RHIBackend::Vulkan);

    if (bindless) {
        desc.defines.push_back("UZ_BINDLESS=1");
        // Use the device's actual shader model — compile with the highest SM available
        const ShaderModel deviceSM = GetActiveShaderModel();
        desc.model = DeviceSMToCompileSM(deviceSM);
        // Minimum SM6.6 for bindless
        if (desc.model < ShaderModelCompile::SM6_6) {
            desc.model = ShaderModelCompile::SM6_6;
        }
    } else {
        // Vulkan requires DXC (FXC cannot emit SPIR-V), so use SM6_0 minimum
        desc.model = isVulkan ? ShaderModelCompile::SM6_0 : ShaderModelCompile::SM5_0;
    }

    if (isVulkan) {
        desc.output = ShaderOutputFormat::SPIRV;
    }
}

/// Returns true if the given shader model requires DXC (SM 6.0+).
/// SM 5.0/5.1 use FXC instead.
static bool RequiresDXC(ShaderModelCompile model) {
    return model >= ShaderModelCompile::SM6_0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PIMPL — holds all subsystems with correct initialization order
// ─────────────────────────────────────────────────────────────────────────────

struct ShaderPipeline::Impl {
    ShaderVFS vfs;
    ShaderSourceManager sourceManager;
    ShaderDependencyGraph depGraph;
    ShaderCache cache;
    ShaderHotReload hotReload;
    ShaderCompiler* compiler = nullptr;       ///< DXC — SM 6.0+
    ShaderCompilerFXC* compilerFXC = nullptr; ///< FXC — SM 5.0
    Config config;

    /// VFS must exist before sourceManager is constructed.
    explicit Impl(const Config& cfg)
        : sourceManager(vfs)
        , cache(cfg.cacheDir)
        , config(cfg)
    {
        hotReload.SetDependencyGraph(&depGraph);
    }
};

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
// Compilation
// ─────────────────────────────────────────────────────────────────────────────

/// Enumerates .hlsl files from a real directory, returning virtual paths.
static std::vector<std::string> EnumerateShaderFiles(
    const std::string& virtualPrefix,
    const std::string& realDir)
{
    std::vector<std::string> result;
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(realDir, ec) || !fs::is_directory(realDir, ec)) {
        return result;
    }

    for (const auto& entry : fs::recursive_directory_iterator(realDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const auto ext = entry.path().extension().string();
        if (ext != ".hlsl") continue;

        // Build virtual path from relative path within the mount
        auto relPath = fs::relative(entry.path(), realDir, ec).generic_string();
        if (ec) continue;

        std::string vPath = virtualPrefix;
        if (!vPath.empty() && vPath.back() != '/') {
            vPath += '/';
        }
        vPath += relPath;
        result.push_back(std::move(vPath));
    }

    return result;
}

/// Work item for parallel shader compilation.
///
/// Created during Phase 1 (sequential collection) for each variant that is
/// not found in cache. Consumed during Phase 2 (parallel compilation).
struct CompileWorkItem {
    ShaderCompileDesc desc;          ///< Full compilation descriptor
    std::string shaderPath;          ///< Virtual path for error reporting
    std::string defines;             ///< Formatted defines string for error messages
    uint64_t identityHash = 0;       ///< Cache key for storing the result
    bool useDXC = true;              ///< True for DXC (SM 6.0+), false for FXC (SM 5.0)
};

/// Result of a single parallel compilation work item.
///
/// Pre-allocated in a vector indexed by work item index, so each worker
/// writes to its own slot without synchronization.
struct CompileWorkResult {
    bool success = false;
    std::vector<uint8_t> bytecode;
    std::string errorMessage;        ///< Non-empty on failure
};

ShaderPipeline::CompileAllResult ShaderPipeline::CompileAll() {
    UZ_PROFILE_FUNCTION();
    CompileAllResult result;

    if (!m_initialized) {
        result.errors.push_back("Pipeline not initialized");
        return result;
    }

    // ── Phase 1: Collect work items (sequential) ────────────────────────

    std::vector<std::string> allShaders;
    {
        UZ_PROFILE_SCOPE("CompileAll_Enumerate");
        for (const auto& mount : m_impl->vfs.GetMounts()) {
            auto files = EnumerateShaderFiles(mount.virtualPrefix, mount.realDirectory);
            allShaders.insert(allShaders.end(),
                              std::make_move_iterator(files.begin()),
                              std::make_move_iterator(files.end()));
        }
    }

    std::vector<CompileWorkItem> workItems;
    {
        UZ_PROFILE_SCOPE("CompileAll_CollectWorkItems");

        for (const auto& shaderPath : allShaders) {
            auto sourceInfo = m_impl->sourceManager.LoadSource(shaderPath);
            if (!sourceInfo) {
                result.failed++;
                result.total++;
                result.errors.push_back("Failed to load: " + shaderPath);
                continue;
            }

            m_impl->depGraph.SetDependencies(shaderPath, sourceInfo->includes);

            auto perms = ParsePermutations(sourceInfo->source);
            auto expanded = ExpandPermutations(perms);
            if (!expanded.Ok()) {
                result.failed++;
                result.total++;
                result.errors.push_back(shaderPath + ": " + expanded.error);
                continue;
            }

            for (const auto& variant : expanded.variants) {
                result.total++;
                const auto defines = FormatDefines(variant);

                std::vector<std::string> defineStrings;
                defineStrings.reserve(variant.size());
                for (const auto& [name, value] : variant) {
                    defineStrings.push_back(name + "=" + value);
                }

                const auto identityHash = m_impl->sourceManager.ComputeIdentityHash(
                    shaderPath, defineStrings,
                    ShaderTarget::DXIL_SM6_6, 0);

                if (m_impl->cache.Contains(m_impl->config.targetBackend, identityHash)) {
                    UZ_STAT_INCREMENT(RHI, CacheHits);
                    result.cached++;
                    continue;
                }
                UZ_STAT_INCREMENT(RHI, CacheMisses);

                ShaderCompileDesc desc;
                desc.source = sourceInfo->source;
                desc.sourceName = shaderPath;
                desc.defines = defineStrings;
                desc.stage = InferStageFromPath(shaderPath);
                ApplyBindlessConfig(desc, m_impl->config.bindless);
                desc.generateDebugInfo = m_impl->config.generateDebugInfo;

                const bool useDXC = RequiresDXC(desc.model);
                if (useDXC && !m_impl->compiler) {
                    result.failed++;
                    result.errors.push_back("No DXC compiler set for: " + shaderPath +
                                            " [" + defines + "]");
                    continue;
                }
                if (!useDXC && !m_impl->compilerFXC) {
                    result.failed++;
                    result.errors.push_back("No FXC compiler set for: " + shaderPath +
                                            " [" + defines + "]");
                    continue;
                }

                CompileWorkItem item;
                item.desc = std::move(desc);
                item.shaderPath = shaderPath;
                item.defines = defines;
                item.identityHash = identityHash;
                item.useDXC = useDXC;
                workItems.push_back(std::move(item));
            }
        }
    }

    // Nothing to compile — all cached or failed during collection
    if (workItems.empty()) {
        UZ_LOG(IO, Info,
               "ShaderPipeline::CompileAll: total={}, cached={}, compiled={}, failed={}",
               result.total, result.cached, result.compiled, result.failed);
        return result;
    }

    // ── Phase 2: Compile (parallel if job system available) ─────────────

    std::vector<CompileWorkResult> workResults(workItems.size());

    // Try parallel compilation via the job system. Falls back to sequential
    // if the job system is not initialized or compiler pool creation fails.
    bool compiledInParallel = false;

    auto* jobSystem = JobSystem::Get();
    if (jobSystem != nullptr && workItems.size() > 1) {
        UZ_PROFILE_SCOPE("CompileAll_ParallelCompile");

        const uint32_t threadCount = jobSystem->GetWorkerCount() + 1;
        const bool needDXC = m_impl->compiler != nullptr;
        const bool needFXC = m_impl->compilerFXC != nullptr;

        // Create per-worker compiler instances — each owns independent COM objects
        std::vector<std::unique_ptr<ShaderCompiler>> dxcPool;
        std::vector<std::unique_ptr<ShaderCompilerFXC>> fxcPool;
        bool poolInitOk = true;

        if (needDXC) {
            dxcPool.resize(threadCount);
            for (uint32_t i = 0; i < threadCount; ++i) {
                dxcPool[i] = std::make_unique<ShaderCompiler>();
                if (!dxcPool[i]->Init()) {
                    UZ_LOG(IO, Warning,
                           "ShaderPipeline: failed to init DXC pool compiler {}/{}, falling back to sequential",
                           i, threadCount);
                    poolInitOk = false;
                    break;
                }
            }
        }

        if (poolInitOk && needFXC) {
            fxcPool.resize(threadCount);
            for (uint32_t i = 0; i < threadCount; ++i) {
                fxcPool[i] = std::make_unique<ShaderCompilerFXC>();
                if (!fxcPool[i]->Init()) {
                    UZ_LOG(IO, Warning,
                           "ShaderPipeline: failed to init FXC pool compiler {}/{}, falling back to sequential",
                           i, threadCount);
                    poolInitOk = false;
                    break;
                }
            }
        }

        if (poolInitOk) {
            UZ_LOG(IO, Info, "ShaderPipeline: compiling {} variants across {} threads",
                   workItems.size(), threadCount);

            // Thread ID → pool index map. Main thread is always index 0.
            // Mutex protects the map; contention is minimal since each thread
            // only registers once on its first work item.
            std::mutex indexMutex;
            std::vector<std::pair<std::thread::id, uint32_t>> threadIndexMap;
            threadIndexMap.reserve(threadCount);
            threadIndexMap.push_back({std::this_thread::get_id(), 0});
            std::atomic<uint32_t> nextIndex{1};

            // Base pointer for computing result index via pointer arithmetic.
            // Stable because ParallelFor operates on the contiguous workItems array.
            CompileWorkItem* const workBase = workItems.data();

            jobSystem->ParallelFor(
                workItems.data(), workItems.size(),
                [&workResults, &dxcPool, &fxcPool,
                 &indexMutex, &threadIndexMap, &nextIndex, threadCount,
                 workBase](CompileWorkItem& item) {

                    // Resolve this thread's compiler pool index
                    const auto tid = std::this_thread::get_id();
                    uint32_t poolIdx = 0;
                    {
                        std::lock_guard<std::mutex> lock(indexMutex);
                        bool found = false;
                        for (const auto& [id, idx] : threadIndexMap) {
                            if (id == tid) {
                                poolIdx = idx;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            poolIdx = nextIndex.fetch_add(1, std::memory_order_relaxed);
                            if (poolIdx >= threadCount) {
                                poolIdx = threadCount - 1;
                            }
                            threadIndexMap.push_back({tid, poolIdx});
                        }
                    }

                    const size_t resultIdx = static_cast<size_t>(&item - workBase);

                    ShaderCompileResult compileResult;
                    if (item.useDXC) {
                        compileResult = dxcPool[poolIdx]->Compile(item.desc);
                    } else {
                        compileResult = fxcPool[poolIdx]->Compile(item.desc);
                    }

                    auto& wr = workResults[resultIdx];
                    wr.success = compileResult.success;
                    if (compileResult.success) {
                        wr.bytecode = std::move(compileResult.bytecode);
                    } else {
                        wr.errorMessage = item.shaderPath + " [" + item.defines + "]: " +
                                          compileResult.errors;
                    }
                });

            compiledInParallel = true;
        }
    }

    // Sequential fallback — no job system, single item, or pool init failed
    if (!compiledInParallel) {
        UZ_PROFILE_SCOPE("CompileAll_SequentialCompile");

        for (size_t i = 0; i < workItems.size(); ++i) {
            auto& item = workItems[i];
            auto compileResult = item.useDXC
                ? m_impl->compiler->Compile(item.desc)
                : m_impl->compilerFXC->Compile(item.desc);

            auto& wr = workResults[i];
            wr.success = compileResult.success;
            if (compileResult.success) {
                wr.bytecode = std::move(compileResult.bytecode);
            } else {
                wr.errorMessage = item.shaderPath + " [" + item.defines + "]: " +
                                  compileResult.errors;
            }
        }
    }

    // ── Phase 3: Store results in cache (sequential) ────────────────────
    {
        UZ_PROFILE_SCOPE("CompileAll_StoreResults");

        for (size_t i = 0; i < workItems.size(); ++i) {
            const auto& wr = workResults[i];
            if (wr.success) {
                UZ_STAT_INCREMENT(RHI, ShaderCompiles);
                m_impl->cache.Store(m_impl->config.targetBackend,
                                    workItems[i].identityHash,
                                    wr.bytecode, workItems[i].shaderPath);
                result.compiled++;
            } else {
                result.failed++;
                result.errors.push_back(wr.errorMessage);
            }
        }
    }

    UZ_LOG(IO, Info,
           "ShaderPipeline::CompileAll: total={}, cached={}, compiled={}, failed={}{}",
           result.total, result.cached, result.compiled, result.failed,
           compiledInParallel ? " (parallel)" : " (sequential)");

    return result;
}

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
