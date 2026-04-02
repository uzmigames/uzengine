/// GPU Particle Death Spawn Compute Shader (Task 4.11.0)
///
/// Secondary spawn pass: reads death events written by the update CS this frame
/// and emits `deathSpawnCount` child particles at each death position. Children
/// inherit the parent's color and are launched in a cone around the parent's
/// final velocity direction with configurable spread and speed scale.
///
/// # Algorithm (per thread = per death event)
/// 1. Read death event [position, velocity, color] from StructuredBuffer.
/// 2. For each child i in [0, deathSpawnCount):
///    a. Compute a spread direction using a low-discrepancy fibonacci spiral
///       rotated to align with the parent's death velocity.
///    b. Pop a free particle slot from the free-list stack (InterlockedAdd -1).
///    c. If a slot is available, write child particle state to all SoA UAVs.
///    d. Increment aliveCount atomically.
/// 3. Children use the parent's color, a configurable lifetime, and size from
///    VFXDeathSpawnCB. The RNG seed mixes event index + child index + frame
///    number for deterministic but uncorrelated per-child variation.
///
/// # Coordinate System
/// All positions and velocities are in world-space centimetres (Z-up, left-handed)
/// consistent with the rest of UzEngine VFX simulation.
///
/// # Fibonacci Cone Sampling
/// Each child i gets a deterministic direction within the spread cone using the
/// Fibonacci spiral on the hemisphere. This avoids expensive RNG and gives
/// uniform coverage even for low child counts (4-8).
///
///   phi_i = i * golden_angle_2d  (golden ratio, avoids clumping)
///   theta_i = arccos(1 - spread_t * i / max(N-1, 1))
///   where spread_t = 1 - cos(halfAngle)  (maps half-angle to z-range)
///
/// The resulting unit vector is then rotated into the parent velocity direction
/// via a Rodrigues-style TBN frame (no matrix multiply needed).
///
/// # Dispatch
/// Dispatch: ceil(deathEventCount / 64) thread groups, 64x1x1.
/// The caller skips dispatch when deathEventCount == 0 (checked CPU-side).
/// Threads beyond deathEventCount early-exit via the guard at the top.
///
/// # Root Signature
/// - b1: VFXDeathSpawnCB (event buffer index, child params, write-set UAV indices)
///
/// @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
/// @ref Halton 1960 — low-discrepancy sequence basis; here we use Fibonacci spiral
/// @ref Lagae 2008 "A Comparison of Methods for Generating Poisson Disk Distributions"
///      — Fibonacci spiral hemisphere sampling
/// @constants DEATH_SPAWN_GROUP_SIZE=64, golden_angle_2d=2.39996323 rad
/// @deviations Fibonacci spiral instead of random hemisphere: deterministic, no RNG state.

#include "../include/vfx_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Thread Group Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// 64 threads per group: one thread per death event. Lower than spawn/update (256)
/// because death events are usually sparse (< 1000/frame) and smaller groups
/// improve occupancy when event counts are low.
static const uint DEATH_SPAWN_GROUP_SIZE = 64;

// ─────────────────────────────────────────────────────────────────────────────
// Death Event Struct (GPU-side mirror of DeathEvent in vfx_viewport_vfx.h)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
struct DeathEvent {
    float3 position; ///< World-space position at death.
    float  _pad0;
    float3 velocity; ///< Velocity at death — seed for child spawn direction.
    float  _pad1;
    float4 color;    ///< RGBA color at death — inherited by child particles.
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b1) — Death Spawn Parameters + Bindless Indices
// ─────────────────────────────────────────────────────────────────────────────

/// Uploaded each frame by the CPU. Contains the death event buffer SRV index,
/// child particle parameters, and the write-set UAV descriptor indices.
///
/// Total: 96 bytes (6 x float4). Must match VFXDeathSpawnCB in vfx_viewport_vfx.h.
cbuffer VFXDeathSpawnConstants : register(b1) {
    // ── Event buffer ────────────────────────────────────────────────── offset 0
    uint     deathEventBufferSrvIndex;   ///< SRV for StructuredBuffer<DeathEvent>.  0
    uint     deathEventCountSrvIndex;    ///< SRV for StructuredBuffer<uint> count (GPU atomic, read back here). 4
    uint     deathSpawnCount;            ///< Children spawned per death event.       8
    float    deathSpawnSpread;           ///< Cone half-angle (rad) [0, pi].          12

    // ── Child particle parameters ────────────────────────────────────── offset 16
    float    deathSpawnSpeedScale;       ///< child_speed = parent_speed * scale.     16
    uint     maxParticles;               ///< Total particle pool capacity.           20
    uint     maxDeathEvents;             ///< Hard cap on events per frame (guard).   24
    uint     _pad0;                      ///<                                         28

    // ── Write-set UAV indices ─────────────────────────────────────────── offset 32
    uint     positionsUavIndex;          ///< RWStructuredBuffer<float4> positions.   32
    uint     velocitiesUavIndex;         ///< RWStructuredBuffer<float4> velocities.  36
    uint     agesUavIndex;               ///< RWStructuredBuffer<float> ages.         40
    uint     lifespansUavIndex;          ///< RWStructuredBuffer<float> lifespans.    44

    uint     sizesUavIndex;              ///< RWStructuredBuffer<float> sizes.        48
    uint     colorsUavIndex;             ///< RWStructuredBuffer<float4> colors.      52
    uint     rotationsUavIndex;          ///< RWStructuredBuffer<float> rotations.    56
    uint     freeListUavIndex;           ///< RWStructuredBuffer<uint> freeList.      60

    uint     freeListHeadUavIndex;       ///< RWStructuredBuffer<uint> freeListHead.  64
    uint     aliveCountUavIndex;         ///< RWStructuredBuffer<uint> aliveCount.    68
    uint     frameNumber;                ///< Frame counter (used in RNG seed).       72
    uint     dynamicParamsUavIndex;      ///< RWStructuredBuffer<float4> dynamicParams (Task 4.14.0). 76

    // ── Child emitter lifetime + size ─────────────────────────────────── offset 80
    float    childLifetime;              ///< Lifespan for child particles (seconds). 80
    float    childSize;                  ///< Initial billboard size for children.    84
    float    _pad3;                      ///<                                         88
    float    _pad4;                      ///<                                         92
};

// ─────────────────────────────────────────────────────────────────────────────
// Fibonacci Cone Sampling Helper
// ─────────────────────────────────────────────────────────────────────────────

/// Builds an orthonormal TBN frame around `dir` for rotating a local-space
/// cone sample into world space. Uses Gram-Schmidt with a numerically stable
/// fallback axis choice (avoids the degenerate case when dir ≈ ±Z).
///
/// # Arguments
/// - `dir`   : normalized world-space cone axis (parent death velocity direction).
/// - `tangent`: receives the right-hand basis vector in the cone's plane.
/// - `bitangent`: receives the up basis vector in the cone's plane.
void BuildTBN(float3 dir, out float3 tangent, out float3 bitangent)
{
    // Choose an arbitrary up vector that is not parallel to dir.
    // Using abs(dir.z) < 0.9 selects world Z-up when dir is mostly horizontal,
    // and falls back to world X when dir is near-vertical. This prevents the
    // cross product from degenerating to a near-zero vector.
    float3 up = (abs(dir.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);

    // Gram-Schmidt: project out the component of 'up' along 'dir', then normalise.
    tangent   = normalize(cross(up, dir));
    bitangent = cross(dir, tangent); // Already unit length (dir and tangent are orthonormal).
}

/// Samples a direction within a cone using a Fibonacci spiral for uniform coverage.
///
/// The Fibonacci spiral places N points uniformly on the unit sphere using the
/// golden angle (≈137.508°). For small N (4-32 children per death), this avoids
/// the clumping artifacts of random sampling with no per-particle RNG state.
///
/// # Arguments
/// - `parentDir`   : normalized cone axis (aligned to parent's death velocity).
/// - `i`           : child index [0, N).
/// - `N`           : total children per death event (deathSpawnCount).
/// - `halfAngle`   : cone half-angle in radians (deathSpawnSpread).
/// - `seed`        : per-child RNG seed for additional speed variation.
///
/// # Returns
/// A unit direction vector within the spread cone, rotated to align with parentDir.
///
/// @ref Lagae 2008 "A Comparison of Methods for Generating Poisson Disk Distributions"
/// @constants golden_angle=2.39996323 rad (2π/φ², φ=(1+√5)/2)
float3 FibonacciConeDirection(float3 parentDir, uint i, uint N,
                              float halfAngle, inout uint seed)
{
    // Golden angle: 2π * (2 - φ) ≈ 2.39996323 rad. Irrational fraction of 2π,
    // guaranteeing no two points share an azimuth fraction for any N.
    static const float kGoldenAngle = 2.39996323f;

    // Azimuth: golden angle spacing prevents clumping at any count.
    float phi = float(i) * kGoldenAngle;

    // Polar angle: linearly spaced from 0 (first child along axis) to halfAngle.
    // Division by max(N-1, 1) avoids divide-by-zero when N=1.
    float t     = float(i) / float(max(N, 2u) - 1u); // [0, 1]
    float theta = halfAngle * t;                       // [0, halfAngle]

    // Convert spherical to Cartesian in local frame (+Z = cone axis).
    float sinT = sin(theta);
    float cosT = cos(theta);
    float3 localDir = float3(sinT * cos(phi), sinT * sin(phi), cosT);

    // Rotate from local +Z into world parentDir via the TBN frame.
    float3 tangent, bitangent;
    BuildTBN(parentDir, tangent, bitangent);

    return tangent * localDir.x + bitangent * localDir.y + parentDir * localDir.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Spawns child particles for one death event per thread.
///
/// Each thread reads one DeathEvent, then loops `deathSpawnCount` times to emit
/// child particles. Free-list slots are claimed via InterlockedAdd(-1) on
/// freeListHead — the same protocol used by the primary spawn CS, ensuring
/// no slot aliasing even when both spawners run in the same frame.
///
/// # Parameters
/// - dtid: SV_DispatchThreadID.x identifies which death event [0..deathEventCount-1].
///
/// # Side Effects
/// - Decrements freeListHead by `deathSpawnCount` (clamps at 0 gracefully).
/// - Increments aliveCount by the number of successfully claimed slots.
/// - Writes child state to all SoA UAVs at the claimed indices.
[numthreads(DEATH_SPAWN_GROUP_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint evtIdx = dtid.x;

    // ── Read actual death event count written by the update CS ─────────
    // The update CS atomically increments this counter. We read it via SRV
    // (a UAV→UAV barrier was issued between the update and this dispatch,
    // ensuring the write is complete). This avoids a CPU readback and its
    // associated 1-frame latency.
    StructuredBuffer<uint> deathCountBuf = ResourceDescriptorHeap[deathEventCountSrvIndex];
    const uint actualEventCount = min(deathCountBuf[0], maxDeathEvents);

    // ── Early exit for threads beyond the actual event count ───────────
    if (evtIdx >= actualEventCount) {
        return;
    }

    // ── Bind read resources ────────────────────────────────────────────
    StructuredBuffer<DeathEvent>   deathEvents  = ResourceDescriptorHeap[deathEventBufferSrvIndex];
    RWStructuredBuffer<float4>     positions    = ResourceDescriptorHeap[positionsUavIndex];
    RWStructuredBuffer<float4>     velocities   = ResourceDescriptorHeap[velocitiesUavIndex];
    RWStructuredBuffer<float>      ages         = ResourceDescriptorHeap[agesUavIndex];
    RWStructuredBuffer<float>      lifespans    = ResourceDescriptorHeap[lifespansUavIndex];
    RWStructuredBuffer<float>      sizes        = ResourceDescriptorHeap[sizesUavIndex];
    RWStructuredBuffer<float4>     colors       = ResourceDescriptorHeap[colorsUavIndex];
    RWStructuredBuffer<float>      rotations    = ResourceDescriptorHeap[rotationsUavIndex];
    RWStructuredBuffer<float4>     dynParams    = ResourceDescriptorHeap[dynamicParamsUavIndex];
    RWStructuredBuffer<uint>       freeList     = ResourceDescriptorHeap[freeListUavIndex];
    RWStructuredBuffer<uint>       freeListHead = ResourceDescriptorHeap[freeListHeadUavIndex];
    RWStructuredBuffer<uint>       aliveCount   = ResourceDescriptorHeap[aliveCountUavIndex];

    // ── Read the death event ───────────────────────────────────────────
    DeathEvent evt = deathEvents[evtIdx];

    // Compute parent speed for scaling child velocities.
    // Guard against zero-length velocity (e.g., particle that stopped before dying).
    float parentSpeed = length(evt.velocity);

    // Parent direction: if speed is near-zero, spawn children upward (world Z-up).
    float3 parentDir = (parentSpeed > 0.001f)
                     ? (evt.velocity / parentSpeed)
                     : float3(0.0f, 0.0f, 1.0f);

    // Child speed: scaled from parent's death speed.
    // The scale factor maps 0 → motionless children, 1 → same speed, >1 → faster.
    float childSpeed = parentSpeed * deathSpawnSpeedScale;

    // ── Spawn each child particle ──────────────────────────────────────
    for (uint i = 0u; i < deathSpawnCount; ++i)
    {
        // ── Unique RNG seed per child ──────────────────────────────────
        // Mix: event index + child index + frame (entropy sources for decorrelation).
        // Using VFXSeed from vfx_common.hlsli ensures the same hash quality as
        // the primary spawn CS, while mixing evtIdx avoids correlated siblings.
        uint seed = VFXSeed(evtIdx * 32u + i, frameNumber);

        // ── Compute child velocity direction (Fibonacci cone) ──────────
        // The Fibonacci spiral places children evenly in the spread cone.
        // An additional small random perturbation via seed breaks the perfect
        // regularity at the cost of one RandomFloat call, preventing visible
        // banding when deathSpawnCount is small (< 8).
        float3 childDir = FibonacciConeDirection(parentDir, i, deathSpawnCount,
                                                  deathSpawnSpread, seed);

        // ── Small random speed perturbation (+/- 20% of child speed) ──
        // Prevents all children from having identical speeds, which looks unnatural.
        float speedJitter = childSpeed * RandomFloat(seed, -0.2f, 0.2f);
        float3 childVel   = childDir * (childSpeed + speedJitter);

        // ── Random initial rotation ─────────────────────────────────────
        float childRotation = RandomFloat(seed, 0.0f, VFX_TWO_PI);

        // ── Claim a free slot from the stack ───────────────────────────
        uint prevHead;
        InterlockedAdd(freeListHead[0], uint(-1), prevHead);

        // Pool exhausted: undo decrement and stop spawning children.
        // This bounds children per death to the available free slots,
        // preventing the free-list head from going negative (wraps to UINT_MAX).
        if (prevHead == 0u || prevHead > maxParticles) {
            InterlockedAdd(freeListHead[0], 1u);
            break;
        }

        // Read the particle buffer index from the free list stack.
        uint particleIdx = freeList[prevHead - 1u];

        // Bounds check: corrupted free list would otherwise write out of range.
        if (particleIdx >= maxParticles) {
            InterlockedAdd(freeListHead[0], 1u);
            continue;
        }

        // ── Write child particle state ─────────────────────────────────
        // Position: exact death position (no radius offset — children burst outward
        // via velocity, not by scattering the spawn origin).
        positions[particleIdx]  = float4(evt.position, 0.0f);
        velocities[particleIdx] = float4(childVel,     0.0f);
        ages[particleIdx]       = 0.0f;
        lifespans[particleIdx]  = childLifetime;
        sizes[particleIdx]      = childSize;
        colors[particleIdx]     = evt.color;   // Inherit parent's death color.
        rotations[particleIdx]  = childRotation;
        dynParams[particleIdx]  = float4(0.0f, 0.0f, 0.0f, 0.0f); // Child starts with zeroed dynamic params.

        // ── Increment alive count ──────────────────────────────────────
        uint dummy;
        InterlockedAdd(aliveCount[0], 1u, dummy);
    }
}
