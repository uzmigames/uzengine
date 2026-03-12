# VFX Graph — Node-Based Authoring, .uzvfx Format, Parameter Curves, Events

> **Depends on**: VFX modules (§25.3), Asset cooking (§43)
> **Format**: `.uzvfx` JSON, `.uzcurve` JSON (parameter curves)
> **Editor**: Visual graph editor (Phase 4), or JSON editing (Phase 3)

## Purpose

A `.uzvfx` file defines a complete particle effect: which modules run, in what order, with what parameters. VFX graphs are:

- **JSON-based**: Human-readable, LLM-friendly, version-controllable
- **Modular**: Reusable, composable effects (explosion base + fire + smoke)
- **Parameterized**: Curves for size/color over lifetime, parameter exposure for runtime tweaking
- **Events**: Emit particles on demand (e.g., bullet impact creates particle burst)

## VFX Graph Format — `.uzvfx` JSON

### Basic Structure

```json
{
  "format": "uzvfx",
  "version": 1,
  "name": "explosion_fire",
  "type": "emitter",
  "description": "Fire explosion with billboarded flames",

  "emitter": {
    "max_particles": 50000,
    "lifespan_default": 3.0,
    "lifetime_variation": 0.5
  },

  "modules": [
    {
      "type": "spawn",
      "enabled": true,
      "config": {
        "rate": 1000,
        "lifespan_min": 1.5,
        "lifespan_max": 3.5,
        "velocity": [0, 8, 0],
        "velocity_variation": 4.0,
        "spread_angle": 60
      }
    },
    {
      "type": "update",
      "enabled": true,
      "config": {
        "gravity": [0, -3.0, 0],
        "drag": 0.98
      }
    },
    {
      "type": "sprite_render",
      "enabled": true,
      "config": {
        "material": "content/vfx/fire_sprite.uzmaterial",
        "sprite_width": 0.5,
        "sprite_height": 0.5,
        "size_over_life": "curves/fire_size.uzcurve",
        "color_over_life": "curves/fire_color.uzcurve",
        "depth_sort": true
      }
    },
    {
      "type": "decay",
      "enabled": true
    }
  ],

  "parameters": {
    "intensity": {
      "type": "float",
      "default": 1.0,
      "min": 0.0,
      "max": 2.0,
      "description": "Overall effect intensity (affects emission rate)"
    },
    "color_tint": {
      "type": "vec3",
      "default": [1.0, 1.0, 1.0],
      "description": "RGB color multiplier"
    },
    "lifetime_scale": {
      "type": "float",
      "default": 1.0,
      "min": 0.5,
      "max": 2.0,
      "description": "Scale all particle lifetimes"
    }
  },

  "events": [
    {
      "name": "burst",
      "description": "Emit a one-time burst of particles",
      "emission_count": 500
    }
  ]
}
```

### Module Reference in Graph

Each module in the `modules` array:

```json
{
  "type": "module_type",        // "spawn", "update", "sprite_render", etc.
  "enabled": true,              // Can disable/enable modules
  "config": {                   // Module-specific parameters
    // Vary by module type
  }
}
```

## Parameter Curves — `.uzcurve` Format

Curves define how properties change over lifetime (0 = start, 1 = end).

```json
{
  "format": "uzcurve",
  "version": 1,
  "name": "fire_size",
  "type": "float",              // "float", "vec3", "vec4"
  "description": "Particle size over lifetime",

  "keys": [
    { "time": 0.0,   "value": 0.1 },    // Start small
    { "time": 0.3,   "value": 1.0 },    // Grow quickly
    { "time": 0.7,   "value": 0.8 },    // Shrink slightly
    { "time": 1.0,   "value": 0.0 }     // Fade to nothing
  ],

  "interpolation": "smoothstep"  // "linear", "cubic", "smoothstep"
}
```

Color curve:

```json
{
  "format": "uzcurve",
  "type": "vec4",               // RGBA color
  "name": "fire_color",

  "keys": [
    { "time": 0.0, "value": [1.0, 0.3, 0.0, 1.0] },   // Orange
    { "time": 0.5, "value": [1.0, 0.7, 0.0, 0.8] },   // Yellow (less opaque)
    { "time": 1.0, "value": [0.5, 0.5, 0.5, 0.0] }    // Gray (transparent)
  ]
}
```

### Curve Evaluation

```cpp
namespace uz::VFX {
    class Curve {
    public:
        /// Sample curve at normalized time [0, 1].
        float Sample(float time) const;
        Vec3 SampleVec3(float time) const;
        Vec4 SampleVec4(float time) const;

    private:
        // Interpolation between keys
        // Linear: y = a + (b - a) * t
        // Cubic: uses Catmull-Rom splines
        // Smoothstep: Hermite smoothing (ease-in-out)
    };
}

// GPU code: sample in compute shader
float age_normalized = age / lifespan;
float size = SampleCurve(size_over_life, age_normalized);
float4 color = SampleCurve(color_over_life, age_normalized);
```

## Parameter Exposure

Parameters allow runtime tweaking without reloading the asset:

```cpp
class ParticleSystem : public Component {
    void SetParameter(StringView name, float value);
    void SetParameter(StringView name, const Vec3& value);
};

// Usage: Lua script
function on_start(ctx)
    local fire = ctx.scene:find_by_name("explosion"):get_component("ParticleSystem")
    fire:set_parameter("intensity", 2.0)      -- Double emission
    fire:set_parameter("color_tint", {1, 0.5, 0})  -- Orange-ish
end
```

Parameters flow through the system:

```
.uzvfx file:
  parameters:
    intensity: { default: 1.0 }

Module config:
  emission_rate: base_rate * intensity_param

Lua API:
  fire:set_parameter("intensity", 2.0)

Effect:
  Emission rate becomes 2.0x base_rate (no reload)
```

## Events

Events allow triggering effects programmatically (e.g., weapon hits, footsteps).

### Event Definition

```json
"events": [
  {
    "name": "burst",
    "description": "One-time emission burst",
    "emission_count": 500,
    "velocity_scale": 1.5
  },
  {
    "name": "impact",
    "description": "Particle burst on collision",
    "emission_count": 100
  }
]
```

### Event Invocation

```cpp
auto particles = entity->Get<ParticleSystem>();
particles->EmitEvent("burst");     // Emit 500 particles at once
particles->EmitEvent("impact");    // Emit 100 particles
```

Lua API:

```lua
local emitter = ctx.scene:find_by_name("projectile"):get_component("ParticleSystem")
emitter:emit_event("impact")  -- Fire event on collision
```

## Composition: Multi-Effect Assets

Complex effects combine multiple emitters:

```json
{
  "format": "uzvfx_composite",
  "name": "explosion_full",
  "description": "Full explosion: fire + smoke + debris",

  "emitters": [
    {
      "name": "fire",
      "graph": "content/vfx/explosion_fire.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.0
    },
    {
      "name": "smoke",
      "graph": "content/vfx/explosion_smoke.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.2   // Start 200ms after fire
    },
    {
      "name": "debris",
      "graph": "content/vfx/debris_burst.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.0
    }
  ]
}
```

## VFX Graph Editor (Phase 4)

Visual authoring with drag-and-drop modules:

```
┌─────────────────────────────────────┐
│ VFX Graph Editor                    │
├─────────────────────────────────────┤
│                                     │
│  [Spawn Module]                     │
│        │                            │
│        ▼                            │
│  [Update Module]                    │
│        │                            │
│        ▼                            │
│  [Collision Module]                 │
│        │                            │
│        ▼                            │
│  [SpriteRender Module]              │
│        │                            │
│        ▼                            │
│  [Decay Module]                     │
│                                     │
├─────────────────────────────────────┤
│ Parameters:                         │
│  intensity:        [slider 0-2]    │
│  color_tint:       [color picker]  │
│  lifetime_scale:   [slider 0.5-2]  │
└─────────────────────────────────────┘
```

Features:

- Drag modules from palette onto canvas
- Connect modules with wires
- Double-click module to edit parameters
- Real-time preview (viewport shows effect)
- Curve editor for size/color (graphical)
- Save/export as `.uzvfx` JSON

## VFX System Integration

```cpp
namespace uz::VFX {
    class VFXGraph {
    public:
        /// Load from .uzvfx file.
        static ResourceHandle<VFXGraph> Load(const uz::Path& path);

        /// Get modules in execution order.
        const std::vector<ModuleHandle>& GetModules() const;

        /// Get parameter definitions.
        const std::map<std::string, Parameter>& GetParameters() const;

        /// Get events.
        const std::vector<Event>& GetEvents() const;
    };

    class ParticleSystem : public PrimitiveComponent {
    public:
        /// Set VFX graph.
        void SetGraph(ResourceHandle<VFXGraph> graph);

        /// Set parameter value.
        void SetParameter(StringView name, float value);

        /// Emit event (triggers particles).
        void EmitEvent(StringView event_name);
    };
}
```

## Example: Weapon Impact Effect

```json
{
  "format": "uzvfx",
  "name": "bullet_impact",
  "type": "emitter",

  "emitter": {
    "max_particles": 1000,
    "lifespan_default": 1.0
  },

  "modules": [
    {
      "type": "spawn",
      "config": {
        "rate": 0,          // No continuous emission
        "velocity": [5, 5, 0],
        "spread_angle": 180  // Emit in all directions
      }
    },
    {
      "type": "update",
      "config": {
        "gravity": [0, -9.81, 0],
        "drag": 0.95
      }
    },
    {
      "type": "sprite_render",
      "config": {
        "material": "content/vfx/spark.uzmaterial",
        "sprite_width": 0.1,
        "sprite_height": 0.1,
        "color_over_life": "curves/spark_fade.uzcurve"
      }
    }
  ],

  "events": [
    {
      "name": "impact",
      "emission_count": 50,
      "description": "Spark burst on bullet hit"
    }
  ]
}
```

Usage in game:

```cpp
// On bullet hit
void OnBulletHit(const HitInfo& hit) {
    // Spawn impact particles at hit location
    auto particles = world->CreateEntity();
    auto emitter = particles->AddComponent<ParticleSystem>();
    emitter->SetGraph(uz::Resource::Load<VFXGraph>("content/vfx/bullet_impact.uzvfx"));
    emitter->SetEmitterTransform({hit.position, Quat::Identity()});

    // Emit the impact event (50 sparks)
    emitter->EmitEvent("impact");
}
```

## Hot-Reload in Editor

During development, modifying `.uzvfx` reloads instantly:

```cpp
#if UZ_DEV_BUILD
uz::VFX::StartWatching("content/vfx/**/*.uzvfx");
// On save in editor:
//   1. Reload .uzvfx file
//   2. Recompile compute shader with new modules
//   3. Update all active emitters using that graph
#endif
```

## Cooking and Packaging

`uzcook` compiles VFX graphs:

```bash
$ uzcook --vfx content/vfx/*.uzvfx
  → Validates JSON
  → Compiles compute shaders (SM6.6)
  → Packages into .uzpak with textures, materials, curves
```

## Comparison

| Feature | Unreal 5 Niagara | Godot GPU Particles | Unity VFX Graph | **UzEngine** |
|---------|-----------------|---------------------|-----------------|------------|
| Format | Binary `.uasset` | Binary (inspector) | Shader graph | **JSON `.uzvfx`** |
| Editor | Visual node graph | Inspector | Visual | **Phase 4: Visual, Phase 3: JSON** |
| Parameters | Exposed in blueprint | Inspector | Graph input | **Exposed + Lua API** |
| Events | Limited | None | Limited | **Event-driven** |
| Hot-reload | Yes | Yes | Yes | **Yes (Phase 3+)** |
| Composition | Emitter stacking | Manual | Manual | **Multi-emitter assets** |
| Curves | Visual editor | Limited | Yes | **JSON `.uzcurve` + editor** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | `.uzvfx` JSON loader, module evaluation, curves, parameters, events (JSON authoring) |
| **Phase 4** | Visual graph editor, Curve editor, Preview viewport, Multi-emitter composition |
| **Phase 5** | Advanced graph features (loops, conditionals), performance profiling tools |
