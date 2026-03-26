# VFX System — GPU Particle Simulation + Decals + Trails

> **Status**: Phase 3 Particle Core (GPU Compute, Indirect Draw) Complete; Phase 4 (Decals, Trails) Planned
> **Phase 3.60.0**: ✓ GPU Particle Core (spawn/update/render compute shaders, ping-pong SoA buffers, lock-free free list, indirect draw args, billboard rendering, soft particle depth fade, global particle budget)
> **Source**: `engine/vfx/`, `engine/vfx/components/particle_emitter.h`, `engine/vfx/core/`, `engine/vfx/passes/`
> **References**: UE5 Niagara (clean-room approach), Godot GPUParticles3D

## Purpose

A fully modular, GPU-driven VFX system supporting 1M+ simultaneous particles across emitters, trails, beams, and decals. VFX graphs are authored as JSON node graphs (.uzvfx), compiled to compute shader instructions, and executed on the GPU. Particle data lives in GPU buffers (Structure-of-Arrays layout), eliminating CPU-GPU sync overhead. The system handles LOD, culling, and budget-based allocation across all active effects.

## Architecture

```
VFX Assets (.uzvfx, .uzdecal, .uztrail)
    │
    ├── Particle Emitter (.uzvfx)
    │   ├── Spawn module: rate, lifespan, velocity
    │   ├── Update module: forces, collision, size over life
    │   └── Render module: sprite, mesh, ribbon, light
    │
    ├── Decal Asset (.uzdecal)
    │   ├── Sprite/atlas, normal, roughness
    │   ├── Lifetime, fade curve
    │   └── Sort order, projection settings
    │
    └── Trail Asset (.uztrail)
        ├── Spline definition
        ├── Width curve, UV scroll rate
        └── Color over distance

                    │
                    ▼
         VFX System (per-frame)
                    │
         ├── 1. Tick emitters (spawn new particles)
         ├── 2. GPU compute: update particle state
         ├── 3. Particle→decal placement (raycast, sorting)
         ├── 4. Trail spline update (tessellation)
         ├── 5. CPU frustum culling (quad+AABB)
         ├── 6. Budget check (total particles vs limit)
         ├── 7. Indirect draw setup (DrawIndirect buffer)
         │
         └── GPU Rendering (per emitter)
             ├── Compute dispatch: update particle buffers
             ├── Indirect draw: variable particle count
             ├── Sprite billboard transform
             └── Depth-sort for transparency (optional)

         │
         ▼
     Render (integrated with regular opaque/transparent passes)
```

## Key Components

### ParticleSystem

```cpp
/// GPU-driven particle emitter. Emits particles, ticks simulation, renders.
/// Attached to entities as a component.
class ParticleSystem : public PrimitiveComponent {
public:
    ResourceHandle<VFXGraph> graph;              // .uzvfx node graph

    /// Number of particles allocated for this emitter.
    /// Runtime allocates from global particle pool on demand.
    /// When budget exceeded, oldest particles are culled.
    uint32_t max_particles = 10000;

    /// Emission rate (particles per second). Can be modulated via curve or parameter.
    float emission_rate = 100.0f;

    /// Spawn velocity (magnitude). Direction uses emitter rotation or parameter.
    float spawn_velocity = 10.0f;

    /// Particle lifespan (seconds).
    float lifetime = 5.0f;

    /// Update emitter position/rotation/scale.
    /// Particle spawn positions are relative to emitter.
    void SetEmitterTransform(const Transform& tr);

    /// Set a parameter used by the VFX graph (e.g., particle color, scale).
    void SetParameter(StringView name, const Vec3& value);

    /// Pause/resume emission and simulation.
    void Pause(bool paused);
};
```

### VFX Graph (.uzvfx Format)

```json
{
    "format": "uzvfx",
    "version": 1,
    "name": "explosion_fire",
    "type": "emitter",

    "spawn": {
        "rate": 1000,
        "lifespan_min": 2.0,
        "lifespan_max": 4.0,
        "velocity": [0, 0, 10],
        "velocity_variation": 5.0,
        "spread_angle": 45
    },

    "update": {
        "gravity": [0, 0, -9.81],
        "damping": 0.95,
        "collision": true,
        "collision_response": "kill"
    },

    "render": {
        "type": "sprite",
        "material": "content/vfx/fire.uzmaterial",
        "sprite_size_min": [0.2, 0.2],
        "sprite_size_max": [1.0, 1.0],
        "size_over_life": "content/vfx/curves/size_fade.uzcurve",
        "color_over_life": "content/vfx/curves/color_fade.uzcurve",
        "sort": "back_to_front",
        "cast_shadow": false
    },

    "module_order": ["spawn", "update", "render"],
    "parameters": {
        "intensity": { "type": "float", "default": 1.0 },
        "color_tint": { "type": "vec3", "default": [1, 1, 1] }
    }
}
```

### GPU Particle Buffer Layout (Structure-of-Arrays)

```cpp
/// Particle data lives on GPU in SoA layout for cache efficiency.
/// Single large buffer: [positions (N*3 floats) | velocities | ... | ages]
/// Each module processes a specific offset range.
struct GPUParticleBuffer {
    // Persistent allocation
    uint32_t capacity = 0;              // Max particles ever
    uint32_t num_alive = 0;             // Currently simulated

    GPUBufferHandle positions;          // float3 × capacity
    GPUBufferHandle velocities;         // float3 × capacity
    GPUBufferHandle ages;               // float × capacity
    GPUBufferHandle lifespans;          // float × capacity
    GPUBufferHandle sizes;              // float × capacity
    GPUBufferHandle colors;             // float4 × capacity (RGBA)
    GPUBufferHandle indirect_args;      // uint32_t[5] for DrawIndirect
};

/// GPU compute shader signature (standard pattern).
/// Each thread processes one particle.
[numthreads(256, 1, 1)]
void ParticleUpdate(uint3 id : SV_DispatchThreadID) {
    if (id.x >= num_alive) return;

    float age = ages[id.x];
    float lifespan = lifespans[id.x];

    // Evaluate curves at normalized age [0,1]
    float age_norm = age / lifespan;
    float4 color = SampleColorCurve(color_over_life, age_norm);
    float size = SampleSizeCurve(size_over_life, age_norm);

    // Physics: integrate velocity with gravity/damping
    float3 vel = velocities[id.x];
    vel += gravity * dt;
    vel *= damping;
    positions[id.x] += vel * dt;

    // Store back
    velocities[id.x] = vel;
    ages[id.x] += dt;
    colors[id.x] = color;
    sizes[id.x] = size;
}
```

### Decal System

```cpp
/// Deferred decal placed in world space.
/// Raycast + placement, sorted by distance.
/// Lives until fade timer expires.
class DecalComponent : public PrimitiveComponent {
public:
    ResourceHandle<DecalAsset> asset;    // .uzdecal sprite + curves

    float fade_lifetime = 10.0f;         // Seconds before auto-destroy

    /// Place decal at position, facing surface normal.
    void Place(const Vec3& position, const Vec3& normal);

    /// Fade out over duration.
    void FadeOut(float duration = 0.5f);
};

/// Decal asset with sprite and fade behavior.
struct DecalAsset {
    ResourceHandle<Material> material;
    float lifetime_seconds = 10.0f;

    // Curves: factor [0,1] over normalized lifespan
    ResourceHandle<Curve> fade_curve;
    ResourceHandle<Curve> scale_curve;
};
```

### Trail / Beam System

```cpp
/// Spline-based trail renderer.
/// Follows entity, tessellates spline, applies width/color curves.
class TrailComponent : public PrimitiveComponent {
public:
    ResourceHandle<Material> trail_material;

    /// Points along the trail (most recent at [0]).
    uint32_t max_trail_points = 100;

    /// Width at start of trail.
    float width_start = 0.5f;

    /// Width at end of trail (fade-out).
    float width_end = 0.0f;

    /// Curve modulating width over trail distance [0,1].
    ResourceHandle<Curve> width_curve;

    /// UV scroll rate (units per second along trail).
    float uv_scroll_rate = 1.0f;
};
```

### Module System (Extensible)

| Module Type | Input | Output | Purpose |
|-------------|-------|--------|---------|
| **Spawn** | Rate, velocity, lifespan | Particle state | Initialize new particles |
| **Update** | Forces, drag, collision | Updated positions/velocities | Advance simulation one frame |
| **Decay** | Age, lifespan | Kill flag | Remove expired particles |
| **Collision** | Raycast against scene | Bounce/kill | Handle world collision |
| **Light** | Color, intensity, radius | Light source | Dynamic lights from particles |
| **SpriteRender** | Position, size, color | Quad vertices | Billboarded sprites |
| **MeshRender** | Position, rotation, scale | Instanced mesh draw | 3D mesh instances |
| **RibbonRender** | Position trail, width | Ribbon mesh | Ribbon/trail visual |

## Comparison

| Feature | Unreal 5 Niagara | Godot GPUParticles3D | Unity VFX Graph | **UzEngine** |
|---------|-----------------|---------------------|-----------------|------------|
| Simulation | GPU compute | GPU compute | GPU compute | **GPU compute (SM6.6+)** |
| Authoring | Node graph (blueprint) | Inspector params | Node graph | **Node graph (JSON .uzvfx)** |
| Max particles | 100K+ | 100K+ | 100K+ | **1M+ (GPU memory limited)** |
| Data layout | Striped (SOA) | Striped | Striped | **SoA (GPU buffer)** |
| Module types | 50+ | Limited | 20+ | **12+ (core), extensible** |
| IK / Skeleton | Full | No | No | **No (Phase 4+)** |
| Trails/beams | Yes | Yes | Yes | **Yes (spline-based)** |
| Decals | Via Blueprint | Limited | Via renderers | **Dedicated DecalComponent** |
| LOD system | Distance + bounds | Simple cull | Screen coverage | **Distance + screen-size + budget** |
| Hot-reload | Yes | Yes | Yes | **Yes (FileWatcher + .uzvfx reload)** |

## File Formats

| Format | Extension | Content |
|--------|-----------|---------|
| VFX Graph | `.uzvfx` | JSON node graph + module order + parameters |
| Decal Asset | `.uzdecal` | JSON: sprite, material, lifetime, curves |
| Trail Asset | `.uztrail` | JSON: material, width curve, UV scroll |

## Rendering Pipeline Integration

VFX rendering is integrated into the main render pass:

```
Opaque pass
    ├── Depth prepass (z-prepass)
    └── Forward shading

Transparent pass
    ├── Particles (sorted back-to-front)
    ├── Trails / Beams (sorted by depth)
    └── UI (post-scene)

Deferred decals (after G-Buffer, before lighting)
    ├── Raycast decal positions
    ├── Sort by distance + sort order
    └── Apply deferred decal pass (normal + roughness)
```

## Particle Budget System

A global particle pool prevents runaway memory and GPU thrashing:

```cpp
class ParticleBudget {
public:
    uint32_t max_total_particles = 500000;  // Global limit
    uint32_t particles_used = 0;            // Current count

    /// Request allocation for emitter.
    /// If budget exceeded, oldest particles from other emitters culled.
    bool AllocateParticles(uint32_t requested, uint32_t& allocated);
};
```

When multiple emitters compete for a fixed budget, the system:
1. Prioritizes emitters by visibility (frustum culled = lower priority)
2. Prioritizes by distance (farther = lower priority)
3. Prioritizes by user-set priority value (0–100)
4. Culls oldest particles from lower-priority emitters if needed

## Implementation Phase

| Phase | Status | What Ships |
|-------|--------|-----------|
| **3.60.0** | ✓ DONE | **GPU Particle Core System**: ParticleEmitter component, GPUParticleBuffer (ping-pong SoA), ParticleBudget (lock-free CAS allocator), VFXSystem manager, ParticleSimulationPass (spawn/update/indirect-args compute), ParticleRenderPass (indirect billboard draw), billboard vertex expansion (SV_VertexID), soft particle depth fade, free list atomic protocol (spawn pop / update push/recycle) |
| **3.60.1** | TBD | **Decal System**: DecalComponent, decal placement (raycast-based), deferred decal pass, fade curves, atlasing |
| **3.60.2** | TBD | **Trail/Beam System**: TrailComponent, spline tessellation, width curves, UV scrolling |
| **Phase 4.0** | Planned | VFX node graph editor (visual authoring), advanced modules (collision, light constraint), LOD system with distance/screen-size culling, decal batching, GPU memory profiling, performance tuning |
