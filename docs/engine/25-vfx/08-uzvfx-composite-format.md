# .uzvfx_composite Format Specification

> **Status**: Implemented (Phase 3.62.0)
> **Format**: JSON
> **References**: [.uzvfx Format](07-uzvfx-format.md), [VFX Modules](03-vfx-modules.md)
> **Source**: `runtime/engine/vfx/loader/vfx_composite_loader.h/.cpp`, `runtime/engine/vfx/composite/vfx_composite.h/.cpp`

## Purpose

The `.uzvfx_composite` format describes a multi-emitter visual effect. A composite effect is a collection of individual `.uzvfx` graphs spawned with staggered timings and spatial offsets, allowing complex effects like explosions (fire + smoke + debris) or magic spells (core + wisps + sparks).

A composite is a lightweight orchestrator: it does not add new simulation logic, but instead composes existing single-emitter effects into a coordinated performance.

## Format Structure

A minimal `.uzvfx_composite` file:

```json
{
  "format": "uzvfx_composite",
  "version": 1,
  "name": "explosion_medium",
  "emitters": [
    { "name": "fire", "graph": "vfx/fire.uzvfx", "offset": [0, 0, 0], "delay": 0.0 },
    { "name": "smoke", "graph": "vfx/smoke.uzvfx", "offset": [0, 0.5, 0], "delay": 0.2 }
  ]
}
```

## Root Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `format` | string | Yes | Must be exactly `"uzvfx_composite"` (case-sensitive). Used to identify file type. |
| `version` | integer | Yes | Version of the `.uzvfx_composite` format. Current version is `1`. Future-proofs the format for breaking changes. |
| `name` | string | Yes | Asset name (e.g., `"explosion_medium"`, `"magic_spell_fire"`). Used for identification and debug logging. |
| `emitters` | array | Yes | Array of emitter references. At least one emitter must be present. |

## Emitters Array

Each emitter in the composite is a spawned instance of a `.uzvfx` graph. Emitters execute independently but are all spawned as part of the same composite effect.

```json
{
  "emitters": [
    {
      "name": "fire",
      "graph": "vfx/fire.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.0
    },
    {
      "name": "smoke",
      "graph": "vfx/smoke.uzvfx",
      "offset": [0, 0.5, 0],
      "delay": 0.2
    },
    {
      "name": "debris",
      "graph": "vfx/debris.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.0
    }
  ]
}
```

### Emitter Reference Fields

| Field | Type | Required | Constraints | Description |
|-------|------|----------|-------------|-------------|
| `name` | string | Yes | Unique per composite | Identifier for this emitter within the composite (e.g., `"fire"`, `"smoke_layer_1"`). Used for parameter/event targeting and debug output. |
| `graph` | string | Yes | Valid asset path | Path to a `.uzvfx` file relative to the asset root (e.g., `"vfx/fire.uzvfx"`). Loaded and instantiated when composite spawns. |
| `offset` | vec3 | Yes | — | Position offset relative to composite spawn position (world-space). Applied to all spawned particles. Example: `[0, 0.5, 0]` spawns particles 0.5 units above the composite center. |
| `delay` | number | Yes | ≥ 0.0 | Spawn delay in seconds. Emitter is not activated until this many seconds after composite spawn. Used for timing chains (e.g., explosion before smoke). |

**Validation:**
- All emitter names within a composite must be unique.
- All referenced `.uzvfx` files must exist and be valid.
- `offset` is a valid vec3 (3 float components).
- `delay` must be ≥ 0.0.

## Delayed Spawn Mechanism

Emitters with `delay > 0` are registered but not spawned immediately. The composite effect tracks elapsed time and spawns delayed emitters when their delay timer expires.

### Timing Example

For an explosion composite with fire (delay=0), smoke (delay=0.5), and debris (delay=0.1):

```
Time 0.0s:     fire spawns (delay=0), debris spawns (delay=0.1 elapsed)
Time 0.1s:     debris particles appear
Time 0.5s:     smoke spawns (delay=0.5 elapsed)
Time 0.5s:     smoke particles appear
```

### Use Cases

1. **Layered explosions**: Fire blooms immediately, then smoke expands after, then debris trails later.
2. **Magic effects**: Core spell effect spawns instantly, surrounding wisps follow after brief delay.
3. **Environmental effects**: Dust settles under wind effect, then secondary swirls follow.

## Parameters and Events (Forwarding)

Composites forward parameter and event calls to their child emitters. When you set a parameter on a composite or emit an event, it applies to all contained emitters (or a specific one by name).

### Parameter Forwarding

```lua
-- Lua: Set intensity on all fire/smoke emitters in composite
entity = ctx.vfx:spawn("vfx/explosion.uzvfx_composite", position)
ctx.vfx:set_parameter(entity, "intensity", 2.0)
-- If fire.uzvfx and smoke.uzvfx both have "intensity" parameter, both are updated.

-- Or target a specific emitter (if implemented):
-- ctx.vfx:set_parameter(entity, "fire.intensity", 2.0)
```

### Event Forwarding

```lua
-- Lua: Emit "burst" event on all emitters that support it
ctx.vfx:emit_event(entity, "burst")
-- If any child emitter has a "burst" event defined, it fires.
```

## Example: Complete Explosion Composite

```json
{
  "format": "uzvfx_composite",
  "version": 1,
  "name": "explosion_large",
  "emitters": [
    {
      "name": "core_flash",
      "graph": "vfx/explosion_core.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.0
    },
    {
      "name": "fire_ring",
      "graph": "vfx/explosion_fire.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.05
    },
    {
      "name": "smoke_plume",
      "graph": "vfx/explosion_smoke.uzvfx",
      "offset": [0, 0.5, 0],
      "delay": 0.3
    },
    {
      "name": "debris_field",
      "graph": "vfx/explosion_debris.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.1
    },
    {
      "name": "shockwave",
      "graph": "vfx/shockwave.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.0
    }
  ]
}
```

This composite creates a layered explosion:
- **core_flash** (t=0.0s): Yellow/white bright particles at center.
- **fire_ring** (t=0.05s): Fire spreads outward.
- **debris_field** (t=0.1s): Chunks are ejected.
- **smoke_plume** (t=0.3s): Large smoke cloud rises from center.
- **shockwave** (t=0.0s): Visible distortion effect.

## File Location & Naming

- **Path**: `.uzpak/vfx/*.uzvfx_composite` (inside the asset package).
- **Naming**: Use lowercase with underscores (e.g., `explosion_large.uzvfx_composite`, `magic_teleport.uzvfx_composite`).
- **Size**: Typically 0.5–2 KB per file (small orchestration metadata).

## Validation Rules

A valid `.uzvfx_composite` file must satisfy:

1. **Format**: `format` field must be exactly `"uzvfx_composite"`.
2. **Version**: `version` field must be `1` (current version).
3. **Required fields**: `name` and `emitters` must be present.
4. **Emitters validation**:
   - At least one emitter in `emitters` array.
   - All emitter names are unique (no duplicates).
   - Each emitter has `name`, `graph`, `offset`, and `delay` fields.
   - `graph` path is non-empty string (relative path to `.uzvfx`).
   - `offset` is a valid vec3 (3 float components).
   - `delay` is a number ≥ 0.0.
5. **Referential integrity**: All referenced `.uzvfx` files must exist and be valid (checked at composite load time).

**Error Handling:**
- If a referenced `.uzvfx` file is missing, composite fails to load and logs an error with the missing file path.
- If a `.uzvfx` file is invalid, composite fails to load.
- Invalid composite files are rejected at load time.

## Interoperability with Single-Emitter Effects

A `.uzvfx_composite` with a single emitter and zero delay is functionally equivalent to spawning the `.uzvfx` directly:

```json
{
  "format": "uzvfx_composite",
  "version": 1,
  "name": "wrapper",
  "emitters": [
    {
      "name": "effect",
      "graph": "vfx/fire.uzvfx",
      "offset": [0, 0, 0],
      "delay": 0.0
    }
  ]
}
```

However, using the `.uzvfx` directly is preferred for efficiency (single indirection vs. two).

## Practical Considerations

### Memory Usage

A composite does not add significant memory overhead:
- Orchestration data (names, timers, offset vectors): ~100 bytes per emitter reference.
- Example: 5-emitter explosion composite ≈ 500 bytes metadata + sum of child `.uzvfx` allocations.

### Performance

- All emitters in a composite update and render independently. No combining or optimization is done.
- A 5-emitter composite uses 5× the compute shaders and 5 separate indirect draw calls.
- Overhead is negligible compared to particle simulation cost.

### Best Practices

1. **Delay for visual impact**: Use delays to create layered effects (fire first, then smoke). Without delays, effects look simultaneous and flat.
2. **Offset for spatial composition**: Use offset to position sub-effects (smoke above explosion, debris scattered).
3. **Reuse single-emitter effects**: Create small, single-purpose `.uzvfx` files (fire, smoke, debris) and compose them. This is more maintainable than monolithic mega-effects.
4. **Limit emitter count**: Keep composites to 3–5 emitters. Beyond that, consider hierarchical composites (future extension).

## Future Extensions (Phase 3.0+)

Potential enhancements (not in current specification):

- **Hierarchical composites**: Composites containing other composites.
- **Emitter sequencing**: Specify that one emitter should start when another ends.
- **Parameter linking**: Couple parameters across emitters (e.g., "debris intensity" scales with "explosion intensity").
- **Physics coupling**: Debris ejection velocity tied to explosion force.

## See Also

- [.uzvfx Format](07-uzvfx-format.md) — Single-emitter graph format
- [VFX System Overview](01-overview.md) — Architecture and asset types
- [Lua VFX API](09-lua-vfx-api.md) — Runtime control from Lua (works with composites)
- [VFX Modules](03-vfx-modules.md) — Module concepts
