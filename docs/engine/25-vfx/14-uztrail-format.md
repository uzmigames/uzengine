# .uztrail JSON Format Specification

> **Status**: Implemented (Phase 3.64.0)
> **Format**: JSON
> **References**: [Trail Rendering Pipeline](15-trail-rendering-pipeline.md), [Trail Configuration Guide](16-trail-configuration-guide.md)
> **Source**: `runtime/engine/vfx/trails/trail_asset.{h,cpp}`

## Purpose

The `.uztrail` format is the serialized representation of a trail/ribbon VFX asset. It defines the complete visual behavior of a ribbon trail: which material to use, how the ribbon width tapers from head to tail, color gradient, texture scrolling speed, point lifetime, and sampling density. A single `.uztrail` file contains one trail definition that can be instantiated multiple times (e.g., sword swipe trails, projectile trails, spell effects).

Unlike particle systems which emit individual particles, trails are continuous ribbons that connect successive position samples of a moving emitter into a camera-facing quad strip.

## Format Structure

A minimal `.uztrail` file:

```json
{
  "format": "uztrail",
  "version": 1,
  "material": "materials/vfx/fire_trail.uzmat",
  "width_start": 0.5,
  "width_end": 0.05,
  "color_start": [1.0, 0.8, 0.2, 1.0],
  "color_end": [1.0, 0.2, 0.0, 0.0],
  "max_points": 100,
  "point_lifetime": 2.0,
  "min_point_distance": 0.1
}
```

A more complete `.uztrail` with all optional fields:

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "sword_trail_fire",
  "material": "materials/vfx/fire_trail.uzmat",
  "width_start": 0.6,
  "width_end": 0.02,
  "color_start": [1.0, 0.8, 0.2, 1.0],
  "color_end": [1.0, 0.2, 0.0, 0.0],
  "uv_scroll_rate": 2.0,
  "max_points": 128,
  "point_lifetime": 1.5,
  "min_point_distance": 0.08,
  "sort_order": 10,
  "ribbon_strands": 3,
  "strand_spacing": 0.06
}
```

## Root Fields

| Field | Type | Required | Default | Range | Description |
|-------|------|----------|---------|-------|-------------|
| `format` | string | Yes | — | — | Must be exactly `"uztrail"` (case-sensitive). Used to identify file type. |
| `version` | integer | Yes | — | — | Version of the `.uztrail` format. Current version is `1`. Future-proofs the format for breaking changes. |
| `name` | string | No | filename stem | — | Asset name (e.g., `"sword_trail_fire"`, `"missile_contrail"`). If omitted, derived from filename. Used for identification and debug logging. |
| `material` | string | Yes | — | — | Path to material asset (`.uzmat` file) containing the ribbon texture. Path is relative to `materials/` root. Example: `"materials/vfx/fire_trail.uzmat"`. Must not be empty. |
| `width_start` | number | No | 0.5 | > 0 | Cross-section width of the ribbon at the newest (head) point in world units (metres). Controls how wide the trail starts. |
| `width_end` | number | No | 0.05 | ≥ 0 | Cross-section width of the ribbon at the oldest (tail) point in metres. Controls how narrow the trail tapers. Can be 0 for a sharp point. |
| `color_start` | array (4 floats) | No | [1, 1, 1, 1] | [0, 1] | RGBA color at the head (newest) point. Values clamped to [0, 1] during loading. |
| `color_end` | array (4 floats) | No | [1, 1, 1, 0] | [0, 1] | RGBA color at the tail (oldest) point. Typically transparent (alpha ≈ 0) for natural fade-out. Values clamped to [0, 1] during loading. |
| `uv_scroll_rate` | number | No | 0.0 | — | Scrolling speed of the V texture coordinate along the trail length, in UV units per second. 0 = no scrolling. Positive values scroll toward the tail; negative values scroll toward the head. Typical range: [-4.0, 4.0]. |
| `max_points` | integer | No | 100 | > 0 | Maximum number of live trail points kept simultaneously. Controls ring buffer capacity. Larger values allow longer trails but consume more memory (each point ≈ 56 bytes). Typical range: 16–512. |
| `point_lifetime` | number | No | 2.0 | > 0 | Maximum lifetime of a single trail point in seconds. Points are removed from the tail once their age exceeds this value. Shorter lifetimes produce shorter, more transient trails. |
| `min_point_distance` | number | No | 0.1 | > 0 | Minimum world-space distance in metres the emitter must travel before a new point is emitted. Lower values produce smoother, denser ribbons but emit more points per second (performance cost). Values < 0.01 may cause degenerate zero-length segments and should be avoided. |
| `sort_order` | integer | No | 0 | — | Rendering sort priority. Higher values draw on top of lower values within the VFX render pass. Trails with equal sort order have undefined relative ordering. Typical range: -128 to 127. |
| `ribbon_strands` | integer | No | 1 | ≥ 1 | Number of parallel ribbon strands generated from the same trail points. Values > 1 produce N parallel quad-strip ribbons offset perpendicular to the camera-facing plane, approximating a tube or wing shape. Vertex count scales as `pointCount * 2 * ribbonStrands`. Keep low (2–4) for real-time use. |
| `strand_spacing` | number | No | 0.1 | ≥ 0 | World-space distance between adjacent ribbon strands in metres. Only meaningful when `ribbon_strands > 1`. A value of 0 collapses all strands into overlapping ribbons. |

## Validation Rules

When loading a `.uztrail` file, the engine enforces these rules:

### Required Fields
- `format` must be exactly `"uztrail"` (case-sensitive)
- `version` must be `1` (current)
- `material` must reference an existing `.uzmat` file and must not be empty

### Numeric Ranges
- `width_start` must be > 0
- `width_end` must be ≥ 0
- `max_points` must be > 0 (typical: 16–512)
- `point_lifetime` must be > 0
- `min_point_distance` must be > 0 (recommend ≥ 0.01)
- `color_start` and `color_end` RGBA components clamped to [0, 1] during loading
- All color components are expected in linear space (not sRGB)

### File References
- `material` path must exist and be a valid `.uzmat` file

### Load-Time Logging
- **Info**: Asset loaded with name, material, configuration summary
- **Warning**: material not found (trail cannot render, asset fails to load)
- **Error**: format != "uztrail", version != 1, or required fields missing (asset rejected)

## Example Trails

### Fire Trail (Sword/Slash Effect)

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "sword_trail_fire",
  "material": "materials/vfx/fire_trail.uzmat",
  "width_start": 0.6,
  "width_end": 0.02,
  "color_start": [1.0, 0.7, 0.2, 1.0],
  "color_end": [1.0, 0.3, 0.0, 0.0],
  "uv_scroll_rate": 3.0,
  "max_points": 80,
  "point_lifetime": 1.2,
  "min_point_distance": 0.08,
  "sort_order": 10
}
```

**Visual effect**: Bright orange head fading to red transparency, fast texture scroll creates flowing appearance, sharp point at tail.

### Smoke Trail (Projectile)

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "projectile_smoke_trail",
  "material": "materials/vfx/smoke_trail.uzmat",
  "width_start": 0.4,
  "width_end": 0.15,
  "color_start": [0.9, 0.9, 0.9, 0.8],
  "color_end": [0.7, 0.7, 0.7, 0.0],
  "uv_scroll_rate": 0.5,
  "max_points": 128,
  "point_lifetime": 3.0,
  "min_point_distance": 0.1,
  "sort_order": 5
}
```

**Visual effect**: Thick, semi-transparent white/gray ribbon that gradually fades. Slow scroll for gentle, drifting appearance. Longer lifetime creates longer trail.

### Ice Trail (Magic/Ability)

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "ice_shards_trail",
  "material": "materials/vfx/ice_trail.uzmat",
  "width_start": 0.3,
  "width_end": 0.0,
  "color_start": [0.3, 0.7, 1.0, 0.9],
  "color_end": [0.5, 0.9, 1.0, 0.0],
  "uv_scroll_rate": -1.5,
  "max_points": 64,
  "point_lifetime": 1.0,
  "min_point_distance": 0.12,
  "sort_order": 8
}
```

**Visual effect**: Cool blue-cyan color, sharp point (width_end = 0), negative scroll creates upstream texture flow, short lifetime for quick, snappy effect.

### Glowing Aura Trail (Persistent Effect)

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "aura_glow_trail",
  "material": "materials/vfx/additive_glow.uzmat",
  "width_start": 0.25,
  "width_end": 0.08,
  "color_start": [1.0, 1.0, 0.0, 0.6],
  "color_end": [1.0, 0.5, 0.0, 0.0],
  "uv_scroll_rate": 0.0,
  "max_points": 200,
  "point_lifetime": 5.0,
  "min_point_distance": 0.05,
  "sort_order": 15
}
```

**Visual effect**: Additive yellow-orange glow, no scroll (static texture), dense points (0.05m spacing), long lifetime creates smooth, persistent aura around moving object.

## C++ API

### Loading

```cpp
namespace uz {

/// Loads a TrailAsset from a .uztrail JSON file on disk.
///
/// Reads the file at `path`, parses JSON, validates all fields, and returns
/// the populated asset. The `sourcePath` field is set to `path`.
///
/// Returns nullptr if the file cannot be read, JSON is malformed, or
/// validation fails. Logs VFX::Error with path and reason on failure.
std::unique_ptr<TrailAsset> TrailAsset::Load(const std::string& path);

/// Loads a TrailAsset from a JSON string (no disk I/O).
///
/// Useful for testing and procedural trail definitions. The `sourcePath`
/// field will be empty. Returns nullptr if JSON is malformed or validation
/// fails. Logs VFX::Error with reason on failure.
std::unique_ptr<TrailAsset> TrailAsset::LoadFromString(const std::string& jsonStr);

}
```

### Asset Structure

```cpp
struct TrailAsset {
    std::string materialPath;        // Path to .uzmat material
    float widthStart = 0.5f;         // Head (newest) point width in metres
    float widthEnd = 0.05f;          // Tail (oldest) point width in metres
    Vec4 colorStart{1, 1, 1, 1};     // Head RGBA color (linear space)
    Vec4 colorEnd{1, 1, 1, 0};       // Tail RGBA color (linear space)
    float uvScrollRate = 0.0f;       // UV V scroll speed (units/second)
    uint32_t maxPoints = 100;        // Ring buffer capacity
    float pointLifetime = 2.0f;      // Max point age in seconds
    float minPointDistance = 0.1f;   // Minimum emission distance in metres
    int sortOrder = 0;               // Rendering priority
    std::string sourcePath;          // Path asset was loaded from (empty if from string)
};
```

## Lua API

```lua
-- Load trail asset from .uztrail file
local trail_asset = uz.asset.load_trail("vfx/trails/sword_fire.uztrail")

-- Create trail component on an entity
-- The component will start empty; points are added each frame via Tick()
local trail = uz.vfx.create_trail(trail_asset)

-- Spawn trail in the world (registers with TrailSystem, returns handle)
local trail_handle = uz.vfx.spawn_trail(trail, initial_position)

-- Update trail each frame (called automatically by engine during update phase)
-- Position is fetched from the entity's transform
uz.vfx.update_trail(trail_handle, entity_position, camera_position)

-- Get active point count
local point_count = uz.vfx.get_trail_point_count(trail_handle)

-- Get total trail length in metres
local trail_length = uz.vfx.get_trail_length(trail_handle)

-- Clear trail (remove all points, keeps component alive)
uz.vfx.clear_trail(trail_handle)

-- Remove trail from world
uz.vfx.destroy_trail(trail_handle)
```

## Performance Considerations

### Memory Usage

Each trail point occupies approximately 56 bytes (position 12 + direction 12 + width 4 + color 16 + age 4 + distance 4 + padding 4).

```
Memory per trail = max_points * 56 bytes + mesh data
  - 100 points = ~5.6 KB + ~48 KB mesh = ~53 KB
  - 256 points = ~14 KB + ~122 KB mesh = ~136 KB
  - 512 points = ~28 KB + ~245 KB mesh = ~273 KB
```

Reduce `max_points` to decrease memory footprint. Longer `point_lifetime` does not increase memory — it allows the ring buffer to stay full longer.

### GPU Cost

Trail rendering cost depends on geometry size:
- Vertex count = `activePoints * 2` (one left + right vertex per point)
- Index count = `(activePoints - 1) * 6` (2 triangles × 3 indices per segment)

At 100 active points per trail: ~200 vertices, ~600 indices per DrawCall.

Reduce `min_point_distance` to increase point density (more vertices, more memory, higher GPU cost).
Increase `min_point_distance` to decrease point density (fewer vertices, lower cost, choppier appearance).

Recommended balance: `min_point_distance = 0.08–0.15` for most trails.

### Point Emission Rate

Points are emitted when the emitter moves ≥ `min_point_distance` metres from the last emitted point.

```
points_per_second ≈ (movement_speed_m/s) / min_point_distance
  - 10 m/s with 0.1m spacing = 100 points/second
  - 10 m/s with 0.05m spacing = 200 points/second
```

High speed + small spacing = explosive point growth. Use `point_lifetime` to cap active point count.

## See Also

- [Trail Rendering Pipeline](15-trail-rendering-pipeline.md) — Geometry generation and GPU rendering
- [Trail Configuration Guide](16-trail-configuration-guide.md) — Practical tuning and usage examples
- [VFX System Overview](01-overview.md) — VFX module architecture
- [Material Format](../guides/assets/material-format.md) — .uzmat material specification
