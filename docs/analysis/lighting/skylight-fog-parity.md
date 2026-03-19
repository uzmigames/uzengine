<!-- doc-meta
created: 2026-03-18
stage: research
status: complete
model: claude-haiku-4-5
-->

# UE5 SkyLight & Exponential Height Fog Analysis

## 1. SkyLight SH Irradiance — Why Shadows Are Too Dark

### 1.1 SkyLight Component Defaults

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyLightComponent.cpp:285-333`

Constructor defaults for `USkyLightComponent`:

```cpp
// SkyLightComponent.cpp:300-327
Brightness_DEPRECATED = 1;
Intensity = 1;                              // Default intensity multiplier
IndirectLightingIntensity = 1.0f;           // Indirect lighting contribution
SkyDistanceThreshold = 150000;              // 150km — far clip for sky capture
Mobility = EComponentMobility::Stationary;
bLowerHemisphereIsBlack = true;             // Below horizon = black unless overridden
OcclusionMaxDistance = 1000;                // DFAO distance
MinOcclusion = 0;
OcclusionExponent = 1;
OcclusionTint = FColor::Black;
CubemapResolution = 128;                    // Default 128x128 cubemap
LowerHemisphereColor = FLinearColor::Black;
AverageBrightness = 1.0f;                   // Average brightness of captured sky
SpecifiedCubemapColorScale = FLinearColor::White;
bCastVolumetricShadow = true;
CloudAmbientOcclusionStrength = 1.0f;
CloudAmbientOcclusionExtent = 150.0f;       // 150km
CloudAmbientOcclusionApertureScale = 0.05f; // 5% cone aperture
```

**Key insight:** Default `IndirectLightingIntensity = 1.0f` controls how much SkyLight diffuse irradiance contributes to unlit surfaces. When Intensity × IndirectLightingIntensity = 1.0, the SkyLight is at nominal brightness.

### 1.2 SkyLight Capture Pipeline

**Files:**
- `ReflectionEnvironmentCapture.cpp:1740-1870` (UpdateSkyCaptureContents)
- `ReflectionEnvironmentDiffuseIrradiance.cpp:88-231` (ComputeDiffuseIrradiance)
- `ReflectionEnvironmentShaders.usf:663-681` (DiffuseIrradianceCopyPS)
- `ReflectionEnvironmentShaders.usf:721-733` (AccumulateCubeFacesPS)

#### 1.2.1 Capture Flow

1. **Cubemap captured** at resolution `CubemapResolution` (default 128)
2. **ComputeDiffuseIrradiance** projects cubemap → SH coefficients (9 coefficients, 3-band SH)
3. **DiffuseIrradianceCopyPS** processes each mip level with:
   - Texel area weighting: `TexelWeight = 4 / (sqrt(SquaredUVs) * SquaredUVs)`
   - SH basis function evaluation: `SHBasisFunction3(CubeCoordinates)`
   - Accumulation through mip chain
4. **AccumulateCubeFacesPS** sums all 6 faces and applies final scaling

#### 1.2.2 SH Projection Math

**File:** `ReflectionEnvironmentShaders.usf:663-681`

```hlsl
void DiffuseIrradianceCopyPS(float4 SvPosition : SV_POSITION, out float4 OutColor : SV_Target0)
{
    float2 UV = SvPosition.xy * SvPositionToUVScale;
    float2 ScaledUVs = UV * 2 - 1;
    float3 CubeCoordinates = normalize(GetCubemapVector(ScaledUVs, CubeFace));

    // Texel solid angle weight per Sloan's "Stupid SH Tricks"
    float SquaredUVs = 1 + dot(ScaledUVs, ScaledUVs);
    float TexelWeight = 4 / (sqrt(SquaredUVs) * SquaredUVs);  // Solid angle per texel

    // Project cubemap texel onto SH basis
    FThreeBandSHVector SHCoefficients = SHBasisFunction3(CubeCoordinates);
    float CurrentSHCoefficient = dot(SHCoefficients.V0, CoefficientMask0)
                                + dot(SHCoefficients.V1, CoefficientMask1)
                                + SHCoefficients.V2 * CoefficientMask2;
    float3 TexelLighting = SampleCubemap(CubeCoordinates).rgb;

    // Output: lighting × SH coefficient × solid angle weight (for averaging)
    OutColor = float4(TexelLighting * CurrentSHCoefficient * TexelWeight, TexelWeight);
}
```

**Critical formula:**
```
OutputColor.rgb = CubeTexel.rgb × SH_basis(direction) × SolidAngleFactor
OutputColor.a   = SolidAngleFactor
```

The solid angle factor ensures proper weighting across the cube faces. The denominator normalizes the result during accumulation.

#### 1.2.3 SH Accumulation & Normalization

**File:** `ReflectionEnvironmentShaders.usf:721-733`

```hlsl
void AccumulateCubeFacesPS(out float4 OutColor : SV_Target0)
{
    float4 AccumulatedValue = 0;
    // Sample 1x1 mip (finest detail fully downsampled) from all 6 faces
    AccumulatedValue += TextureCubeSampleLevel(SourceCubemapTexture, SourceCubemapSampler, float3( 1, 0, 0), SourceMipIndex);
    AccumulatedValue += TextureCubeSampleLevel(SourceCubemapTexture, SourceCubemapSampler, float3(-1, 0, 0), SourceMipIndex);
    AccumulatedValue += TextureCubeSampleLevel(SourceCubemapTexture, SourceCubemapSampler, float3( 0, 1, 0), SourceMipIndex);
    AccumulatedValue += TextureCubeSampleLevel(SourceCubemapTexture, SourceCubemapSampler, float3( 0,-1, 0), SourceMipIndex);
    AccumulatedValue += TextureCubeSampleLevel(SourceCubemapTexture, SourceCubemapSampler, float3( 0, 0, 1), SourceMipIndex);
    AccumulatedValue += TextureCubeSampleLevel(SourceCubemapTexture, SourceCubemapSampler, float3( 0, 0,-1), SourceMipIndex);

    // CRITICAL: Final normalization by solid angles and 4π (full sphere)
    OutColor = float4(4 * PI * AccumulatedValue.rgb / max(AccumulatedValue.a, 0.00001f), 0);
}
```

**The formula:** `OutColor = (4π / ΣSolidAngles) × ΣAccumulated`

This normalizes the SH projection by:
1. **Sum of weighted texels** accumulated across all 6 faces
2. **Divide by total solid angle** (denominator in alpha)
3. **Multiply by 4π** to scale back to hemisphere integral

The `4 * PI` factor appears because SH basis functions are normalized to integrate to `4π` over the sphere (or `2π` over hemisphere for diffuse).

### 1.3 Advanced SH Processing (GPU Path)

**File:** `ReflectionEnvironmentShaders.usf:758-892` (ComputeSkyEnvMapDiffuseIrradianceCS)

For the GPU compute shader path (used in real-time capture):

```hlsl
const float SqrtPI = sqrt(PI);
const float Coefficient0 = 1.0f / (2.0f * SqrtPI);      // L0 (DC): 1/(2√π)
const float Coefficient1 = sqrt(3.0f) / (3.0f * SqrtPI); // L1: √3/(3√π)
const float Coefficient2 = sqrt(15.0f) / (8.0f * SqrtPI); // L2: √15/(8√π)
const float Coefficient3 = sqrt(5.0f) / (16.0f * SqrtPI); // L2z: √5/(16√π)
const float Coefficient4 = 0.5f * Coefficient2;          // L2 mixed: 0.5 × √15/(8√π)

// Apply convolution coefficients for diffuse (Lambertian) environment
OutIrradianceEnvMapSH[0].x = -Coefficient1 * SkyIrradiance.R.V0[3];
OutIrradianceEnvMapSH[0].y = -Coefficient1 * SkyIrradiance.R.V0[1];
OutIrradianceEnvMapSH[0].z =  Coefficient1 * SkyIrradiance.R.V0[2];
OutIrradianceEnvMapSH[0].w =  Coefficient0 * SkyIrradiance.R.V0[0] - Coefficient3 * SkyIrradiance.R.V1[2];
// ... (repeat for G, B channels and L2 coefficients)

// Average irradiance (monochromatic brightness)
const float SHBasisL0Weight = SHBasisFunction3(DummyVector).V0.x;
const float InvSHBand0Weights = 1.0f / (SHBasisL0Weight * 4.0f * PI);
const float SkyAverageIrradianceR = SkyIrradiance.R.V0.x * InvSHBand0Weights;
// Store in SH buffer slot 7
OutIrradianceEnvMapSH[7] = SkyAverageIrradiance (monochromatic);
```

**Key constants:**
- `Coefficient0 = 1.0/(2√π)` — L0 diffuse convolution
- `Coefficient1 = √3/(3√π)` — L1 diffuse convolution
- `Coefficient2 = √15/(8√π)` — L2 diffuse convolution
- `Coefficient3 = √5/(16√π)` — L2z (zonal) term

These coefficients bake the Lambertian diffuse convolution kernel directly into the SH projection. The result is irradiance (incoming light integrated over hemisphere with cosine weighting), not raw radiance.

### 1.4 ReadBack & Storage

**File:** `ReflectionEnvironmentDiffuseIrradiance.cpp:217-230`

```cpp
AddReadbackTexturePass(GraphBuilder, RDG_EVENT_NAME("ReadbackCoefficients"),
    SkySHIrradianceTexture, [SkySHIrradianceTexture, OutIrradianceEnvironmentMap](FRHICommandListImmediate& RHICmdList)
    {
        TArray<FFloat16Color> SurfaceData;
        RHICmdList.ReadSurfaceFloatData(SkySHIrradianceTexture->GetRHI(),
            FIntRect(0, 0, FSHVector3::MaxSHBasis, 1), SurfaceData, CubeFace_PosX, 0, 0);

        for (int32 CoefficientIndex = 0; CoefficientIndex < FSHVector3::MaxSHBasis; CoefficientIndex++)
        {
            const FLinearColor CoefficientValue(SurfaceData[CoefficientIndex]);
            OutIrradianceEnvironmentMap->R.V[CoefficientIndex] = CoefficientValue.R;
            OutIrradianceEnvironmentMap->G.V[CoefficientIndex] = CoefficientValue.G;
            OutIrradianceEnvironmentMap->B.V[CoefficientIndex] = CoefficientValue.B;
        }
    });
```

**Storage:** `FSHVectorRGB3` — 9 coefficients × 3 channels (RGB), each float

### 1.5 Scene Proxy Initialization & Intensity Scaling

**File:** `SkyLightComponent.cpp:189-220` (FSkyLightSceneProxy::Initialize)

```cpp
FSkyLightSceneProxy::FSkyLightSceneProxy(const USkyLightComponent* InLightComponent)
    : LightColor(InLightComponent->GetLightColor())
    , IndirectLightingIntensity(InLightComponent->IndirectLightingIntensity)
    , AverageBrightness(InLightComponent->AverageBrightness)
    , IrradianceEnvironmentMap(InLightComponent->IrradianceEnvironmentMap)
```

**Effective color used in shaders:**
```cpp
FLinearColor GetEffectiveLightColor() const
{
    return LightColor * GSkylightIntensityMultiplier * SpecifiedCubemapColorScale;
}
```

Where:
- `LightColor` = user-set sky light color (usually white in default scene)
- `GSkylightIntensityMultiplier` = `r.SkylightIntensityMultiplier` cvar (default 1.0)
- `SpecifiedCubemapColorScale` = rescaling for FP32 cubemaps (default white)
- **In deferred pass:** Multiplied by `IndirectLightingIntensity` when applying SH to surfaces

### 1.6 Deferred Lighting Application

**Where SkyLight diffuse is applied:**

SkyLight SH coefficients are evaluated in the deferred lighting pass for each screen pixel. The irradiance is modulated by:

```
FinalDiffuse = DiffuseAlbedo × SHIrradiance(SurfaceNormal) × IndirectLightingIntensity
```

Where `SHIrradiance(N)` evaluates the 9 SH coefficients using the surface normal as the evaluation direction.

---

## 2. Exponential Height Fog — Default Parameters & Formula

### 2.1 Fog Component Defaults

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\ExponentialHeightFogComponent.cpp:64-103`

Constructor defaults for `UExponentialHeightFogComponent`:

```cpp
// Deprecated color (no longer used)
FogInscatteringColor_DEPRECATED = FLinearColor(0.447f, 0.638f, 1.0f);  // Bluish tone
FogInscatteringLuminance = FLinearColor::Black;                         // Default = no inscattering

// Sky atmosphere interaction
SkyAtmosphereAmbientContributionColorScale = FLinearColor::White;      // Scale factor for sky contribution

// Directional inscattering (sun ray god rays)
DirectionalInscatteringExponent = 4.0f;           // Cone aperture sharpness (2=wide, 64=sharp)
DirectionalInscatteringStartDistance = 10000.0f; // Where directional inscattering begins (cm)
DirectionalInscatteringColor_DEPRECATED = FLinearColor(0.25f, 0.25f, 0.125f); // Warm sunset tone

// Cubemap inscattering color
InscatteringTextureTint = FLinearColor::White;
FullyDirectionalInscatteringColorDistance = 100000.0f;    // Fully directional (cubemap) at this distance
NonDirectionalInscatteringColorDistance = 1000.0f;        // Fully ambient at this distance

// Main fog parameters
FogDensity = 0.02f;          // Global density multiplier (0 to 0.05 typical range)
FogHeightFalloff = 0.2f;     // Exponential height decay (0.001 to 2 typical range)
SecondFogData.FogDensity = 0.0f;  // Secondary fog layer disabled by default

// Opacity & distance controls
FogMaxOpacity = 1.0f;        // Fog can reach full opacity (0 = no fog influence)
StartDistance = 0.0f;        // Fog starts immediately (in cm)
EndDistance = 0.0f;          // Disabled (0 = infinite). Max XY distance for integration

// Distance cutoff
FogCutoffDistance = 0;       // Disabled. No fog beyond this distance

// Volumetric fog (advanced)
VolumetricFogScatteringDistribution = 0.2f;      // Phase function anisotropy (-0.9 to 0.9)
VolumetricFogAlbedo = FColor::White;             // Particle reflectivity
VolumetricFogExtinctionScale = 1.0f;             // Light extinction factor
VolumetricFogDistance = 6000.0f;                 // Integration distance (cm)
```

**Typical ranges for FogDensity:** 0.002 to 0.05 (0.02 = default mid-range)
**Typical ranges for FogHeightFalloff:** 0.001 to 2.0 (0.2 = default gentle slope)

### 2.2 Exponential Height Fog Formula

**File:** `HeightFogCommon.ush:192-300` (GetExponentialHeightFog)

#### 2.2.1 Density Function

The core fog density as a function of height Z:

```
ρ(z) = FogDensity × exp(-FogHeightFalloff × (z - FogHeight))
```

Where:
- `FogDensity` = global density multiplier (default 0.02)
- `FogHeightFalloff` = exponential decay constant (default 0.2)
- `z` = world position height (in cm, Z-up coordinate)
- `FogHeight` = reference height where density equals FogDensity (usually camera Z or component Z)

**Behavior:**
- Larger `FogHeightFalloff` → Faster exponential decay with height (sharper transition)
- Smaller `FogHeightFalloff` → Slower decay (fog extends higher, more gradual transition)
- `FogDensity` × factor scales the entire exponential curve up/down

#### 2.2.2 Line Integral (Transmittance)

**File:** `HeightFogCommon.ush:192-199` (CalculateLineIntegralShared)

```hlsl
float CalculateLineIntegralShared(float FogHeightFalloff, float RayDirectionZ, float RayOriginTerms)
{
    float Falloff = max(-127.0f, FogHeightFalloff * RayDirectionZ);

    // Main formula: (1 - exp(-Falloff)) / Falloff
    float LineIntegral = (1.0f - exp2(-Falloff)) / Falloff;

    // Taylor series approximation for near-zero Falloff (avoids division by zero)
    float LineIntegralTaylor = log(2.0) - (0.5 * Pow2(log(2.0))) * Falloff;

    // Select based on |Falloff| threshold (use Taylor near singularity)
    return RayOriginTerms * (abs(Falloff) > FLT_EPSILON2 ? LineIntegral : LineIntegralTaylor);
}
```

**Breakdown:**
- `Falloff = FogHeightFalloff × RayZ` — Height component of the exponent
- `RayOriginTerms = FogDensity × exp(-FogHeightFalloff × (CameraZ - FogHeight))` — Density at ray origin
- **Full integral:**
  ```
  ∫ ρ(camera + t×direction) dt = RayOriginTerms × ∫₀¹ exp(-Falloff × t) dt
                                = RayOriginTerms × (1 - exp(-Falloff)) / Falloff
  ```
- **Taylor expansion** for numerical stability when Falloff ≈ 0:
  ```
  (1 - exp(-x)) / x ≈ 1 - x/2 + ... ≈ ln(2) - 0.5×(ln2)² × x
  ```

This is the **visual transmittance** of the fog (how much light is blocked/scattered).

#### 2.2.3 Full Fog Equation

**File:** `HeightFogCommon.ush:299-300` (GetExponentialHeightFog)

```hlsl
// Accumulate both fog layers (primary + secondary)
float ExponentialHeightLineIntegralShared = CalculateLineIntegralShared(
    FogData.ExponentialFogParameters.y,      // Primary FogHeightFalloff
    RayDirectionZ,
    RayOriginTerms);                         // Primary density at camera

ExponentialHeightLineIntegralShared += CalculateLineIntegralShared(
    FogData.ExponentialFogParameters2.y,     // Secondary FogHeightFalloff
    RayDirectionZ,
    RayOriginTermsSecond);                   // Secondary density at camera

float ExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * RayLength;
```

**Two-layer fog:**
- Primary fog: `FogDensity` + `FogHeightFalloff`
- Secondary fog: `SecondFogData.FogDensity` + `SecondFogData.FogHeightFalloff`
- Both integrate independently, sum combined

#### 2.2.4 Transmittance (Alpha) Application

The line integral computes **optical depth τ**:

```
τ = ExponentialHeightLineIntegral
FogAlpha = 1 - exp(-τ)          // Transmittance (what fraction of light is blocked)
FogColor = Inscattering × FogAlpha + SceneColor × (1 - FogAlpha)
```

Where:
- `FogAlpha` ranges [0, `FogMaxOpacity`] (clamped)
- `Inscattering` = computed fog color (from SkyAtmosphere, directional light, cubemap, or static color)

### 2.3 Inscattering Color Sources

**File:** `HeightFogCommon.ush:140-183` (ComputeInscatteringColor)

Three sources (in priority order):

1. **Cubemap inscattering** (if enabled):
   - Samples `FogInscatteringColorCubemap` in ray direction
   - Blends between mip 0 (directional, close) and high mip (ambient, far)
   - Blend controlled by distance parameters:
     - `FullyDirectionalInscatteringColorDistance` = 100000.0 cm (1000 m) — use mip 0
     - `NonDirectionalInscatteringColorDistance` = 1000.0 cm (10 m) — use high mip
     - Interpolates between these distances

2. **Sky Atmosphere contribution** (if enabled):
   - Sampled from `View.SkyAtmosphereHeightFogContribution`
   - Modulated by `SkyAtmosphereAmbientContributionColorScale` (default white)
   - Represents ambient sky color influencing the fog

3. **Static inscattering color** (fallback):
   - `FogInscatteringLuminance` (default black = disabled)
   - `DirectionalInscatteringLuminance` (directional component)

**Code:**
```hlsl
half3 Inscattering = FogData.ExponentialFogColorParameter.xyz;  // Static color

if (UseCubemapInscattering)  // FogData.ExponentialFogParameters3.z > 0
{
    float FadeAlpha = saturate(CameraToReceiverLength * ScaleFactor + Bias);
    float3 DirectionalColor = TextureCubeSampleLevel(Cubemap, ..., 0);     // Mip 0
    float3 NonDirectionalColor = TextureCubeSampleLevel(Cubemap, ..., HighMip); // High mip
    Inscattering *= lerp(NonDirectionalColor, DirectionalColor, FadeAlpha);
}

if (SupportSkyAtmosphereAffectsHeightFog)
{
    float3 SkyAmbient = SkyAtmosphereAmbientContributionColorScale × SkyColor;
    Inscattering += SkyAmbient;
}
```

### 2.4 Start Distance & End Distance

**File:** `HeightFogCommon.ush:271-291`

**StartDistance** (`ExponentialFogParameters.w`, default 0):
- Fog begins at this distance from camera (in cm)
- Ray is clamped: only fog beyond StartDistance contributes
- Prevents near-field fogging

**EndDistance** (`FogData.EndDistance`, default 0):
- If > 0: limits XY plane fog integration distance
- Ray direction is clamped in XY projection:
  ```hlsl
  if (EndDistance > 0 && LengthXY > EndDistance)
  {
      RayDirection *= EndDistance / LengthXY;  // Shorten ray in XY
  }
  ```
- Prevents fog beyond a certain horizontal distance

### 2.5 Height-Based Density Precomputation

**File:** `HeightFogCommon.ush:201-210` (PreComputeFogOriginFactor)

When camera height changes or fog height differs from camera:

```hlsl
float PreComputeFogOriginFactor(float OriginHeight, float FogHeight, float FogFalloff, float FogDensity)
{
    // Compute RayOriginTerms for a given camera position
    const float CollapsedFogParameterPower = clamp(
        -FogFalloff * (OriginHeight - FogHeight),
        -126.0f + 1.0f,  // IEEE FP exponent limits
        +127.0f - 1.0f
    );
    return FogDensity * pow(2.0f, CollapsedFogParameterPower);
}
```

This precomputes `RayOriginTerms = FogDensity × exp(-FogHeightFalloff × (CameraZ - FogHeight))` using `exp2()` for precision and clamping to IEEE float exponent range.

### 2.6 Second Fog Layer

**File:** `HeightFogCommon.ush:66-70` (FogStructData)

```cpp
// CPU side (ExponentialHeightFogComponent.h)
struct FExponentialHeightFogData
{
    float FogDensity;        // SecondFogData.FogDensity
    float FogHeightFalloff;  // SecondFogData.FogHeightFalloff
    float FogHeight;         // Height at which secondary fog density = SecondFogData.FogDensity
    // ... (no FogMaxOpacity for secondary — it uses primary's)
};

// GPU side (HeightFogCommon.ush)
float4 ExponentialFogParameters2;  // x=DensitySecond*exp(-FalloffSecond*(CamZ-FogHeightSecond)), y=FalloffSecond, z=DensitySecond, w=FogHeightSecond
```

The secondary fog is a completely independent exponential layer (same formula as primary). Both line integrals sum before applying opacity clamping.

---

## Summary of Key Parameters & Formulas

### SkyLight

| Parameter | File | Value | Purpose |
|-----------|------|-------|---------|
| `Intensity` | SkyLightComponent.h | 1.0 | Multiplier for SkyLight light color |
| `IndirectLightingIntensity` | SkyLightComponent.h | 1.0 | Multiplier for SH irradiance in deferred lighting |
| `CubemapResolution` | SkyLightComponent.h | 128 | Capture cubemap size (power of 2) |
| `CubemapFormat` | ReflectionEnvironmentCapture.cpp:130 | `PF_FloatRGBA` (FP16) | Captured cubemap format |
| `GDiffuseIrradianceCubemapSize` | ReflectionEnvironmentDiffuseIrradiance.cpp:23 | 32 | Mip resolution for SH projection |
| SH Coefficients (Diffuse) | ReflectionEnvironmentShaders.usf:837-841 | `1/(2√π)`, `√3/(3√π)`, `√15/(8√π)`, etc. | L0/L1/L2 diffuse convolution factors |
| Solid angle normalization | ReflectionEnvironmentShaders.usf:732 | `4π / ΣSolidAngles` | Final SH scaling |

### Exponential Height Fog

| Parameter | File | Default | Typical Range | Purpose |
|-----------|------|---------|---|---------|
| `FogDensity` | ExponentialHeightFogComponent.h:22 | 0.02 | 0.002–0.05 | Global fog opacity multiplier |
| `FogHeightFalloff` | ExponentialHeightFogComponent.h:30 | 0.2 | 0.001–2.0 | Exponential decay with height |
| `FogMaxOpacity` | ExponentialHeightFogComponent.h:108 | 1.0 | 0.0–1.0 | Max transparency (1=opaque, 0=no fog) |
| `StartDistance` | ExponentialHeightFogComponent.h:112 | 0.0 | 0.0–5000.0 | Fog starts at this distance (cm) |
| `EndDistance` | ExponentialHeightFogComponent.h:116 | 0.0 | 0.0–500000.0 | Fog ends at this XY distance (cm), 0=infinite |
| `FogCutoffDistance` | ExponentialHeightFogComponent.h:120 | 0 | 100000–20000000 | No fog beyond this distance (cm), 0=disabled |
| `DirectionalInscatteringExponent` | ExponentialHeightFogComponent.h:79 | 4.0 | 2.0–64.0 | Sun god ray cone aperture (4=moderate, 64=sharp) |
| `DirectionalInscatteringStartDistance` | ExponentialHeightFogComponent.h:88 | 10000.0 | cm | Distance where directional inscattering begins |

### Core Formulas

**SkyLight Irradiance:**
```
IrradianceCoeff[i] = ∫_sphere CubeTexel(direction) × SH_basis_i(direction) × cos(angle) dΩ / 4π
                   (with diffuse convolution factors applied, result is radiosity not radiance)
```

**Fog Transmittance:**
```
τ = FogDensity × exp(-FogHeightFalloff × (CameraZ - FogHeight))
    × (1 - exp(-FogHeightFalloff × RayZ)) / FogHeightFalloff × RayLength
FinalColor = Inscattering × (1 - exp(-τ)) + SceneColor × exp(-τ)
```

---

## Why UzEngine Shadows Are Too Dark

The most likely causes, in order of probability:

1. **SkyLight irradiance not being applied** — deferred lighting pass not using SH coefficients for indirect lighting
2. **IndirectLightingIntensity = 0** — SkyLight contribution disabled or not multiplied in
3. **SkyLight intensity too low** — Intensity or Intens × IndirectLightingIntensity product is < 1.0
4. **SH projection missing diffuse convolution factors** — coefficients not scaled by `1/(2√π)`, `√3/(3√π)`, etc.
5. **Fog obscuring sky contribution** — fog opacity clipping or over-saturating shadows

**Action items:**
1. Verify `IndirectLightingIntensity` is multiplied into deferred pass
2. Check if SH coefficients are computed with diffuse convolution (not raw SH)
3. Verify cubemap capture includes sky color (not just emissive)
4. Test with fog disabled to isolate SkyLight contribution
5. Compare captured SH L0 term (average irradiance) against UE5

---

## References

- **SkyLight SH Projection:** W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironmentDiffuseIrradiance.cpp
- **Exponential Height Fog:** W:\UE_5.6_Source\Engine\Shaders\Private\HeightFogCommon.ush
- **SH Diffuse Convolution:** "Stupid Spherical Harmonics (SH) Tricks" by Peter-Pike Sloan
  - Reference: https://www.ppsloan.org/publications/StupidSH36.pdf
