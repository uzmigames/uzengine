#pragma once

/// VFX System -- central manager for all active GPU particle emitters.
///
/// Owns the global ParticleBudget and orchestrates per-frame spawn counting.
/// Each registered emitter gets its spawn count calculated during Tick().
/// Actual compute shader dispatch is handled by render passes that query
/// GetActiveEmitters().
///
/// # Lifecycle
/// ```
/// VFXSystem vfx;
/// vfx.Initialize(device);
/// vfx.RegisterEmitter(&emitter);
/// // Each frame:
/// vfx.Tick(dt);
/// // Render passes iterate vfx.GetActiveEmitters()
/// vfx.UnregisterEmitter(&emitter);
/// vfx.Shutdown(device);
/// ```
///
/// # Thread Safety
/// Not thread-safe. All methods must be called from the main/render thread.
/// Render passes access emitter data read-only during GPU submission.

#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/module_registry.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace uz {

class IRHIDevice;
struct ParticleEmitter;

// ---------------------------------------------------------------------------
// VFXSystem -- central VFX manager
// ---------------------------------------------------------------------------

/// Central VFX system managing all active particle emitters.
///
/// Owns the global ParticleBudget and orchestrates per-frame emitter updates.
/// The system does NOT own the emitters themselves -- they are typically stored
/// as ECS components and their lifetimes are managed by the entity system.
///
/// # Responsibilities
/// - Maintain the global particle budget
/// - Track registered emitters
/// - Calculate per-frame spawn counts (Tick)
/// - Provide emitter list to render passes
///
/// # Non-Responsibilities
/// - Does NOT dispatch compute shaders (render passes do this)
/// - Does NOT own emitter lifetime (ECS does)
/// - Does NOT handle emitter GPU readback (render passes do this)
class VFXSystem {
public:
    VFXSystem() = default;
    ~VFXSystem() = default;

    // Non-copyable (owns budget state), movable.
    VFXSystem(const VFXSystem&) = delete;
    VFXSystem& operator=(const VFXSystem&) = delete;
    VFXSystem(VFXSystem&&) noexcept = default;
    VFXSystem& operator=(VFXSystem&&) noexcept = default;

    /// Initializes the VFX system with a global particle budget.
    ///
    /// # Parameters
    /// - `device`: RHI device (stored for future use, e.g., shared resources).
    /// - `maxTotalParticles`: Global particle budget cap.
    ///   Default 500K (~60 MB GPU memory at ~120 bytes/particle).
    ///
    /// # Side Effects
    /// - Logs VFX/Info with budget size.
    void Initialize(IRHIDevice* device, uint32_t maxTotalParticles = 500000);

    /// Shuts down the VFX system and releases any owned resources.
    ///
    /// All emitters should be unregistered before calling this.
    /// Logs a warning if emitters are still registered at shutdown.
    ///
    /// # Parameters
    /// - `device`: RHI device for resource cleanup.
    void Shutdown(IRHIDevice* device);

    /// Registers an emitter for per-frame simulation.
    ///
    /// The emitter must already be initialized (GPU buffers allocated).
    /// Registering the same emitter twice is a no-op (logged as warning).
    ///
    /// # Parameters
    /// - `emitter`: Non-null pointer to an initialized ParticleEmitter.
    void RegisterEmitter(ParticleEmitter* emitter);

    /// Unregisters an emitter from per-frame simulation.
    ///
    /// Does NOT destroy the emitter or its GPU buffers -- the caller is
    /// responsible for calling `emitter->Shutdown()` separately.
    /// Unregistering a non-registered emitter is a no-op (logged as warning).
    ///
    /// # Parameters
    /// - `emitter`: Non-null pointer to a previously registered emitter.
    void UnregisterEmitter(ParticleEmitter* emitter);

    /// Ticks all registered emitters: calculates spawn counts for this frame.
    ///
    /// Does NOT dispatch compute -- that is done by the render passes which
    /// iterate `GetActiveEmitters()` and bind each emitters GPU resources.
    ///
    /// # Parameters
    /// - `dt`: Frame delta time in seconds. Must be >= 0.
    void Tick(float dt);

    /// Returns all currently registered emitters (for render pass iteration).
    ///
    /// # Returns
    /// Const reference to the internal emitter list. Valid until the next
    /// Register/Unregister call.
    [[nodiscard]] const std::vector<ParticleEmitter*>& GetActiveEmitters() const noexcept {
        return emitters_;
    }

    /// Returns the global particle budget (mutable access for emitter init).
    [[nodiscard]] ParticleBudget& GetBudget() noexcept { return budget_; }

    /// Returns the global particle budget (const access for stats).
    [[nodiscard]] const ParticleBudget& GetBudget() const noexcept { return budget_; }

    /// Returns the module registry (mutable access for asset loading).
    ///
    /// # Returns
    /// Reference to the owned ModuleRegistry. Only valid after Initialize().
    [[nodiscard]] ModuleRegistry& GetModuleRegistry() noexcept;

    /// Returns the module registry (const access for queries).
    ///
    /// # Returns
    /// Const reference to the owned ModuleRegistry. Only valid after Initialize().
    [[nodiscard]] const ModuleRegistry& GetModuleRegistry() const noexcept;

    /// Returns the number of currently registered emitters.
    [[nodiscard]] uint32_t GetActiveEmitterCount() const noexcept {
        return static_cast<uint32_t>(emitters_.size());
    }

    /// Returns the total alive particle count across all emitters.
    /// This is a CPU-side approximation updated each Tick().
    [[nodiscard]] uint32_t GetTotalAliveParticles() const noexcept {
        return totalAliveParticles_;
    }

    /// Returns true if the system has been initialized.
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    /// Built-in and user module factory registry.
    std::unique_ptr<ModuleRegistry> moduleRegistry_;

    /// Registered emitters (non-owning pointers).
    std::vector<ParticleEmitter*> emitters_;

    /// Global particle budget.
    ParticleBudget budget_;

    /// Cached total alive particles (updated each Tick).
    uint32_t totalAliveParticles_ = 0;

    /// Whether Initialize() has been called.
    bool initialized_ = false;
};

} // namespace uz
