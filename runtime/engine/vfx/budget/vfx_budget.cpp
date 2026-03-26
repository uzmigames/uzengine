/// VFXBudget implementation -- global particle quality level and statistics.

#include "engine/vfx/budget/vfx_budget.h"

#include "engine/logging/log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFXQualityLevel -- free functions
// ─────────────────────────────────────────────────────────────────────────────

const char* VFXQualityLevelToString(VFXQualityLevel level) noexcept {
    switch (level) {
        case VFXQualityLevel::Low:    return "Low";
        case VFXQualityLevel::Medium: return "Medium";
        case VFXQualityLevel::High:   return "High";
        case VFXQualityLevel::Ultra:  return "Ultra";
    }
    return "Unknown";
}

uint32_t VFXQualityLevelToMaxParticles(VFXQualityLevel level) noexcept {
    switch (level) {
        case VFXQualityLevel::Low:    return 125'000u;
        case VFXQualityLevel::Medium: return 250'000u;
        case VFXQualityLevel::High:   return 500'000u;
        case VFXQualityLevel::Ultra:  return 1'000'000u;
    }
    return 0u;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXBudget -- Construction
// ─────────────────────────────────────────────────────────────────────────────

VFXBudget::VFXBudget(VFXQualityLevel level)
    : qualityLevel_(level)
    , maxParticles_(VFXQualityLevelToMaxParticles(level))
{
    // stats_ is zero-initialised by VFXBudgetStats default member initialisers;
    // populate totalBudget so it is correct before the first UpdateStats() call.
    stats_.totalBudget       = maxParticles_;
    stats_.particlesAvailable = maxParticles_;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXBudget -- Quality Level
// ─────────────────────────────────────────────────────────────────────────────

void VFXBudget::SetQualityLevel(VFXQualityLevel level) {
    if (level == qualityLevel_) {
        return;
    }

    UZ_LOG(VFX, Info,
        "VFX quality level changed: {} -> {} ({} particles)",
        VFXQualityLevelToString(qualityLevel_),
        VFXQualityLevelToString(level),
        VFXQualityLevelToMaxParticles(level));

    qualityLevel_ = level;
    maxParticles_ = VFXQualityLevelToMaxParticles(level);

    // Keep totalBudget consistent so callers reading stats before the next
    // UpdateStats() call still see the correct cap.
    stats_.totalBudget = maxParticles_;
}

VFXQualityLevel VFXBudget::GetQualityLevel() const noexcept {
    return qualityLevel_;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXBudget -- Budget Query
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VFXBudget::GetMaxParticles() const noexcept {
    return maxParticles_;
}

bool VFXBudget::HasBudget(uint32_t requested) const noexcept {
    // Guard against integer overflow: if particlesUsed is near UINT32_MAX
    // the addition could wrap. Check subtractively instead.
    if (requested > maxParticles_) {
        return false;
    }
    return stats_.particlesUsed <= (maxParticles_ - requested);
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXBudget -- Statistics
// ─────────────────────────────────────────────────────────────────────────────

void VFXBudget::UpdateStats(
    uint32_t aliveParticles,
    uint32_t activeEmitters,
    uint32_t culledEmitters)
{
    stats_.totalBudget     = maxParticles_;
    stats_.particlesUsed   = aliveParticles;
    stats_.activeEmitters  = activeEmitters;
    stats_.culledEmitters  = culledEmitters;

    // Clamp available to 0 rather than wrapping if somehow aliveParticles
    // exceeds the budget (can occur transiently during quality level changes).
    stats_.particlesAvailable =
        (aliveParticles <= maxParticles_) ? (maxParticles_ - aliveParticles) : 0u;

    // Avoid division by zero for a zero-budget configuration.
    stats_.budgetUtilization = (maxParticles_ > 0u)
        ? (static_cast<float>(aliveParticles) / static_cast<float>(maxParticles_))
        : 0.0f;

    // Clamp to [0, 1] -- aliveParticles could exceed maxParticles_ transiently.
    if (stats_.budgetUtilization > 1.0f) {
        stats_.budgetUtilization = 1.0f;
    }
}

const VFXBudgetStats& VFXBudget::GetStats() const noexcept {
    return stats_;
}

} // namespace uz
