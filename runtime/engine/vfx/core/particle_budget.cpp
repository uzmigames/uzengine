/// Particle Budget -- lock-free CAS implementation for global particle allocation.

#include "particle_budget.h"

#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

ParticleBudget::ParticleBudget(uint32_t maxParticles) noexcept
    : max_(maxParticles)
{
}

bool ParticleBudget::Allocate(uint32_t requested, uint32_t& allocated) noexcept
{
    allocated = 0;
    if (requested == 0) return false;

    // Lock-free CAS loop: atomically reserve as many particles as available.
    uint32_t currentUsed = used_.load(std::memory_order_relaxed);
    while (true) {
        if (currentUsed >= max_) {
            UZ_LOG(VFX, Warning, "Particle budget exhausted: used={}, max={}",
                   currentUsed, max_);
            return false;
        }

        const uint32_t available = max_ - currentUsed;
        const uint32_t toAllocate = std::min(requested, available);
        const uint32_t newUsed = currentUsed + toAllocate;

        if (used_.compare_exchange_weak(currentUsed, newUsed,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
            allocated = toAllocate;
            return true;
        }
        // CAS failed -- currentUsed was updated by compare_exchange_weak, retry
    }
}

void ParticleBudget::Return(uint32_t count) noexcept
{
    if (count == 0) return;

    const uint32_t prev = used_.fetch_sub(count, std::memory_order_acq_rel);
    UZ_CHECKF(prev >= count,
              "ParticleBudget::Return: returning %u but only %u in use", count, prev);
}

uint32_t ParticleBudget::GetAvailable() const noexcept
{
    const uint32_t currentUsed = used_.load(std::memory_order_relaxed);
    return (currentUsed < max_) ? (max_ - currentUsed) : 0;
}

float ParticleBudget::GetUsageRatio() const noexcept
{
    if (max_ == 0) return 0.0f;
    return static_cast<float>(used_.load(std::memory_order_relaxed))
         / static_cast<float>(max_);
}

}  // namespace uz
