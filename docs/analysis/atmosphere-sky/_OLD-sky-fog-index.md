<!-- doc-meta
title: UE5 Sky Atmosphere to Height Fog Integration — Complete Analysis
created: 2026-03-19
category: rendering
tags: sky-atmosphere, height-fog, fog-rendering
-->

# UE5 Sky Atmosphere to Height Fog Integration — Complete Analysis

This analysis documents **exactly how UE5 5.6 connects SkyAtmosphere to ExponentialHeightFog** to produce the blue horizon haze effect. Every code reference includes file path and line number.

## Documents in This Analysis

### 1. **ue5-sky-atmosphere-fog-integration.md** — START HERE
**Comprehensive pattern documentation**

- Complete data flow from SkyAtmosphere component through rendering to GPU
- Where `SkyAtmosphereHeightFogContribution` is initialized (SkyAtmosphere.cpp:603)
- Where `AtmosphereLightIlluminanceOnGroundPostTransmittance` is computed (DirectionalLightComponent.cpp:605)
- Where `SkyAtmosphereAmbientContributionColorScale` is applied (FogRendering.cpp:267, 85)
- Visual data flow diagram showing all transformations
- Summary section for UzEngine adaptation with exact implementation steps

**Best for**: Understanding the complete integration pattern and high-level design.

---

### 2. **ue5-fog-shader-code-reference.md** — SHADER IMPLEMENTATION
**Exact HLSL code from HeightFogCommon.ush**

- Complete `ComputeInscatteringColor()` function (lines 140-183) with inline annotations
- `FogStructData` struct definition (lines 64-85)
- Directional light inscattering section (lines 313-325) showing how atmosphere colors are injected
- Final fog color assembly (lines 357-385)
- All preprocessor dependencies explained
- Utility functions and their purposes

**Best for**: Copy-pasting shader code and understanding shader-side calculations.

**Key code snippets ready to adapt**:
```hlsl
// Line 175-179: Add sky ambient
SkyAmbient = SkyAtmosphereAmbientContributionColorScale.rgb
           * View.SkyAtmosphereHeightFogContribution
           * GetViewDistanceSkyLightColor();
Inscattering += SkyAmbient;

// Line 319: Add directional atmosphere light
DirectionalInscatteringColor += SkyAtmosphereHeightFogContribution
                              * AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;
```

---

### 3. **ue5-fog-cpp-data-flow.md** — C++ IMPLEMENTATION
**Line-by-line C++ code flow from game thread to GPU**

**Phase breakdown**:
1. Scene proxy initialization (SkyAtmosphereComponent.cpp:603)
2. Atmosphere light transmittance computation (SkyAtmosphereRendering.cpp:454, 458)
3. View uniform setup (SceneRendering.cpp:1440, 1451)
4. Fog constants initialization (FogRendering.cpp:267, 85)
5. Light proxy storage of transmittance (DirectionalLightComponent.cpp:582-606)
6. Shader parameter binding (FogRendering.cpp:117-122)
7. GPU shader execution

**Complete data flow diagram** showing every transformation from component property to GPU register.

**Critical values table** with defaults and purposes.

**Best for**: Implementing the C++ rendering pipeline integration.

---

## Quick Summary: How UE5 Makes Fog Blue

The fog color at the horizon comes from **two layered mechanisms**:

### Layer 1: Ambient Sky Color (Lines 175-179 in shader)
The sky light's ambient color is blended into fog:
```hlsl
SkyAmbient = SkyAtmosphereAmbientContributionColorScale  // From fog component (default white)
           × SkyAtmosphereHeightFogContribution          // From sky component (default 1.0)
           × GetViewDistanceSkyLightColor();             // Actual sky light color
```

**Result**: Fog gets the sky's ambient color, modulated by a 0-1 blend factor.

### Layer 2: Directional Light After Atmosphere Filtering (Lines 313-325 in shader)
The sun's color is passed through the atmosphere (which scatters blue and absorbs red at horizon):
```hlsl
DirectionalInscatteringColor += SkyAtmosphereHeightFogContribution       // 0-1 blend
                              × AtmosphereLightIlluminanceOnGroundPostTransmittance[0]
```

Where:
```cpp
AtmosphereLightIlluminanceOnGroundPostTransmittance = SunColor × AtmosphereTransmittance
                                                    = Light.Color × LUT_Sample(sun_direction)
```

**Result**: Fog gets the sun's color after it's been filtered by atmospheric scattering (blue overhead, orange at horizon).

---

## Critical Implementation Points

### Three Parameters That Control the Effect

| Parameter | Source | Default | Controls |
|-----------|--------|---------|----------|
| `SkyAtmosphereHeightFogContribution` | SkyAtmosphereComponent | 1.0f | Master blend factor (0=disabled, 1=full) |
| `SkyAtmosphereAmbientContributionColorScale` | ExponentialHeightFogComponent | White (1,1,1,1) | Scales ambient sky contribution |
| `AtmosphereLightIlluminanceOnGroundPostTransmittance` | Computed from LUT | Dynamic | Sun color after atmospheric filtering |

### Computation Chain
```
SkyAtmosphereComponent (property: HeightFogContribution)
    └→ FSkyAtmosphereSceneProxy.HeightFogContribution
        └→ ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution
            └→ GPU shader: View.SkyAtmosphereHeightFogContribution

Atmosphere LUT sample (at sun direction)
    └→ FAtmosphereSetup.GetTransmittanceAtGroundLevel(direction)
        └→ SkyAtmosphereRendering.cpp:454
            └→ SetAtmosphereRelatedProperties(TransmittanceTowardSun)
                └→ DirectionalLightSceneProxy.TransmittanceTowardSun
                    └→ GetSunIlluminanceOnGroundPostTransmittance() = Color × Transmittance
                        └→ ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[0]
                            └→ GPU shader: InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0]
```

---

## Exact File References

| Step | File | Line | Code |
|------|------|------|------|
| Initialize proxy | SkyAtmosphereComponent.cpp | 603 | `HeightFogContribution = InComponent->HeightFogContribution;` |
| Compute transmittance | SkyAtmosphereRendering.cpp | 454 | `TransmittanceTowardSun = ...GetTransmittanceAtGroundLevel(...);` |
| Store in light proxy | SkyAtmosphereRendering.cpp | 458 | `SetAtmosphereRelatedProperties(TransmittanceTowardSun, ...);` |
| Copy to view uniform | SceneRendering.cpp | 1440 | `ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = ...GetHeightFogContribution();` |
| Set light illum. | SceneRendering.cpp | 1451 | `ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index] = ...GetSunIlluminanceOnGroundPostTransmittance();` |
| Enable for fog | SceneRendering.cpp | 1452 | `...AtmosphereLightIlluminanceOnGroundPostTransmittance[Index].A = 1.0f;` |
| Copy fog's scale | FogRendering.cpp | 267 | `View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;` |
| Pack to uniform | FogRendering.cpp | 85 | `OutParameters.SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;` |
| Compute result | DirectionalLightComponent.cpp | 605 | `return GetOuterSpaceIlluminance() × GetAtmosphereTransmittanceTowardSun();` |
| Apply in fog | HeightFogCommon.ush | 175 | `SkyAmbient = SkyAtmosphereAmbientContributionColorScale.rgb × View.SkyAtmosphereHeightFogContribution × GetViewDistanceSkyLightColor();` |
| Apply directional | HeightFogCommon.ush | 319 | `DirectionalInscatteringColor += SkyAtmosphereHeightFogContribution × AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;` |

---

## For UzEngine Implementation

### Step-by-step checklist:

1. **Add to SkyLight component**:
   - Property: `HeightFogContribution` (float, default 1.0f)
   - Pass to scene proxy

2. **In scene proxy (render thread)**:
   - Store `HeightFogContribution` from component
   - Copy to `ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution`

3. **In atmosphere rendering pass**:
   - Compute transmittance from LUT: `GetTransmittanceAtGroundLevel(sun_direction)`
   - Call on light proxy: `SetAtmosphereRelatedProperties(transmittance, luminance)`

4. **In view uniform setup**:
   - Set `ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[0] = SunColor × Transmittance`
   - Set alpha to 1.0f to enable in fog

5. **In fog shader**:
   - Add ambient: `FogColor += SkyAtmosphereHeightFogContribution × SkyAtmosphereAmbientContributionColorScale × SkyLightColor`
   - Add directional: `DirectionalColor += SkyAtmosphereHeightFogContribution × AtmosphereLightIlluminanceOnGroundPostTransmittance`

6. **Gate with preprocessor**:
   - `#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG` (both sides)

### Critical Numbers (No Approximations)
- Default `HeightFogContribution`: **1.0f** (not 0.5f, not tuned)
- Uniform phase function: **1.0f / (4.0f * PI)** (not 0.08f approximation)
- No inverse or compensation factors — all direct multiplies
- Transmittance is sampled from LUT, not computed analytically

---

## Verification in UE5 Source

To verify this analysis in actual UE5:
1. Open `W:/UE_5.6_Source/Engine/Shaders/Private/HeightFogCommon.ush`
2. Find line 175 (sky ambient contribution)
3. Find line 319 (directional atmosphere light)
4. Check line 375 (final fog color assembly)
5. Open `W:/UE_5.6_Source/Renderer/Private/SceneRendering.cpp`
6. Find line 1440, 1451, 1452 (view uniform setup)

All line numbers are exact for UE5.6.

---

## Why This Approach

UE5 uses this two-layer approach because:
1. **Ambient layer** (sky color) provides the **base haze tone** (blue overhead)
2. **Directional layer** (sun after transmittance) provides the **glow and color shift** (orange at horizon)
3. Both are **independently scalable** via `HeightFogContribution` and `SkyAtmosphereAmbientContributionColorScale`
4. Both use **transmittance from the same atmosphere model**, ensuring **visual consistency**

---

## Files Included

All analysis files are in `e:/UzEngine/docs/analysis/`:
- `ue5-sky-atmosphere-fog-integration.md` (high-level pattern)
- `ue5-fog-shader-code-reference.md` (shader code)
- `ue5-fog-cpp-data-flow.md` (C++ implementation)
- `SKY-ATMOSPHERE-FOG-INDEX.md` (this file)

---

## Next Steps

**For understanding**: Read `ue5-sky-atmosphere-fog-integration.md`

**For implementation**: Reference the exact line numbers in `ue5-fog-cpp-data-flow.md` and `ue5-fog-shader-code-reference.md`

**For validation**: Compare UzEngine implementation with actual UE5 source at the cited line numbers

