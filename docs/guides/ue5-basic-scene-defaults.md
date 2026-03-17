<!-- doc-meta -->
# UE5 5.6 "Basic" Scene Template — Exact Default Values

**Source**: UE5 5.6 engine source code (W:\UE_5.6_Source)
**Generated**: 2026-03-17
**Purpose**: Provide EXACT numeric values for UzEngine to match UE5 rendering defaults

---

## 1. DirectionalLight (Sun) Defaults

Source file: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp:988-1057`

### Constructor Defaults (UDirectionalLightComponent::UDirectionalLightComponent)

| Property | Value | Type | Notes |
|----------|-------|------|-------|
| **Intensity** | 10.0f | float | Base brightness (lux-equivalent). Set by ULightComponentBase default is π (3.1415926535897932f), overridden to 10 in DirectionalLight constructor |
| **LightColor** | White (255, 255, 255) | FColor | Linear RGB white |
| **bAffectsWorld** | true | bool | Light is enabled |
| **CastShadows** | true | bool | Casts dynamic shadows |
| **CastStaticShadows** | true | bool | Casts static shadows |
| **CastDynamicShadows** | true | bool | Casts shadows on dynamic objects |
| **LightSourceAngle** | 0.5357f | float | **Sun's angular diameter in degrees** — this is the exact angle subtended by Earth's sun |
| **LightSourceSoftAngle** | 0.0f | float | No soft penumbra |
| **ShadowSourceAngleFactor** | 1.0f | float | Factor applied to source angle for ray-traced shadows |
| **DynamicShadowDistanceMovableLight** | 40000.0f | float | CSM coverage distance for movable lights (cm) |
| **DynamicShadowDistanceStationaryLight** | 0.0f | float | CSM disabled for stationary lights by default |
| **DistanceFieldShadowDistance** | 51200.0f | float | Distance field shadow coverage (cm) |
| **DynamicShadowCascades** | 4 | int32 | Four cascades for CSM |
| **CascadeDistributionExponent** | 3.0f | float | Cascade size distribution exponent |
| **CascadeTransitionFraction** | 0.1f | float | 10% transition region between cascades |
| **ShadowDistanceFadeoutFraction** | 0.1f | float | 10% fade-out at far shadow extent |
| **ShadowBias** | 0.5f | float | Self-shadowing bias |
| **ShadowSlopeBias** | 0.5f | float | Slope-dependent bias |
| **IndirectLightingIntensity** | 1.0f | float | GI contribution scale |
| **CastTranslucentShadows** | true | bool | Casts shadows on translucent objects |
| **bUseInsetShadowsForMovableObjects** | true | bool | Per-object shadows for movable geom outside CSM |
| **bCastVolumetricShadow** | true | bool | Shadows volumetric fog |
| **bAtmosphereSunLight** | true | bool | **CRITICAL: Light interacts with atmosphere/sky** |
| **AtmosphereSunLightIndex** | 0 | int32 | This is the primary sun (index 0; max 2 lights) |
| **AtmosphereSunDiskColorScale** | White (1, 1, 1) | FLinearColor | No color tint on sun disk |
| **bPerPixelAtmosphereTransmittance** | false | bool | Use global transmittance (not per-pixel) |

### Inherited from ULightComponent

Source file: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\LightComponent.cpp:453-496`

| Property | Value | Type | Notes |
|----------|-------|------|-------|
| **Temperature** | 6500.0f | float | D65 white point (neutral daylight) |
| **bUseTemperature** | false | bool | Temperature is ignored; use LightColor instead |
| **ShadowResolutionScale** | 1.0f | float | 1x base shadow resolution |
| **ContactShadowLength** | 0.0f | float | Screen-space contact shadows disabled |
| **ContactShadowCastingIntensity** | 1.0f | float | Contact shadow strength (if enabled) |
| **SpecularScale** | 1.0f | float | Specular highlight multiplier |
| **DiffuseScale** | 1.0f | float | Diffuse lighting multiplier |

---

## 2. SkyAtmosphere Component Defaults

Source file: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyAtmosphereComponent.cpp:89-142`

### Constructor Defaults (USkyAtmosphereComponent::USkyAtmosphereComponent)

**Design**: Physically-based Earth atmosphere model. All distances in kilometers.

| Property | Value | Type | Notes |
|----------|-------|------|-------|
| **TransformMode** | `PlanetTopAtAbsoluteWorldOrigin` | enum | Planet surface is at world origin |
| **BottomRadius** | 6360.0f | float | Earth radius in km |
| **AtmosphereHeight** | 60.0f | float | EarthTopRadius (6420) - BottomRadius (6360) = 60 km |
| **GroundAlbedo** | (170, 170, 170) | FColor(u8) | 0.667 linear (midtone earth) |
| **RayleighScattering (raw)** | (0.005802, 0.013558, 0.033100) | FLinearColor | Base Rayleigh scatter coefficients (per km) |
| **RayleighScatteringScale** | 0.033100f | float | Scale factor derived from raw.B component |
| **RayleighScattering (normalized)** | (0.1752, 0.4093, 1.0) | FLinearColor | Normalized by dividing raw by raw.B |
| **RayleighExponentialDistribution** | 8.0f | float | Scale height where Rayleigh density = 40% (km) |
| **MieScattering** | White (1, 1, 1) | FColor | Mie scatter is achromatic (white) |
| **MieScatteringScale** | 0.003996f | float | Mie scatter coefficient scale |
| **MieAbsorption** | White (1, 1, 1) | FColor | Mie absorption is achromatic |
| **MieAbsorptionScale** | 0.000444f | float | Mie absorption coefficient scale |
| **MieAnisotropy** | 0.8f | float | Forward scattering bias (0=isotropic, 1=forward) |
| **MieExponentialDistribution** | 1.2f | float | Scale height where Mie density = 40% (km) |
| **OtherAbsorptionScale** | 0.001881f | float | Ozone layer absorption scale |
| **OtherAbsorption (normalized)** | (0.3455, 1.0, 0.0452) | FLinearColor | Normalized ozone coefficients |
| **OtherTentDistribution.TipAltitude** | 25.0f | float | Ozone peak altitude (km) |
| **OtherTentDistribution.TipValue** | 1.0f | float | Ozone peak density |
| **OtherTentDistribution.Width** | 15.0f | float | Ozone layer width (km) |
| **SkyLuminanceFactor** | White (1, 1, 1) | FLinearColor | No luminance scaling |
| **SkyAndAerialPerspectiveLuminanceFactor** | White (1, 1, 1) | FLinearColor | No luminance scaling |
| **MultiScatteringFactor** | 1.0f | float | Multiple scattering contribution scale |
| **AerialPespectiveViewDistanceScale** | 1.0f | float | Aerial perspective thickness (1=normal) |
| **AerialPerspectiveStartDepth** | 0.1f | float | Start depth for aerial perspective (km) |
| **HeightFogContribution** | 1.0f | float | Contribution to exponential height fog |
| **TransmittanceMinLightElevationAngle** | -90.0f | float | Allow transmittance down to horizon |
| **TraceSampleCountScale** | 1.0f | float | Sample count scale for quality |
| **bRenderInMainPass** | true | bool | Rendered during main geometry pass |

---

## 3. SkyLight Component Defaults

Source file: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyLightComponent.cpp:285-333`

### Constructor Defaults (USkyLightComponent::USkyLightComponent)

| Property | Value | Type | Notes |
|----------|-------|------|-------|
| **Intensity** | 1.0f | float | Base brightness for IBL |
| **IndirectLightingIntensity** | 1.0f | float | GI contribution multiplier |
| **LightColor** | White (255, 255, 255) | FColor | Tint on sky light |
| **bLowerHemisphereIsBlack** | true | bool | Ground does NOT contribute reflection |
| **LowerHemisphereColor** | Black (0, 0, 0) | FLinearColor | No lower hemisphere reflection |
| **OcclusionMaxDistance** | 1000.0f | float | Max DFAO distance (cm) |
| **OcclusionExponent** | 1.0f | float | AO power curve (1=linear) |
| **Contrast** | 0.0f | float | No AO contrast (set by base) |
| **MinOcclusion** | 0.0f | float | AO can go fully black |
| **OcclusionTint** | Black (0, 0, 0) | FColor | AO tint is neutral |
| **CubemapResolution** | 128 | int32 | Sky cubemap resolution (power of 2) |
| **SourceType** | `SLS_CapturedScene` | enum | Capture from scene (NOT a specified cubemap) |
| **SkyDistanceThreshold** | 150000.0f | float | Distance threshold for sky capture (cm) |
| **bRealTimeCapture** | false | bool | Capture is not updated every frame |
| **Mobility** | Stationary | enum | Light is stationary (pre-baked) |
| **CastShadows** | true | bool | (from ULightComponentBase) |
| **bCastVolumetricShadow** | true | bool | Shadows volumetric fog |
| **bAffectReflection** | true | bool | Used in ray-traced reflections |
| **bAffectGlobalIllumination** | true | bool | Affects GI |
| **SamplesPerPixel** | 4 | int32 | RTX sample count |
| **CloudAmbientOcclusion** | false | bool | Cloud AO not enabled |
| **CloudAmbientOcclusionStrength** | 1.0f | float | Cloud AO intensity (if enabled) |
| **CloudAmbientOcclusionExtent** | 150.0f | float | Cloud AO capture radius (km) |

---

## 4. ExponentialHeightFog Component Defaults

Source file: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\ExponentialHeightFogComponent.cpp:64-103`

### Constructor Defaults (UExponentialHeightFogComponent::UExponentialHeightFogComponent)

| Property | Value | Type | Notes |
|----------|-------|------|-------|
| **FogDensity** | 0.02f | float | Global fog density |
| **FogHeightFalloff** | 0.2f | float | Exponential decay with height |
| **FogInscatteringLuminance** | Black (0, 0, 0) | FLinearColor | No inscattering (sky atmosphere provides it) |
| **FogMaxOpacity** | 1.0f | float | Fog can be fully opaque |
| **StartDistance** | 0.0f | float | Fog starts at camera |
| **EndDistance** | 0.0f | float | No end distance (infinite fog) |
| **FogCutoffDistance** | 0.0f | float | No cutoff distance |
| **DirectionalInscatteringExponent** | 4.0f | float | Light shaft cone angle exponent |
| **DirectionalInscatteringStartDistance** | 10000.0f | float | Light shafts start distance (cm) |
| **DirectionalInscatteringLuminance** | Black (0, 0, 0) | FLinearColor | No directional inscattering |
| **InscatteringTextureTint** | White (1, 1, 1) | FLinearColor | No tint |
| **FullyDirectionalInscatteringColorDistance** | 100000.0f | float | Distance for full directional (cm) |
| **NonDirectionalInscatteringColorDistance** | 1000.0f | float | Distance for non-directional (cm) |
| **VolumetricFogScatteringDistribution** | 0.2f | float | Mie anisotropy for volumetric fog |
| **VolumetricFogAlbedo** | White (255, 255, 255) | FColor | Fog particles reflect light |
| **VolumetricFogExtinctionScale** | 1.0f | float | Absorption scale for volumetric fog |
| **VolumetricFogDistance** | 6000.0f | float | Max volumetric fog render distance (cm) |
| **bEnableVolumetricFog** | false | bool | Volumetric fog disabled by default |
| **SecondFogData.FogDensity** | 0.0f | float | No second fog layer |
| **SkyAtmosphereAmbientContributionColorScale** | White (1, 1, 1) | FLinearColor | Sky atmosphere contribution scale |
| **bRenderInMainPass** | true | bool | Rendered in main geometry pass |
| **bVisibleInReflectionCaptures** | true | bool | Affects reflection captures |
| **bVisibleInRealTimeSkyCaptures** | true | bool | Affects sky light captures |

---

## 5. Light Base Class (ULightComponentBase) Defaults

Source file: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\LightComponent.cpp:427-446`

| Property | Value | Type | Notes |
|----------|-------|------|-------|
| **Intensity** | π (3.1415926535897932f) | float | **Base default is π**. Overridden per light type. |
| **LightColor** | White (255, 255, 255) | FColor | White light (linear RGB) |
| **bAffectsWorld** | true | bool | Light affects scene |
| **CastShadows** | true | bool | Casts shadows by default |
| **CastStaticShadows** | true | bool | Casts shadows on static geometry |
| **CastDynamicShadows** | true | bool | Casts shadows on dynamic objects |
| **bAffectReflection** | true | bool | Affects reflections (RTX) |
| **bAffectGlobalIllumination** | true | bool | Affects GI (RTX) |
| **CastRaytracedShadow** | `UseProjectSetting` | enum | Ray-traced shadows follow project settings |
| **VolumetricScatteringIntensity** | 1.0f | float | Volumetric scattering strength |
| **DeepShadowLayerDistribution** | 0.5f | float | Deep shadow exponential distribution (0=linear) |

---

## 6. PostProcess Settings (Default/Minimal)

**Note**: The "Basic" template does NOT create an explicit PostProcessVolume. These are engine defaults if one is added.

### AutoExposure (if enabled)

- **AutoExposureMethod**: Histogram
- **AutoExposureMinBrightness**: 0.3f (dark areas)
- **AutoExposureMaxBrightness**: 2.0f (bright areas)
- **AutoExposureBias**: 0.0f (no over/under exposure)

### FilmToneMap (ACES)

- **FilmSlope**: 0.88f
- **FilmToe**: 0.55f
- **FilmShoulder**: 0.26f
- **FilmBlackClip**: 0.0f
- **FilmWhiteClip**: 0.04f

### Bloom (if enabled)

- **BloomIntensity**: 0.675f
- **BloomThreshold**: -1.0f (no threshold)
- **BloomSizeScale**: 1.0f

---

## 7. CRITICAL: Sun Illuminance → Atmosphere Shader

### The Key Question: How does DirectionalLight Intensity map to atmosphere SunIlluminance?

Based on UE5 source analysis:

1. **DirectionalLight.Intensity** = 10.0f (default)
2. **Light color** = White (1, 1, 1) linear
3. **LightSourceAngle** = 0.5357° (sun's angular diameter)

### The Conversion Path

In UE5's rendering pipeline:

```
DirectionalLight Intensity (10.0)
    ↓
Light Brightness = Intensity * LightColor
    = 10.0 * (1, 1, 1)
    = (10, 10, 10)
    ↓
Luminous Intensity (lm/sr) → SunDiscOuterSpaceLuminance
    ↓
Atmosphere Shader receives SunIlluminance in lux
```

**UzEngine mapping**:
- DirectionalLight Intensity: **10.0f** (lux equivalent)
- Sun color: **Linear white (1, 1, 1)**
- Effective illuminance = 10.0 lux (this is NOT Earth's sun ~130k lux, but a game-scaled value)

The atmosphere shader will use:
- `SunIlluminance` as a float3 (usually white, scaled by intensity)
- `SunDirection` from light rotation
- `SunSolidAngle` derived from `LightSourceAngle = 0.5357°`

### Mie Anisotropy & Forward Scattering

- **MieAnisotropy** = 0.8f → 80% forward scattering (halos around sun)
- **RayleighScattering** = physically-based blue spectrum (shorter wavelengths scatter more)
- **MieScattering** = achromatic (neutral particle scattering)

---

## 8. Scene Hierarchy of "Basic" Template

The Basic template creates:

```
Level
├── DirectionalLight (bAtmosphereSunLight=true)
├── SkyAtmosphere (responds to DirectionalLight)
├── SkyLight (captures scene for IBL)
├── ExponentialHeightFog (optional, usually disabled)
└── Geometry
    ├── Floor (static mesh plane)
    └── Sphere (static mesh for testing)
```

---

## 9. Key Values for UzEngine Implementation

### Per-Frame Constants to Pass to Shaders

| Shader Constant | UE5 Source | Value | Units |
|-----------------|-----------|-------|-------|
| `SunIlluminance` | DirectionalLight.Intensity × LightColor | (10, 10, 10) or scaled variant | lux/float3 |
| `SunDirection` | DirectionalLight rotation | Computed from transform | XYZ unit vector |
| `SunSolidAngle` | LightSourceAngle | 0.5357° → ~0.00068 steradians | radians (converted) |
| `AtmosphereBottomRadius` | SkyAtmosphere.BottomRadius | 6360.0 | km |
| `AtmosphereTopRadius` | BottomRadius + AtmosphereHeight | 6420.0 | km |
| `RayleighScatteringCoeff` | SkyAtmosphere.RayleighScattering × Scale | (0.0001917, 0.0000449, 0.0000000) | km⁻¹ (per km) |
| `MieScatteringCoeff` | SkyAtmosphere.MieScattering × Scale | (0.003996, 0.003996, 0.003996) | km⁻¹ |
| `MieAnisotropy` | SkyAtmosphere.MieAnisotropy | 0.8 | unitless |

### Unit Conversions

- **1 kilometer = 100,000 centimeters** (UzEngine unit)
- **Atmosphere scale**: 1 km at 6360 km radius ≈ 0.00016 (tiny in Earth terms)
- **LightSourceAngle 0.5357° to radians**: `0.5357 * π / 180 ≈ 0.00935 rad`
- **Sun solid angle**: `π * tan²(angle/2) ≈ 0.00068 steradians`

---

## Summary for UzEngine

To match UE5 Basic scene lighting:

1. **Create DirectionalLight**:
   - Intensity: 10.0
   - Color: (1, 1, 1) linear white
   - Direction: Matches sky sun direction
   - bAtmosphereSunLight: enabled

2. **Create SkyAtmosphere**:
   - Use Earth model (BottomRadius=6360, Height=60)
   - Physical Rayleigh/Mie scattering values (see table above)
   - No post-processing luminance scaling (factors = 1.0)

3. **Create SkyLight**:
   - Intensity: 1.0
   - Real-time capture from SkyAtmosphere + scene
   - Resolution: 128×128 minimum

4. **Optional ExponentialHeightFog**:
   - FogDensity: 0.02 (subtle)
   - FogHeightFalloff: 0.2
   - No directional inscattering (let sky atmosphere dominate)

5. **Shader Constants**:
   - Pass sun intensity × color as `SunIlluminance`
   - Pass LightSourceAngle as `SunSolidAngle` (converted to radians)
   - Pass atmosphere parameters (Rayleigh, Mie coefficients)

---

## References

- DirectionalLightComponent.cpp: Lines 988-1057 (constructor)
- LightComponent.cpp: Lines 427-496 (base constructor)
- SkyAtmosphereComponent.cpp: Lines 89-142 (constructor)
- SkyLightComponent.cpp: Lines 285-333 (constructor)
- ExponentialHeightFogComponent.cpp: Lines 64-103 (constructor)
