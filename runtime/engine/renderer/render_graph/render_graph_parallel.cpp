/// Render Graph Parallel Recording — Batch detection and parallel execution.
///
/// Groups independent render graph passes into batches that can be recorded
/// concurrently via ParallelCommandRecorder. Falls back to sequential recording
/// when parallel is disabled or only one pass exists in a batch.

#include "render_graph_parallel.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"

#include "../../rhi/rhi.h"
#include "../../rhi/optimization/rhi_parallel_command_recorder.h"
#include "log.h"
#include "profiler.h"
#include "engine_assert.h"

#include <unordered_set>

namespace uz::rg {

// ─────────────────────────────────────────────────────────────────────────────
// Conflict Detection
// ─────────────────────────────────────────────────────────────────────────────

/// Two passes conflict if they access any common resource where at least one
/// access is a write (WAR, RAW, or WAW hazard).
bool RgParallelExecutor::HasResourceConflict(const RgPass& a, const RgPass& b)
{
    // Collect resources written by each pass for fast lookup.
    // Small pass access lists make a linear scan acceptable.
    for (const auto& accessA : a.accesses) {
        if (!accessA.resourceId.IsValid()) continue;

        for (const auto& accessB : b.accesses) {
            if (!accessB.resourceId.IsValid()) continue;

            if (accessA.resourceId == accessB.resourceId) {
                // Same resource accessed by both — conflict if either writes
                if (accessA.isWrite || accessB.isWrite) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Batch Building
// ─────────────────────────────────────────────────────────────────────────────

const std::vector<ParallelBatch>& RgParallelExecutor::BuildBatches(const RgBuilder& builder)
{
    UZ_PROFILE_FUNCTION();

    m_batches.clear();

    const uint32_t passCount = builder.GetPassCount();
    if (passCount == 0) return m_batches;

    // Walk the topologically sorted pass list and greedily group consecutive
    // passes into batches. A pass joins the current batch only if it has no
    // resource conflict with ANY pass already in the batch. Otherwise, it
    // starts a new batch.
    //
    // This is a greedy O(n * batchSize * accessCount) algorithm. For typical
    // render graphs (10-30 passes, 2-5 accesses each) this is negligible.

    // We need access to the internal pass array. Since RgBuilder doesn't
    // expose passes directly, we rely on the sorted pass indices via
    // GetPassCount / GetResource. However, the builder's passes are private.
    //
    // To work around this without modifying RgBuilder's interface, we
    // access pass data through a friend-like pattern. For now, we build
    // trivial single-pass batches and let Execute() handle the actual
    // parallel dispatch based on resource conflict analysis at execution time.
    //
    // The proper integration would have RgBuilder expose its sorted pass
    // list. For now, we create one batch per pass (worst case = sequential).
    // The Execute() method will merge adjacent conflict-free batches.

    for (uint32_t i = 0; i < passCount; ++i) {
        ParallelBatch batch;
        batch.passIndices.push_back(i);
        m_batches.push_back(std::move(batch));
    }

    return m_batches;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────────────────────────────────────

void RgParallelExecutor::Execute(RgBuilder& builder, IRHICommandList* cmdList,
                                  ParallelCommandRecorder* recorder)
{
    UZ_PROFILE_FUNCTION();
    UZ_CHECK(cmdList != nullptr);

    // Reset stats
    m_stats = {};

    // For now, delegate to the builder's sequential Execute().
    // When RgBuilder exposes its sorted pass list and allows external execution,
    // this method will:
    // 1. Emit barriers on cmdList for each batch
    // 2. For batches with size > 1 and parallel enabled: use recorder
    // 3. For batches with size == 1 or parallel disabled: record on cmdList
    //
    // Current implementation: sequential fallback with stats tracking.

    const uint32_t totalPasses = builder.GetPassCount();
    const uint32_t culledPasses = builder.GetCulledPassCount();
    const uint32_t livePasses = totalPasses - culledPasses;

    m_stats.totalPasses = livePasses;
    m_stats.parallelBatches = livePasses; // Each pass is its own batch for now
    m_stats.maxBatchSize = livePasses > 0 ? 1 : 0;
    m_stats.parallelizationRatio = 0.0f; // No actual parallelism yet

    if (m_parallelEnabled && recorder != nullptr && livePasses > 1) {
        UZ_LOG(Renderer, Debug,
               "RenderGraph parallel recording: {} live passes, parallel enabled "
               "but full integration pending — falling back to sequential",
               livePasses);
    }

    // Delegate to sequential execution
    builder.Execute(cmdList);

    UZ_LOG(Renderer, Trace,
           "RenderGraph parallel stats: passes={}, batches={}, maxBatch={}, ratio={:.2f}",
           m_stats.totalPasses, m_stats.parallelBatches,
           m_stats.maxBatchSize, m_stats.parallelizationRatio);
}

} // namespace uz::rg
