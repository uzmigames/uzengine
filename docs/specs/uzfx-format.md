<!-- doc-meta
title: .uzfx Particle Emitter Asset Format
category: specs
theme: vfx
created: 2026-03-26
-->

# .uzfx — Declarative Particle Emitter Definitions

JSON-based asset format for data-driven VFX particle emitter configurations. Loaded by `UzfxLoader` and used to construct `ParticleEmitter` instances with fully configured module stacks at runtime.

## Format

```json
{
  "name": "Effect Name",
  "version": 1,
  "emitter": {
    "max_particles": 10000,
    "emission_rate": 500.0,
    "lifetime": 2.0,
    "looping": true
  },
  "modules": {
    "spawn": { ... },
    "update": [ ... ],
    "render": { ... }
  }
}
```

## Top-Level Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | string | `"unnamed"` | Human-readable effect name (for logs/debug) |
| `version` | uint | `1` | Format version (for future migration) |

## Emitter Section

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_particles` | uint | `10000` | Maximum particles from global budget |
| `emission_rate` | float | `100.0` | Particles spawned per second |
| `lifetime` | float | `2.0` | Particle lifetime in seconds |
| `looping` | bool | `true` | Restart after all particles die |

## Module Types

### Spawn Module

```json
{"type": "spawn", "velocity": [0,10,0], "spread_angle": 30.0, "rate": 500.0, "lifespan_min": 0.5, "lifespan_max": 2.0}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `velocity` | vec3 | `[0,0,200]` | Initial spawn velocity (cm/s) |
| `spread_angle` | float | `0.0` | Cone spread angle (degrees) |
| `rate` | float | `emission_rate` | Override spawn rate |
| `lifespan_min` | float | `lifetime*0.5` | Minimum particle lifespan |
| `lifespan_max` | float | `lifetime` | Maximum particle lifespan |

### Update Module

```json
{"type": "update", "gravity": [0,-9.81,0], "drag": 0.98}
```

### Turbulence Module

```json
{"type": "turbulence", "strength": 2.0, "frequency": 0.5, "octaves": 3}
```

### Decay Module

```json
{"type": "decay"}
```

No parameters — applies alpha fade over lifetime.

### Attractor Module

```json
{"type": "attractor", "position": [0,0,0], "strength": 10.0, "radius": 5.0, "kill_on_arrival": false}
```

### Orbit Module

```json
{"type": "orbit", "center": [0,0,0], "speed": 1.0, "radius": 5.0, "axis": [0,0,1]}
```

### Collision Module

```json
{"type": "collision", "bounce_factor": 0.6, "depth_buffer_collision": true, "collision_offset": 0.01}
```

### Vector Field Module

```json
{"type": "vector_field", "intensity": 1.0, "tightness": 0.0, "bounds": [10,10,10]}
```

### Render Modules

#### Sprite (billboard)
```json
{"type": "sprite", "width": 0.5, "height": 0.5}
```

#### Mesh (instanced)
```json
{"type": "mesh", "mesh_path": "meshes/spark.obj", "material_path": "", "scale": [1,1,1], "cast_shadow": false}
```

#### Light (per-particle)
```json
{"type": "light", "intensity": 1.0, "radius": 5.0, "cast_shadow": false}
```

## File Location

`.uzfx` files are stored in the demo or project's `assets/` directory and loaded via VFS-relative paths:

```
runtime/demos/fire_effect/assets/fire_effect.uzfx
sample/content/effects/campfire.uzfx
```

## Engine API

```cpp
#include "engine/vfx/asset/uzfx_loader.h"

// Parse
auto desc = uz::UzfxLoader::Load(jsonString);

// Instantiate
uz::ParticleEmitter emitter;
uz::UzfxLoader::Instantiate(desc, emitter, device, budget);
```

## ECS Integration

```json
{
  "type": "vfx_emitter",
  "asset": "effects/fire.uzfx",
  "emitting": true,
  "looping": true
}
```

The `VfxEmitterComponent` can reference a `.uzfx` asset via the `asset` field, or use inline parameters for simple effects.
