#pragma once

/// UzEngine Subsystem Registry
///
/// Manages the lifecycle of all engine subsystems: registration, dependency-aware
/// initialization (topological sort), ordered shutdown, and per-frame ticking.
///
/// # Initialization Order
/// 1. Subsystems are grouped by `InitPhase` (PreInit -> Init -> Start).
/// 2. Within each phase, Kahn's algorithm produces a topological order
///    respecting `GetDependencies()`.
/// 3. On circular dependency, `InitAll()` logs the cycle and returns false.
///
/// # Shutdown Order
/// Strict reverse of initialization order, guaranteeing that dependents
/// shut down before their dependencies.
///
/// # Example
/// ```cpp
/// SubsystemRegistry registry;
/// registry.Register(std::make_unique<LoggingSubsystem>());
/// registry.Register(std::make_unique<WindowSubsystem>());
/// registry.Register(std::make_unique<RendererSubsystem>());
/// if (!registry.InitAll()) { /* handle failure */ }
/// // ... game loop: registry.TickGroup(TickGroup::Update, dt);
/// registry.ShutdownAll();
/// ```

#include "subsystem.h"
#include "log.h"

#include <memory>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(Lifecycle);

namespace uz {

/// Owns and orchestrates all engine subsystems through their lifecycle.
///
/// # Thread Safety
/// Not thread-safe. All methods must be called from the main thread.
///
/// # Ownership
/// Takes ownership of subsystems via `std::unique_ptr`. Subsystems are destroyed
/// after `ShutdownAll()` completes (or when the registry itself is destroyed).
class SubsystemRegistry {
public:
    SubsystemRegistry() = default;
    ~SubsystemRegistry();

    /// Non-copyable, non-movable (subsystem pointers are cached in initOrder_).
    SubsystemRegistry(const SubsystemRegistry&) = delete;
    SubsystemRegistry& operator=(const SubsystemRegistry&) = delete;
    SubsystemRegistry(SubsystemRegistry&&) = delete;
    SubsystemRegistry& operator=(SubsystemRegistry&&) = delete;

    /// Registers a subsystem for lifecycle management.
    ///
    /// Must be called before `InitAll()`. Registering after init is undefined behavior.
    ///
    /// # Arguments
    /// * `subsystem` - Unique ownership of the subsystem. Must not be null.
    ///
    /// # Panics
    /// Asserts if `subsystem` is null or if a subsystem with the same name already exists.
    void Register(std::unique_ptr<ISubsystem> subsystem);

    /// Initializes all registered subsystems in dependency-sorted, phase-grouped order.
    ///
    /// # Returns
    /// - `true` if all subsystems initialized successfully.
    /// - `false` if a circular dependency was detected or any subsystem's `Init()` returned false.
    ///
    /// # Side Effects
    /// - Logs each subsystem's init start/completion with timing.
    /// - On failure, shuts down all already-initialized subsystems in reverse order.
    ///
    /// # Errors
    /// - Circular dependency: logs the names of all involved subsystems.
    /// - Init failure: logs which subsystem failed and rolls back.
    [[nodiscard]] bool InitAll();

    /// Shuts down all initialized subsystems in reverse init order.
    ///
    /// Safe to call multiple times (no-op after the first call).
    /// Safe to call even if `InitAll()` was never called or failed partway.
    void ShutdownAll();

    /// Ticks all subsystems belonging to the specified tick group.
    ///
    /// # Arguments
    /// * `group` - Which tick group to execute.
    /// * `deltaTime` - Frame delta time in seconds (already time-scaled).
    ///
    /// # Behavior
    /// Iterates subsystems in init order, calling `Tick(dt)` on those whose
    /// `GetTickGroup()` matches `group`.
    void RunTickGroup(enum TickGroup group, float deltaTime);

    /// Finds a subsystem by name.
    ///
    /// # Arguments
    /// * `name` - The subsystem name (as returned by `GetName()`).
    ///
    /// # Returns
    /// - Pointer to the subsystem if found.
    /// - `nullptr` if no subsystem with that name is registered.
    [[nodiscard]] ISubsystem* Find(const char* name) const;

    /// Returns the topologically sorted init order.
    ///
    /// Only valid after a successful `InitAll()` call. Empty before that.
    ///
    /// # Returns
    /// Reference to the internal init-order vector. Do not store across `Register()` calls.
    [[nodiscard]] const std::vector<ISubsystem*>& GetInitOrder() const { return initOrder_; }

    /// Returns the number of registered subsystems.
    [[nodiscard]] size_t GetCount() const { return subsystems_.size(); }

private:
    /// All registered subsystems, in registration order.
    std::vector<std::unique_ptr<ISubsystem>> subsystems_;

    /// Topologically sorted init order (pointers into subsystems_).
    /// Populated by InitAll().
    std::vector<ISubsystem*> initOrder_;

    /// True if ShutdownAll() has been called (prevents double shutdown).
    bool shutdownCalled_ = false;
};

} // namespace uz
