# UE5 5.6 Exponential Height Fog — Complete Default Values & Formulas

**Source**: W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\ExponentialHeightFogComponent.cpp (lines 64–103)

## Constructor Defaults

### Primary Fog

| Parameter | Default | Line | Notes |
|-----------|---------|------|-------|
| FogDensity | 0.02f | 81 | Global fog density |
| FogHeightFalloff | 0.2f | 82 | Height falloff exponent |
| FogMaxOpacity | 1.0f | 86 | Max opacity (0=transparent, 1=opaque) |
| StartDistance | 0.0f | 87 | Distance fog begins from camera |
| EndDistance | 0.0f | 88 | XY-plane max distance (0=disabled) |
| FogCutoffDistance | 0.0f | 91 | Beyond this distance, no fog (disabled by default) |

### Secondary Fog (SecondFogData)

| Parameter | Default | Location |
|-----------|---------|----------|
| SecondFogData.FogDensity | 0.0f | ExponentialHeightFogComponent.cpp:84 |
| SecondFogData.FogHeightFalloff | 0.2f | ExponentialHeightFogData.h:25 |
| SecondFogData.FogHeightOffset | 0.0f | ExponentialHeightFogData.h:29 |

### Inscattering Colors

| Parameter | Default | Line |
|-----------|---------|------|
| FogInscatteringLuminance | Black (0,0,0,0) | 68 |
| FogInscatteringColor_DEPRECATED | (0.447, 0.638, 1.0) | 67 |
| SkyAtmosphereAmbientContributionColorScale | White (1,1,1,1) | 70 |

### Directional Inscattering (Sun Haze)

| Parameter | Default | Line |
|-----------|---------|------|
| DirectionalInscatteringExponent | 4.0f | 72 |
| DirectionalInscatteringStartDistance | 10000.0f | 73 |
| DirectionalInscatteringLuminance | Black (0,0,0,0) | 75 |
| DirectionalInscatteringColor_DEPRECATED | (0.25, 0.25, 0.125) | 74 |

### Inscattering Cubemap

| Parameter | Default | Line |
|-----------|---------|------|
| InscatteringTextureTint | White (1,1,1,1) | 77 |
| FullyDirectionalInscatteringColorDistance | 100000.0f | 78 |
| NonDirectionalInscatteringColorDistance | 1000.0f | 79 |

### Volumetric Fog

| Parameter | Default | Line |
|-----------|---------|------|
| VolumetricFogScatteringDistribution | 0.2f | 98 |
| VolumetricFogAlbedo | White | 99 |
| VolumetricFogExtinctionScale | 1.0f | 100 |
| VolumetricFogDistance | 6000.0f | 101 |
| VolumetricFogStaticLightingScatteringIntensity | 1.0f | 102 |

### Visibility

| Parameter | Default | Line |
|-----------|---------|------|
| bRenderInMainPass | true | 94 |
| bVisibleInReflectionCaptures | true | 95 |
| bVisibleInRealTimeSkyCaptures | true | 96 |
| bHoldout | false | 93 |

### Fog Base Height

The fog height is NOT a constructor default. It comes from the actor's world Z position:
```cpp
Ds.SetHeight(GetComponentLocation().Z);  // ExponentialHeightFogComponent.cpp:118
```

---

## CPU-Side Calculations

**Function**: FSceneRenderer::InitFogConstants() at FogRendering.cpp:208-304

### Collapsed Fog Parameter Formula

```cpp
// FogRendering.cpp:247-253
const float CollapsedFogParameterPower = FMath::Clamp(
    -FogInfo.FogData[i].HeightFalloff * (ObserverHeight - FogInfo.FogData[i].Height),
    -126.f + 1.f,
    +127.f - 1.f
);
CollapsedFogParameter[i] = FogInfo.FogData[i].Density * FMath::Pow(2.0f, CollapsedFogParameterPower);
```

### Height Clamping

```cpp
float MaxObserverHeightDifference = 65536.0f;  // 655m max
const float ObserverHeight = FMath::Min<float>(
    View.ViewMatrices.GetViewOrigin().Z,
    FogInfo.FogData[i].Height + MaxObserverHeightDifference
);
```

### Packed Shader Parameters

```cpp
// FogRendering.cpp:256-261
View.ExponentialFogParameters = FVector4f(
    CollapsedFogParameter[0],              // x
    FogInfo.FogData[0].HeightFalloff,      // y
    MaxObserverHeight,                     // z
    FogInfo.StartDistance                  // w
);

View.ExponentialFogParameters2 = FVector4f(
    CollapsedFogParameter[1],              // x
    FogInfo.FogData[1].HeightFalloff,      // y
    FogInfo.FogData[1].Density,            // z
    FogInfo.FogData[1].Height              // w
);

View.ExponentialFogParameters3 = FVector4f(
    FogInfo.FogData[0].Density,            // x
    FogInfo.FogData[0].Height,             // y
    InscatteringColorCubemap ? 1.0f : 0.0f, // z
    FogInfo.FogCutoffDistance              // w
);
```

---

## GPU-Side Calculations

**Function**: CalculateLineIntegralShared() at HeightFogCommon.ush:192-198

```hlsl
float CalculateLineIntegralShared(float FogHeightFalloff, float RayDirectionZ, float RayOriginTerms)
{
    float Falloff = max(-127.0f, FogHeightFalloff * RayDirectionZ);
    float LineIntegral = (1.0f - exp2(-Falloff)) / Falloff;
    float LineIntegralTaylor = log(2.0) - (0.5 * Pow2(log(2.0))) * Falloff;

    return RayOriginTerms * (abs(Falloff) > FLT_EPSILON2 ? LineIntegral : LineIntegralTaylor);
}
```

Epsilon thresholds (HeightFogCommon.ush:56-57):
- FLT_EPSILON = 0.001f
- FLT_EPSILON2 = 0.01f

---

## Key Patterns

1. **Height Clamping**: ObserverHeight = min(CameraZ, FogHeight + 65536.0)
2. **Collapsed Parameter**: Caches FogDensity * 2^(-HeightFalloff * (CameraZ - FogZ))
3. **Taylor Series**: Used when |Falloff| < 0.01f to avoid division-by-zero
4. **Secondary Fog**: Additive, disabled by default (FogDensity = 0)
5. **Directional Inscattering**: Phase exponent 4.0, start distance 10000.0 cm

---

## Key Sources

- ExponentialHeightFogComponent.cpp:64-103 (defaults)
- ExponentialHeightFogData.h:11-37 (struct)
- FogRendering.cpp:208-304 (CPU initialization)
- HeightFogCommon.ush:56-386 (GPU math)
