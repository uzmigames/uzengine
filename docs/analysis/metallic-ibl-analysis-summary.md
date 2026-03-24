<!-- doc-meta
title: UE5 Metallic + IBL Complete Analysis
project: UzEngine
created: 2026-03-23
keywords: metallic, IBL, deferred shading, environment lighting, PBR
-->

# UE5 Metallic Behavior Without IBL — Complete Analysis

## Executive Summary

**Question**: When `metallic=1.0` and there is NO IBL/environment lighting (no sky light, no reflection captures), what does UE5 do?

**Answer**: UE5 shows **pure black on surfaces where the specular highlight is not visible**. This is correct and physically accurate behavior for metals — they are mirrors, and without an environment to reflect, they reflect nothing.

---

## Key Findings

### 1. Color Decomposition

UE5 splits materials into diffuse and specular based on metallic:

```hlsl
GBuffer.DiffuseColor = GBuffer.BaseColor * (1 - GBuffer.Metallic);
GBuffer.SpecularColor = ComputeF0(GBuffer.Specular, GBuffer.BaseColor, GBuffer.Metallic);
```

**Result for metallic=1.0**:
- `DiffuseColor = 0` — no diffuse scattering
- `SpecularColor = BaseColor` — metal F0 value
- **Only specular (mirror) reflection is possible**

**Source**: `DeferredShadingCommon.ush:750-751`

### 2. Direct Lighting (Single Direction)

Direct lights (sun, point, spot) apply **both diffuse and specular**:

```hlsl
float3 DiffuseLighting = Diffuse_Lambert(GBuffer.DiffuseColor) * ...;
float3 SpecularLighting = GGX_BRDF(GBuffer.SpecularColor, ...) * ...;
```

**For metallic=1.0**:
- Diffuse term: `0 * anything = 0` ✓
- Specular term: `GGX_BRDF(BaseColor, ...)` ← Only source of light

**Source**: `DeferredLightingCommon.ush:420,454`

### 3. Indirect Lighting (Sky + Reflections) — SEPARATE PASS

Sky light and reflections are applied in a **completely different renderer pass**:

```cpp
// IndirectLightRendering.cpp:2146-2159
AddSkyReflectionPass(
    // Checks: if (Scene->SkyLight && SkyLight->ProcessedTexture) { ... }
    // Applies: Sky diffuse + reflection captures
);
```

**When NO SkyLight or NO reflection captures exist**:
- Sky light diffuse: **ZERO**
- Reflection captures: **ZERO**
- Total indirect lighting: **ZERO**

**Source**: `IndirectLightRendering.cpp:1987-1991`

### 4. The Result: YES, Pure Black Without IBL

| Condition | Result | Why |
|-----------|--------|-----|
| metallic=1.0, direct light only, highlight visible | BRIGHT | GGX specular reflects light |
| metallic=1.0, direct light only, highlight NOT visible | **BLACK** | No diffuse, no indirect |
| metallic=1.0, + sky light | BLUE-TINTED | Reflects blue sky |
| metallic=1.0, + reflection capture | COLORED | Reflects probe color |
| metallic=0.0, direct light only | GRAY | Diffuse + small specular |

---

## UE5 Implementation Details

### The Deferred Shading Pipeline (High Level)

```
1. Base Pass
   ├─ Write GBuffer (BaseColor, Normal, Metallic, Roughness, etc.)
   └─ (NO lighting computation yet in deferred)

2. Deferred Light Pass (per light)
   ├─ Read GBuffer
   ├─ Compute DiffuseColor = BaseColor * (1 - Metallic)
   ├─ Compute SpecularColor = ComputeF0(Metallic, BaseColor)
   ├─ Apply direct light to BOTH
   └─ Write to SceneColor (additive)

3. Indirect Lighting Pass (separate)
   ├─ Read GBuffer
   ├─ If (SkyLight exists and has texture):
   │  └─ Apply sky diffuse to DiffuseColor
   │  └─ Apply sky specular to SpecularColor
   ├─ If (Reflection captures exist):
   │  └─ Apply probes to SpecularColor
   └─ Write to SceneColor (additive)

Result: SceneColor = DirectLighting + IndirectLighting
```

### Critical Code Paths

**Step 1: Decomposition** (BasePass)
- File: `DeferredShadingCommon.ush:750-751`
- Code: `DiffuseColor = BaseColor - BaseColor * Metallic`

**Step 2: Direct Lighting** (DeferredLightPixelShader)
- File: `DeferredLightingCommon.ush:321-462` (AccumulateDynamicLighting)
- Code: Applies both Diffuse_Lambert(DiffuseColor) and GGX(SpecularColor)

**Step 3: Indirect Lighting** (IndirectLightRendering)
- File: `IndirectLightRendering.cpp:1957-2235` (RenderDeferredReflectionsAndSkyLighting)
- Code: Calls AddSkyReflectionPass if Scene->SkyLight exists
- **CRITICAL**: If no sky light, this contribution is zero

---

## Why Camera Movement Matters

Metallic surfaces appear to change dramatically as you move the camera because:

1. **The specular highlight is view-dependent**
   ```hlsl
   float3 H = normalize(L + V);  // V = view direction
   // As V changes, H changes → GGX output changes → highlight moves
   ```

2. **Without IBL, ONLY the highlight provides light**
   - When highlight is visible → bright
   - When highlight leaves → black

3. **With IBL, the surface is always lit**
   - Highlight still moves, but surface remains visible
   - Sky/probe reflections provide base brightness

---

## Implementation Checklist for UzEngine

### Phase 1: Verify Metallic Decomposition

- [ ] Material parser reads metallic value [0, 1]
- [ ] Deferred pass computes: `DiffuseColor = BaseColor * (1 - Metallic)`
- [ ] Deferred pass computes: `SpecularColor = Lerp(0.04, BaseColor, Metallic)`
- [ ] Test: metallic=0 → full diffuse, metallic=1 → zero diffuse
- [ ] Test: Gold (1, 0.84, 0) at metallic=1 → specular shows golden hue

### Phase 2: Verify Direct Lighting Respects Decomposition

- [ ] Diffuse term: `Diffuse_Lambert(DiffuseColor) * LightColor`
- [ ] Specular term: `GGX(SpecularColor, Roughness, L, V) * LightColor`
- [ ] Test: metallic=1, single directional light
  - Front: Bright golden highlight
  - Back: Pure black (expected)

### Phase 3: Implement IBL Application

- [ ] Sky light pass reads GBuffer (metallic)
- [ ] Sky light applies ONLY to SpecularColor (not DiffuseColor, as diffuse=0 for metals)
- [ ] Actually: Sky diffuse DOES apply to DiffuseColor via SH coefficients
- [ ] Reflection probes apply ONLY to SpecularColor
- [ ] Test: metallic=1 with sky light
  - Back: Reflects sky (blue tint, not black)

### Phase 4: Visual Validation

- [ ] Scene: Single directional light, NO sky light, NO probes
  - Metallic sphere looks like polished mirror
  - Bright highlight, black elsewhere ✓
  - Non-metallic sphere looks like matte paint
  - Visible from all angles ✓

- [ ] Scene: Add sky light
  - Metallic sphere now reflects blue sky
  - Back side is not black anymore ✓
  - Non-metallic sphere gets blue tint on backlit ✓

---

## Physics & References

### Why Metals Have No Diffuse

Metals are perfect reflectors above a certain threshold (Fresnel angle). At normal incidence, they reflect ~90% of light specularly. The remaining ~10% is:
- Scattered by surface imperfections (represented by roughness)
- Not diffuse scattering (that requires subsurface light penetration)

**Reference**: Physically Based Rendering (Pharr, Jakob, Humphreys)

### Why Specular Highlight Moves with Camera

The specular lobe in GGX is determined by:
- Light direction (L)
- View direction (V)
- Half-vector (H = normalize(L + V))

As camera moves (V changes), H changes → the angle of perfect reflection changes → highlight position moves on the surface.

**Reference**: Cook & Torrance 1982, GGX by Walter et al. 2007

---

## Common Implementation Mistakes

### Mistake 1: Applying Diffuse to Metals

```hlsl
// WRONG
float3 diffuse = Diffuse_Lambert(BaseColor) * (1 - Metallic);
// This still applies SOME diffuse when metallic=0.9
```

**Correct**:
```hlsl
// CORRECT
float3 diffuse = Diffuse_Lambert(BaseColor * (1 - Metallic));
// This ensures DiffuseColor is zero when metallic=1.0
```

### Mistake 2: Skipping Indirect Pass When No IBL

```cpp
// WRONG
if (hasSkyLight) {
    ApplyIndirectLighting(...);
}
// If no sky light, indirect is forgotten
```

**Correct**:
```cpp
// CORRECT
ApplyIndirectLighting(...);
// Pass handles "if (scene->skyLight) { ... }" internally
// If no sky light, it contributes zero (correct)
```

### Mistake 3: Not Separating Direct & Indirect

```hlsl
// WRONG
LightAccumulator += DirectDiffuse + DirectSpecular + SkyDiffuse + SkySpecular;
// Hard to debug, can't validate each component
```

**Correct**:
```hlsl
// CORRECT (separate passes)
// Deferred light pass: DirectDiffuse + DirectSpecular
// Sky reflection pass: SkyDiffuse + SkySpecular (added separately)
```

---

## UE5 Source File Reference

| Component | File | Lines |
|-----------|------|-------|
| Decomposition | `DeferredShadingCommon.ush` | 750-751 |
| Direct diffuse | `DeferredLightingCommon.ush` | 420, 454 |
| Direct specular | `DeferredLightingCommon.ush` | 420, 454 |
| Accumulation | `DeferredLightingCommon.ush` | 321-462 |
| Sky light check | `IndirectLightRendering.cpp` | 1987-1991 |
| Sky pass | `IndirectLightRendering.cpp` | 2146-2159 |
| F0 computation | `BRDF.ush` | ComputeF0() |

---

## Conclusion

UE5's behavior is **correct and should be replicated exactly** in UzEngine:

1. Metallic=1.0 → zero diffuse, pure specular
2. Direct light only → only specular highlight visible
3. No IBL → metallic surfaces are black when highlight is absent
4. With IBL → metallic surfaces reflect the environment

If your implementation matches this, **the black backlit side is correct**. The solution is to add sky lighting or environment probes, not to "fix" the metallic behavior.

