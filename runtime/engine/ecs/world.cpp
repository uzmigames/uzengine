/// UzEngine World — ECS Container Implementation
///
/// Implements entity lifecycle, generational handle tracking, and registry operations.

#include "world.h"

UZ_DEFINE_LOG_CATEGORY(ECS, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Entity Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

Entity World::CreateEntity() {
    auto enttEntity = registry_.create();
    auto slot = static_cast<uint32_t>(entt::to_integral(enttEntity));

    EnsureGenerationSlot(slot);
    generations_[slot]++;

    Entity handle;
    handle.id = slot;
    handle.generation = generations_[slot];

    UZ_LOG(ECS, Trace, "CreateEntity: id={}, generation={}", handle.id, handle.generation);
    return handle;
}

void World::DestroyEntity(Entity entity) {
    UZ_CHECK(IsValid(entity));

    UZ_LOG(ECS, Trace, "DestroyEntity: id={}, generation={}", entity.id, entity.generation);

    registry_.destroy(ToEntt(entity));
    // Generation stays — next CreateEntity that reuses this slot will bump it
}

bool World::IsValid(Entity entity) const {
    if (!entity.IsValid()) {
        return false;
    }

    if (entity.id >= generations_.size()) {
        return false;
    }

    if (entity.generation != generations_[entity.id]) {
        return false;
    }

    // Also verify EnTT still considers it alive (handles edge cases)
    return registry_.valid(ToEntt(entity));
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

size_t World::EntityCount() const {
    return static_cast<size_t>(registry_.storage<entt::entity>()->free_list());
}

void World::Clear() {
    auto count = EntityCount();
    UZ_LOG(ECS, Info, "World::Clear: destroying {} entities", count);

    registry_.clear();
    // Don't clear generations_ — keep generation history so stale handles stay invalid
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

entt::entity World::ToEntt(Entity entity) const {
    return static_cast<entt::entity>(entity.id);
}

void World::EnsureGenerationSlot(uint32_t slot) {
    if (slot >= generations_.size()) {
        generations_.resize(static_cast<size_t>(slot) + 1, 0);
    }
}

} // namespace uz
