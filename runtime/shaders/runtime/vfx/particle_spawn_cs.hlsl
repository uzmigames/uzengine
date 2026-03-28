/// GPU Particle Spawn Compute Shader
///
/// Initializes newly emitted particles by acquiring free slots from a
/// lock-free free list (stack) and writing initial state to the ping-pong
/// write buffers. Each thread spawns exactly one particle (if a free slot
/// is available).
///
/// # Free List Protocol
/// The free list is a stack: `free_list[0..free_list_head-1]` contains indices
/// of dead/unused particle slots. Spawning decrements `free_list_head` via
/// InterlockedAdd(-1) and reads the freed index at `free_list[oldHead - 1]`.
/// If the stack is empty (head reaches 0), the thread exits without spawning.
///
/// # Ping-Pong Strategy
/// Spawn writes ONLY to the WRITE set (UAV). It never reads from the READ set
/// because newly spawned particles have no prior state. The CPU swaps which
/// set is READ vs WRITE each frame by flipping descriptor indices in the cbuffer.
///
/// # RNG Seeding
/// Each particle gets a unique seed: VFXSeed(particleIndex + spawnIndex, frameNumber).
/// Since particleIndex comes from the free list (unpredictable order) and spawnIndex
/// provides additional entropy, this produces well-distributed values without
/// persistent per-particle RNG state.
///
/// # Bindless Resource Access (SM6.6)
/// All SoA buffers are accessed via ResourceDescriptorHeap[index] using
/// descriptor indices stored in the constant buffer. This avoids fixed register
/// assignments and allows the CPU to freely remap buffers between ping-pong sets.
///
/// # Thread Group
/// [256, 1, 1] -- one thread per particle to spawn.
/// Dispatch: ceil(spawnCount / 256) groups. Threads beyond spawnCount exit early.
///
/// # Root Signature
/// - b0: VFXSpawnConstants (emitter params + bindless descriptor indices)
///
/// @ref UzEngine VFX system -- GPU particle simulation pipeline
/// @constants SPAWN_GROUP_SIZE=256
/// @deviations NONE
///
/// # SM5 Register Layout (non-bindless fallback)
/// - t0: StructuredBuffer<uint>             freeList
/// - u0: RWStructuredBuffer<float3>         positions
/// - u1: RWStructuredBuffer<float3>         velocities
/// - u2: RWStructuredBuffer<float>          ages
/// - u3: RWStructuredBuffer<float>          lifespans
/// - u4: RWStructuredBuffer<float>          sizes
/// - u5: RWStructuredBuffer<float4>         colors
/// - u6: RWStructuredBuffer<float>          rotations
/// - u7: RWStructuredBuffer<uint>           freeListHead (1 element)
/// - u8: RWStructuredBuffer<uint>           aliveCount   (1 element)

#include "../include/vfx_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Thread Group Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Threads per group for spawn dispatch. 256 is the standard for 1D particle
/// workloads -- high occupancy on all major GPU architectures (GCN, RDNA, Ada).
static const uint SPAWN_GROUP_SIZE = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0) -- Emitter Parameters + Bindless Indices
// ─────────────────────────────────────────────────────────────────────────────

/// Spawn constants uploaded by the CPU each frame. Contains emitter parameters
/// for initializing particle state and descriptor heap indices for bindless
/// buffer access.
///
/// Layout is tightly packed with explicit padding for 16-byte CBV alignment.
/// Total: 128 bytes (8 x float4).
cbuffer VFXSpawnConstants : register(b1) {
    // ── Emitter Parameters ──────────────────────────────────────────────
    float3   emitterPos;           ///< World-space emitter origin.              0
    float    spawnRadius;          ///< Random offset sphere radius.             12

    float3   spawnVelocity;        ///< Base emission velocity (dir + speed).    16
    float    velocityVariation;    ///< +/- speed randomization range.           28

    float    spreadAngle;          ///< Half-cone angle in radians (0 = laser).  32
    float    minLifetime;          ///< Minimum particle lifespan in seconds.    36
    float    maxLifetime;          ///< Maximum particle lifespan in seconds.    40
    float    initialSize;          ///< Initial billboard size (world units).    44

    float4   initialColor;         ///< RGBA initial particle color (linear).    48

    float    dt;                   ///< Frame delta time in seconds.             64
    uint     frameNumber;          ///< Monotonically increasing frame counter.  68
    uint     spawnCount;           ///< Number of particles to spawn this frame. 72
    uint     maxParticles;         ///< Total particle pool capacity.            76

#ifdef UZ_BINDLESS
    // ── Bindless Descriptor Indices (SM6.6) ─────────────────────────────
    // SRV: free list stack (read to get free index)
    uint     freeListSrvIndex;     ///< StructuredBuffer<uint> free_list.        80

    // UAV: write set particle attribute buffers
    uint     positionsUavIndex;    ///< RWStructuredBuffer<float3> positions.    84
    uint     velocitiesUavIndex;   ///< RWStructuredBuffer<float3> velocities.   88
    uint     agesUavIndex;         ///< RWStructuredBuffer<float> ages.          92

    uint     lifespansUavIndex;    ///< RWStructuredBuffer<float> lifespans.     96
    uint     sizesUavIndex;        ///< RWStructuredBuffer<float> sizes.         100
    uint     colorsUavIndex;       ///< RWStructuredBuffer<float4> colors.       104
    uint     rotationsUavIndex;    ///< RWStructuredBuffer<float> rotations.     108

    // UAV: atomic counters
    uint     freeListHeadUavIndex; ///< RWStructuredBuffer<uint> (1 element).    112
    uint     aliveCountUavIndex;   ///< RWStructuredBuffer<uint> (1 element).    116
    uint     _pad0;                ///<                                          120
    uint     _pad1;                ///<                                          124
#endif
};

#ifndef UZ_BINDLESS
/// SM5 fallback: explicit register bindings for particle buffers.
StructuredBuffer<uint>             sm5_freeList      : register(t0);
RWStructuredBuffer<float3>         sm5_positions     : register(u0);
RWStructuredBuffer<float3>         sm5_velocities    : register(u1);
RWStructuredBuffer<float>          sm5_ages          : register(u2);
RWStructuredBuffer<float>          sm5_lifespans     : register(u3);
RWStructuredBuffer<float>          sm5_sizes         : register(u4);
RWStructuredBuffer<float4>         sm5_colors        : register(u5);
RWStructuredBuffer<float>          sm5_rotations     : register(u6);
RWStructuredBuffer<uint>           sm5_freeListHead  : register(u7);
RWStructuredBuffer<uint>           sm5_aliveCount    : register(u8);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Spawns one particle per thread by popping a free index from the GPU stack
/// and initializing all SoA attribute buffers.
///
/// # Parameters
/// - dtid: SV_DispatchThreadID.x identifies which spawn slot [0..spawnCount-1].
///
/// # Side Effects
/// - Decrements free_list_head atomically (pops free index).
/// - Increments alive_count atomically.
/// - Writes initial state to all WRITE set UAVs at the claimed index.
[numthreads(SPAWN_GROUP_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint spawnIdx = dtid.x;

    // ── Early exit for excess threads ───────────────────────────────────
    if (spawnIdx >= spawnCount) {
        return;
    }

    // ── Bind resources ──────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    RWStructuredBuffer<uint>   freeListHead = ResourceDescriptorHeap[freeListHeadUavIndex];
    StructuredBuffer<uint>     freeList     = ResourceDescriptorHeap[freeListSrvIndex];
    RWStructuredBuffer<float3> positions    = ResourceDescriptorHeap[positionsUavIndex];
    RWStructuredBuffer<float3> velocities   = ResourceDescriptorHeap[velocitiesUavIndex];
    RWStructuredBuffer<float>  ages         = ResourceDescriptorHeap[agesUavIndex];
    RWStructuredBuffer<float>  lifespans    = ResourceDescriptorHeap[lifespansUavIndex];
    RWStructuredBuffer<float>  sizes        = ResourceDescriptorHeap[sizesUavIndex];
    RWStructuredBuffer<float4> colors       = ResourceDescriptorHeap[colorsUavIndex];
    RWStructuredBuffer<float>  rotations    = ResourceDescriptorHeap[rotationsUavIndex];
    RWStructuredBuffer<uint>   aliveCountUav = ResourceDescriptorHeap[aliveCountUavIndex];
#else
    #define freeListHead  sm5_freeListHead
    #define freeList      sm5_freeList
    #define positions     sm5_positions
    #define velocities    sm5_velocities
    #define ages          sm5_ages
    #define lifespans     sm5_lifespans
    #define sizes         sm5_sizes
    #define colors        sm5_colors
    #define rotations     sm5_rotations
    #define aliveCountUav sm5_aliveCount
#endif

    // ── Acquire a free slot from the stack ──────────────────────────────
    // Atomically decrement the head counter. The returned value is the
    // pre-decrement head, so we read freeList[prevHead - 1].
    uint prevHead;
    InterlockedAdd(freeListHead[0], -1, prevHead);

    // Pool exhausted -- undo the decrement and bail out.
    if (prevHead == 0 || prevHead > maxParticles) {
        InterlockedAdd(freeListHead[0], 1);
        return;
    }

    // Read the particle buffer index from the free list
    uint particleIdx = freeList[prevHead - 1];

    // Bounds check the index from the free list
    if (particleIdx >= maxParticles) {
        InterlockedAdd(freeListHead[0], 1);
        return;
    }

    // ── Initialize RNG ──────────────────────────────────────────────────
    // Unique seed per particle per frame prevents correlated spawns.
    // Adding spawnIdx provides extra entropy when the same particleIdx
    // is recycled across different spawn batches.
    uint seed = VFXSeed(particleIdx + spawnIdx, frameNumber);

    // ── Position: emitter origin + random sphere offset ─────────────────
    float3 pos = emitterPos + RandomInSphere(seed, spawnRadius);

    // ── Velocity: base direction + cone spread + speed variation ────────
    float  baseSpeed = length(spawnVelocity);
    float3 baseDir   = (baseSpeed > 0.001f)
                     ? spawnVelocity / baseSpeed
                     : float3(0, 1, 0);  // Default upward if zero velocity

    float3 dir = (spreadAngle > 0.001f)
               ? RandomInCone(seed, baseDir, spreadAngle)
               : baseDir;

    float finalSpeed = baseSpeed + RandomFloat(seed, -velocityVariation,
                                                      velocityVariation);
    float3 vel = dir * max(finalSpeed, 0.0f);

    // ── Lifetime: uniform random between min and max ────────────────────
    float lifespan = RandomFloat(seed, minLifetime, maxLifetime);

    // ── Initial rotation: random full circle for visual variety ─────────
    float rotation = RandomFloat(seed, 0.0f, VFX_TWO_PI);

    // ── Write particle state to WRITE set buffers ───────────────────────
    positions[particleIdx]  = pos;
    velocities[particleIdx] = vel;
    ages[particleIdx]       = 0.0f;
    lifespans[particleIdx]  = lifespan;
    sizes[particleIdx]      = initialSize;
    colors[particleIdx]     = initialColor;
    rotations[particleIdx]  = rotation;

    // ── Increment alive count ───────────────────────────────────────────
    uint dummy;
    InterlockedAdd(aliveCountUav[0], 1, dummy);

#ifndef UZ_BINDLESS
    #undef freeListHead
    #undef freeList
    #undef positions
    #undef velocities
    #undef ages
    #undef lifespans
    #undef sizes
    #undef colors
    #undef rotations
    #undef aliveCountUav
#endif
}
