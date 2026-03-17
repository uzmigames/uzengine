<!-- doc-meta
title: UE5 Sky Atmosphere Injection into Exponential Height Fog
description: Exact mechanism for how sky atmosphere color reaches fog shader
author: unreal-architect
date: 2026-03-17
revision: 1.0
-->

# UE5 Sky Atmosphere Injection into Exponential Height Fog

## Problem Statement

When `ExponentialHeightFog.FogInscatteringLuminance = Black(0,0,0)` in UE5, the horizon still appears white/blue. This is because the SkyAtmosphere component is injecting its color into the fog via a separate mechanism.

## Data Flow: CPU → GPU → Shader

### 1. Component Layer (CPU)

**File**: `Engine\Source\Runtime\Engine\Classes\Components\SkyAtmosphereComponent.h` (lines 156-158)
**File**: `Engine\Source\Runtime\Engine\Private\Components\SkyAtmosphereComponent.cpp` (lines 89-131)

The `USkyAtmosphereComponent` has three key properties:

```cpp
/// Line 156-158 in SkyAtmosphereComponent.h
/** Scale the sky and atmosphere lights contribution to the height fog when SupportSkyAtmosphereAffectsHeightFog project setting is true.*/
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art Direction",
    meta = (UIMin = 0.0, UIMax = 1.0, ClampMin = 0.0, SliderExponent = 2.0))
float HeightFogContribution;  // Default: 1.0 (line 130)
```

And the ExponentialHeightFogComponent:

**File**: `Engine\Source\Runtime\Engine\Private\Components\ExponentialHeightFogComponent.cpp` (lines 64-102)

```cpp
/// Line 70 in ExponentialHeightFogComponent.cpp
SkyAtmosphereAmbientContributionColorScale = FLinearColor::White;  // Default WHITE
```

### 2. Scene Proxy Layer (Render Thread)

**File**: `Engine\Source\Runtime\Engine\Public\SceneProxies\SkyAtmosphereSceneProxy.h` (lines 15-60)

The proxy stores the HeightFogContribution value:

```cpp
class FSkyAtmosphereSceneProxy
{
    // Line 27
    float GetHeightFogContribution() const { return HeightFogContribution; }

private:
    // Line 50
    float HeightFogContribution;
};
```

**File**: `Engine\Source\Runtime\Engine\Private\Components\SkyAtmosphereComponent.cpp` (lines 594-608)

The proxy is initialized from the component:

```cpp
/// Lines 594-608
FSkyAtmosphereSceneProxy::FSkyAtmosphereSceneProxy(const USkyAtmosphereComponent* InComponent)
	: bStaticLightingBuilt(false)
	, AtmosphereSetup(*InComponent)
	, bHoldout(InComponent->bHoldout > 0)
	, bRenderInMainPass(InComponent->bRenderInMainPass > 0)
{
	SkyLuminanceFactor = InComponent->SkyLuminanceFactor;
	SkyAndAerialPerspectiveLuminanceFactor = InComponent->SkyAndAerialPerspectiveLuminanceFactor;
	AerialPespectiveViewDistanceScale = InComponent->AerialPespectiveViewDistanceScale;
	HeightFogContribution = InComponent->HeightFogContribution;  // Line 603: DIRECTLY COPIED
	AerialPerspectiveStartDepthKm = InComponent->AerialPerspectiveStartDepth;
	TraceSampleCountScale = InComponent->TraceSampleCountScale;
```

### 3. View Uniform Buffer Setup

**File**: `Engine\Source\Runtime\Renderer\Private\SceneRendering.cpp` (lines 1435-1478)

The render thread queries the sky proxy and populates View uniform parameters:

```cpp
/// Lines 1439-1440
ViewUniformShaderParameters.SkyAtmosphereSkyLuminanceFactor = SkyAtmosphereSceneProxy.GetSkyLuminanceFactor();
ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = SkyAtmosphereSceneProxy.GetHeightFogContribution();

// And lines 1477-1478 (when no sky atmosphere exists)
ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = 0.0f;
```

**File**: `Engine\Source\Runtime\Renderer\Private\FogRendering.cpp` (lines 63-95)

The fog uniform buffer is populated with view parameters:

```cpp
/// Lines 84-85
OutParameters.ExponentialFogParameters3 = View.ExponentialFogParameters3;
OutParameters.SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;

/// Line 268 in FogRendering.cpp
View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;
```

## GPU Shader Computation

### HeightFogCommon.ush: Sky Ambient Injection

**File**: `Engine\Shaders\Private\HeightFogCommon.ush` (lines 140-183)

The key function that injects sky atmosphere into fog inscattering:

```hlsl
/// Lines 140-183
float3 ComputeInscatteringColor(float3 CameraToReceiver, float CameraToReceiverLength, FogStructData FogData, uint EyeIndex)
{
	half3 Inscattering = FogData.ExponentialFogColorParameter.xyz;

#if PERMUTATION_SUPPORT_FOG_INSCATTERING_TEXTURE
	BRANCH
	if (FogData.ExponentialFogParameters3.z > 0)
	{
		// ... cubemap sampling code (lines 146-171) ...
		Inscattering *= lerp(NonDirectionalColor, DirectionalColor, FadeAlpha);
	}
#endif

#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
	/// LINE 175: THE CRITICAL INJECTION POINT
	float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb
		* View.SkyAtmosphereHeightFogContribution.xxx
		* GetViewDistanceSkyLightColor();

	#if PROJECT_EXPFOG_MATCHES_VFOG
	SkyAmbient *= FogData.VolumetricFogAlbedo;
	#endif

	Inscattering += SkyAmbient;  /// LINE 179: ADDITIVE BLEND
#endif

	return Inscattering;
}
```

**Key components of the injection**:
1. **`FogData.SkyAtmosphereAmbientContributionColorScale`** — the ExponentialHeightFog component's color scale (default WHITE)
2. **`View.SkyAtmosphereHeightFogContribution`** — the SkyAtmosphere component's contribution factor (0.0—1.0, default 1.0)
3. **`GetViewDistanceSkyLightColor()`** — the actual sky atmosphere color (computed by the SkyAtmosphere render pass)

### Directional Inscattering from Atmosphere

**File**: `Engine\Shaders\Private\HeightFogCommon.ush` (lines 304-354)

There's ALSO directional inscattering injection in the directional light path:

```hlsl
/// Lines 313-345 (PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG only)
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
	const float UniformPhaseFunction = 1.0f / (4.0f * PI);
	half3 DirectionalInscatteringColor;
	half3 DirectionalLightInscattering;

	// No need to test View.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].a
	// because InscatteringLightDirection.w above is doing the same test already.

	/// LINE 319: DIRECTIONAL INJECTION FROM ATMOSPHERE LIGHTS
	DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
		+ InView.SkyAtmosphereHeightFogContribution
		* InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;

	DirectionalLightInscattering = DirectionalInscatteringColor
		* pow(saturate(dot(CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz)),
		      FogData.DirectionalInscatteringColor.w) * UniformPhaseFunction;

	if (InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1].a > 0.0f)
	{
		// ... secondary light handling (lines 327-336) ...
	}
#else
	// Fallback when sky atmosphere is disabled (lines 338-344)
#endif
```

## Fog Struct Definition

**File**: `Engine\Shaders\Private\HeightFogCommon.ush` (lines 59-85)

```hlsl
/// Lines 59-85
// FogStruct.ExponentialFogParameters: FogDensity * exp2(-FogHeightFalloff * (CameraWorldPosition.z - FogHeight)) in x,
//   FogHeightFalloff in y, MaxWorldObserverHeight in z, StartDistance in w.
// FogStruct.ExponentialFogParameters2: FogDensitySecond * exp2(-FogHeightFalloffSecond * (CameraWorldPosition.z - FogHeightSecond)) in x,
//   FogHeightFalloffSecond in y, FogDensitySecond in z, FogHeightSecond in w
// FogStruct.ExponentialFogParameters3: FogDensity in x, FogHeight in y, whether to use cubemap fog color in z, FogCutoffDistance in w.
// FogStruct.FogInscatteringTextureParameters: mip distance scale in x, bias in y, num mips in z

struct FogStructData
{
	float4 ExponentialFogParameters;
	float4 ExponentialFogParameters2;
	float4 ExponentialFogColorParameter;
	float4 ExponentialFogParameters3;
	float4 SkyAtmosphereAmbientContributionColorScale;  /// LINE 70: SKY ATMOSPHERE INJECTION COLOR
	float4 InscatteringLightDirection;
	float4 DirectionalInscatteringColor;
	float2 SinCosInscatteringColorCubemapRotation;
	float3 FogInscatteringTextureParameters;
	float EndDistance;
	float ApplyVolumetricFog;
	float VolumetricFogStartDistance;
	float VolumetricFogNearFadeInDistanceInv;
	float3 VolumetricFogAlbedo;
	float VolumetricFogPhaseG;
};
```

## The Injection Equation

The final fog inscattering color is computed as:

```
FinalInscatteringColor =
    ExponentialFogColor
    + (FogCubemapColor if enabled)
    + SkyAtmosphereAmbientContributionColorScale * SkyAtmosphereHeightFogContribution * GetViewDistanceSkyLightColor()
    + DirectionalInscattering
```

Where:
- **ExponentialFogColor** = `FogData.ExponentialFogColorParameter.xyz`
- **SkyAtmosphereAmbientContributionColorScale** = from ExponentialHeightFog component (default WHITE)
- **SkyAtmosphereHeightFogContribution** = from SkyAtmosphere component (0.0–1.0, default 1.0)
- **GetViewDistanceSkyLightColor()** = the actual sky radiance at the camera's altitude (computed from the SkyAtmosphere's transmittance LUT and sky view LUT)

## Key Insight: Why Horizon Appears White with Black FogInscatteringLuminance

When `FogInscatteringLuminance = Black`, the ExponentialFogColor contribution is zero. However:

1. **SkyAtmosphereAmbientContributionColorScale = White** (default)
2. **SkyAtmosphereHeightFogContribution = 1.0** (default, meaning "fully enabled")
3. The sky atmosphere is rendering at the horizon and computing a light blue/white sky color via ray-marching through the Rayleigh/Mie scattering atmosphere
4. **Line 175 in HeightFogCommon.ush** adds this sky color to the fog inscattering

Result: Even with zero ExponentialFogColor, you get the sky atmosphere's color because it's additively blended in via the SkyAmbient term.

## Project Settings Required

**File**: `Engine\Shaders\Private\HeightFogCommon.ush` (lines 174, 313)

The sky atmosphere injection is **gated by a project setting**:

```cpp
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
```

This is a compile-time flag. In UE5 5.6, this is likely defined based on:
- Project setting: "Support Sky Atmosphere Affects Height Fog" (must be enabled in project settings)
- Or shader permutation flags when the setting is enabled

## UzEngine Implementation Requirements

To match UE5's behavior:

1. **Store HeightFogContribution in SkyAtmosphereComponent** (0.0–1.0 scale factor, default 1.0)
2. **Store SkyAtmosphereAmbientContributionColorScale in ExponentialHeightFogComponent** (FLinearColor, default WHITE)
3. **Pass SkyAtmosphereHeightFogContribution to GPU** via View uniform parameters
4. **In fog shader**: Compute `SkyAmbient = SkyAtmosphereAmbientContributionColorScale * SkyAtmosphereHeightFogContribution * GetSkyColor()`
5. **Blend additively**: `FinalFogColor = ExponentialFogColor + SkyAmbient + ...`
6. **Conditionally compile** with a project setting flag (or always enable if sky atmosphere support is present)

## Related UE5 Source Files

| File | Purpose | Key Content |
|------|---------|-------------|
| `ExponentialHeightFogComponent.h` | Component properties | `SkyAtmosphereAmbientContributionColorScale` property |
| `ExponentialHeightFogComponent.cpp` | Component initialization | Default value: WHITE |
| `SkyAtmosphereComponent.h` | Sky component properties | `HeightFogContribution` property (0.0–1.0) |
| `SkyAtmosphereComponent.cpp` | Proxy creation | Copies `HeightFogContribution` to proxy |
| `SkyAtmosphereSceneProxy.h` | Render-thread proxy | Getter for `HeightFogContribution` |
| `SceneRendering.cpp` | View setup | Copies proxy value → `ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution` |
| `FogRendering.cpp` | Fog uniform buffer | Copies fog component value → uniform buffer |
| `HeightFogCommon.ush` | GPU computation | `ComputeInscatteringColor()` with sky injection (line 175) |
| `HeightFogPixelShader.usf` | Fog pass entrypoint | Calls `CalculateHeightFog()` |

## Summary

**The exact mechanism**:

1. **SkyAtmosphere component** exposes `HeightFogContribution` (0.0–1.0), default 1.0
2. **ExponentialHeightFog component** exposes `SkyAtmosphereAmbientContributionColorScale` (RGB color), default WHITE
3. **Render thread** extracts both values and puts them in the view uniform buffer
4. **Fog shader** reads `View.SkyAtmosphereHeightFogContribution` and the sky color from `GetViewDistanceSkyLightColor()`
5. **Computes**: `SkyAmbient = SkyAtmosphereAmbientContributionColorScale * HeightFogContribution * SkyColor`
6. **Adds to fog**: `FinalFogColor += SkyAmbient`

This is why the horizon looks white/blue even when FogInscatteringLuminance is black—the sky atmosphere color is injected separately from the base fog color.
