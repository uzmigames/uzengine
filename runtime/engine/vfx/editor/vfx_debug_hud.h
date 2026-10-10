#pragma once

/// VFX per-emitter debug HUD — Niagara-parity `fx.Niagara.DebugHud`.
///
/// Gives developers a per-emitter cost breakdown at a glance: alive
/// counts, CS / render GPU time, LOD level, and budget utilisation.
/// Without a VFX-specific HUD, perf debugging relies on GPU captures.
///
/// # Data flow
///
/// 1. The renderer calls `VfxDebugHudCollector::ReportEmitter(...)` once
///    per emitter per frame, passing the measurements gathered from the
///    `--gpu-profile` timestamp queries plus the SoA alive counts.
/// 2. At end-of-frame the overlay (or editor UI) calls `Aggregate()` to
///    collapse per-emitter samples into a HUD-friendly view: sorted by
///    total cost, top-N highlighted, totals reduced across every
///    emitter.
/// 3. The HUD widget (deferred to `phase7_7.17.1`) renders the resolved
///    `VfxDebugHudView` as a table overlay.
///
/// The collector clears its per-frame buffer after each `Aggregate`
/// call, so the same instance can be reused across frames without
/// accumulating stale data.
///
/// @ref UE5 FNiagaraDebugHud + FNiagaraDebugHudSettings
/// @ref phase7_7.17.1-vfx-debug-hud-widget-integration

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame emitter sample — what the renderer reports.
// ─────────────────────────────────────────────────────────────────────────────

/// Measurements for one emitter in one frame.
///
/// `aliveCount` / `maxParticles` drive the budget bar; GPU timings feed
/// the top-N highlight. LOD level is reported so the HUD can surface
/// when an emitter is running at reduced quality.
struct VfxDebugHudEmitterSample {
    std::string emitterName;     ///< Stable identifier — used as display key.
    uint32_t    aliveCount      = 0u;
    uint32_t    maxParticles    = 0u;
    float       simulationMs    = 0.0f;   ///< Sum of spawn + update CS time.
    float       renderMs        = 0.0f;   ///< Billboard / mesh / trail render time.
    uint32_t    lodLevel        = 0u;     ///< Matches VFXLodSettings indexing.
    bool        culled          = false;  ///< True when frustum-culled this frame.
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-module sample (phase 8.5.0) — timestamp-query bracket per module.
// ─────────────────────────────────────────────────────────────────────────────

/// VFX simulation / render stage a module participates in.
///
/// Matches `EmitterModuleStack`: spawn modules run once per new particle,
/// update modules run every frame on alive particles, and a single render
/// module handles the draw. Reporting the stage lets the HUD group rows by
/// phase even when module names repeat across stages.
enum class VfxDebugHudStage : uint8_t {
    Spawn  = 0,
    Update = 1,
    Render = 2,
};

/// Measurement for one module dispatch inside an emitter for one frame.
///
/// The GPU `--gpu-profile` timestamp-query harness wraps each module dispatch
/// with a labelled region; on resolve, that region's duration is pushed here
/// via `VfxDebugHudCollector::ReportModule`. Multiple samples for the same
/// `(emitterName, moduleName, stage)` tuple inside a single frame sum during
/// aggregation — useful for modules that dispatch more than once per tick.
struct VfxDebugHudModuleSample {
    std::string       emitterName;          ///< Matches the enclosing emitter sample.
    std::string       moduleName;           ///< `IVFXModule::GetName()` value.
    VfxDebugHudStage  stage = VfxDebugHudStage::Update;
    float             gpuMs = 0.0f;         ///< GPU time for the dispatch.
};

/// One row inside a module breakdown, resolved during `Aggregate`.
///
/// `percentOfEmitterTotal` is the share of this module's cost against the
/// emitter's `totalMs` — `0.0f` when the emitter has no recorded GPU time
/// (purely culled emitters). The aggregator sorts breakdown entries by
/// `gpuMs` descending so the slow module floats to the top.
struct VfxDebugHudModuleEntry {
    std::string       moduleName;
    VfxDebugHudStage  stage = VfxDebugHudStage::Update;
    float             gpuMs = 0.0f;
    float             percentOfEmitterTotal = 0.0f;
};

/// Ordered module breakdown attached to a HUD row.
///
/// Rendered when the row expands (widget integration in `phase7_7.17.1`).
/// `totalMs` is the sum of every entry's `gpuMs` — matches the outer row's
/// `simulationMs + renderMs` within timestamp-query noise margins.
struct VfxDebugHudModuleBreakdown {
    std::vector<VfxDebugHudModuleEntry> entries; ///< Sorted by gpuMs descending.
    float totalMs = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Aggregated HUD view — what the overlay reads.
// ─────────────────────────────────────────────────────────────────────────────

/// One row in the sorted HUD view. Adds derived fields (`totalMs`,
/// `budgetPercent`, `isTopN`) to the raw sample.
struct VfxDebugHudRow {
    std::string emitterName;
    uint32_t    aliveCount     = 0u;
    uint32_t    maxParticles   = 0u;
    float       simulationMs   = 0.0f;
    float       renderMs       = 0.0f;
    float       totalMs        = 0.0f;   ///< simulationMs + renderMs
    float       budgetPercent  = 0.0f;   ///< 100 * alive / max, 0 when max == 0
    uint32_t    lodLevel       = 0u;
    bool        culled         = false;
    bool        isTopN         = false;  ///< Among the top-N most expensive.

    /// Per-module breakdown populated when `ReportModule` samples matched
    /// this emitter. Empty when no module samples were reported.
    /// @ref UE5 FNiagaraPerfReportModule
    VfxDebugHudModuleBreakdown moduleBreakdown{};
};

/// Frame-wide totals + sorted per-row view.
struct VfxDebugHudView {
    /// Rows sorted by totalMs descending. Top-N rows have `isTopN == true`.
    std::vector<VfxDebugHudRow> rows;

    /// Sum of alive particles across every reported emitter this frame.
    uint32_t totalAliveParticles = 0u;

    /// Sum of total GPU time across every reported emitter.
    float totalGpuMs = 0.0f;

    /// Total configured budget across every reported emitter's
    /// `maxParticles`. 0 when no emitters were reported.
    uint32_t totalBudget = 0u;

    /// Number of emitters flagged `culled == true` this frame. Useful
    /// as a single "how much are we saving?" metric.
    uint32_t culledCount = 0u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Collector — writes per-frame, reads once at end-of-frame.
// ─────────────────────────────────────────────────────────────────────────────

/// Collects emitter samples during a frame + produces a sorted view
/// at end-of-frame.
class VfxDebugHudCollector {
public:
    /// Reports one emitter's measurements for this frame. Called by the
    /// renderer / scene-vfx-sync path; typically once per emitter.
    void ReportEmitter(const VfxDebugHudEmitterSample& sample);

    /// Convenience overload that appends an in-place constructed sample.
    void ReportEmitter(std::string emitterName,
                       uint32_t aliveCount,
                       uint32_t maxParticles,
                       float simulationMs,
                       float renderMs,
                       uint32_t lodLevel,
                       bool culled);

    /// Reports one module's GPU time inside an emitter for this frame
    /// (phase 8.5.0). Matching samples (same emitter + module + stage)
    /// accumulate during aggregation so modules that dispatch multiple
    /// times per frame still appear as a single breakdown entry.
    ///
    /// Call sites: the GPU timestamp-query harness at each module dispatch
    /// boundary (spawn CS, update CS, render draw). Safe to call before or
    /// after the matching `ReportEmitter` — the aggregator joins by name.
    ///
    /// @ref UE5 FNiagaraPerfReportModule
    void ReportModule(const VfxDebugHudModuleSample& sample);

    /// Convenience overload mirroring `ReportEmitter`'s field-list form.
    void ReportModule(std::string emitterName,
                      std::string moduleName,
                      VfxDebugHudStage stage,
                      float gpuMs);

    /// Collapses the per-frame samples into a sorted view and clears the
    /// internal buffer. Returns a view with rows sorted by total GPU ms
    /// descending; the first `topNCount` rows get `isTopN == true`.
    ///
    /// `topNCount == 0` disables top-N highlighting but still sorts.
    [[nodiscard]] VfxDebugHudView Aggregate(uint32_t topNCount = 3u);

    /// Number of samples currently buffered for this frame.
    [[nodiscard]] uint32_t PendingSampleCount() const noexcept {
        return static_cast<uint32_t>(m_samples.size());
    }

    /// Number of module samples buffered for this frame.
    [[nodiscard]] uint32_t PendingModuleSampleCount() const noexcept {
        return static_cast<uint32_t>(m_moduleSamples.size());
    }

    /// Explicitly drops the per-frame buffer (e.g. when switching scenes).
    void Clear() noexcept {
        m_samples.clear();
        m_moduleSamples.clear();
    }

private:
    std::vector<VfxDebugHudEmitterSample> m_samples;
    std::vector<VfxDebugHudModuleSample>  m_moduleSamples;
};

// ─────────────────────────────────────────────────────────────────────────────
// Stage-name helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a static-storage string for `stage` — used by the HUD widget
/// and by docs / logs. Never returns null.
[[nodiscard]] const char* VfxDebugHudStageName(VfxDebugHudStage stage) noexcept;

} // namespace uz
