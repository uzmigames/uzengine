<!-- doc-meta
type: analysis
category: rendering
title: Shadow Comparison Semantics — UE5 Pattern Adapted for UzEngine
description: How UE5 handles shadow depth comparison and the key pattern to adopt.
tags: [shadow-mapping, comparison-function, depth-space, linear-depth]
status: complete
last-updated: 2026-03-16
-->

# Shadow Comparison Semantics — UE5 Pattern for UzEngine

## The Problem You're Solving

Your engine uses DX12 reversed-Z (0 = far, 1 = near), but shadow maps should use **standard depth semantics** (0 = near, 1 = far). The comparison must happen in a consistent depth space.

UE5 solves this elegantly.

---

## UE5's Solution: Two-Phase Conversion

### Phase 1: Shadow Depth Rendering (Vertex Shader)

**File**: `ShadowDepthVertexShader.usf`, line 104

```hlsl
const float InvMaxSubjectDepth = PassStruct.ShadowParams.w;  // 1 / (MaxZ - MinZ)
float AdjustedDepth = (1 - OutPosition.z * InvMaxSubjectDepth) + DepthBias;
ShadowDepth = AdjustedDepth;
OutPosition.z = AdjustedDepth;
```

**What happens**:
1. `OutPosition.z` is standard DX12 reversed-Z (0 = far, 1 = near)
2. Multiply by `InvMaxSubjectDepth` to normalize into [0, 1] range
3. `1 - (...)` flips the range: now 0 = near, 1 = far (standard)
4. Add depth bias (to stored depth)
5. **Store this linear depth into shadow texture**

**Result**: Shadow texture contains **linear normalized depth [0, 1]** where 0 = near, 1 = far.

---

### Phase 2: Shadow Projection (Pixel Shader)

**File**: `ShadowProjectionPixelShader.usf`, lines 147 + 169–173

```hlsl
// Convert scene depth from reversed-Z to linear
float ShadowZ = 1 - ShadowPosition.z;

// Clamp to prevent comparing against unrendered far plane (1.0)
float LightSpacePixelDepthForOpaque = min(ShadowZ, 0.99999f);
```

**What happens**:
1. `ShadowPosition.z` is the scene depth in shadow-space clip coordinates (reversed-Z)
2. `1 - ShadowPosition.z` converts to linear depth (0 = near, 1 = far)
3. Clamp to 0.99999 to avoid the shadow map's unrendered far plane (cleared to 1.0)
4. **Now ready to compare against stored shadow depth**

**Result**: Scene depth and shadow depth are now **in the same space**.

---

### Phase 3: Comparison (Manual PCF)

**File**: `ShadowFilteringCommon.ush`, lines 162–170

```hlsl
// The standard comparison is SceneDepth < ShadowmapDepth
float ConstantFactor = (Settings.SceneDepth * TransitionScale - 1);
float4 ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor);

// Expands to:
// ShadowFactor = saturate((ShadowmapDepth - SceneDepth) * TransitionScale + 1);
```

**Interpretation**:
- If `ShadowmapDepth > SceneDepth` → result is 1.0 (unshadowed)
- If `ShadowmapDepth < SceneDepth` → result is 0.0 (shadowed)
- Range in between → soft transition controlled by `TransitionScale` (default 60.0)

**Key insight**: **`ShadowmapDepth > SceneDepth` is unshadowed.**

---

## The Pattern: Three Invariants

After analyzing UE5 code, three invariants emerge:

### Invariant 1: Shadow Texture Depth Is Always Standard [0, 1]

Whether you use orthographic or perspective projections, the stored shadow depth must always be:
- **0 = near plane**
- **1 = far plane**
- **Linear** (not in clip space)

**UE5's code**:
```hlsl
float AdjustedDepth = (1 - clip_z * InvMax) + bias;
```

This **always** produces standard linear depth.

### Invariant 2: Scene Depth Must Match Shadow Depth Space Before Comparison

Both depths must be in the **same coordinate system** before comparison.

**UE5's code**:
```hlsl
float ShadowZ = 1 - ShadowPosition.z;  // Convert reversed-Z to linear
```

If your main view is reversed-Z, you must flip it here.

### Invariant 3: The Comparison Is Always Standard

```
unshadowed = (shadowmap_depth > scene_depth)
```

This is the standard depth test semantics. No flipping, no inverting.

---

## Why This Design?

### Benefit 1: Depth Bias Is Meaningful

Since shadow depth is linear, a depth bias of 0.001 is **0.1% of the shadow frustum**, regardless of perspective distortion. This makes bias values consistent across the view.

### Benefit 2: PCF Transitions Are Consistent

The soft transition width (TransitionScale = 60.0) applies uniformly in linear depth space, avoiding perspective-dependent shadow acne.

### Benefit 3: No Double-Flipping

You convert once at storage (VS) and once at readback (PS), not three times scattered throughout.

---

## UzEngine Implementation Guidance

### For Directional Light Shadows

```cpp
// Vertex shader: ShadowDepthVertexShader.cpp
float DepthLinear = (1.0f - ClipDepth * InvMaxSubjectDepth);
float DepthBiased = DepthLinear + DepthBias;
// Write DepthBiased to shadow target
```

```hlsl
// Pixel shader: ShadowProjectionPixelShader.hlsl
float SceneDepthLinear = 1.0f - SceneDepthReversedZ;
float SceneDepthClamped = min(SceneDepthLinear, 0.99999f);

// Sample shadow map (which contains linear depth)
float ShadowmapDepth = SampleShadow(UV).r;

// Compare
float Unshadowed = saturate((ShadowmapDepth - SceneDepthClamped) * TransitionScale + 1.0);
```

### For Point Light Shadows

UE5 uses hardware PCF here with `SCF_Less`:

```cpp
// Hardware comparison: depth < shadowmap_depth → 1.0 (lit)
SamplerComparisonState ShadowSampler : register(s0);
float result = Texture.SampleCmp(ShadowSampler, coords, compareDepth);
```

But the same logic applies: both depths must be in the same space before the hardware does the comparison.

---

## Comparison Direction Cheat Sheet

| Depth Space | Semantics | Unshadowed Test | Shadowed Test |
|-------------|-----------|-----------------|---------------|
| **Standard (0=near, 1=far)** | StoredDepth > SceneDepth | Yes | No |
| **Reversed-Z (0=far, 1=near)** | StoredDepth < SceneDepth | Yes | No |

**UE5 uses**: Standard (left), so it compares `StoredDepth > SceneDepth`.

---

## Testing Your Implementation

Once you've implemented shadow sampling, verify with these checksums:

1. **Unrendered pixels (SceneDepth = 1.0) should be unshadowed**
   - After clamping to 0.99999, comparison should yield 1.0 (unshadowed)

2. **Very close pixels (SceneDepth ≈ ShadowmapDepth) should show soft transition**
   - Adjust TransitionScale (default 60.0) to see transition width change

3. **Pixels far behind shadow caster should be fully shadowed**
   - SceneDepth >> ShadowmapDepth should yield 0.0

4. **No self-shadowing on flat surfaces**
   - Use slope bias to prevent caster from shadowing itself

---

## References

- **Shadow depth rendering**: `ShadowDepthVertexShader.usf`, lines 99–107
- **Scene depth conversion**: `ShadowProjectionPixelShader.usf`, line 147
- **Depth clamping**: `ShadowProjectionPixelShader.usf`, line 171
- **PCF comparison**: `ShadowFilteringCommon.ush`, lines 162–170
- **Default TransitionScale**: `ShadowRendering.cpp`, line 70 (60.0 by default)
