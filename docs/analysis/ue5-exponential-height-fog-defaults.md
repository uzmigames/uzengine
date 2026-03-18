<!-- doc-meta
title: UE5 5.6 ExponentialHeightFog Defaults & Formula
desc: Exact default property values and RayOriginTerms precomputation formula
tags: [ue5, fog, rendering, defaults, constants]
-->

# UE5 5.6 ExponentialHeightFog Defaults & Formula

## Executive Summary

All default values and formulas extracted from UE5 5.6 source with exact file:line citations. This document provides the canonical reference for implementing height fog systems matching UE5's behavior.

---

## 1. Default Property Values (Component Level)

### Primary Fog (First Fog)

| Property | Default Value | Type | File:Line |
|----------|---------------|------|-----------|
| **FogDensity** | 0.02f | float | ExponentialHeightFogComponent.cpp:81 |
| **FogHeightFalloff** | 0.2f | float | ExponentialHeightFogComponent.cpp:82 |
| **FogMaxOpacity** | 1.0f | float | ExponentialHeightFogComponent.cpp:86 |
| **StartDistance** | 0.0f | float | ExponentialHeightFogComponent.cpp:87 |
| **EndDistance** | 0.0f | float | ExponentialHeightFogComponent.cpp:88 |
| **FogCutoffDistance** | 0 (disabled) | float | ExponentialHeightFogComponent.cpp:91 |

### Inscattering & Color

| Property | Default Value | Type | File:Line |
|----------|---------------|------|-----------|
| **FogInscatteringLuminance** | FLinearColor::Black (0,0,0,1) | FLinearColor | ExponentialHeightFogComponent.cpp:68 |
| **SkyAtmosphereAmbientContributionColorScale** | FLinearColor::White (1,1,1,1) | FLinearColor | ExponentialHeightFogComponent.cpp:70 |

### Directional Inscattering

| Property | Default Value | Type | File:Line |
|----------|---------------|------|-----------|
| **DirectionalInscatteringExponent** | 4.0f | float | ExponentialHeightFogComponent.cpp:72 |
| **DirectionalInscatteringStartDistance** | 10000.0f | float | ExponentialHeightFogComponent.cpp:73 |
| **DirectionalInscatteringLuminance** | FLinearColor::Black (0,0,0,1) | FLinearColor | ExponentialHeightFogComponent.cpp:75 |

### Inscattering Texture (Cubemap)

| Property | Default Value | Type | File:Line |
|----------|---------------|------|-----------|
| **InscatteringTextureTint** | FLinearColor::White (1,1,1,1) | FLinearColor | ExponentialHeightFogComponent.cpp:77 |
| **FullyDirectionalInscatteringColorDistance** | 100000.0f | float | ExponentialHeightFogComponent.cpp:78 |
| **NonDirectionalInscatteringColorDistance** | 1000.0f | float | ExponentialHeightFogComponent.cpp:79 |

### Secondary Fog (Second Term)

| Property | Default Value | Type | File:Line |
|----------|---------------|------|-----------|
| **SecondFogData.FogDensity** | 0.0f (no influence) | float | ExponentialHeightFogComponent.cpp:84 |
| **SecondFogData.FogHeightFalloff** | 0.2f | float | ExponentialHeightFogData.h:25 |
| **SecondFogData.FogHeightOffset** | 0.0f | float | ExponentialHeightFogData.h:29 |

### Volumetric Fog Parameters

| Property | Default Value | Type | File:Line |
|----------|---------------|------|-----------|
| **VolumetricFogScatteringDistribution** | 0.2f | float | ExponentialHeightFogComponent.cpp:98 |
| **VolumetricFogAlbedo** | FColor::White (255,255,255,255) | FColor | ExponentialHeightFogComponent.cpp:99 |
| **VolumetricFogExtinctionScale** | 1.0f | float | ExponentialHeightFogComponent.cpp:100 |
| **VolumetricFogDistance** | 6000.0f | float | ExponentialHeightFogComponent.cpp:101 |
| **VolumetricFogStaticLightingScatteringIntensity** | 1 (no visible default in constructor, see property default) | float | ExponentialHeightFogComponent.cpp:102 |

### Deprecated Color Values (Legacy)

| Property | Default Value | Purpose | File:Line |
|----------|---------------|---------|-----------|
| **FogInscatteringColor_DEPRECATED** | FLinearColor(0.447f, 0.638f, 1.0f) | Legacy, migrated to FogInscatteringLuminance | ExponentialHeightFogComponent.cpp:67 |
| **DirectionalInscatteringColor_DEPRECATED** | FLinearColor(0.25f, 0.25f, 0.125f) | Legacy, migrated to DirectionalInscatteringLuminance | ExponentialHeightFogComponent.cpp:74 |

---

## 2. RayOriginTerms Precomputation Formula

### In CPU (C++) — FSceneRenderer::InitFogConstants()

The **RayOriginTerms** is precomputed on the CPU and passed to the shader. This is NOT a direct property on the component; it's derived from FogDensity and camera/fog height.

**Formula:**

```cpp
// From FogRendering.cpp:208-254 (InitFogConstants function)

const float CollapsedFogParameterPower = FMath::Clamp(
    -FogInfo.FogData[i].HeightFalloff * (ObserverHeight - FogInfo.FogData[i].Height),
    -126.f + 1.f,    // min exponent for IEEE floating point
    +127.f - 1.f     // max exponent for IEEE floating point
);

CollapsedFogParameter[i] = FogInfo.FogData[i].Density * FMath::Pow(2.0f, CollapsedFogParameterPower);
```

**File:Line:** FogRendering.cpp:247-253

**Breakdown:**
- **HeightFalloff**: FogHeightFalloff (default 0.2f)
- **ObserverHeight**: Camera's Z height (clamped to MaxObserverHeight)
- **Height**: FogHeight (from fog actor position or component Z)
- **Density**: FogDensity (default 0.02f)
- **Result**: RayOriginTerms (stored in ExponentialFogParameters.x)

The exponent is clamped to prevent IEEE floating-point overflow/underflow:
- Min exponent: -125 (2^-125 ≈ 4.7e-38)
- Max exponent: +126 (2^126 ≈ 8.5e37)

### In Shader (HLSL) — Line Integral Calculation

**File:** HeightFogCommon.ush:192-199

```hlsl
float CalculateLineIntegralShared(float FogHeightFalloff, float RayDirectionZ, float RayOriginTerms)
{
    float Falloff = max(-127.0f, FogHeightFalloff * RayDirectionZ);
    float LineIntegral = ( 1.0f - exp2(-Falloff) ) / Falloff;
    float LineIntegralTaylor = log(2.0) - ( 0.5 * Pow2( log(2.0) ) ) * Falloff;

    return RayOriginTerms * ( abs(Falloff) > FLT_EPSILON2 ? LineIntegral : LineIntegralTaylor );
}
```

**Key Formula Elements:**
- **Falloff**: `FogHeightFalloff * RayDirectionZ` (clamped to max -127.0)
- **LineIntegral**: `(1 - exp2(-Falloff)) / Falloff` (main case)
- **LineIntegralTaylor**: `log(2) - 0.5 * (log(2))² * Falloff` (Taylor expansion for near-zero Falloff)
- **Result**: `RayOriginTerms * LineIntegral`

The Taylor expansion is used when `|Falloff| ≤ FLT_EPSILON2` (0.01) to avoid numerical division-by-zero issues.

---

## 3. View-Space Parameter Packing

When SetupFogUniformParameters() is called (FogRendering.cpp:63-94), the following uniform buffer is filled:

### ExponentialFogParameters (float4)

| Component | Value | Source |
|-----------|-------|--------|
| x | CollapsedFogParameter[0] (RayOriginTerms) | InitFogConstants:253 |
| y | FogHeightFalloff | InitFogConstants:256 |
| z | MaxObserverHeight | InitFogConstants:256 |
| w | StartDistance | InitFogConstants:256 |

**File:Line:** FogRendering.cpp:256

### ExponentialFogParameters2 (float4)

| Component | Value | Source |
|-----------|-------|--------|
| x | CollapsedFogParameter[1] (RayOriginTerms for second fog) | InitFogConstants:257 |
| y | FogHeightFalloff (second fog) | InitFogConstants:257 |
| z | Density (second fog, FogData[1].Density) | InitFogConstants:257 |
| w | Height (second fog, FogData[1].Height) | InitFogConstants:257 |

**File:Line:** FogRendering.cpp:257

### ExponentialFogColorParameter (float4)

| Component | Value | Source |
|-----------|-------|--------|
| xyz | FogInscatteringColor (RGB) | FogRendering.cpp:258 |
| w | 1.0f - FogMaxOpacity (inverted opacity) | FogRendering.cpp:82 |

**File:Line:** FogRendering.cpp:82

### ExponentialFogParameters3 (float4)

| Component | Value | Source |
|-----------|-------|--------|
| x | Density (FogData[0].Density) | InitFogConstants:260 |
| y | Height (FogData[0].Height) | InitFogConstants:260 |
| z | bHasCubemap ? 1.0f : 0.0f | InitFogConstants:260 |
| w | FogCutoffDistance | InitFogConstants:260 |

**File:Line:** FogRendering.cpp:260

### SkyAtmosphereAmbientContributionColorScale (float4)

| Component | Value | Type |
|-----------|-------|------|
| xyz | Component color scale (default White = 1,1,1) | FLinearColor |
| w | 1.0 (unused) | implicitly |

**File:Line:** FogRendering.cpp:85, initialized to White in ExponentialHeightFogComponent.cpp:70

---

## 4. Shader Integration Constants

**File:** HeightFogCommon.ush

| Constant | Value | Purpose | File:Line |
|----------|-------|---------|-----------|
| **FLT_EPSILON** | 0.001f | Threshold for near-zero comparisons | HeightFogCommon.ush:56 |
| **FLT_EPSILON2** | 0.01f | Threshold for line integral Taylor expansion | HeightFogCommon.ush:57 |
| **MinFalloff** | -127.0f | Clamp floor for Falloff = FogHeightFalloff * RayDirectionZ | HeightFogCommon.ush:194 |

### SkyAtmosphere Contribution

When `PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG` is enabled:

**File:** HeightFogCommon.ush:175-179

```hlsl
float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb
                  * View.SkyAtmosphereHeightFogContribution.xxx
                  * GetViewDistanceSkyLightColor();
#if PROJECT_EXPFOG_MATCHES_VFOG
    SkyAmbient *= FogData.VolumetricFogAlbedo;
#endif
Inscattering += SkyAmbient;
```

Key point: **View.SkyAtmosphereHeightFogContribution** is a float value (not a property on the fog component itself) that modulates the SkyAtmosphereAmbientContributionColorScale. Default = 1.0 when SkyAtmosphere is present.

---

## 5. Height Fog Rendering Order (Pipeline)

**File:** FogRendering.cpp

1. **InitFogConstants()** (line 208) — CPU pre-computation of RayOriginTerms
2. **SetupFogUniformParameters()** (line 63) — Pack parameters into uniform buffer
3. **CreateFogUniformBuffer()** (line 117) — Create RDG uniform buffer
4. **RenderFog()** (line 520) — Execute fog pass with packed uniforms
5. **GetExponentialHeightFog()** (HeightFogCommon.ush:213) — Shader-side fog calculation

---

## 6. Summary Table: All Defaults in Order

| Name | Value | Category | File:Line |
|------|-------|----------|-----------|
| FogDensity | 0.02f | Primary Fog | ExponentialHeightFogComponent.cpp:81 |
| FogHeightFalloff | 0.2f | Primary Fog | ExponentialHeightFogComponent.cpp:82 |
| FogMaxOpacity | 1.0f | Primary Fog | ExponentialHeightFogComponent.cpp:86 |
| StartDistance | 0.0f | Primary Fog | ExponentialHeightFogComponent.cpp:87 |
| EndDistance | 0.0f | Primary Fog | ExponentialHeightFogComponent.cpp:88 |
| FogCutoffDistance | 0.0f | Primary Fog | ExponentialHeightFogComponent.cpp:91 |
| FogInscatteringLuminance | (0,0,0,1) | Color | ExponentialHeightFogComponent.cpp:68 |
| DirectionalInscatteringExponent | 4.0f | Directional | ExponentialHeightFogComponent.cpp:72 |
| DirectionalInscatteringStartDistance | 10000.0f | Directional | ExponentialHeightFogComponent.cpp:73 |
| DirectionalInscatteringLuminance | (0,0,0,1) | Directional | ExponentialHeightFogComponent.cpp:75 |
| SkyAtmosphereAmbientContributionColorScale | (1,1,1,1) | Sky | ExponentialHeightFogComponent.cpp:70 |
| InscatteringTextureTint | (1,1,1,1) | Texture | ExponentialHeightFogComponent.cpp:77 |
| FullyDirectionalInscatteringColorDistance | 100000.0f | Texture | ExponentialHeightFogComponent.cpp:78 |
| NonDirectionalInscatteringColorDistance | 1000.0f | Texture | ExponentialHeightFogComponent.cpp:79 |
| SecondFogData.FogDensity | 0.0f | Secondary Fog | ExponentialHeightFogComponent.cpp:84 |
| SecondFogData.FogHeightFalloff | 0.2f | Secondary Fog | ExponentialHeightFogData.h:25 |
| SecondFogData.FogHeightOffset | 0.0f | Secondary Fog | ExponentialHeightFogData.h:29 |
| VolumetricFogScatteringDistribution | 0.2f | Volumetric | ExponentialHeightFogComponent.cpp:98 |
| VolumetricFogAlbedo | White | Volumetric | ExponentialHeightFogComponent.cpp:99 |
| VolumetricFogExtinctionScale | 1.0f | Volumetric | ExponentialHeightFogComponent.cpp:100 |
| VolumetricFogDistance | 6000.0f | Volumetric | ExponentialHeightFogComponent.cpp:101 |

---

## 7. Critical Implementation Notes for UzEngine

### NEVER Hardcode These Values
All of the above must be properties that can be edited. The defaults must be applied in the component/actor constructor only.

### RayOriginTerms is NOT a Property
RayOriginTerms is a **derived/precomputed value**, not a direct property. It must be computed per-frame in the render thread based on:
- FogDensity (property)
- FogHeightFalloff (property)
- ObserverHeight (camera Z, clamped)
- FogHeight (actor Z)

### SkyAtmosphereHeightFogContribution is NOT a Fog Property
This is stored in the **View** structure, not on the fog component. It represents how much the sky atmosphere contributes to fog ambient color. Default = 1.0.

### FogInscatteringColor vs FogInscatteringLuminance
UE5 **deprecated** FogInscatteringColor in favor of FogInscatteringLuminance. When loading old maps, UE5 migrates the old value:

**File:Line:** ExponentialHeightFogComponent.cpp:358-368

```cpp
if (Ar.IsLoading() && (Ar.CustomVer(...) < ..SkyAtmosphereAffectsHeightFogWithBetterDefault))
{
    FogInscatteringLuminance = FogInscatteringColor_DEPRECATED;
    DirectionalInscatteringLuminance = DirectionalInscatteringColor_DEPRECATED;
}
```

Always use the **Luminance** versions in new code.

### Property Clamping in Editor
Editor-side validation clamps values:

**File:Line:** ExponentialHeightFogComponent.cpp:203-208

```cpp
FogDensity = FMath::Clamp(FogDensity, 0.0f, 10.0f);
FogHeightFalloff = FMath::Clamp(FogHeightFalloff, 0.0f, 2.0f);
FogMaxOpacity = FMath::Clamp(FogMaxOpacity, 0.0f, 1.0f);
StartDistance = FMath::Clamp(StartDistance, 0.0f, (float)WORLD_MAX);
```

---

## References

- **ExponentialHeightFogComponent.h**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\ExponentialHeightFogComponent.h`
- **ExponentialHeightFogComponent.cpp**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\ExponentialHeightFogComponent.cpp`
- **ExponentialHeightFogData.h**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\ExponentialHeightFogData.h`
- **HeightFogCommon.ush**: `W:\UE_5.6_Source\Engine\Shaders\Private\HeightFogCommon.ush`
- **FogRendering.cpp**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\FogRendering.cpp`
