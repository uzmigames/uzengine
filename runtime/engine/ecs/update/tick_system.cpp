/// TickSystem implementation — per-group tick dispatch with interval throttling.

#include "tick_system.h"
#include "tickable_component.h"
#include "world.h"
#include "log.h"
#include "engine_assert.h"
#include "profiler.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void TickSystem::RegisterTickable(Entity entity, TickableComponent* comp, const World& world) {
    UZ_CHECK(comp != nullptr);
    UZ_CHECK(world.IsValid(entity));

    const size_t idx = GroupIndex(comp->GetTickGroup());
    if (idx >= kGroupCount) {
        UZ_LOG(ECS, Warning, "TickSystem: cannot register tickable with TickGroup::None");
        return;
    }

    // Check for duplicates.
    auto& group = m_groups[idx];
    for (const auto& entry : group) {
        if (entry.entity.id == entity.id && entry.entity.generation == entity.generation) {
            UZ_LOG(ECS, Warning, "TickSystem: entity {} already registered in tick group {}",
                   entity.id, static_cast<int>(comp->GetTickGroup()));
            return;
        }
    }

    group.push_back({entity, comp});
    UZ_LOG(ECS, Debug, "TickSystem: registered entity {} in tick group {}",
           entity.id, static_cast<int>(comp->GetTickGroup()));
}

void TickSystem::UnregisterTickable(Entity entity) {
    for (auto& group : m_groups) {
        auto it = std::remove_if(group.begin(), group.end(),
            [&entity](const TickEntry& e) {
                return e.entity.id == entity.id;
            });
        if (it != group.end()) {
            group.erase(it, group.end());
            UZ_LOG(ECS, Debug, "TickSystem: unregistered entity {}", entity.id);
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void TickSystem::Tick(TickGroup group, float deltaTime, const World& world) {
    UZ_PROFILE_FUNCTION();

    const size_t idx = GroupIndex(group);
    if (idx >= kGroupCount) return;

    auto& entries = m_groups[idx];

    for (auto& entry : entries) {
        // Skip destroyed entities.
        if (!world.IsValid(entry.entity)) continue;

        // Skip disabled tickables.
        if (!entry.component->IsTickEnabled()) continue;

        // Interval throttling.
        const float interval = entry.component->GetTickInterval();
        if (interval > 0.F) {
            entry.component->m_timeSinceLastTick += deltaTime;
            if (entry.component->m_timeSinceLastTick < interval) continue;

            // Fire with actual elapsed time, preserve overshoot.
            const float elapsed = entry.component->m_timeSinceLastTick;
            entry.component->m_timeSinceLastTick -= interval;
            entry.component->OnTick(elapsed);
        } else {
            entry.component->OnTick(deltaTime);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

size_t TickSystem::GetTickableCount() const {
    size_t total = 0;
    for (const auto& group : m_groups) {
        total += group.size();
    }
    return total;
}

size_t TickSystem::GetTickableCount(TickGroup group) const {
    const size_t idx = GroupIndex(group);
    if (idx >= kGroupCount) return 0;
    return m_groups[idx].size();
}

void TickSystem::Clear() {
    for (auto& group : m_groups) {
        group.clear();
    }
    UZ_LOG(ECS, Debug, "TickSystem: cleared all tickables");
}

size_t TickSystem::GroupIndex(TickGroup group) {
    const auto val = static_cast<uint8_t>(group);
    if (val >= kGroupCount) return kGroupCount; // None or invalid
    return val;
}

} // namespace uz
