#pragma once

/// UzEngine Subsystem Interface
///
/// Defines the contract for engine subsystems participating in the application lifecycle.
/// Each subsystem declares its initialization phase, tick group, and dependencies,
/// enabling the registry to build a correct startup/shutdown/tick order.
///
/// # Lifecycle
/// 1. `Init()` — called once during startup, in topologically-sorted dependency order
/// 2. `Tick(dt)` — called every frame for subsystems with a tick group
/// 3. `Shutdown()` — called once during teardown, in reverse init order
///
/// # Example
/// ```cpp
/// class AudioSubsystem final : public ISubsystem {
/// public:
///     const char* GetName() const override { return "Audio"; }
///     InitPhase GetInitPhase() const override { return InitPhase::Init; }
///     TickGroup GetTickGroup() const override { return TickGroup::PostUpdate; }
///     std::vector<const char*> GetDependencies() const override { return {"Window"}; }
///     bool Init() override { /* ... */ return true; }
///     void Shutdown() override { /* ... */ }
///     void Tick(float deltaTime) override { /* ... */ }
/// };
/// ```

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// InitPhase — determines when a subsystem is initialized
// ─────────────────────────────────────────────────────────────────────────────

/// Initialization phase for a subsystem.
///
/// Subsystems are initialized in phase order (PreInit -> Init -> Start).
/// Within each phase, topological sort by dependencies determines the exact order.
enum class InitPhase : uint8_t {
    PreInit = 0, ///< CPU-only setup: logging, config, paths, VFS. No GPU calls allowed.
    Init = 1,    ///< GPU initialization: window creation, RHI device, renderer setup.
    Start = 2,   ///< World setup: scene loading, entity spawning, gameplay init.
};

// ─────────────────────────────────────────────────────────────────────────────
// TickGroup — determines when a subsystem is ticked each frame
// ─────────────────────────────────────────────────────────────────────────────

/// Tick group for per-frame execution ordering.
///
/// Subsystems are ticked in group order each frame. Within a group,
/// tick order matches the topological init order.
enum class TickGroup : uint8_t {
    PrePhysics = 0,  ///< Before physics step: input processing, animation sampling.
    PostPhysics = 1, ///< After physics step: physics queries, constraint resolution.
    Update = 2,      ///< Main game logic: AI, gameplay, scripting.
    PostUpdate = 3,  ///< Post-update: audio sync, UI layout, late transforms.
    None = 255,      ///< Subsystem does not tick. Used for init-only subsystems.
};

// ─────────────────────────────────────────────────────────────────────────────
// ISubsystem — interface for engine subsystems
// ─────────────────────────────────────────────────────────────────────────────

/// Interface for engine subsystems participating in the application lifecycle.
///
/// Every major engine system (renderer, audio, physics, input, etc.) implements
/// this interface to participate in ordered initialization, shutdown, and per-frame ticking.
///
/// # Thread Safety
/// All lifecycle methods (Init, Shutdown, Tick) are called from the main thread.
/// Subsystems that use worker threads must synchronize internally.
///
/// # Ownership
/// Subsystem instances are owned by `SubsystemRegistry` via `std::unique_ptr`.
/// The registry guarantees that `Shutdown()` is called before destruction.
class ISubsystem {
protected:
    ISubsystem() = default;

public:
    virtual ~ISubsystem() = default;
    ISubsystem(const ISubsystem&) = delete;
    ISubsystem& operator=(const ISubsystem&) = delete;
    ISubsystem(ISubsystem&&) = delete;
    ISubsystem& operator=(ISubsystem&&) = delete;

    /// Human-readable name used for logging, debugging, and dependency resolution.
    ///
    /// # Returns
    /// A non-null, static string identifying this subsystem (e.g., "Renderer", "Audio").
    /// The pointer must remain valid for the lifetime of the subsystem.
    [[nodiscard]] virtual const char* GetName() const = 0;

    /// Initialization phase this subsystem belongs to.
    ///
    /// # Returns
    /// The phase during which `Init()` should be called.
    [[nodiscard]] virtual InitPhase GetInitPhase() const = 0;

    /// Tick group this subsystem belongs to.
    ///
    /// # Returns
    /// - A `TickGroup` value to participate in per-frame ticking.
    /// - `TickGroup::None` if this subsystem should not be ticked (default).
    [[nodiscard]] virtual TickGroup GetTickGroup() const { return TickGroup::None; }

    /// Names of subsystems that must be initialized before this one.
    ///
    /// The registry uses these names to build a topological sort within each init phase.
    /// All named dependencies must exist and belong to the same or earlier init phase.
    ///
    /// # Returns
    /// A vector of subsystem names. Empty if no dependencies (default).
    ///
    /// # Performance
    /// Called once during `InitAll()`, not per-frame. Returning by value is acceptable.
    [[nodiscard]] virtual std::vector<const char*> GetDependencies() const { return {}; }

    /// Initializes the subsystem during the appropriate init phase.
    ///
    /// Called exactly once by the registry. Must be idempotent-safe (calling twice
    /// should not crash, though the registry guarantees single invocation).
    ///
    /// # Returns
    /// - `true` on success.
    /// - `false` on failure. The registry will abort initialization and log the error.
    ///
    /// # Side Effects
    /// May allocate resources, open files, create GPU objects, etc.
    [[nodiscard]] virtual bool Init() = 0;

    /// Shuts down the subsystem, releasing all resources.
    ///
    /// Called exactly once by the registry, in reverse init order.
    /// Must tolerate being called even if `Init()` was never called (e.g., if
    /// a prior subsystem's init failed and shutdown is rolling back).
    virtual void Shutdown() = 0;

    /// Per-frame tick called for subsystems with a tick group.
    ///
    /// Only called if `GetTickGroup() != TickGroup::None`.
    /// Called from the main thread in tick-group order.
    ///
    /// # Arguments
    /// * `deltaTime` - Elapsed time since last frame in seconds (already scaled by timeScale).
    virtual void Tick(float deltaTime) { (void)deltaTime; }
};

} // namespace uz
