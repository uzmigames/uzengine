/// GPU Particle Update Compute Shader
///
/// Advances particle simulation by one time step: applies gravity, damping,
/// and Euler integration, then tests for particle death (age >= lifespan).
/// Dead particles are recycled back to the free list for reuse by the spawn
/// shader.
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
/// 4. Alive particles: apply gravity, apply frame-rate-independent damping,
///    integrate position, write to WRITE set, increment alive_count_new.
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
/// - b0: VFXUpdateConstants (simulation params + bindless descriptor indices)
///
/// @ref Harada 2007 "Real-Time Particle System on the GPU"
/// @constants UPDATE_GROUP_SIZE=256
/// @deviations Linear damping approximation instead of exponential decay
///
/// # SM5 Register Layout (non-bindless fallback)
/// SRV (READ set):
/// - t0: StructuredBuffer<float3>           positionsIn
/// - t1: StructuredBuffer<float3>           velocitiesIn
/// - t2: StructuredBuffer<float>            agesIn
/// - t3: StructuredBuffer<float>            lifespansIn
/// - t4: StructuredBuffer<float>            sizesIn
/// - t5: StructuredBuffer<float4>           colorsIn
/// - t6: StructuredBuffer<float>            rotationsIn
/// UAV (WRITE set + shared):
/// - u0: RWStructuredBuffer<float3>         positionsOut
/// - u1: RWStructuredBuffer<float3>         velocitiesOut
/// - u2: RWStructuredBuffer<float>          agesOut
/// - u3: RWStructuredBuffer<float>          lifespansOut
/// - u4: RWStructuredBuffer<float>          sizesOut
/// - u5: RWStructuredBuffer<float4>         colorsOut
/// - u6: RWStructuredBuffer<float>          rotationsOut
/// - u7: RWStructuredBuffer<uint>           freeList
/// - u8: RWStructuredBuffer<uint>           freeListHead (1 element)
/// - u9: RWStructuredBuffer<uint>           aliveCountNew (1 element)

#include "../include/vfx_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Thread Group Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Threads per group for update dispatch. Matches spawn shader for consistency.
static const uint UPDATE_GROUP_SIZE = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0) -- Simulation Parameters + Bindless Indices
// ─────────────────────────────────────────────────────────────────────────────

/// Update constants uploaded by the CPU each frame. Contains physics parameters
/// and descriptor heap indices for both READ (SRV) and WRITE (UAV) buffer sets.
///
/// Layout is 16-byte aligned per HLSL packing rules. Total: 112 bytes.
cbuffer VFXUpdateConstants : register(b0) {
    // ── Simulation Parameters ───────────────────────────────────────────
    float3   gravity;              ///< Gravitational acceleration (m/s^2).      0
                                   ///< Typical: (0, -9.81, 0) for downward.
    float    dt;                   ///< Frame delta time in seconds.             12

    float    damping;              ///< Velocity damping coefficient [0, inf).   16
                                   ///< 0 = no damping. Higher = more drag.
                                   ///< Applied as vel *= (1 - damping * dt).
    uint     aliveCount;           ///< Number of alive particles (prev frame).  20
    uint     frameNumber;          ///< Monotonically increasing frame counter.  24
    uint     maxParticles;         ///< Total particle pool capacity.            28

#ifdef UZ_BINDLESS
    // ── Bindless Descriptor Indices: READ set (SRV) ─────────────────────
    uint     positionsSrvIndex;    ///< StructuredBuffer<float3> positions.      32
    uint     velocitiesSrvIndex;   ///< StructuredBuffer<float3> velocities.     36
    uint     agesSrvIndex;         ///< StructuredBuffer<float> ages.            40
    uint     lifespansSrvIndex;    ///< StructuredBuffer<float> lifespans.       44

    uint     sizesSrvIndex;        ///< StructuredBuffer<float> sizes.           48
    uint     colorsSrvIndex;       ///< StructuredBuffer<float4> colors.         52
    uint     rotationsSrvIndex;    ///< StructuredBuffer<float> rotations.       56
    uint     _padSrv0;             ///<                                          60

    // ── Bindless Descriptor Indices: WRITE set (UAV) ────────────────────
    uint     positionsUavIndex;    ///< RWStructuredBuffer<float3> positions.    64
    uint     velocitiesUavIndex;   ///< RWStructuredBuffer<float3> velocities.   68
    uint     agesUavIndex;         ///< RWStructuredBuffer<float> ages.          72
    uint     lifespansUavIndex;    ///< RWStructuredBuffer<float> lifespans.     76

    uint     sizesUavIndex;        ///< RWStructuredBuffer<float> sizes.         80
    uint     colorsUavIndex;       ///< RWStructuredBuffer<float4> colors.       84
    uint     rotationsUavIndex;    ///< RWStructuredBuffer<float> rotations.     88
    uint     _padUav0;             ///<                                          92

    // ── Bindless Descriptor Indices: Shared Buffers ─────────────────────
    uint     freeListUavIndex;     ///< RWStructuredBuffer<uint> free_list.      96
    uint     freeListHeadUavIndex; ///< RWStructuredBuffer<uint> (1 element).    100
    uint     aliveCountNewUavIndex;///< RWStructuredBuffer<uint> (1 element).    104
                                   ///< New alive count for current frame.
                                   ///< Must be reset to 0 before dispatch.
    uint     _padShared0;          ///<                                          108
#endif
};

#ifndef UZ_BINDLESS
/// SM5 fallback: explicit register bindings for READ set (SRV).
StructuredBuffer<float3>           sm5_positionsIn   : register(t0);
StructuredBuffer<float3>           sm5_velocitiesIn  : register(t1);
StructuredBuffer<float>            sm5_agesIn        : register(t2);
StructuredBuffer<float>            sm5_lifespansIn   : register(t3);
StructuredBuffer<float>            sm5_sizesIn       : register(t4);
StructuredBuffer<float4>           sm5_colorsIn      : register(t5);
StructuredBuffer<float>            sm5_rotationsIn   : register(t6);

/// SM5 fallback: explicit register bindings for WRITE set (UAV) + shared.
RWStructuredBuffer<float3>         sm5_positionsOut   : register(u0);
RWStructuredBuffer<float3>         sm5_velocitiesOut  : register(u1);
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
/// - Integrates position via explicit Euler.
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
    StructuredBuffer<float3> positionsIn  = ResourceDescriptorHeap[positionsSrvIndex];
    StructuredBuffer<float3> velocitiesIn = ResourceDescriptorHeap[velocitiesSrvIndex];
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
    float3 pos      = positionsIn[threadIdx];
    float3 vel      = velocitiesIn[threadIdx];
    float  age      = agesIn[threadIdx];
    float  lifespan = lifespansIn[threadIdx];
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

    // ── Integrate position (explicit Euler) ─────────────────────────────
    pos += vel * dt;

    // ── Write updated state to WRITE set (UAV) ─────────────────────────
#ifdef UZ_BINDLESS
    RWStructuredBuffer<float3> positionsOut  = ResourceDescriptorHeap[positionsUavIndex];
    RWStructuredBuffer<float3> velocitiesOut = ResourceDescriptorHeap[velocitiesUavIndex];
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

    positionsOut[threadIdx]  = pos;
    velocitiesOut[threadIdx] = vel;
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
