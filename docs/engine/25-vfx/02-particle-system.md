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

## GPU Particle Buffer Layout (Structure-of-Arrays)

Particles are stored as parallel arrays (one per attribute) for cache efficiency:

```cpp
namespace uz::VFX {
    /// GPU particle storage: Structure-of-Arrays layout.
    /// All particles' positions are contiguous, then velocities, then ages, etc.
    /// This is more cache-efficient than Array-of-Structs.
    struct GPUParticleBuffer {
        uz::GPUBufferHandle positions;      // float3 × capacity
        uz::GPUBufferHandle velocities;     // float3 × capacity
        uz::GPUBufferHandle ages;           // float × capacity
        uz::GPUBufferHandle lifespans;      // float × capacity
        uz::GPUBufferHandle sizes;          // float × capacity
        uz::GPUBufferHandle colors;         // float4 (RGBA) × capacity
        uz::GPUBufferHandle rotations;      // float × capacity (2D angle)

        // Indirect draw args: [vertex_count_per_instance, instance_count, ...]
        uz::GPUBufferHandle indirect_args;  // uint32_t[5]

        uint32_t capacity = 0;              // Max particles
        uint32_t num_alive = 0;             // Currently alive (CPU-side tracking)
    };
}
```

### Memory Layout Example

For 1000 particles:

```
┌─────────────────────────────────────────┐
│ Position Buffer (1000 × 12 bytes)       │ 12 KB
│ [x0,y0,z0, x1,y1,z1, ..., x999,y999,z999]
├─────────────────────────────────────────┤
│ Velocity Buffer (1000 × 12 bytes)       │ 12 KB
│ [vx0,vy0,vz0, vx1,vy1,vz1, ...]
├─────────────────────────────────────────┤
│ Age Buffer (1000 × 4 bytes)             │ 4 KB
│ [age0, age1, ..., age999]
├─────────────────────────────────────────┤
│ Lifespan Buffer (1000 × 4 bytes)        │ 4 KB
│ [life0, life1, ..., life999]
├─────────────────────────────────────────┤
│ Size Buffer (1000 × 4 bytes)            │ 4 KB
│ [size0, size1, ..., size999]
├─────────────────────────────────────────┤
│ Color Buffer (1000 × 16 bytes)          │ 16 KB
│ [r0,g0,b0,a0, r1,g1,b1,a1, ...]
└─────────────────────────────────────────┘
Total: ~52 KB for 1000 particles
Scale: 1M particles = ~52 MB (typical GPU has 4–8 GB)
```

## ParticleSystem Component

```cpp
namespace uz::VFX {
    /// GPU-driven particle emitter.
    class ParticleSystem : public uz::PrimitiveComponent {
    public:
        UZ_COMPONENT(ParticleSystem);

        /// VFX graph asset (spawn/update/render module definitions).
        uz::ResourceHandle<VFXGraph> graph;

        /// Maximum particles allocated for this emitter.
        /// Allocated on-demand from global particle pool.
        uint32_t max_particles = 10000;

        /// Particle spawn rate (particles per second).
        /// Can be modulated via curve.
        float emission_rate = 100.0f;

        /// Initial velocity (magnitude, direction from emitter rotation).
        Vec3 spawn_velocity = {0, 0, 10};

        /// Particle lifespan in seconds.
        float lifetime = 5.0f;

        /// Relative spawn position offset from emitter.
        Vec3 spawn_offset = {0, 0, 0};

        /// Is this emitter currently emitting?
        bool is_emitting = true;

        // Methods
        void Start();           // Begin emitting
        void Stop();            // Stop emitting
        void Pause(bool paused);  // Pause/resume

        /// Set emitter transform (where particles spawn).
        void SetEmitterTransform(const Transform& tr);

        /// Set VFX parameter (e.g., "intensity", "color", "scale").
        void SetParameter(StringView name, float value);
        void SetParameter(StringView name, const Vec3& value);
        void SetParameter(StringView name, const Vec4& value);

        /// Get current particle count.
        uint32_t GetAliveParticleCount() const { return buffer_.num_alive; }

    private:
        GPUParticleBuffer buffer_;
        uint32_t particles_to_spawn_this_frame_ = 0;

        void OnTick(float dt) override;
        void UpdateGPUBuffers();
        void DispatchComputeShader(float dt);
    };
}
```

## Compute Shader

```hlsl
// particle_update.hlsl
#pragma pack_matrix(row_major)

cbuffer EmitterData : register(b0) {
    float dt;
    float emission_rate;
    float lifetime;
    int num_particles;
    float3 gravity;
    float damping;
    float3 emitter_pos;
    float emitter_scale;
};

// Input: particle data
StructuredBuffer<float3> in_positions : register(t0);
StructuredBuffer<float3> in_velocities : register(t1);
StructuredBuffer<float> in_ages : register(t2);
StructuredBuffer<float> in_lifespans : register(t3);

// Output: updated particle data
RWStructuredBuffer<float3> out_positions : register(u0);
RWStructuredBuffer<float3> out_velocities : register(u1);
RWStructuredBuffer<float> out_ages : register(u2);
RWStructuredBuffer<float> out_sizes : register(u3);
RWStructuredBuffer<float4> out_colors : register(u4);

// Indirect args buffer
RWStructuredBuffer<uint> indirect_args : register(u5);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint idx = id.x;
    if (idx >= num_particles) return;

    float3 pos = in_positions[idx];
    float3 vel = in_velocities[idx];
    float age = in_ages[idx];
    float lifespan = in_lifespans[idx];

    // Normalized age [0, 1]
    float age_norm = age / lifespan;

    // Update velocity: apply gravity and damping
    vel += gravity * dt;
    vel *= damping;

    // Update position
    pos += vel * dt;
    age += dt;

    // Sample curves: size and color over lifetime
    float size = SampleSizeCurve(age_norm);
    float4 color = SampleColorCurve(age_norm);

    // Kill particle if expired
    if (age >= lifespan) {
        // Move out-of-bounds (trick: render will skip)
        pos.y = -10000.0f;
        age = lifespan;
    }

    // Write back
    out_positions[idx] = pos;
    out_velocities[idx] = vel;
    out_ages[idx] = age;
    out_sizes[idx] = size;
    out_colors[idx] = color;

    // Update indirect args: instance count = num_alive
    // (simplified; actual uses atomic operations)
    if (age < lifespan) {
        InterlockedAdd(indirect_args[1], 1);
    }
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
// particle_render.hlsl
StructuredBuffer<float3> positions : register(t0);
StructuredBuffer<float> sizes : register(t1);
StructuredBuffer<float4> colors : register(t2);
Texture2D sprite_atlas : register(t3);
SamplerState sprite_sampler : register(s0);

struct VS_Input {
    uint vertex_id : SV_VertexID;
};

struct VS_Output {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VS_Output main(VS_Input input) {
    uint particle_id = input.vertex_id / 4;  // 4 vertices per quad
    uint vertex_in_quad = input.vertex_id % 4;

    float3 particle_pos = positions[particle_id];
    float size = sizes[particle_id];
    float4 color = colors[particle_id];

    // Billboard: expand quad perpendicular to camera
    float3 to_camera = normalize(camera_pos - particle_pos);
    float3 right = normalize(cross(to_camera, float3(0, 1, 0))) * size;
    float3 up = float3(0, 1, 0) * size;

    // Four corners of billboard
    float3 corners[4] = {
        particle_pos - right - up,  // BL
        particle_pos + right - up,  // BR
        particle_pos - right + up,  // TL
        particle_pos + right + up,  // TR
    };

    float2 uv_corners[4] = {
        float2(0, 1),
        float2(1, 1),
        float2(0, 0),
        float2(1, 0),
    };

    VS_Output output;
    output.position = mul(float4(corners[vertex_in_quad], 1), view_projection);
    output.uv = uv_corners[vertex_in_quad];
    output.color = color;
    return output;
}
```

## Indirect Draw

Avoids CPU-GPU sync by letting GPU determine instance count:

```cpp
// CPU side
ID3D12CommandList* cmdlist = ...;
ID3D12Resource* particle_buffer = ...;
ID3D12Resource* indirect_args = ...;

// Dispatch compute to update particles
cmdlist->Dispatch(num_particles / 256, 1, 1);

// Draw with indirect args (GPU reads instance count)
cmdlist->ExecuteIndirect(
    command_signature,  // Contains DrawInstanced command
    1,                  // Num commands
    indirect_args,
    0,                  // Offset in indirect buffer
    nullptr             // No count buffer
);
```

## Global Particle Budget

Multiple emitters compete for fixed memory:

```cpp
namespace uz::VFX {
    /// Global budget for all active particles.
    class ParticleBudget {
    public:
        uint32_t max_total_particles = 500000;
        uint32_t particles_used = 0;

        /// Request allocation for emitter.
        /// If budget exceeded, cull oldest particles from lower-priority emitters.
        bool AllocateParticles(uint32_t requested, uint32_t& allocated);

        void ReturnParticles(uint32_t count);
    };
}

// Allocation algorithm:
bool ParticleBudget::AllocateParticles(uint32_t requested, uint32_t& allocated) {
    if (particles_used + requested <= max_total_particles) {
        // Room available
        allocated = requested;
        particles_used += requested;
        return true;
    }

    // Over budget: cull lower-priority emitters
    CullLowestPriorityEmitters(requested);

    allocated = std::min(requested, max_total_particles - particles_used);
    particles_used += allocated;
    return allocated > 0;
}
```

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

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | GPU compute loop, SoA buffers, spawn/update modules, billboard rendering, indirect draw |
| **Phase 4** | Advanced modules (collision, light), LOD system, decal placement, performance optimization |
