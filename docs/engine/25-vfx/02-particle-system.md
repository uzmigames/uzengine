# Particle System — GPU Simulation, SoA Layout, 1M+ Particles, Indirect Draw

> **Depends on**: VFX system (§25), RHI (§03), Rendering (§02)
> **Simulation**: GPU compute shaders (SM6.6), fixed-rate ticking
> **References**: Unreal 5 Niagara (GPU simulation), Godot GPUParticles3D

## Purpose

A GPU-driven particle system simulates 1M+ particles with minimal CPU cost. Particle data lives on GPU in Structure-of-Arrays layout (cache-efficient). Each frame:

1. Compute shader spawns new particles and updates existing ones
2. Indirect buffer specifies draw count (avoids CPU-GPU sync)
3. Billboard/mesh rendering draws all active particles in one draw call

## Architecture Overview

```
┌──────────────────────────┐
│ Particle Emitter         │
│  - Configuration         │
│  - Spawn rate            │
│  - Lifetime              │
└─────────┬────────────────┘
          │
          ▼
    ┌──────────────────┐
    │ GPU Buffers (SoA)│
    │  - Position[N]   │
    │  - Velocity[N]   │
    │  - Age[N]        │
    │  - Lifespan[N]   │
    │  - Color[N]      │
    │  - Size[N]       │
    └─────────┬────────┘
              │
              ▼
        ┌────────────────┐
        │ Compute Shader │
        │  - Spawn       │
        │  - Update      │
        │  - Age         │
        │  - Kill        │
        └─────────┬──────┘
                  │
                  ▼
        ┌────────────────┐
        │ Indirect Buffer│
        │  - Draw count  │
        │  - Instances   │
        └─────────┬──────┘
                  │
                  ▼
        ┌────────────────┐
        │ Render Pass    │
        │  - DrawIndirect│
        │  - Billboard   │
        │  - Depth sort  │
        └────────────────┘
```

## GPU Particle Buffer Layout (Structure-of-Arrays Ping-Pong)

Particles are stored as parallel arrays (one per attribute) for cache efficiency. The buffer uses **ping-pong double-buffering**: two identical attribute sets alternate between read (SRV) and write (UAV) each frame.

```cpp
namespace uz {
    /// Per-set SoA buffers for one side of the ping-pong pair (56 bytes per particle).
    struct ParticleAttributeSet {
        RHIBufferHandle positions;      // float3 × capacity (12 bytes per particle)
        RHIBufferHandle velocities;     // float3 × capacity (12 bytes per particle)
        RHIBufferHandle ages;           // float  × capacity (4 bytes per particle)
        RHIBufferHandle lifespans;      // float  × capacity (4 bytes per particle)
        RHIBufferHandle sizes;          // float  × capacity (4 bytes per particle)
        RHIBufferHandle colors;         // float4 × capacity (16 bytes per particle, RGBA)
        RHIBufferHandle rotations;      // float  × capacity (4 bytes per particle, 2D angle)
    };

    /// GPU particle storage: two ping-pong attribute sets + shared bookkeeping.
    struct GPUParticleBuffer {
        ParticleAttributeSet sets[2];   // Read set vs write set, swap via XOR
        uint32_t readIndex;             // 0 or 1 (SRV side)
        uint32_t writeIndex;            // 0 or 1 (UAV side)
        uint32_t capacity;              // Max particles ever

        // Shared bookkeeping buffers (updated by GPU atomics, NOT double-buffered)
        RHIBufferHandle aliveCount;     // uint32 atomic: num alive particles
        RHIBufferHandle freeList;       // uint32[capacity]: stack of free indices
        RHIBufferHandle freeListHead;   // uint32 atomic: top-of-stack for free list
        RHIBufferHandle idToIndex;      // uint32[capacity]: ParticleID -> buffer index map
        RHIBufferHandle indirectDrawArgs;     // RHIDrawIndexedIndirectArgs (20 bytes)
        RHIBufferHandle indirectDispatchArgs; // RHIDispatchIndirectArgs (12 bytes)

        void Swap() noexcept { readIndex ^= 1; writeIndex ^= 1; }
    };
}
```

### Memory Layout Example

For 1000 particles with 2 attribute sets:

```
┌──────────────────────────────────────────────────────────────────┐
│ Attribute Set 0 (Read)                                           │
├──────────────────────────────────────────────────────────────────┤
│ Position Buffer (1000 × 12 bytes)                    │ 12 KB
│ Velocity Buffer (1000 × 12 bytes)                    │ 12 KB
│ Age Buffer (1000 × 4 bytes)                          │ 4 KB
│ Lifespan Buffer (1000 × 4 bytes)                     │ 4 KB
│ Size Buffer (1000 × 4 bytes)                         │ 4 KB
│ Color Buffer (1000 × 16 bytes)                       │ 16 KB
│ Rotation Buffer (1000 × 4 bytes)                     │ 4 KB
│ Subtotal: 56 KB                                      │
├──────────────────────────────────────────────────────────────────┤
│ Attribute Set 1 (Write)                                          │
│ (Same layout as above)                               │ 56 KB
├──────────────────────────────────────────────────────────────────┤
│ Shared Bookkeeping                                               │
│ aliveCount (1 × 4 bytes)                             │ 4 bytes
│ freeList (1000 × 4 bytes)                            │ 4 KB
│ freeListHead (1 × 4 bytes)                           │ 4 bytes
│ idToIndex (1000 × 4 bytes)                           │ 4 KB
│ indirectDrawArgs (20 bytes)                          │ 20 bytes
│ indirectDispatchArgs (12 bytes)                      │ 12 bytes
│ Subtotal: ~8.1 KB                                    │
└──────────────────────────────────────────────────────────────────┘
Total: ~120.1 KB for 1000 particles
Scale: 1M particles ≈ 120 MB (typical GPU has 4–8 GB)
Per-particle cost: ~120 bytes (attribute data + amortized bookkeeping)
```

### Ping-Pong XOR Swap Pattern

The buffer uses efficient index toggling via XOR:

```cpp
// Before frame N
readIndex = 0, writeIndex = 1

// Compute shader reads from sets[0], writes to sets[1]
// ...after simulation...
buffer.Swap();  // XOR both indices

// Frame N+1
readIndex = 1, writeIndex = 0  // Roles swapped with no memory copy
```

### Free List Atomic Protocol

Particle slots are recycled via a lock-free free list (stack):

```hlsl
// In particle_spawn_cs.hlsl:
// Free list is a stack: free_list[0..freeListHead-1] contains dead indices

uint oldHead;
InterlockedAdd(freeListHead, -1, oldHead);  // Atomically pop from stack
if (oldHead == 0) {
    // Stack empty, no more free slots
    return;
}
uint freedIndex = freeList[oldHead - 1];    // Read the freed slot
// Initialize particle at freedIndex
```

On death, particles are recycled:

```hlsl
// In particle_update_cs.hlsl:
if (age >= lifespan) {
    uint oldHead;
    InterlockedAdd(freeListHead, 1, oldHead);  // Atomically push to stack
    freeList[oldHead] = particleIndex;         // Return this index to the pool
}
```

## ParticleEmitter Component

The `ParticleEmitter` struct is a pure ECS component holding emitter configuration and GPU buffers:

```cpp
namespace uz {
    /// GPU-driven particle emitter ECS component.
    struct ParticleEmitter {
        // -- Configuration (read by compute shader) ----

        /// Maximum particles this emitter can have alive simultaneously.
        /// The actual capacity may be lower if budget is constrained.
        uint32_t maxParticles = 10000;

        /// Particle spawn rate (particles per second).
        /// Set to 0 to stop emission without disabling the emitter.
        float emissionRate = 100.0f;

        /// Base particle lifespan in seconds.
        float lifetime = 5.0f;

        /// Random lifespan variation (subtracted from base lifetime).
        /// Actual: lifetime - random(0, lifetimeVariation)
        float lifetimeVariation = 0.0f;

        /// Initial spawn velocity direction and magnitude (world space).
        Vec3 spawnVelocity = {0.0f, 10.0f, 0.0f};

        /// Random velocity spread magnitude.
        /// Each component gets random(-velocityVariation, +velocityVariation).
        float velocityVariation = 1.0f;

        /// Spread cone half-angle in degrees (0 = straight, 90 = hemisphere).
        float spreadAngle = 30.0f;

        /// Gravity acceleration applied each frame (world space, m/s^2).
        Vec3 gravity = {0.0f, -9.81f, 0.0f};

        /// Velocity damping per frame (0 = full stop, 1 = no damping).
        /// Applied multiplicatively: velocity *= damping each tick.
        float damping = 0.98f;

        /// Whether the emitter is currently spawning new particles.
        bool isEmitting = true;

        /// Priority for budget allocation (0 = lowest, 100 = highest).
        uint8_t priority = 50;

        // -- Runtime State (managed by VFXSystem) ----

        /// GPU particle storage (ping-pong SoA buffers + shared bookkeeping).
        GPUParticleBuffer buffer;

        /// Current alive particle count (CPU-side mirror of GPU aliveCount).
        /// Updated once per frame via GPU readback.
        uint32_t cpuAliveCount = 0;

        /// Fractional spawn accumulator for smooth sub-frame emission.
        /// Carries leftover fractional particles to the next frame.
        float spawnAccumulator = 0.0f;

        /// Whether GPU buffers have been allocated.
        bool initialized = false;

        /// Particles actually allocated from budget (may be < maxParticles).
        uint32_t allocatedFromBudget = 0;

        // -- Methods --

        /// Allocates GPU buffers from the particle budget.
        /// May grant fewer particles than maxParticles if budget is tight.
        bool Initialize(IRHIDevice* device, ParticleBudget& budget);

        /// Releases GPU buffers and returns allocation to budget.
        void Shutdown(IRHIDevice* device, ParticleBudget& budget);

        /// Calculates spawn count for this frame using emissionRate and dt.
        /// Carries fractional particles via spawnAccumulator.
        uint32_t CalculateSpawnCount(float dt) noexcept;
    };
}
```

## Compute Shader

```hlsl
// particle_spawn.hlsl
#include "vfx_common.hlsli"

cbuffer VFXSpawnConstants : register(b0) {
    float3   emitterPos;         // World-space emitter origin
    float    spawnRadius;        // Random offset sphere radius

    float3   spawnVelocity;      // Base emission velocity
    float    velocityVariation;  // +/- speed randomization

    float    spreadAngle;        // Half-cone angle in radians
    float    minLifetime;        // Minimum particle lifespan
    float    maxLifetime;        // Maximum particle lifespan
    float    initialSize;        // Initial billboard size

    float4   initialColor;       // RGBA initial color (linear)

    float    dt;                 // Frame delta time
    uint     frameNumber;        // Monotonically increasing frame counter
    uint     spawnCount;         // Number of particles to spawn this frame
    uint     maxParticles;       // Total particle pool capacity
};

// Spawn shader: atomically pop free slot, initialize particle
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint threadIdx = id.x;
    if (threadIdx >= spawnCount) return;

    // Atomically pop free slot from stack
    uint oldHead;
    InterlockedAdd(freeListHead, -1, oldHead);
    if (oldHead == 0) return;  // Free list exhausted

    uint bufferIdx = freeList[oldHead - 1];

    // Initialize particle using RNG seeded with (bufferIdx, frameNumber)
    float3 pos = emitterPos + RandomInSphere(...);
    float3 vel = RandomInCone(spawnVelocity, spreadAngle, ...);

    // Write to WRITE set (ping-pong target)
    positions_write[bufferIdx] = pos;
    velocities_write[bufferIdx] = vel;
    ages_write[bufferIdx] = 0.0f;
    lifespans_write[bufferIdx] = RandomFloat(minLifetime, maxLifetime, ...);
    sizes_write[bufferIdx] = initialSize;
    colors_write[bufferIdx] = initialColor;
    rotations_write[bufferIdx] = 0.0f;

    // Increment alive counter atomically
    InterlockedAdd(aliveCount, 1);
}
```

### Update Shader (`particle_update_cs.hlsl`)

Advances particle physics each frame:

```hlsl
#include "vfx_common.hlsli"

cbuffer VFXUpdateConstants : register(b0) {
    float3   gravity;
    float    damping;
    float    dt;
    uint     numAlive;  // Current alive count to process
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint idx = id.x;
    if (idx >= numAlive) return;

    // Read from READ set (after XOR swap from previous frame)
    float3 pos = positions_read[idx];
    float3 vel = velocities_read[idx];
    float age = ages_read[idx];
    float lifespan = lifespans_read[idx];

    // Physics integration
    vel += gravity * dt;
    vel *= damping;
    pos += vel * dt;
    age += dt;

    // Write to WRITE set
    positions_write[idx] = pos;
    velocities_write[idx] = vel;
    ages_write[idx] = age;
    lifespans_write[idx] = lifespan;

    // Recycle dead particles
    if (age >= lifespan) {
        uint oldHead;
        InterlockedAdd(freeListHead, 1, oldHead);  // Atomically push to stack
        freeList[oldHead] = idx;                   // Return to pool
        InterlockedAdd(aliveCount, -1);            // Decrement alive count
    }
}
```

### Indirect Args Shader (`particle_indirect_args_cs.hlsl`)

Writes GPU-driven draw and dispatch arguments:

```hlsl
cbuffer IndirectArgsConstants : register(b0) {
    uint aliveCountIdx;
    uint indirectDrawArgsIdx;
    uint indirectDispatchArgsIdx;
    uint verticesPerParticle;  // 6 for billboards
};

[numthreads(1, 1, 1)]
void main() {
    uint alive = aliveCount[0];

    // Write DrawIndexedIndirectArgs: [IndexCount, InstanceCount, ...]
    indirectDrawArgs[0] = verticesPerParticle;
    indirectDrawArgs[1] = alive;  // Variable instance count
    indirectDrawArgs[2] = 0;
    indirectDrawArgs[3] = 0;
    indirectDrawArgs[4] = 0;

    // Write DispatchIndirectArgs: [ThreadGroupCountX, Y, Z]
    indirectDispatchArgs[0] = (alive + 255) / 256;  // Ceil divide for 256 threads
    indirectDispatchArgs[1] = 1;
    indirectDispatchArgs[2] = 1;
}
```

## Particle Lifecycle

```
┌──────────────┐
│ Spawned      │ age = 0, lifespan = 5.0s
├──────────────┤
│ Updating     │ age += dt every frame
│ (0 to 5.0s)  │
│ - Velocity   │ Curves evaluate at age/lifespan
│ - Position   │ Size and color fade over time
│ - Gravity    │
├──────────────┤
│ Expired      │ age > lifespan
│ (killed)     │ Moved out-of-bounds, not rendered
└──────────────┘
```

## Spawn Module

```cpp
namespace uz::VFX {
    /// Spawn configuration.
    struct SpawnModule {
        float rate;              // Particles per second
        float lifespan_min;
        float lifespan_max;
        Vec3 velocity;           // Base velocity
        float velocity_variation;  // Random spread
        float spread_angle;      // Spread cone angle
    };
}

// In compute shader:
float particles_to_spawn = emission_rate * dt;
// Fractional part carried over to next frame for smooth rate

for (int i = 0; i < particles_to_spawn; i++) {
    int spawn_idx = num_alive;
    if (spawn_idx >= capacity) break;

    // Random initial velocity
    float3 vel = normalize(velocity + RandomVec3(spread_angle)) * (velocity_magnitude + RandomFloat(-variation, +variation));
    Vec3 lifespan = RandomFloat(lifespan_min, lifespan_max);

    positions[spawn_idx] = emitter_pos + RandomInSphere(spawn_radius);
    velocities[spawn_idx] = vel;
    ages[spawn_idx] = 0;
    lifespans[spawn_idx] = lifespan;
}
```

## Update Module

Applied each frame to all alive particles:

```cpp
// Physics
velocity += gravity * dt;
velocity *= damping;
position += velocity * dt;

// Collision (raycast check)
if (collision_enabled && Raycast(position, down, hit)) {
    if (collision_response == Kill) {
        age = lifespan;  // Kill
    } else if (collision_response == Bounce) {
        velocity = reflect(velocity, hit.normal) * bounce_factor;
    }
}

// Age
age += dt;
```

## Rendering: Billboard

Particles rendered as camera-facing quads:

```hlsl
// particle_billboard_vs.hlsl
#include "common.hlsli"

StructuredBuffer<float3> positions : register(t0);  // Particle positions
StructuredBuffer<float> sizes : register(t1);       // Billboard sizes
StructuredBuffer<float4> colors : register(t2);     // RGBA colors
StructuredBuffer<float> rotations : register(t3);   // 2D rotation angles

cbuffer ViewConstants : register(b0) {
    float4x4 viewProjection;
    float3 cameraPos;
    float3 cameraRight;
    float3 cameraUp;
    float pad;
};

struct VS_Output {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float depth : TEXCOORD1;  // For soft particle depth fade
};

VS_Output main(uint vertexId : SV_VertexID) {
    // 6 vertices per particle (2 triangles)
    uint particleIdx = vertexId / 6;
    uint vertexInQuad = vertexId % 6;

    float3 pos = positions[particleIdx];
    float size = sizes[particleIdx];
    float4 color = colors[particleIdx];
    float rotation = rotations[particleIdx];

    // Billboard offsets and UVs (triangle strip: 2 triangles per quad)
    float2 offsets[6] = {
        float2(-1, -1), float2(1, -1), float2(1, 1),
        float2(-1, -1), float2(1, 1),  float2(-1, 1)
    };
    float2 uvs[6] = {
        float2(0, 1), float2(1, 1), float2(1, 0),
        float2(0, 1), float2(1, 0), float2(0, 0)
    };

    // Apply 2D rotation
    float2 offset = offsets[vertexInQuad];
    float c = cos(rotation);
    float s = sin(rotation);
    float2 rotated = float2(
        offset.x * c - offset.y * s,
        offset.x * s + offset.y * c
    );

    // Expand billboard
    float3 worldPos = pos + (cameraRight * rotated.x + cameraUp * rotated.y) * size;

    VS_Output output;
    output.position = mul(float4(worldPos, 1.0), viewProjection);
    output.uv = uvs[vertexInQuad];
    output.color = color;
    output.depth = output.position.z;
    return output;
}
```

### Particle Pixel Shader (`particle_billboard_ps.hlsl`)

```hlsl
#include "common.hlsli"

Texture2D spriteAtlas : register(t0);
Texture2D depthBuffer : register(t1);  // Scene depth for soft particles
SamplerState spriteSampler : register(s0);

cbuffer PixelConstants : register(b0) {
    float softParticleDistance;  // World units for fade
    float3 pad;
};

struct PS_Input {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float depth : TEXCOORD1;
};

float4 main(PS_Input input) : SV_TARGET {
    // Sample sprite texture
    float4 texColor = spriteAtlas.Sample(spriteSampler, input.uv);

    // Alpha erosion: discard near-transparent pixels
    if (texColor.a < 0.01) discard;

    // Blend with vertex color
    float4 finalColor = texColor * input.color;

    // Soft particle depth fade (fade out near geometry)
    float sceneDepth = depthBuffer.Load(uint3(input.position.xy, 0)).x;
    float depthDiff = sceneDepth - input.depth;
    float softFade = saturate(depthDiff / softParticleDistance);
    finalColor.a *= softFade;

    return finalColor;
}
```

## Render Graph Pipeline

Particle simulation and rendering are integrated into the render graph as compute and graphics passes.

### Render Graph Sequence (Per Frame)

```
1. ParticleSimulationPass (Compute)
   ├── Spawn compute: emit new particles (free list atomic pop)
   ├── UAV barrier
   ├── Update compute: physics, aging, recycling (free list atomic push)
   ├── UAV barrier
   └── Indirect Args compute: write GPU-driven draw/dispatch args

2. ParticleRenderPass (Graphics - Transparent Phase)
   ├── Set PSO: Billboard alpha-blend or additive
   ├── Bind particle buffers as SRV
   ├── ExecuteIndirect with indirectDrawArgs (GPU-driven)
   │   └── Draws ceil(alive / 256) * 256 vertices (6 per particle)
   └── Back-to-front sorting via depth write
```

### No CPU Readback

The indirect args buffer eliminates CPU-GPU synchronization. The `particle_indirect_args_cs.hlsl` compute shader writes the draw count from `aliveCount[0]` directly to the indirect buffer. `ExecuteIndirect` reads this without blocking the CPU.

```cpp
// Render pass uses ExecuteIndirect (GPU-driven, no stall)
renderGraph.AddPass<ParticleRenderPass>([](RenderContext& ctx) {
    // Bind particle buffers and PSO
    ctx.ExecuteIndirect(
        commandSignature,       // DrawIndexedIndirect
        1,                      // Num commands
        indirectDrawArgs,       // GPU writes alive count here
        0,
        nullptr                 // No count buffer
    );
});
```

## Global Particle Budget

Multiple emitters compete for fixed GPU memory via a lock-free global budget allocator.

### ParticleBudget Class

```cpp
namespace uz {
    /// Global particle budget enforcer (thread-safe via atomics).
    class ParticleBudget {
    public:
        /// Constructs budget with max particle count.
        /// Default: 500,000 (~60 MB at 120 bytes/particle).
        explicit ParticleBudget(uint32_t maxParticles = 500000) noexcept;

        /// Requests allocation. Uses lock-free CAS loop.
        /// Returns true if any particles allocated (allocated may be < requested).
        bool Allocate(uint32_t requested, uint32_t& allocated) noexcept;

        /// Returns particles to budget (when emitter shuts down).
        void Return(uint32_t count) noexcept;

        /// Current usage and limits.
        uint32_t GetUsed() const noexcept;
        uint32_t GetAvailable() const noexcept;
        uint32_t GetMax() const noexcept;
        float GetUsageRatio() const noexcept;

        /// Adjust budget for quality settings.
        void SetMax(uint32_t newMax) noexcept;

    private:
        uint32_t max_;
        std::atomic<uint32_t> used_{0};  // Lock-free CAS allocation
    };
}
```

### Allocation Semantics

The budget tracks **allocated** (reserved) particles, not alive particles. An emitter with `maxParticles=10000` consumes 10000 from the budget even if only 500 are currently alive. This guarantees worst-case memory bounds.

```cpp
// VFXSystem initialization
ParticleBudget budget(500000);  // 500K particles global limit

// Emitter initialization
ParticleEmitter emitter;
emitter.maxParticles = 10000;
bool success = emitter.Initialize(device, budget);
// budget.used_ now += 10000 (or less if budget was tight)

// Emitter shutdown
emitter.Shutdown(device, budget);
// budget.used_ -= allocatedFromBudget
```

### Lock-Free CAS Loop

The `Allocate()` method uses atomic compare-and-swap to avoid mutex contention:

```cpp
bool ParticleBudget::Allocate(uint32_t requested, uint32_t& allocated) noexcept {
    uint32_t expected = used_.load(std::memory_order_relaxed);
    uint32_t desired;

    do {
        if (expected >= max_) {
            allocated = 0;
            return false;  // Budget exhausted
        }
        desired = std::min(expected + requested, max_);
        allocated = desired - expected;
    } while (!used_.compare_exchange_weak(expected, desired,
                                          std::memory_order_release,
                                          std::memory_order_relaxed));

    return allocated > 0;
}
```

This allows multiple emitters to request allocations concurrently on separate threads without locks.

## Performance

Per-frame overhead:

```
Compute dispatch: ~0.5 ms (100K particles)
Draw call: ~1.0 ms (billboard rendering + transparency sort)
Total: ~1.5 ms at 1M particles across all emitters
```

## Comparison

| Feature | Unreal 5 Niagara | Godot GPUParticles3D | Unity VFX Graph | **UzEngine** |
|---------|-----------------|---------------------|-----------------|------------|
| Simulation | GPU compute | GPU compute | GPU compute | **GPU compute (SM6.6)** |
| Max particles | 100K+ | 100K+ | 100K+ | **1M+** |
| Data layout | SoA (GPU) | SoA | SoA | **SoA** |
| Module system | 50+ modules | Limited | 20+ modules | **12+ core, extensible** |
| Indirect draw | Yes | Yes | Yes | **Yes** |
| Billboard render | Yes | Yes | Yes | **Yes** |

## API Reference

### VFXSystem (Central Manager)

Located in `engine/vfx/vfx_system.h`.

```cpp
namespace uz {
    /// Central VFX system managing all active particle emitters.
    class VFXSystem {
    public:
        /// Initializes VFX system with global particle budget.
        void Initialize(IRHIDevice* device, uint32_t maxTotalParticles = 500000);

        /// Shuts down VFX system and releases resources.
        void Shutdown(IRHIDevice* device);

        /// Registers an emitter for per-frame simulation.
        void RegisterEmitter(ParticleEmitter* emitter);

        /// Unregisters an emitter from simulation.
        void UnregisterEmitter(ParticleEmitter* emitter);

        /// Per-frame tick: calculates spawn counts for all emitters.
        void Tick(float dt);

        /// Returns list of active emitters for render passes to query.
        const std::vector<ParticleEmitter*>& GetActiveEmitters() const;

        /// Access to global particle budget.
        ParticleBudget& GetBudget() { return budget_; }
    };
}
```

### ParticleEmitter (ECS Component)

Located in `engine/vfx/components/particle_emitter.h`.

```cpp
namespace uz {
    struct ParticleEmitter {
        // Configuration
        uint32_t maxParticles = 10000;
        float emissionRate = 100.0f;
        float lifetime = 5.0f;
        float lifetimeVariation = 0.0f;
        Vec3 spawnVelocity = {0, 10, 0};
        float velocityVariation = 1.0f;
        float spreadAngle = 30.0f;      // Degrees
        Vec3 gravity = {0, -9.81f, 0};
        float damping = 0.98f;
        bool isEmitting = true;
        uint8_t priority = 50;          // Budget priority [0-100]

        // Runtime state
        GPUParticleBuffer buffer;
        uint32_t cpuAliveCount = 0;
        float spawnAccumulator = 0.0f;
        bool initialized = false;
        uint32_t allocatedFromBudget = 0;

        // Methods
        bool Initialize(IRHIDevice* device, ParticleBudget& budget);
        void Shutdown(IRHIDevice* device, ParticleBudget& budget);
        uint32_t CalculateSpawnCount(float dt) noexcept;
    };
}
```

### GPUParticleBuffer (GPU Storage)

Located in `engine/vfx/core/gpu_particle_buffer.h`.

```cpp
namespace uz {
    struct ParticleAttributeSet {
        RHIBufferHandle positions;
        RHIBufferHandle velocities;
        RHIBufferHandle ages;
        RHIBufferHandle lifespans;
        RHIBufferHandle sizes;
        RHIBufferHandle colors;
        RHIBufferHandle rotations;

        bool IsValid() const noexcept;
    };

    struct GPUParticleBuffer {
        ParticleAttributeSet sets[2];   // Ping-pong
        uint32_t readIndex = 0;
        uint32_t writeIndex = 1;
        uint32_t capacity = 0;

        // Shared bookkeeping (not double-buffered)
        RHIBufferHandle aliveCount;
        RHIBufferHandle freeList;
        RHIBufferHandle freeListHead;
        RHIBufferHandle idToIndex;
        RHIBufferHandle indirectDrawArgs;
        RHIBufferHandle indirectDispatchArgs;

        const ParticleAttributeSet& ReadSet() const noexcept;
        ParticleAttributeSet& WriteSet() noexcept;
        void Swap() noexcept;  // XOR toggle
        bool IsValid() const noexcept;

        bool Create(IRHIDevice* device, uint32_t particleCapacity);
        void Destroy(IRHIDevice* device);
    };
}
```

### ParticleBudget (Global Allocator)

Located in `engine/vfx/core/particle_budget.h`.

```cpp
namespace uz {
    class ParticleBudget {
    public:
        explicit ParticleBudget(uint32_t maxParticles = 500000) noexcept;

        /// Allocate particles (thread-safe, lock-free).
        bool Allocate(uint32_t requested, uint32_t& allocated) noexcept;

        /// Return particles to budget.
        void Return(uint32_t count) noexcept;

        /// Query budget state.
        uint32_t GetUsed() const noexcept;
        uint32_t GetAvailable() const noexcept;
        uint32_t GetMax() const noexcept;
        float GetUsageRatio() const noexcept;

        /// Adjust budget.
        void SetMax(uint32_t newMax) noexcept;
    };
}
```

### Render Passes

Located in `engine/vfx/passes/`.

```cpp
namespace uz::VFX {
    /// Compute pass: spawn, update, and indirect args generation.
    class ParticleSimulationPass : public RenderGraphPass {
        // GPU-driven particle simulation
    };

    /// Graphics pass: billboard rendering in transparent phase.
    class ParticleRenderPass : public RenderGraphPass {
        // ExecuteIndirect billboard rendering
    };
}
```

### Source Files

| File | Purpose |
|------|---------|
| `engine/vfx/vfx_system.h/.cpp` | Central VFX manager |
| `engine/vfx/components/particle_emitter.h/.cpp` | Emitter ECS component |
| `engine/vfx/core/gpu_particle_buffer.h/.cpp` | GPU SoA ping-pong storage |
| `engine/vfx/core/particle_budget.h/.cpp` | Lock-free budget allocator |
| `engine/vfx/passes/particle_simulation_pass.h/.cpp` | Compute dispatch pass |
| `engine/vfx/passes/particle_render_pass.h/.cpp` | Billboard rendering pass |
| `shaders/runtime/vfx/particle_spawn_cs.hlsl` | Spawn shader (free list pop) |
| `shaders/runtime/vfx/particle_update_cs.hlsl` | Update shader (physics + recycle) |
| `shaders/runtime/vfx/particle_indirect_args_cs.hlsl` | Indirect args generation |
| `shaders/runtime/vfx/particle_billboard_vs.hlsl` | Billboard vertex expansion |
| `shaders/runtime/vfx/particle_billboard_ps.hlsl` | Billboard with soft fade |
| `shaders/runtime/include/vfx_common.hlsli` | RNG, helpers (RandomFloat, RandomInCone, VFXSeed) |

## Implementation Status

| Phase | Status | Details |
|-------|--------|---------|
| **3.60.0** | ✓ COMPLETE | **GPU Particle Core System**: ParticleEmitter, GPUParticleBuffer (ping-pong SoA + bookkeeping), ParticleBudget (lock-free), VFXSystem, simulation passes (spawn/update/indirect-args), render pass (indirect billboard), soft depth fade |
| **3.61.0** | ✓ COMPLETE | **Module system** (15 modules: spawn, update, decay, sprite_render, collision, light_render, mesh_render, turbulence, constraint, attractor, vector_field, sub_emitter, orbit, pin, distance_constraint) + ModuleRegistry + ModuleCompositor |
| **3.62.0** | ✓ COMPLETE | **VFX graph/asset system** (vfx_composite, vfx_graph, vfx_hot_reload), VFX significance budgeting, particle sort (bitonic merge, SM6.6 bindless) |
| **3.63.0** | ✓ COMPLETE | **Decal system** (GPU raycast placement, depth-aware fade, deferred rendering) |
| **3.64.0** | ✓ COMPLETE | **Trail/Ribbon system** (spline tessellation, width curves, UV scroll, indexed triangle rendering) |
| **3.65.0** | ✓ COMPLETE | **Advanced features**: collision depth-buffer raycast, light emission from particles, mesh instancing, sub-emitter spawning, orbit/pin/attractor forces, vector field sampling |
| **4.0+** | Planned | LOD system, texture atlasing, GPU profiling counters, multi-emitter batching optimization |
