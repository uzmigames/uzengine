# Trail Configuration & Usage Guide

> **Status**: Implemented (Phase 3.64.0)
> **References**: [.uztrail Format](14-uztrail-format.md), [Trail Rendering Pipeline](15-trail-rendering-pipeline.md)
> **Source**: `runtime/engine/vfx/trails/trail_component.{h,cpp}`

## Purpose

This guide helps you configure and use the trail/ribbon VFX system. It covers parameter tuning with visual examples, common patterns, performance optimization, and best practices for smooth, responsive trails.

## Core Parameters

### `width_start` & `width_end` — Ribbon Shape

Controls the cross-section profile of the ribbon along its length.

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| `width_start` | 0.1–2.0 | 0.5 | Head (newest point) width in metres |
| `width_end` | 0.0–1.0 | 0.05 | Tail (oldest point) width in metres |

The ribbon width is **linearly interpolated** along the trail from head to tail.

#### Visual Examples

```
Scenario 1: Sharp Point (width_end = 0.0)
    Head: 0.6m wide
    │░░░░░░░░░░│  Newest point
    │░░░░░░░░│
    │░░░░░░░│
    │░░░░░│
    │░░░│
    └─ Oldest point (invisible)

    Effect: Sword slash, magic bolt trajectory
    Use: Melee attacks, projectile trails


Scenario 2: Rounded Taper (width_end = 0.2)
    Head: 0.6m wide
    │░░░░░░░░░░│  Newest point
    │░░░░░░░░│
    │░░░░░░│
    │░░░░░│
    │░░░░│
    │░░░│  Tail: 0.2m wide

    Effect: Smoke, aura, gradual fade
    Use: Environmental effects, persistent halos


Scenario 3: Full Width (width_end = width_start = 0.5)
    Head: 0.5m wide
    │░░░░░░░░░░│  Newest point
    │░░░░░░░░░░│
    │░░░░░░░░░░│
    │░░░░░░░░░░│
    │░░░░░░░░░░│
    │░░░░░░░░░░│  Tail: 0.5m wide

    Effect: Cylinder-shaped tube
    Use: Light beams, force fields (if additively blended)
```

#### Configuration Tips

- **Sword slash**: `width_start=0.6, width_end=0.0` → sharp point
- **Smoke trail**: `width_start=0.4, width_end=0.15` → rounded, full-length ribbon
- **Projectile trail**: `width_start=0.3, width_end=0.05` → tapered to point
- **Light beam**: `width_start=0.5, width_end=0.5` → uniform width

### `color_start` & `color_end` — Gradient & Fade

Controls the color and transparency taper along the trail.

| Parameter | Type | Default | Range |
|-----------|------|---------|-------|
| `color_start` | RGBA | [1, 1, 1, 1] | [0, 1] per channel |
| `color_end` | RGBA | [1, 1, 1, 0] | [0, 1] per channel |

Colors are **linearly interpolated** along the trail from head to tail. **Alpha channel** is critical for fade-out.

#### Visual Examples

##### Fire Trail

```
color_start: [1.0, 0.8, 0.2, 1.0]  ← Bright orange, opaque
color_end:   [1.0, 0.2, 0.0, 0.0]  ← Dark red, fully transparent

Visual result:
    Head: Bright orange/yellow ░
    │                         ░
    │                        ░
    │ Intermediate: Orange ░
    │                      ░
    │ Tail: Red/transparent
    │                  ░
    └─ Nearly invisible
```

**Use case**: Fire, magic projectiles, hot effects

##### Ice Trail

```
color_start: [0.3, 0.7, 1.0, 0.9]  ← Cyan-blue, mostly opaque
color_end:   [0.5, 0.9, 1.0, 0.0]  ← Light cyan, fully transparent

Visual result:
    Head: Deep blue ░
    │             ░
    │ Intermediate: Cyan ░
    │               ░
    │ Tail: Light blue, fading
    │         ░
    └─ Invisible
```

**Use case**: Ice, frost, cold magic

##### Smoke Trail

```
color_start: [0.9, 0.9, 0.9, 0.8]  ← Light gray, mostly opaque
color_end:   [0.7, 0.7, 0.7, 0.0]  ← Dark gray, fully transparent

Visual result:
    Head: Light gray ░
    │               ░
    │ Intermediate: Gray ░
    │              ░
    │ Tail: Dark gray, fading
    │        ░
    └─ Invisible
```

**Use case**: Smoke, dust, exhaust

#### Configuration Tips

**Important**: Set `color_end.alpha = 0` for natural fade-out (or close to 0, e.g., 0.1).

```
Good practices:
- color_start.a = 0.8–1.0 (opaque)
- color_end.a = 0.0–0.1 (transparent)

Anti-pattern:
- color_end.a = 0.5+ → trail stays visible at tail, abrupt cutoff
```

**Color matching by effect type**:

| Effect | color_start | color_end |
|--------|-------------|-----------|
| Fire | [1, 0.8, 0.2, 1] | [1, 0.2, 0, 0] |
| Ice | [0.3, 0.7, 1, 0.9] | [0.5, 0.9, 1, 0] |
| Lightning | [1, 1, 0.3, 1] | [1, 1, 0.8, 0] |
| Smoke | [0.9, 0.9, 0.9, 0.8] | [0.7, 0.7, 0.7, 0] |
| Blood | [0.8, 0.1, 0.1, 0.9] | [0.5, 0, 0, 0] |
| Energy | [0, 1, 0.8, 1] | [0.3, 1, 0.9, 0] |

### `uv_scroll_rate` — Animated Texture Flow

Controls how fast the trail's texture scrolls along its length.

| Parameter | Unit | Default | Typical Range |
|-----------|------|---------|---------------|
| `uv_scroll_rate` | UV units/sec | 0.0 | -4.0 to 4.0 |

Positive values scroll texture toward the tail (downstream flow).
Negative values scroll toward the head (upstream flow).
Zero disables scrolling (static texture).

#### Visual Examples

```
Scenario 1: No Scroll (uv_scroll_rate = 0.0)
    Frame 0:  [████████████]  Texture static
    Frame 1:  [████████████]  Same pattern
    Frame 2:  [████████████]  No motion

    Effect: Static ribbon with fixed texture alignment
    Use: Static trails, glow auras


Scenario 2: Downstream Scroll (uv_scroll_rate = 2.0)
    Frame 0:  [████████████]  Pattern at head
    Frame 1:  [███████████ ]  Pattern moves toward tail
    Frame 2:  [██████████  ]  Pattern continues downstream

    Effect: Texture flows from head → tail, suggests forward motion
    Use: Fire trails (flames flowing backward), projectile contrails


Scenario 3: Upstream Scroll (uv_scroll_rate = -1.5)
    Frame 0:  [████████████]  Pattern at head
    Frame 1:  [ ███████████]  Pattern moves toward head
    Frame 2:  [  ██████████]  Pattern continues upstream

    Effect: Texture flows from tail → head, suggests backward motion
    Use: Ice shards (reverse flow), swipe effects (pulling energy)
```

#### Configuration Tips

**Texture matching**:
- If your trail texture is a repeating **animated sequence** (stripes, waves), use non-zero scroll
- If your trail texture is **gradient-based** (feathering, soft fade), scroll is optional

**Common settings**:

| Effect | uv_scroll_rate | Reason |
|--------|-----------------|--------|
| Fire | 3.0–4.0 | Fast flames flowing downstream |
| Smoke | 0.5–1.0 | Slow, gentle drift |
| Ice | -1.5 | Upstream for "reverse energy" feel |
| Magic bolt | 2.0–3.0 | Medium speed, forward motion |
| Persistent aura | 0.0 | Static glow, no animation |

### `max_points` — Trail Length & Memory

Controls the maximum number of live trail points stored in the ring buffer.

| Parameter | Type | Default | Typical Range |
|-----------|------|---------|---|
| `max_points` | integer | 100 | 16–512 |

More points = longer trail, smoother shape, higher memory & GPU cost.

#### Memory Usage

Each trail point occupies **56 bytes** (Vec3 position 12 + Vec3 direction 12 + float width 4 + Vec4 color 16 + float age 4 + float distance 4 + padding 4).

```
Memory per trail:
  16 points  ≈ 0.9 KB
  64 points  ≈ 3.5 KB
  128 points ≈ 7.0 KB
  256 points ≈ 14 KB
  512 points ≈ 28 KB
```

For 100 concurrent trails with avg 64 points:
```
100 trails × 64 points × 56 bytes = 358 KB  (negligible)
```

#### Geometry & GPU Cost

Geometry size depends on **active points**, not capacity.

```
N active points → 2N vertices → (N-1) segments → 6(N-1) indices

Cost example (per trail):
  16 points → 32 vertices, 90 indices (180 triangles)
  64 points → 128 vertices, 378 indices (126 triangles)
  128 points → 256 vertices, 762 indices (254 triangles)
```

Typical DrawCall: ~200 vertices, ~600 indices (cost: <0.1 ms per trail on modern GPUs).

#### Configuration Tips

Balance trail length vs. performance:

| Trail Type | max_points | Typical Active | Reason |
|-----------|-------------|---|---|
| Sword slash | 64–128 | 30–60 | Short-lived, responsive |
| Projectile | 100–150 | 40–80 | Medium length |
| Movement trace | 200–256 | 100–150 | Longer trail |
| Persistent aura | 200–256 | 150+ | Always visible |
| Environmental | 128–256 | 60–120 | Aesthetic balance |

**Memory vs. visual quality**:
- If trails look "choppy" (visible segments), increase `max_points`
- If you have many trails and memory is tight, reduce `max_points`

### `point_lifetime` — Trail Duration

Controls how long each trail point lives before being culled.

| Parameter | Unit | Default | Range |
|-----------|------|---------|-------|
| `point_lifetime` | seconds | 2.0 | 0.1–10.0 |

A point is **removed** once its age exceeds `point_lifetime`. Longer lifetime = longer trail (independent of speed).

#### Visual Examples

```
Scenario 1: Short Lifetime (point_lifetime = 0.5s)
    At 10 m/s with 0.1m spacing (100 points/sec):
    Active points = lifetime × rate = 0.5 × 100 = 50 points

    Trail length ≈ 50 × 0.1m = 5 metres

    Effect: Short, quick trail. Responsive to emitter changes.
    Use: Fast-moving projectiles, quick melee swipes


Scenario 2: Medium Lifetime (point_lifetime = 2.0s)
    At 10 m/s with 0.1m spacing (100 points/sec):
    Active points = 2.0 × 100 = 200 points (clamped to max_points)

    Trail length ≈ 20 metres (if max_points = 200)

    Effect: Medium trail. Balanced visibility and performance.
    Use: General-purpose trails, combat effects


Scenario 3: Long Lifetime (point_lifetime = 5.0s)
    At 10 m/s with 0.1m spacing:
    Desired points = 5.0 × 100 = 500 points

    Trail length ≈ 50 metres (if max_points allows)

    Effect: Long, persistent trail. Shows full path history.
    Use: Slow-moving effects, environmental traces
```

#### Configuration Tips

**Don't confuse `point_lifetime` with `fade_duration`** (there is no built-in fade animation in the current trail system; fade is driven by `color_end.alpha`).

**Trail length formula**:
```
Approx trail_length = min(max_points, point_lifetime × emission_rate) × min_point_distance

emission_rate ≈ emitter_speed / min_point_distance
```

**Quick tuning**:

| Effect | point_lifetime | Reasoning |
|--------|---|---|
| Sword slash (quick) | 0.5–1.0 | Disappears quickly after swing |
| Magic projectile | 1.0–2.0 | Medium trail, balanced |
| Movement trace | 2.0–3.0 | Shows path history |
| Persistent aura | 3.0–5.0+ | Always visible around moving object |

### `min_point_distance` — Emission Spacing

Controls the minimum distance the emitter must travel before a new point is emitted.

| Parameter | Unit | Default | Range |
|-----------|------|---------|-------|
| `min_point_distance` | metres | 0.1 | 0.01–0.5 |

Smaller distance = more points emitted per second = smoother trail, higher cost.
Larger distance = fewer points = choppier trail, lower cost.

#### Visual Examples

```
Scenario 1: Dense Spacing (min_point_distance = 0.05)
    At 10 m/s: 10 / 0.05 = 200 points/second

    With point_lifetime = 2.0s: ~400 active points (clamped to max_points)

    Effect: Very smooth ribbon, fine detail in curves
    Use: High-quality visuals, where detail matters


Scenario 2: Moderate Spacing (min_point_distance = 0.1)
    At 10 m/s: 10 / 0.1 = 100 points/second

    With point_lifetime = 2.0s: ~200 active points

    Effect: Balanced smoothness and performance
    Use: General-purpose trails


Scenario 3: Coarse Spacing (min_point_distance = 0.25)
    At 10 m/s: 10 / 0.25 = 40 points/second

    With point_lifetime = 2.0s: ~80 active points

    Effect: Visible segments, choppier appearance
    Use: Performance-critical scenarios, low-detail trails
```

#### Configuration Tips

**Balance smoothness vs. performance**:

| Scenario | min_point_distance | Effect |
|----------|---|---|
| Close camera + high detail | 0.05–0.08 | Very smooth, higher cost |
| General purpose | 0.1–0.15 | Balanced smoothness & cost |
| High-speed objects | 0.15–0.2 | Coarser but still acceptable |
| Performance-critical | 0.25–0.5 | Visible segments, very cheap |

**Avoid values < 0.01**: Very small spacing can cause **degenerate zero-length segments** and numerical instability.

**Rule of thumb**: Set `min_point_distance` to 1/10th of the ribbon width for smooth appearance.

```cpp
// Example auto-tuning
min_point_distance = width_start / 10.0f;  // Adjust for smoothness
```

## Common Patterns

### Pattern 1: Sword/Melee Slash

Visual: Quick, sharp trail that disappears immediately after swipe.

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "sword_slash",
  "material": "materials/vfx/sword_trail.uzmat",
  "width_start": 0.6,
  "width_end": 0.0,
  "color_start": [1.0, 0.9, 0.5, 1.0],
  "color_end": [1.0, 0.5, 0.0, 0.0],
  "uv_scroll_rate": 2.0,
  "max_points": 80,
  "point_lifetime": 0.8,
  "min_point_distance": 0.08,
  "sort_order": 10
}
```

**Key traits**:
- `width_end = 0` → sharp point
- `point_lifetime = 0.8s` → short-lived
- `color_end.a = 0` → fades out
- Moderate scroll for flowing effect

### Pattern 2: Projectile Contrail

Visual: Medium-length tail following projectile, medium speed scroll.

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "fireball_contrail",
  "material": "materials/vfx/fire_trail.uzmat",
  "width_start": 0.4,
  "width_end": 0.08,
  "color_start": [1.0, 0.8, 0.1, 0.9],
  "color_end": [1.0, 0.3, 0.0, 0.0],
  "uv_scroll_rate": 1.5,
  "max_points": 128,
  "point_lifetime": 2.0,
  "min_point_distance": 0.12,
  "sort_order": 5
}
```

**Key traits**:
- `width_end > 0` → rounded taper
- `point_lifetime = 2.0s` → medium trail
- `min_point_distance = 0.12` → slightly coarser for performance
- Moderate scroll

### Pattern 3: Movement Trace

Visual: Long trail showing character's movement path, used for ghost/afterimage effects.

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "movement_trace",
  "material": "materials/vfx/movement_trail.uzmat",
  "width_start": 0.2,
  "width_end": 0.1,
  "color_start": [0.5, 0.8, 1.0, 0.5],
  "color_end": [0.5, 0.8, 1.0, 0.0],
  "uv_scroll_rate": 0.0,
  "max_points": 256,
  "point_lifetime": 4.0,
  "min_point_distance": 0.05,
  "sort_order": 2
}
```

**Key traits**:
- `max_points = 256` → long trail (up to 12+ meters at 10 m/s)
- `point_lifetime = 4.0s` → persistent
- `uv_scroll_rate = 0` → static texture
- Dense spacing (`min_point_distance = 0.05`)

### Pattern 4: Persistent Aura

Visual: Constant glow around moving object, low cost.

```json
{
  "format": "uztrail",
  "version": 1,
  "name": "magic_aura",
  "material": "materials/vfx/additive_glow.uzmat",
  "width_start": 0.3,
  "width_end": 0.15,
  "color_start": [0.8, 0.4, 1.0, 0.4],
  "color_end": [0.8, 0.4, 1.0, 0.0],
  "uv_scroll_rate": 0.0,
  "max_points": 80,
  "point_lifetime": 1.0,
  "min_point_distance": 0.15,
  "sort_order": 1
}
```

**Key traits**:
- `width_end > 0` → full-length ribbon
- `point_lifetime = 1.0s` → short, but density creates continuous effect
- `min_point_distance = 0.15` → coarse, very cheap
- Static texture (no scroll)

## TrailComponent C++ API

### Initialization

```cpp
// Create with default config
uz::TrailComponent trail;

// Configure before first Tick()
trail.materialPath = "materials/vfx/sword_fire.uzmat";
trail.maxTrailPoints = 100;
trail.widthStart = 0.6f;
trail.widthEnd = 0.02f;
trail.colorStart = uz::Vec4(1, 0.8, 0.2, 1);
trail.colorEnd = uz::Vec4(1, 0.2, 0, 0);
trail.pointLifetime = 1.5f;
trail.minPointDistance = 0.1f;
trail.uv ScrollRate = 2.0f;
trail.isActive = true;
```

### Per-Frame Update

```cpp
// In your update loop (e.g., MonoBehaviour-style)
void Update(float deltaTime) {
    uz::Vec3 currentPos = entity.GetWorldPosition();
    trail.Tick(deltaTime, currentPos);

    // Query trail state for debugging or logic
    uint32_t pointCount = trail.GetActivePointCount();
    float trailLength = trail.GetTotalLength();
    uz::Vec4 colorAt50Percent = trail.GetColorAtNormalized(0.5f);
}
```

### Reset

```cpp
// Clear all points (e.g., on teleport)
trail.Clear();

// Or deactivate temporarily
trail.isActive = false;
trail.Clear();
trail.isActive = true;
```

### TrailSystem Integration

```cpp
// Register trail in the system (returns handle)
uz::TrailSystem::TrailHandle handle = trailSystem.RegisterTrail(std::move(trail));

// Each frame: update all trails
std::unordered_map<uz::TrailSystem::TrailHandle, uz::Vec3> positions;
positions[handle] = entity.GetWorldPosition();
trailSystem.Update(deltaTime, positions, camera.GetWorldPosition());

// Retrieve render data for pass submission
auto renderData = trailSystem.GetRenderData();
uz::TrailPassDesc passDesc{ colorTarget, depthTarget, /* items... */ };
// ... populate passDesc.items from renderData ...
AddTrailRenderPass(rgBuilder, passDesc);

// Cleanup
trailSystem.UnregisterTrail(handle);
```

## Performance Optimization Tips

### Budget Tracking

Monitor total active points across all trails:

```cpp
uint32_t totalPoints = trailSystem.GetTotalSegmentCount();
uint32_t activeTrails = trailSystem.GetActiveTrailCount();

UZ_LOG(VFX, Info, "Trails: {} active, {} total points", activeTrails, totalPoints);
```

**Target**: Keep `totalPoints < 2000` for good performance on mid-range hardware.

### LOD Strategies

Reduce trail quality based on distance:

```cpp
float distanceToCamera = (trailCentroid - camera.GetPos()).Length();

if (distanceToCamera < 5.0f) {
    // Close: high quality
    trail.minPointDistance = 0.05f;
    trail.maxTrailPoints = 200;
} else if (distanceToCamera < 20.0f) {
    // Medium: medium quality
    trail.minPointDistance = 0.1f;
    trail.maxTrailPoints = 128;
} else {
    // Far: low quality
    trail.minPointDistance = 0.2f;
    trail.maxTrailPoints = 64;
}
```

### Conditional Rendering

Disable trails when off-screen or at low FPS:

```cpp
// Skip trails if frame time is already high
if (frameTime > 16.0f) {  // < 60 FPS
    trail.isActive = false;
} else {
    trail.isActive = true;
}
```

## Troubleshooting

### Problem: Trail Appears Broken/Flickering

**Cause**: `min_point_distance` too small, emitter too slow, or `max_points` too low.

**Solution**:
- Increase `min_point_distance` to 0.1–0.15
- Ensure emitter is actually moving (distance check in Tick)
- Increase `max_points` if trail is clipped

### Problem: Trail Looks Choppy (Visible Segments)

**Cause**: `min_point_distance` too large, or points are being culled too fast.

**Solution**:
- Decrease `min_point_distance` to 0.05–0.08
- Increase `point_lifetime` or `max_points`

### Problem: Trail Disappears Suddenly

**Cause**: `point_lifetime` too short, or emitter stopped moving.

**Solution**:
- Increase `point_lifetime` to 2.0–3.0
- Check that emitter is moving (stationary emitters emit only one point)
- Check `isActive` flag

### Problem: Texture Not Scrolling

**Cause**: `uv_scroll_rate = 0` or texture not authored for scrolling.

**Solution**:
- Set `uv_scroll_rate` to 1.0–3.0
- Ensure trail material/texture is a repeating animated pattern

### Problem: Trail Memory Usage Too High

**Cause**: Too many trails, too many `max_points`, or trails not being cleaned up.

**Solution**:
- Reduce `max_points` per trail (e.g., 100 → 64)
- Implement LOD system (reduce quality at distance)
- Ensure trails are unregistered when entities die

## See Also

- [.uztrail Format](14-uztrail-format.md) — Asset file specification
- [Trail Rendering Pipeline](15-trail-rendering-pipeline.md) — Technical implementation
- [VFX System Overview](01-overview.md) — Broader VFX architecture
- [Decal Configuration Guide](13-decal-pool-atlas.md) — Similar optimization strategies
