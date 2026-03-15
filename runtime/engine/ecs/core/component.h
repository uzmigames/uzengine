/// Component base classes for UzEngine ECS.
///
/// Defines the three-level component hierarchy:
///   - `Component`      — base with lifecycle callbacks, entity/world references
///   - `ActorComponent` — logic-only component with no transform
///   - `SceneComponent` — component that participates in the scene hierarchy
///
/// # Lifecycle Order
/// OnRegister → OnInitialize → OnBeginPlay → OnEndPlay → OnUninitialize → OnUnregister
///
/// # Thread Safety
/// All lifecycle callbacks are called on the main thread only.
/// Component state is not thread-safe; do not mutate from worker threads.

#pragma once

#include "entity.h"

#include <cstdint>

namespace uz {

class World; // forward declaration — defined in world.h

// ─────────────────────────────────────────────────────────────────────────────
// ComponentState — lifecycle state machine
// ─────────────────────────────────────────────────────────────────────────────

/// Lifecycle state of a component attached to an entity.
///
/// States advance in a single direction:
/// `Unregistered → Registered → Initialized → Playing`
/// and reverse symmetrically during teardown.
enum class ComponentState : uint8_t {
    /// Component has been constructed but not yet attached to an entity.
    /// `m_entity` and `m_world` are null/invalid.
    Unregistered,

    /// Component has been attached to an entity and `OnRegister` has been called.
    /// Safe to read `GetEntity()` and `GetWorld()`.
    Registered,

    /// `OnInitialize` has been called. The world is preparing to begin play
    /// (e.g., loading is complete, all components are registered).
    Initialized,

    /// `OnBeginPlay` has been called. Gameplay systems are running.
    /// The component should participate in tick/updates.
    Playing,
};

// ─────────────────────────────────────────────────────────────────────────────
// Component — base class for all ECS components
// ─────────────────────────────────────────────────────────────────────────────

/// Base component class providing lifecycle callbacks and owner/world context.
///
/// All components in UzEngine inherit from this class. It is NOT meant to be
/// instantiated directly. Prefer `ActorComponent` for logic-only components
/// or `SceneComponent` for components that carry a spatial transform.
///
/// Components are stored in EnTT's sparse sets and looked up via
/// `World::GetComponent<T>(entity)`. Although components can have virtual
/// methods for extensibility, they should remain lightweight. Heavy data
/// (meshes, textures, audio buffers) is referenced by handle, not embedded.
///
/// # Accessing Siblings
/// ```cpp
/// void MyComponent::OnBeginPlay() {
///     auto* vel = GetWorld()->GetComponent<VelocityComponent>(GetEntity());
/// }
/// ```
///
/// # Thread Safety
/// All lifecycle methods are called on the main thread only. Do not call
/// lifecycle methods or mutate state from worker threads.
class Component {
public:
    /// Destructor. Virtual to allow safe polymorphic deletion.
    virtual ~Component() = default;

    // Non-copyable, non-movable. Components live inside EnTT sparse sets and
    // their addresses must remain stable after insertion.
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    // ── Lifecycle Callbacks ───────────────────────────────────────────────────

    /// Called immediately after the component is attached to an entity.
    ///
    /// `GetEntity()` and `GetWorld()` are valid here. Use to register
    /// event listeners or initialize references to sibling components.
    virtual void OnRegister() {}

    /// Called when the world finishes registering all components before play.
    ///
    /// All entities and components exist at this point. Safe to query the
    /// world for other entities. Use for cross-entity initialization that
    /// requires all components to be registered first.
    virtual void OnInitialize() {}

    /// Called when gameplay begins (e.g., scene starts running).
    ///
    /// Use to subscribe to gameplay events, start timers, or cache
    /// frequently-accessed component references. Called after all
    /// `OnInitialize()` callbacks have completed.
    virtual void OnBeginPlay() {}

    /// Called when gameplay ends (e.g., scene stops running, PIE ends).
    ///
    /// Mirror of `OnBeginPlay`. Unsubscribe events and stop any ongoing
    /// actions. After this call the component may still be queried but
    /// should not drive gameplay.
    virtual void OnEndPlay() {}

    /// Called when the world begins teardown after `OnEndPlay`.
    ///
    /// Mirror of `OnInitialize`. Release cross-entity references here.
    /// Do not access other entities after this returns.
    virtual void OnUninitialize() {}

    /// Called immediately before the component is detached from its entity.
    ///
    /// Mirror of `OnRegister`. Unregister event listeners and release any
    /// resources that were acquired in `OnRegister`. After this call the
    /// component is destroyed.
    virtual void OnUnregister() {}

    // ── Accessors ─────────────────────────────────────────────────────────────

    /// Returns the entity that owns this component.
    ///
    /// # Returns
    /// A valid `Entity` after `OnRegister` has been called.
    /// `Entity::Invalid()` if the component is in the `Unregistered` state.
    [[nodiscard]] Entity GetEntity() const noexcept { return m_entity; }

    /// Returns a non-owning pointer to the World this component belongs to.
    ///
    /// # Returns
    /// Non-null after `OnRegister`. Null if state is `Unregistered`.
    [[nodiscard]] World* GetWorld() const noexcept { return m_world; }

    /// Returns the current lifecycle state of this component.
    [[nodiscard]] ComponentState GetState() const noexcept { return m_state; }

    /// Returns whether this component is enabled.
    ///
    /// Disabled components are skipped by tick-dispatching systems.
    /// Lifecycle callbacks are NOT affected — they fire regardless.
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

    /// Enables or disables this component.
    ///
    /// # Arguments
    /// * `enabled` - `true` to enable (default), `false` to suppress ticking.
    void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

protected:
    /// Protected default constructor. Only concrete subclasses are instantiated.
    Component() = default;

private:
    // World sets these fields during lifecycle management.
    friend class World;

    /// The entity that owns this component. Set by World on registration.
    Entity m_entity = Entity::Invalid();

    /// Non-owning pointer to the owning World. Set by World on registration.
    World* m_world = nullptr;

    /// Current lifecycle state. Managed exclusively by World.
    ComponentState m_state = ComponentState::Unregistered;

    /// Whether this component participates in tick dispatch.
    bool m_enabled = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// ActorComponent — logic-only component, no spatial transform
// ─────────────────────────────────────────────────────────────────────────────

/// Logic-only component. Carries no transform and does not participate in
/// the scene hierarchy.
///
/// Use `ActorComponent` for:
/// - Movement controllers (velocity, steering)
/// - Input binding components
/// - AI/behaviour-tree controllers
/// - Game-state flags (health, team, faction)
///
/// To access a sibling component:
/// ```cpp
/// auto* health = GetWorld()->GetComponent<HealthComponent>(GetEntity());
/// ```
class ActorComponent : public Component {
    // No additional state. Inherits full Component lifecycle.
};

} // namespace uz
