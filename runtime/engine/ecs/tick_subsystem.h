/// TickSubsystem — ISubsystem adapter that drives the TickSystem each frame.
///
/// Owns a `TickSystem` and dispatches all tick groups in order during its
/// `Tick()` callback. Registered in the `Start` phase with `TickGroup::Update`.
///
/// # Tick Order
/// `TickAllGroups()` dispatches groups in fixed order:
///   PrePhysics → PostPhysics → Update → PostUpdate
///
/// # Thread Safety
/// Main thread only. All tick callbacks are synchronous.

#pragma once

#include "tick_system.h"
#include "subsystem.h"

namespace uz {

class World; // forward declaration

// ─────────────────────────────────────────────────────────────────────────────
// TickSubsystem
// ─────────────────────────────────────────────────────────────────────────────

/// Subsystem that owns and drives the TickSystem.
///
/// # Lifecycle
/// - `Init()`: Creates the TickSystem. Requires a World pointer to be set via `SetWorld()`.
/// - `Tick()`: Dispatches all tick groups in order.
/// - `Shutdown()`: Clears all registered tickables.
class TickSubsystem final : public ISubsystem {
public:
    TickSubsystem() = default;
    ~TickSubsystem() override = default;

    // Non-copyable, non-movable (inherited from ISubsystem).

    /// Returns "Tick".
    [[nodiscard]] const char* GetName() const override { return "Tick"; }

    /// Returns `InitPhase::Start` — needs World to be ready.
    [[nodiscard]] InitPhase GetInitPhase() const override { return InitPhase::Start; }

    /// Returns `TickGroup::Update` — the subsystem ticks during Update.
    ///
    /// Note: internally it dispatches ALL groups in order, not just Update.
    /// Using Update as the subsystem's own group ensures it runs after
    /// physics subsystems that might use PrePhysics/PostPhysics.
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::Update; }

    /// Depends on "World" subsystem.
    [[nodiscard]] std::vector<const char*> GetDependencies() const override { return {"World"}; }

    /// Initializes the tick subsystem.
    ///
    /// # Returns
    /// Always true. TickSystem construction cannot fail.
    [[nodiscard]] bool Init() override;

    /// Clears all tickables and releases resources.
    void Shutdown() override;

    /// Dispatches all tick groups in order for the current frame.
    ///
    /// # Arguments
    /// * `deltaTime` - Frame delta in seconds.
    void Tick(float deltaTime) override;

    /// Sets the World pointer used for entity validity checks during tick.
    ///
    /// Must be called after the World is created and before the first tick.
    ///
    /// # Arguments
    /// * `world` - Non-null pointer to the active World.
    void SetWorld(World* world) { m_world = world; }

    /// Returns a mutable reference to the owned TickSystem.
    ///
    /// Use this to register/unregister tickable components.
    [[nodiscard]] TickSystem& GetTickSystem() { return m_tickSystem; }

    /// Returns a const reference to the owned TickSystem.
    [[nodiscard]] const TickSystem& GetTickSystem() const { return m_tickSystem; }

private:
    TickSystem m_tickSystem;
    World* m_world = nullptr;
};

} // namespace uz
