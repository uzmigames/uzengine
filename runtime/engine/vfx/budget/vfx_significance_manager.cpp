/// VFXSignificanceManager implementation -- emitter ranking and budget enforcement.

#include "engine/vfx/budget/vfx_significance_manager.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Distance attenuation coefficient: 1% falloff per 100 cm (1 metre).
/// At 10000 cm (100 m) the distance factor is ~0.5 (half of the 0-cm value).
static constexpr float kDistanceFalloff = 0.0001f;

/// Significance multiplier applied to off-screen emitters.
/// A factor of 0.1 heavily penalises culled emitters without zeroing their score,
/// so a very high-priority off-screen emitter can still outrank a low-priority
/// on-screen one.
static constexpr float kOffScreenMultiplier = 0.1f;

/// Spawn-rate factor for each LOD tier, indexed by VFXLodLevel cast to uint8_t.
/// Full=1.0, Reduced=0.5, Minimal=0.25, Culled=0.0.
static constexpr float kLodSpawnFactor[4] = {1.0f, 0.5f, 0.25f, 0.0f};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the effective particle demand for an emitter at the given LOD level.
///
/// Multiplies the emitter's full-demand by the LOD spawn factor and rounds up
/// so that fractional demands never silently vanish (e.g. Minimal on a 3-particle
/// emitter still costs 1 particle, not 0).
static uint32_t EffectiveDemand(const VFXEmitterSignificance& e, VFXLodLevel lod) noexcept {
    const float factor = kLodSpawnFactor[static_cast<uint8_t>(lod)];
    return static_cast<uint32_t>(std::ceil(static_cast<float>(e.particleDemand) * factor));
}

/// Returns the next (lower quality) LOD tier, or Culled if already at Culled.
static VFXLodLevel DegradeLod(VFXLodLevel lod) noexcept {
    switch (lod) {
        case VFXLodLevel::Full:    return VFXLodLevel::Reduced;
        case VFXLodLevel::Reduced: return VFXLodLevel::Minimal;
        case VFXLodLevel::Minimal: return VFXLodLevel::Culled;
        case VFXLodLevel::Culled:  return VFXLodLevel::Culled;
    }
    return VFXLodLevel::Culled; // unreachable, satisfies compiler
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXSignificanceManager -- ComputeScore
// ─────────────────────────────────────────────────────────────────────────────

float VFXSignificanceManager::ComputeScore(
    uint32_t priority,
    float distanceToCamera,
    bool isFrustumVisible) noexcept
{
    // Guard against negative distances from imprecise caller math.
    const float safeDist = distanceToCamera < 0.0f ? 0.0f : distanceToCamera;

    const float distanceScore = 1.0f / (1.0f + safeDist * kDistanceFalloff);
    const float visibilityScore = isFrustumVisible ? 1.0f : kOffScreenMultiplier;

    return static_cast<float>(priority) * distanceScore * visibilityScore;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXSignificanceManager -- RankEmitters
// ─────────────────────────────────────────────────────────────────────────────

void VFXSignificanceManager::RankEmitters(
    std::vector<VFXEmitterSignificance>& emitters) const
{
    for (auto& e : emitters) {
        e.score = ComputeScore(e.priority, e.distanceToCamera, e.isFrustumVisible);
    }

    std::sort(emitters.begin(), emitters.end(),
        [](const VFXEmitterSignificance& a, const VFXEmitterSignificance& b) noexcept {
            return a.score > b.score; // descending -- highest significance first
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXSignificanceManager -- EnforceBudget
// ─────────────────────────────────────────────────────────────────────────────

void VFXSignificanceManager::EnforceBudget(
    std::vector<VFXEmitterSignificance>& emitters,
    uint32_t maxBudget) const
{
    if (emitters.empty()) {
        return;
    }

    // Step 1: rank by significance and reset all LOD levels to Full.
    RankEmitters(emitters);
    for (auto& e : emitters) {
        e.appliedLod = VFXLodLevel::Full;
    }

    // Step 2: compute total demand at Full quality.
    uint32_t totalDemand = 0;
    for (const auto& e : emitters) {
        totalDemand += e.particleDemand; // Full demand, no factor needed
    }

    if (totalDemand <= maxBudget) {
        // Budget is sufficient -- nothing to degrade.
        return;
    }

    // Step 3: degrade emitters from lowest significance upward.
    // Emitters are already sorted descending by score, so we iterate from the
    // back (lowest significance) toward the front (highest significance).
    //
    // For each emitter we try all degradation steps before moving to the next,
    // stopping as soon as the cumulative demand fits.
    for (int i = static_cast<int>(emitters.size()) - 1; i >= 0; --i) {
        auto& e = emitters[static_cast<size_t>(i)];

        // Try each degradation step for this emitter until the budget is met
        // or the emitter is fully culled.
        while (e.appliedLod != VFXLodLevel::Culled && totalDemand > maxBudget) {
            const uint32_t oldDemand = EffectiveDemand(e, e.appliedLod);
            e.appliedLod = DegradeLod(e.appliedLod);
            const uint32_t newDemand = EffectiveDemand(e, e.appliedLod);

            // oldDemand >= newDemand always, so this subtraction is safe.
            totalDemand -= (oldDemand - newDemand);

            if (e.appliedLod == VFXLodLevel::Culled) {
                UZ_LOG(VFX, Trace,
                    "Budget enforcement: culled emitter id={} (score={:.2f}, demand={})",
                    e.emitterId, e.score, e.particleDemand);
            }
        }

        if (totalDemand <= maxBudget) {
            break;
        }
    }

    // If we exhausted all emitters and still exceed the budget, we can do no
    // more (all emitters are at Culled). This can happen when maxBudget is 0.
}

} // namespace uz
