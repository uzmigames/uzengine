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
/// - u0: RWStructuredBuffer<float4>         positions  (xyz=pos, w=unused)
/// - u1: RWStructuredBuffer<float4>         velocities (xyz=vel, w=unused)
/// - u2: RWStructuredBuffer<float>          ages
/// - u3: RWStructuredBuffer<float>          lifespans
/// - u4: RWStructuredBuffer<float>          sizes
/// - u5: RWStructuredBuffer<float4>         colors
/// - u6: RWStructuredBuffer<float>          rotations
/// - u7: RWStructuredBuffer<uint>           freeListHead (1 element)
/// - u8: RWStructuredBuffer<uint>           aliveCount   (1 element)
/// - u9: RWStructuredBuffer<float4>         dynamicParams

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
/// Total: 192 bytes (12 x float4).
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
    uint     positionsUavIndex;    ///< RWStructuredBuffer<float4> positions.    84
    uint     velocitiesUavIndex;   ///< RWStructuredBuffer<float4> velocities.   88
    uint     agesUavIndex;         ///< RWStructuredBuffer<float> ages.          92

    uint     lifespansUavIndex;    ///< RWStructuredBuffer<float> lifespans.     96
    uint     sizesUavIndex;        ///< RWStructuredBuffer<float> sizes.         100
    uint     colorsUavIndex;       ///< RWStructuredBuffer<float4> colors.       104
    uint     rotationsUavIndex;    ///< RWStructuredBuffer<float> rotations.     108

    // UAV: atomic counters
    uint     freeListHeadUavIndex; ///< RWStructuredBuffer<uint> (1 element).    112
    uint     aliveCountUavIndex;   ///< RWStructuredBuffer<uint> (1 element).    116

    // ── Spawn shape parameters ──────────────────────────────────────────
    uint     spawnShape;           ///< 0=Point,1=Sphere,2=Box,3=Ring,4=Cone,5=Cylinder,6=Disc,7=Line. 120
    uint     spawnOnSurface;       ///< 0=volume fill, 1=surface only.           124

    float    shapeParam1;          ///< Ring: width, Cone: half-angle, Cyl: height, Line: len. 128
    float    shapeParam2;          ///< Cone: height.                            132
    float    shapeParam3;          ///< Reserved.                                136
    uint     _pad0;                ///<                                          140

    // ── Initial rotation range + velocity inheritance ──────────────────── offset 144
    float    initialRotationMin;   ///< Minimum initial rotation (radians).      144
    float    initialRotationMax;   ///< Maximum initial rotation (radians).      148
    float    emitterVelocityX;     ///< Emitter movement velocity X (m/s).       152
    float    emitterVelocityY;     ///< Emitter movement velocity Y (m/s).       156

    float    emitterVelocityZ;     ///< Emitter movement velocity Z (m/s).       160
    float    velocityInheritance;  ///< Fraction of emitter vel added [0,1].     164
    uint     dynamicParamsUavIndex; ///< UAV for RWStructuredBuffer<float4> dynamic params. 168
    float    defaultDynParam0;     ///< Default dynamic param X (overwritten to normalizedAge in update). 172

    float    defaultDynParam1;     ///< Default dynamic param Y (user-defined).  176
    float    defaultDynParam2;     ///< Default dynamic param Z (user-defined).  180
    float    defaultDynParam3;     ///< Default dynamic param W (user-defined).  184
    uint     _padSpawnNew1;        ///<                                          188
#endif
};

#ifndef UZ_BINDLESS
/// SM5 fallback: explicit register bindings for particle buffers.
StructuredBuffer<uint>             sm5_freeList      : register(t0);
RWStructuredBuffer<float4>         sm5_positions     : register(u0);
RWStructuredBuffer<float4>         sm5_velocities    : register(u1);
RWStructuredBuffer<float>          sm5_ages          : register(u2);
RWStructuredBuffer<float>          sm5_lifespans     : register(u3);
RWStructuredBuffer<float>          sm5_sizes         : register(u4);
RWStructuredBuffer<float4>         sm5_colors        : register(u5);
RWStructuredBuffer<float>          sm5_rotations     : register(u6);
RWStructuredBuffer<uint>           sm5_freeListHead  : register(u7);
RWStructuredBuffer<uint>           sm5_aliveCount    : register(u8);
RWStructuredBuffer<float4>         sm5_dynamicParams : register(u9);
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
    RWStructuredBuffer<float4> positions    = ResourceDescriptorHeap[positionsUavIndex];
    RWStructuredBuffer<float4> velocities   = ResourceDescriptorHeap[velocitiesUavIndex];
    RWStructuredBuffer<float>  ages         = ResourceDescriptorHeap[agesUavIndex];
    RWStructuredBuffer<float>  lifespans    = ResourceDescriptorHeap[lifespansUavIndex];
    RWStructuredBuffer<float>  sizes        = ResourceDescriptorHeap[sizesUavIndex];
    RWStructuredBuffer<float4> colors       = ResourceDescriptorHeap[colorsUavIndex];
    RWStructuredBuffer<float>  rotations    = ResourceDescriptorHeap[rotationsUavIndex];
    RWStructuredBuffer<uint>   aliveCountUav = ResourceDescriptorHeap[aliveCountUavIndex];
    // dynamicParams bound conditionally — index 0 means not wired (no buffer allocated).
    // Accessing ResourceDescriptorHeap[0] as RWStructuredBuffer crashes (wrong descriptor type).
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
    #define dynamicParams sm5_dynamicParams
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

    // ── Position: shape-dependent spawn location ──────────────────────
    // spawnShape: 0=Point, 1=Sphere, 2=Box, 3=Ring, 4=Cone,
    //             5=Cylinder, 6=Disc, 7=Line
    // spawnOnSurface: 0=volume fill, 1=surface only
    float3 pos = emitterPos;
    float3 spawnNormal = float3(0, 0, 1); // default up — used for velocity direction on some shapes

    if (spawnShape == 0) {
        // ── Point: all particles from exact emitter origin ──────────
        // (pos already set to emitterPos)
    }
    else if (spawnShape == 1) {
        // ── Sphere: uniform volume or surface distribution ──────────
        if (spawnOnSurface != 0) {
            // Surface: random direction * exact radius
            float3 dir = RandomUnitVector(seed);
            pos = emitterPos + dir * spawnRadius;
            spawnNormal = dir;
        } else {
            // Volume: cube-root corrected radius for uniform distribution
            pos = emitterPos + RandomInSphere(seed, spawnRadius);
        }
    }
    else if (spawnShape == 2) {
        // ── Box: axis-aligned box with half-extents (spawnRadius, shapeParam1, shapeParam2)
        // spawnRadius = half-extent X, shapeParam1 = half-extent Y, shapeParam2 = half-extent Z
        float hx = spawnRadius;
        float hy = (shapeParam1 > 0.0f) ? shapeParam1 : spawnRadius;
        float hz = (shapeParam2 > 0.0f) ? shapeParam2 : spawnRadius;

        if (spawnOnSurface != 0) {
            // Surface: pick one of 6 faces weighted by area, then uniform point on that face
            float areaXY = hx * hy; // +Z and -Z faces
            float areaXZ = hx * hz; // +Y and -Y faces
            float areaYZ = hy * hz; // +X and -X faces
            float totalArea = 2.0f * (areaXY + areaXZ + areaYZ);
            float pick = RandomFloat01(seed) * totalArea;

            float lx = RandomFloat(seed, -hx, hx);
            float ly = RandomFloat(seed, -hy, hy);
            float lz = RandomFloat(seed, -hz, hz);

            if (pick < 2.0f * areaYZ) {
                // +X or -X face
                float sign = (pick < areaYZ) ? 1.0f : -1.0f;
                pos = emitterPos + float3(sign * hx, ly, lz);
                spawnNormal = float3(sign, 0, 0);
            } else if (pick < 2.0f * (areaYZ + areaXZ)) {
                // +Y or -Y face
                float sign = (pick < 2.0f * areaYZ + areaXZ) ? 1.0f : -1.0f;
                pos = emitterPos + float3(lx, sign * hy, lz);
                spawnNormal = float3(0, sign, 0);
            } else {
                // +Z or -Z face
                float sign = (pick < totalArea - areaXY) ? 1.0f : -1.0f;
                pos = emitterPos + float3(lx, ly, sign * hz);
                spawnNormal = float3(0, 0, sign);
            }
        } else {
            // Volume: uniform random inside the box
            pos = emitterPos + float3(
                RandomFloat(seed, -hx, hx),
                RandomFloat(seed, -hy, hy),
                RandomFloat(seed, -hz, hz)
            );
        }
    }
    else if (spawnShape == 3) {
        // ── Ring: torus in XY plane, major radius = spawnRadius, ring width = shapeParam1
        float angle = RandomFloat(seed, 0.0f, VFX_TWO_PI);
        float cosA = cos(angle);
        float sinA = sin(angle);
        float ringWidth = max(shapeParam1, 0.0f);
        float r = spawnRadius + RandomFloat(seed, -ringWidth, ringWidth);
        float zOffset = RandomFloat(seed, -ringWidth, ringWidth);
        pos = emitterPos + float3(cosA * r, sinA * r, zOffset);
        spawnNormal = float3(cosA, sinA, 0.0f);
    }
    else if (spawnShape == 4) {
        // ── Cone: particles on cone surface or volume
        // shapeParam1 = half-angle (radians), shapeParam2 = height along Z
        float angle = RandomFloat(seed, 0.0f, VFX_TWO_PI);
        float coneAngle = clamp(shapeParam1, 0.001f, 1.5707f); // clamp to (0, pi/2)
        float coneHeight = max(shapeParam2, 0.01f);
        float t = RandomFloat01(seed);

        // For uniform distribution on cone surface, weight by radius (linear in t)
        if (spawnOnSurface == 0) {
            // Volume fill: sqrt for uniform area distribution within cone cross-section
            t = sqrt(t);
        }

        float r = t * coneHeight * tan(coneAngle);
        float cosA = cos(angle);
        float sinA = sin(angle);
        pos = emitterPos + float3(cosA * r, sinA * r, t * coneHeight);

        // Normal perpendicular to cone surface
        float sinCone = sin(coneAngle);
        float cosCone = cos(coneAngle);
        spawnNormal = normalize(float3(cosA * cosCone, sinA * cosCone, sinCone));
    }
    else if (spawnShape == 5) {
        // ── Cylinder: height along Z, radius = spawnRadius, height = shapeParam1
        float angle = RandomFloat(seed, 0.0f, VFX_TWO_PI);
        float cosA = cos(angle);
        float sinA = sin(angle);
        float cylHeight = max(shapeParam1, 0.01f);
        float h = RandomFloat(seed, -cylHeight * 0.5f, cylHeight * 0.5f);

        if (spawnOnSurface != 0) {
            // Surface only: particles on the cylindrical shell
            pos = emitterPos + float3(cosA * spawnRadius, sinA * spawnRadius, h);
        } else {
            // Volume: sqrt-corrected radius for uniform area distribution
            float r = sqrt(RandomFloat01(seed)) * spawnRadius;
            pos = emitterPos + float3(cosA * r, sinA * r, h);
        }
        spawnNormal = float3(cosA, sinA, 0.0f);
    }
    else if (spawnShape == 6) {
        // ── Disc: flat circle in XY plane at emitter origin
        float angle = RandomFloat(seed, 0.0f, VFX_TWO_PI);
        float r;
        if (spawnOnSurface != 0) {
            // Edge only: particles on the circumference
            r = spawnRadius;
        } else {
            // Area: sqrt-corrected for uniform distribution
            r = sqrt(RandomFloat01(seed)) * spawnRadius;
        }
        pos = emitterPos + float3(cos(angle) * r, sin(angle) * r, 0.0f);
        // Normal is up for disc
    }
    else if (spawnShape == 7) {
        // ── Line: along X axis, length = shapeParam1
        float lineLen = max(shapeParam1, 0.01f);
        float t = RandomFloat(seed, -0.5f, 0.5f);
        pos = emitterPos + float3(t * lineLen, 0.0f, 0.0f);
        spawnNormal = float3(0, 0, 1); // perpendicular to line
    }

    // ── Velocity: base direction + cone spread + speed variation ────────
    float  baseSpeed = length(spawnVelocity);
    float3 baseDir   = (baseSpeed > 0.001f)
                     ? spawnVelocity / baseSpeed
                     : float3(0, 1, 0);  // Default upward if zero velocity

    // For shapes with meaningful normals (Ring, Cone, Cylinder), blend
    // spawnNormal into the velocity direction when spread is used.
    // This makes particles emit outward from the shape surface.
    float3 emitDir = baseDir;
    if (spawnShape >= 3 && spawnShape <= 5 && baseSpeed < 0.001f) {
        // If no explicit velocity set, use the shape surface normal
        emitDir = spawnNormal;
    }

    float3 dir = (spreadAngle > 0.001f)
               ? RandomInCone(seed, emitDir, spreadAngle)
               : emitDir;

    float finalSpeed = baseSpeed + RandomFloat(seed, -velocityVariation,
                                                      velocityVariation);
    // For shapes with normals and no explicit velocity, give a default outward speed
    if (baseSpeed < 0.001f && spawnShape >= 3 && spawnShape <= 5) {
        finalSpeed = max(finalSpeed, 1.0f);
    }
    float3 vel = dir * max(finalSpeed, 0.0f);

    // ── Lifetime: uniform random between min and max ────────────────────
    float lifespan = RandomFloat(seed, minLifetime, maxLifetime);

    // ── Velocity inheritance: add emitter movement velocity ────────────
    // When the emitter is moving, newly spawned particles inherit a fraction
    // of its velocity. The CPU computes the emitter velocity from position
    // delta and pre-multiplies by the inheritance factor.
#ifdef UZ_BINDLESS
    vel += float3(emitterVelocityX, emitterVelocityY, emitterVelocityZ) * velocityInheritance;
#endif

    // ── Initial rotation: configurable min/max range ─────────────────────
#ifdef UZ_BINDLESS
    float rotation = RandomFloat(seed, initialRotationMin, initialRotationMax);
#else
    float rotation = RandomFloat(seed, 0.0f, VFX_TWO_PI);
#endif

    // ── Write particle state to WRITE set buffers ───────────────────────
    // Pack float3 into float4 (w=0) to match buffer stride of 16 bytes.
    // The billboard VS reads as StructuredBuffer<float4> and extracts .xyz.
    positions[particleIdx]  = float4(pos, 0.0f);
    velocities[particleIdx] = float4(vel, 0.0f);
    ages[particleIdx]       = 0.0f;
    lifespans[particleIdx]  = lifespan;
    sizes[particleIdx]      = initialSize;
    colors[particleIdx]     = initialColor;
    rotations[particleIdx]  = rotation;

    // ── Dynamic material parameters (Task 4.14.0) ──────────────────────
    // Initialize per-particle float4 from emitter defaults. The update CS
    // overwrites .x with normalizedAge each frame; .yzw remain user-defined.
#ifdef UZ_BINDLESS
    if (dynamicParamsUavIndex != 0 && dynamicParamsUavIndex != 0xFFFFFFFF) {
        RWStructuredBuffer<float4> dynParams = ResourceDescriptorHeap[dynamicParamsUavIndex];
        dynParams[particleIdx] = float4(defaultDynParam0, defaultDynParam1,
                                        defaultDynParam2, defaultDynParam3);
    }
#else
    dynamicParams[particleIdx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#endif

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
    #undef dynamicParams
#endif
}
