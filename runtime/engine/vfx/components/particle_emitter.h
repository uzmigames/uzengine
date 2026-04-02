#pragma once

/// GPU-driven Particle Emitter ECS component.
///
/// Attached to entities to emit particles. Each emitter owns a GPUParticleBuffer
/// and is managed by VFXSystem. The compute shader handles all simulation;
/// the CPU only provides configuration and spawn count.
///
/// # Lifecycle
/// 1. Add ParticleEmitter component to entity
/// 2. Call `Initialize(device, budget)` to allocate GPU buffers
/// 3. VFXSystem ticks emitters each frame (compute dispatch)
/// 4. Call `Shutdown(device, budget)` to release GPU buffers
///
/// # Thread Safety
/// Not thread-safe. All methods must be called on the main thread.
/// GPU buffer contents are only accessed by the compute shader.
///
/// # Example
/// ```cpp
/// ParticleEmitter emitter;
/// emitter.maxParticles = 10000;
/// emitter.emissionRate = 500.0f;
/// emitter.Initialize(device, budget);
/// ```

#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/events/vfx_event.h"
#include "engine/vfx/lod/vfx_lod.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/parameters/vfx_parameter_store.h"
#include "vector3.h"

#include <cstdint>
#include <string>

namespace uz {

class IRHIDevice;

// ---------------------------------------------------------------------------
// ParticleEmitter -- GPU particle emitter ECS component (pure data + methods)
// ---------------------------------------------------------------------------

/// GPU-driven particle emitter attached to scene entities.
///
/// Contains emitter configuration (rates, velocities, forces) and runtime
/// state (GPU buffers, spawn accumulator). The compute shader performs all
/// per-particle simulation; the CPU side only computes how many particles
/// to spawn each frame and provides the configuration constants.
///
/// # Budget Integration
/// On `Initialize()`, the emitter requests `maxParticles` from the global
/// `ParticleBudget`. The budget may grant fewer particles than requested
/// if the global cap is nearly exhausted. The granted count becomes the
/// actual GPU buffer capacity (`buffer.capacity`).
///
/// # Spawn Accumulator
/// The `spawnAccumulator` carries fractional particles between frames.
/// At 100 particles/sec with a 16ms frame, `emissionRate * dt = 1.6`.
/// Frame N spawns 1 particle and carries 0.6 to Frame N+1, which then
/// spawns 2 particles (1.6 + 0.6 = 2.2, carry 0.2). This prevents
/// emission stutter at low rates.
struct ParticleEmitter {
    // -- Configuration (set by user, read by compute shader) ------------------

    /// Maximum particles this emitter can have alive simultaneously.
    /// The actual capacity may be lower if the budget is constrained.
    uint32_t maxParticles = 10000;

    /// Particle spawn rate (particles per second).
    /// Set to 0 to stop emission without disabling the emitter
    /// (existing particles continue to simulate).
    float emissionRate = 100.0f;

    /// Base particle lifespan in seconds.
    float lifetime = 5.0f;

    /// Random lifespan variation (subtracted from base lifetime).
    /// Actual lifespan per particle: `lifetime - random(0, lifetimeVariation)`.
    /// Clamped so the result is never negative.
    float lifetimeVariation = 0.0f;

    /// Initial spawn velocity direction and magnitude (world space, cm/s).
    /// Z-up: positive Z = upward.
    Vec3 spawnVelocity = {0.0f, 0.0f, 1000.0f};

    /// Random velocity spread magnitude added to spawnVelocity.
    /// Each component gets `random(-velocityVariation, +velocityVariation)`.
    float velocityVariation = 1.0f;

    /// Spread cone half-angle in degrees (0 = straight line, 90 = hemisphere).
    float spreadAngle = 30.0f;

    /// Soft particle fade distance in world units (cm).
    /// Controls the range over which particles fade when intersecting scene depth.
    /// Larger values produce a softer, more gradual fade. Zero disables soft particles.
    /// @ref Lorach 2007 "Soft Particles" — depth-based alpha fade at geometry intersections.
    float softParticleFadeRange = 100.0f;

    /// Gravity acceleration applied each frame (world space, cm/s^2).
    /// Z-up: negative Z = downward. 981 cm/s^2 = 9.81 m/s^2.
    Vec3 gravity = {0.0f, 0.0f, -981.0f};

    /// Velocity damping per frame (0 = full stop, 1 = no damping).
    /// Applied multiplicatively: `velocity *= damping` each tick.
    float damping = 0.98f;

    /// Whether the emitter is currently spawning new particles.
    /// When false, existing particles continue to simulate until they die.
    bool isEmitting = true;

    /// Path to the VFX graph asset that defines this emitter's behavior.
    /// Used by Lua `uz.vfx:spawn()` to match emitters by graph type.
    /// Example: "effects/fire/campfire.uzfx"
    std::string graphPath;

    /// Priority for budget allocation (0 = lowest, 100 = highest).
    /// Higher-priority emitters are allocated first when the budget is tight.
    uint8_t priority = 50;

    /// Per-emitter LOD distance settings.
    /// Used by VFXSystem to evaluate LOD level each frame.
    VFXLodSettings lodSettings;

    /// Whether LOD evaluation is enabled for this emitter.
    /// When false, the emitter always runs at Full quality.
    bool lodEnabled = true;

    /// Current LOD level (set by VFXSystem each frame, read-only for scripts).
    VFXLodLevel currentLodLevel = VFXLodLevel::Full;

    // -- Runtime State (managed by VFXSystem, do not modify directly) ---------

    /// GPU particle storage (ping-pong SoA buffers + shared bookkeeping).
    GPUParticleBuffer buffer;

    /// Number of particles currently alive (CPU-side mirror of GPU counter).
    /// Updated once per frame by reading back from `buffer.aliveCount`.
    uint32_t cpuAliveCount = 0;

    /// Fractional spawn accumulator for sub-frame emission smoothing.
    /// Carries leftover fractional particles to the next frame.
    float spawnAccumulator = 0.0f;

    /// Delay before this emitter starts spawning (seconds).
    /// Used by composite effects to stagger sub-emitter activation.
    /// When > 0, the emitter is registered but does not spawn until
    /// timeSinceRegistration >= startDelay.
    float startDelay = 0.0f;

    /// Time elapsed since registration (seconds).
    /// Incremented each frame by VFXSystem::Tick(). Emission begins
    /// when this value reaches or exceeds startDelay.
    float timeSinceRegistration = 0.0f;

    /// Whether this emitter has been initialized (GPU buffers allocated).
    bool initialized = false;

    /// Number of particles actually allocated from the budget.
    /// May be less than maxParticles if the budget was constrained.
    uint32_t allocatedFromBudget = 0;

    // -- Module Stack ---------------------------------------------------------

    /// Module stack defining this emitter's spawn/update/render behavior.
    /// When empty (no modules), falls back to the hardcoded shaders from 3.60.0.
    EmitterModuleStack moduleStack;

    // -- Parameters -----------------------------------------------------------

    /// Per-instance parameter store for graph-driven emitters.
    /// Parameters are uploaded to the GPU constant buffer each frame.
    VFXParameterStore parameters;

    // -- Event Queue ----------------------------------------------------------

    /// Per-emitter event queue for burst emission triggers.
    ///
    /// Gameplay code (C++ or Lua) calls `eventQueue.Emit("burst", count)`
    /// to schedule a one-shot particle burst. VFXSystem drains the queue
    /// each frame during the simulation pass.
    VFXEventQueue eventQueue;

    // -- Methods --------------------------------------------------------------

    /// Allocates GPU buffers from the particle budget.
    ///
    /// Creates the full GPUParticleBuffer (two SoA attribute sets + shared
    /// bookkeeping buffers). The budget may grant fewer particles than
    /// `maxParticles` if the global cap is nearly exhausted.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    /// - `budget`: Global particle budget to allocate from.
    ///
    /// # Returns
    /// `true` if allocation succeeded (at least 1 particle granted).
    /// `false` if the budget is fully exhausted or device is null.
    ///
    /// # Side Effects
    /// - Allocates GPU memory proportional to granted particle count.
    /// - Logs VFX/Info on success with granted count.
    /// - Logs VFX/Warning if budget grants fewer than requested.
    /// - Logs VFX/Error on failure.
    [[nodiscard]] bool Initialize(IRHIDevice* device, ParticleBudget& budget);

    /// Releases GPU buffers and returns allocation to budget.
    ///
    /// Safe to call even if not initialized (no-op in that case).
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer destruction. Must not be null.
    /// - `budget`: Global particle budget to return allocation to.
    ///
    /// # Side Effects
    /// - Frees GPU memory.
    /// - Logs VFX/Info with released particle count.
    void Shutdown(IRHIDevice* device, ParticleBudget& budget);

    /// Calculates how many particles to spawn this frame.
    ///
    /// Uses `emissionRate` and `dt`, carrying fractional particles via
    /// `spawnAccumulator`. Returns 0 if the emitter is not active or not
    /// initialized. The result is clamped to available buffer capacity.
    ///
    /// # Parameters
    /// - `dt`: Frame delta time in seconds. Must be >= 0.
    ///
    /// # Returns
    /// Integer number of particles to spawn this frame (may be 0).
    [[nodiscard]] uint32_t CalculateSpawnCount(float dt) noexcept;
};

} // namespace uz
