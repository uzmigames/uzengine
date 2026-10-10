#pragma once

/// VFX System -- central manager for all active GPU particle emitters.
///
/// Owns the global ParticleBudget and tracks registered emitters. Tick()
/// advances each emitter's registration clock and aggregates alive-count
/// stats; per-frame spawn counts are computed (exactly once) by the frame's
/// VFX simulation dispatch, which also issues the compute work for the
/// emitters returned by GetActiveEmitters().
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
/// - Advance emitter registration clocks and aggregate alive stats (Tick)
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

    // Non-copyable, non-movable (ParticleBudget contains std::atomic).
    // Always held via std::unique_ptr — move semantics are not needed.
    VFXSystem(const VFXSystem&) = delete;
    VFXSystem& operator=(const VFXSystem&) = delete;
    VFXSystem(VFXSystem&&) = delete;
    VFXSystem& operator=(VFXSystem&&) = delete;

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

    /// Ticks all registered emitters: advances `timeSinceRegistration`, polls
    /// the alive-count readback and aggregates stats. Does NOT consume the
    /// spawn accumulator — `CalculateSpawnCount` has a single caller (the
    /// frame VFX simulation dispatch) to avoid double-counted spawns.
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

    // ── Runtime scalability state (driven by `r.VFX.*` CVars) ───────────
    //
    // The CVar values are sampled once per `Tick()` and cached on the
    // VFXSystem so per-frame inline dispatch / render code does not have
    // to hit the registry hash lookup hundreds of times. Getters return
    // the values cached on the most-recent Tick.

    /// Returns `true` when GPU compute simulation is allowed.
    /// Mirror of `r.VFX.AllowGPUSim` after `Tick()`.
    [[nodiscard]] bool IsGPUSimAllowed() const noexcept { return allowGPUSim_; }

    /// Returns the current quality level (0=low, 1=medium, 2=high, 3=cinematic).
    /// Mirror of `r.VFX.QualityLevel`.
    [[nodiscard]] uint32_t GetQualityLevel() const noexcept { return qualityLevel_; }

    /// Returns the spawn-rate multiplier for the active quality level.
    /// 0.25 (low) -> 0.5 (medium) -> 1.0 (high) -> 1.5 (cinematic).
    [[nodiscard]] float GetQualitySpawnRateMultiplier() const noexcept;

    /// Returns the LOD-distance multiplier for the active quality level.
    /// 0.5 (low) -> 0.75 (medium) -> 1.0 (high) -> 1.5 (cinematic).
    [[nodiscard]] float GetQualityLodDistanceMultiplier() const noexcept;

    /// Returns `true` when global VFX simulation is paused
    /// (`r.VFX.SimulatePaused = 1`). Render still runs — only sim dt is
    /// short-circuited to zero.
    [[nodiscard]] bool IsSimulationPaused() const noexcept { return simulationPaused_; }

    /// Returns the global VFX dt multiplier (`r.VFX.TimeScale`). Default 1.0.
    /// Slow-mo: 0..1; fast-forward: > 1; zero: same effect as
    /// `IsSimulationPaused()` but also short-circuits emitter timeScale.
    [[nodiscard]] float GetTimeScale() const noexcept { return timeScale_; }

    /// Looks up a registered emitter by name. Returns `nullptr` when no
    /// emitter has the given name. Used by the public C API for
    /// `uz_vfx_seek`/`uz_vfx_pause`/etc to address emitters from outside
    /// the engine without exposing raw pointers.
    [[nodiscard]] ParticleEmitter* FindEmitterByName(const std::string& name) const noexcept;

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

    /// Stored so `Tick` can poll the per-emitter `aliveCount` readback
    /// ring without plumbing a device pointer through every call site.
    IRHIDevice* device_ = nullptr;

    // ── Cached scalability state (sampled once per `Tick()` from CVars) ──
    bool     allowGPUSim_      = true;   ///< r.VFX.AllowGPUSim
    uint32_t qualityLevel_     = 2u;     ///< r.VFX.QualityLevel (0..3, default High)
    bool     simulationPaused_ = false;  ///< r.VFX.SimulatePaused
    float    timeScale_        = 1.0f;   ///< r.VFX.TimeScale
};

} // namespace uz
