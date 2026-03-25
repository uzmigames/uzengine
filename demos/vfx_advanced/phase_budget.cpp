/// VFX Advanced Demo -- Phase 4: Budget Stress Test.
///
/// Isolated in its own translation unit because `vfx_significance_manager.h`
/// and `vfx_lod.h` both declare `VFXLodLevel` inside `namespace uz`. Including
/// both in the same TU would produce a redefinition error.
///
/// This file includes only `vfx_significance_manager.h` and `vfx_budget.h`.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/vfx/budget/vfx_significance_manager.h"
#include "engine/vfx/budget/vfx_budget.h"

// ─────────────────────────────────────────────────────────────────────────────
// Budget phase constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of emitters created for the stress test.
static constexpr int kEmitterCount = 50;

/// Particle budget cap for the primary enforcement run.
static constexpr uint32_t kBudgetCap = 100'000;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (local to this TU)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

void PrintSeparator() {
    std::printf("──────────────────────────────────────────────────────────────\n");
}

void PrintPhaseHeader(const char* title) {
    std::printf("\n");
    PrintSeparator();
    std::printf("  %s\n", title);
    PrintSeparator();
}

/// Converts the significance-manager's `VFXLodLevel` to its display name.
const char* SigLodName(uz::VFXLodLevel lod) noexcept {
    switch (lod) {
        case uz::VFXLodLevel::Full:    return "Full";
        case uz::VFXLodLevel::Reduced: return "Reduced";
        case uz::VFXLodLevel::Minimal: return "Minimal";
        case uz::VFXLodLevel::Culled:  return "Culled";
    }
    return "Unknown";
}

/// Computes effective particle demand after LOD reduction.
///
/// Maps the significance-manager's `VFXLodLevel` to a spawn-rate fraction
/// (Full=1.0, Reduced=0.5, Minimal=0.25, Culled=0.0) and multiplies by the
/// full demand. The result is truncated to uint32_t (matching integer division).
///
/// # Parameters
/// - `fullDemand`: Particle demand at `VFXLodLevel::Full`.
/// - `lod`:        LOD level assigned by `EnforceBudget()`.
///
/// # Returns
/// Effective particle demand, rounded down.
uint32_t EffectiveDemand(uint32_t fullDemand, uz::VFXLodLevel lod) noexcept {
    switch (lod) {
        case uz::VFXLodLevel::Full:    return fullDemand;
        case uz::VFXLodLevel::Reduced: return fullDemand / 2;
        case uz::VFXLodLevel::Minimal: return fullDemand / 4;
        case uz::VFXLodLevel::Culled:  return 0;
    }
    return 0;
}

/// Builds 50 deterministic `VFXEmitterSignificance` records.
///
/// Emitter index `i` (0-based) is assigned:
/// - `priority`         = (i % 10) + 1              (range 1–10)
/// - `distanceToCamera` = 10.0f + i * 10.0f         (range 10–500 m)
/// - `isFrustumVisible` = (i % 3 != 0)              (every 3rd emitter is hidden)
/// - `particleDemand`   = (i + 1) * 500             (range 500–25,000)
///
/// # Returns
/// Vector of 50 populated records with `appliedLod` initialised to `Full`.
std::vector<uz::VFXEmitterSignificance> BuildEmitters() {
    std::vector<uz::VFXEmitterSignificance> emitters;
    emitters.reserve(static_cast<size_t>(kEmitterCount));

    for (int i = 0; i < kEmitterCount; ++i) {
        uz::VFXEmitterSignificance e;
        e.emitterId        = static_cast<uint32_t>(i);
        e.priority         = static_cast<uint32_t>((i % 10) + 1);
        e.distanceToCamera = 10.0f + static_cast<float>(i) * 10.0f;
        e.isFrustumVisible = (i % 3 != 0);
        e.particleDemand   = static_cast<uint32_t>((i + 1) * 500);
        e.appliedLod       = uz::VFXLodLevel::Full;
        e.score            = 0.0f;
        emitters.push_back(e);
    }

    return emitters;
}

/// Counts kept and culled emitters and sums effective particle demand.
///
/// # Parameters
/// - `emitters`:  Post-enforcement vector (read-only).
/// - `outKept`:   Written with the count of emitters whose LOD != Culled.
/// - `outCulled`: Written with the count of emitters at Culled LOD.
/// - `outTotal`:  Written with the sum of EffectiveDemand across all emitters.
void SumEnforcedStats(
        const std::vector<uz::VFXEmitterSignificance>& emitters,
        uint32_t& outKept,
        uint32_t& outCulled,
        uint32_t& outTotal) {
    outKept   = 0;
    outCulled = 0;
    outTotal  = 0;
    for (const auto& e : emitters) {
        if (e.appliedLod == uz::VFXLodLevel::Culled) {
            ++outCulled;
        } else {
            ++outKept;
        }
        outTotal += EffectiveDemand(e.particleDemand, e.appliedLod);
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// RunBudgetPhase -- exported to main.cpp via forward declaration
// ─────────────────────────────────────────────────────────────────────────────

/// Runs the significance-based budget stress test across all four quality levels.
///
/// **Primary run** (100,000-particle cap):
/// - Calls `VFXSignificanceManager::EnforceBudget()` on 50 emitters.
/// - Prints a ranked per-emitter table: ID, priority, distance, visibility,
///   significance score, assigned LOD, and effective particle demand.
/// - Verifies total effective demand <= 100,000.
/// - Prints a `VFXBudgetStats` snapshot.
///
/// **Quality-level sweep** (Low / Medium / High / Ultra):
/// - Re-runs enforcement with each quality level's particle cap.
/// - Prints a single summary line per quality level.
/// - Verifies effective demand <= cap in all cases.
///
/// # Returns
/// `true` if total effective demand <= budget cap in every enforcement run;
/// `false` on any over-budget result.
bool RunBudgetPhase() {
    PrintPhaseHeader("Phase 4 -- Budget Stress Test (50 emitters, 100K cap)");

    uz::VFXSignificanceManager sigManager;
    bool allOk = true;

    // ── Primary run: 100,000-particle cap ────────────────────────────────────
    {
        std::vector<uz::VFXEmitterSignificance> emitters = BuildEmitters();

        uint32_t rawTotal = 0;
        for (const auto& e : emitters) { rawTotal += e.particleDemand; }

        std::printf("  %d emitters  rawDemand=%u  cap=%u\n\n",
                    kEmitterCount, rawTotal, kBudgetCap);

        sigManager.EnforceBudget(emitters, kBudgetCap);

        // Print per-emitter table sorted by significance (EnforceBudget sorts internally).
        std::printf("  %-4s  %-5s  %-5s  %-6s  %-8s  %-9s  %-9s  %s\n",
                    "Rank", "ID", "Pri", "Dist", "Visible", "Score", "LOD", "EffDemand");
        PrintSeparator();

        for (size_t idx = 0; idx < emitters.size(); ++idx) {
            const auto& e = emitters[idx];
            std::printf("  %4zu  %5u  %5u  %6.0f  %-8s  %9.4f  %-9s  %u\n",
                        idx + 1,
                        e.emitterId,
                        e.priority,
                        static_cast<double>(e.distanceToCamera),
                        e.isFrustumVisible ? "yes" : "no",
                        static_cast<double>(e.score),
                        SigLodName(e.appliedLod),
                        EffectiveDemand(e.particleDemand, e.appliedLod));
        }
        PrintSeparator();

        uint32_t kept, culled, effectiveTotal;
        SumEnforcedStats(emitters, kept, culled, effectiveTotal);

        std::printf("  Kept=%u  Culled=%u  EffectiveTotal=%u  Cap=%u  Fits=%s\n\n",
                    kept, culled, effectiveTotal, kBudgetCap,
                    (effectiveTotal <= kBudgetCap) ? "YES" : "NO");

        if (effectiveTotal > kBudgetCap) {
            std::fprintf(stderr,
                         "  [FAIL] Effective total %u exceeds cap %u after enforcement\n",
                         effectiveTotal, kBudgetCap);
            allOk = false;
        } else {
            std::printf("  [OK]  Budget enforced: effectiveTotal=%u <= cap=%u\n",
                        effectiveTotal, kBudgetCap);
        }

        // ── VFXBudget stats snapshot ──────────────────────────────────────────
        uz::VFXBudget budget{uz::VFXQualityLevel::High};
        budget.UpdateStats(effectiveTotal, kept, culled);

        const uz::VFXBudgetStats& stats = budget.GetStats();

        std::printf("\n  VFXBudgetStats (quality=High, maxParticles=%u):\n",
                    budget.GetMaxParticles());
        std::printf("    totalBudget       : %u\n", stats.totalBudget);
        std::printf("    particlesUsed     : %u\n", stats.particlesUsed);
        std::printf("    particlesAvailable: %u\n", stats.particlesAvailable);
        std::printf("    activeEmitters    : %u\n", stats.activeEmitters);
        std::printf("    culledEmitters    : %u\n", stats.culledEmitters);
        std::printf("    budgetUtilization : %.2f%%\n",
                    static_cast<double>(stats.budgetUtilization) * 100.0);
    }

    // ── Quality-level sweep: Low / Medium / High / Ultra ─────────────────────
    PrintSeparator();
    std::printf("\n  Quality-level sweep (%d emitters, same distribution):\n",
                kEmitterCount);
    std::printf("  %-8s  %-10s  %-6s  %-8s  %-12s  %s\n",
                "Quality", "MaxPart", "Kept", "Culled", "EffDemand", "Fits");
    PrintSeparator();

    static constexpr uz::VFXQualityLevel kLevels[] = {
        uz::VFXQualityLevel::Low,
        uz::VFXQualityLevel::Medium,
        uz::VFXQualityLevel::High,
        uz::VFXQualityLevel::Ultra,
    };

    for (const auto level : kLevels) {
        const uint32_t maxPart = uz::VFXQualityLevelToMaxParticles(level);

        std::vector<uz::VFXEmitterSignificance> emitters = BuildEmitters();
        sigManager.EnforceBudget(emitters, maxPart);

        uint32_t kept, culled, effectiveTotal;
        SumEnforcedStats(emitters, kept, culled, effectiveTotal);

        const bool fits = (effectiveTotal <= maxPart);
        if (!fits) {
            std::fprintf(stderr,
                         "  [FAIL] Quality=%s: effectiveTotal=%u > maxPart=%u\n",
                         uz::VFXQualityLevelToString(level), effectiveTotal, maxPart);
            allOk = false;
        }

        std::printf("  %-8s  %10u  %6u  %8u  %12u  %s\n",
                    uz::VFXQualityLevelToString(level),
                    maxPart,
                    kept,
                    culled,
                    effectiveTotal,
                    fits ? "YES" : "NO");

        // Lifecycle check: construct a VFXBudget, change its level, update stats.
        uz::VFXBudget budget{uz::VFXQualityLevel::Low};
        budget.SetQualityLevel(level);
        budget.UpdateStats(effectiveTotal, kept, culled);

        // HasBudget(1) must be true when effectiveTotal < maxPart.
        const bool hasBudget = budget.HasBudget(1);
        if (effectiveTotal < maxPart && !hasBudget) {
            std::fprintf(stderr,
                         "  [FAIL] Quality=%s: HasBudget(1) returned false with room remaining\n",
                         uz::VFXQualityLevelToString(level));
            allOk = false;
        }
    }

    PrintSeparator();
    std::printf("\n  Phase 4 result: [%s]\n", allOk ? "OK" : "FAIL");

    return allOk;
}
