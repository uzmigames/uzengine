<!-- doc-meta
title: Render Quality Consensus Analysis — UE5 + Unity HDRP + Godot 4 vs UzEngine
description: Cross-engine consensus on rendering techniques, gap matrix, and root-cause analysis of UzEngine visual quality issues
author: engine-architect
date: 2026-03-22
-->

# Render Quality Consensus Analysis

**Sources**: UE5 5.6, Unity HDRP 16, Godot 4 Forward+
**UzEngine Audit**: `uzengine-renderer-audit.md` (2026-03-22)
**Purpose**: Identify consensus best practices, map gaps, diagnose visual quality root causes

---

## 1. Universal Consensus — What All Three Engines Agree On

These are non-negotiable. Any engine deviating from these will look wrong.

### 1.1 BRDF Model

**Consensus**: GGX Trowbridge-Reitz microfacet specular + Burley/Disney diffuse.

| Engine | Specular NDF | Geometry Term | Diffuse | F0 Dielectric |
|--------|-------------|----------------|---------|---------------|
| UE5 | GGX (alpha = roughness²) | Smith height-correlated joint | Burley 2012 | 0.04 |
| Unity HDRP | GGX (same) | Smith (same) | Lambert (LDR) / GGX (IBL) | 0.04 |
| Godot 4 | GGX (Trowbridge-Reitz) | Smith (same) | Burley via `mix()` | 0.04 (via 0.16 * specular²) |
| **UzEngine** | GGX ✓ | Smith height-correlated ✓ | Burley ✓ | 0.04 ✓ |

**Verdict**: UzEngine BRDF is CORRECT and at parity with all three engines. This is NOT the source of the visual quality issue.

### 1.2 Energy Conservation / Multiscatter

**Consensus**: Single-scatter BRDF loses energy at high roughness. Multiscatter compensation is required for metals to not look unnaturally dark.

| Engine | Multiscatter Method |
|--------|---------------------|
| UE5 | Kulla-Conty (2017) analytic fit |
| Unity HDRP | GGX multiple scattering LUT (Turquin 2019) |
| Godot 4 | Implicit via energy conservation term |
| **UzEngine** | Kulla-Conty + Turquin analytic fit ✓ |

**Verdict**: UzEngine multiscatter is CORRECT.

### 1.3 Shadow Quality

**Consensus**: CSM (4 cascades) + PCF filtering + contact shadows.

| Engine | Cascades | Filter | Contact Shadows |
|--------|----------|--------|-----------------|
| UE5 | 4 (default) | 16-tap Poisson PCF | Screen-space ray march, 16 steps |
| Unity HDRP | 4 (default) | PCF 5x5 | 8-step screen-space |
| Godot 4 | 4 (default) | PCF + soft shadow scale | PCSS-style via blocker search |
| **UzEngine** | 1-8 (default 4) | 5x5 PCF + 16-tap Poisson + PCSS ✓ | 8-step ray march ✓ |

**Verdict**: UzEngine shadows are AT OR ABOVE consensus quality. NOT the root cause.

### 1.4 Tonemapping

**Consensus**: ACES filmic curve (Narkowicz polynomial fit, same coefficients across all engines).

| Engine | Tonemapper | Coefficients |
|--------|-----------|--------------|
| UE5 | ACES fitted | a=2.51, b=0.03, c=2.43, d=0.59, e=0.14 |
| Unity HDRP | ACES filmic | Same coefficients |
| Godot 4 | ACES-like simplified | Similar curve |
| **UzEngine** | ACES filmic ✓ | a=2.51, b=0.03, c=2.43, d=0.59, e=0.14 ✓ |

**Verdict**: UzEngine tonemapping MATCHES consensus.

### 1.5 IBL (Image-Based Lighting)

**Consensus**: Split-sum IBL — diffuse irradiance map (SH or cubemap) + specular prefiltered cubemap + BRDF integration LUT.

| Engine | Diffuse | Specular | BRDF LUT |
|--------|---------|----------|----------|
| UE5 | 32x32 irradiance cubemap | 128x128, 5 mips | 256x256 RG16F |
| Unity HDRP | SH L2 (9 coefficients) | Prefiltered cubemap, 6 mips | Same split-sum LUT |
| Godot 4 | SH L2 per probe | Prefiltered per-roughness | Integrated in shaders |
| **UzEngine** | SH L2 7-coeff ✓ | 128x128 5 mips ✓ | 256x256 RG16F ✓ |

**Verdict**: UzEngine IBL matches consensus.

### 1.6 Post-Process Stack Order

**Consensus** (all three engines agree on this exact order):
```
HDR Render → Bloom → SSR Composite → Auto-Exposure → Tonemap → Color Grade → TAA/TSR → Sharpen → LDR Effects
```

UzEngine audit does NOT document whether SSR composite happens before or after bloom. SSR is MISSING, which means the pipeline order is currently:
```
HDR Render → Bloom → Auto-Exposure → Tonemap → Color Grade → TSR → Sharpen → LDR Effects
```
This is acceptable as a fallback but not the full pipeline.

### 1.7 Screen-Space Reflections

**Consensus**: All three engines have SSR as a PRIMARY reflection layer above probes.

| Engine | SSR Method | Status |
|--------|-----------|--------|
| UE5 | Hi-Z ray march + roughness fade | Default ON |
| Unity HDRP | Hi-Z pyramid trace + temporal accumulation | Default ON |
| Godot 4 | Screen-space ray march | Default ON |
| **UzEngine** | Declared but NOT IMPLEMENTED | **MISSING** |

**Verdict**: SSR is the SINGLE LARGEST MISSING FEATURE that affects visual quality on reflective surfaces.

### 1.8 Temporal Anti-Aliasing

**Consensus**: All three engines use TAA with Halton jitter as foundational anti-aliasing, on which SSR, SSGI, and contact shadows depend for quality.

| Engine | TAA | Jitter |
|--------|-----|--------|
| UE5 | TAA + TSR | Halton 2-3 sequence |
| Unity HDRP | TAA mandatory for temporal features | Halton, jitterStrength=1.0 |
| Godot 4 | TAA (optional) | Halton |
| **UzEngine** | TSR (good) + TAA minimal | TSR uses Halton ✓, TAA header-only |

**Verdict**: TSR present and functional. Full TAA with jitter needed for temporal features (SSR, SSGI).

---

## 2. UzEngine Gap Matrix

| Feature | UE5 | Unity HDRP | Godot 4 | UzEngine | Gap Level |
|---------|-----|------------|---------|----------|-----------|
| GGX BRDF | ✓ | ✓ | ✓ | ✓ | NONE |
| Burley Diffuse | ✓ | ✓ (partial) | ✓ | ✓ | NONE |
| Energy Conservation (multiscatter) | ✓ | ✓ | partial | ✓ | NONE |
| IBL Split-Sum | ✓ | ✓ | ✓ | ✓ | NONE |
| BRDF LUT 256x256 | ✓ | ✓ | ✓ | ✓ | NONE |
| CSM 4-cascade | ✓ | ✓ | ✓ | ✓ | NONE |
| PCF Filtering | ✓ | ✓ | ✓ | ✓ | NONE |
| PCSS Soft Shadows | ✓ (via VSM) | partial | ✓ | ✓ | NONE |
| Contact Shadows | ✓ | ✓ | ✓ | ✓ | NONE |
| ACES Tonemapping | ✓ | ✓ | ✓ | ✓ | NONE |
| Bloom (pyramid) | ✓ | ✓ | ✓ | ✓ | NONE |
| Auto-Exposure (histogram) | ✓ | ✓ | ✓ | ✓ | NONE |
| **Screen-Space Reflections** | ✓ | ✓ | ✓ | **MISSING** | **CRITICAL** |
| Motion Blur | ✓ | ✓ | ✓ | ✓ | NONE |
| GTAO / AO | ✓ (GTAO) | ✓ (HBAO+) | ✓ (SSAO) | ✓ (GTAO) | NONE |
| Sky Atmosphere (Bruneton) | ✓ | ✓ | ✓ | ✓ | NONE |
| Volumetric Fog | ✓ (froxels) | ✓ | ✓ | height fog only | MEDIUM |
| **Micro-shadows** | ✓ | ✓ | ✓ | **TODO (disabled)** | **HIGH** |
| **Full TAA** | ✓ | ✓ | ✓ | TSR only / TAA minimal | **MEDIUM** |
| **DOF** | ✓ | ✓ | ✓ | disabled | MEDIUM |
| Color Grading (LUT + per-range) | ✓ | ✓ | ✓ | minimal | LOW |
| Subsurface Scattering | ✓ | ✓ | ✓ | missing | LOW |
| Anisotropic Materials | ✓ | ✓ | ✓ | missing | LOW |
| Box Reflection Probes | ✓ | ✓ | ✓ | sphere only | LOW |
| Parallax Correction (probes) | ✓ | ✓ | partial | missing | LOW |
| Volumetric Clouds | ✓ | partial | no | ✓ (PoC2) | NONE |
| Aerial Perspective | ✓ | ✓ | no | ✓ | NONE |

---

## 3. Root Cause Analysis — Why Does the Current Render Look Bad?

The audit's own assessment is "GOOD" across most systems. But the user says the render looks terrible. This discrepancy must be resolved honestly.

### 3.1 Finding #1: SSR Absence Creates Flat, Unrealistic Surfaces

**Technical cause**: All metallic and rough surfaces (floors, wet stone, metal props) should show screen-space reflections. Without SSR, these surfaces fall back to the sky IBL cubemap (128x128, low-frequency). The result is that everything reflective appears to glow with a uniform sky color instead of reflecting its actual environment.

**Visual impact**: VERY HIGH. A shiny floor that doesn't reflect nearby columns, walls, or characters looks immediately "fake". This single absence makes every scene look like a 2015-era game.

**Consensus agreement**: 3/3 engines have SSR as default-on.

### 3.2 Finding #2: Micro-Shadow TODO Reduces Contact Realism

**Technical cause**: The micro-shadow system in `deferred_lighting_ps.hlsl:1038-1040` is disabled due to a suspected GTAO texture state conflict. This means small surface details — texture bumps, material surface irregularities — cast no self-shadowing. Every surface looks slightly "smooth" and plastic even at close range.

**Visual impact**: MEDIUM-HIGH. The difference between "looks like a game" and "looks real" is often sub-centimeter contact shadowing on surface details.

**Consensus agreement**: 3/3 engines have micro-shadows or an equivalent.

### 3.3 Finding #3: Exposure Value Is Off

**Technical cause**: `r.Exposure = 1.4` default. The standard middle-gray exposure target is 0.18 (18%). An exposure of 1.4 overbrightens the scene, compressing highlights and making the mid-tones look washed out. Combined with ACES filmic which already boosts highlights, this creates bloomy, overexposed appearances.

UE5 default: `AutoExposureBias = 0.0` (target middle gray 0.18, no manual offset)
UzEngine: Manual exposure 1.4 — 8× brighter than neutral.

**Visual impact**: HIGH. If the scene is overexposed, no amount of shadow or reflection work will make it look right. Everything is too bright and washed out.

**Consensus agreement**: 3/3 engines default to auto-exposure targeting 18% middle gray, NOT a manual 1.4× multiplier.

### 3.4 Finding #4: SkyLight Intensity 3.5 Is Too High for Correct IBL Balance

**Technical cause**: `r.SkyLight.Intensity = 3.5`. This is the IBL ambient multiplier applied to ALL indirect diffuse and specular lighting. Standard values across engines are 1.0–1.5 for outdoor scenes. At 3.5, the ambient IBL contribution floods into shadow regions, killing contrast and making shadows appear milky gray instead of dark and saturated.

UE5 default: SkyLight Intensity = 1.0
Unity HDRP: Ambient intensity = 1.0
Godot: Sky energy = 1.0
UzEngine: 3.5 — 3.5× too high.

**Visual impact**: HIGH. Overly bright ambient light kills shadow contrast. Dark corners should be dark. If every shadow is lit by 3.5× ambient, the entire scene loses depth and looks flat.

### 3.5 Finding #5: TAA Is Minimal — All Temporal Features Are Noisy

**Technical cause**: TAA is header-only with no jitter generation. Without proper sub-pixel jitter accumulation, every temporal feature (contact shadows with temporal dither, SSGI when enabled, TSR upsampling) accumulates incorrectly or not at all. Contact shadows use temporal jitter (`frameId * float2(47,17)`) but if TAA isn't resolving the jitter pattern, the result is a noisy, shimmering pattern.

**Visual impact**: MEDIUM. Moving the camera reveals shimmer on shadow edges, surface details, and any high-frequency texture content. Makes the renderer look "cheap" even when the underlying math is correct.

### 3.6 Summary: Top 5 Visual Impact Items

| Rank | Issue | Root Cause | Visual Impact |
|------|-------|------------|---------------|
| 1 | SSR MISSING | No implementation | All reflective surfaces look flat/fake |
| 2 | Exposure 1.4 (too high) | r.Exposure default too bright | Overexposed, washed out mid-tones |
| 3 | SkyLight 3.5 (too high) | IBL ambient multiplier 3.5× reference | Milky shadows, no contrast, flat depth |
| 4 | Micro-shadow TODO | GTAO SRV conflict, disabled | No surface contact shadowing |
| 5 | TAA minimal | No jitter resolve pass | Shimmer on edges, noisy temporal features |

### 3.7 Honest Assessment

The audit marked most systems as "GOOD" — and technically the BRDF math, IBL pipeline, shadow filtering, bloom, and tonemapping are correctly implemented. **The render looks bad not because the algorithms are wrong but because the exposure and IBL defaults are calibrated incorrectly**, and because the missing SSR means reflective surfaces fall back to low-frequency sky IBL.

A developer can implement perfect GGX with exact Kulla-Conty multiscatter, but if `r.Exposure = 1.4` and `r.SkyLight.Intensity = 3.5`, the scene will still look wrong. The math is right. The calibration is wrong.

**The render is fixable without writing new systems.** The two highest-impact changes are parameter adjustments that take minutes.

---

## 4. What Is NOT Wrong

These are NOT contributing to the visual quality issues and should not be changed:

- GGX specular distribution: CORRECT
- Burley diffuse: CORRECT
- Kulla-Conty multiscatter: CORRECT
- ACES tonemap coefficients: CORRECT
- CSM cascade count and quality: CORRECT
- Bloom pyramid: CORRECT
- GTAO ambient occlusion: CORRECT (except SRV conflict blocking micro-shadow)
- Sky atmosphere Bruneton: CORRECT
- Aerial perspective: CORRECT
- Reflection probes: CORRECT
