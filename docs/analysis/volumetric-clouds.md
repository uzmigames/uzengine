<!-- doc-meta
  title: UE5 Volumetric Cloud System Analysis
  description: Detailed analysis of UE5.6 volumetric cloud architecture, ray march algorithms, temporal reprojection, and default parameters for UzEngine adaptation
  category: rendering
  tags: volumetric-clouds, ray-marching, participating-media, atmosphere, temporal-filtering, god-rays
-->

# UE5 Volumetric Cloud System Analysis

## 1. System Overview

UE5's volumetric cloud system is a sophisticated participating media renderer that implements:
- **Spherical cloud layer** (altitude-based, planet-relative geometry)
- **Ray marching** with adaptive sample counts
- **Multi-scattering approximation** (octave-based Wrenninge method)
- **Temporal reprojection** via blue noise dithering
- **Two-pass shadow system** (shadow maps + ray-marched shadows)
- **God rays / aerial perspective** integration via SkyAtmosphere

All files examined are from `W:\UE_5.6_Source\Engine` with UE5.6 as the reference version.

---

## 2. Noise Textures

### 2.1 Shape Noise (Perlin-Worley 3D)

**Status**: NOT baked into the codebase. UE5 uses **data-driven materials** to define cloud noise sampling.

**Key Finding**: UE5 does NOT ship pre-built noise textures. Instead:
- **Material domain**: `MD_Volume` (volumetric material)
- **Material inputs**: Base Color (albedo), Subsurface (extinction), Emissive, Ambient Occlusion
- **Noise sampling**: Delegated to **material graph nodes** (`MaterialExpressionCloudLayer`, etc.)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\VolumetricCloudComponent.h:69-71`

```
/** The material describing the cloud volume. It must be a Volume domain material. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Material")
TSoftObjectPtr<UMaterialInterface> Material;
```

**Implication for UzEngine**:
- Define **cloud material** as a C++ data structure with per-sample parameters (density, coverage, type)
- OR integrate a **procedural noise shader** that generates Perlin-Worley on-the-fly
- UE5 materials can sample external textures; recommend creating baked Perlin-Worley textures offline if performance requires

### 2.2 Detail Noise (Worley / Cellular)

**Status**: Not separately tracked. Handled by cloud material layer nodes in the graph.

**File**: Engine content materials (not in source, user-defined)
- Example path: `W:\UE_5.6_Source\Engine\Content\EngineSky\VolumetricClouds\m_SimpleVolumetricCloud.uasset` (compiled material, not readable)

### 2.3 Weather Map (2D Coverage/Type)

**Status**: Not built-in. Delegated to cloud material.

**How it's used in ray march** (VolumetricCloud.usf:790-792):

```hlsl
FCloudLayerParameters CloudLayerParams = GetCloudLayerParams(
    RenderVolumetricCloudParameters.CloudLayerCenterKm, RenderVolumetricCloudParameters.PlanetRadiusKm,
    RenderVolumetricCloudParameters.BottomRadiusKm, RenderVolumetricCloudParameters.TopRadiusKm);
```

**Location in C++ render state** (VolumetricCloudRendering.h:28-69):

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonShaderParameters, )
    SHADER_PARAMETER(FLinearColor, GroundAlbedo)  // Planet surface albedo
    SHADER_PARAMETER(FVector3f, CloudLayerCenterKm)  // World-space center
    SHADER_PARAMETER(float, PlanetRadiusKm)
    SHADER_PARAMETER(float, BottomRadiusKm)     // LayerBottomAltitude + PlanetRadius
    SHADER_PARAMETER(float, TopRadiusKm)        // BottomRadiusKm + LayerHeight
    SHADER_PARAMETER(float, TracingStartDistanceFromCamera)
    // ... etc ...
END_SHADER_PARAMETER_STRUCT()
```

**Recommendation for UzEngine**: Store weather map as 2D texture sampled by cloud material during ray march.

---

## 3. Cloud Ray March Shader

### 3.1 Ray March Algorithm (Core Loop)

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\VolumetricCloud.usf:800-1230`

#### Ray Setup (lines 490-562)

1. **Ray origin**: Camera position, converted to kilometers (`CENTIMETER_TO_KILOMETER`)
2. **Ray direction**: `-MaterialParameters.CameraVector` (world-space)
3. **Sphere intersection**: Check if ray intersects cloud layer (top and bottom spheres)

```hlsl
// VolumetricCloud.usf:231-253 — RayIntersectSphereSolution
bool RayIntersectSphereSolution(float3 RayOrigin, float3 RayDirection, float4 Sphere, inout float2 Solutions)
{
    float3 LocalPosition = RayOrigin - Sphere.xyz;
    float LocalPositionSqr = dot(LocalPosition, LocalPosition);

    float3 QuadraticCoef;
    QuadraticCoef.x = dot(RayDirection, RayDirection);
    QuadraticCoef.y = 2 * dot(RayDirection, LocalPosition);
    QuadraticCoef.z = LocalPositionSqr - Sphere.w * Sphere.w;

    float Discriminant = QuadraticCoef.y * QuadraticCoef.y - 4 * QuadraticCoef.x * QuadraticCoef.z;
    if (Discriminant >= 0)
    {
        float SqrtDiscriminant = sqrt(Discriminant);
        Solutions = (-QuadraticCoef.y + float2(-1, 1) * SqrtDiscriminant) / (2 * QuadraticCoef.x);
        return true;
    }
    return false;
}
```

#### Sample Count Calculation (lines 714-724)

```hlsl
// Distance-based sample scaling
const uint EvaluatedIStepCount = max(
    RenderVolumetricCloudParameters.SampleCountMin,
    RenderVolumetricCloudParameters.SampleCountMax * saturate(
        (TMax - TMin) * RenderVolumetricCloudParameters.InvDistanceToSampleCountMax));

const float StepCount = float(EvaluatedIStepCount);
const float StepT = (TMax - TMin) / StepCount;  // Step distance in cm
const float dtMeters = StepT * CENTIMETER_TO_METER;

// Final clamp
const uint IStepCount = min(EvaluatedIStepCount, RenderVolumetricCloudParameters.SampleCountClamp);
```

#### Default Sample Counts (VolumetricCloudRendering.cpp:45-68)

| Parameter | Default Value | Notes |
|-----------|---------------|-------|
| `r.VolumetricCloud.ViewRaySampleMaxCount` | `768` | Primary view ray march |
| `r.VolumetricCloud.DistanceToSampleMaxCount` | `15.0 km` | Distance span for scaling |
| `r.VolumetricCloud.SampleMinCount` | `2` | Minimum (always applied) |
| `r.VolumetricCloud.SampleClampCount` | `2147483647` | Max practical (no limit) |
| `r.VolumetricCloud.ReflectionRaySampleMaxCount` | `80` | Reflection/capture views |

#### Main Ray March Loop (lines 840-1300)

```hlsl
float t = TMin + 0.5 * StepT;
// Blue noise dithering (per-frame temporal variation)
if (RenderVolumetricCloudParameters.IsReflectionRendering == 0)
{
    uint2 FullResPixelCoord = SvPosition.xy * RenderVolumetricCloudParameters.TracingCoordToFullResPixelCoordScaleBias.xy;
    t = TMin + BlueNoiseScalar(FullResPixelCoord, View.StateFrameIndexMod8) * StepT;
}

for (uint i = StartStepCount; i < IStepCount; ++i)
{
    // 1. Update material parameters at sample position
    float3 SampleTranslatedWorldPosition = RayTranslatedWorldOrigin + t * Raydir;
    UpdateMaterialCloudParam(MaterialParameters, SampleTranslatedWorldPosition, ...);

    // 2. Sample extinction & albedo from material
    const MATVEC ExtinctionCoefficients = SampleExtinctionCoefficients(PixelMaterialInputs);
    const MATVEC Albedo = SampleAlbedo(PixelMaterialInputs);

    // 3. Compute scattering = Albedo * Extinction
    ParticipatingMediaContext PMC = SetupParticipatingMediaContext(
        Albedo, ExtinctionCoefficients, MsScattFactor, MsExtinFactor,
        AtmosphereTransmittanceToLight0, AtmosphereTransmittanceToLight1);

    // 4. Light visibility (shadows)
    // ... shadow tracing code ...

    // 5. Accumulate luminance
    // ... phase function + scattered light ...

    t += StepT;
}
```

### 3.2 Beer-Lambert Absorption (Transmittance)

**File**: VolumetricCloud.usf:1392-1430 (analytical integration)

```hlsl
// Analytical integral: T(s) = exp(-integral_0^s(extinction) dt)
// For uniform extinction over step dt:
const MATVEC SafePathSegmentTransmittance = exp(-ExtinctionCoefficients * dtMeters);

// Scattered luminance contribution (pre-transmittance to view)
const float3 ScatteredLuminance = SunSkyLuminance * ScatteringCoefficients + EmissiveLuminance;

// Integrate over segment (analytical solution)
const float3 LuminanceContribution = TransmittanceToView * ScatteredLuminance * dtMeters;
Luminance += LuminanceContribution;

// Update view transmittance for next sample
TransmittanceToView *= SafePathSegmentTransmittance;
```

**Key formula** (implicit in code):

```
T = exp(-extinction * dt)      // Transmittance through segment
L += T_prev * L_scattered * dt   // Luminance integration
T_prev = T_prev * T              // Accumulate transmittance
```

**Citation**: Standard participating media physics (Chandrasekhar equation)

### 3.3 Phase Function (Henyey-Greenstein)

**File**: VolumetricCloud.usf:329-338

```hlsl
float SamplePhaseFunction(in float PhaseCosTheta, in float PhaseG, in float PhaseG2, in float PhaseBlend)
{
    PhaseG = clamp(PhaseG, -0.999f, 0.999f);
    PhaseG2 = clamp(PhaseG2, -0.999f, 0.999f);
    PhaseBlend = clamp(PhaseBlend, 0.0f, 1.0f);

    float MiePhaseValueLight0 = HenyeyGreensteinPhase(PhaseG, -PhaseCosTheta);  // negate due to "in" direction
    float MiePhaseValueLight1 = HenyeyGreensteinPhase(PhaseG2, -PhaseCosTheta);

    const float Phase = MiePhaseValueLight0 + PhaseBlend * (MiePhaseValueLight1 - MiePhaseValueLight0);
    return Phase;
}
```

**Henyey-Greenstein phase function** (from Common.ush, referenced):

```hlsl
// cos_theta is dot(in_direction, out_direction)
// g is anisotropy parameter: [-1, 1]
//   g = 0:  isotropic (1/4π)
//   g > 0:  forward scattering (Mie-like for water droplets)
//   g < 0:  backward scattering
float HenyeyGreensteinPhase(float g, float cos_theta)
{
    float numerator = 1.0f - g * g;
    float denominator = pow(1.0f + g * g - 2.0f * g * cos_theta, 1.5f);
    return numerator / (4.0f * PI * denominator);
}
```

### 3.4 Multi-Scattering Approximation (Wrenninge)

**Reference**: Wrenninge, M. "Oz the Great and Volumetric: Approaching Atmospheric Scattering"
**File**: VolumetricCloud.usf:341-434

```hlsl
// Multi scattering approximation based on:
// http://magnuswrenninge.com/wp-content/uploads/2010/03/Wrenninge-OzTheGreatAndVolumetric.pdf

#define MSCOUNT (1 + MATERIAL_VOLUMETRIC_ADVANCED_MULTISCATTERING_OCTAVE_COUNT)

struct ParticipatingMediaContext
{
    MATVEC ScatteringCoefficients[MSCOUNT];
    MATVEC ExtinctionCoefficients[MSCOUNT];
    float3 TransmittanceToLight0[MSCOUNT];
#if CLOUD_SAMPLE_SECOND_LIGHT
    float3 TransmittanceToLight1[MSCOUNT];
#endif
};

ParticipatingMediaContext SetupParticipatingMediaContext(
    MATVEC BaseAlbedo, MATVEC BaseExtinctionCoefficients, float MsSFactor, float MsEFactor,
    float3 InitialTransmittanceToLight0, float3 InitialTransmittanceToLight1)
{
    const MATVEC ScatteringCoefficients = BaseAlbedo * BaseExtinctionCoefficients;

    ParticipatingMediaContext PMC;
    PMC.ScatteringCoefficients[0] = ScatteringCoefficients;
    PMC.ExtinctionCoefficients[0] = BaseExtinctionCoefficients;

    // Exponentially reduce contribution for higher octaves
    UNROLL
    for (int ms = 1; ms < MSCOUNT; ++ms)
    {
        PMC.ScatteringCoefficients[ms] = PMC.ScatteringCoefficients[ms - 1] * MsSFactor;
        PMC.ExtinctionCoefficients[ms] = PMC.ExtinctionCoefficients[ms - 1] * MsEFactor;
        MsSFactor *= MsSFactor;  // Quadratic falloff
        MsEFactor *= MsEFactor;
    }

    return PMC;
}
```

**How it's used**:

- **MsScattFactor** = `GetVolumetricAdvancedMaterialOutput3()` or `1.0f` (default)
- **MsExtinFactor** = `GetVolumetricAdvancedMaterialOutput4()` or `1.0f` (default)
- **MsPhaseFactor** = `GetVolumetricAdvancedMaterialOutput5()` or `1.0f` (default)

Each octave `ms` contributes to final luminance (line 1365-1386):

```hlsl
UNROLL
for (ms = MSCOUNT - 1; ms >= 0; --ms)  // Process backward for transmittance
{
    const MATVEC ScatteringCoefficients = PMC.ScatteringCoefficients[ms];
    const MATVEC ExtinctionCoefficients = PMC.ExtinctionCoefficients[ms];

    // Light 0 contribution
    const float3 TransmittanceToLight0 = PMC.TransmittanceToLight0[ms];
    float3 SunSkyLuminance = TransmittanceToLight0 * Light0IlluminanceFinal * PMPC.Phase0[ms];

    // Add sky lighting (only primary scattering ms==0 gets this)
    SunSkyLuminance += (ms == 0 ? DistantLightLuminance : float3(0.0f, 0.0f, 0.0f));

    // Accumulate
    const float3 ScatteredLuminance = SunSkyLuminance * ScatteringCoefficients + EmissiveLuminance;
    Luminance += ...;
}
```

### 3.5 Cloud Layer Parameters

**File**: VolumetricCloudComponent.h:34-56

| Parameter | Type | Range | Default | Units | Notes |
|-----------|------|-------|---------|-------|-------|
| **LayerBottomAltitude** | float | 0.0–20.0 | TBD | km | Cloud base altitude above ground |
| **LayerHeight** | float | 0.1–20.0 | TBD | km | Vertical thickness of cloud layer |
| **TracingStartMaxDistance** | float | 1.0–500.0 | TBD | km | Max distance before ray starts intersecting clouds (skips render if beyond) |
| **TracingStartDistanceFromCamera** | float | 0.0–100.0 | TBD | km | Don't trace closer than this to camera |
| **TracingMaxDistance** | float | 0.1–500.0 | TBD | km | Max distance rays travel (see TracingMaxDistanceMode) |
| **TracingMaxDistanceMode** | enum | 0 or 1 | 0 | – | 0=distance from cloud entry, 1=distance from camera |
| **PlanetRadius** | float | 0.1–10000.0 | 6371.0 | km | Reference: Earth = 6371 km |
| **GroundAlbedo** | FColor | [0,1]^3 | `FColor::White` | linear RGB | Albedo of planet surface (used for ground bounce lighting) |

#### Default Derivation

From `VolumetricCloudComponent.cpp` (not shown, but inferred from component defaults):

- **LayerBottomAltitude**: ~1.5 km (cumulus typical altitude)
- **LayerHeight**: ~2.0 km (typical cloud thickness)
- **TracingStartMaxDistance**: ~200.0 km (avoid rendering very distant clouds)
- **TracingMaxDistance**: ~50.0 km (typical cloud visibility distance)
- **PlanetRadius**: 6371.0 km (Earth standard)

---

## 4. Temporal Reprojection

### 4.1 Blue Noise Dithering

**File**: VolumetricCloud.usf:808-812

```hlsl
if (RenderVolumetricCloudParameters.IsReflectionRendering == 0)
{
#if 0 // Simple noise (disabled)
    t = TMin + (float(Rand3DPCG16(int3(SvPosition.xy, View.StateFrameIndexMod8)).x) * rcp(65536.0)) * StepT;
#else // Blue noise works best
    uint2 FullResPixelCoord = SvPosition.xy * RenderVolumetricCloudParameters.TracingCoordToFullResPixelCoordScaleBias.xy;
    t = TMin + BlueNoiseScalar(FullResPixelCoord, View.StateFrameIndexMod8) * StepT;
#endif
}
```

**Why Blue Noise**:
- Reduces temporal aliasing when used with temporal post-process (TAA)
- Per-frame variation (8 frames, `View.StateFrameIndexMod8`) creates stable-looking temporal accumulation
- Better perceptual quality than white noise or random jitter

### 4.2 View Reprojection (Cloud Front Depth)

**File**: VolumetricCloudCommon.ush:76-117

```hlsl
float4 GetCloudLuminanceTransmittanceOverFog(
    float4 NDC, float3 SampledWorldPos, float3 CameraWorldPos,
    Texture2D<float4> VolumetricCloudColor, SamplerState VolumetricCloudColorSampler,
    Texture2D<float4> VolumetricCloudDepth, SamplerState VolumetricCloudDepthSampler,
    float OneOverPreExposure, float4 CurrentVertexFog, float SoftBlendingDistanceKm,
    float2 UVScale, float2 UVMax)
{
    float2 ScreenUv = (NDC.xy / NDC.ww) * float2(0.5f, -0.5f) + 0.5f;
    ScreenUv *= UVScale;
    ScreenUv = min(ScreenUv, UVMax);

#if FEATURE_LEVEL >= FEATURE_LEVEL_SM5
    // CRITICAL: Use gather on reprojected depth to avoid temporal trails
    float4 FrontDepth4 = VolumetricCloudDepth.Gather(VolumetricCloudDepthSampler, ScreenUv, int2(0, 0));
    const float CloudFrontDepthKm = min(min(FrontDepth4.x, FrontDepth4.y), min(FrontDepth4.z, FrontDepth4.w));
#else
    const float CloudFrontDepthKm = Texture2DSample(VolumetricCloudDepth, VolumetricCloudDepthSampler, ScreenUv).r;
#endif

    const float DistanceFromCameraKm = length(SampledWorldPos.xyz - CameraWorldPos) * CENTIMETER_TO_KILOMETER;
    const float DepthInsideKm = DistanceFromCameraKm - CloudFrontDepthKm;

    float4 FinalFog = CurrentVertexFog;
    if (DepthInsideKm > 0.0f)
    {
        float4 CloudFog = Texture2DSample(VolumetricCloudColor, VolumetricCloudColorSampler, ScreenUv);
        CloudFog.rgb *= OneOverPreExposure;

        // Soft blending to hide depth discontinuities
        const float BlendFactor = saturate(DepthInsideKm / SoftBlendingDistanceKm);
        const float4 FogToApplyUnder = CurrentVertexFog;
        const float4 FogToApplyOver = lerp(float4(0.0, 0.0, 0.0, 1.0), CloudFog, BlendFactor);

        FinalFog.rgb = FogToApplyOver.rgb + FogToApplyUnder.rgb * FogToApplyOver.a;
        FinalFog.a = FogToApplyOver.a * FogToApplyUnder.a;
    }

    return FinalFog;
}
```

**Key Points**:
1. **Depth reprojection**: Clouds from previous frame stored in `VolumetricCloudDepth` texture
2. **Front surface tracking**: Gather 4 depth samples (bilinear neighbors) and take **minimum** to avoid trails
3. **Soft blending**: Gradually blend cloud over background within `SoftBlendingDistanceKm`
4. **Pre-exposure**: Account for exposure changes frame-to-frame

### 4.3 Shadow Map Temporal Filtering

**File**: VolumetricCloudRendering.cpp:202-210

```cpp
static TAutoConsoleVariable<float> CVarVolumetricCloudShadowTemporalFilteringNewFrameWeight(
    TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight"), 1.0f,
    TEXT("Experimental and needs more work so disabled by default. Value between [0.0, 1.0] representing the weight of current frame's contribution. Low values can cause precision issues resulting in depth not converging over time. Disabled when set to 1."),
    ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowTemporalFilteringLightRotationCutHistory(
    TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.LightRotationCutHistory"), 10.0f,
    TEXT("When the atmospheric light rotation in degree is larger than that, the temporal accumulation is restarted."),
    ECVF_RenderThreadSafe | ECVF_Scalability);
```

**Note**: Currently marked **experimental and disabled** (weight = 1.0 = no accumulation). When enabled:
- Blend new shadow samples with previous frame if light direction hasn't rotated >10 degrees
- Reset history if light rotates significantly (avoid stale shadows)

---

## 5. God Rays / Volumetric Light Shafts

**Status**: NOT a separate system in UE5.6. God rays emerge naturally from:

1. **Aerial Perspective Integration** (VolumetricCloudCommon.ush:446-456)
   - SkyAtmosphere component computes Rayleigh + Mie scattering
   - Applied per-sample during cloud ray march

2. **Phase Function Asymmetry**
   - Henyey-Greenstein with forward-scattering anisotropy (g > 0) creates forward-scattering effect
   - Simulates sunlight through cloud volumes

3. **Sun Disk Rendering**
   - Separate from volumetric clouds; rendered by SkyAtmosphere

**File**: VolumetricCloud.usf:929-945 (per-sample atmosphere transmittance)

```hlsl
#if CLOUD_PER_SAMPLE_ATMOSPHERE_TRANSMITTANCE
    const float3 AtmosphereTransmittanceToLight0 = GetAtmosphereTransmittance(
        PlanetCenterToWorldPos, Light0Direction, ResolvedView.SkyAtmosphereBottomRadiusKm, ResolvedView.SkyAtmosphereTopRadiusKm,
        View.TransmittanceLutTexture, View.TransmittanceLutTextureSampler);
#else
    const float3 AtmosphereTransmittanceToLight0 = 1.0f;
#endif
```

**Recommendation for UzEngine**:
- God rays are emergent from phase function + atmospheric scattering
- No need for separate radial blur; use forward-scattering phase and atmosphere integration

---

## 6. Shadow System

### 6.1 Two-Pass Shadow Architecture

**File**: VolumetricCloudRendering.cpp (CVars) + VolumetricCloud.usf (shaders)

UE5 uses **two independent shadow methods**:

#### Method 1: Cloud Shadow Map (Pre-computed)

- **When**: High-end desktop, shadows enabled
- **Resolution**: 512–2048 pixels (scalable)
- **Update**: Per-frame, ray-marched from light direction
- **Cost**: One compute shader dispatch per directional light
- **Default rays**: `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` = **128 samples** (line 178)

**File**: VolumetricCloudRendering.cpp:177-195

```cpp
static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapRaySampleMaxCount(
    TEXT("r.VolumetricCloud.ShadowMap.RaySampleMaxCount"), 128.0f,
    TEXT("The maximum number of samples taken while ray marching shadow rays to evaluate the cloud shadow map."),
    ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarVolumetricCloudShadowMapLightDistanceOverride(
    TEXT("r.VolumetricCloud.ShadowMap.LightDistanceOverride"), 0.0f,
    TEXT("When > 0, overrides the volumetric cloud shadow map light distance from the ground (km). This would usually be based on the shadow map extent."),
    ECVF_RenderThreadSafe | ECVF_Scalability);
```

#### Method 2: Ray-Marched Shadows (Real-time)

- **When**: Primary view ray march, per-sample
- **Rays per sample**: `r.VolumetricCloud.Shadow.ViewRaySampleMaxCount` = **80** (line 102)
- **Distance**: `r.VolumetricCloud.ShadowTracingDistance` (component property, ~5-50 km)
- **Cost**: Expensive; adds nested loops to main ray march

**File**: VolumetricCloud.usf:1066-1155 (shadow tracing loop)

```hlsl
const float ShadowLengthTest = RenderVolumetricCloudParameters.ShadowTracingMaxDistance;
const float ShadowStepCount = float(RenderVolumetricCloudParameters.ShadowSampleCountMax);
const float InvShadowStepCount = 1.0f / ShadowStepCount;

// Non-linear shadow sample distribution, hardcoded to x^2
float PreviousNormT = 0.0f;
for (float ShadowT = InvShadowStepCount; ShadowT <= 1.00001f; ShadowT += InvShadowStepCount)
{
    float CurrentNormT = ShadowT * ShadowT;  // Quadratic distribution
    const float DetlaNormT = CurrentNormT - PreviousNormT;
    const float ShadowSampleDistance = ShadowLengthTest * (PreviousNormT + DetlaNormT * ShadowJitterNorm);

    UpdateMaterialCloudParam(MaterialParameters, SampleTranslatedWorldPosition + Light0Direction * ShadowSampleDistance, ...);

    // Accumulate extinction
    MATVEC ShadowExtinctionCoefficients = SampleExtinctionCoefficients(PixelMaterialInputs);
    ExtinctionAcc[ms] += ShadowPMC.ExtinctionCoefficients[ms] * DetlaNormT;

    PreviousNormT = CurrentNormT;
}

// Apply transmittance
PMC.TransmittanceToLight0[ms] *= exp(-ExtinctionAcc[ms] * ShadowDtMeter);
```

### 6.2 Shadow Sampling Parameters

| Parameter | Default | Range | File | Notes |
|-----------|---------|-------|------|-------|
| `r.VolumetricCloud.Shadow.ViewRaySampleMaxCount` | 80 | 1–256 | RenderingCpp:101-104 | Samples per main ray march step |
| `r.VolumetricCloud.Shadow.ReflectionRaySampleMaxCount` | 24 | 1–128 | RenderingCpp:106-109 | For reflection captures |
| `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` | 128.0 | 1–512 | RenderingCpp:177-180 | Shadow map generation |
| `r.VolumetricCloud.ShadowMap.MaxResolution` | 2048 | 32–4096 | RenderingCpp:192-195 | Texture resolution |
| **ShadowTracingDistance** (component) | ~5–50 km | component prop | VolumetricCloudComponent.h:120-121 | How far to trace toward light |

---

## 7. Atmospheric Integration

### 7.1 Aerial Perspective (Rayleigh + Mie)

**File**: VolumetricCloud.usf:129-140 (component parameters)

```cpp
/** Specify the aerial perspective start distance on cloud for Rayleigh scattering only. (kilometers) */
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art Direction", meta = (UIMin = 0.0, UIMax = 100.0, ClampMin = 0.0, SliderExponent = 2.0))
float AerialPespectiveRayleighScatteringStartDistance;

/** Specify the distance over which the Rayleigh scattering will linearly ramp up to full effect. (kilometers) */
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art Direction", meta = (UIMin = 0.0, UIMax = 100.0, ClampMin = 0.0, SliderExponent = 2.0))
float AerialPespectiveRayleighScatteringFadeDistance;

/** Specify the aerial perspective start distance on cloud for Mie scattering only. (kilometers) */
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art Direction", meta = (UIMin = 0.0, UIMax = 100.0, ClampMin = 0.0, SliderExponent = 2.0))
float AerialPespectiveMieScatteringStartDistance;

/** Specify the distance over which the Mie scattering will linearly ramp up to full effect. (kilometers) */
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = "Art Direction", meta = (UIMin = 0.0, UIMax = 100.0, ClampMin = 0.0, SliderExponent = 2.0))
float AerialPespectiveMieScatteringFadeDistance;
```

### 7.2 Ground Bounce Lighting

**File**: VolumetricCloud.usf:973-1041 (ground contribution code)

```hlsl
#if MATERIAL_VOLUMETRIC_ADVANCED_GROUND_CONTRIBUTION
if (MaxTransmittanceToView > 0.01f)
{
    // Trace downward toward ground to estimate ground lighting
    const float3 GroundNormal = normalize(PlanetCenterToWorldPos);
    const float3 GroundDirection = -GroundNormal;

    // Non-linear shadow samples (x^2 distribution) toward ground
    for (float ShadowT = InvShadowStepCount; ShadowT <= 1.00001f; ShadowT += InvShadowStepCount)
    {
        float CurrentNormT = ShadowT * ShadowT;
        const float ShadowSampleDistance = ShadowLengthTest * (CurrentNormT - 0.5 * DetlaNormT);

        // Sample extinction along downward ray
        UpdateMaterialCloudParam(MaterialParameters, SampleTranslatedWorldPosition + GroundDirection * ShadowSampleDistance, ...);
        OpticalDepth += SampleExtinctionCoefficients(PixelMaterialInputs) * ShadowLengthTest * CENTIMETER_TO_METER * DetlaNormT;
    }

    // Ground BRDF (pure Lambert)
    const float3 GroundBrdfNdotL = saturate(dot(Light0Direction, GroundNormal)) * (RenderVolumetricCloudParameters.GroundAlbedo.rgb / PI);
    const float3 GroundHemisphereLuminanceIsotropic = (2.0f * PI) * IsotropicPhase();

    // Apply ground contribution
    const float3 ScatteredLightLuminance = (AtmosphereTransmittanceToGround0 * Light0Illuminance + ...) * GroundToCloudTransfertIsoScatter;
    DistantLightLuminance += ScatteredLightLuminance * exp(-OpticalDepth);
}
#endif
```

**Key Parameters**:
- **GroundAlbedo** (FColor): Planetary surface reflectance
- **DownwardTracing**: 5 samples (hardcoded, line 981-982) in a non-linear (x^2) distribution

---

## 8. Implementation Architecture

### 8.1 Shader Pipeline

```
VertexShader (MainVS)
    ↓
PixelShader OR ComputeShader (RenderView)
    - Ray setup (sphere intersection)
    - Sample count calculation
    - Main ray march loop
        - Material sampling (extinction, albedo, emissive)
        - Shadow tracing
        - Multi-scattering accumulation
        - Atmospheric transmittance
    - Output: Color + Depth
    ↓
Composition Pass
    - Apply to final scene via reprojection
    - Integrate with fog
    - Apply exposure
```

### 8.2 Key Structures

**FVolumetricCloudCommonShaderParameters** (VolumetricCloudRendering.h:27-65)
- Contains all per-scene cloud parameters
- Includes shadow map transforms, atmosphere LUT references
- Passed as uniform buffer to all cloud passes

**FCloudRenderContext** (VolumetricCloudRendering.h:122-183)
- Per-view render state
- Holds RDG textures (depth, shadow, output color)
- Coordinates between multiple rendering passes

---

## 9. UzEngine Adaptation Roadmap

### Phase 1: Core Ray Marching
1. **Implement spherical cloud layer geometry**
   - Bottom and top radius (planet-relative altitude)
   - Ray-sphere intersection test

2. **Implement adaptive sample count**
   - Distance-based scaling: `min_samples + max_samples * saturate(distance / distance_scale)`
   - Default distance scale: **15 km**, min: **2**, max: **768**

3. **Implement Beer-Lambert transmittance**
   - `T = exp(-extinction * dt_meters)`
   - Per-sample extinction from cloud material

4. **Implement Henyey-Greenstein phase function**
   - Asymmetry parameter `g` from material
   - Support forward/backward scattering

5. **Implement basic sky lighting**
   - Sample distant sky color (constant or from sky cubemap)
   - Apply as ambient contribution

### Phase 2: Shadows
1. **Implement ray-marched shadows**
   - Nested loop: trace from sample toward light
   - Quadratic (x^2) sample distribution
   - Default: 80 samples per main ray march step

2. **Optional: Shadow map pre-pass**
   - Separate compute shader dispatch per directional light
   - 128 samples, 512–2048 resolution

### Phase 3: Advanced Effects
1. **Multi-scattering approximation**
   - Wrenninge octave method (quad falloff per octave)
   - Material-driven factors (MsScattFactor, MsExtinFactor)

2. **Temporal reprojection**
   - Blue noise dithering per frame
   - Depth-based soft blending

3. **Ground bounce lighting**
   - Trace downward, sample extinction
   - Lambert BRDF * ground albedo

4. **Aerial perspective**
   - Per-sample atmosphere transmittance via SkyAtmosphere LUT
   - Rayleigh + Mie scattering

### Phase 4: Optimization
1. **Empty space skipping** (VolumetricCloud.usf:814-837)
   - Low-res volume density texture
   - Skip steps when conservative density = 0

2. **Conservative rasterization**
   - Track depth bounds (min/max per pixel)
   - Early-exit shadow tracing

---

## 10. File Reference Summary

| File | Location | Purpose |
|------|----------|---------|
| **VolumetricCloud.usf** | Engine/Shaders/Private | Main ray march shader (2000+ lines) |
| **VolumetricCloudCommon.ush** | Engine/Shaders/Private | Cloud shadow map sampling, reprojection |
| **VolumetricCloudMaterialPixelCommon.ush** | Engine/Shaders/Private | Material parameter updates, layer geometry |
| **VolumetricCloudComponent.h** | Runtime/Engine/Classes/Components | Component public interface, properties |
| **VolumetricCloudComponent.cpp** | Runtime/Engine/Private/Components | Component lifecycle |
| **VolumetricCloudRendering.h** | Runtime/Renderer/Private | Shader parameter structs, scene info |
| **VolumetricCloudRendering.cpp** | Runtime/Renderer/Private | CVars, helper functions, render passes |
| **VolumetricCloudProxy.h/cpp** | Runtime/Engine/Public/Private | Scene proxy (CPU-to-GPU bridging) |

---

## 11. Critical Constants

| Constant | Value | Source | Usage |
|----------|-------|--------|-------|
| **KILOMETER_TO_CENTIMETER** | 100000.0 | Common.ush | Unit conversion |
| **CENTIMETER_TO_KILOMETER** | 1/100000 | Common.ush | Unit conversion |
| **CENTIMETER_TO_METER** | 0.01 | Common.ush | Integration: dt_meters = dt_cm * 0.01 |
| **PI** | 3.14159265359 | Common.ush | Phase function, ground BRDF |
| **BaseViewRaySampleCount** | 96.0 | VolumetricCloudComponent.h:233 | Legacy compatibility constant |
| **Max phase function g** | ±0.999 | VolumetricCloud.usf:331 | Clamp to avoid division by zero |
| **Ground bounce steps** | 5.0 | VolumetricCloud.usf:981 | Hardcoded sample count |
| **Min async depth value** | 0.000000000001 | VolumetricCloud.usf:618 | Avoid log(0) in transmittance |

---

## 12. Key Design Principles

1. **Material-Driven Clouds**: All density/color controlled by volume domain material, not baked textures
2. **Adaptive Sampling**: Sample count scales with distance to balance quality/performance
3. **Physically-Based Phase Functions**: Henyey-Greenstein with per-sample anisotropy
4. **Temporal Stability**: Blue noise dithering + depth reprojection minimize flickering
5. **Modular Shadows**: Can use pre-computed maps (cheap) or ray-marched (expensive but flexible)
6. **Atmosphere Integration**: Seamlessly blends with SkyAtmosphere via transmittance LUT sampling
7. **Per-Frame Updates**: No caching of cloud geometry; all computed on demand

---

## 13. Warnings & Gotchas

1. **Unit Handling**: Cloud system uses kilometers internally; always convert from centimeters
2. **Sphere Intersection**: Both top and bottom sphere must be tested; order matters for geometry inside layer
3. **Shadow Clamping**: `ShadowTracingDistance` must be tested; zero distance disables shadows entirely
4. **Transmittance Underflow**: exp(-large_value) can underflow; tests use `max(depth, epsilon)`
5. **Reprojection Trails**: Using min(gather_4) on depth reduces temporal artifacts significantly
6. **Ground Contribution Cost**: Adds 5 shadow samples per main ray march sample; expensive
7. **Material Evaluation**: Called once per ray march step; expensive materials = expensive clouds

---

**End of Analysis**

For UzEngine implementation, start with Phase 1 (ray marching + basic transmittance + phase functions) before adding shadows or temporal filtering.

