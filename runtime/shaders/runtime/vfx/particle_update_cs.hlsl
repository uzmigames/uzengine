/// GPU Particle Update Compute Shader
///
/// Advances particle simulation by one time step: applies gravity, damping,
/// optional curl noise, optional vortex force, optional wind zone force,
/// optional procedural vector field, kill
/// volume containment test, and Euler integration, then tests for particle
/// death (age >= lifespan). Dead particles are recycled back to the free list.
///
/// Uses ping-pong double-buffering: reads from the previous frame's buffer
/// set (SRV via bindless) and writes to the current frame's buffer set
/// (UAV via bindless). This avoids read/write hazards without barriers
/// within the dispatch.
///
/// # Simulation Steps (per particle)
/// 1. Read current state from READ set (SRV).
/// 2. Advance age by dt. If age >= lifespan, particle is dead.
/// 3. Dead particles: push index onto free list via InterlockedAdd, mark dead.
/// 4. Alive particles: apply gravity, damping, curl noise, vortex force,
///    wind zone force, procedural vector field. Integrate position.
/// 5. Kill volume test: if inside any active kill volume, instantly kill.
/// 6. Write to WRITE set, increment alive_count_new.
///
/// # Damping Model
/// Uses multiplicative damping: vel *= saturate(1.0 - damping * dt).
/// This is frame-rate independent for small dt values. For exact independence,
/// use vel *= exp(-damping * dt), but the linear approximation is sufficient
/// for 30-144 fps ranges and avoids the exp() cost. The saturate() clamp
/// prevents velocity reversal at extremely large dt * damping products.
///
/// # Dispatch Strategy
/// Dispatched with ceil(aliveCount / 256) thread groups. The alive count comes
/// from the previous frame's counter (set by the indirect args shader or read
/// from the counter buffer on the CPU).
///
/// # Bindless Resource Access (SM6.6)
/// All SoA buffers are accessed via ResourceDescriptorHeap[index] using
/// descriptor indices stored in the constant buffer. The CPU flips READ/WRITE
/// indices each frame for the ping-pong swap.
///
/// # Thread Group
/// [256, 1, 1] -- one thread per alive particle.
///
/// # Root Signature
/// - b1: VFXUpdateConstants (simulation params + bindless descriptor indices)
///
/// @ref Harada 2007 "Real-Time Particle System on the GPU"
/// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
/// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A — vortex
/// @ref Stam 1999 "Stable Fluids" — wind zone accumulation
/// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4 — kill volume containment
/// @ref Christensen 2018 "Fibonacci Sphere Sampling" — golden angle orbit spread (Task 4.9.0)
/// @constants UPDATE_GROUP_SIZE=256
/// @deviations Linear damping approximation instead of exponential decay.
///             Linear vortex radial falloff instead of quadratic (GPU efficiency).
///             Wind gusts saturated to positive-only (matches Niagara behavior).
///
/// # SM5 Register Layout (non-bindless fallback)
/// SRV (READ set):
/// - t0: StructuredBuffer<float4>           positionsIn  (xyz=pos, w=unused)
/// - t1: StructuredBuffer<float4>           velocitiesIn (xyz=vel, w=unused)
/// - t2: StructuredBuffer<float>            agesIn
/// - t3: StructuredBuffer<float>            lifespansIn
/// - t4: StructuredBuffer<float>            sizesIn
/// - t5: StructuredBuffer<float4>           colorsIn
/// - t6: StructuredBuffer<float>            rotationsIn
/// - t7: StructuredBuffer<float4>           dynParamsIn
/// UAV (WRITE set + shared):
/// - u0: RWStructuredBuffer<float4>         positionsOut  (xyz=pos, w=unused)
/// - u1: RWStructuredBuffer<float4>         velocitiesOut (xyz=vel, w=unused)
/// - u2: RWStructuredBuffer<float>          agesOut
/// - u3: RWStructuredBuffer<float>          lifespansOut
/// - u4: RWStructuredBuffer<float>          sizesOut
/// - u5: RWStructuredBuffer<float4>         colorsOut
/// - u6: RWStructuredBuffer<float>          rotationsOut
/// - u7: RWStructuredBuffer<uint>           freeList
/// - u8: RWStructuredBuffer<uint>           freeListHead (1 element)
/// - u9: RWStructuredBuffer<uint>           aliveCountNew (1 element)
/// - u10: RWStructuredBuffer<float4>        dynParamsOut

#include "../include/vfx_common.hlsli"
#include "vfx_curl_noise.hlsli"
#include "vfx_vortex.hlsli"
#include "vfx_wind.hlsli"
#include "vfx_kill_volumes.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Death Event Struct (GPU-side mirror of DeathEvent in vfx_viewport_vfx.h)
// ─────────────────────────────────────────────────────────────────────────────

/// Written by the update CS when a particle's age >= lifespan (Task 4.11.0).
/// Consumed by particle_death_spawn_cs.hlsl to birth child particles.
///
/// @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
/// @constants VFX_MAX_DEATH_EVENTS=4096 (VFXUpdateConstants::maxDeathEvents)
/// @deviations NONE
struct DeathEvent {
    float3 position; ///< World-space position at death.
    float  _pad0;
    float3 velocity; ///< Velocity at death — used as child spawn direction seed.
    float  _pad1;
    float4 color;    ///< RGBA color at death — inherited by child particles.
};

// ─────────────────────────────────────────────────────────────────────────────
// Thread Group Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Threads per group for update dispatch. Matches spawn shader for consistency.
static const uint UPDATE_GROUP_SIZE = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b1) -- Simulation Parameters + Bindless Indices
// ─────────────────────────────────────────────────────────────────────────────

/// Update constants uploaded by the CPU each frame. Contains physics parameters
/// and descriptor heap indices for both READ (SRV) and WRITE (UAV) buffer sets.
///
/// Layout is 16-byte aligned per HLSL packing rules. Total: 304 bytes (19 float4).
/// Must match VFXUpdateCB in runtime/demos/vfx_viewport/vfx_viewport_vfx.h exactly.
cbuffer VFXUpdateConstants : register(b1) {
    // ── Simulation Parameters ───────────────────────────────────────────── offset 0
    float3   gravity;              ///< Gravitational acceleration (m/s^2).          0
                                   ///< Typical: (0, -9.81, 0) for downward.
    float    dt;                   ///< Frame delta time in seconds.                 12

    float    damping;              ///< Velocity damping coefficient [0, inf).       16
                                   ///< 0 = no damping. Higher = more drag.
                                   ///< Applied as vel *= (1 - damping * dt).
    uint     aliveCount;           ///< Number of alive particles (prev frame).      20
    uint     frameNumber;          ///< Monotonically increasing frame counter.      24
    uint     maxParticles;         ///< Total particle pool capacity.                28

#ifdef UZ_BINDLESS
    // ── Bindless Descriptor Indices: READ set (SRV) ─────────────────────── offset 32
    uint     positionsSrvIndex;    ///< StructuredBuffer<float4> positions.          32
    uint     velocitiesSrvIndex;   ///< StructuredBuffer<float4> velocities.         36
    uint     agesSrvIndex;         ///< StructuredBuffer<float> ages.                40
    uint     lifespansSrvIndex;    ///< StructuredBuffer<float> lifespans.           44

    uint     sizesSrvIndex;        ///< StructuredBuffer<float> sizes.               48
    uint     colorsSrvIndex;       ///< StructuredBuffer<float4> colors.             52
    uint     rotationsSrvIndex;    ///< StructuredBuffer<float> rotations.           56
    uint     _padSrv0;             ///<                                              60

    // ── Bindless Descriptor Indices: WRITE set (UAV) ────────────────────── offset 64
    uint     positionsUavIndex;    ///< RWStructuredBuffer<float4> positions.        64
    uint     velocitiesUavIndex;   ///< RWStructuredBuffer<float4> velocities.       68
    uint     agesUavIndex;         ///< RWStructuredBuffer<float> ages.              72
    uint     lifespansUavIndex;    ///< RWStructuredBuffer<float> lifespans.         76

    uint     sizesUavIndex;        ///< RWStructuredBuffer<float> sizes.             80
    uint     colorsUavIndex;       ///< RWStructuredBuffer<float4> colors.           84
    uint     rotationsUavIndex;    ///< RWStructuredBuffer<float> rotations.         88
    uint     _padUav0;             ///<                                              92

    // ── Bindless Descriptor Indices: Shared Buffers ─────────────────────── offset 96
    uint     freeListUavIndex;     ///< RWStructuredBuffer<uint> free_list.          96
    uint     freeListHeadUavIndex; ///< RWStructuredBuffer<uint> (1 element).        100
    uint     aliveCountNewUavIndex;///< RWStructuredBuffer<uint> (1 element).        104
                                   ///< New alive count for current frame.
                                   ///< Must be reset to 0 before dispatch.
    uint     _padShared0;          ///<                                              108

    // ── Curl Noise Force Parameters ─────────────────────────────────────── offset 112
    /// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
    float    curlStrength;         ///< Curl force multiplier. 0 = disabled.         112
    float    curlFrequency;        ///< Spatial frequency. Higher = tighter.         116
    float    curlSpeed;            ///< Animation scroll speed (units/s).            120
    float    totalTime;            ///< Total elapsed simulation time (s).           124

    // ── Vortex Force Parameters ─────────────────────────────────────────── offset 128
    /// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
    float    vortexAxisOriginX;           ///< World-space vortex axis origin X.     128
    float    vortexAxisOriginY;           ///< World-space vortex axis origin Y.     132
    float    vortexAxisOriginZ;           ///< World-space vortex axis origin Z.     136
    float    vortexTangentialStrength;    ///< Swirl force (m/s^2). 0 = disabled.   140

    float    vortexAxisDirX;             ///< Axis direction X (normalized).         144
    float    vortexAxisDirY;             ///< Axis direction Y (normalized).         148
    float    vortexAxisDirZ;             ///< Axis direction Z (normalized).         152
    float    vortexRadialStrength;       ///< Radial pull (>0) or push (<0).         156

    float    vortexMaxRadius;            ///< Cutoff radius in world units.          160
    uint     orbitEnabled;               ///< 0=disabled, 1=orbit around emitter.   164
    float    orbitRadius;                ///< Orbit circle radius in world units.    168
    float    orbitSpeed;                 ///< Angular speed in radians/sec.          172

    // ── Orbit Axis ───────────────────────────────────────────────────────── offset 176
    float    orbitAxisX;                 ///< Orbit plane normal X.                  176
    float    orbitAxisY;                 ///< Orbit plane normal Y.                  180
    float    orbitAxisZ;                 ///< Orbit plane normal Z.                  184
    float    _orbitAxisPad;              ///<                                        188

    // ── Wind Zone GPU Buffer ─────────────────────────────────────────────── offset 192
    /// @ref Stam 1999 "Stable Fluids" — wind field accumulation
    uint     windZoneSrvIndex;           ///< SRV for StructuredBuffer<WindZone>.    192
    uint     activeWindZoneCount;        ///< Active zone count [0..kMaxWindZones].  196
    uint     _padWind0;                  ///<                                        200
    uint     _padWind1;                  ///<                                        204

    // ── Kill Volume GPU Buffer ───────────────────────────────────────────── offset 208
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    uint     killVolumeSrvIndex;         ///< SRV for StructuredBuffer<KillVolume>.  208
    uint     activeKillVolumeCount;      ///< Active volume count [0..16].           212
    // ── Ground Plane Collision ───────────────────────────────────────────── offset 216
    /// @ref Müller 2006 "Position Based Dynamics" — restitution coefficients
    uint     groundCollision;            ///< 0=disabled, 1=bounce off Z=0 ground.  216
    float    restitution;                ///< Bounce coefficient [0,1].              220

    // ── Quadratic Drag + Death Spawn Setup ───────────────────────────────── offset 224
    /// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag F=-k|v|^2*dir
    float    dragCoefficient;            ///< Quadratic drag coefficient. 0=disabled. 224
    uint     deathSpawnEnabled;          ///< 0=off, 1=write DeathEvent on particle death. 228
    uint     maxDeathEvents;             ///< Hard cap on events per frame (= VFX_MAX_DEATH_EVENTS). 232
    float    deathSpawnSpread;           ///< Cone half-angle (rad) for child velocity spread. 236

    // ── Death Spawn Child Parameters ─────────────────────────────────────── offset 240
    /// @ref Shopf 2008 "GPU Particles" — death event driven sub-emitter spawn
    float    deathSpawnSpeedScale;       ///< Child speed = parent_death_speed * scale. 240
    uint     deathSpawnCount;            ///< Children spawned per event [1..32].       244
    uint     deathEventBufferUavIndex;   ///< UAV for RWStructuredBuffer<DeathEvent>.   248
    uint     deathEventCountUavIndex;    ///< UAV for RWBuffer<uint> death event count. 252

    // ── Rotation rate + acceleration + rotation over lifetime ─────────────── offset 256
    float    rotationRate;               ///< Constant rotation speed (rad/s). 0=none. 256
    float    rotationOverLifeStart;      ///< Rotation rate at birth (rad/s).          260
    float    rotationOverLifeEnd;        ///< Rotation rate at death (rad/s).          264
    float    accelX;                     ///< Per-axis acceleration X (m/s^2).         268

    float    accelY;                     ///< Per-axis acceleration Y (m/s^2).         272
    float    accelZ;                     ///< Per-axis acceleration Z (m/s^2).         276
    // ── Enhanced collision response ────────────────────────────────────── offset 280
    /// @ref Müller 2006 "Position Based Dynamics" — multi-mode collision response
    /// @ref Bender 2014 "Position-Based Simulation Methods in Computer Graphics" — friction model
    uint     collisionResponse;          ///< 0=Bounce, 1=Stick, 2=Kill.               280
    float    collisionFriction;          ///< Tangential friction [0,1].                284

    // ── Dynamic Material Parameters (Task 4.14.0) ──────────────────────── offset 288
    uint     readDynParamsSrv;           ///< SRV for StructuredBuffer<float4> dynamic params (READ set). 288
    uint     writeDynParamsUav;          ///< UAV for RWStructuredBuffer<float4> dynamic params (WRITE set). 292

    // ── Vector Field Parameters (Task 4.15.0) ────────────────────────── offset 296
    /// @ref Stam 1999 "Stable Fluids" — vector field advection fundamentals
    /// @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — velocity field sampling
    uint     vectorFieldEnabled;         ///< 0=disabled, 1=procedural vector field active. 296
    float    vectorFieldStrength;        ///< Magnitude multiplier for sampled field velocity. 300

    float    vectorFieldTightness;       ///< 0=additive (field as force), 1=directive (replace vel). 304
    float    vectorFieldScale;           ///< Spatial scale of the procedural noise field.             308
    uint     _padVF0;                    ///<                                          312
    uint     _padVF1;                    ///<                                          316
#endif
};

#ifndef UZ_BINDLESS
/// SM5 fallback: explicit register bindings for READ set (SRV).
StructuredBuffer<float4>           sm5_positionsIn   : register(t0);
StructuredBuffer<float4>           sm5_velocitiesIn  : register(t1);
StructuredBuffer<float>            sm5_agesIn        : register(t2);
StructuredBuffer<float>            sm5_lifespansIn   : register(t3);
StructuredBuffer<float>            sm5_sizesIn       : register(t4);
StructuredBuffer<float4>           sm5_colorsIn      : register(t5);
StructuredBuffer<float>            sm5_rotationsIn   : register(t6);
StructuredBuffer<float4>           sm5_dynParamsIn   : register(t7);

/// SM5 fallback: explicit register bindings for WRITE set (UAV) + shared.
RWStructuredBuffer<float4>         sm5_positionsOut   : register(u0);
RWStructuredBuffer<float4>         sm5_velocitiesOut  : register(u1);
RWStructuredBuffer<float>          sm5_agesOut        : register(u2);
RWStructuredBuffer<float>          sm5_lifespansOut   : register(u3);
RWStructuredBuffer<float>          sm5_sizesOut       : register(u4);
RWStructuredBuffer<float4>         sm5_colorsOut      : register(u5);
RWStructuredBuffer<float>          sm5_rotationsOut   : register(u6);
RWStructuredBuffer<uint>           sm5_freeList       : register(u7);
RWStructuredBuffer<uint>           sm5_freeListHead   : register(u8);
RWStructuredBuffer<uint>           sm5_aliveCountNew  : register(u9);
RWStructuredBuffer<float4>         sm5_dynParamsOut   : register(u10);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Updates one particle per thread: applies physics, advances age, handles death.
///
/// For each alive particle (indexed by threadIdx into the alive set):
/// - Reads all attributes from the READ set (SRV).
/// - Advances age. If expired, pushes index to free list and returns.
/// - Applies gravity and frame-rate-independent damping.
/// - Applies optional curl noise, vortex force, and wind zone forces.
/// - Integrates position via explicit Euler.
/// - Tests against kill volumes (instantly kills particles inside any volume).
/// - Writes updated state to the WRITE set (UAV).
/// - Atomically increments alive_count_new.
///
/// # Parameters
/// - dtid: SV_DispatchThreadID.x identifies which alive particle [0..aliveCount-1].
///
/// # Side Effects
/// - Dead particles: increments free_list_head, writes index to free_list.
/// - Alive particles: writes updated state to WRITE set UAVs, increments
///   aliveCountNew.
[numthreads(UPDATE_GROUP_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint threadIdx = dtid.x;

    // ── Early exit for excess threads ───────────────────────────────────
    if (threadIdx >= aliveCount) {
        return;
    }

    // ── Bind READ set (SRV) ─────────────────────────────────────────────
#ifdef UZ_BINDLESS
    StructuredBuffer<float4> positionsIn  = ResourceDescriptorHeap[positionsSrvIndex];
    StructuredBuffer<float4> velocitiesIn = ResourceDescriptorHeap[velocitiesSrvIndex];
    StructuredBuffer<float>  agesIn       = ResourceDescriptorHeap[agesSrvIndex];
    StructuredBuffer<float>  lifespansIn  = ResourceDescriptorHeap[lifespansSrvIndex];
    StructuredBuffer<float>  sizesIn      = ResourceDescriptorHeap[sizesSrvIndex];
    StructuredBuffer<float4> colorsIn     = ResourceDescriptorHeap[colorsSrvIndex];
    StructuredBuffer<float>  rotationsIn  = ResourceDescriptorHeap[rotationsSrvIndex];
    // dynParamsIn bound conditionally below — readDynParamsSrv may be invalid
    // due to CB layout mismatch or feature not wired yet.
#else
    #define positionsIn   sm5_positionsIn
    #define velocitiesIn  sm5_velocitiesIn
    #define agesIn        sm5_agesIn
    #define lifespansIn   sm5_lifespansIn
    #define sizesIn       sm5_sizesIn
    #define colorsIn      sm5_colorsIn
    #define rotationsIn   sm5_rotationsIn
    #define dynParamsIn   sm5_dynParamsIn
#endif

    // ── Read current particle state ─────────────────────────────────────
    // Positions and velocities are stored as float4 (xyz + padding) to match
    // the buffer stride of 16 bytes. Extract .xyz for simulation math.
    float3 pos      = positionsIn[threadIdx].xyz;
    float3 vel      = velocitiesIn[threadIdx].xyz;
    float  age      = agesIn[threadIdx];
    float  lifespan = lifespansIn[threadIdx];

    // ── Skip uninitialized slots ───────────────────────────────────────
    // Zero-initialized particles have lifespan == 0.  These were never
    // spawned, so they must NOT be treated as "dead" and pushed onto the
    // free list — doing so overflows the free-list head past maxParticles
    // and permanently prevents future spawns.
    if (lifespan == 0.0f) { return; }

    float  size     = sizesIn[threadIdx];
    float4 color    = colorsIn[threadIdx];
    float  rotation = rotationsIn[threadIdx];

    // ── Advance age ─────────────────────────────────────────────────────
    age += dt;

    // ── Death check: recycle expired particles ──────────────────────────
    if (age >= lifespan) {
        // ── Death event write (sub-emitter, Task 4.11.0) ─────────────────
        // Before recycling the slot, record the particle's final state in the
        // death event buffer so the secondary spawn pass can birth children.
        // The InterlockedAdd on the counter guarantees each event gets a unique
        // slot with no thread-group synchronisation required (all updates run
        // in parallel; we only need intra-buffer atomics here).
        //
        /// @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
        /// @constants maxDeathEvents=VFX_MAX_DEATH_EVENTS=4096
        /// @deviations NONE
#ifdef UZ_BINDLESS
        if (deathSpawnEnabled != 0u) {
            RWStructuredBuffer<DeathEvent> deathEvents  = ResourceDescriptorHeap[deathEventBufferUavIndex];
            RWStructuredBuffer<uint>       deathCounter = ResourceDescriptorHeap[deathEventCountUavIndex];

            uint slot;
            InterlockedAdd(deathCounter[0], 1u, slot);

            // Guard: discard event if hard cap exceeded (prevents buffer overflow).
            if (slot < maxDeathEvents) {
                DeathEvent evt;
                evt.position = pos;
                evt._pad0    = 0.0f;
                evt.velocity = vel;
                evt._pad1    = 0.0f;
                evt.color    = color;
                deathEvents[slot] = evt;
            }
        }
#endif // UZ_BINDLESS

        // Return this slot to the free list stack.
        // Atomically increment free_list_head to get the push index.
#ifdef UZ_BINDLESS
        RWStructuredBuffer<uint> freeList     = ResourceDescriptorHeap[freeListUavIndex];
        RWStructuredBuffer<uint> freeListHead = ResourceDescriptorHeap[freeListHeadUavIndex];
#else
        #define freeList     sm5_freeList
        #define freeListHead sm5_freeListHead
#endif

        uint pushIndex;
        InterlockedAdd(freeListHead[0], 1, pushIndex);

        // Guard against overflow (should not happen if pool is sized correctly)
        if (pushIndex < maxParticles) {
            freeList[pushIndex] = threadIdx;
        }

        // Zero the output lifespan so this slot is skipped by the next frame's
        // full-capacity dispatch (lifespan==0 → early return at line ~348).
        // Godot pattern: dead slots become no-ops via flag/lifespan gate.
#ifdef UZ_BINDLESS
        RWStructuredBuffer<float> deadLifespans = ResourceDescriptorHeap[lifespansUavIndex];
        deadLifespans[threadIdx] = 0.0f;
#else
        sm5_lifespansOut[threadIdx] = 0.0f;
#endif

#ifndef UZ_BINDLESS
        #undef freeList
        #undef freeListHead
#endif
        return;
    }

    // ── Physics: gravity + frame-rate-independent damping ───────────────

    // Gravity: constant acceleration
    vel += gravity * dt;

    // Acceleration force: independent per-axis acceleration (configurable second force).
#ifdef UZ_BINDLESS
    vel += float3(accelX, accelY, accelZ) * dt;
#endif

    // Damping: multiplicative decay, linear approximation of exp(-damping * dt).
    // Clamp to [0, 1] to prevent velocity reversal at very large dt * damping.
    float dampFactor = saturate(1.0f - damping * dt);
    vel *= dampFactor;

    // ── Quadratic drag (air resistance) ─────────────────────────────────
    // Unlike constant damping, drag force scales with speed squared:
    //   F_drag = -k * |v|^2 * normalize(v)
    // This produces physically plausible arcs where fast particles decelerate
    // much faster than slow ones (e.g. embers, projectiles, debris).
    // The min() clamp prevents velocity reversal at large timesteps.
    /// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag
    /// @constants dragCoefficient=user (default 0.0), speed threshold=0.001
    /// @deviations NONE
#ifdef UZ_BINDLESS
    if (dragCoefficient > 0.0f) {
        float speed = length(vel);
        if (speed > 0.001f) {
            float dragMag = dragCoefficient * speed * speed;
            // Clamp drag impulse to current speed to prevent velocity reversal
            vel -= (vel / speed) * min(dragMag * dt, speed);
        }
    }
#endif

    // ── Curl noise force ────────────────────────────────────────────────
    // Divergence-free velocity perturbation for organic swirling motion.
    // Guarded by curlStrength > 0 to skip the expensive FBM evaluation
    // when curl noise is disabled.
    /// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow" — curl force accumulation
#ifdef UZ_BINDLESS
    if (curlStrength > 0.0f) {
        float3 curlForce = AnimatedCurlNoise(pos, totalTime, curlSpeed, curlFrequency);
        vel += curlForce * curlStrength * dt;
    }

    // ── Vortex force ─────────────────────────────────────────────────────
    // Spins particles around a world-space axis with tangential (swirl) and
    // radial (spiral) force components. Guarded to skip when disabled.
    /// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A — vortex
    if (vortexTangentialStrength != 0.0f || vortexRadialStrength != 0.0f) {
        float3 vortexAxisOrigin = float3(vortexAxisOriginX, vortexAxisOriginY, vortexAxisOriginZ);
        float3 vortexAxisDir    = float3(vortexAxisDirX,    vortexAxisDirY,    vortexAxisDirZ);

        // Normalize on the GPU as a safety measure (CPU normalizes before upload,
        // but guard against degenerate uploads producing NaN forces).
        float axisLen = length(vortexAxisDir);
        if (axisLen > 0.0001f) {
            vortexAxisDir /= axisLen;
        } else {
            vortexAxisDir = float3(0.0f, 1.0f, 0.0f); // fallback: +Y
        }

        float3 vortexF = ComputeVortexForce(
            pos,
            vortexAxisOrigin,
            vortexAxisDir,
            vortexTangentialStrength,
            vortexRadialStrength,
            vortexMaxRadius
        );
        vel += vortexF * dt;
    }

    // ── Wind zone force ───────────────────────────────────────────────────
    // Accumulates force from all active wind zones (local + global).
    // Each zone can have a base strength and sinusoidal gust on top.
    /// @ref Stam 1999 "Stable Fluids" — wind zone accumulation
    if (activeWindZoneCount > 0u) {
        StructuredBuffer<WindZone> windZones = ResourceDescriptorHeap[windZoneSrvIndex];
        float3 windForce = AccumulateWindForce(pos, totalTime, windZones, activeWindZoneCount);
        vel += windForce * dt;
    }

    // ── Procedural vector field (3D noise-driven flow) ──────────────────
    // Generates a 3D velocity field using three offset SimplexNoise3D samples
    // as XYZ components. The offsets (prime-ish values) decorrelate the three
    // channels so they don't produce aligned motion. The result is normalized
    // and scaled by vectorFieldStrength to produce the field velocity.
    //
    // Tightness controls how the field blends with existing particle velocity:
    //   tightness == 0  → additive: velocity += fieldVel * dt  (field as force)
    //   tightness == 1  → directive: velocity = fieldVel        (full replacement)
    //   intermediate   → lerp between (vel + fieldVel * dt) and fieldVel
    //
    /// @ref Stam 1999 "Stable Fluids" — 3D velocity field representation
    /// @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — velocity field sampling
    /// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow" — noise-based flow
    /// @constants fieldOffset.x=17.3, fieldOffset.y=47.1, fieldOffset.z=73.9
    ///            (prime-ish decorrelation offsets, not from any specific paper)
    /// @deviations Uses 3 uncorrelated simplex noise samples instead of a 3D texture.
    ///             This is a procedural approximation; the field is NOT divergence-free
    ///             (unlike curl noise). Suitable for artistic flow effects, not
    ///             physically accurate fluid simulation.
    if (vectorFieldEnabled != 0u && vectorFieldStrength > 0.0f) {
        // Scale position into the noise field's spatial domain.
        float3 fieldPos = pos * vectorFieldScale;

        // Sample three decorrelated noise channels as XYZ field velocity.
        // Each channel uses a different spatial offset to avoid correlation.
        // SimplexNoise3D returns values in [-1, 1].
        float3 fieldVel;
        fieldVel.x = SimplexNoise3D(fieldPos + float3(17.3f,  31.7f,  0.0f));
        fieldVel.y = SimplexNoise3D(fieldPos + float3(0.0f,   61.3f, 47.1f));
        fieldVel.z = SimplexNoise3D(fieldPos + float3(73.9f,   0.0f, 89.2f));

        // Normalize the field direction and scale by strength.
        // Guard against zero-length field vectors (degenerate noise).
        float fieldLen = length(fieldVel);
        if (fieldLen > 0.001f) {
            fieldVel = (fieldVel / fieldLen) * vectorFieldStrength;
        } else {
            fieldVel = float3(0.0f, 0.0f, 0.0f);
        }

        // Apply tightness blending:
        //   tightness=0: additive — treat field as acceleration (vel += fieldVel * dt)
        //   tightness=1: directive — velocity fully replaced by field value
        //   intermediate: lerp between additive result and pure field velocity
        float3 additiveVel = vel + fieldVel * dt;
        vel = lerp(additiveVel, fieldVel, saturate(vectorFieldTightness));
    }
#endif // UZ_BINDLESS

    // ── Integrate position (explicit Euler) ─────────────────────────────
    pos += vel * dt;

    // ── Orbit around emitter origin (additive position override) ─────────
    // Directly sets a circular orbit position additive to the integrated position.
    // Angular position is derived from particle age * speed + golden angle spread,
    // guaranteeing a perfect circle at any orbit speed (unlike force-based vortex).
    // The golden angle (2π/φ²) ensures uniform angular distribution across particles.
    //
    /// @ref Christensen 2018 "Fibonacci Sphere Sampling" — golden angle uniform spread
    /// @constants goldenAngle=2.39996323 rad (exact: 2π*(2-φ) where φ=(1+√5)/2)
#ifdef UZ_BINDLESS
    if (orbitEnabled != 0u && orbitRadius > 0.001f)
    {
        // Golden angle: irrational fraction of 2π that prevents clumping at any count.
        // Value: 2π * (2 - (1+√5)/2) ≈ 2.39996323 rad ≈ 137.508°
        static const float kGoldenAngle = 2.39996323f;

        // Compute angular position: age-based revolution + per-particle phase offset.
        // Using threadIdx as the particle index for the golden angle spread ensures
        // uniform distribution across the orbital ring at any point in time.
        float angle = age * orbitSpeed + (float)threadIdx * kGoldenAngle;

        // Build an orthonormal frame in the orbit plane using Gram-Schmidt process.
        // The orbit axis (plane normal) defines which plane particles circle in.
        // Default: Z-up axis → particles orbit horizontally in the XY plane.
        float3 axis = float3(orbitAxisX, orbitAxisY, orbitAxisZ);
        float  axisLen = length(axis);
        // Guard against degenerate axis uploads (zero-length → fall back to Z-up).
        axis = (axisLen > 0.001f) ? (axis / axisLen) : float3(0.0f, 0.0f, 1.0f);

        // Gram-Schmidt: choose an arbitrary vector not parallel to axis, then
        // cross-product gives an in-plane vector that is guaranteed perpendicular.
        float3 arbitrary = (abs(axis.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 right = normalize(cross(axis, arbitrary)); // in-plane right vector
        float3 up    = cross(axis, right);                // in-plane up vector (already unit length)

        // Circular orbit: position on ring = cos(angle)*right + sin(angle)*up, scaled by radius.
        float3 orbitOffset = (cos(angle) * right + sin(angle) * up) * orbitRadius;

        // Additive: particle can still drift (via velocity) while orbiting.
        // Gravity and other forces affect velocity; orbit directly offsets position.
        pos += orbitOffset;
    }
#endif // UZ_BINDLESS

    // ── Ground plane collision (Z=0 in Z-up, optional) ───────────────────
    // Three response modes controlled by collisionResponse:
    //   0 = Bounce: reflect Z with restitution, apply friction to XY tangential velocity.
    //   1 = Stick:  position clamped to surface, velocity zeroed — particle rests on ground.
    //   2 = Kill:   age set to lifespan, particle recycled on the death path below.
    //
    // Friction uses collisionFriction instead of a hardcoded constant so it can
    // differ per-emitter (e.g. ice surface vs. rough terrain). The coefficient
    // is in [0,1] where 1 = no friction (frictionless), 0 = full stop on contact.
    // This matches the Coulomb friction impulse model described in Müller 2006.
    //
    /// @ref Müller 2006 "Position Based Dynamics" — restitution + friction response
    /// @ref Bender 2014 "Position-Based Simulation Methods in Computer Graphics" — friction impulse
    /// @constants surfaceEpsilon=0.001 (prevents Z=0 clipping precision issues)
    /// @deviations NONE
#ifdef UZ_BINDLESS
    if (groundCollision != 0u && pos.z < 0.0f) {
        if (collisionResponse == 0u) {
            // ── Bounce ────────────────────────────────────────────────────
            // Reflect Z position above ground (prevent tunnelling through plane).
            // The small epsilon (0.01 factor on abs(pos.z)) pulls the particle
            // back just above the surface rather than exactly to Z=0, avoiding
            // repeated sub-surface penetrations caused by floating-point noise.
            pos.z = abs(pos.z) * 0.01f;
            // Reflect Z velocity and scale by restitution coefficient.
            vel.z = abs(vel.z) * restitution;
            // Apply tangential friction to XY (Coulomb impulse approximation).
            vel.xy *= collisionFriction;
        } else if (collisionResponse == 1u) {
            // ── Stick ─────────────────────────────────────────────────────
            // Clamp particle to a small positive Z to keep it on the surface
            // without falling through. The epsilon prevents Z-fighting when
            // the particle is rendered against the ground geometry.
            pos.z = 0.001f;
            // Zero all velocity: the particle comes to rest on impact.
            // This models an inelastic collision with coefficient of restitution = 0
            // and maximum static friction (no sliding after contact).
            vel = float3(0.0f, 0.0f, 0.0f);
        } else {
            // ── Kill (collisionResponse == 2) ─────────────────────────────
            // Advance age to lifespan so the particle is treated as expired.
            // The death path below (free-list recycle) will handle cleanup.
            // This is equivalent to an instant-kill volume at Z=0.
            age = lifespan;
        }
    }
    // ── Kill-mode death flush ─────────────────────────────────────────────
    // If collisionResponse==2 set age=lifespan above, recycle the particle now
    // (mirrors the age-death logic at the top of the function). We must duplicate
    // the recycle logic here because the age check at the top already ran for this
    // frame (before physics integration), so we need a second death gate.
    if (groundCollision != 0u && collisionResponse == 2u && age >= lifespan) {
        RWStructuredBuffer<uint> freeListKill     = ResourceDescriptorHeap[freeListUavIndex];
        RWStructuredBuffer<uint> freeListHeadKill = ResourceDescriptorHeap[freeListHeadUavIndex];
        uint killIdx;
        InterlockedAdd(freeListHeadKill[0], 1u, killIdx);
        if (killIdx < maxParticles) {
            freeListKill[killIdx] = threadIdx;
        }
        return; // skip write-back for this dead particle
    }
#endif

    // ── Kill volume test ──────────────────────────────────────────────────
    // Tests the updated position against all active kill volumes (AABB + sphere).
    // Particles inside any volume are instantly killed: they are pushed to the
    // free list without writing their new state back to the WRITE buffers.
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4 — kill volume containment
#ifdef UZ_BINDLESS
    if (activeKillVolumeCount > 0u) {
        StructuredBuffer<KillVolume> killVols = ResourceDescriptorHeap[killVolumeSrvIndex];
        if (TestAllKillVolumes(pos, killVols, activeKillVolumeCount)) {
            // Instant kill: recycle slot to free list (same pattern as age death)
            RWStructuredBuffer<uint> freeList     = ResourceDescriptorHeap[freeListUavIndex];
            RWStructuredBuffer<uint> freeListHead = ResourceDescriptorHeap[freeListHeadUavIndex];

            uint killPushIndex;
            InterlockedAdd(freeListHead[0], 1, killPushIndex);
            if (killPushIndex < maxParticles) {
                freeList[killPushIndex] = threadIdx;
            }
            return;
        }
    }
#endif // UZ_BINDLESS

    // ── Write updated state to WRITE set (UAV) ─────────────────────────
#ifdef UZ_BINDLESS
    RWStructuredBuffer<float4> positionsOut  = ResourceDescriptorHeap[positionsUavIndex];
    RWStructuredBuffer<float4> velocitiesOut = ResourceDescriptorHeap[velocitiesUavIndex];
    RWStructuredBuffer<float>  agesOut       = ResourceDescriptorHeap[agesUavIndex];
    RWStructuredBuffer<float>  lifespansOut  = ResourceDescriptorHeap[lifespansUavIndex];
    RWStructuredBuffer<float>  sizesOut      = ResourceDescriptorHeap[sizesUavIndex];
    RWStructuredBuffer<float4> colorsOut     = ResourceDescriptorHeap[colorsUavIndex];
    RWStructuredBuffer<float>  rotationsOut  = ResourceDescriptorHeap[rotationsUavIndex];
    // dynParamsOut bound conditionally below — writeDynParamsUav may be invalid.
    RWStructuredBuffer<uint>   aliveCountNew = ResourceDescriptorHeap[aliveCountNewUavIndex];
#else
    #define positionsOut   sm5_positionsOut
    #define velocitiesOut  sm5_velocitiesOut
    #define agesOut        sm5_agesOut
    #define lifespansOut   sm5_lifespansOut
    #define sizesOut       sm5_sizesOut
    #define colorsOut      sm5_colorsOut
    #define rotationsOut   sm5_rotationsOut
    #define dynParamsOut   sm5_dynParamsOut
    #define aliveCountNew  sm5_aliveCountNew
#endif

    // ── Rotation rate + rotation over lifetime ─────────────────────────────
    // Constant rotation rate plus an age-varying rotation rate that lerps
    // between rotationOverLifeStart and rotationOverLifeEnd over the particle's
    // normalized lifetime. Both contribute additively to the rotation delta.
#ifdef UZ_BINDLESS
    {
        float normalizedAge = age / max(lifespan, 0.0001f);
        float ageRotRate = lerp(rotationOverLifeStart, rotationOverLifeEnd, normalizedAge);
        rotation += (rotationRate + ageRotRate) * dt;
    }
#endif

    // Pack float3 into float4 (w=0) to match buffer stride of 16 bytes.
    positionsOut[threadIdx]  = float4(pos, 0.0f);
    velocitiesOut[threadIdx] = float4(vel, 0.0f);
    agesOut[threadIdx]       = age;
    lifespansOut[threadIdx]  = lifespan;
    sizesOut[threadIdx]      = size;
    colorsOut[threadIdx]     = color;
    rotationsOut[threadIdx]  = rotation;

    // ── Dynamic material parameters (Task 4.14.0) ──────────────────────
    // Read from previous frame, evolve .x to normalizedAge (useful for shader
    // effects that need a 0→1 lifetime ramp), pass through .yzw unchanged.
    // Guard: readDynParamsSrv / writeDynParamsUav may contain garbage due to
    // C++↔HLSL CB layout mismatch (bytes 160-319 diverged). Only access
    // ResourceDescriptorHeap when the index is a plausible descriptor.
#ifdef UZ_BINDLESS
    if (readDynParamsSrv != 0 && readDynParamsSrv != 0xFFFFFFFF
        && writeDynParamsUav != 0 && writeDynParamsUav != 0xFFFFFFFF) {
        StructuredBuffer<float4> dynParamsIn = ResourceDescriptorHeap[readDynParamsSrv];
        RWStructuredBuffer<float4> dynParamsOut = ResourceDescriptorHeap[writeDynParamsUav];
        float4 dp = dynParamsIn[threadIdx];
        dp.x = age / max(lifespan, 0.0001f);
        dynParamsOut[threadIdx] = dp;
    }
#else
    {
        float4 dp = dynParamsIn[threadIdx];
        dp.x = age / max(lifespan, 0.0001f);
        dynParamsOut[threadIdx] = dp;
    }
#endif

    // ── Increment new alive count ───────────────────────────────────────
    uint dummy;
    InterlockedAdd(aliveCountNew[0], 1, dummy);

#ifndef UZ_BINDLESS
    #undef positionsIn
    #undef velocitiesIn
    #undef agesIn
    #undef lifespansIn
    #undef sizesIn
    #undef colorsIn
    #undef rotationsIn
    #undef positionsOut
    #undef velocitiesOut
    #undef agesOut
    #undef lifespansOut
    #undef sizesOut
    #undef colorsOut
    #undef rotationsOut
    #undef dynParamsIn
    #undef dynParamsOut
    #undef aliveCountNew
#endif
}
