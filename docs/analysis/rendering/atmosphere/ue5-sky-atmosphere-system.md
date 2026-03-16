<!-- doc-meta
title: UE5 Sky Atmosphere System Architecture Analysis
description: Complete breakdown of UE5.6 sky atmosphere rendering, including render pass structure, Bruneton model implementation, component data, height fog integration, sky-to-IBL capture, and performance optimizations.
date: 2026-03-15
status: complete
source: W:\UE_5.6_Source (UE5.6 full source)
tags: [sky, atmosphere, bruneton, fog, ibl, render-graph, lut, scattering]
-->

# UE5 Sky Atmosphere System Architecture Analysis

## Executive Summary

UE5's Sky Atmosphere is a sophisticated system that renders physically-based atmospheric scattering using precomputed lookup tables (LUTs) and ray-marching. The architecture separates concerns into:

1. **Scene proxy pattern** — USkyAtmosphereComponent → FSkyAtmosphereSceneProxy → FSkyAtmosphereRenderSceneInfo
2. **Multiple render passes** — Transmittance, Multi-scattering, Sky-view, and Aerial perspective LUTs (precomputed)
3. **Render graph integration** — Async compute support, optional depth-bound optimization, depth test filtering
4. **Aerial perspective volume** — 3D froxel volume (32x32x16 default) for per-pixel atmospheric fog on any distance
5. **Height fog integration** — Unified fog parameters using sky atmosphere as light source
6. **Real-time reflection capture** — Sky atmosphere captured into cubemap for IBL

---

## 1. Render Pass Architecture

### 1.1 Pass Locations & Timing

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp`

```cpp
enum class ESkyAtmospherePassLocation : uint32
{
  BeforeBasePass,      // Default: kicks before BasePass (better overlap with DF shadows)
  BeforePrePass,       // When async compute enabled (overlaps with Pre-pass/Nanite rasterization)
};

// Location decision:
ESkyAtmospherePassLocation GetSkyAtmospherePassLocation()
{
  return CVarSkyAtmosphereAsyncCompute.GetValueOnAnyThread() != 0
    ? BeforePrePass
    : BeforeBasePass;
}
```

### 1.2 Pass Hierarchy

```
RenderSkyAtmosphereLookUpTables() [RDG pass]
  ├─ Transmittance LUT (256x64)
  │   └─ Ray march from camera through atmosphere to space
  │   └─ Stores: Transmittance (RGBA16F by default, R8BG8B8A8 if CVarSkyAtmosphereTransmittanceLUTUseSmallFormat=1)
  │
  ├─ Multi-Scattering LUT (32x32)
  │   └─ Dual-scattering approximation after transmittance
  │   └─ Ground bounce + secondary scattering
  │   └─ Factor: 2.0 recommended (CVarSkyAtmosphereMultiScatteringLUTHighQuality=0)
  │   └─ Stores: Luminance (RGBA16F)
  │
  ├─ Sky-View LUT (192x104 default, configurable)
  │   └─ Per-view lookup: angle → sky color
  │   └─ Input: ViewZenithCosAngle, AzimuthAngle
  │   └─ Output: Luminance (RGB10A2 or RGBA16F)
  │   └─ FastSky mode: replaces full per-pixel ray-march
  │
  └─ Aerial Perspective Volume (32x32x16 default)
      └─ 3D froxel volume in view frustum space
      └─ Per-slice ray-march samples: 2 (default)
      └─ Depth distribution: squared (sqrt bias toward near)
      └─ Stores: Luminance + Transmittance (per froxel)
      └─ Applied per-pixel in BasePass via froxel sample

RenderSkyAtmosphereInternal() [Per-view, after LUT generation]
  ├─ Full ray-march per pixel (if !FastSkyLUT or debug)
  │   └─ Uses: Transmittance + MultiScatter LUTs
  │   └─ Sample count: 2-32 (CVarSkyAtmosphere.SampleCountMin/Max)
  │   └─ Distance-based LOD: fewer samples in distance
  │
  └─ Fast Sky LUT lookup (if CVarSkyAtmosphereFastSkyLUT=1)
      └─ Look up pre-computed Sky-View LUT
      └─ Optional depth test: skip if depth < AerialPerspectiveStartDepth
```

### 1.3 Render Target Binding & Depth Handling

**FSkyAtmosphereRenderContext** (struct passed to all render functions):

```cpp
struct FSkyAtmosphereRenderContext
{
  // Optimization flags
  bool bUseDepthBoundTestIfPossible;        // Early depth test: skip if opaque depth < AP start
  bool bForceRayMarching;                   // Disable FastSky LUT fallback
  bool bDepthReadDisabled;                  // Don't sample scene depth (use far Z instead)
  bool bDisableBlending;                    // No blending (for reflection capture)
  bool bFastSky;                            // Enable Sky-View LUT
  bool bFastAerialPerspective;              // Enable AP volume instead of per-pixel trace
  bool bFastAerialPerspectiveDepthTest;     // Test depth before AP apply

  // LUT textures
  FRDGTextureRef TransmittanceLut;
  FRDGTextureRef MultiScatteredLuminanceLut;
  FRDGTextureRef SkyAtmosphereViewLutTexture;
  FRDGTextureRef SkyAtmosphereCameraAerialPerspectiveVolume;
  FRDGTextureRef SkyAtmosphereCameraAerialPerspectiveVolumeMieOnly;
  FRDGTextureRef SkyAtmosphereViewLutTexture;

  // Optional cloud integration
  bool bShouldSampleCloudShadow;
  bool bShouldSampleCloudSkyAO;
  FRDGTextureRef VolumetricCloudShadowMap[2];   // For volumetric shadow sampling
  FRDGTextureRef VolumetricCloudSkyAO;
  FRDGTextureRef VolumetricCloudDepthTexture;

  // MSAA support
  uint8 MSAASampleCount;
  FRDGTextureRef MSAADepthTexture;
};
```

**Key insight**: Sky atmosphere renders **only where there is no geometry** via depth test. Aerial perspective applies to all pixels (opaque + translucent).

---

## 2. Shader Structure & Bruneton Implementation

### 2.1 Core Atmosphere Model

**Files**:
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf` (main pixel shader)
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush` (shared math)

**Shader Variants** (via preprocessor defines):

```hlsl
// Key compile-time variants
#define TRANSMITTANCE_PASS 0              // 1 = computing transmittance LUT
#define MULTISCATT_PASS 0                 // 1 = computing multi-scattering LUT
#define SKYVIEWLUT_PASS 0                 // 1 = computing sky-view LUT
#define SKYLIGHT_PASS 0                   // 1 = computing sky light IBL

#define FASTSKY_ENABLED 0                 // 1 = use sky-view LUT instead of ray-march
#define FASTAERIALPERSPECTIVE_ENABLED 0   // 1 = use AP volume instead of per-pixel
#define SOURCE_DISK_ENABLED 0             // 1 = render sun disk
#define PER_PIXEL_NOISE 0                 // 1 = dither ray-march samples

#define SAMPLE_OPAQUE_SHADOW 0            // 1 = sample light shadow for transmittance
#define SAMPLE_CLOUD_SHADOW 0             // 1 = sample volumetric cloud shadow
#define SAMPLE_CLOUD_SKYAO 0              // 1 = sample volumetric cloud AO
#define VIRTUAL_SHADOW_MAP 0              // 1 = use VSM instead of CSM

#define SEPARATE_MIE_RAYLEIGH_SCATTERING 0 // 1 = separate Mie/Rayleigh channels
#define SECOND_ATMOSPHERE_LIGHT_ENABLED 0 // 1 = secondary light source
```

### 2.2 Ray-Sphere Intersection (Atmosphere Bounds)

```glsl
// Core geometry: planetary atmosphere as two concentric spheres
float RaySphereIntersectNearest(
  float3 RayOrigin, float3 RayDir,
  float3 SphereCenter, float SphereRadius)
{
  // Returns distance to nearest intersection, or -1 if no hit
  float2 Sol = RayIntersectSphere(RayOrigin, RayDir, float4(SphereCenter, SphereRadius));

  // Always traverse from outside-in, with planet radius safety edge:
  // PLANET_RADIUS_RATIO_SAFE_EDGE = 1.00000155763 (1 + 0.01km / 6420km)
  return Sol.y > 0.0 ? Sol.y : (Sol.x > 0.0 ? Sol.x : -1.0);
}
```

### 2.3 Transmittance LUT Structure

**Compute**: `RenderSkyAtmosphereLookUpTables()` → Transmittance pass

**UV encoding**:

```hlsl
// getTransmittanceLutUvs: Encode (viewHeight, zenithCosAngle) → UV [0,1]
void getTransmittanceLutUvs(
  in float viewHeight,                 // Height above ground (km)
  in float viewZenithCosAngle,         // cos(zenith angle) to ray direction
  in float BottomRadius, in float TopRadius,
  out float2 UV)
{
  float H = sqrt(max(0.0, TopRadius^2 - BottomRadius^2));  // Atmosphere height
  float Rho = sqrt(max(0.0, viewHeight^2 - BottomRadius^2)); // Horizontal distance from center

  // Distance along ray to atmosphere boundary
  float Discriminant = viewHeight^2 * (cos(zenith)^2 - 1.0) + TopRadius^2;
  float D = max(0.0, -viewHeight*cos(zenith) + sqrt(Discriminant));

  float Dmin = TopRadius - viewHeight;
  float Dmax = Rho + H;
  UV.x = (D - Dmin) / (Dmax - Dmin);     // Zenith angle → X [0,1]
  UV.y = Rho / H;                        // Height → Y [0,1]
}
```

**Physical parameters sampled**:

```hlsl
// Rayleigh scattering density profile (exponential falloff)
float RayleighDensity = exp(-viewHeight / RayleighExponentialDistribution);

// Mie scattering density profile (different decay rate)
float MieDensity = exp(-viewHeight / MieExponentialDistribution);

// Absorption (ozone layer, tent distribution)
// Peaks at 20-25km, zeros at edges
float AbsorptionDensity = TentDistribution(viewHeight,
  AbsorptionDensity0LayerWidth,
  AbsorptionDensity0ConstantTerm,
  AbsorptionDensity0LinearTerm);

// Integrate along ray path: transmittance = exp(-integral of extinction)
float3 Transmittance = exp(-(
    RayleighScattering * RayleighDensity
  + MieExtinction * MieDensity
  + AbsorptionExtinction * AbsorptionDensity
) * rayDistance);
```

### 2.4 Multi-Scattering LUT

**Purpose**: Approximate light bouncing after first scatter (ground + air)

**Structure**:
- Input: (LightZenith, ViewHeight)
- Output: Luminance of secondary scattered light
- Sample count: 2 (default, fast) or 64 (HighQuality mode)

```hlsl
// Dual-scattering approximation:
// 1. Light → Ground bounce (sample ground albedo)
// 2. Light → Particle → Eye (second scattering from that ground point)

// MultiScatteringFactor scale applied in shader:
// Luminance *= (1.0 + MultiScatteringFactor * MultiScatterLUT)
// Recommended: 2.0 for default sky realism
```

### 2.5 Sky-View LUT (FastSky)

**Purpose**: Per-view lookup table to replace full ray-march

**Inputs**: ViewZenith, Azimuth (spherical coords of view direction)

**Generation**: `SkyViewLutParamsToUv()` encodes direction into UV

```hlsl
void SkyViewLutParamsToUv(
  bool IntersectGround,                 // Does ray hit terrain?
  float ViewZenithCosAngle,
  float3 ViewDir,
  float ViewHeight,
  float BottomRadius,
  float4 SkyViewLutSizeAndInvSize,
  out float2 UV)
{
  float Vhorizon = sqrt(ViewHeight^2 - BottomRadius^2);  // Horizon distance
  float CosBeta = Vhorizon / ViewHeight;                  // Horizon angle
  float Beta = acosFast4(CosBeta);

  // Non-linear UV mapping to emphasize near-horizon details
  float2 UV_Base = float2(
    asinFast(ViewZenithCosAngle) / PI,   // Zenith → U
    atan2(ViewDir.y, ViewDir.x) / (2*PI) // Azimuth → V
  );

  // Apply non-linear weight near horizon (higher resolution there)
  // ...
}
```

**Resolution**: 192x104 default (configurable, CVarSkyAtmosphere.FastSkyLUT.Width/Height)

### 2.6 Aerial Perspective Volume (AP)

**Concept**: 3D froxel texture storing integrated fog per camera-relative cell

**Structure**:
- Dimensions: 32×32×16 (XY screen, Z depth) by default
- Depth distribution: **Non-linear (square root)** to emphasize near distances
- Slice thickness: 4 km per slice (AP_KM_PER_SLICE = 4.0)
- Total volume depth: 16 slices × 4 km = 64 km (configurable)
- Format: RGBA16F (Luminance.RGB + Transmittance.A)

**Depth coordinate calculation**:

```hlsl
// Linear depth from camera to surface (km)
float tDepth = max(0.0, length(WorldPositionRelativeToCamera) - AerialPerspectiveVolumeStartDepth);

// Linear slice in [0, DepthResolution]
float LinearSlice = tDepth * AerialPerspectiveVolumeDepthSliceLengthKmInv;

// Non-linear W: sqrt(LinearSlice / DepthResolution) for better near-distance detail
float LinearW = LinearSlice / DepthResolution;     // [0, 1]
float NonLinW = sqrt(LinearW);                     // sqrt(W) pushes detail toward near
float NonLinSlice = NonLinW * DepthResolution;

// Fade near camera (weight ramping from 0 to 1)
float Weight = 1.0;
if (NonLinSlice < HalfSliceDepth)  // sqrt(0.5) ≈ 0.707
{
  Weight = saturate(NonLinSlice * NonLinSlice * 2.0);  // Quadratic fade
}
Weight *= saturate(tDepth * NearFadeOutRangeInvDepthKm);  // 1cm fade region
```

---

## 3. Component Data Structure

### 3.1 USkyAtmosphereComponent Properties

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\SkyAtmosphereComponent.h`

```cpp
// Planet geometry
ESkyAtmosphereTransformMode TransformMode;  // How to interpret component transform
float BottomRadius;                         // Ground radius (km), default 6371
FColor GroundAlbedo;                        // Bounce light color

// Atmosphere layer geometry
float AtmosphereHeight;                     // Top radius - bottom radius (km)

// Multi-scattering control
float MultiScatteringFactor;                // Scale factor (0-100), default 2.0
                                            // 0 = single scatter only
                                            // 2.0 = realistic secondary scattering

// Rayleigh (molecule) scattering
float RayleighScatteringScale;              // 0-2 multiplier
FLinearColor RayleighScattering;            // RGB coefficients (1/km)
float RayleighExponentialDistribution;      // Scale height (km), ~8.5 for Earth

// Mie (particle) scattering
float MieScatteringScale;                   // 0-5 multiplier
FLinearColor MieScattering;                 // RGB coefficients (1/km)
float MieAbsorptionScale;                   // 0-5 multiplier
FLinearColor MieAbsorption;                 // Absorption coefficients (1/km)
float MieAnisotropy;                        // Forward scattering bias (0-0.999)
                                            // 0 = isotropic
                                            // 0.8+ = strong forward scattering (halos)
float MieExponentialDistribution;           // Scale height (km), ~1.2 for Earth

// Absorption layer (ozone approximation)
float OtherAbsorptionScale;                 // 0-0.2 multiplier
FLinearColor OtherAbsorption;               // Peak absorption value
FTentDistribution OtherTentDistribution;    // Altitude range (typically 10-40 km)

// Art direction
FLinearColor SkyLuminanceFactor;            // Tint to entire sky
FLinearColor SkyAndAerialPerspectiveLuminanceFactor; // Tint for AP too
float AerialPespectiveViewDistanceScale;    // 0-3, make fog thicker by scaling distances
float AerialPerspectiveStartDepth;          // Start AP fade at this distance (km)
float HeightFogContribution;                // 0-1, scale sky's contribution to height fog

// Sun tuning
float TransmittanceMinLightElevationAngle;  // Keep sun visible even below horizon (°)

// Rendering
uint8 bRenderInMainPass;                    // Render in BasePass (vs post-process)
uint8 bHoldout;                             // Render black with alpha=0
```

### 3.2 GPU Constant Buffer Structure

**FAtmosphereUniformShaderParameters** — uploaded once per component change:

```cpp
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FAtmosphereUniformShaderParameters, )
  SHADER_PARAMETER(float, MultiScatteringFactor)
  SHADER_PARAMETER(float, BottomRadiusKm)
  SHADER_PARAMETER(float, TopRadiusKm)

  SHADER_PARAMETER(float, RayleighDensityExpScale)
  SHADER_PARAMETER(FLinearColor, RayleighScattering)

  SHADER_PARAMETER(FLinearColor, MieScattering)
  SHADER_PARAMETER(float, MieDensityExpScale)
  SHADER_PARAMETER(FLinearColor, MieExtinction)
  SHADER_PARAMETER(float, MiePhaseG)
  SHADER_PARAMETER(FLinearColor, MieAbsorption)

  SHADER_PARAMETER(float, AbsorptionDensity0LayerWidth)
  SHADER_PARAMETER(float, AbsorptionDensity0ConstantTerm)
  SHADER_PARAMETER(float, AbsorptionDensity0LinearTerm)
  SHADER_PARAMETER(float, AbsorptionDensity1ConstantTerm)
  SHADER_PARAMETER(float, AbsorptionDensity1LinearTerm)
  SHADER_PARAMETER(FLinearColor, AbsorptionExtinction)

  SHADER_PARAMETER(FLinearColor, GroundAlbedo)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// Registering creates a UniformBuffer for each component
TUniformBufferRef<FAtmosphereUniformShaderParameters> AtmosphereUniformBuffer;
```

**FSkyAtmosphereInternalCommonParameters** — updated per-frame (contains computed values):

```cpp
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyAtmosphereInternalCommonParameters, )
  // Ray-march sample counts (LOD based on distance)
  SHADER_PARAMETER(float, SampleCountMin)      // 2 (cvar: r.SkyAtmosphere.SampleCountMin)
  SHADER_PARAMETER(float, SampleCountMax)      // 32 (cvar: r.SkyAtmosphere.SampleCountMax)
  SHADER_PARAMETER(float, DistanceToSampleCountMaxInv) // 1/150km

  // Fast Sky LUT sample counts (lower quality)
  SHADER_PARAMETER(float, FastSkySampleCountMin)
  SHADER_PARAMETER(float, FastSkySampleCountMax)
  SHADER_PARAMETER(float, FastSkyDistanceToSampleCountMaxInv)

  // Aerial Perspective volume parameters
  SHADER_PARAMETER(FVector4f, CameraAerialPerspectiveVolumeSizeAndInvSize)
  SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolution)    // 16
  SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolutionInv) // 1/16
  SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthKm)
  SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthKmInv)
  SHADER_PARAMETER(float, CameraAerialPerspectiveSampleCountPerSlice)      // 2

  // LUT dimensions (for UV wrapping & sampling)
  SHADER_PARAMETER(FVector4f, TransmittanceLutSizeAndInvSize)  // (256, 64, 1/256, 1/64)
  SHADER_PARAMETER(FVector4f, MultiScatteredLuminanceLutSizeAndInvSize)
  SHADER_PARAMETER(FVector4f, SkyViewLutSizeAndInvSize)

  // Sample counts for LUT generation
  SHADER_PARAMETER(float, TransmittanceSampleCount)       // 10 (for gen pass)
  SHADER_PARAMETER(float, MultiScatteringSampleCount)     // 15

  // Art direction
  SHADER_PARAMETER(float, AerialPespectiveViewDistanceScale)
  SHADER_PARAMETER(float, FogShowFlagFactor)  // 1 or 0 if show-flag disabled
  SHADER_PARAMETER(FVector3f, SkyLuminanceFactor)
  SHADER_PARAMETER(FVector3f, SkyAndAerialPerspectiveLuminanceFactor)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
```

---

## 4. Height Fog Integration

### 4.1 Unified Fog Parameters

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\FogRendering.h`

```cpp
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters,)
  // Exponential height fog (original UE fog)
  SHADER_PARAMETER(FVector4f, ExponentialFogParameters)   // (Density, HeightFalloff, ?, ?)
  SHADER_PARAMETER(FVector4f, ExponentialFogParameters2)
  SHADER_PARAMETER(FVector4f, ExponentialFogColorParameter)
  SHADER_PARAMETER(FVector4f, ExponentialFogParameters3)

  // Sky Atmosphere contribution to fog color & ambient light
  SHADER_PARAMETER(FVector4f, SkyAtmosphereAmbientContributionColorScale)

  // Directional inscattering (light from sky dome)
  SHADER_PARAMETER(FVector4f, InscatteringLightDirection)  // .W = start distance
  SHADER_PARAMETER(FVector4f, DirectionalInscatteringColor)
  SHADER_PARAMETER(FVector2f, SinCosInscatteringColorCubemapRotation)
  SHADER_PARAMETER(FVector3f, FogInscatteringTextureParameters)
  SHADER_PARAMETER(float, EndDistance)

  // Volumetric fog
  SHADER_PARAMETER(float, ApplyVolumetricFog)
  SHADER_PARAMETER(float, VolumetricFogStartDistance)
  SHADER_PARAMETER(float, VolumetricFogNearFadeInDistanceInv)
  SHADER_PARAMETER(FVector3f, VolumetricFogAlbedo)
  SHADER_PARAMETER(float, VolumetricFogPhaseG)

  // Textures
  SHADER_PARAMETER_TEXTURE(TextureCube, FogInscatteringColorCubemap)  // Sky cubemap
  SHADER_PARAMETER_SAMPLER(SamplerState, FogInscatteringColorSampler)
  SHADER_PARAMETER_RDG_TEXTURE(Texture3D, IntegratedLightScattering)  // Volumetric fog
  SHADER_PARAMETER_SAMPLER(SamplerState, IntegratedLightScatteringSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
```

### 4.2 Sky Atmosphere Affects Fog

**Parameter**: `HeightFogContribution` (0-1) on USkyAtmosphereComponent

When enabled (`r.SupportSkyAtmosphereAffectsHeightFog = 1`):
- Fog color is tinted by sky atmosphere color
- Fog ambient light uses sky atmosphere ambient
- Volumetric fog density can be modulated by sky atmosphere transmittance

**Setup flow** (in `SetupFogUniformParameters`):
1. Query SkyAtmosphere for ambient light color
2. Sample Transmittance LUT at player position + light direction
3. Modulate fog parameters by transmittance
4. Blend sky color into fog inscattering

---

## 5. Sky-to-IBL Capture (Reflection Probes)

### 5.1 Real-Time Capture Integration

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp` (lines 1467+)

When `USkyLightComponent.bRealTimeCaptureEnabled = true`:

```cpp
bool bRealTimeReflectionCaptureSkyAtmosphereViewLutTexture =
  Views.Num() > 0 && Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled;

if (bRealTimeReflectionCaptureSkyAtmosphereViewLutTexture)
{
  // Render 360° sky-view LUT for entire sky dome
  FRDGTextureRef ReflectionCaptureSkyLut = GraphBuilder.CreateTexture(
    FRDGTextureDesc::Create2D(FIntPoint(SkyViewLutWidth, SkyViewLutHeight),
                              PF_R10G10B10A2,  // or RGBA16F if CVarSkyAtmosphereLUT32
                              FClearValueBinding::Black,
                              TexCreate_ShaderResource | TexCreate_UAV)
  );

  // Shader renders with View.RenderingReflectionCaptureMask != 0
  // This produces cubemap-compatible panorama for real-time reflection capture

  Scene->RealTimeReflectionCaptureSkyAtmosphereViewLutTexture =
    ConvertToExternalAccessTexture(GraphBuilder, ReflectionCaptureSkyLut);
}
```

### 5.2 360° Aerial Perspective Capture

For scenes with sky materials or volumetric clouds, also capture AP volume as 360° panorama:

```cpp
bool bRealTimeReflectionCapture360APLutTexture =
  Views.Num() > 0 && Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled
  && (Views[0].bSceneHasSkyMaterial || Scene->HasVolumetricCloud());

if (bRealTimeReflectionCapture360APLutTexture)
{
  // Create 360 AP volume (spherical harmonics style)
  FRDGTextureRef ReflectionCaptureAP360 = GraphBuilder.CreateTexture(
    FRDGTextureDesc::Create2D(FIntPoint(APWidth, APHeight),
                              PF_A32B32G32R32F,  // Full precision for integration
                              FClearValueBinding::Black,
                              TexCreate_ShaderResource | TexCreate_UAV)
  );

  // Render per-ray AP evaluation for entire sphere
  Scene->RealTimeReflectionCaptureCamera360APLutTexture =
    ConvertToExternalAccessTexture(GraphBuilder, ReflectionCaptureAP360);
}
```

**Usage**: These textures feed into `USkyLightComponent::UpdateReflectionCapture()` to create IBL cubemap that includes atmospheric scattering.

---

## 6. Performance Optimizations

### 6.1 Console Variables for Scalability

| Cvar | Default | Purpose |
|------|---------|---------|
| `r.SkyAtmosphere` | 1 | Global on/off toggle |
| `r.SkyAtmosphere.FastSkyLUT` | 1 | Use pre-computed Sky-View LUT instead of per-pixel ray-march |
| `r.SkyAtmosphere.FastSkyLUT.SampleCountMax` | 32 | Samples for Sky-View LUT generation |
| `r.SkyAtmosphere.SampleCountMin` | 2 | Min samples for full ray-march (if !FastSky) |
| `r.SkyAtmosphere.SampleCountMax` | 32 | Max samples (full distance) |
| `r.SkyAtmosphere.DistanceToSampleCountMax` | 150 | Distance at which to use max samples (km) |
| `r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque` | 1 | Use AP volume for opaque (vs per-pixel) |
| `r.SkyAtmosphere.AerialPerspectiveDepthTest` | 1 | Skip AP before AP start depth |
| `r.SkyAtmosphere.TransmittanceLUT` | 1 | Generate transmittance LUT |
| `r.SkyAtmosphere.TransmittanceLUTUseSmallFormat` | 0 | Use R8GB8B8A8 instead of RGBA16F |
| `r.SkyAtmosphere.AsycCompute` | 0 | Run LUT generation on async compute queue (overlaps with pre-pass) |

### 6.2 Sample Count Interpolation

```hlsl
// In shader, interpolate sample count by distance
float DistanceScale = distance / CVarSkyAtmosphere.DistanceToSampleCountMax;
float SampleCountFloat = mix(SampleCountMin, SampleCountMax, saturate(DistanceScale));
int SampleCount = max(1, int(SampleCountFloat));
```

### 6.3 Early Depth Test Optimization

```hlsl
// If depth-bound test enabled and pixel depth < AP start, skip sky rendering
if (CVarSkyAtmosphereAerialPerspectiveDepthTest && SceneDepth < AerialPerspectiveStartDepth)
{
  discard;  // Pixel is opaque, geometry already rendered here
}
```

### 6.4 Async Compute Overlap

When `CVarSkyAtmosphereAsyncCompute = 1`:
- Sky LUT generation kicks **BeforePrePass**
- Overlaps compute work with rasterization (Pre-pass, Nanite)
- Texture barriers ensure ready before BasePass
- Cost: more complex sync, but GPU utilization better

---

## 7. Key Data Flows

### 7.1 Component Update → GPU

```
USkyAtmosphereComponent::OnComponentTransformed()
  ↓
FSceneProxy::SendRenderTransform()
  ↓
FSkyAtmosphereSceneProxy::SetTransform()
  ↓
Scene->GetSkyAtmosphereSceneInfo()->AtmosphereUniformBuffer
  ↓ (Render thread, next frame)
RenderSkyAtmosphereLookUpTables() regenerates LUTs
```

### 7.2 Render Thread Execution

```
FSceneRenderer::Render() / DeferredShadingRenderer
  ├─ AddSkyAtmosphereLookUpTablePass()
  │   └─ RenderSkyAtmosphereLookUpTables()
  │       └─ RenderTransmittanceLUT() [Compute pass]
  │       └─ RenderMultiScatteringLUT() [Compute pass]
  │       └─ RenderSkyViewLUT() [Compute pass]
  │       └─ RenderAerialPerspectiveVolume() [Compute pass]
  │
  ├─ BasePass
  │   └─ (Sky rendered here if bRenderInMainPass)
  │
  └─ PostProcess
      └─ (Sky rendered here if !bRenderInMainPass)
```

### 7.3 Per-Pixel Sampling

```hlsl
// In any shader (material, post-process, etc.)

// Sample atmospheric transmission from sun
float3 SunTransmittance = Texture2DSampleLevel(TransmittanceLut, SamplerLinearClamp,
  GetTransmittanceLutUvs(ViewHeight, SunZenithCos, BottomRadius, TopRadius), 0.0);

// Apply to lighting
DirectionalLightColor *= SunTransmittance;

// Sample sky color with aerial perspective
float4 SkyColor = Texture3DSample(SkyAtmosphereCameraAerialPerspectiveVolume,
  SamplerLinearClamp,
  FroxelCoord);  // (ScreenUV, NonLinearDepthW)

SceneColor = lerp(SceneColor, SkyColor.rgb, SkyColor.a);  // Blend with transmittance
```

---

## 8. Key Architectural Patterns for UzEngine Adaptation

### Pattern 1: **Scene Proxy for GPU Parameters**
- Component data (CPU) → Scene proxy (CPU read-only) → Uniform buffer (GPU)
- Changes invalidate/regenerate proxy but don't affect runtime performance
- Allows late changes without shader recompile

### Pattern 2: **LUT-based Sky Rendering**
- Precompute expensive atmospheric scattering offline per frame
- 4 LUTs: Transmittance (simple lookup), MultiScatter (2 bounces), SkyView (full hemisphere), AP (3D fog)
- Reduces per-pixel cost from O(32 samples) to O(1 texture lookup)

### Pattern 3: **View-Dependent Aerial Perspective Volume**
- 3D froxel cache in view-relative coordinates
- Regenerated per-frame per-view (matches camera frustum)
- Applied before BasePass via compute pass
- Non-linear depth distribution emphasizes near distances

### Pattern 4: **Render Pass Placement Flexibility**
- Can run before/after pre-pass depending on async compute availability
- Depth test optional (optimize by skipping where opaque exists)
- Integrates with both forward and deferred rendering

### Pattern 5: **Unified Fog System**
- Sky atmosphere acts as light source for height fog
- Same scattering parameters control both sky and fog color
- Aerial perspective volume applies to all surfaces (opaque + translucent)

### Pattern 6: **Real-Time IBL Capture**
- Sky atmosphere rendered to panorama texture
- Captures both direct sky color AND aerially-scattered light
- Feeds reflection probes automatically

---

## References

**Source Files**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp` (1793 lines)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.h`
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf`
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\SkyAtmosphereComponent.h` (250+ lines)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\FogRendering.h`

**Academic References** (implied in code):
- Bruneton et al. "Precomputed Atmospheric Scattering" (EGSR 2008)
- Hillaire et al. "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (SIGGRAPH 2020)

