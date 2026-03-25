/// VFX Budget -- global particle quality level and per-frame budget statistics.
///
/// # Overview
/// `VFXBudget` wraps the global particle cap with a named quality-level enum
/// so that game code can express intent ("Medium quality") rather than magic
/// numbers.  Per-frame statistics (`VFXBudgetStats`) are updated once per frame
/// by the VFX system and exposed for debug overlays and profiling dashboards.
///
/// # Quality Level → Max Particles
/// | Level  | Max Particles |
/// |--------|--------------|
/// | Low    |    125,000   |
/// | Medium |    250,000   |
/// | High   |    500,000   |
/// | Ultra  |  1,000,000   |
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main/simulation thread.
///
/// # Example
/// ```cpp
/// VFXBudget budget(VFXQualityLevel::High);
/// budget.SetQualityLevel(VFXQualityLevel::Medium);  // dynamic quality change
///
/// // After simulation:
/// budget.UpdateStats(aliveCount, activeEmitters, culledEmitters);
///
/// const auto& stats = budget.GetStats();
/// DrawDebugText("VFX: %u / %u (%.1f%%)",
///     stats.particlesUsed, stats.totalBudget,
///     stats.budgetUtilization * 100.0f);
/// ```

#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFXQualityLevel -- named quality presets for the particle system
// ─────────────────────────────────────────────────────────────────────────────

/// Named particle quality presets that map to concrete maximum particle counts.
///
/// Ordered from lowest to highest quality. Use `VFXQualityLevelToMaxParticles()`
/// to convert to an integer cap.
enum class VFXQualityLevel : uint8_t {
    Low    = 0,  ///< 125,000 max particles -- suitable for low-end GPUs.
    Medium = 1,  ///< 250,000 max particles -- balanced default for mid-range hardware.
    High   = 2,  ///< 500,000 max particles -- recommended for modern discrete GPUs.
    Ultra  = 3,  ///< 1,000,000 max particles -- high-end workstations and consoles.
};

/// Converts a `VFXQualityLevel` to its human-readable name.
///
/// # Returns
/// A null-terminated string literal ("Low", "Medium", "High", or "Ultra").
/// Returns "Unknown" for out-of-range values.
[[nodiscard]] const char* VFXQualityLevelToString(VFXQualityLevel level) noexcept;

/// Returns the maximum particle count for the given quality level.
///
/// | Level  | Particles  |
/// |--------|-----------|
/// | Low    |   125,000 |
/// | Medium |   250,000 |
/// | High   |   500,000 |
/// | Ultra  | 1,000,000 |
///
/// # Returns
/// Particle cap as a `uint32_t`. Returns 0 for out-of-range values.
[[nodiscard]] uint32_t VFXQualityLevelToMaxParticles(VFXQualityLevel level) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// VFXBudgetStats -- runtime particle budget statistics
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime particle budget statistics for debug overlays and profiling.
///
/// Updated once per frame by `VFXBudget::UpdateStats()`. All counts are
/// CPU-side values; GPU-side alive counts may lag by one frame.
struct VFXBudgetStats {
    /// Maximum particles allowed globally (mirrors `VFXBudget::GetMaxParticles()`).
    uint32_t totalBudget = 0;

    /// Number of particles currently alive across all active emitters.
    uint32_t particlesUsed = 0;

    /// Remaining budget: `totalBudget - particlesUsed` (clamped to 0 on overflow).
    uint32_t particlesAvailable = 0;

    /// Number of emitters currently at Full, Reduced, or Minimal LOD.
    uint32_t activeEmitters = 0;

    /// Number of emitters currently at Culled LOD (spawning 0 new particles).
    uint32_t culledEmitters = 0;

    /// Fraction of the budget in use: `particlesUsed / totalBudget` in [0, 1].
    /// 0.0 when `totalBudget` is 0 (avoids division by zero).
    float budgetUtilization = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXBudget -- global particle budget with quality level support
// ─────────────────────────────────────────────────────────────────────────────

/// Global particle budget manager with quality level presets.
///
/// Wraps a maximum particle count derived from a `VFXQualityLevel` and
/// provides helpers for checking whether more particles can be spawned and
/// for collecting per-frame statistics.
///
/// # Ownership
/// Plain value type -- copy or store by value. Holds no GPU resources.
///
/// # Relationship with ParticleBudget
/// `ParticleBudget` (core/) handles lock-free per-emitter GPU allocation.
/// `VFXBudget` sits above that layer, providing quality-level semantics and
/// the per-frame statistics overlay used by the editor and debug HUD.
class VFXBudget {
public:
    // ── Construction ─────────────────────────────────────────────────────────

    /// Constructs a budget at the given quality level.
    ///
    /// # Parameters
    /// - `level`: Initial quality preset. Default: `High` (500,000 particles).
    explicit VFXBudget(VFXQualityLevel level = VFXQualityLevel::High);

    ~VFXBudget() = default;

    VFXBudget(const VFXBudget&) = default;
    VFXBudget& operator=(const VFXBudget&) = default;
    VFXBudget(VFXBudget&&) noexcept = default;
    VFXBudget& operator=(VFXBudget&&) noexcept = default;

    // ── Quality Level ─────────────────────────────────────────────────────────

    /// Changes the active quality level and updates `maxParticles_` accordingly.
    ///
    /// Does not retroactively affect existing GPU allocations; callers should
    /// flush and re-initialise emitters when changing quality at runtime.
    ///
    /// # Parameters
    /// - `level`: New quality preset.
    ///
    /// # Side Effects
    /// - Updates `maxParticles_`.
    /// - Logs `VFX/Info` with the old and new quality level names.
    void SetQualityLevel(VFXQualityLevel level);

    /// Returns the currently active quality level.
    [[nodiscard]] VFXQualityLevel GetQualityLevel() const noexcept;

    // ── Budget Query ──────────────────────────────────────────────────────────

    /// Returns the maximum particle count for the current quality level.
    [[nodiscard]] uint32_t GetMaxParticles() const noexcept;

    /// Returns true if `stats_.particlesUsed + requested <= maxParticles_`.
    ///
    /// # Parameters
    /// - `requested`: Number of particles the caller wants to add.
    ///
    /// # Returns
    /// `true` if there is sufficient budget for `requested` more particles.
    /// `false` if the request would exceed the cap (does NOT allocate anything).
    ///
    /// # Note
    /// This is a read-only check. Actual allocation tracking is handled by
    /// `ParticleBudget` (core/); this method is for pre-flight checks and HUD.
    [[nodiscard]] bool HasBudget(uint32_t requested) const noexcept;

    // ── Statistics ────────────────────────────────────────────────────────────

    /// Updates per-frame statistics from simulation results.
    ///
    /// Should be called once per frame, after the VFX system has finished
    /// its simulation pass and significance enforcement.
    ///
    /// # Parameters
    /// - `aliveParticles`  -- Total alive (CPU-mirrored) particle count.
    /// - `activeEmitters`  -- Emitters at Full / Reduced / Minimal LOD.
    /// - `culledEmitters`  -- Emitters at Culled LOD.
    ///
    /// # Side Effects
    /// - Writes all fields of `stats_` including `budgetUtilization`.
    void UpdateStats(
        uint32_t aliveParticles,
        uint32_t activeEmitters,
        uint32_t culledEmitters);

    /// Returns the statistics last written by `UpdateStats()`.
    ///
    /// # Returns
    /// Const reference valid until the next call to `UpdateStats()` or
    /// `SetQualityLevel()`.
    [[nodiscard]] const VFXBudgetStats& GetStats() const noexcept;

private:
    // ── Data ──────────────────────────────────────────────────────────────────

    /// Active quality preset (determines maxParticles_).
    VFXQualityLevel qualityLevel_ = VFXQualityLevel::High;

    /// Particle cap derived from qualityLevel_. Updated by SetQualityLevel().
    uint32_t maxParticles_ = 0;

    /// Per-frame budget statistics; populated by UpdateStats().
    VFXBudgetStats stats_;
};

} // namespace uz
