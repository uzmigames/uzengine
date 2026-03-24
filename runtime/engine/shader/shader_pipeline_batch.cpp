/// ShaderPipeline Batch Compilation
///
/// Implements CompileAll() — the parallel batch compilation path.
///
/// Phase 1 (sequential): enumerate shaders via VFS mounts, expand permutations,
///   check the bytecode cache, collect uncached variants as work items.
/// Phase 2 (parallel): compile uncached variants using per-worker DXC/FXC
///   instances via the job system. Falls back to sequential if the job system
///   is unavailable or compiler pool creation fails.
/// Phase 3 (sequential): store compiled bytecode in the cache, aggregate stats.
///
/// Windows-only: depends on DXC (ShaderCompiler) and FXC (ShaderCompilerFXC)
/// which are Windows-only shader compilers.

#ifdef _WIN32

#include "shader_pipeline_internal.h"

#include "profiler.h"
#include "shader_permutation.h"

#include "../job/job_system.h"
#include "../job/counter.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

namespace uz {

UZ_DECLARE_STAT(RHI, ShaderCompiles);
UZ_DECLARE_STAT(RHI, CacheHits);
UZ_DECLARE_STAT(RHI, CacheMisses);

// ─────────────────────────────────────────────────────────────────────────────
// File enumeration
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

// ─────────────────────────────────────────────────────────────────────────────
// Work item types for parallel compilation
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// CompileAll — parallel batch compilation
// ─────────────────────────────────────────────────────────────────────────────

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

} // namespace uz

#endif // _WIN32
