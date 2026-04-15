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

    /// Explicitly drops the per-frame buffer (e.g. when switching scenes).
    void Clear() noexcept { m_samples.clear(); }

private:
    std::vector<VfxDebugHudEmitterSample> m_samples;
};

} // namespace uz
