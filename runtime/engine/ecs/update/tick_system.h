/// TickSystem — dispatches per-frame OnTick calls to TickableComponents.
///
/// Maintains flat vectors of tickable entries grouped by `TickGroup`.
/// The `Tick(group, dt)` method iterates a single group, checking entity
/// validity, enable state, and interval throttling before dispatching.
///
/// # Performance
/// No allocations on the tick hot path. Tickables are stored contiguously
/// per group for cache-friendly iteration. Registration/unregistration
/// may allocate or compact vectors.
///
/// # Thread Safety
/// All methods must be called from the main thread only.

#pragma once

#include "entity.h"
#include "subsystem.h"

#include <array>
#include <cstddef>
#include <vector>

namespace uz {

class TickableComponent; // forward declaration
class World;             // forward declaration

// ─────────────────────────────────────────────────────────────────────────────
// TickSystem
// ─────────────────────────────────────────────────────────────────────────────

/// Manages tick dispatch for all registered TickableComponents in a World.
///
/// # Usage
/// ```cpp
/// TickSystem tickSys;
/// tickSys.RegisterTickable(entity, &myTickable, world);
/// // Each frame:
/// tickSys.Tick(TickGroup::PrePhysics, dt, world);
/// tickSys.Tick(TickGroup::PostPhysics, dt, world);
/// tickSys.Tick(TickGroup::Update, dt, world);
/// tickSys.Tick(TickGroup::PostUpdate, dt, world);
/// ```
class TickSystem {
public:
    TickSystem() = default;

    /// Registers a tickable component for tick dispatch.
    ///
    /// The component is placed into the vector for its declared `GetTickGroup()`.
    /// Duplicate registrations for the same entity are ignored with a warning.
    ///
    /// # Arguments
    /// * `entity` - The entity owning the component. Must be valid.
    /// * `comp` - Non-null pointer to the tickable component. Must outlive registration.
    /// * `world` - The World used for entity validity checks.
    void RegisterTickable(Entity entity, TickableComponent* comp, const World& world);

    /// Unregisters a tickable component by entity.
    ///
    /// Removes the entry from its tick group. No-op if not found.
    ///
    /// # Arguments
    /// * `entity` - The entity to unregister.
    void UnregisterTickable(Entity entity);

    /// Dispatches OnTick for all tickable components in the given group.
    ///
    /// Skips components that are:
    /// - Disabled (`IsTickEnabled() == false`)
    /// - Owned by invalid/destroyed entities
    /// - Not yet due for their interval-based tick
    ///
    /// # Arguments
    /// * `group` - The tick group to dispatch.
    /// * `deltaTime` - Frame delta time in seconds.
    /// * `world` - The World used for entity validity checks.
    void Tick(TickGroup group, float deltaTime, const World& world);

    /// Returns the total number of registered tickables across all groups.
    [[nodiscard]] size_t GetTickableCount() const;

    /// Returns the number of registered tickables in a specific group.
    [[nodiscard]] size_t GetTickableCount(TickGroup group) const;

    /// Removes all registered tickables.
    void Clear();

private:
    /// Number of tick groups (PrePhysics, PostPhysics, Update, PostUpdate).
    static constexpr size_t kGroupCount = 4;

    /// Internal entry tracking a registered tickable.
    struct TickEntry {
        Entity entity;
        TickableComponent* component = nullptr;
    };

    /// One vector per tick group, indexed by the TickGroup enum value.
    std::array<std::vector<TickEntry>, kGroupCount> m_groups;

    /// Maps a TickGroup enum to an array index. Returns kGroupCount for None.
    [[nodiscard]] static size_t GroupIndex(TickGroup group);
};

} // namespace uz
