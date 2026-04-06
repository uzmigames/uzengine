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
class MaterialInterface;

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

    /// World-space position of the emitter (synced from entity TransformComponent).
    Vec3 worldPosition = {0.0f, 0.0f, 0.0f};

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

    // ── Billboard Rendering Parameters ────────────────────────────────────

    /// Emissive multiplier (additive glow contribution). 0 = disabled.
    float emissiveIntensity = 0.0f;

    /// 0=Unlit (default), 1=Lit (NdotL directional + ambient).
    uint32_t litMode = 0;

    /// Spawn radius for shape-based spawn (cm). 0 = point spawn.
    float spawnRadius = 0.0f;

    /// Initial billboard size in world units (cm).
    float initialSize = 10.0f;

    /// Initial particle color (RGBA, linear).
    Vec4 initialColor = {1.0f, 0.9f, 0.6f, 1.0f};

    /// Spawn shape: 0=Point, 1=Sphere, 2=Box, 3=Ring, 4=Cone, 5=Cylinder, 6=Disc, 7=Line.
    uint32_t spawnShape = 0;

    /// Camera offset — push/pull along camera forward (cm). 0 = disabled.
    float cameraOffset = 0.0f;
    /// Sprite pivot X [-1,1]. 0 = center.
    float pivotOffsetX = 0.0f;
    /// Sprite pivot Y [-1,1]. 0 = center.
    float pivotOffsetY = 0.0f;

    /// Size by speed parameters.
    uint32_t sizeBySpeedEnabled = 0;
    float sizeBySpeedMin = 0.5f;
    float sizeBySpeedMax = 2.0f;
    float speedRangeMax = 1000.0f;

    /// Size over lifetime (piecewise linear: start→peak→end).
    float sizeOverLifeStart = 1.0f;
    float sizeOverLifePeak = 1.0f;
    float sizeOverLifeEnd = 0.0f;
    float sizeOverLifePeakTime = 0.3f;

    /// Color over lifetime (linear interpolation start→end).
    float colorLifeStartR = 1.0f, colorLifeStartG = 1.0f, colorLifeStartB = 1.0f, colorLifeStartA = 1.0f;
    float colorLifeEndR = 1.0f, colorLifeEndG = 1.0f, colorLifeEndB = 1.0f, colorLifeEndA = 0.0f;

    /// Billboard facing mode: 0=FaceCamera, 1=VelocityAligned, 2=FaceCameraPlane, 3=Custom, 4=FaceCameraPosition.
    uint32_t facingMode = 0;
    float customFacingX = 0.0f, customFacingY = 0.0f, customFacingZ = 1.0f;

    /// Velocity stretch multiplier. 0 = no stretch.
    float stretchFactor = 0.0f;

    /// Blend mode: 0=AlphaBlend, 1=Additive, 2=PremulAlpha.
    uint32_t blendMode = 0;

    /// Color by speed (reuses colorLifeStart/End with speed instead of age).
    uint32_t colorBySpeedEnabled = 0;

    /// Screen-space size mode (constant pixel coverage).
    uint32_t screenSpaceSizeEnabled = 0;
    float screenSpacePixelSize = 8.0f;

    // ── Simulation Force Parameters ────────────────────────────────────

    /// Gravity acceleration applied each frame (world space, cm/s^2).
    Vec3 gravity = {0.0f, 0.0f, -981.0f};

    /// Velocity damping per frame (0 = full stop, 1 = no damping).
    /// Applied multiplicatively: `velocity *= damping` each tick.
    float damping = 0.98f;

    /// Curl noise force parameters.
    float curlStrength = 0.0f;    ///< 0 = disabled.
    float curlFrequency = 0.5f;
    float curlSpeed = 0.2f;

    /// Vortex force parameters.
    float vortexAxisOriginX = 0.0f, vortexAxisOriginY = 0.0f, vortexAxisOriginZ = 0.0f;
    float vortexAxisDirX = 0.0f, vortexAxisDirY = 0.0f, vortexAxisDirZ = 1.0f;
    float vortexTangentialStrength = 0.0f;
    float vortexRadialStrength = 0.0f;
    float vortexMaxRadius = 500.0f;

    /// Ground collision. 0=disabled, 1=bounce.
    uint32_t groundCollision = 0;
    float restitution = 0.3f;
    uint32_t collisionResponse = 0;  ///< 0=Bounce, 1=Stick, 2=Kill.
    float collisionFriction = 0.8f;

    /// Orbit around point.
    uint32_t orbitEnabled = 0;
    float orbitRadius = 0.0f;
    float orbitSpeed = 0.0f;
    float orbitAxisX = 0.0f, orbitAxisY = 0.0f, orbitAxisZ = 1.0f;

    /// Simple wind (direction + strength). 0 strength = disabled.
    float windDirX = 1.0f, windDirY = 0.0f, windDirZ = 0.0f;
    float windStrength = 0.0f;

    /// Quadratic drag (air resistance). 0 = disabled.
    float dragCoefficient = 0.0f;

    /// Per-axis acceleration (independent of gravity).
    float accelX = 0.0f, accelY = 0.0f, accelZ = 0.0f;

    /// Rotation rate (continuous spin, rad/s). 0 = no spin.
    float rotationRate = 0.0f;
    float rotationOverLifeStart = 0.0f;
    float rotationOverLifeEnd = 0.0f;

    // ── Particle Light Emission ────────────────────────────────────────
    uint32_t particleLightEnabled = 0;     ///< 0=off, 1=inject point light at emitter position.
    float particleLightRadius     = 500.0f;///< Point light radius (cm).
    float particleLightIntensity  = 2.0f;  ///< Point light intensity multiplier.
    float particleLightFlicker    = 0.0f;  ///< Flicker amplitude [0,1]. 0=steady.
    float particleLightFlickerSpeed = 5.0f;///< Flicker oscillation frequency (Hz).

    /// Emitter lifecycle — auto-disable after duration (seconds). 0 = infinite.
    float emitterDuration = 0.0f;
    /// Restart count before stopping. 0 = infinite.
    uint32_t loopCount = 0;

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

    /// Fractional death accumulator — prevents truncation-to-zero at high FPS.
    float deathAccumulator = 0.0f;

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

    /// Whether the GPU free list/alive count need initial upload.
    /// Set to true by SyncVfxEmitters, consumed by RegisterParticlePasses.
    bool needsInitialUpload = false;

    /// Number of particles actually allocated from the budget.
    /// May be less than maxParticles if the budget was constrained.
    uint32_t allocatedFromBudget = 0;

    // -- Material ------------------------------------------------------------

    /// Material for this emitter's rendered particles. Accepts any MaterialInterface
    /// subclass: VFXMaterial, Material (reuse mesh material), or MaterialInstance
    /// (per-emitter overrides). When null, uses default unlit white.
    MaterialInterface* material = nullptr;

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
