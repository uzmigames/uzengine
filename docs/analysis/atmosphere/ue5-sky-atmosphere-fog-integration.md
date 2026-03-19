<!-- doc-meta
title: UE5 SkyAtmosphere to ExponentialHeightFog Integration Pattern
created: 2026-03-19
category: rendering
tags: sky-atmosphere, height-fog, atmospheric-lighting
-->

# UE5 SkyAtmosphere to ExponentialHeightFog Integration Pattern

## Overview

UE5 connects the SkyAtmosphere component to ExponentialHeightFog via two mechanisms:

1. **Ambient color contribution** via `SkyAtmosphereAmbientContributionColorScale`
2. **Directional light transmittance** via `AtmosphereLightIlluminanceOnGroundPostTransmittance`
3. **Height fog blend factor** via `SkyAtmosphereHeightFogContribution`

This document extracts the exact data flow and calculation sequences from UE5 5.6 source code.

---

## 1. Fog Initialization: Setting SkyAtmosphereAmbientContributionColorScale

### Source: `FogRendering.cpp:267`

```cpp
View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/FogRendering.cpp:267`

**Context**: `FSceneRenderer::InitFogConstants()` copies the fog component's `SkyAtmosphereAmbientContributionColorScale` property to the view for shader consumption.

**What it does**: This is a scale factor (typically a color multiplier like `(1, 1, 1, 1)`) that controls how much of the sky atmosphere's ambient color contributes to the fog color.

---

## 2. Fog Shader Parameters: Passing to HLSL

### Source: `FogRendering.cpp:85`

```cpp
OutParameters.SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/FogRendering.cpp:85`

**Context**: `SetupFogUniformParameters()` packs the view's `SkyAtmosphereAmbientContributionColorScale` into the fog uniform buffer struct `FFogUniformParameters` for the pixel shader.

**Struct definition**: Line 70 in `HeightFogCommon.ush`:
```hlsl
float4 SkyAtmosphereAmbientContributionColorScale;
```

---

## 3. Fog Color Computation: ComputeInscatteringColor

### Source: `HeightFogCommon.ush:140-183`

**Function signature** (line 140):
```hlsl
float3 ComputeInscatteringColor(float3 CameraToReceiver, float CameraToReceiverLength, FogStructData FogData, uint EyeIndex)
{
    half3 Inscattering = FogData.ExponentialFogColorParameter.xyz;  // Line 142

    // ... cubemap texture sampling (lines 144-172) ...

    // Sky atmosphere contribution (lines 174-180)
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
    float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb
                      * View.SkyAtmosphereHeightFogContribution.xxx
                      * GetViewDistanceSkyLightColor();
    #if PROJECT_EXPFOG_MATCHES_VFOG
    SkyAmbient *= FogData.VolumetricFogAlbedo;
    #endif
    Inscattering += SkyAmbient;  // Line 179
#endif

    return Inscattering;
}
```

**Key lines**:
- **174-180**: The sky atmosphere color is computed by multiplying three factors:
  1. `SkyAtmosphereAmbientContributionColorScale` (the fog's sky contribution scale)
  2. `View.SkyAtmosphereHeightFogContribution` (the atmosphere's height fog blend factor, 0-1)
  3. `GetViewDistanceSkyLightColor()` (the sky light's ambient color sampled from the view)

---

## 4. Height Fog Composition: Final Color Assembly

### Source: `HeightFogCommon.ush:375`

```hlsl
half3 FogColor = (InscatteringColor) * (1 - ExpFogFactor) + DirectionalInscattering;
```

**File**: `W:/UE_5.6_Source/Engine/Shaders/Private/HeightFogCommon.ush:375`

**Context**: Inside `GetExponentialHeightFog()`, the final fog color is a blend of:
1. **Inscattering color** (ambient + sky atmosphere + cubemap): `InscatteringColor * (1 - ExpFogFactor)`
2. **Directional inscattering** from atmosphere lights: `DirectionalInscattering`

The `InscatteringColor` computed at line 301 includes the sky atmosphere contribution from `ComputeInscatteringColor()`.

---

## 5. Directional Light Inscattering from Sky Atmosphere

### Source: `HeightFogCommon.ush:313-325`

```hlsl
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
    const float UniformPhaseFunction = 1.0f / (4.0f * PI);
    half3 DirectionalInscatteringColor;
    half3 DirectionalLightInscattering;

    // Line 319: Atmosphere light illuminance on ground AFTER transmittance through atmosphere
    DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
                                 + InView.SkyAtmosphereHeightFogContribution
                                 * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;

    DirectionalLightInscattering = DirectionalInscatteringColor
    #if PROJECT_EXPFOG_MATCHES_VFOG
            * HenyeyGreensteinPhase(FogData.VolumetricFogPhaseG, dot(-CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz));
    #else
            * pow(saturate(dot(CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz)), FogData.DirectionalInscatteringColor.w) * UniformPhaseFunction;
    #endif
```

**Key line 319**:
- The directional inscattering color is augmented with:
  - `SkyAtmosphereHeightFogContribution` (0-1 blend factor)
  - `AtmosphereLightIlluminanceOnGroundPostTransmittance[0]` (atmosphere light color after transmittance)

This is the mechanism that gives fog its **blue color when looking toward/away from the sun**.

---

## 6. AtmosphereLightIlluminanceOnGroundPostTransmittance: Where It's Set

### Source: `SceneRendering.cpp:1451`

```cpp
ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index]
    = Light->Proxy->GetSunIlluminanceOnGroundPostTransmittance();
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp:1451`

**Context**: Inside the loop at line 1443 that fills atmosphere light shader parameters for each atmosphere light (up to 2).

**Line 1452**: Also sets the alpha channel to 1.0f to indicate "this light has sky atmosphere enabled and should affect height fog".

---

## 7. AtmosphereLightIlluminanceOnGroundPostTransmittance: Calculation

### Source: `DirectionalLightComponent.cpp:603-606`

```cpp
virtual FLinearColor GetSunIlluminanceOnGroundPostTransmittance() const override
{
    return GetOuterSpaceIlluminance() * GetAtmosphereTransmittanceTowardSun();
}
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp:603-606`

**Calculation**:
- `GetOuterSpaceIlluminance()` → Returns the directional light's color (line 593-595)
- `GetAtmosphereTransmittanceTowardSun()` → Returns transmittance from space to ground (line 598-601)

**Result**: The light color after it has been filtered/attenuated by atmospheric transmittance (Rayleigh scattering, aerosol absorption, etc.).

---

## 8. GetAtmosphereTransmittanceTowardSun: Where It's Computed

### Source: `SkyAtmosphereRendering.cpp:454`

```cpp
const FLinearColor TransmittanceTowardSun = SkyAtmosphereSceneProxy.GetAtmosphereSetup()
    .GetTransmittanceAtGroundLevel(AtmosphereLightDirection);
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp:454`

**Context**: Function `UpdateLightSunPropertiesFromAtmosphere()` at line 449.

**Calculation method**:
- Calls `FAtmosphereSetup::GetTransmittanceAtGroundLevel(FVector LightDirection)`
- This samples the **atmosphere transmittance LUT** (pre-computed or sampled from the sky atmosphere setup)
- Returns a `FLinearColor` representing how much light makes it from space to ground level along that direction

### Source: `SkyAtmosphereRendering.cpp:458`

```cpp
AtmosphereLight.Proxy->SetAtmosphereRelatedProperties(TransmittanceTowardSun, SunDiskOuterSpaceLuminance);
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp:458`

This stores the computed transmittance in the light proxy so it can be retrieved later during view uniform setup.

---

## 9. SkyAtmosphereHeightFogContribution: Where It's Set

### Source: `SkyAtmosphereComponent.cpp:603`

```cpp
HeightFogContribution = InComponent->HeightFogContribution;
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Components/SkyAtmosphereComponent.cpp:603`

**Context**: Inside `FSkyAtmosphereSceneProxy` constructor that reads from a `USkyAtmosphereComponent`.

**What it is**: A float property on the SkyAtmosphere component (default 1.0f at line 585: `HeightFogContribution = 1.0f;`). Controls how much the sky atmosphere affects the fog color (0 = no effect, 1 = full effect).

### Source: `SceneRendering.cpp:1440`

```cpp
ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = SkyAtmosphereSceneProxy.GetHeightFogContribution();
```

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp:1440`

This copies the proxy's `HeightFogContribution` to the view uniform parameters for shader access.

---

## 10. SkyAtmosphereAmbientContributionColorScale: Where It Comes From

The `SkyAtmosphereAmbientContributionColorScale` is a property on the `ExponentialHeightFogComponent`:

**Header location**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Classes/Components/ExponentialHeightFogComponent.h`

**Set at line 267 in FogRendering.cpp**:
```cpp
View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;
```

This is a **per-fog-component** parameter that scales how much the ambient sky light contributes to the fog's inscattering color.

---

## Data Flow Diagram

```
SkyAtmosphereComponent
    ├─ HeightFogContribution (default 1.0)
    │   ├─→ FSkyAtmosphereSceneProxy.GetHeightFogContribution()
    │       └─→ ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution
    │           └─→ HeightFogCommon.ush: View.SkyAtmosphereHeightFogContribution
    │
    └─ GetTransmittanceAtGroundLevel(direction) [atmosphere LUT sample]
        └─→ SkyAtmosphereRendering.cpp:454
            └─→ SetAtmosphereRelatedProperties(TransmittanceTowardSun, ...)
                └─→ Light->Proxy->TransmittanceTowardSun (stored)
                    └─→ DirectionalLightComponent.cpp:600
                        └─→ GetAtmosphereTransmittanceTowardSun() → returns transmittance
                            └─→ GetSunIlluminanceOnGroundPostTransmittance()
                                = OuterSpaceIlluminance * TransmittanceTowardSun
                                └─→ SceneRendering.cpp:1451
                                    └─→ ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index]
                                        └─→ HeightFogCommon.ush:319
                                            └─→ InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0]
                                                └─→ DirectionalInscatteringColor += SkyAtmosphereHeightFogContribution * this_value

ExponentialHeightFogComponent
    └─ SkyAtmosphereAmbientContributionColorScale (user-set color scale)
        └─→ FogRendering.cpp:267
            └─→ ViewUniformShaderParameters.SkyAtmosphereAmbientContributionColorScale
                └─→ HeightFogCommon.ush:70 (FFogUniformParameters struct member)
                    └─→ ComputeInscatteringColor() line 175
                        └─→ SkyAmbient = SkyAtmosphereAmbientContributionColorScale.rgb
                                      * View.SkyAtmosphereHeightFogContribution
                                      * GetViewDistanceSkyLightColor()
                            └─→ Added to Inscattering color (line 179)
```

---

## Critical Shader Preprocessor Flags

Both mechanisms depend on a preprocessor flag:

**`PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG`** (defined in shader compilation):
- **Lines 174-180 (ComputeInscatteringColor)**: Ambient sky contribution only compiles if this is 1
- **Lines 313-345 (DirectionalInscattering)**: Atmosphere light illuminance injection only compiles if this is 1

This allows projects to opt-in to sky atmosphere affecting height fog.

---

## Summary for UzEngine Adaptation

### The Blue Horizon Effect

The blue haze at the horizon in UE5 comes from **two sources**:

1. **Ambient sky color** via:
   - `ComputeInscatteringColor()` multiplication of:
     - `SkyAtmosphereAmbientContributionColorScale` (fog component setting, typically white ~1.0)
     - `SkyAtmosphereHeightFogContribution` (sky component setting, 0-1)
     - `GetViewDistanceSkyLightColor()` (the actual sky light color from the sky light component)

2. **Directional inscattering** via:
   - Atmosphere light color after transmittance: `AtmosphereLightIlluminanceOnGroundPostTransmittance`
   - Multiplied by `SkyAtmosphereHeightFogContribution` (0-1 blend)
   - Modulated by a phase function based on view-to-light angle

### To Implement in UzEngine

**Required implementations**:
1. Store `SkyAtmosphereHeightFogContribution` from SkyLightComponent to ViewUniformParameters
2. Compute `AtmosphereLightIlluminanceOnGroundPostTransmittance` = DirectionalLight.Color * AtmosphereTransmittance(direction)
3. Sample atmosphere transmittance LUT at ground level for the sun direction
4. In fog shader, add sky atmosphere contribution to inscattering color:
   ```hlsl
   FogColor += SkyAtmosphereHeightFogContribution
            * SkyAtmosphereAmbientContributionColorScale
            * GetSkyLightColor()
   ```
5. In directional inscattering, add atmosphere light illuminance:
   ```hlsl
   DirectionalInscatteringColor += SkyAtmosphereHeightFogContribution
                                 * AtmosphereLightIlluminanceOnGroundPostTransmittance
   ```

### Critical Numbers from UE5

- Default `HeightFogContribution` value: **1.0f** (line 585, SkyAtmosphereComponent.cpp)
- Uniform phase function: **1.0 / (4.0 * PI)** ≈ 0.0796 (line 314, HeightFogCommon.ush)
- No inversion or compensation factors — all values are direct linear multiplies

---

## References

| File | Line | Content |
|------|------|---------|
| FogRendering.cpp | 267 | `View.SkyAtmosphereAmbientContributionColorScale = ...` |
| FogRendering.cpp | 85 | `OutParameters.SkyAtmosphereAmbientContributionColorScale = ...` |
| HeightFogCommon.ush | 140-183 | `ComputeInscatteringColor()` function |
| HeightFogCommon.ush | 174-180 | Sky atmosphere ambient contribution |
| HeightFogCommon.ush | 313-325 | Directional inscattering with sky atmosphere |
| HeightFogCommon.ush | 375 | Final fog color assembly |
| SceneRendering.cpp | 1451 | Set `AtmosphereLightIlluminanceOnGroundPostTransmittance` |
| DirectionalLightComponent.cpp | 603-606 | `GetSunIlluminanceOnGroundPostTransmittance()` |
| SkyAtmosphereRendering.cpp | 454 | Compute transmittance from LUT |
| SkyAtmosphereRendering.cpp | 458 | Store transmittance in light proxy |
| SkyAtmosphereComponent.cpp | 603 | Initialize `HeightFogContribution` |
| SceneRendering.cpp | 1440 | Pass `SkyAtmosphereHeightFogContribution` to view uniform |

