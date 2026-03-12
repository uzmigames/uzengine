# VFX Modules — Spawn, Update, Render, Light, Collision Modules

> **Depends on**: Particle system (§25.2), VFX graph (§25.4)
> **Used by**: VFX authoring, emitter behavior customization
> **Extensible**: Custom modules via virtual interface

## Purpose

Modules are reusable VFX components that define behavior:

- **Spawn modules**: Initial particle state (velocity, lifetime, position)
- **Update modules**: Per-frame changes (gravity, drag, collision)
- **Render modules**: Visual output (sprite, mesh, ribbon, light)
- **Decay modules**: Remove particles (age-based, collision-based)

Modules are chained and evaluated in order, enabling composition of complex effects from simple pieces.

## Module Architecture

```cpp
namespace uz::VFX {
    /// Base class for all VFX modules.
    class Module {
    public:
        virtual ~Module() = default;

        /// Execute module logic in compute shader.
        /// Input: particle indices and GPU buffers
        /// Output: modified GPU buffer values
        virtual void Execute(
            const ExecuteContext& context  // Particle data, constants, etc.
        ) = 0;

        /// Get HLSL code snippet for compute shader compilation.
        virtual std::string GetShaderCode() const = 0;

        /// Get parameters (exposed in VFX editor, UI, Lua).
        virtual std::vector<Parameter> GetParameters() const = 0;
    };

    /// Execution context: GPU buffers and compute settings.
    struct ExecuteContext {
        uint32_t num_particles;
        float dt;

        // GPU buffers (RWStructuredBuffer in shader)
        GPUBufferHandle positions;
        GPUBufferHandle velocities;
        GPUBufferHandle ages;
        GPUBufferHandle colors;
        GPUBufferHandle sizes;

        // Parameters from .uzvfx file
        std::map<std::string, Vec4> params;
    };
}
```

## Core Modules

### Spawn Module

Initializes new particles each frame.

```cpp
namespace uz::VFX {
    class SpawnModule : public Module {
    public:
        struct Config {
            float rate = 100.0f;         // Particles per second
            float lifespan_min = 1.0f;
            float lifespan_max = 3.0f;
            Vec3 velocity = {0, 5, 0};   // Base velocity
            float velocity_variation = 1.0f;
            float spread_angle = 45.0f;  // Cone angle
        } config;

        std::string GetShaderCode() const override {
            return R"(
                // Compute: spawn new particles
                if (idx < particles_to_spawn) {
                    float3 vel = normalize(velocity + RandomVec3(spread_angle)) * length(velocity);
                    vel += RandomVec3() * velocity_variation;

                    positions[idx] = emitter_pos + RandomInSphere(spawn_radius);
                    velocities[idx] = vel;
                    ages[idx] = 0;
                    lifespans[idx] = RandomFloat(lifespan_min, lifespan_max);
                    colors[idx] = float4(1, 1, 1, 1);
                    sizes[idx] = 1.0;
                }
            )";
        }
    };
}
```

### Update Module (Gravity + Damping)

Applies physics each frame.

```cpp
class UpdateModule : public Module {
public:
    struct Config {
        Vec3 gravity = {0, -9.81, 0};
        float drag = 0.95f;             // Per-frame damping
        float angular_velocity = 0.0f;  // Rotation per second
    } config;

    std::string GetShaderCode() const override {
        return R"(
            // Physics update
            float3 vel = velocities[idx];
            vel += gravity * dt;
            vel *= drag;
            positions[idx] += vel * dt;
            velocities[idx] = vel;
        )";
    }
};
```

### Decay Module

Removes expired particles.

```cpp
class DecayModule : public Module {
public:
    std::string GetShaderCode() const override {
        return R"(
            // Kill if expired
            if (ages[idx] >= lifespans[idx]) {
                positions[idx].y = -10000;  // Out-of-bounds (skipped by renderer)
                ages[idx] = lifespans[idx];
            }
        )";
    }
};
```

### Render Module — Sprite

Renders particles as billboarded quads (most common).

```cpp
class SpriteRenderModule : public Module {
public:
    struct Config {
        uz::ResourceHandle<Material> material;
        float sprite_width = 1.0f;
        float sprite_height = 1.0f;
        CurveHandle size_over_life;      // Size curve
        CurveHandle color_over_life;     // Color curve
        bool depth_sort = true;          // Sort back-to-front
    } config;

    std::string GetShaderCode() const override {
        return R"(
            // Sample curves at normalized age
            float age_norm = ages[idx] / lifespans[idx];
            float size = SampleCurve(size_over_life, age_norm);
            float4 color = SampleCurve(color_over_life, age_norm);

            sizes[idx] = sprite_width * size;
            colors[idx] = color;
        )";
    }

    // Render: called after compute
    void RenderSprites(const RenderContext& ctx) {
        // Iterate all particles
        // For each alive particle:
        //   Create billboard quad
        //   Sample material texture
        //   Add to transparent render queue
    }
};
```

### Render Module — Mesh

Renders oriented mesh instances (e.g., snowflakes, debris).

```cpp
class MeshRenderModule : public Module {
public:
    struct Config {
        uz::ResourceHandle<MeshAsset> mesh;
        uz::ResourceHandle<Material> material;
        Vec3 mesh_scale = {1, 1, 1};
        bool cast_shadow = false;
    } config;

    void RenderMeshes(const RenderContext& ctx) {
        // Iterate particles
        // For each alive particle:
        //   Create instance with transform (position, rotation, scale)
        //   Collect into mesh instancing batch
        //   Render with instanced draw call
    }
};
```

### Render Module — Ribbon/Trail

Connects sequential particles into a ribbon.

```cpp
class RibbonRenderModule : public Module {
public:
    struct Config {
        uz::ResourceHandle<Material> material;
        float width_start = 1.0f;
        float width_end = 0.0f;          // Fade to point
        CurveHandle width_curve;
        float uv_scroll_rate = 1.0f;     // Units per second
    } config;

    // Ribbon: trail of particles, tessellated into a mesh
    void RenderRibbon(const RenderContext& ctx) {
        // Collect particle positions (most recent at [0])
        // Create ribbon mesh: quads between sequential particles
        // Apply width curve for tapering
    }
};
```

### Render Module — Light

Emits dynamic lights from particles.

```cpp
class LightRenderModule : public Module {
public:
    struct Config {
        float radius = 10.0f;
        float intensity = 1.0f;
        CurveHandle intensity_over_life;
        bool cast_shadow = false;
    } config;

    void RenderLights(const RenderContext& ctx) {
        for (uint32_t i = 0; i < num_alive; i++) {
            float age_norm = ages[i] / lifespans[i];
            float intensity = SampleCurve(intensity_over_life, age_norm);

            // Create light source at particle position
            LightComponent light;
            light.position = positions[i];
            light.color = colors[i];
            light.intensity = intensity;
            light.radius = radius;
            ctx.light_list->Add(light);
        }
    }
};
```

## Advanced Modules

### Collision Module

Raycast particles against world geometry.

```cpp
class CollisionModule : public Module {
public:
    enum class CollisionResponse { Kill, Bounce, Slide };

    struct Config {
        CollisionResponse response = Kill;
        float bounce_factor = 0.5f;      // Elasticity
    } config;

    std::string GetShaderCode() const override {
        return R"(
            // Raycast from current position in direction of velocity
            RaycastResult hit;
            if (Raycast(positions[idx], velocities[idx], hit)) {
                if (response == Kill) {
                    ages[idx] = lifespans[idx];  // Mark expired
                } else if (response == Bounce) {
                    velocities[idx] = reflect(velocities[idx], hit.normal) * bounce_factor;
                    positions[idx] = hit.position + hit.normal * 0.1f;  // Push away from surface
                }
            }
        )";
    }
};
```

### Turbulence Module

Adds Perlin noise velocity.

```cpp
class TurbulenceModule : public Module {
public:
    struct Config {
        float strength = 1.0f;
        float frequency = 1.0f;          // Noise scale
        float octaves = 3.0f;            // Fractal detail
    } config;

    std::string GetShaderCode() const override {
        return R"(
            // Perlin noise velocity
            float3 noise_vel = Perlin3D(positions[idx] * frequency, time) * strength;
            velocities[idx] += noise_vel * dt;
        )";
    }
};
```

### Constraint Module

Constraints particles to bounds.

```cpp
class ConstraintModule : public Module {
public:
    struct Config {
        Vec3 bounds_min = {-50, -50, -50};
        Vec3 bounds_max = {50, 50, 50};
    } config;

    std::string GetShaderCode() const override {
        return R"(
            // Clamp to bounds
            positions[idx] = clamp(positions[idx], bounds_min, bounds_max);
        )";
    }
};
```

## Module Composition

Modules are evaluated in order:

```json
{
  "uzvfx": "explosion_fire",
  "modules": [
    { "type": "spawn", "config": { "rate": 500, "velocity": [0, 10, 0] } },
    { "type": "update", "config": { "gravity": [0, -9.81, 0], "drag": 0.95 } },
    { "type": "sprite_render", "config": { "size_over_life": "curves/size_fade.uzcurve" } },
    { "type": "decay" }
  ]
}
```

Execution order (per frame):

```
1. Spawn Module   → Create new particles (age=0)
2. Update Module  → Apply gravity, drag
3. Collision Mod  → Check hits, bounce/kill
4. Render Module  → Set size/color curves
5. Decay Module   → Mark expired as dead
```

## Parameter Exposure

Parameters from modules are accessible in editor and Lua:

```cpp
class ParticleSystem {
    void SetParameter(StringView name, float value);
    void SetParameter(StringView name, const Vec3& value);
};

// Usage:
auto emitter = scene->FindByName("fire_effect")->Get<ParticleSystem>();
emitter->SetParameter("emission_rate", 200.0f);  // Double emission
emitter->SetParameter("spawn_velocity", {0, 15, 0});  // Faster upward
```

## Custom Modules

Games can extend with custom modules:

```cpp
// Game-specific: wind force
class WindModule : public uz::VFX::Module {
    struct Config {
        Vec3 wind_direction = {1, 0, 0};
        float wind_strength = 5.0f;
    } config;

    std::string GetShaderCode() const override {
        return R"(
            velocities[idx] += wind_direction * wind_strength * dt;
        )";
    }
};

// Register custom module
uz::VFX::ModuleRegistry::Register<WindModule>("wind");
```

## Module Table Reference

| Module Type | Input | Output | Phase |
|-------------|-------|--------|-------|
| **Spawn** | Rate, velocity, lifespan | Position, velocity, age | 3 (Phase 3) |
| **Update** | Velocity, gravity, drag | Velocity, position | 3 |
| **Decay** | Age, lifespan | Kill flag | 3 |
| **SpriteRender** | Position, size, color curves | Size, color, UV | 3 |
| **Collision** | Position, velocity, physics world | Velocity, position, kill flag | 4 (Phase 4) |
| **Light** | Position, color, intensity | Light list | 4 |
| **Turbulence** | Position, time | Velocity delta | 4 |
| **Constraint** | Position, bounds | Clamped position | 4 |
| **MeshRender** | Position, rotation, scale | Instanced draw | 4 |
| **RibbonRender** | Position history, width | Ribbon mesh | 4 |

## Performance Guidelines

Module overhead scales linearly with particle count:

```
Spawn Module:       ~0.01 ms (100K particles)
Update Module:      ~0.05 ms (gravity, drag)
Collision Module:   ~0.3 ms (raycast per particle)
Render Module:      ~0.2 ms (curve sampling)
─────────────────────────────────
Total per 100K:     ~0.6 ms (on SM6.6 GPU)
```

Optimization:

- **Combine simple modules**: Multiple small modules overhead, combine when possible
- **Batch raycasts**: Collision module batches all particles' raycasts in one compute dispatch
- **Curve caching**: Pre-sample curves if unchanged per frame

## Comparison

| Feature | Unreal 5 Niagara | Godot GPUParticles3D | **UzEngine** |
|---------|-----------------|---------------------|------------|
| Module count | 50+ | Limited | **12+ core** |
| Custom modules | Via C++ class | Limited | **Virtual interface** |
| Composition | Node graph | Inspector | **`.uzvfx` JSON + programmatic** |
| Performance | High (optimized) | Medium | **High (SoA, compute)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Spawn, Update, Decay, SpriteRender modules |
| **Phase 4** | Collision, Light, Turbulence, Constraint, MeshRender, RibbonRender modules |
| **Phase 5** | Custom module API, module profiling tools |
