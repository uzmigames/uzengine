#pragma once

/// UzEngine World Subsystem
///
/// ISubsystem wrapper that owns a `World` instance and integrates it into
/// the engine's lifecycle (init/shutdown/tick). Registered in the `Start` phase
/// since the ECS world is a gameplay-level construct that depends on core
/// engine systems being ready.
///
/// # Tick Group
/// Uses `TickGroup::None` — the World itself does not tick. Individual ECS systems
/// will be registered as their own subsystems or driven by a SystemScheduler in the future.

#include "world.h"
#include "subsystem.h"

#include <memory>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// WorldSubsystem — ISubsystem adapter for the ECS World
// ─────────────────────────────────────────────────────────────────────────────

/// Subsystem that owns and manages the ECS World lifecycle.
///
/// # Lifecycle
/// - `Init()`: Creates the World instance.
/// - `Shutdown()`: Clears and destroys the World.
/// - `Tick()`: No-op (TickGroup::None). Future: may drive a system scheduler.
///
/// # Access
/// Use `GetWorld()` to access the World after `Init()` has been called.
/// Returns nullptr before Init or after Shutdown.
class WorldSubsystem final : public ISubsystem {
public:
    WorldSubsystem() = default;
    ~WorldSubsystem() override = default;

    /// Returns "World".
    [[nodiscard]] const char* GetName() const override { return "World"; }

    /// Returns `InitPhase::Start` — world is created after core systems are ready.
    [[nodiscard]] InitPhase GetInitPhase() const override { return InitPhase::Start; }

    /// Returns `TickGroup::None` — the World does not tick on its own.
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::None; }

    /// No dependencies — the World is self-contained.
    [[nodiscard]] std::vector<const char*> GetDependencies() const override { return {}; }

    /// Creates the ECS World.
    ///
    /// # Returns
    /// Always returns `true`. World construction cannot fail.
    ///
    /// # Side Effects
    /// Logs at Info level.
    [[nodiscard]] bool Init() override;

    /// Clears all entities and destroys the World.
    ///
    /// # Side Effects
    /// Logs at Info level.
    void Shutdown() override;

    /// Returns a pointer to the owned World, or nullptr if not initialized.
    ///
    /// # Returns
    /// - Non-null pointer after `Init()` and before `Shutdown()`.
    /// - `nullptr` otherwise.
    [[nodiscard]] World* GetWorld() { return world_.get(); }

    /// Returns a const pointer to the owned World.
    [[nodiscard]] const World* GetWorld() const { return world_.get(); }

private:
    std::unique_ptr<World> world_;
};

} // namespace uz
