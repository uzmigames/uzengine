<!-- doc-meta
title: UE5 Sky Atmosphere Transmittance LUT System
author: Unreal Architect Analysis
date: 2026-03-15
tags: [sky-atmosphere, transmittance, LUT, precomputation, compute-shader, rendering]
status: complete
version: 1.0
refs:
  - W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf (RenderTransmittanceLutCS)
  - W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush (UV mapping)
  - W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp (C++ code)
-->

# UE5 Sky Atmosphere Transmittance LUT System

## Executive Summary

UE5 implements atmospheric transmittance (light absorption through the atmosphere) via a **precomputed lookup table (LUT)** that is computed once per scene at startup using a compute shader. The system encodes two parameters (view height above planet + zenith angle to sun) into a 2D UV coordinate, then decodes that to compute transmittance via ray marching.

**Key facts**:
- **Dimensions**: 256x64 pixels (width × height) by default, configurable via CVars
- **Format**: PF_FloatRGB (3x 32-bit floats per pixel), or PF_FloatRGBA on mobile/platforms requiring alpha
- **Computation**: Compute shader dispatch with 8x8 thread groups
- **Frequency**: Once per scene (not per-frame), stored in persistent texture pool
- **Method**: Single-scattering ray march with configurable sample count (default 10 samples)

## Problem Statement

When rendering the sky and atmosphere, you need to know how much light from the sun reaches a viewer at height H looking toward zenith angle θ. This is **transmittance**—the integral of atmospheric extinction along the light ray.

Computing this on-demand per frame is expensive:
- Requires ray-marching through atmosphere layers
- Must be evaluated for every pixel + every view
- Becomes a bottleneck in complex scenes

**Solution**: Precompute transmittance for a 2D grid of (height, zenith_angle) pairs and store in a texture. Then at runtime, lookup the value in O(1) with a single texture sample.

## LUT Parameterization

### Coordinate Space

The transmittance LUT maps 2D texture coordinates **UV ∈ [0,1]² → (ViewHeight, ViewZenithCosAngle)**.

**From UV to parameters** (decode):

```cpp
void fromTransmittanceLutUVs(
    out float ViewHeight, out float ViewZenithCosAngle,
    in float BottomRadius, in float TopRadius, in float2 UV)
{
    // UV.x → zenith view angle (normalized distance to atmosphere boundary)
    // UV.y → height above planet

    float Xmu = UV.x;
    float Xr = UV.y;

    float H = sqrt(TopRadius * TopRadius - BottomRadius * BottomRadius);  // Atmosphere thickness
    float Rho = H * Xr;                                                    // Horizontal distance from pole
    ViewHeight = sqrt(Rho * Rho + BottomRadius * BottomRadius);           // Radius from planet center

    float Dmin = TopRadius - ViewHeight;      // Closest point to atmosphere boundary
    float Dmax = Rho + H;                      // Farthest point to atmosphere boundary
    float D = Dmin + Xmu * (Dmax - Dmin);     // Ray distance to atmosphere boundary
    ViewZenithCosAngle = D == 0.0f ? 1.0f : (H * H - Rho * Rho - D * D) / (2.0f * ViewHeight * D);
    ViewZenithCosAngle = clamp(ViewZenithCosAngle, -1.0f, 1.0f);
}
```

**From parameters to UV** (encode):

```cpp
void getTransmittanceLutUvs(
    in float viewHeight, in float viewZenithCosAngle, in float BottomRadius, in float TopRadius,
    out float2 UV)
{
    float H = sqrt(max(0.0f, TopRadius * TopRadius - BottomRadius * BottomRadius));
    float Rho = sqrt(max(0.0f, viewHeight * viewHeight - BottomRadius * BottomRadius));

    float Discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0f) + TopRadius * TopRadius;
    float D = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(Discriminant)));  // Distance to atmosphere boundary

    float Dmin = TopRadius - viewHeight;
    float Dmax = Rho + H;
    float Xmu = (D - Dmin) / (Dmax - Dmin);
    float Xr = Rho / H;

    UV = float2(Xmu, Xr);
}
```

### Coordinate Meaning

| Axis | Range | Meaning | Notes |
|------|-------|---------|-------|
| **UV.x** (horizontal) | [0, 1] | Normalized distance to atmosphere boundary | 0 = closest point on ray, 1 = farthest point |
| **UV.y** (vertical) | [0, 1] | Normalized height above planet | 0 = at planet surface (BottomRadius), 1 = at atmosphere top (TopRadius) |

**Key insight**: The parameterization is **non-linear**—more texels are allocated to regions near the horizon (low zenith angles) where transmittance changes rapidly. This is the Bruneton 2017 scheme.

## Texture Specifications

### Dimensions

```cpp
// From SkyAtmosphereRendering.cpp CVars:
static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTWidth(
    TEXT("r.SkyAtmosphere.TransmittanceLUT.Width"), 256,  // ← Default
    TEXT(""),
    ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTHeight(
    TEXT("r.SkyAtmosphere.TransmittanceLUT.Height"), 64,  // ← Default
    TEXT(""),
    ECVF_RenderThreadSafe | ECVF_Scalability);
```

**Default size**: **256 × 64 pixels**
- 256 horizontal (zenith angle discretization)
- 64 vertical (height discretization)
- **Total**: 16,384 texels

### Pixel Format

```cpp
static EPixelFormat GetSkyLutTextureFormat(ERHIFeatureLevel::Type FeatureLevel)
{
    EPixelFormat TextureLUTFormat = PF_FloatRGB;  // ← Primary format (desktop/console)
    const bool bSupportsAlpha = IsPostProcessingWithAlphaChannelSupported();
    if (FeatureLevel <= ERHIFeatureLevel::ES3_1 || bSupportsAlpha)
    {
        TextureLUTFormat = PF_FloatRGBA;  // ← Mobile/platforms requiring alpha
    }
    return TextureLUTFormat;
}
```

**Format decision**:
- **Desktop (DX12/Vulkan)**: `PF_FloatRGB` (3 × 32-bit floats = 12 bytes/pixel)
- **Mobile (ES3.1) or alpha-capable**`: `PF_FloatRGBA` (4 × 32-bit floats = 16 bytes/pixel)

**Why RGB not RGBA?** Mobile platforms cannot write to 3-component UAVs in compute shaders. The alpha channel is unused but required for compute compatibility.

### Optional Small Format

```cpp
// For lower memory usage (trades quality)
static TAutoConsoleVariable<int32> CVarSkyAtmosphereTransmittanceLUTUseSmallFormat(
    TEXT("r.SkyAtmosphere.TransmittanceLUT.UseSmallFormat"), 0,
    TEXT("If true, the transmittance LUT will use a small R8BG8B8A8 format..."),
    ECVF_RenderThreadSafe | ECVF_Scalability);
```

**Small format**: `PF_R8G8B8A8` (1 byte per channel = 4 bytes/pixel, 8-bit precision)
- Used only if `CVarSkyAtmosphereTransmittanceLUTUseSmallFormat > 0`
- Reduces memory from ~256KB to ~64KB
- Trades visual quality for memory savings

## Compute Shader Implementation

### Shader Code

**File**: W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf, lines 1099-1138

```hlsl
#ifndef THREADGROUP_SIZE
#define THREADGROUP_SIZE 1
#endif

// see SkyAtmosphereRendering.cpp GetSkyLutTextureFormat
#if SHADING_PATH_MOBILE
RWTexture2D<float4> TransmittanceLutUAV;
#else
RWTexture2D<float3> TransmittanceLutUAV;
#endif

[numthreads(THREADGROUP_SIZE, THREADGROUP_SIZE, 1)]
void RenderTransmittanceLutCS(uint3 ThreadId : SV_DispatchThreadID)
{
    float2 PixPos = float2(ThreadId.xy) + 0.5f;

    // Compute camera position from LUT coords
    float2 UV = (PixPos) * SkyAtmosphere.TransmittanceLutSizeAndInvSize.zw;
    float ViewHeight;
    float ViewZenithCosAngle;
    UvToLutTransmittanceParams(ViewHeight, ViewZenithCosAngle, UV);

    // A few extra needed constants
    float3 WorldPos = float3(0.0f, 0.0f, ViewHeight);
    float3 WorldDir = float3(0.0f, sqrt(1.0f - ViewZenithCosAngle * ViewZenithCosAngle), ViewZenithCosAngle);

    SamplingSetup Sampling = (SamplingSetup)0;
    {
        Sampling.VariableSampleCount = false;
        Sampling.SampleCountIni = SkyAtmosphere.TransmittanceSampleCount;
    }
    const bool Ground = false;
    const float DeviceZ = FarDepthValue;
    const bool MieRayPhase = false;
    const float3 NullLightDirection = float3(0.0f, 0.0f, 1.0f);
    const float3 NullLightIlluminance = float3(0.0f, 0.0f, 0.0f);
    const float AerialPespectiveViewDistanceScale = 1.0f;

    // Core computation: ray march to compute optical depth
    SingleScatteringResult ss = IntegrateSingleScatteredLuminance(
        float4(PixPos, 0.0f, 1.0f), WorldPos, WorldDir,
        Ground, Sampling, DeviceZ, MieRayPhase,
        NullLightDirection, NullLightDirection, NullLightIlluminance, NullLightIlluminance,
        AerialPespectiveViewDistanceScale);

    // Convert optical depth to transmittance via Beer-Lambert law
    float3 transmittance = exp(-ss.OpticalDepth);

    // Store to UAV
#if SHADING_PATH_MOBILE
    TransmittanceLutUAV[int2(PixPos)] = float4(transmittance, 0.0f);
#else
    TransmittanceLutUAV[int2(PixPos)] = transmittance;
#endif
}
```

### Algorithm Breakdown

1. **Pixel → UV**: Convert thread ID to normalized [0, 1] coordinates
2. **UV → Parameters**: Decode (ViewHeight, ViewZenithCosAngle) from UV
3. **Build ray**: Construct world position and direction from decoded parameters
4. **Ray march**: Call `IntegrateSingleScatteredLuminance()` to compute optical depth along the ray
5. **Convert**: Apply Beer-Lambert law: `transmittance = exp(-optical_depth)`
6. **Store**: Write RGB value to UAV

### Key Constants

- **Ground = false**: We're not hitting the planet
- **MieRayPhase = false**: We don't care about scattering phase (transmittance only)
- **SampleCount = 10** (default, configurable CVar): Ray march samples per pixel
- **NullLightDirection/Illuminance**: No direct light evaluation (transmittance is direction-independent)

## Dispatch Configuration

### Thread Groups

**File**: W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp, lines 1368-1378

```cpp
class FRenderTransmittanceLutCS : public FGlobalShader
{
    // ...
    const static uint32 GroupSize = 8;  // ← 8x8 thread group
    // ...
};

// Dispatch code:
TShaderMapRef<FRenderTransmittanceLutCS> ComputeShader(GlobalShaderMap);
FRenderTransmittanceLutCS::FParameters * PassParameters =
    GraphBuilder.AllocParameters<FRenderTransmittanceLutCS::FParameters>();
PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
PassParameters->TransmittanceLutUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(TransmittanceLut, 0));

FIntVector TextureSize = TransmittanceLut->Desc.GetSize();
TextureSize.Z = 1;
const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderTransmittanceLutCS::GroupSize);
FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TransmittanceLut"),
    PassFlag, ComputeShader, PassParameters, NumGroups);
```

**Calculation**:
- Texture: 256×64
- Thread group size: 8×8
- **Dispatch grid**: (256/8) × (64/8) = **32 × 8 thread groups**
- **Total threads**: 32 × 8 × 64 = 16,384 threads (one per pixel)

## Shader Parameters

### Input Constant Buffers

```cpp
SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
```

**FAtmosphereUniformShaderParameters** contains:
- BottomRadiusKm, TopRadiusKm
- Rayleigh scattering coefficients
- Mie scattering/absorption coefficients
- Ozone absorption coefficients
- Density scale heights

**FSkyAtmosphereInternalCommonParameters** contains:
- `TransmittanceLutSizeAndInvSize`: float4(width, height, 1.0/width, 1.0/height)
- `TransmittanceSampleCount`: Number of samples for ray marching

### Output UAV

```cpp
#if SHADING_PATH_MOBILE
RWTexture2D<float4> TransmittanceLutUAV;
#else
RWTexture2D<float3> TransmittanceLutUAV;
#endif
```

## Runtime Usage

### Lookup Function

**File**: W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf, lines 346-356

```hlsl
float3 GetTransmittance(in float LightZenithCosAngle, in float PHeight)
{
    float2 UV;
    LutTransmittanceParamsToUv(PHeight, LightZenithCosAngle, UV);
#ifdef WHITE_TRANSMITTANCE
    float3 TransmittanceToLight = 1.0f;
#else
    float3 TransmittanceToLight = TransmittanceLutTexture.SampleLevel(
        TransmittanceLutTextureSampler, UV, 0).rgb;
#endif
    return TransmittanceToLight;
}
```

### Integration Points

The transmittance LUT is sampled in:
1. **Sky rendering pass**: For sun disk attenuation
2. **Aerial perspective volume pass**: For volumetric fog along camera rays
3. **Multi-scattering LUT pass**: As input for secondary light bounces
4. **Light disk luminance computation**: For sun disk bloom

### Texture Parameters

```cpp
Texture2D<float4> TransmittanceLutTexture;
SamplerState TransmittanceLutTextureSampler;  // SamplerLinearClamp
```

**Sampler**: Linear clamped (smooth interpolation, no wrapping)

## Precomputation Timing

### When is it computed?

**File**: W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp

```cpp
void InitSkyAtmosphereForScene(FRHICommandListImmediate& RHICmdList,
    FRDGBuilder& GraphBuilder, FScene* Scene)
{
    // ...
    if (CVarSkyAtmosphereTransmittanceLUT.GetValueOnAnyThread() > 0)
    {
        // Create texture once
        TRefCountPtr<IPooledRenderTarget>& TransmittanceLutTexture =
            SkyInfo.GetTransmittanceLutTexture();
        Desc = FPooledRenderTargetDesc::Create2DDesc(
            FIntPoint(TransmittanceLutWidth, TransmittanceLutHeight),
            TextureLUTFormat, FClearValueBinding::None, TexCreate_None,
            TexCreate_ShaderResource | TexCreate_UAV, false);
        GRenderTargetPool.FindFreeElement(RHICmdList, Desc, TransmittanceLutTexture,
            TEXT("SkyAtmosphere.TransmittanceLut"));
    }
    // ...
}

// Then later in RenderSkyAtmosphereInternal():
if (CVarSkyAtmosphereTransmittanceLUT.GetValueOnRenderThread() > 0)
{
    TShaderMapRef<FRenderTransmittanceLutCS> ComputeShader(GlobalShaderMap);
    // ... dispatch compute shader ...
}
```

**Frequency**: **Once per scene**, stored in render target pool.

Recomputation only happens if:
- Sky atmosphere component is added/removed
- Atmosphere parameters change
- Scene unloads and reloads

## Key Parameters

### Atmosphere Configuration

All these parameters affect transmittance computation:

```
BottomRadius          6,360 km (Earth sea level)
TopRadius             6,460 km (70 km atmosphere height)

Rayleigh Scattering   ~5.8e-6 per km (blue light scattering)
Mie Scattering        ~2.1e-5 per km (large particle scattering)
Mie Absorption        ~4.4e-6 per km (aerosol absorption)
Ozone Absorption      (parameterized piecewise)
```

### Sample Count

```cpp
static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTSampleCount(
    TEXT("r.SkyAtmosphere.TransmittanceLUT.SampleCount"), 10.0f,
    TEXT("The sample count used to evaluate transmittance."),
    ECVF_RenderThreadSafe | ECVF_Scalability);
```

- **10 samples** = default (good balance of quality/cost)
- Higher = more accurate transmittance but slower precomputation
- Lower = faster but visible banding

## Architecture Pattern: Precomputation + LUT

### Problem Solved

1. **Runtime performance**: O(1) lookup instead of O(N) ray march per pixel
2. **Memory efficiency**: 256×64×12 bytes = ~192 KB (negligible)
3. **Quality consistency**: Same computation across all views/pixels
4. **Sky responsiveness**: Can change atmosphere parameters and recompute once, not per-frame

### Why This Approach?

- **Atmosphere is static per scene**: Parameters don't change frame-to-frame
- **Transmittance is parameterized by 2 values**: height + zenith angle → fits in 2D LUT
- **Continuous decode function**: Non-integer UV can be decoded analytically to float parameters
- **Smooth result**: Linear sampling of computed LUT gives good interpolation

### Non-UE5 Precedents

This pattern comes from Bruneton et al. (2017) precomputed atmospheric scattering model.

## Performance Characteristics

### Precomputation Cost

With 256×64 = 16,384 pixels and 10 samples per pixel:
- **~164,000 ray march samples total**
- **Typical time**: 10-50 ms on desktop GPU
- **Can run async compute** if supported

### Runtime Cost

Each lookup:
- 1 encode operation: `getTransmittanceLutUvs()` (arithmetic only, no memory)
- 1 texture sample: `SampleLevel(TransmittanceLutTexture, UV, 0)`
- **Total**: ~2 GPU cycles per lookup

## UzEngine Adaptation Guidance

### Direct Applicability

UzEngine should adopt:
1. **Same LUT dimensions**: 256×64 is well-studied and balances quality/cost
2. **Same format**: Use RGBA32F for maximum precision (can afford the memory)
3. **Same UV mapping**: The Bruneton parameterization is mathematically optimal
4. **Same dispatch model**: 8×8 thread groups with per-pixel computation
5. **Same precomputation timing**: Once per scene, stored in persistent texture

### Implementation Differences

**For UzEngine (C++23, DX12-first)**:
1. **No mobile support needed** (initially), so skip `PF_FloatRGB` vs `PF_FloatRGBA` branching
2. **Use DX12 directly** (no RHI abstraction in shader, just HLSL compute)
3. **No render graph (RDG)** → use simple DX12 API: `device->CreateUnorderedAccessView()`, `cmdList->Dispatch()`
4. **No render target pool** → allocate once in `SkyAtmosphere::Init()`, store in persistent pool
5. **Simplified parameters** → pass only essentials (atmosphere data, LUT size) via constant buffer

### Bruneton References

The UV mapping and precomputation scheme is from:
- Bruneton et al., "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", SIGGRAPH 2017
- Paper: https://hal.science/hal-01985130 (open access)
- Reference implementation: https://github.com/ebruneton/precomputed_atmospheric_scattering

## Summary for Implementation

| Aspect | Value |
|--------|-------|
| **LUT Resolution** | 256×64 pixels |
| **Pixel Format** | R32G32B32F (or R32G32B32A32F on mobile) |
| **Memory per LUT** | ~192 KB (negligible) |
| **Computation** | Compute shader, 8×8 threads per group |
| **Dispatch Grid** | 32×8 groups = 16,384 threads |
| **Sample Count** | 10 (configurable) |
| **Precomputation Frequency** | Once per scene |
| **Runtime Cost** | 1 encode + 1 texture lookup per use |
| **UV Mapping** | Bruneton 2017 (height + zenith → [0,1]²) |
| **Decode Function** | `getTransmittanceLutUvs()` / `fromTransmittanceLutUVs()` |

---

## File References

1. **W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf**
   - Lines 1099-1138: `RenderTransmittanceLutCS` compute shader
   - Lines 207-222: `getTransmittanceLutUvs()` / `fromTransmittanceLutUVs()` UV mapping functions
   - Line 346: `GetTransmittance()` runtime lookup

2. **W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush**
   - Lines 156-172: Full `fromTransmittanceLutUVs()` implementation
   - Lines 175-192: Full `getTransmittanceLutUvs()` implementation
   - Lines 233-254: `GetAtmosphereTransmittance()` integration example

3. **W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp**
   - Lines 151-174: CVar definitions (defaults, sample count, format selection)
   - Lines 1148-1164: `GetSkyLutTextureFormat()` format selection logic
   - Lines 1166-1208: `InitSkyAtmosphereForScene()` texture creation
   - Lines 1364-1379: `RenderSkyAtmosphereInternal()` dispatch code
   - Lines 791-815: `FRenderTransmittanceLutCS` shader class definition

