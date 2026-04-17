<!-- doc-meta
title: "Volumetric Fog Scene Darkening — Root Cause Analysis & Fix Plan"
description: "Diagnosis of why UzEngine's volumetric fog darkens the scene, with engine comparison and concrete fix plan"
tags: ["volumetric-fog", "energy-balance", "darkening", "diagnosis"]
status: "active"
author: "claude-code"
date: "2026-04-05"
-->

# Volumetric Fog Scene Darkening — Root Cause Analysis

## Problem Statement

After implementing volumetric fog, the scene appears noticeably darker — as if it's
always overcast/cloudy. The fog absorbs light but doesn't add enough back, creating a
net energy loss that darkens everything.

## Root Causes Identified (3 issues)

### Issue 1: HARDCODED AMBIENT — No Sky Irradiance Injection (CRITICAL)

**File**: `runtime/shaders/runtime/atmosphere/volumetric_fog_inject_cs.hlsl:114`

```hlsl
// CURRENT (WRONG):
float3 ambientFogColor = albedo * 0.3;  // hardcoded 30% — arbitrary, too low
float3 ambientInscatter = ambientFogColor * extinction * M_1_PI * 0.25;
```

**The problem**: The ambient term is a hardcoded `albedo * 0.3` constant. It doesn't
sample the actual sky irradiance, sky light color, or environment lighting. This means:
- In bright sunny scenes, the fog ambient is too low (30% of albedo vs real sky)
- The fog doesn't respond to time-of-day or sky color changes
- Shadowed areas inside fog receive almost no light → scene darkens

**How UE5/Frostbite do it**:
```hlsl
// UE5 VolumetricFog.usf — sky light injection
float3 skyIrradiance = GetSkyLightReflection(worldPos);
float3 skyInscatter = albedo * extinction * skyIrradiance * (1.0 / (4.0 * PI));
// + point lights, spot lights, etc.
```

Frostbite (Hillaire 2016):
```hlsl
// Sky contribution per froxel
sky_irradiance = SampleSkyIrradiance(froxelWorldPos);
sky_factor = 1.0 - exp(-extinction * sliceThickness);
inscattering += sky_irradiance * sky_factor;
```

**Fix**: Replace the hardcoded `0.3` with actual sky light color from the CB. The
`sunColor` and `sunIntensity` are already in VolumetricFogCB, but sky/ambient light
color is NOT. Need to add `skyLightColor` (from SkyLightComponent) to VolumetricFogCB
and use it for ambient inscattering.

### Issue 2: DEFAULT DENSITY TOO HIGH (0.15 vs UE5's 0.02)

**File**: `runtime/engine/ecs/environment/height_fog_component.h:41`

```cpp
// CURRENT:
float density = 0.15f;  // 7.5x higher than UE5 default!

// UE5 DEFAULT:
float FogDensity = 0.02f;  // Very subtle, barely visible
```

**The problem**: UzEngine's default fog density is 7.5x higher than UE5. At 0.15,
even moderate distances accumulate significant extinction. Combined with the weak
ambient (Issue 1), the scene loses a lot of light.

**Reference defaults**:
| Engine      | Default Density | Notes                           |
|-------------|----------------|---------------------------------|
| UE5         | 0.02           | Very conservative, barely visible |
| Frostbite   | 0.01-0.05      | Scene-dependent                 |
| CryEngine   | 0.01-0.2       | Artist-tuned range              |
| **UzEngine**| **0.15**       | Too high for default            |

### Issue 3: EMISSIVE NOT DENSITY-SCALED (minor)

**File**: `runtime/shaders/runtime/atmosphere/volumetric_fog_inject_cs.hlsl:117`

```hlsl
// CURRENT:
float3 inscatter = sunInscatter + ambientInscatter + emissive * density;
```

This is actually reasonable (emissive is density-scaled), but since `emissive` defaults
to `(0,0,0)` in the component, this term contributes nothing. In UE5, when
`PROJECT_EXPFOG_MATCHES_VFOG` is enabled, the emissive term acts as a multi-scattering
approximation — it compensates for light lost in shadowed areas.

**Fix**: Consider providing a small default emissive based on sky color, or add a
multi-scatter boost factor.

---

## How Production Engines Prevent Darkening

### 1. UE5 — Three-Tier Energy Balance

| Tier | Mechanism | Effect |
|------|-----------|--------|
| 1 | Conservative density (0.02) | Fog barely affects brightness by default |
| 2 | Albedo = 1.0 (pure scattering) | No light absorbed, only redirected |
| 3 | Sky light injection per froxel | Ambient fills shadowed fog areas |
| 4 | Emissive = density-scaled compensation | Multi-scatter approximation |

**Key insight**: UE5 injects ALL light sources into each froxel:
- Directional light (sun) × shadow × phase function
- Sky light (SH or cubemap) × isotropic phase (1/4pi)
- Point/spot lights × attenuation × shadow × phase
- Emissive (density-scaled ambient boost)

### 2. Frostbite — Hillaire 2016

@ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite"

- Uses **scattering albedo >= 0.95** for water fog
- Injects sky irradiance as: `sky_factor = 1 - exp(-extinction * depth_step)`
- Multi-scatter boost: `mix(1.0, 2.0, phaseAnisotropy)` — low g gets more boost
- Phase function g = 0.6 for atmospheric fog (not too directional)

### 3. CryEngine — Split Anisotropy

@ref Wronski 2014 "Volumetric Fog: Unified Compute Shader Based Solution"

- **Two separate anisotropy values**:
  - Atmosphere (global): g = 0.2-0.4 → isotropic, fills shadows
  - Radial (sun bloom): g = 0.9 → concentrates sun glow forward
- Ambient boost: `(1 + anisotropy_radial)` factor
- This lets artists have dramatic sun shafts without shadow darkening

---

## Concrete Fix Plan

### Fix A: Add Sky Light Injection (HIGH PRIORITY)

1. **VolumetricFogCB** — add fields:
   ```cpp
   float skyLightR, skyLightG, skyLightB;  // From SkyLightComponent
   float ambientBoost;                      // Multi-scatter compensation factor
   ```

2. **frame_atmosphere_passes.cpp** — fill from SkyLightComponent:
   ```cpp
   fogCB.skyLightR = skyLightColor.x * skyLightIntensity;
   fogCB.skyLightG = skyLightColor.y * skyLightIntensity;
   fogCB.skyLightB = skyLightColor.z * skyLightIntensity;
   fogCB.ambientBoost = 1.0f; // default: no extra boost
   ```

3. **volumetric_fog_inject_cs.hlsl** — replace hardcoded ambient:
   ```hlsl
   // NEW: sample actual sky light
   float3 skyLight = float3(skyLightR, skyLightG, skyLightB);
   float isotropicPhase = 1.0 / (4.0 * PI);
   
   // Multi-scatter boost: low anisotropy → more isotropic → more ambient needed
   float multiScatterBoost = lerp(2.0, 1.0, abs(phaseG));
   
   float3 ambientInscatter = albedo * extinction * skyLight * isotropicPhase * multiScatterBoost;
   ```

### Fix B: Lower Default Density (MEDIUM PRIORITY)

In `height_fog_component.h`:
```cpp
float density = 0.02f;  // Match UE5 default (was 0.15)
```

And lower `baseHeight` or adjust `heightFalloff` so fog is still subtly visible:
```cpp
float heightFalloff = 0.2f;  // Keep — matches UE5
float baseHeight = 0.0f;     // Ground level (was -5000, which pushes dense fog underground)
```

### Fix C: Add Multi-Scatter Approximation (LOW PRIORITY)

Frostbite-style multi-scatter boost in the inject shader:
```hlsl
// Approximate multiple scattering: photons that scatter more than once
// contribute additional ambient light, especially at low anisotropy.
// @ref Hillaire 2016 — multi-scatter factor
float multiScatterFactor = lerp(2.0, 1.0, abs(phaseG));
float3 totalInscatter = sunInscatter + ambientInscatter * multiScatterFactor + emissive * density;
```

### Fix D: Expose CVar for Quick Tuning (OPTIONAL)

```cpp
static auto* cv_FogAmbientBoost = UZ_CVAR_FLOAT("r.Fog.AmbientBoost", 1.0f, "Volumetric fog ambient multiplier");
```

This lets artists quickly adjust without recompiling while dialing in the energy balance.

---

## Expected Visual Impact

| Before Fix | After Fix |
|-----------|-----------|
| Scene darkens with fog | Fog adds haze without net brightness loss |
| Shadowed areas go nearly black | Shadows retain ambient illumination |
| Looks "always overcast" | Looks like natural atmospheric perspective |
| Fog color disconnected from sky | Fog color matches sky/environment lighting |

---

## Parameter Quick Reference

### Defaults That Prevent Darkening

| Parameter | Recommended Default | Why |
|-----------|-------------------|-----|
| `density` | 0.02 | Conservative — artist increases explicitly |
| `albedo` | (1, 1, 1) | Pure scattering — no energy absorbed |
| `phaseG` | 0.2 | Low anisotropy — light spreads evenly |
| `extinctionScale` | 1.0 | No amplification |
| `emissive` | (0, 0, 0) | Disabled by default, artist-tuned |
| `skyLight` | From SkyLightComponent | Actual environment lighting |
| `ambientBoost` | 1.0 | Multi-scatter compensation |

### Henyey-Greenstein g Values

| g Value | Behavior | Use Case |
|---------|----------|----------|
| 0.0 | Isotropic (even scatter) | Maximum shadow fill, soft fog |
| 0.2 | Slightly forward | **Recommended default** — balanced |
| 0.5-0.6 | Forward scatter | Standard atmospheric fog |
| 0.8-0.9 | Strong forward | Sun shafts, dense clouds |

**Key**: Lower g = more ambient light in shadows = less darkening.
The default 0.2 is correct for UzEngine — it prevents the "overcast" look.

---

## Related Analysis Documents

- [volumetric-fog-energy-balance.md](volumetric-fog-energy-balance.md) — Frostbite/CryEngine deep dive
- [volumetric-fog-implementation-guide.md](volumetric-fog-implementation-guide.md) — UE5 parameter mapping

## References

- @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite" SIGGRAPH 2015
- @ref Wronski 2014 "Volumetric Fog: Unified Compute Shader Based Solution" SIGGRAPH 2014
- @ref Bruneton 2017 "Precomputed Atmospheric Scattering" — sky irradiance for fog ambient
