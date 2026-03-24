<!-- doc-meta
title: Metallic + IBL Analysis — Start Here
project: UzEngine
created: 2026-03-23
keywords: metallic, IBL, analysis summary, UE5 behavior
-->

# Metallic + IBL Complete Analysis — Start Here

## Quick Answer

**Your question**: When `metallic=1.0` and there is NO IBL (no sky light, no reflection captures), does UE5 show pure black on metallic objects?

**Answer**: **YES. This is correct and expected behavior.**

UE5 (and physics) treats metals as pure mirrors. Mirrors reflect only what they can "see":
- With an environment (sky light) → reflects blue sky, reflections
- Without environment → reflects nothing → appears black

---

## The Problem

When camera moves, your metallic spheres change appearance dramatically:
- Facing light with direct highlight → **BRIGHT**
- Away from highlight → **BLACK**
- Move camera → appearance changes

**Why?** Metallic surfaces have **zero diffuse lighting**, so the ONLY light source is:
1. Direct specular highlight (from sun/lights) — this moves with camera
2. Indirect reflections (from sky/probes) — this is ZERO without IBL

With only the specular highlight, the appearance changes as drastically as a mirror's reflection would.

---

## Documents in This Analysis

### 1. **ue5-metallic-no-ibl.md** ← Start here for the core findings
- What UE5 does mathematically
- Why it's physically correct
- Where in source code this happens

### 2. **metallic-shading-reference.md** ← For implementation details
- Complete color decomposition math
- Direct lighting formulas
- C++ implementation patterns
- Test cases for validation

### 3. **metallic-appearance-camera-movement.md** ← For understanding the visual issue
- Why camera movement matters
- Timeline of visual changes
- How to fix it (add IBL)
- Verification tests

### 4. **metallic-ibl-analysis-summary.md** ← For complete picture
- Full architecture overview
- Implementation checklist
- Common mistakes to avoid
- Physics references

### 5. **ue5-metallic-api-reference.md** ← For detailed function signatures
- HLSL shader functions
- C++ class/function signatures
- GPU pipeline diagram
- Data flow walkthrough

---

## TL;DR: The Math

```
With metallic = 1.0:
  DiffuseColor = 0
  SpecularColor = BaseColor
  TotalLight = DirectSpecular + IndirectSpecular
             = GGX_Highlight + (SkyLight or ProbeReflections)

Without IBL:
  TotalLight = GGX_Highlight + 0
             = GGX_Highlight ONLY

When highlight is not visible:
  TotalLight = 0 + 0 = BLACK ✓ (correct)
```

---

## TL;DR: How to Fix It

### Option 1: Add Sky Light (Recommended)
```cpp
skyLight = CreateSkyLight();
skyLight->SetTexture(SkyTexture);  // From procedural sky or capture
```

**Effect**: Metallic objects now reflect blue sky → always visible

### Option 2: Add Reflection Probes
```cpp
reflectionCapture = CreateReflectionCapture();
reflectionCapture->CaptureScene();  // Bakes local reflections
```

**Effect**: Local reflections work like mirrors in a room

### Option 3: Use Procedural Sky + Sky Light
```cpp
// Generate procedural sky, bake it to cubemap, feed to sky light
cube = GenerateProceduralSkyCubemap(timeOfDay, weatherType);
skyLight->SetTexture(cube);
```

**Effect**: Dynamic sky light that updates with time/weather

### Option 4: Add More Directional Lights
```cpp
// More lights = more highlights = more visible always
AddLight(direction: North, intensity: 1.0);
AddLight(direction: South, intensity: 0.3);
AddLight(direction: East, intensity: 0.3);
```

**Effect**: Multiple highlights make surface always have SOME light
- Not realistic but practical for screenshots/gameplay
- Real world only has one light (sun)

---

## Technical Validation

### Is Your Implementation Correct?

Run this test:

1. **Create scene**:
   - Gold sphere (metallic=1.0, roughness=0.3)
   - Gray sphere (metallic=0.0) for comparison
   - One directional light only
   - NO sky light, NO reflection probes

2. **Look at results**:
   - **Gold backlit**: Pure black ✓ (metallic behavior)
   - **Gray backlit**: Medium gray ✓ (diffuse visible)
   - **Gold front**: Bright highlight ✓
   - **Gray front**: Bright all over ✓

3. **Add sky light**:
   - **Gold backlit**: Now has blue tint ✓ (reflects sky)
   - **Gray backlit**: Brighter blue tint ✓

If your results match this, **your implementation is correct**.

---

## Why This Is Correct Physics

From "Physically Based Rendering" (Pharr, Jakob, Humphreys):

> Metals are highly specular; they have little to no diffuse reflection. A metal surface at normal incidence reflects 90%+ of light via specular (mirror-like) reflection. The remaining fraction is either:
> - Scattered by surface roughness (but still specular, not diffuse)
> - Absorbed

Key insight: **Metals cannot diffusely scatter light.** They either:
- Specularly reflect (mirror-like)
- Absorb (darkness)

Without an environment to reflect, there's nothing to show.

---

## UE5 Source Files (Quick Reference)

| What | File | Line |
|------|------|------|
| Metal decomposition | `DeferredShadingCommon.ush` | 750-751 |
| Direct lighting | `DeferredLightingCommon.ush` | 420, 454 |
| Sky light check | `IndirectLightRendering.cpp` | 1987-1991 |
| Sky light pass | `IndirectLightRendering.cpp` | 2146-2159 |
| GBuffer struct | `DeferredShadingCommon.ush` | 340-387 |
| Specular BRDF | `BRDF.ush` | SpecularBRDF() |

---

## Next Steps for UzEngine

1. **Read `ue5-metallic-no-ibl.md`** — understand what UE5 does
2. **Check your deferred shader**:
   - Is `DiffuseColor = BaseColor * (1 - Metallic)` exactly right?
   - Is indirect lighting a separate pass?
3. **Add sky light support** if not present
4. **Test** with the validation test above
5. **Reference `metallic-shading-reference.md`** for implementation details

---

## Key Insight

**The "problem" you're describing is not a problem — it's correct behavior.**

If your metallic objects look black without IBL, that means your metallic decomposition is working correctly. The solution is to add environment lighting (sky light or probes), not to "fix" the metallic behavior.

This is the same in all physically-based renderers (UE5, Godot, Unity, custom engines). The first lesson in PBR is: **"Your scene needs environment lighting."**

---

## Document Map

```
START HERE (this file)
  ↓
ue5-metallic-no-ibl.md [CORE FINDINGS]
  ├─ What UE5 does
  ├─ Why (color decomposition)
  └─ Where (source files & line numbers)
  ↓
metallic-appearance-camera-movement.md [VISUAL EXPLANATION]
  ├─ Why it changes with camera
  ├─ Visual timeline
  └─ How to fix it
  ↓
metallic-shading-reference.md [IMPLEMENTATION]
  ├─ Math formulas
  ├─ C++ patterns
  └─ Test cases
  ↓
metallic-ibl-analysis-summary.md [COMPLETE GUIDE]
  ├─ Full architecture
  ├─ Checklist
  └─ Common mistakes
  ↓
ue5-metallic-api-reference.md [FUNCTION SIGNATURES]
  ├─ HLSL functions
  ├─ C++ signatures
  └─ Source file locations
```

---

## Questions Answered

- **Q: Does UE5 show black on unlit metallic surfaces without IBL?**
  A: Yes, this is correct. Metals are mirrors; without environment, they reflect nothing.

- **Q: Is there a fallback for metallic without IBL?**
  A: No. UE5 is physically based; there is no fake ambient or compensation.

- **Q: Why does it change with camera movement?**
  A: Specular highlight is view-dependent. Without IBL, only the highlight provides light.

- **Q: Is my implementation wrong?**
  A: Probably not. Test it with the validation test above. If results match UE5, you're correct.

- **Q: How do I fix the black backlit metallic?**
  A: Add sky light or reflection probes. This is the correct solution.

---

## Summary

✓ UE5 correctly treats metallic as zero-diffuse, pure-specular
✓ Without IBL, metallic surfaces are black when specular highlight is absent
✓ This is **physically accurate and expected**
✓ The solution is to **add environment lighting (sky light or probes)**
✓ Your implementation is correct if it matches the validation test

