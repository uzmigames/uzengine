/// VFX Particle Simulation Pass -- Render graph compute pass for GPU particle sim.
///
/// Registers a compute pass in the render graph that drives the full particle
/// simulation pipeline for every active emitter in the scene. For each emitter
/// the pass dispatches three compute shaders in sequence:
///
/// 1. **Update CS** -- steps physics, aging, and module evaluation for all alive
///    particles. Dead particles are recycled to the free list.
/// 2. **Spawn CS** -- emits `spawnCount` new particles into the write attribute set
///    by popping indices from the free list (which now includes just-died slots).
/// 3. **Indirect Args CS** -- fills the indirect draw/dispatch argument buffers
///    from the alive count so subsequent rendering can use `DrawIndexedIndirect`.
///
/// Update runs BEFORE spawn to prevent a write-after-write race: both shaders
/// write to the same WRITE set buffers, and if spawn ran first, update would
/// clobber freshly-spawned lifespan values with the death marker (0.0).
///
/// UAV barriers are inserted between each stage to prevent read-after-write hazards
/// on the particle attribute buffers and bookkeeping counters.
///
/// # Render Graph Integration
/// The pass is registered as a single compute pass with no explicit render graph
/// resource tracking (all buffers are external persistent resources owned by
/// `GPUParticleBuffer`). Internal UAV barriers are emitted explicitly inside the
/// execution callback.
///
/// # Pipeline Root Signature Layout
/// Matches the global engine root signature:
/// - Slot 1 (b1): Root CBV for per-dispatch constants.
/// - Slot 4 (u0..u3): UAV descriptor table for output buffers.
/// - Slot 5 (t0..tN): SRV descriptor table for read-only input buffers.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#pragma once

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

struct GPUParticleBuffer;
struct EmitterModuleStack;
class ModuleCompositor;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ---------------------------------------------------------------------------
// ParticleSimConstants -- CPU-side mirror of particle sim constant buffers
// ---------------------------------------------------------------------------

/// CPU-side mirror of the spawn shader's cbuffer VFXSpawnConstants (register b1).
///
/// Must be byte-identical to the HLSL cbuffer layout in particle_spawn_cs.hlsl.
/// All emitter parameters + bindless descriptor indices for SM6.6 resource access.
///
/// Total size: 192 bytes (12 x float4, 256-byte aligned by CBRingAllocator).
///
/// @ref UzEngine VFX system -- GPU particle simulation pipeline
struct ParticleSpawnConstants {
    // ── Emitter Parameters ──────────────────────────────────────────── offset 0
    float    emitterPos[3]       = {0.0f, 0.0f, 0.0f}; ///< World-space emitter origin.
    float    spawnRadius         = 0.0f;  ///< Random offset sphere radius.

    float    spawnVelocity[3]    = {0.0f, 10.0f, 0.0f}; ///< Base emission velocity.
    float    velocityVariation   = 1.0f;  ///< +/- speed randomization range.

    float    spreadAngle         = 0.0f;  ///< Half-cone angle in radians (0 = laser).
    float    minLifetime         = 1.0f;  ///< Minimum particle lifespan (seconds).
    float    maxLifetime         = 5.0f;  ///< Maximum particle lifespan (seconds).
    float    initialSize         = 1.0f;  ///< Initial billboard size (world units).

    float    initialColor[4]     = {1.0f, 1.0f, 1.0f, 1.0f}; ///< RGBA initial color (linear).

    float    dt                  = 0.0f;  ///< Frame delta time (seconds).
    uint32_t frameNumber         = 0;     ///< Monotonically increasing frame counter.
    uint32_t spawnCount          = 0;     ///< Particles to spawn this frame.
    uint32_t maxParticles        = 0;     ///< Total particle pool capacity.

    // ── Bindless Descriptor Indices (SM6.6) ─────────────────────────── offset 80
    uint32_t freeListSrvIndex    = 0;     ///< StructuredBuffer<uint> free list SRV.
    uint32_t positionsUavIndex   = 0;     ///< RWStructuredBuffer<float4> positions UAV.
    uint32_t velocitiesUavIndex  = 0;     ///< RWStructuredBuffer<float4> velocities UAV.
    uint32_t agesUavIndex        = 0;     ///< RWStructuredBuffer<float> ages UAV.

    uint32_t lifespansUavIndex   = 0;     ///< RWStructuredBuffer<float> lifespans UAV.
    uint32_t sizesUavIndex       = 0;     ///< RWStructuredBuffer<float> sizes UAV.
    uint32_t colorsUavIndex      = 0;     ///< RWStructuredBuffer<float4> colors UAV.
    uint32_t rotationsUavIndex   = 0;     ///< RWStructuredBuffer<float> rotations UAV.

    uint32_t freeListHeadUavIndex = 0;    ///< RWStructuredBuffer<uint> freeListHead UAV.
    uint32_t aliveCountUavIndex  = 0;     ///< RWStructuredBuffer<uint> aliveCount UAV.
    uint32_t spawnShape          = 0;     ///< 0=Point,1=Sphere,2=Box,3=Ring,4=Cone,5=Cylinder,6=Disc,7=Line.
    uint32_t spawnOnSurface      = 0;     ///< 0=volume fill, 1=surface only.

    float    shapeParam1         = 0.0f;  ///< Shape-specific: ring width / cone half-angle / cyl height / line len.
    float    shapeParam2         = 0.0f;  ///< Shape-specific: cone height.
    float    shapeParam3         = 0.0f;  ///< Reserved.
    uint32_t _pad0               = 0;

    // ── Initial rotation range + velocity inheritance ──────────────── offset 144
    float    initialRotationMin  = 0.0f;  ///< Minimum initial rotation (radians).
    float    initialRotationMax  = 6.283185f; ///< Maximum initial rotation (radians). Default: 2*PI.
    float    emitterVelocityX    = 0.0f;  ///< Emitter movement velocity X (m/s).
    float    emitterVelocityY    = 0.0f;  ///< Emitter movement velocity Y (m/s).

    float    emitterVelocityZ    = 0.0f;  ///< Emitter movement velocity Z (m/s).
    float    velocityInheritance = 0.0f;  ///< Fraction of emitter vel added [0,1].
    uint32_t dynamicParamsUavIndex = 0;   ///< UAV for RWStructuredBuffer<float4> dynamic params.
    float    defaultDynParam0    = 0.0f;  ///< Default dynamic param X.

    float    defaultDynParam1    = 0.0f;  ///< Default dynamic param Y.
    float    defaultDynParam2    = 0.0f;  ///< Default dynamic param Z.
    float    defaultDynParam3    = 0.0f;  ///< Default dynamic param W.
    /// @ref Salmon 2011 "Parallel Random Numbers" -- 0=use frameNumber, >0=fixed seed.
    uint32_t emitterSeed         = 0;     ///< Deterministic seed override.
};
static_assert(sizeof(ParticleSpawnConstants) == 192,
              "ParticleSpawnConstants must be 192 bytes to match HLSL VFXSpawnConstants");

/// CPU-side mirror of the update shader's cbuffer VFXUpdateConstants (register b1).
///
/// Must be byte-identical to the HLSL cbuffer layout in particle_update_cs.hlsl.
/// Contains physics parameters and descriptor heap indices for both READ (SRV)
/// and WRITE (UAV) buffer sets.
///
/// Total size: 384 bytes (24 x float4, 256-byte aligned by CBRingAllocator).
///
/// @ref Harada 2007 "Real-Time Particle System on the GPU"
struct ParticleUpdateConstants {
    // ── Simulation Parameters ───────────────────────────────────────── offset 0
    float    gravity[3]          = {0.0f, -9.81f, 0.0f}; ///< Gravitational acceleration (m/s^2).
    float    dt                  = 0.0f;  ///< Frame delta time (seconds).

    float    damping             = 0.0f;  ///< Velocity damping coefficient [0, inf).
    uint32_t aliveCount          = 0;     ///< Number of alive particles (prev frame).
    uint32_t frameNumber         = 0;     ///< Monotonically increasing frame counter.
    uint32_t maxParticles        = 0;     ///< Total particle pool capacity.

    // ── Bindless Descriptor Indices: READ set (SRV) ─────────────────── offset 32
    uint32_t positionsSrvIndex   = 0;
    uint32_t velocitiesSrvIndex  = 0;
    uint32_t agesSrvIndex        = 0;
    uint32_t lifespansSrvIndex   = 0;

    uint32_t sizesSrvIndex       = 0;
    uint32_t colorsSrvIndex      = 0;
    uint32_t rotationsSrvIndex   = 0;
    uint32_t _padSrv0            = 0;

    // ── Bindless Descriptor Indices: WRITE set (UAV) ────────────────── offset 64
    uint32_t positionsUavIndex   = 0;
    uint32_t velocitiesUavIndex  = 0;
    uint32_t agesUavIndex        = 0;
    uint32_t lifespansUavIndex   = 0;

    uint32_t sizesUavIndex       = 0;
    uint32_t colorsUavIndex      = 0;
    uint32_t rotationsUavIndex   = 0;
    uint32_t _padUav0            = 0;

    // ── Bindless Descriptor Indices: Shared Buffers ─────────────────── offset 96
    uint32_t freeListUavIndex    = 0;
    uint32_t freeListHeadUavIndex = 0;
    uint32_t aliveCountNewUavIndex = 0;
    uint32_t _padShared0         = 0;

    // ── Curl Noise Force Parameters ─────────────────────────────────── offset 112
    /// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
    float    curlStrength        = 0.0f;  ///< Curl force multiplier. 0 = disabled.
    float    curlFrequency       = 1.0f;  ///< Spatial frequency.
    float    curlSpeed           = 0.0f;  ///< Animation scroll speed (units/s).
    float    totalTime           = 0.0f;  ///< Total elapsed simulation time (s).

    // ── Vortex Force Parameters ─────────────────────────────────────── offset 128
    /// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
    float    vortexAxisOriginX   = 0.0f;
    float    vortexAxisOriginY   = 0.0f;
    float    vortexAxisOriginZ   = 0.0f;
    float    vortexTangentialStrength = 0.0f;

    float    vortexAxisDirX      = 0.0f;
    float    vortexAxisDirY      = 0.0f;
    float    vortexAxisDirZ      = 1.0f;  ///< Default: Z-up axis.
    float    vortexRadialStrength = 0.0f;

    float    vortexMaxRadius     = 100.0f;
    uint32_t orbitEnabled        = 0;
    float    orbitRadius         = 50.0f;
    float    orbitSpeed          = 1.0f;

    // ── Orbit Axis ───────────────────────────────────────────────────── offset 176
    float    orbitAxisX          = 0.0f;
    float    orbitAxisY          = 0.0f;
    float    orbitAxisZ          = 1.0f;
    float    _orbitAxisPad       = 0.0f;

    // ── Wind Zone GPU Buffer ─────────────────────────────────────────── offset 192
    /// @ref Stam 1999 "Stable Fluids"
    uint32_t windZoneSrvIndex    = 0;
    uint32_t activeWindZoneCount = 0;
    uint32_t _padWind0           = 0;
    uint32_t _padWind1           = 0;

    // ── Kill Volume GPU Buffer ───────────────────────────────────────── offset 208
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    uint32_t killVolumeSrvIndex  = 0;
    uint32_t activeKillVolumeCount = 0;
    /// @ref Muller 2006 "Position Based Dynamics" -- restitution coefficients
    uint32_t groundCollision     = 0;     ///< 0=disabled, 1=bounce off Z=0 ground.
    float    restitution         = 0.5f;  ///< Bounce coefficient [0,1].

    // ── Quadratic Drag + Death Spawn Setup ───────────────────────────── offset 224
    /// @ref Witkin & Baraff 2001 "Physically Based Modeling" -- quadratic drag
    float    dragCoefficient     = 0.0f;  ///< Quadratic drag coefficient. 0=disabled.
    uint32_t deathSpawnEnabled   = 0;     ///< 0=off, 1=write DeathEvent on particle death.
    uint32_t maxDeathEvents      = 4096;  ///< Hard cap on events per frame.
    float    deathSpawnSpread    = 0.5f;  ///< Cone half-angle (rad) for child velocity spread.

    // ── Death Spawn Child Parameters ─────────────────────────────────── offset 240
    /// @ref Shopf 2008 "GPU Particles" -- death event driven sub-emitter spawn
    float    deathSpawnSpeedScale = 1.0f;
    uint32_t deathSpawnCount     = 4;
    uint32_t deathEventBufferUavIndex = 0;
    uint32_t deathEventCountUavIndex = 0;

    // ── Rotation rate + acceleration + rotation over lifetime ─────────── offset 256
    float    rotationRate        = 0.0f;  ///< Constant rotation speed (rad/s).
    float    rotationOverLifeStart = 0.0f;
    float    rotationOverLifeEnd = 0.0f;
    float    accelX              = 0.0f;

    float    accelY              = 0.0f;
    float    accelZ              = 0.0f;
    /// @ref Muller 2006 "Position Based Dynamics" -- multi-mode collision response
    uint32_t collisionResponse   = 0;     ///< 0=Bounce, 1=Stick, 2=Kill.
    float    collisionFriction   = 0.3f;

    // ── Dynamic Material Parameters (Task 4.14.0) ──────────────────── offset 288
    uint32_t readDynParamsSrv    = 0;
    uint32_t writeDynParamsUav   = 0;
    /// @ref Stam 1999 "Stable Fluids" -- vector field advection
    uint32_t vectorFieldEnabled  = 0;
    float    vectorFieldStrength = 0.0f;

    float    vectorFieldTightness = 0.0f;
    float    vectorFieldScale    = 1.0f;
    /// @ref Witkin & Baraff 2001 "Physically Based Modeling" -- point forces
    uint32_t pointForceEnabled   = 0;
    float    pointForceX         = 0.0f;

    float    pointForceY         = 0.0f;
    float    pointForceZ         = 0.0f;
    float    pointForceStrength  = 0.0f;
    float    pointForceRadius    = 100.0f;
    uint32_t _padPF0             = 0;
    uint32_t _padPF1             = 0;

    // ── Procedural Terrain Collision (Task 4.21.0) ──────────────────── offset 344
    /// @ref Muller 2006 "Position Based Dynamics" -- restitution + friction on terrain
    uint32_t terrainCollisionEnabled = 0;
    float    terrainAmplitude    = 10.0f;
    float    terrainFrequency    = 0.02f;
    float    terrainBaseHeight   = 0.0f;

    // ── Collision Event Spawn (Task 4.25.0) ─────────────────────────── offset 360
    /// @ref Shopf 2008 "GPU Particles" -- event buffer for sub-emitter spawning
    uint32_t collisionSpawnEnabled = 0;
    uint32_t collisionSpawnCount = 4;
    uint32_t _padCollEvt0        = 0;
    uint32_t _padCollEvt1        = 0;
    uint32_t _padCollEvt2        = 0;
    uint32_t _padCollEvt3        = 0;
};
static_assert(sizeof(ParticleUpdateConstants) == 384,
              "ParticleUpdateConstants must be 384 bytes to match HLSL VFXUpdateConstants");

/// CPU-side mirror of the indirect args shader's cbuffer VFXIndirectArgsConstants (register b0).
///
/// Must be byte-identical to the HLSL cbuffer layout in particle_indirect_args_cs.hlsl.
/// Contains bindless descriptor indices for the alive count SRV and indirect args UAV.
///
/// Total size: 16 bytes (1 x float4).
struct ParticleIndirectArgsConstants {
    uint32_t aliveCountSrvIndex   = 0;  ///< SRV index: StructuredBuffer<uint> (1 element).
    uint32_t indirectArgsUavIndex = 0;  ///< UAV index: RWByteAddressBuffer (32 bytes).
    uint32_t _pad0                = 0;
    uint32_t _pad1                = 0;
};
static_assert(sizeof(ParticleIndirectArgsConstants) == 16,
              "ParticleIndirectArgsConstants must be 16 bytes to match HLSL VFXIndirectArgsConstants");

// ---------------------------------------------------------------------------
// EmitterSimDispatch -- per-emitter data for the simulation pass
// ---------------------------------------------------------------------------

/// Per-emitter dispatch data for the particle simulation pass.
///
/// Contains the GPU buffer pointers, pre-filled constant buffer allocations,
/// and descriptor heap indices needed to dispatch all three compute shaders
/// (spawn, update, indirect args) for a single emitter.
///
/// # Ownership
/// Does NOT own any of the pointed-to resources. The caller must ensure
/// `buffer` remains valid until `RgBuilder::Execute()` completes.
struct EmitterSimDispatch {
    /// The emitter's GPU particle storage (ping-pong attribute sets + bookkeeping).
    GPUParticleBuffer* buffer = nullptr;

    /// Pre-filled CB allocation for the spawn shader constants.
    CBAllocation spawnCB = {};

    /// Pre-filled CB allocation for the update shader constants.
    CBAllocation updateCB = {};

    /// Pre-filled CB allocation for the indirect args shader constants.
    CBAllocation indirectCB = {};

    /// Per-emitter composed spawn PSO (from module compositor). When valid,
    /// overrides desc.spawnPipeline for this emitter.
    RHIPipelineHandle composedSpawnPSO;

    /// Per-emitter composed update PSO (from module compositor). When valid,
    /// overrides desc.updatePipeline for this emitter.
    RHIPipelineHandle composedUpdatePSO;

    /// Number of particles to spawn this frame. May be 0 (skip spawn dispatch).
    uint32_t spawnCount = 0;

    /// Number of currently alive particles (from CPU readback of previous frame).
    uint32_t aliveCount = 0;

    // -- Descriptor heap indices for SRV/UAV binding (SM5 path) ---------------

    /// SRV index for the read attribute set (positions, velocities, etc.).
    uint32_t readSetSrvIndex = UINT32_MAX;

    /// UAV index for the write attribute set.
    uint32_t writeSetUavIndex = UINT32_MAX;

    /// UAV index for the alive count buffer.
    uint32_t aliveCountUavIndex = UINT32_MAX;

    /// UAV index for the free list buffer.
    uint32_t freeListUavIndex = UINT32_MAX;

    /// UAV index for the indirect draw args buffer.
    uint32_t indirectDrawArgsUavIndex = UINT32_MAX;

    /// UAV index for the indirect dispatch args buffer.
    uint32_t indirectDispatchArgsUavIndex = UINT32_MAX;

    // -- Render graph resource IDs (for automatic barrier tracking) -----------

    /// RgResourceIds for the 7 write-set attribute buffers.
    /// Invalid (default) if not imported into the render graph.
    rg::RgResourceId rgWritePositions;
    rg::RgResourceId rgWriteVelocities;
    rg::RgResourceId rgWriteAges;
    rg::RgResourceId rgWriteLifespans;
    rg::RgResourceId rgWriteSizes;
    rg::RgResourceId rgWriteColors;
    rg::RgResourceId rgWriteRotations;

    /// RgResourceIds for shared bookkeeping buffers.
    rg::RgResourceId rgAliveCount;
    rg::RgResourceId rgFreeList;
    rg::RgResourceId rgFreeListHead;
    rg::RgResourceId rgIdToIndex;
    rg::RgResourceId rgIndirectDrawArgs;
    rg::RgResourceId rgIndirectDispatchArgs;
};

// ---------------------------------------------------------------------------
// ParticleSimulationPassDesc -- full pass configuration
// ---------------------------------------------------------------------------

/// Configuration for the GPU particle simulation render graph pass.
///
/// All fields must be set before calling `AddParticleSimulationPass`. The pass
/// does not take ownership of any pointed-to objects -- the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct ParticleSimulationPassDesc {
    // -- Pipeline handles for the 3 compute shaders ----------------------------

    /// Compute PSO for the particle spawn shader.
    RHIPipelineHandle spawnPipeline;

    /// Compute PSO for the particle update shader.
    RHIPipelineHandle updatePipeline;

    /// Compute PSO for the indirect args generation shader.
    RHIPipelineHandle indirectArgsPipeline;

    // -- Per-emitter dispatch data --------------------------------------------

    /// Array of active emitters to simulate this frame.
    /// Empty means the pass is a no-op (but still registered for graph consistency).
    std::vector<EmitterSimDispatch> emitters;

    /// Small upload-heap buffer containing a single uint32 = 0.
    /// Used to zero-clear aliveCount each frame via CopyBuffer.
    RHIBufferHandle zeroBuffer;
};

// ---------------------------------------------------------------------------
// AddParticleSimulationPass
// ---------------------------------------------------------------------------

/// Registers the GPU particle simulation compute pass in the render graph.
///
/// For each emitter in `desc.emitters`, the pass executes:
/// 1. Zero-clear aliveCount via CopyBuffer.
/// 2. Bind update pipeline, set update CB, dispatch ceil(capacity / 256) groups.
/// 3. UAV barrier on all written buffers (attribute set, alive count, free list).
/// 4. freeList UAV→SRV transition.
/// 5. Bind spawn pipeline, set spawn CB, dispatch ceil(spawnCount / 256) groups.
/// 6. freeList SRV→UAV transition.
/// 7. aliveCount UAV→SRV transition.
/// 8. Bind indirect args pipeline, set indirect CB, dispatch 1 group.
/// 9. aliveCount SRV→UAV transition.
///
/// Emitters with `spawnCount == 0` skip the spawn dispatch but still run
/// update and indirect args.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration (pipelines, per-emitter dispatch data).
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if any pipeline handle is invalid or if any emitter's `buffer` is null.
void AddParticleSimulationPass(rg::RgBuilder& builder,
                               const ParticleSimulationPassDesc& desc);

// ---------------------------------------------------------------------------
// ComposeEmitterShaders -- module composition integration point
// ---------------------------------------------------------------------------

/// Composes HLSL shaders for an emitter's module stack if dirty.
///
/// Checks `stack.dirty` and, if true, invokes the `ModuleCompositor` to
/// regenerate `composedUpdateHLSL` and `composedSpawnHLSL` from the current
/// module lists. Clears the dirty flag and updates the cache key on success.
///
/// If the module stack is empty (no modules), this is a no-op -- the emitter
/// falls back to the hardcoded shaders from 3.60.0.
///
/// # Parameters
/// * `stack` -- The emitter's module stack (modified in place).
/// * `compositor` -- The module compositor used to generate HLSL.
///
/// # Returns
/// `true` if composition succeeded or was not needed (stack clean or empty).
/// `false` if composition failed (e.g., all modules returned empty shader code).
///
/// # Side Effects
/// - Updates `stack.composedUpdateHLSL`, `stack.composedSpawnHLSL`,
///   `stack.composedCacheKey`, and clears `stack.dirty` on success.
/// - Logs VFX/Info when shaders are recomposed.
[[nodiscard]] bool ComposeEmitterShaders(EmitterModuleStack& stack,
                                         const ModuleCompositor& compositor);

} // namespace uz
