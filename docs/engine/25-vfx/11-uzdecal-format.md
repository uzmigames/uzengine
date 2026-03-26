# .uzdecal JSON Format Specification

> **Status**: Implemented (Phase 3.63.0)
> **Format**: JSON
> **References**: [Decal System Overview](05-decals.md), [Deferred Decal Pipeline](12-deferred-decal-pipeline.md), [Decal Pool & Atlas](13-decal-pool-atlas.md)
> **Source**: `runtime/engine/vfx/decals/decal_asset.{h,cpp}`

## Purpose

The `.uzdecal` format is the serialized representation of a decal asset. It defines a complete decal: its material, projection properties, blend mode, lifetime, and fade behavior. A single `.uzdecal` file contains one decal definition that can be instantiated multiple times in a scene (e.g., the same bullet hole decal repeated across many surfaces).

Unlike `.uzvfx` (particle effects) which are time-based, decals are spatial: they project a texture onto surfaces using a box-shaped projection volume.

## Format Structure

A minimal `.uzdecal` file:

```json
{
  "format": "uzdecal",
  "version": 1,
  "material": "materials/bullet_hole.uzmat",
  "blend_mode": "Opaque",
  "lifetime": 10.0,
  "fade_duration": 2.0,
  "normal_intensity": 0.8,
  "roughness_override": -1.0,
  "sort_order": 0,
  "half_extents": [0.5, 0.5, 0.2]
}
```

A more complete `.uzdecal` with optional fields:

```json
{
  "format": "uzdecal",
  "version": 1,
  "name": "bullet_hole_9mm",
  "material": "materials/bullet_hole.uzmat",
  "blend_mode": "Opaque",
  "lifetime": 10.0,
  "fade_duration": 2.0,
  "fade_curve": "curves/fade_linear.uzcurve",
  "normal_intensity": 0.8,
  "roughness_override": -1.0,
  "sort_order": 100,
  "half_extents": [0.5, 0.5, 0.2],
  "max_instances": 500,
  "backface_rejection_threshold": 0.1,
  "depth_fade": {
    "enabled": true,
    "fade_range": [0.1, 2.0]
  }
}
```

## Root Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `format` | string | Yes | — | Must be exactly `"uzdecal"` (case-sensitive). Used to identify file type. |
| `version` | integer | Yes | — | Version of the `.uzdecal` format. Current version is `1`. Future-proofs the format for breaking changes. |
| `name` | string | No | filename stem | Asset name (e.g., `"bullet_hole_9mm"`, `"blood_splatter"`). If omitted, derived from filename. Used for identification and debug logging. |
| `material` | string | Yes | — | Path to material asset (`.uzmat` file) containing albedo, normal, and roughness textures. Path is relative to `materials/` root. Example: `"materials/bullet_hole.uzmat"`. |
| `blend_mode` | string | Yes | — | How to blend decal with surface. Valid values: `"Opaque"`, `"Additive"`, `"Multiply"`, `"Screen"`. See [Blend Modes](#blend-modes) section. |
| `lifetime` | number | No | 10.0 | Decal lifetime in seconds before auto-fade begins. Must be > 0. Set to large value (e.g., 999999) for persistent decals. |
| `fade_duration` | number | No | 2.0 | Duration of fade-out animation in seconds. Must be ≥ 0. If 0, decal snaps to invisible at end of lifetime. |
| `fade_curve` | string | No | linear | Path to curve asset (`.uzcurve` file) defining opacity over time. If omitted, uses linear fade. Values must be in range [0, 1]. |
| `normal_intensity` | number | No | 0.8 | How strongly the decal's normal map affects the surface normal. Range: 0–1. 0 = decal normal ignored, 1 = decal normal dominates. |
| `roughness_override` | number | No | -1.0 | Override surface roughness with decal roughness. Range: -1 to 1. If -1, surface roughness is preserved. If 0–1, decal's roughness texture is sampled and applied. |
| `sort_order` | integer | No | 0 | Rendering order within the decal pass. Higher values render later (on top). Range: -128 to 127. Useful for layering decals. |
| `half_extents` | vec3 | Yes | — | Size of projection box (width, height, depth) in meters. [0.5, 0.5, 0.2] means ±0.5m in X/Y, ±0.2m in Z. All values must be > 0. |
| `max_instances` | integer | No | 500 | Maximum number of instances of this decal that can be active simultaneously. Used for pool pre-allocation. Must be > 0, ≤ 10000. |
| `backface_rejection_threshold` | number | No | 0.1 | Minimum dot product between surface normal and decal forward direction to accept projection. Range: -1 to 1. 0.1 (default) rejects surfaces nearly perpendicular to decal. Use 0 for all-facing, -1 for two-sided. |
| `depth_fade` | object | No | disabled | Optional depth-based fade (see [Depth Fade](#depth-fade) section). |

## Blend Modes

Blend modes determine how the decal's color interacts with the surface's color:

| Mode | Enum Value | Effect | Use Case |
|------|-----------|--------|----------|
| `Opaque` | 0 | Decal color replaces surface color entirely. `out = decal_color` | Bullet holes, visible damage, graffiti |
| `Additive` | 1 | Adds decal color to surface. `out = surface_color + decal_color` (clamped to [0,1]) | Glowing marks, light scatters, luminous paint |
| `Multiply` | 2 | Darkens surface by decal. `out = surface_color * decal_color` | Dirt, mud, scorch marks, soot |
| `Screen` | 3 | Inverse multiply (brightens). `out = 1 - (1 - surface_color) * (1 - decal_color)` | Bleach stains, bleached areas, light streaks |

**Blending Formula** (applied per-channel, pre-fade):
```
// All modes apply normal map and roughness override identically.
// Only color blending differs per mode.
final_color = blend_mode_func(surface_color, decal_color)
final_color = lerp(surface_color, final_color, fade_alpha)
```

## Fade Curve

The `fade_curve` field references a `.uzcurve` asset that defines opacity over the decal's lifetime.

**Curve Requirements:**
- Must be a 1D curve (single output channel)
- Input domain: normalized lifetime [0, 1] where 0 = spawn time, 1 = end of lifetime
- Output range: [0, 1] representing opacity (0 = transparent, 1 = opaque)
- If omitted, uses linear fade: `opacity(t) = 1 - (t / lifetime)` for first `fade_duration` seconds, then 0

**Example curve (manual):**
```json
{
  "format": "uzcurve",
  "version": 1,
  "name": "decal_fade_linear",
  "input_type": "float",
  "output_type": "float",
  "keyframes": [
    { "time": 0.0, "value": 1.0, "tangent": -1.0 },
    { "time": 1.0, "value": 0.0, "tangent": -1.0 }
  ]
}
```

**Common fade patterns:**
- **Instant fade**: Single keyframe at (0, 1), then drop to (1, 0) at frame boundary
- **Linear fade**: (0, 1) → (1, 0) with constant slope
- **Ease-out fade**: (0, 1) → (1, 0) with decreasing slope (e.g., quadratic ease)
- **Hold then fade**: (0, 1) plateau → (0.7, 1) → (1, 0) sharp drop

## Depth Fade

Optional depth-based fading: decal fades as surfaces get farther from the decal's depth plane.

```json
{
  "depth_fade": {
    "enabled": true,
    "fade_range": [0.1, 2.0]
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | boolean | false | If true, depth-based fading is active. |
| `fade_range` | vec2 | [0.1, 2.0] | [near_distance, far_distance] in meters. Decal opaque at near_distance, fully transparent at far_distance. Linear interpolation between. |

**Depth Fade Formula:**
```hlsl
float surface_depth = length(world_pos - decal_center);
float depth_alpha = 1.0 - saturate((surface_depth - fade_range.x) / (fade_range.y - fade_range.x));
final_alpha *= depth_alpha;
```

## Validation Rules

When loading a `.uzdecal` file, the engine enforces these rules:

### Required Fields
- `format` must be `"uzdecal"`
- `version` must be `1` (current)
- `material` must reference an existing `.uzmat` file
- `blend_mode` must be one of: `"Opaque"`, `"Additive"`, `"Multiply"`, `"Screen"` (case-sensitive)
- `half_extents` must have all components > 0

### Numeric Ranges
- `lifetime` > 0
- `fade_duration` ≥ 0
- `normal_intensity` ∈ [0, 1]
- `roughness_override` ∈ [-1, 1]
- `sort_order` ∈ [-128, 127]
- `max_instances` ∈ [1, 10000]
- `backface_rejection_threshold` ∈ [-1, 1]
- `depth_fade.fade_range`: fade_range[0] < fade_range[1], both > 0

### File References
- `material` path must exist and be a valid `.uzmat` file
- `fade_curve` path (if present) must exist and be a valid `.uzcurve` file

### Load-Time Logging
- **Info**: Asset loaded with name, material, blend mode
- **Warning**: fade_curve referenced but not found (falls back to linear)
- **Warning**: material not found (decal cannot render, asset fails to load)
- **Error**: format != "uzdecal" or version != 1 (asset rejected)

## Example Decals

### Bullet Hole (Opaque)

```json
{
  "format": "uzdecal",
  "version": 1,
  "name": "bullet_hole_9mm",
  "material": "materials/bullet_holes/9mm.uzmat",
  "blend_mode": "Opaque",
  "lifetime": 30.0,
  "fade_duration": 3.0,
  "normal_intensity": 0.5,
  "roughness_override": 0.6,
  "sort_order": 50,
  "half_extents": [0.15, 0.15, 0.1],
  "max_instances": 1000,
  "backface_rejection_threshold": 0.1
}
```

### Blood Splatter (Multiply)

```json
{
  "format": "uzdecal",
  "version": 1,
  "name": "blood_splatter",
  "material": "materials/decals/blood.uzmat",
  "blend_mode": "Multiply",
  "lifetime": 60.0,
  "fade_duration": 5.0,
  "fade_curve": "curves/blood_fade_easeout.uzcurve",
  "normal_intensity": 0.2,
  "roughness_override": -1.0,
  "sort_order": 0,
  "half_extents": [1.5, 1.5, 0.3],
  "max_instances": 200,
  "backface_rejection_threshold": 0.0,
  "depth_fade": {
    "enabled": true,
    "fade_range": [0.2, 3.0]
  }
}
```

### Scorch Mark (Additive)

```json
{
  "format": "uzdecal",
  "version": 1,
  "name": "scorch_mark",
  "material": "materials/decals/scorch.uzmat",
  "blend_mode": "Additive",
  "lifetime": 999999,
  "fade_duration": 0.0,
  "normal_intensity": 0.0,
  "roughness_override": 0.8,
  "sort_order": 10,
  "half_extents": [2.0, 2.0, 0.5],
  "max_instances": 500
}
```

### Screen Bleach (Bright spots)

```json
{
  "format": "uzdecal",
  "version": 1,
  "name": "bleach_splash",
  "material": "materials/decals/bleach.uzmat",
  "blend_mode": "Screen",
  "lifetime": 15.0,
  "fade_duration": 2.0,
  "normal_intensity": 0.1,
  "roughness_override": 0.3,
  "sort_order": 5,
  "half_extents": [0.8, 0.8, 0.2],
  "max_instances": 300
}
```

## Loading & Parsing

**C++ API:**

```cpp
namespace uz::VFX {
    /// Loads a .uzdecal file and returns a DecalAsset.
    /// Returns nullptr if file is invalid or references are broken.
    /// Logs Info on success, Warning on missing fade_curve, Error on format/file errors.
    DecalAsset* LoadDecalAsset(const std::string& path);
}
```

**Lua API:**

```lua
-- Load decal asset from .uzdecal file
local decal_asset = uz.asset.load_decal("decals/bullet_hole.uzdecal")

-- Spawn decal instance
local decal = uz.scene.spawn_decal(decal_asset, position, rotation)

-- Set lifetime override (in seconds)
uz.decal.set_lifetime(decal, 20.0)

-- Remove decal immediately
uz.decal.destroy(decal)
```

## See Also

- [Decal System Overview](05-decals.md) — Conceptual overview and usage
- [Deferred Decal Pipeline](12-deferred-decal-pipeline.md) — Rendering implementation
- [Decal Pool & Atlas](13-decal-pool-atlas.md) — Performance optimization
- [.uzcurve Format](05-uzcurve-format.md) — Fade curve specification
- [Material Format](../../../guides/assets/material-format.md) — .uzmat material specification
