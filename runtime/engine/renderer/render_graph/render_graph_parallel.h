#pragma once

/// Render Graph Parallel Recording — Identifies independent pass batches and records them concurrently.
///
/// This utility analyzes the render graph's compiled pass order and groups passes
/// that have no resource dependencies into parallel batches. Each batch can be
/// recorded concurrently using `ParallelCommandRecorder`.
///
/// # Design
/// The render graph's topological sort already establishes a valid execution order.
/// Within that order, consecutive passes that share no resource dependencies can
/// be recorded in parallel. This module identifies those batches and optionally
/// drives parallel recording via the RHI's `ParallelCommandRecorder`.
///
/// # Thread Safety
/// Not thread-safe. All methods must be called from the main render thread.
/// The `ParallelCommandRecorder` handles internal thread safety for command list
/// acquisition/release.
///
/// # Integration
/// This is a standalone utility that can be used alongside `RgBuilder::Execute()`.
/// To enable parallel recording, call `SetParallelRecordingEnabled(true)` on
/// an `RgParallelExecutor` before calling its `Execute()` method.

#include <cstdint>
#include <vector>

namespace uz {
class IRHIDevice;
class IRHICommandList;
class ParallelCommandRecorder;
} // namespace uz

namespace uz::rg {

class RgBuilder;
struct RgPass;

// ─────────────────────────────────────────────────────────────────────────────
// ParallelRecordingStats
// ─────────────────────────────────────────────────────────────────────────────

/// Statistics about parallel pass recording for the current frame.
///
/// Populated by `RgParallelExecutor::Execute()` and queryable via
/// `GetParallelRecordingStats()`.
struct ParallelRecordingStats {
    uint32_t totalPasses = 0;           ///< Total non-culled passes executed.
    uint32_t parallelBatches = 0;       ///< Number of batches (groups of independent passes).
    uint32_t maxBatchSize = 0;          ///< Largest batch size encountered.
    float parallelizationRatio = 0.0f;  ///< Fraction of passes in batches of size > 1.
};

// ─────────────────────────────────────────────────────────────────────────────
// ParallelBatch — a group of independent passes
// ─────────────────────────────────────────────────────────────────────────────

/// A batch of passes that can be recorded concurrently.
///
/// All passes in a batch have no resource dependencies on each other —
/// they read/write disjoint resource sets (within the batch).
struct ParallelBatch {
    std::vector<uint32_t> passIndices; ///< Indices into the pass array.
};

// ─────────────────────────────────────────────────────────────────────────────
// RgParallelExecutor
// ─────────────────────────────────────────────────────────────────────────────

/// Executes a compiled render graph with optional parallel pass recording.
///
/// When parallel recording is enabled and a batch contains more than one pass,
/// the executor uses `ParallelCommandRecorder` to record those passes on
/// separate threads. Otherwise, passes are recorded sequentially on the
/// provided command list.
///
/// # Usage
/// ```cpp
/// RgParallelExecutor executor;
/// executor.SetParallelRecordingEnabled(true);
/// executor.Execute(builder, cmdList, device, &recorder);
/// auto stats = executor.GetParallelRecordingStats();
/// ```
///
/// # Lifetime
/// Per-frame, like `RgBuilder`. Create a new executor each frame or reuse
/// and call `Execute()` once per compiled graph.
class RgParallelExecutor {
public:
    /// Enables or disables parallel recording of independent passes.
    ///
    /// When disabled (default), all passes are recorded sequentially on
    /// the provided command list, identical to `RgBuilder::Execute()`.
    ///
    /// # Arguments
    /// * `enabled` — `true` to enable parallel recording.
    void SetParallelRecordingEnabled(bool enabled) { m_parallelEnabled = enabled; }

    /// Returns whether parallel recording is enabled.
    [[nodiscard]] bool IsParallelRecordingEnabled() const { return m_parallelEnabled; }

    /// Analyzes the compiled render graph and groups independent passes into batches.
    ///
    /// Must be called after `RgBuilder::Compile()` and before `Execute()`.
    /// Populates internal batch data used by `Execute()`.
    ///
    /// # Arguments
    /// * `builder` — A compiled render graph builder.
    ///
    /// # Returns
    /// The batches identified. Also stored internally for `Execute()`.
    [[nodiscard]] const std::vector<ParallelBatch>& BuildBatches(const RgBuilder& builder);

    /// Executes the compiled render graph, using parallel recording for batches.
    ///
    /// If parallel recording is disabled or no `ParallelCommandRecorder` is provided,
    /// falls back to sequential recording on `cmdList`.
    ///
    /// Barriers are always emitted on the primary `cmdList` before each batch.
    ///
    /// # Arguments
    /// * `builder` — A compiled render graph builder. Will be consumed (physical resources acquired/released).
    /// * `cmdList` — Primary command list for sequential recording and barrier emission.
    /// * `recorder` — Optional parallel command recorder. Required for parallel recording.
    ///   Pass `nullptr` to force sequential execution.
    void Execute(RgBuilder& builder, IRHICommandList* cmdList,
                 ParallelCommandRecorder* recorder = nullptr);

    /// Returns statistics from the most recent `Execute()` call.
    [[nodiscard]] const ParallelRecordingStats& GetParallelRecordingStats() const { return m_stats; }

private:
    /// Checks whether two passes share any resource dependencies.
    ///
    /// Returns `true` if the passes access any common resource where at least
    /// one access is a write (WAR, RAW, or WAW hazard).
    static bool HasResourceConflict(const RgPass& a, const RgPass& b);

    bool m_parallelEnabled = false;
    std::vector<ParallelBatch> m_batches;
    ParallelRecordingStats m_stats{};
};

} // namespace uz::rg
