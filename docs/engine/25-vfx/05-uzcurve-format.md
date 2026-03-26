---
title: .uzcurve JSON Format Specification
status: Done (Phase 3.61.0)
last-updated: 2026-03-24
---

# .uzcurve JSON Format Specification

Complete specification for the `.uzcurve` keyframe-based animation curve format used by the VFX system.

> **File Extension**: `.uzcurve`
> **Format**: JSON
> **Parser**: `uz::VFXCurveLoader`

## Overview

The `.uzcurve` format enables artists and designers to author animation curves without writing code. Curves define how particle properties (size, color, velocity, etc.) change over a particle's lifetime.

**Curve Types**:
- **Float**: Single scalar value (e.g., size multiplier)
- **Vec3**: 3D vector with w=0 (e.g., velocity offset, direction)
- **Vec4**: 4D vector (e.g., RGBA color)

**Interpolation Modes**:
- **linear**: Simple linear interpolation between keyframes
- **cubic**: Catmull-Rom cubic spline (smooth, natural curves)
- **smoothstep**: Hermite smoothstep with ease-in/ease-out (good for color/opacity)

## File Structure

### Minimal Example (Float Curve)

```json
{
  "type": "float",
  "interpolation": "linear",
  "keys": [
    { "time": 0.0, "value": 1.0 },
    { "time": 1.0, "value": 0.0 }
  ]
}
```

This defines a linear fade from 1.0 to 0.0 over a particle's lifetime.

### Complete Structure

```json
{
  "type": "float" | "vec3" | "vec4",
  "interpolation": "linear" | "cubic" | "smoothstep",
  "keys": [
    { "time": 0.0, "value": ... },
    { "time": 0.25, "value": ... },
    { "time": 0.5, "value": ... },
    { "time": 1.0, "value": ... }
  ]
}
```

## Schema Reference

### Root Object

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | string | Yes | Curve data type: `"float"`, `"vec3"`, or `"vec4"` |
| `interpolation` | string | Yes | Interpolation mode: `"linear"`, `"cubic"`, or `"smoothstep"` |
| `keys` | array | Yes | Array of keyframe objects (min 2 required) |

### Keyframe Object (Float Curves)

```json
{
  "time": 0.5,
  "value": 2.5
}
```

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `time` | number | [0.0, 1.0] | Normalized lifetime (0 = spawn, 1 = death) |
| `value` | number | any | Float scalar value at this time |

### Keyframe Object (Vec3 Curves)

```json
{
  "time": 0.0,
  "value": [1.0, 0.0, 0.0]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `time` | number | Normalized lifetime [0.0, 1.0] |
| `value` | array (3) | [x, y, z] components (w implicitly 0.0) |

### Keyframe Object (Vec4 Curves)

```json
{
  "time": 0.5,
  "value": [1.0, 0.5, 0.0, 1.0]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `time` | number | Normalized lifetime [0.0, 1.0] |
| `value` | array (4) | [x, y, z, w] components |

## Validation Rules

The VFXCurveLoader applies the following validation rules:

### Time Constraints
- ✅ All `time` values must be in the range [0.0, 1.0]
- ✅ Keys must be sorted in ascending order by `time`
- ❌ Out-of-order keys: **ERROR** (fails to load)
- ❌ Duplicate times: **ERROR** (ambiguous sampling)
- ❌ Time outside [0, 1]: **ERROR**

### Keyframe Constraints
- ✅ Minimum 2 keyframes required (define start and end)
- ✅ No maximum keyframe count
- ❌ Fewer than 2 keys: **ERROR** (insufficient data)
- ❌ Empty `keys` array: **ERROR**

### Type Matching
- ✅ Vec3 curves must have `value` arrays of length 3
- ✅ Vec4 curves must have `value` arrays of length 4
- ✅ Float curves must have scalar `value` (number, not array)
- ❌ Type mismatch (e.g., float curve with array value): **ERROR**

### Interpolation
- ✅ Valid modes: `"linear"`, `"cubic"`, `"smoothstep"`
- ❌ Invalid interpolation value: **ERROR**
- ❌ Missing interpolation field: **ERROR**

### Error Handling
- Logs `VFX::Error` with specific validation failure details
- Returns `std::nullopt` on any validation error
- Caller must check returned optional before using the curve

## Interpolation Modes

### Linear

Simple linear interpolation (lerp) between adjacent keyframes.

```
value(t) = mix(key[i].value, key[i+1].value, blend)
```

**Use Cases**:
- Sharp transitions (e.g., sudden color changes)
- Simple fade effects
- When you want predictable, uncurved blending

**Example**:
```json
{
  "type": "float",
  "interpolation": "linear",
  "keys": [
    { "time": 0.0, "value": 0.0 },
    { "time": 1.0, "value": 1.0 }
  ]
}
```

Result: Constant rate of increase from 0 to 1.

---

### Cubic (Catmull-Rom)

Cubic Catmull-Rom spline interpolation using 4 surrounding keyframes.

```
value(t) = cubic_hermite(key[i-1], key[i], key[i+1], key[i+2], blend)
```

**Properties**:
- Smooth, natural curves (C2 continuous, zero second derivatives at keyframes)
- Passes through all keyframes exactly
- Uses 4 keyframes for interpolation (lookahead + lookbehind)
- First/last keyframes extrapolate based on nearest neighbor

**Use Cases**:
- Smooth motion curves (position, velocity)
- Natural color transitions
- Arc-like trajectories

**Example**:
```json
{
  "type": "vec3",
  "interpolation": "cubic",
  "keys": [
    { "time": 0.0, "value": [0.0, 0.0, 0.0] },
    { "time": 0.5, "value": [1.0, 2.0, 0.5] },
    { "time": 1.0, "value": [2.0, 0.0, 0.0] }
  ]
}
```

Result: Smooth arc-like trajectory through the keyframes.

---

### Smoothstep

Hermite smoothstep with ease-in/ease-out blending.

```
t_smooth = t * t * (3 - 2*t)  // Hermite smoothstep
value(t) = mix(key[i].value, key[i+1].value, t_smooth)
```

**Properties**:
- Non-linear time mapping (accelerates in, decelerates out)
- Smooth start and end (zero velocity at keyframe boundaries)
- Creates a gentle easing effect

**Use Cases**:
- Color fades with ease-in/ease-out
- Opacity curves
- Gentle scale animations
- UI element transitions

**Example**:
```json
{
  "type": "vec4",
  "interpolation": "smoothstep",
  "keys": [
    { "time": 0.0, "value": [1.0, 1.0, 1.0, 1.0] },
    { "time": 1.0, "value": [0.5, 0.0, 0.0, 0.0] }
  ]
}
```

Result: Color smoothly fades with gentle ease-in/ease-out.

---

## Examples

### Example 1: Size Over Lifetime (Float)

Particles grow larger and then shrink away.

```json
{
  "type": "float",
  "interpolation": "cubic",
  "keys": [
    { "time": 0.0, "value": 0.5 },
    { "time": 0.3, "value": 1.5 },
    { "time": 1.0, "value": 0.1 }
  ]
}
```

**Sampling**:
- At spawn (t=0.0): size multiplier = 0.5
- At peak (t≈0.3): size multiplier = 1.5 (largest)
- At death (t=1.0): size multiplier = 0.1 (fading away)

---

### Example 2: Color Over Lifetime (Vec4)

Particles transition from blue → white → red with fade-out.

```json
{
  "type": "vec4",
  "interpolation": "smoothstep",
  "keys": [
    { "time": 0.0, "value": [0.0, 0.0, 1.0, 1.0] },
    { "time": 0.5, "value": [1.0, 1.0, 1.0, 1.0] },
    { "time": 1.0, "value": [1.0, 0.0, 0.0, 0.0] }
  ]
}
```

**Sampling**:
- At spawn (t=0.0): RGBA = [0, 0, 1, 1] (blue, opaque)
- At midlife (t=0.5): RGBA = [1, 1, 1, 1] (white, opaque)
- At death (t=1.0): RGBA = [1, 0, 0, 0] (red, transparent)

---

### Example 3: Velocity Offset (Vec3)

Velocity impulse peaks at mid-lifetime, applying wind-like effect.

```json
{
  "type": "vec3",
  "interpolation": "cubic",
  "keys": [
    { "time": 0.0, "value": [0.0, 0.0, 0.0] },
    { "time": 0.3, "value": [5.0, 0.0, 2.0] },
    { "time": 0.7, "value": [3.0, 0.0, 1.0] },
    { "time": 1.0, "value": [0.0, 0.0, 0.0] }
  ]
}
```

**Sampling**:
- Early (t < 0.3): Wind effect ramps up
- Mid (t = 0.3): Peak wind velocity = (5, 0, 2) m/s
- Late (t > 0.7): Wind effect fades out
- At death (t=1.0): Wind effect = zero

---

### Example 4: Simple Linear Fade (Float)

Brightness decreases linearly throughout lifetime.

```json
{
  "type": "float",
  "interpolation": "linear",
  "keys": [
    { "time": 0.0, "value": 1.0 },
    { "time": 1.0, "value": 0.0 }
  ]
}
```

**Sampling**:
- At spawn: brightness = 1.0
- At 25% lifetime: brightness = 0.75
- At 50% lifetime: brightness = 0.5
- At death: brightness = 0.0

---

## Loading Curves

### Via VFXCurveLoader

```cpp
#include "engine/vfx/curves/vfx_curve_loader.h"

// Load from JSON string
std::string json = R"({
  "type": "float",
  "interpolation": "cubic",
  "keys": [
    { "time": 0.0, "value": 0.0 },
    { "time": 1.0, "value": 1.0 }
  ]
})";

std::optional<uz::VFXCurve> curve = uz::VFXCurveLoader::LoadFromJson(json);
if (curve) {
    float sample = curve->SampleFloat(0.5f);  // Sample at 50% lifetime
}

// Load from file
std::optional<uz::VFXCurve> fileCurve = uz::VFXCurveLoader::LoadFromFile("assets/size_curve.uzcurve");
```

### Sampling Curves

```cpp
// Float curves
float value = curve.SampleFloat(0.25f);  // Sample at 25% lifetime

// Vec4 curves
uz::Vec4 color = curve.SampleVec4(0.5f); // Sample at 50% lifetime
```

### GPU Usage (Baked Texture)

```cpp
// Bake curve to 1D GPU texture for efficient shader sampling
curve.BakeToTexture(rhiDevice, 256);  // 256 texels

// In shader:
// float t = Age / Lifespan;  // Normalized time
// float4 sampledColor = BakedCurveTexture.Sample(LinearSampler, t);
```

---

## Best Practices

### When Designing Curves

**Do**:
- ✅ Start and end keyframes at t=0.0 and t=1.0
- ✅ Use meaningful keyframe counts (3-5 for simple curves, up to 10+ for complex)
- ✅ Choose interpolation modes that match your visual intent
- ✅ Test curves in the editor before committing
- ✅ Use descriptive file names (`size_over_life.uzcurve`, `color_fade.uzcurve`)

**Don't**:
- ❌ Create curves with non-monotonic (unsorted) time values
- ❌ Use more keyframes than necessary (increases file size, GPU cost)
- ❌ Hardcode curve values in code when a `.uzcurve` file would be clearer
- ❌ Use cubic interpolation for very few keyframes (linear is simpler)

### Performance Considerations

| Aspect | Impact | Notes |
|--------|--------|-------|
| Keyframe Count | O(log N) CPU lookup | Baked textures eliminate lookup cost |
| Interpolation Mode | O(1) | All modes have same GPU cost when baked |
| Texture Baking | One-time 256-texel cost | Then single GPU texture fetch per sample |

**Recommendation**: Bake curves to GPU textures for runtime use. CPU-side sampling is fine for editor preview.

---

## File Format Version

**Current Version**: 1.0

No version field is included in the `.uzcurve` JSON. Future changes to the format will be handled via:
1. Backward-compatible additions (new optional fields)
2. Version field if breaking changes are needed

---

## See Also

- [VFX Module API](04-module-api.md) — Module parameters and configuration
- [VFX System Overview](01-overview.md) — High-level architecture
- `runtime/engine/vfx/curves/vfx_curve.h` — C++ API
- `runtime/engine/vfx/curves/vfx_curve_loader.h` — Loader implementation
