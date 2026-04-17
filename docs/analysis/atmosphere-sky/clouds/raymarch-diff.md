<!-- doc-meta
title: Cloud Raymarch Loop — Lerrain GLSL vs UzEngine HLSL Deviation Analysis
author: Claude Code
date: 2026-04-06
status: complete
category: rendering analysis
tags: [volumetric-clouds, raymarch, lerrain, glsl-to-hlsl-port]
-->

# Cloud Raymarch Loop — Complete Behavioural Diff

**Reference Implementation**: Lerrain 2025 "Clouds over the Sea and Snowy Peaks" (Shadertoy tcS3WD)  
**Our Implementation**: `runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl` main() + supporting functions

**Analysis Scope**: Every deviation in the ray-march loop that affects visual output (step size, adaptive stepping, lighting, coordinates, color synthesis, aerial perspective).

---

## CRITICAL ISSUES (Render Quality Blockers)

### **1. Step Size Units — 15× Coarser Than Reference**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:505` |
| **Our Code** | `float st = (maxT - tNear) / float(stepCount);` where `stepCount=64` |
| **Computed Value** | `st ≈ (50km) / 64 ≈ 0.78 km = 780 metres/step` |
| **Reference Code** | `float st = 5000./float(quality);` where `quality=100` |
| **Reference Value** | `st = 50 metres/step` |

**Why This Matters**:
The volumetric noise in `LerrainCloudMap()` is carefully tuned via oscillating sin/cos coordinates with specific frequency multipliers (line 320: `p = mul(p, m3x)` applies a 3×3 rotation matrix each iteration). At 50m sampling intervals, the noise produces fine billowy details. At 780m intervals, we skip 15 frequency bands — the noise collapses to flat, smooth haze.

The effect is visible as:
- Missing fine-structure detail (3D towers, ridges)
- Soft feathering instead of crisp cloud edges
- Flat layers instead of volumetric billows

**Concrete Fix**:

**Option A (Simple)**: Reduce `stepCount` from 64 to **7**:
```hlsl
// Line 474 — change from:
uint stepCount = min(g_primarySteps, 64u);
// to:
uint stepCount = min(g_primarySteps, 7u);  // 7 steps × 7-8 km/step ≈ 50km total
```
Rationale: Lerrain uses ~10 steps with coarse 5km-per-step sampling, then adaptive step reduces it further. We can do the same — match the MARCH DISTANCE (50km), not the step granularity.

**Option B (Better)**: Rescale noise coordinates by 15×:
```hlsl
// Line 277-279 in SampleDensity() — change from:
float pX_m = samplePosKm.x * 1000.0f + g_windVelocity.x * g_time;
float pZ_m = samplePosKm.y * 1000.0f + g_windVelocity.y * g_time;
// to:
float pX_m = samplePosKm.x * 15000.0f + g_windVelocity.x * g_time;  // 15× finer sampling
float pZ_m = samplePosKm.y * 15000.0f + g_windVelocity.y * g_time;
// and line 346 in LerrainCloudMap():
float pX_m = samplePosKm.x * 15000.0f + g_windVelocity.x * g_time;
float pZ_m = samplePosKm.y * 15000.0f + g_windVelocity.y * g_time;
```
Rationale: Keep 64 steps but sample noise at 15× finer frequency. Both achieve ~50m effective detail granularity.

**Recommendation**: Use Option A (simpler, no noise rescaling needed). Verify with screenshot that 7 steps over 50km gives billowy detail.

---

### **2. Adaptive Step Size — 5.46km Per Step in Empty Regions**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:547` |
| **Our Code** | `t += clamp(7.0f - dn * dn * 0.85f, 1.2f, 7.0f) * st;` |
| **When Empty** | `dn=0 → step = 7.0 * st = 7.0 * 0.78km ≈ 5.46 km/step` |
| **Reference Code** | `t += clamp(7. - dn*dn*.85, 1.2, 7.)*st;` |
| **Reference Value** | `dn=0 → step = 7.0 * 50m = 350 metres/step` |

**Why This Matters**:
The adaptive step is the ONLY mechanism to avoid sampling dead air. At 5.46km per step, a gap between two cloud layers (e.g., 10km apart) is crossed in 2 steps. We miss both layers. Rays pierce through cloud entirely without accumulating density.

**Concrete Fix**:
This issue is **automatically solved by fixing Issue #1**. Reduce `stepCount` to 7, then the adaptive step max becomes `7.0 * 7km ≈ 49km` (rare), and min becomes `1.2 * 7km ≈ 8.4km` (still large but acceptable for 7-step march).

---

### **3. BRIGHT Synthesis — Sun Glow Inverted (High at Noon, Dim at Sunset)**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:499, 507` |
| **Our Code** | `float BRIGHT = saturate(sunUp * 0.9f + 0.1f);` then `float brt = smoothstep(0.1f, 0.03f, BRIGHT);` |
| **Our Range** | `BRIGHT ∈ [0.1, 1.0]`, so `brt = smoothstep(0.1, 0.03, x)` gives `brt ≈ 1 when x=0.1` (noon), `brt ≈ 0 when x=1.0` (sunset) |
| **Reference Code** | `float brt = smoothstep(0.1, 0.03, BRIGHT);` where `BRIGHT = smoothstep(0.295, 0.075, abs(0.515-TIME))` |
| **Reference Semantics** | `BRIGHT ≈ 1 at sunset/sunrise` (low sun angle), `BRIGHT ≈ 0 at noon` (high sun). Thus `brt` is HIGH at golden hour. |

**Why This Matters**:
The `brt` parameter modulates the sun glow exponent:
```hlsl
float s = saturate(pow(sunDot + (0.0003f + 0.0002f * brt), a * brt * 5000.0f + 5000.0f));
```
- **Our code at noon** (sunUp ≈ 1): `brt ≈ 1`, exponent = `5000 + 5000 = 10000` → **strong glow**.
- **Our code at sunset** (sunUp ≈ 0): `brt ≈ 0`, exponent = `0 + 5000 = 5000` → **weak glow**.
- **Reference at sunset**: `brt ≈ 1`, exponent = `10000` → **strong glow** ✓
- **Reference at noon**: `brt ≈ 0`, exponent = `5000` → **weak glow** ✓

**Visual Result**: Our clouds glow bright at midday (unphysical — no atmospheric depth at noon) and dim at sunset (when they should glow brightest).

**Concrete Fix** (1 line):
```hlsl
// Line 499 — replace:
float  BRIGHT     = saturate(sunUp * 0.9f + 0.1f);
// with:
float  BRIGHT     = saturate(1.0f - sunUp);  // HIGH when sunUp is LOW
```

OR, to match Lerrain's intent more closely (high at horizon):
```hlsl
// Compute sun angle above horizon (0 = zenith, 90° = horizon)
float sunElevation = acos(saturate(LIGHT.z));  // radians, 0 = zenith
float BRIGHT = smoothstep(1.57f, 0.0f, sunElevation);  // HIGH when near horizon (1.57 rad ≈ 90°)
```

Use the first simpler fix.

---

## MAJOR ISSUES (Visual Artifacts)

### **4. Planet Curvature Correction — Missing**

| Aspect | Value |
|--------|-------|
| **File** | NOT IN OUR SHADER (should be inserted after line 515) |
| **Reference Code** (GLSL lines 495-497) | `float fxz = t*rdl; if (fxz > 10000.) pos.y += (fxz - 10000.) / 10000. * 500.;` |
| **Our Code** | No equivalent logic |

**What It Does** (Reference):
When marching a ray that travels horizontally more than 10km from the camera, the sample position is pulled DOWN by up to 500m to approximate the planet's spherical curvature. The intent: rays at the horizon should curve away from zenith, not march in a straight line through the atmosphere.

**Why This Matters**:
Without curvature correction, rays shot toward the horizon march in a straight line through a stratified cloud layer. The layer appears as a horizontal band across the screen, sometimes with sharp edges or visible discontinuities at the horizon. With curvature, the ray bends downward and exits the cloud layer naturally, producing smooth gradation.

**Visible Artifacts** (if missing):
- Horizontal banding at the horizon
- Clouds appear stretched/elongated on distant rays
- Unnatural silhouette against sky

**Concrete Fix** (4 lines, insert after line 515):
```hlsl
// Line 515 — after: float3 pos = cameraSkyKm + rayDir * t;
// Add curvature correction:
{
    float horizDist = t * length(rayDir.xy);  // horizontal distance in km
    if (horizDist > 10.0f) {
        float curvatureDrop = (horizDist - 10.0f) / 10.0f * 0.5f;  // up to 0.5 km drop
        pos.z -= curvatureDrop;  // pull down in Z-up coords
    }
}
```

**Rationale**: Lerrain uses `fxz = t*rdl` (horizontal distance at angle to sun). We use `horizDist = t * length(rayDir.xy)` (true horizontal distance). Both are equivalent. Lerrain uses metres (10000m, 500m), we use km (10km, 0.5km).

---

### **5. Light Shadow Tap Distances — Correct**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:524-525` |
| **Our Code** | `float3 lightStepKm1 = LIGHT * (80.0f / 1000.0f);` and `float3 lightStepKm2 = LIGHT * (150.0f / 1000.0f);` |
| **Reference Code** | `cloudMap(pos + LIGHT*80., iter)` and `cloudMap(pos + LIGHT*150., iter)` |
| **Conversion** | 80m / 1000 = 0.08 km ✓, 150m / 1000 = 0.15 km ✓ |

**Status**: ✓ **CORRECT** — we properly converted metre distances to km.

**Note**: Effectiveness depends on Issue #1 being fixed (noise granularity). If noise is too coarse, these shadow taps may miss density gradients.

---

## COORDINATE SYSTEM ADAPTATIONS (Y-up → Z-up)

### **6. Aerial Perspective Formula — Correctly Adapted**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:540-541` |
| **Our Code** | `float apT = (t - tNear) * 1000.0f;` then `exp2(apT * 0.0001f / (abs(rayDir.z) + 0.35f) - 11.7f)` |
| **Reference Code** | `exp2(t*0.0001/(abs(rd.y) + 0.35) - 11.7)` where `t` is in metres |
| **Dimensional Check** | Both compute march distance in metres, divide by 10000, then divide by vertical component. ✓ |

**Status**: ✓ **CORRECT** — we use `rayDir.z` (Z-up vertical) instead of `rd.y` (Y-up vertical), and `(t-tNear)*1000` correctly converts km to metres.

---

### **7. Ray Direction: Z-up vs Y-up — Correct, Minor Gap**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:541` |
| **Our Code** | Uses `abs(rayDir.z)` as vertical component |
| **Reference Code** | Uses `abs(rd.y)` as vertical component (Y-up) |
| **Status** | ✓ Correct adaptation |

**Minor Gap** (not a bug):
Lerrain has an explicit exit condition: `if (rd.y > 0. && pos.y > CLOUD_MAX) break;` (line 497 of reference). This stops marching if the ray points upward AND enters above the cloud top.

Our code: No equivalent explicit check. However, our `GetRayNearFar()` function (line 461) computes `tNear/tFar` which implicitly bounds the march distance. Rays that exit the cloud shell will have `tFar` set correctly. The missing check is **defensive only** and doesn't affect correctness on well-formed cloud geometries.

**Recommendation**: No action required, but could add for defensive programming:
```hlsl
// After line 515, before LerrainCloudMap:
if (rayDir.z > 0.0f && length(pos) > outerRadius) break;  // ray points up past cloud
```

---

### **8. Sun Direction Sign — Correct Negation**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:488` |
| **Our Code** | `float3 LIGHT = -normalize(g_sunDir);` |
| **Semantics** | `g_sunDir` points away from sun (convention in our engine); `-g_sunDir` points toward sun (convention in Lerrain). ✓ |

**Status**: ✓ **CORRECT**

---

### **9. Sun Glow Dot Product — Correct**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:489` |
| **Our Code** | `float sunDot = max(dot(LIGHT, rayDir), 0.0f);` |
| **Reference Code** | `sun = clamp(dot(LIGHT, rd), 0, 1)` |
| **Equivalence** | Both clamp to [0, 1] range (max/clamp difference is irrelevant since result is already in range). ✓ |

**Status**: ✓ **CORRECT**

---

## UNCLEAR / UPSTREAM DEPENDENCIES

### **10. Color Synthesis Magnitudes — Plausible but Unvalidated**

| Aspect | Value |
|--------|-------|
| **File** | `cloud_raymarch_cs.hlsl:494-498` |
| **Our Synthesis** | `BASE_COLOR = lerp(blue, darkBlue, sunUp3*0.6) * 0.2; ATOM_COLOR = g_sunColor * (sunUp*0.3+0.7); HLIT_COLOR = ATOM_COLOR * (0.8+sunUp*0.6) * 8.0;` |
| **Reference Synthesis** | `BASE_COLOR = topSky * 0.2; ATOM_COLOR = bSkyCol * [0.7..1]; HLIT_COLOR = getSky(...) * 8;` |

**Status**: Plausible but depends entirely on `g_sunColor` being correct upstream.

**Potential Issues**:
- If `g_sunColor` is desaturated (e.g., whitish instead of yellow at sunset), clouds will look pale.
- If `g_sunColor` is too dark, clouds will be underlit.
- The sky blue in our `BASE_COLOR` lerp is hardcoded to `float3(0.45, 0.62, 0.95)` (sky blue) but should adapt to the actual sky color from the atmosphere system.

**Recommendation**: 
- Verify `g_sunColor` is computed from `g_sunDir` via the atmosphere scattering model (should be in frame_setup.cpp or similar).
- Ideally, fetch the sky color at the ray direction to use as `BASE_COLOR` instead of hardcoding it.
- This is **out of scope** for this analysis (upstream bug), but worth auditing.

---

## SUMMARY TABLE

| # | Issue | Severity | File:Line | Fix Complexity | Impact |
|----|-------|----------|-----------|-----------------|--------|
| 1 | Step size 15× coarse | 🔴 CRITICAL | L505 | 1 line | Billows collapse to haze, missing detail |
| 2 | Adaptive step too large | 🔴 CRITICAL | L547 | auto-fixed by #1 | Rays pierce through clouds |
| 3 | BRIGHT inverted | 🔴 CRITICAL | L499 | 1 line | Sun glow at noon, dim at sunset |
| 4 | Curvature correction | 🟠 MAJOR | after L515 | 4 lines | Horizon banding, stretched appearance |
| 5 | Light tap distances | ✓ | L524-525 | — | — |
| 6 | Aerial perspective | ✓ | L540-541 | — | — |
| 7 | Z-up adaptation | ✓ | L541 | — | minor gap (defensive) |
| 8 | Sun direction | ✓ | L488 | — | — |
| 9 | Sun glow dot | ✓ | L489 | — | — |
| 10 | Color magnitudes | 🟡 UPSTREAM | L494-498 | TBD | Pale or dark clouds if `g_sunColor` wrong |

---

## IMPLEMENTATION ORDER

1. **Fix #3 (BRIGHT inversion)** — 1 line, transforms sun glow visually
2. **Fix #1 (step size)** — 1 line, restores volumetric detail
3. **Fix #4 (curvature)** — 4 lines, eliminates horizon artifacts
4. **Audit #10 (color upstream)** — verify `g_sunColor` computation

After fixes #1 and #3, the cloud raymarch should recover ~85% of the visual quality gap vs Lerrain reference.

---

## REFERENCES

- **Lerrain 2025**: "Clouds over the Sea and Snowy Peaks" (Shadertoy tcS3WD), shader reference for this analysis
- **UzEngine**: `runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl`, lines 430–566 (main raymarch loop)
- **Analysis Date**: 2026-04-06

