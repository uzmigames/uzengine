# .uzvfx JSON Format Specification

> **Status**: Planned (Phase 2.5)
> **Format**: JSON
> **References**: [VFX System Overview](01-overview.md), [VFX Modules](03-vfx-modules.md), [Module API](04-module-api.md)

## Purpose

The `.uzvfx` format is the serialized representation of a VFX graph asset. It defines a complete particle effect: spawn behavior, physics simulation, forces, constraints, rendering, and lifecycle events. A single `.uzvfx` file contains one emitter with its complete configuration, module stack, parameters, and events.

## Format Structure

A minimal `.uzvfx` file:

```json
{
  "format": "uzvfx",
  "version": 1,
  "name": "fire_effect",
  "emitter": {
    "max_particles": 10000,
    "lifespan_default": 5.0
  },
  "modules": [
    { "type": "spawn", "config": { "rate": 500 } },
    { "type": "update", "config": {} },
    { "type": "sprite_render", "config": { "sprite_width": 0.5, "sprite_height": 0.5 } }
  ],
  "parameters": [],
  "events": []
}
```

## Root Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `format` | string | Yes | Must be exactly `"uzvfx"` (case-sensitive). Used to identify file type. |
| `version` | integer | Yes | Version of the `.uzvfx` format. Current version is `1`. Future-proofs the format for breaking changes. |
| `name` | string | Yes | Asset name (e.g., `"fire_effect"`, `"explosion"`). Used for identification and debug logging. |
| `emitter` | object | Yes | Emitter configuration (see [Emitter Configuration](#emitter-configuration) section). |
| `modules` | array | Yes | Ordered array of module objects. At least one module must be present. |
| `parameters` | array | No | Array of exposed parameters (default: empty). Used for runtime tuning. |
| `events` | array | No | Array of named events (default: empty). Used for triggered emissions. |

## Emitter Configuration

The `emitter` object defines the lifetime and capacity of the particle emitter:

```json
{
  "emitter": {
    "max_particles": 10000,
    "lifespan_default": 5.0
  }
}
```

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| `max_particles` | integer | Required | > 0, ≤ 65536 | Maximum particles this emitter can hold. Memory allocated at load time. |
| `lifespan_default` | number | Required | > 0.0 | Default particle lifespan in seconds. Modules may override per-particle. |

**Notes:**
- `max_particles` directly affects GPU memory usage: 56 bytes per particle (SoA layout) × 2 (ping-pong) = 112 bytes per particle minimum. Example: 10000 particles ≈ 1.12 MB.
- `lifespan_default` is a fallback; spawn module `lifespan_min`/`lifespan_max` override it.

## Modules Array

Modules define the behavior pipeline. They execute in order each frame. Each module must have `type` and `config`:

```json
{
  "modules": [
    { "type": "spawn", "config": { ... } },
    { "type": "update", "config": { ... } },
    { "type": "turbulence", "config": { ... } },
    { "type": "sprite_render", "config": { ... } }
  ]
}
```

### Module Type: `spawn`

Spawns new particles each frame.

```json
{
  "type": "spawn",
  "config": {
    "rate": 500,
    "spread_angle": 60,
    "spawn_velocity": [0, 8, 0],
    "lifespan_min": 1.0,
    "lifespan_max": 3.0,
    "spawn_shape": "Sphere",
    "spawn_radius": 0.5
  }
}
```

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| `rate` | number | 0 | ≥ 0, per frame | Particles to spawn per frame. Fractional rates accumulate (e.g., rate=0.5 spawns 1 particle every 2 frames). |
| `spread_angle` | number | 0 | 0–360 degrees | Cone spread angle for velocity direction. 0 = no spread, 360 = full sphere. |
| `spawn_velocity` | vec3 | [0, 0, 0] | — | Initial velocity in m/s (world-space, before spread applied). |
| `lifespan_min` | number | emitter.lifespan_default | > 0.0 | Minimum random lifespan in seconds. |
| `lifespan_max` | number | emitter.lifespan_default | ≥ lifespan_min | Maximum random lifespan in seconds. |
| `spawn_shape` | string | "Point" | "Point", "Sphere", "Box", "Cone" | Spatial distribution shape for spawn positions. |
| `spawn_radius` | number | 0 | ≥ 0 | Radius/size of spawn shape (ignored for "Point"). |

**Spawn Shape Details:**
- **"Point"**: All particles spawn at emitter origin. `spawn_radius` ignored.
- **"Sphere"**: Particles distributed randomly within a sphere of radius `spawn_radius`. Uniform in 3D volume.
- **"Box"**: Particles distributed within a box of half-size `spawn_radius`. Uniform in 3D volume.
- **"Cone"**: Particles spawn on the surface of a cone pointing forward (+Y), with `spread_angle` controlling cone angle and `spawn_radius` controlling height.

### Module Type: `update`

Updates particle physics each frame (gravity, drag, velocity).

```json
{
  "type": "update",
  "config": {
    "gravity": [0, -9.81, 0],
    "drag": 0.98,
    "angular_velocity": 0.0
  }
}
```

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| `gravity` | vec3 | [0, 0, 0] | — | Acceleration applied every frame (m/s²). Example: [0, -9.81, 0] for Earth gravity. |
| `drag` | number | 1.0 | 0.0–1.0 | Velocity damping multiplier per frame. 1.0 = no drag, 0.9 = 10% loss per frame, 0.0 = instant stop. |
| `angular_velocity` | number | 0.0 | — | Rotational speed (degrees/second) for 2D rotation. Applied per frame. |

**Physics Formula:**
```
velocity += gravity * deltaTime
velocity *= drag^deltaTime
position += velocity * deltaTime
rotation += angular_velocity * deltaTime
```

### Module Type: `turbulence`

Adds procedural noise-based forces (Perlin noise, FBM).

```json
{
  "type": "turbulence",
  "config": {
    "strength": 2.0,
    "frequency": 0.5,
    "octaves": 3
  }
}
```

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| `strength` | number | 1.0 | ≥ 0 | Magnitude of noise-based force applied to velocity (m/s²). |
| `frequency` | number | 1.0 | > 0 | Spatial frequency of noise. Higher = more localized variation. |
| `octaves` | integer | 1 | 1–8 | Number of Perlin octaves (fractal Brownian motion). Higher = more detail, more expensive. |

**Implementation:**
Uses GPU FBM3D noise (Perlin) in world-space coordinates. Force is applied as a curl noise field to avoid blocky gradients.

### Module Type: `constraint`

Constrains particles within a bounding box.

```json
{
  "type": "constraint",
  "config": {
    "bounds_min": [-10, -10, -10],
    "bounds_max": [10, 10, 10]
  }
}
```

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| `bounds_min` | vec3 | Required | — | Minimum corner of bounding box (world-space). |
| `bounds_max` | vec3 | Required | — | Maximum corner of bounding box (world-space). |

**Behavior:**
When a particle position exceeds bounds, it is clamped to the boundary and velocity is zeroed in that axis (elastic collision with walls).

### Module Type: `sprite_render`

Renders particles as camera-facing billboards.

```json
{
  "type": "sprite_render",
  "config": {
    "sprite_width": 0.5,
    "sprite_height": 0.5,
    "depth_sort": true
  }
}
```

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| `sprite_width` | number | 1.0 | > 0 | Billboard width in world-space meters. |
| `sprite_height` | number | 1.0 | > 0 | Billboard height in world-space meters. |
| `depth_sort` | boolean | false | — | If true, sort particles by depth before rendering (expensive, required for transparent blend). |

**Rendering:**
Draws particles as fullscreen quads with quad-per-particle. Uses particle position and camera forward/right/up to compute billboard corners. Applies particle color and optional texture.

### Module Type: `decay`

Marks particles as dead when lifespan expires (implicit, but can be listed explicitly).

```json
{
  "type": "decay",
  "config": {}
}
```

**Behavior:**
Compares particle age against lifespan. When `age >= lifespan`, particle is marked dead and recycled. Always present implicitly; listing explicitly is optional.

## Parameters

Parameters expose runtime tunables for each effect. They are used by Lua scripts and the editor to adjust effect properties at runtime.

```json
{
  "parameters": [
    { "name": "intensity", "type": "float", "default": 1.0, "min": 0.0, "max": 10.0 },
    { "name": "wind_dir", "type": "vec3", "default_vec3": [1.0, 0.0, 0.0] },
    { "name": "color_tint", "type": "vec4", "default_vec4": [1.0, 0.5, 0.0, 1.0] }
  ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Unique parameter identifier (e.g., `"intensity"`, `"wind_dir"`). Used in Lua API: `set_parameter(entity, name, value)`. |
| `type` | string | Yes | Data type: `"float"`, `"vec3"`, or `"vec4"`. |
| `default` | number | (type-dependent) | Default value for `"float"` type. |
| `default_vec3` | vec3 | (type-dependent) | Default value for `"vec3"` type. |
| `default_vec4` | vec4 | (type-dependent) | Default value for `"vec4"` type. |
| `min` | number | No (float only) | Minimum allowed value (float only). Clamps at set time. |
| `max` | number | No (float only) | Maximum allowed value (float only). Clamps at set time. |

**Validation:**
- Parameter names must be unique (no duplicates).
- Float parameters with min/max: enforces `min <= max`. Default clamped to range.
- Vec3/Vec4 parameters: no range constraints (used for colors, directions).

**Runtime Usage (Lua):**
```lua
entity = ctx.vfx:spawn("vfx/fire.uzvfx", position)
ctx.vfx:set_parameter(entity, "intensity", 2.5)
ctx.vfx:set_parameter(entity, "wind_dir", { 1.0, 0.5, 0.0 })
```

## Events

Events are named, triggered emissions that spawn additional particles with custom velocity scaling.

```json
{
  "events": [
    { "name": "burst", "emission_count": 100, "velocity_scale": 2.0 },
    { "name": "explode", "emission_count": 500, "velocity_scale": 5.0 }
  ]
}
```

| Field | Type | Required | Constraints | Description |
|-------|------|----------|-------------|-------------|
| `name` | string | Yes | Unique event identifier (e.g., `"burst"`, `"explode"`). Used in Lua: `emit_event(entity, name)`. |
| `emission_count` | integer | Yes | > 0 | Number of particles to spawn when event is triggered. |
| `velocity_scale` | number | Yes | > 0 | Multiplier applied to spawn velocity (e.g., 2.0 = double velocity). |

**Validation:**
- Event names must be unique (no duplicates).
- Both `emission_count` and `velocity_scale` must be > 0.

**Runtime Usage (Lua):**
```lua
-- Trigger "burst" event on entity
ctx.vfx:emit_event(entity, "burst")

-- Spawned particles use spawn module's velocity × velocity_scale
-- Example: spawn_velocity=[0,8,0], velocity_scale=2.0 → initial velocity=[0,16,0]
```

## Validation Rules

A valid `.uzvfx` file must satisfy:

1. **Format**: `format` field must be exactly `"uzvfx"`.
2. **Version**: `version` field must be `1` (current version).
3. **Required fields**: `name`, `emitter`, `modules` must be present.
4. **Emitter validation**:
   - `max_particles` > 0 and ≤ 65536.
   - `lifespan_default` > 0.0.
5. **Modules validation**:
   - At least one module in `modules` array.
   - Each module has `type` (valid type name) and `config` (object, may be empty).
6. **Parameter validation**:
   - All parameter names are unique (no duplicates).
   - Each parameter has `name`, `type`, and appropriate default field.
   - Float parameters with min/max satisfy `min <= max`.
7. **Event validation**:
   - All event names are unique (no duplicates).
   - Each event has `name`, `emission_count` > 0, and `velocity_scale` > 0.
8. **Vector field validation**: All `vec3` and `vec4` fields must have exactly 3 or 4 float components (no extras, no missing).

**Error Handling:**
The VFX asset loader logs errors with location (file path, field name) and validation context. Invalid files are rejected at load time; the effect fails to spawn.

## Example: Complete Fire Effect

```json
{
  "format": "uzvfx",
  "version": 1,
  "name": "fire_campfire",
  "emitter": {
    "max_particles": 5000,
    "lifespan_default": 3.0
  },
  "modules": [
    {
      "type": "spawn",
      "config": {
        "rate": 200,
        "spread_angle": 90,
        "spawn_velocity": [0, 2, 0],
        "lifespan_min": 2.0,
        "lifespan_max": 4.0,
        "spawn_shape": "Sphere",
        "spawn_radius": 0.3
      }
    },
    {
      "type": "update",
      "config": {
        "gravity": [0, 1.5, 0],
        "drag": 0.97,
        "angular_velocity": 45.0
      }
    },
    {
      "type": "turbulence",
      "config": {
        "strength": 0.8,
        "frequency": 0.5,
        "octaves": 2
      }
    },
    {
      "type": "sprite_render",
      "config": {
        "sprite_width": 0.4,
        "sprite_height": 0.4,
        "depth_sort": true
      }
    }
  ],
  "parameters": [
    {
      "name": "intensity",
      "type": "float",
      "default": 1.0,
      "min": 0.1,
      "max": 3.0
    },
    {
      "name": "flame_color",
      "type": "vec4",
      "default_vec4": [1.0, 0.5, 0.0, 1.0]
    }
  ],
  "events": [
    {
      "name": "flare",
      "emission_count": 150,
      "velocity_scale": 1.5
    }
  ]
}
```

## File Location & Naming

- **Path**: `.uzpak/vfx/*.uzvfx` (inside the asset package).
- **Naming**: Use lowercase with underscores (e.g., `fire_campfire.uzvfx`, `explosion_large.uzvfx`).
- **Size**: Typically 2–10 KB per file (JSON is verbose but human-readable).

## See Also

- [VFX System Overview](01-overview.md) — Architecture and asset types
- [VFX Modules](03-vfx-modules.md) — Module concepts and behavior
- [Module API](04-module-api.md) — C++ API for custom modules
- [Lua VFX API](09-lua-vfx-api.md) — Runtime control from Lua
- [Composite Format](08-uzvfx-composite-format.md) — Multi-emitter effects
