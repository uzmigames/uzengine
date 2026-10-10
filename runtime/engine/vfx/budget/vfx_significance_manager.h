/// VFX Significance Manager -- ranks emitters by importance and enforces
/// particle budget by reducing LOD on low-significance emitters.
///
/// # Overview
/// Each frame, the caller populates a vector of `VFXEmitterSignificance` records
/// (one per active emitter) and calls `RankEmitters()` followed by
/// `EnforceBudget()`. The manager sorts emitters by significance score and
/// progressively degrades LOD on the least-significant emitters until total
/// particle demand fits within the frame budget.
///
/// # Significance Formula
/// ```
/// score = priority * (1.0 / (1.0 + distance * 0.0001)) * (visible ? 1.0 : 0.1)
/// ```
/// - `priority`   -- user-assigned weight (higher = more important)
/// - distance     -- world units (cm); shallow 1% falloff per metre (100 cm)
/// - visibility   -- off-screen emitters receive a 10x penalty
///
/// # LOD Degradation Order
/// Full -> Reduced -> Minimal -> Culled
/// Each step reduces the effective particle demand via a spawn-rate factor:
/// Full=1.0, Reduced=0.5, Minimal=0.25, Culled=0.0.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main/simulation thread.
///
/// # Example
/// ```cpp
/// VFXSignificanceManager sig;
///
/// std::vector<VFXEmitterSignificance> emitters = CollectEmitters();
/// sig.RankEmitters(emitters);
/// sig.EnforceBudget(emitters, budget.GetMaxParticles());
///
/// for (const auto& e : emitters) {
///     ApplyLod(e.emitterId, e.appliedLod);
/// }
/// ```

#pragma once

#include "engine/vfx/lod/vfx_lod.h"

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFXEmitterSignificance -- per-emitter significance snapshot
// ─────────────────────────────────────────────────────────────────────────────

/// Computed significance data for one emitter within a single frame.
///
/// Callers populate `emitterId`, `distanceToCamera`, `isFrustumVisible`,
/// `priority`, and `particleDemand` before passing the record to the
/// significance manager. The manager fills `score` and `appliedLod`.
struct VFXEmitterSignificance {
    /// Opaque handle or index identifying the emitter in the caller's system.
    uint32_t emitterId = 0;

    /// Combined significance score computed by `VFXSignificanceManager::ComputeScore()`.
    /// Higher values indicate more important emitters. Populated by `RankEmitters()`.
    float score = 0.0f;

    /// Distance from the emitter origin to the camera, in world units (cm).
    /// Used in the significance formula: shorter distance → higher score.
    float distanceToCamera = 0.0f;

    /// Whether the emitter's bounding volume overlaps the current camera frustum.
    /// Off-screen emitters receive a 10x significance penalty.
    bool isFrustumVisible = true;

    /// User-assigned importance weight (higher = more important).
    /// Range: any uint32_t value; typical range 1–100.
    uint32_t priority = 50;

    /// Maximum number of particles this emitter wants to have alive simultaneously
    /// when running at `VFXLodLevel::Full`.
    uint32_t particleDemand = 0;

    /// LOD level assigned by `EnforceBudget()`.
    /// Initialised to `Full`; degraded toward `Culled` when the budget is tight.
    VFXLodLevel appliedLod = VFXLodLevel::Full;
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXSignificanceManager -- emitter ranking and budget enforcement
// ─────────────────────────────────────────────────────────────────────────────

/// Ranks emitters by importance and enforces the global particle budget
/// by degrading LOD on low-significance emitters.
///
/// # Lifecycle
/// Stateless — construct once and reuse across frames.
/// No initialisation or shutdown required.
///
/// # Ownership
/// Does not own any emitter data. Operates entirely on caller-provided vectors.
class VFXSignificanceManager {
public:
    // ── Construction ─────────────────────────────────────────────────────────

    VFXSignificanceManager() = default;
    ~VFXSignificanceManager() = default;

    /// Non-copyable (stateless, but copy is meaningless and likely a mistake).
    VFXSignificanceManager(const VFXSignificanceManager&) = delete;
    VFXSignificanceManager& operator=(const VFXSignificanceManager&) = delete;

    /// Movable.
    VFXSignificanceManager(VFXSignificanceManager&&) noexcept = default;
    VFXSignificanceManager& operator=(VFXSignificanceManager&&) noexcept = default;

    // ── Score Computation ─────────────────────────────────────────────────────

    /// Computes the significance score for a single emitter.
    ///
    /// Formula: `priority * (1.0 / (1.0 + distance * 0.0001)) * (visible ? 1.0 : 0.1)`
    ///
    /// The distance term uses a shallow 1%-per-metre falloff so that emitters
    /// at 10000 cm (100 m) have half the base score of emitters at 0 cm. The visibility
    /// multiplier heavily penalises off-screen emitters without culling them
    /// entirely, preserving priority for important background effects.
    ///
    /// # Parameters
    /// - `priority`          -- User weight (any uint32_t; higher = more important).
    /// - `distanceToCamera`  -- Distance in world units (cm). Clamped to [0, ∞) internally.
    /// - `isFrustumVisible`  -- True if any part of the emitter is inside the frustum.
    ///
    /// # Returns
    /// Significance score in [0, ∞). Returns 0.0 if `priority` is 0.
    [[nodiscard]] static float ComputeScore(
        uint32_t priority,
        float distanceToCamera,
        bool isFrustumVisible) noexcept;

    // ── Ranking ───────────────────────────────────────────────────────────────

    /// Computes and sorts emitters by significance (descending).
    ///
    /// For each emitter in `emitters`, calls `ComputeScore()` to populate
    /// `VFXEmitterSignificance::score`, then sorts the vector so the most
    /// significant emitter is at index 0.
    ///
    /// This is a pure sort with no side effects on LOD levels; call
    /// `EnforceBudget()` afterwards to degrade low-significance emitters.
    ///
    /// # Parameters
    /// - `emitters` -- [in/out] Emitters to rank. `score` is written; other
    ///                 input fields (`distanceToCamera`, `isFrustumVisible`,
    ///                 `priority`) must be pre-filled by the caller.
    ///
    /// # Side Effects
    /// - Mutates `score` on every element.
    /// - Reorders the vector in-place (descending score).
    void RankEmitters(std::vector<VFXEmitterSignificance>& emitters) const;

    // ── Budget Enforcement ────────────────────────────────────────────────────

    /// Degrades LOD on low-significance emitters until total demand ≤ maxBudget.
    ///
    /// Algorithm:
    /// 1. Calls `RankEmitters()` to sort by significance (descending).
    /// 2. Sums all `particleDemand` values. If ≤ `maxBudget`, exits immediately.
    /// 3. Iterates from the lowest-significance emitter upward, progressively
    ///    degrading each emitter's `appliedLod`:
    ///    - Full → Reduced  (demand × 0.5)
    ///    - Reduced → Minimal (demand × 0.25)
    ///    - Minimal → Culled  (demand × 0.0)
    ///    Each step recalculates the emitter's effective demand and checks whether
    ///    the total now fits within the budget.
    /// 4. Stops as soon as total demand fits within `maxBudget`.
    ///
    /// High-significance emitters are degraded only after all lower-significance
    /// emitters have been fully culled.
    ///
    /// # Parameters
    /// - `emitters`  -- [in/out] Emitters to evaluate. Must have valid
    ///                  `particleDemand` and `score` fields. `appliedLod` is
    ///                  reset to `Full` before enforcement begins.
    /// - `maxBudget` -- Maximum total particle count allowed across all emitters.
    ///
    /// # Side Effects
    /// - Calls `RankEmitters()`, mutating `score` and reordering the vector.
    /// - Writes `appliedLod` on every element.
    /// - Logs culled emitters via `UZ_LOG(VFX, Verbose, ...)`.
    void EnforceBudget(
        std::vector<VFXEmitterSignificance>& emitters,
        uint32_t maxBudget) const;
};

} // namespace uz
