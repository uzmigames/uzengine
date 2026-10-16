#pragma once

/// UzEngine World — ECS Container
///
/// Wraps `entt::registry` with generational entity handles for use-after-destroy
/// detection. Provides the primary API for creating/destroying entities and
/// adding/removing/querying components.
///
/// # Generational Handles
/// Each entity SLOT (`entt::to_entity` of the packed id) has a `uint32_t` World
/// generation counter, independent of EnTT's 12-bit version. `Entity::id` stays the
/// packed EnTT id (index | version << 20); `generations_` is keyed by the slot index,
/// so memory is O(slots), not O(slots x versions). When a slot is reused the counter
/// is bumped (skipping 0 on wrap), so stale `Entity` handles from before the
/// destruction fail `IsValid()` even after EnTT's packed id repeats (period 4095).
///
/// # Thread Safety
/// Not thread-safe. All methods must be called from the main thread (game loop).
///
/// # Component Requirements
/// Components must be default-constructible or move-constructible plain data structs.
/// No virtual functions, no side-effect constructors. Follow the `CTransform`, `CMeshRenderer`
/// naming convention (C-prefix for ECS components).

#include "entity.h"
#include "log.h"
#include "engine_assert.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(ECS);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// World — primary ECS container
// ─────────────────────────────────────────────────────────────────────────────

/// Primary ECS container owning all entities and their components.
///
/// Wraps `entt::registry` and adds generational handle validation. All entity
/// manipulation goes through this class to ensure generation tracking stays consistent.
///
/// # Ownership
/// The World owns the `entt::registry` and the generation table. It does NOT own
/// the systems that operate on it — systems are free functions or subsystem ticks.
///
/// # Example
/// ```cpp
/// uz::World world;
/// auto entity = world.CreateEntity();
/// world.AddComponent<CTransform>(entity, CTransform{});
/// auto& transform = world.GetComponent<CTransform>(entity);
/// world.DestroyEntity(entity);
/// // entity handle is now stale — world.IsValid(entity) returns false
/// ```
class World {
public:
    World() = default;
    ~World() = default;

    /// Non-copyable (registry is not copyable).
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /// Movable.
    World(World&&) = default;
    World& operator=(World&&) = default;

    // ── Entity Lifecycle ────────────────────────────────────────────────

    /// Creates a new entity and returns a generational handle.
    ///
    /// The returned handle includes the current generation for the allocated slot,
    /// enabling use-after-destroy detection if the entity is later destroyed.
    ///
    /// # Returns
    /// A valid `Entity` handle with a unique id and current generation.
    ///
    /// # Side Effects
    /// Logs at Trace level when an entity is created.
    [[nodiscard]] Entity CreateEntity();

    /// Destroys an entity, removing all its components.
    ///
    /// The entity's slot may be reused by future `CreateEntity()` calls.
    /// Any `Entity` handles pointing to this slot become stale (their generation
    /// will no longer match).
    ///
    /// # Arguments
    /// * `entity` - The entity to destroy. Must be valid (asserts via UZ_CHECK).
    ///
    /// # Side Effects
    /// Logs at Trace level when an entity is destroyed.
    void DestroyEntity(Entity entity);

    /// Validates whether an entity handle still refers to a living entity.
    ///
    /// Checks both that the underlying EnTT entity is alive AND that the
    /// generation stored in the handle matches the current slot generation.
    ///
    /// # Arguments
    /// * `entity` - The handle to validate.
    ///
    /// # Returns
    /// `true` if the entity is alive and the generation matches.
    [[nodiscard]] bool IsValid(Entity entity) const;

    // ── Component Operations ────────────────────────────────────────────

    template <typename T, typename... Args>
    /// Adds a component to an entity by constructing it in-place.
    ///
    /// If the entity already has a component of type T, this replaces it.
    ///
    /// # Type Parameters
    /// * `T` - Component type. Must be a plain data struct (no virtual, no side-effect ctor).
    ///
    /// # Arguments
    /// * `entity` - Must be valid (asserts via UZ_CHECK).
    /// * `args` - Forwarded to the component constructor.
    ///
    /// # Returns
    /// Mutable reference to the newly emplaced component.
    T& AddComponent(Entity entity, Args&&... args);

    template <typename T>
    /// Removes a component from an entity.
    ///
    /// No-op if the entity does not have the component (logs a warning).
    ///
    /// # Type Parameters
    /// * `T` - Component type to remove.
    ///
    /// # Arguments
    /// * `entity` - Must be valid (asserts via UZ_CHECK).
    void RemoveComponent(Entity entity);

    template <typename T>
    /// Returns a mutable reference to an entity's component.
    ///
    /// # Type Parameters
    /// * `T` - Component type to retrieve.
    ///
    /// # Arguments
    /// * `entity` - Must be valid and must have the component (asserts via UZ_CHECK).
    ///
    /// # Returns
    /// Mutable reference to the component.
    [[nodiscard]] T& GetComponent(Entity entity);

    template <typename T>
    /// Returns a const reference to an entity's component.
    ///
    /// # Type Parameters
    /// * `T` - Component type to retrieve.
    ///
    /// # Arguments
    /// * `entity` - Must be valid and must have the component (asserts via UZ_CHECK).
    ///
    /// # Returns
    /// Const reference to the component.
    [[nodiscard]] const T& GetComponent(Entity entity) const;

    template <typename T>
    /// Checks whether an entity has a specific component.
    ///
    /// # Type Parameters
    /// * `T` - Component type to check for.
    ///
    /// # Arguments
    /// * `entity` - Must be valid (asserts via UZ_CHECK).
    ///
    /// # Returns
    /// `true` if the entity has a component of type T.
    [[nodiscard]] bool HasComponent(Entity entity) const;

    // ── Iteration ───────────────────────────────────────────────────────

    template <typename... Ts>
    /// Returns an EnTT view for iterating entities with the specified components.
    ///
    /// This exposes EnTT's view API directly — no additional wrapping.
    /// Use `entt::view` for single-component queries and consider `entt::group`
    /// for performance-critical multi-component hot paths.
    ///
    /// # Type Parameters
    /// * `Ts...` - Component types to include in the view.
    ///
    /// # Returns
    /// An `entt::view` that iterates all entities having all specified components.
    ///
    /// # Example
    /// ```cpp
    /// for (auto [enttEntity, transform, mesh] : world.View<CTransform, CMeshRenderer>().each()) {
    ///     // process transform and mesh
    /// }
    /// ```
    [[nodiscard]] auto View();

    template <typename... Ts>
    /// Returns a const EnTT view for read-only iteration.
    [[nodiscard]] auto View() const;

    // ── Queries ─────────────────────────────────────────────────────────

    /// Returns the number of currently alive entities.
    ///
    /// # Returns
    /// Count of entities that have been created and not yet destroyed.
    [[nodiscard]] size_t EntityCount() const;

    /// Destroys all entities and their components.
    ///
    /// After this call, all previously returned Entity handles become stale.
    ///
    /// # Side Effects
    /// Logs at Info level the number of entities cleared.
    void Clear();

    /// Returns a mutable reference to the underlying EnTT registry.
    ///
    /// Prefer the World API for entity/component operations. Direct registry access
    /// is provided for advanced use cases (custom queries, signals, observers).
    ///
    /// # Warning
    /// Creating or destroying entities directly via the registry bypasses generation
    /// tracking and will cause stale-handle bugs. Only use for component queries.
    [[nodiscard]] entt::registry& GetRegistry() { return registry_; }

    /// Returns a const reference to the underlying EnTT registry.
    [[nodiscard]] const entt::registry& GetRegistry() const { return registry_; }

    /// Reconstructs a valid Entity handle from a raw EnTT entity.
    ///
    /// This is intended for use in iteration loops where the caller obtained an
    /// `entt::entity` from a registry view and needs a fully-formed generational
    /// `Entity` handle (with correct generation) to pass back to Lua or other API.
    ///
    /// # Arguments
    /// * `enttEntity` — A live entt entity obtained from a registry view or storage iterator.
    ///
    /// # Returns
    /// An `Entity` with `id = entt::to_integral(enttEntity)` and the current generation
    /// for that slot. The returned handle will pass `World::IsValid()` as long as the
    /// underlying entity is still alive.
    [[nodiscard]] Entity EntityFromEntt(entt::entity enttEntity) const {
        const uint32_t packed = static_cast<uint32_t>(entt::to_integral(enttEntity));
        const uint32_t slot   = SlotOf(packed);
        Entity e;
        e.id = packed;
        e.generation = (slot < generations_.size()) ? generations_[slot] : 0u;
        return e;
    }

    /// Number of entity slots ever allocated (bounded-memory contract / diagnostics).
    /// Always <= the registry's entity storage size; O(slots), never O(slots x versions).
    [[nodiscard]] size_t SlotCount() const noexcept { return generations_.size(); }

private:
    entt::registry registry_;

    /// Per-slot World generation, indexed by the SLOT index
    /// (`entt::to_entity` of the packed id) - never by the packed id itself.
    /// Size == number of entity slots ever allocated; O(slots), not O(slots x versions).
    std::vector<uint32_t> generations_;

    /// Extracts the slot index (entt index bits) from a packed entity id.
    [[nodiscard]] static uint32_t SlotOf(uint32_t packedId) noexcept {
        return static_cast<uint32_t>(entt::to_entity(static_cast<entt::entity>(packedId)));
    }

    /// Converts an Entity handle to the underlying entt::entity.
    ///
    /// # Arguments
    /// * `entity` - Must have a valid id (not UINT32_MAX).
    ///
    /// # Returns
    /// The corresponding `entt::entity` value.
    [[nodiscard]] entt::entity ToEntt(Entity entity) const;

    /// Ensures the generations_ vector is large enough for the given slot index.
    void EnsureGenerationSlot(uint32_t slot);
};

// ─────────────────────────────────────────────────────────────────────────────
// Template Implementations
// ─────────────────────────────────────────────────────────────────────────────

template <typename T, typename... Args>
T& World::AddComponent(Entity entity, Args&&... args) {
    UZ_CHECK(IsValid(entity));
    const auto entt = ToEntt(entity);
    // If the entity already has this component, remove it first and re-emplace.
    // This provides overwrite semantics while staying compatible with non-copyable
    // component types (SceneComponent, etc.) that cannot use emplace_or_replace.
    if (registry_.all_of<T>(entt)) {
        registry_.erase<T>(entt);
    }
    return registry_.emplace<T>(entt, std::forward<Args>(args)...);
}

template <typename T>
void World::RemoveComponent(Entity entity) {
    UZ_CHECK(IsValid(entity));
    auto enttEntity = ToEntt(entity);
    if (!registry_.all_of<T>(enttEntity)) {
        UZ_LOG(ECS, Warning, "RemoveComponent: entity {} does not have the requested component", entity.id);
        return;
    }
    registry_.remove<T>(enttEntity);
}

template <typename T>
T& World::GetComponent(Entity entity) {
    UZ_CHECK(IsValid(entity));
    auto enttEntity = ToEntt(entity);
    UZ_CHECK(registry_.all_of<T>(enttEntity));
    return registry_.get<T>(enttEntity);
}

template <typename T>
const T& World::GetComponent(Entity entity) const {
    UZ_CHECK(IsValid(entity));
    auto enttEntity = ToEntt(entity);
    UZ_CHECK(registry_.all_of<T>(enttEntity));
    return registry_.get<T>(enttEntity);
}

template <typename T>
bool World::HasComponent(Entity entity) const {
    UZ_CHECK(IsValid(entity));
    return registry_.all_of<T>(ToEntt(entity));
}

template <typename... Ts>
auto World::View() {
    return registry_.view<Ts...>();
}

template <typename... Ts>
auto World::View() const {
    return registry_.view<Ts...>();
}

} // namespace uz
