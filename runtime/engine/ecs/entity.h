#pragma once

/// UzEngine Entity Handle
///
/// A generational handle wrapping an EnTT entity. The generation counter enables
/// use-after-destroy detection: if an entity is destroyed and its slot reused,
/// stale handles will fail validation because their generation no longer matches.
///
/// # Thread Safety
/// Entity handles are plain data and can be freely copied across threads.
/// However, all operations that use an Entity (create, destroy, add/get component)
/// must be performed on the main thread via the World class.

#include <cstdint>
#include <functional>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Entity — generational handle for ECS entities
// ─────────────────────────────────────────────────────────────────────────────

/// Generational entity handle for use-after-destroy detection.
///
/// Wraps an entity ID and a generation counter. The World class increments
/// the generation each time a slot is reused, so stale handles automatically
/// become invalid without requiring explicit invalidation.
///
/// # Layout
/// - `id` — the entity slot index (maps to `entt::entity` under the hood)
/// - `generation` — monotonically increasing counter per slot
///
/// # Null Entity
/// `Entity::Invalid()` returns a sentinel with `id = UINT32_MAX` and `generation = 0`.
/// This is the default-constructed state and compares equal to all other invalid entities.
struct Entity {
    uint32_t id = UINT32_MAX;    ///< Entity slot index. UINT32_MAX = null/invalid.
    uint32_t generation = 0;     ///< Generation counter for use-after-destroy detection.

    /// Returns a null/invalid entity sentinel.
    ///
    /// # Returns
    /// An Entity with `id = UINT32_MAX` and `generation = 0`.
    [[nodiscard]] static constexpr Entity Invalid() { return Entity{}; }

    /// Checks whether this handle refers to a potentially valid entity.
    ///
    /// This is a local check only — it does NOT verify that the entity is
    /// still alive in the World. Use `World::IsValid(entity)` for full validation.
    ///
    /// # Returns
    /// `true` if `id != UINT32_MAX` (i.e., not the null sentinel).
    [[nodiscard]] constexpr bool IsValid() const { return id != UINT32_MAX; }

    /// Equality comparison. Two handles are equal iff both id and generation match.
    [[nodiscard]] constexpr bool operator==(const Entity& other) const {
        return id == other.id && generation == other.generation;
    }

    /// Inequality comparison.
    [[nodiscard]] constexpr bool operator!=(const Entity& other) const {
        return !(*this == other);
    }
};

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// std::hash specialization for Entity (enables use in unordered containers)
// ─────────────────────────────────────────────────────────────────────────────

template <>
struct std::hash<uz::Entity> {
    /// Hashes an Entity by combining id and generation.
    std::size_t operator()(const uz::Entity& e) const noexcept {
        return std::hash<uint64_t>{}(static_cast<uint64_t>(e.id) << 32 | e.generation);
    }
};
