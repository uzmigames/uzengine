/// GPU Particle Update Compute Shader
///
/// Advances particle simulation by one time step: applies gravity, damping,
/// optional curl noise, optional vortex force, optional wind zone force, kill
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
///    wind zone force. Integrate position.
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

#include "../include/vfx_common.hlsli"
#include "vfx_curl_noise.hlsli"
#include "vfx_vortex.hlsli"
#include "vfx_wind.hlsli"
#include "vfx_kill_volumes.hlsli"

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
/// Layout is 16-byte aligned per HLSL packing rules. Total: 208 bytes (13 float4).
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
    float    _padVortex0;                ///<                                        164
    float    _padVortex1;                ///<                                        168
    float    _padVortex2;                ///<                                        172

    // ── Wind Zone GPU Buffer ─────────────────────────────────────────────── offset 176
    /// @ref Stam 1999 "Stable Fluids" — wind field accumulation
    uint     windZoneSrvIndex;           ///< SRV for StructuredBuffer<WindZone>.    176
    uint     activeWindZoneCount;        ///< Active zone count [0..kMaxWindZones].  180
    uint     _padWind0;                  ///<                                        184
    uint     _padWind1;                  ///<                                        188

    // ── Kill Volume GPU Buffer ───────────────────────────────────────────── offset 192
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    uint     killVolumeSrvIndex;         ///< SRV for StructuredBuffer<KillVolume>.  192
    uint     activeKillVolumeCount;      ///< Active volume count [0..16].           196
    uint     _padKV0;                    ///<                                        200
    uint     _padKV1;                    ///<                                        204
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
#else
    #define positionsIn   sm5_positionsIn
    #define velocitiesIn  sm5_velocitiesIn
    #define agesIn        sm5_agesIn
    #define lifespansIn   sm5_lifespansIn
    #define sizesIn       sm5_sizesIn
    #define colorsIn      sm5_colorsIn
    #define rotationsIn   sm5_rotationsIn
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

#ifndef UZ_BINDLESS
        #undef freeList
        #undef freeListHead
#endif
        return;
    }

    // ── Physics: gravity + frame-rate-independent damping ───────────────

    // Gravity: constant acceleration
    vel += gravity * dt;

    // Damping: multiplicative decay, linear approximation of exp(-damping * dt).
    // Clamp to [0, 1] to prevent velocity reversal at very large dt * damping.
    float dampFactor = saturate(1.0f - damping * dt);
    vel *= dampFactor;

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
#endif // UZ_BINDLESS

    // ── Integrate position (explicit Euler) ─────────────────────────────
    pos += vel * dt;

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
    RWStructuredBuffer<uint>   aliveCountNew = ResourceDescriptorHeap[aliveCountNewUavIndex];
#else
    #define positionsOut   sm5_positionsOut
    #define velocitiesOut  sm5_velocitiesOut
    #define agesOut        sm5_agesOut
    #define lifespansOut   sm5_lifespansOut
    #define sizesOut       sm5_sizesOut
    #define colorsOut      sm5_colorsOut
    #define rotationsOut   sm5_rotationsOut
    #define aliveCountNew  sm5_aliveCountNew
#endif

    // Pack float3 into float4 (w=0) to match buffer stride of 16 bytes.
    positionsOut[threadIdx]  = float4(pos, 0.0f);
    velocitiesOut[threadIdx] = float4(vel, 0.0f);
    agesOut[threadIdx]       = age;
    lifespansOut[threadIdx]  = lifespan;
    sizesOut[threadIdx]      = size;
    colorsOut[threadIdx]     = color;
    rotationsOut[threadIdx]  = rotation;

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
    #undef aliveCountNew
#endif
}
