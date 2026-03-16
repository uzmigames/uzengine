<!-- doc-meta
{
  "author": "Claude Code",
  "date": "2026-03-16",
  "status": "final",
  "priority": "P0",
  "version": "1.0"
}
-->

# UE5 SM5 (Non-Bindless) Fallback Patterns for Lighting & IBL

## Executive Summary

UE5 handles SM5 (feature level 11_0 / DX11) hardware that lacks compute shaders and bindless resources through **strategic fallbacks and offline pre-computation**:

1. **Auto-exposure**: Dual-path system — histogram compute (SM5+) OR pixel shader + readback (ES3.1/mobile)
2. **BRDF/Irradiance/Prefiltered specular LUTs**: **Pre-baked offline**, no runtime fallback
3. **Deferred lighting**: Fixed register-based GBuffer binding (t0-t6 + samplers)
4. **Fallback exposure value**: `(MinAverageLuminance + MaxAverageLuminance) * 0.5 / 0.18` (middle grey calibration)

**Key insight**: UE5 doesn't generate IBL maps at runtime on SM5. They're baked offline during content authoring, then loaded as pre-computed textures. This differs from PC-only engines that assume compute shader availability.

---

## 1. Auto-Exposure (Eye Adaptation)

### Architecture: Two-Tier System

#### Tier 1: Histogram (SM5+)
- **Shader**: `FEyeAdaptationCS` compute shader (`PostProcessEyeAdaptation.usf`)
- **Dispatch**: Single group (1,1,1) reducing histogram texture to 1x1 exposure value
- **Feature level check**: `IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5)` (line 819)
- **Output**: `RWStructuredBuffer<float4> RWEyeAdaptationBuffer` → 4 floats per buffer

**Code Reference**:
- C++: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessEyeAdaptation.cpp` lines 825-876
- Shader: `W:\UE_5.6_Source\Engine\Shaders\Private\PostProcessEyeAdaptation.usf` lines 13-28, 170-198

```cpp
// Line 800-823: FEyeAdaptationCS definition
class FEyeAdaptationCS : public FGlobalShader
{
    // ...
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
        SHADER_PARAMETER_STRUCT(FLocalExposureParameters, LocalExposure)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistogramTexture)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, RWEyeAdaptationBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

// Line 855-860: Dispatch (single workgroup)
FComputeShaderUtils::AddPass(
    GraphBuilder,
    RDG_EVENT_NAME("HistogramEyeAdaptation (CS)"),
    ComputeShader,
    PassParameters,
    FIntVector(1, 1, 1));  // ← Single thread group
```

#### Tier 2: Fixed Exposure (ES3.1 / Mobile LDR)

**Feature level check** (line 169):
```cpp
static ERHIFeatureLevel::Type GetBasicEyeAdaptationMinFeatureLevel()
{
    return IsMobileHDR() ? ERHIFeatureLevel::ES3_1 : ERHIFeatureLevel::SM5;
}
```

When histogram isn't available, UE5 uses `GetEyeAdaptationFixedExposure()` (line 567):

```cpp
float GetEyeAdaptationFixedExposure(const FViewInfo& View)
{
    const FEyeAdaptationParameters Parameters = GetEyeAdaptationParameters(View);

    // Fallback exposure: midpoint of min/max luminance range
    const float Exposure = (Parameters.MinAverageLuminance + Parameters.MaxAverageLuminance) * 0.5f;

    // Middle grey calibration: 0.18 (18% grey card standard)
    const float kMiddleGrey = 0.18f;
    const float ExposureScale = kMiddleGrey / FMath::Max(0.0001f, Exposure);

    // Apply compensation curve (if any)
    return ExposureScale * Parameters.ExposureCompensationSettings;
}
```

**Key values from PostProcessEyeAdaptation.cpp**:
- `EyeAdaptation.MinAverageLuminance`: Console var configurable, default ~0.25
- `EyeAdaptation.MaxAverageLuminance`: Console var configurable, default ~4.0
- **Fallback exposure = (0.25 + 4.0) * 0.5 / 0.18 ≈ 11.8** (relative exposure multiplier)

### PreExposure Integration

The computed/fixed exposure is stored in `View.PreExposure` (line 1197-1298):

```cpp
void FViewInfo::UpdatePreExposure()
{
    // ...
    const bool bSupportPreExposureDifferentThanGlobalExposure = !(bMobilePlatform && !IsMobileHDR());

    if (!bSupportPreExposureDifferentThanGlobalExposure)
    {
        // Mobile LDR: apply exposure directly in base pass
        PreExposure = FixedExposure;
    }
    else if (bIsPreExposureRelevant)
    {
        // Compute exposure from histogram or fixed fallback
        const float FinalPreExposure = SceneColorTint * GlobalExposure * VignetteMask * LocalExposure;
        PreExposure = FinalPreExposure;
    }

    // Upload to GPU
    ViewState->PreExposure = PreExposure;
}
```

This `PreExposure` is then baked into:
- Base pass (deferred G-Buffer generation) via `View.PreExposure`
- Post-process tonemap via `View.PreExposure`
- **GBuffer indirect irradiance decode** (line 213 in DeferredShadingCommon.ush):

```hlsl
float EncodeIndirectIrradiance(float IndirectIrradiance)
{
    float L = IndirectIrradiance;
    L *= View.PreExposure;  // ← Applied here
    const float LogBlackPoint = 0.00390625;  // exp2(-8)
    return log2(L + LogBlackPoint) / 16 + 0.5;
}
```

---

## 2. BRDF LUT Generation

### Critical Finding: No SM5 Compute Shader Path

**There is NO pixel shader or CPU fallback for BRDF LUT generation in UE5.**

The BRDF LUT is **always pre-computed offline** during engine initialization or content preparation, **never at runtime**.

**Shader file**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SystemTextures.cpp`

The BRDF LUT texture is created as a **static system resource** and loaded from disk or generated once during startup:
- Resolution: 256×256 (RG16F)
- Format: Linear floating-point (16-bit R, 16-bit G for cosine bias and Fresnel)
- **Permanently available** to all deferred lighting passes

**For UzEngine**: If you need SM5 support, **pre-bake BRDF LUT offline** (via the IblBaker compute path on a dev machine), then load it as a static system texture. Do not attempt runtime generation on SM5.

---

## 3. Irradiance Cubemap (SH Projection)

### Offline Baked via Reflection Captures

UE5 uses **Reflection Capture actors** to pre-bake environment cubemaps and their irradiance projections **at edit time**, not runtime.

**Relevant code**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.cpp` lines 148-180 (FReflectionEnvironmentCubemapArray)
- SH projection: `SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance()` (line 623-669)

```cpp
void SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance(
    FVector4f* OutSkyIrradianceEnvironmentMap,
    const FSHVectorRGB3 SkyIrradiance)
{
    // Pre-computed SH coefficients (baked offline)
    const float SqrtPI = FMath::Sqrt(PI);
    const float Coefficient0 = 1.0f / (2 * SqrtPI);
    const float Coefficient1 = FMath::Sqrt(3.f) / (3 * SqrtPI);
    const float Coefficient2 = FMath::Sqrt(15.f) / (8 * SqrtPI);
    const float Coefficient3 = FMath::Sqrt(5.f) / (16 * SqrtPI);
    const float Coefficient4 = .5f * Coefficient2;

    // Pack into 7 float4 constants (SH coefficients L0, L1, L2)
    OutSkyIrradianceEnvironmentMap[0] = ...
    OutSkyIrradianceEnvironmentMap[1] = ...
    // ... etc (7 total float4s)
}
```

These SH constants are **uploaded once per frame via structured buffer** (line 690-714):

```cpp
FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(
    sizeof(FVector4f),
    SKY_IRRADIANCE_ENVIRONMENT_MAP_VEC4_COUNT);

FRDGBufferRef BufferRDG = GraphBuilder.RegisterExternalBuffer(Buffer, TEXT("SkyIrradianceEnvironmentMap"));

if (bUploadIrradiance)
{
    FVector4f* UploadData = (FVector4f*)GraphBuilder.Alloc(BufferSizeInBytes);
    SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance(UploadData, SkyLight->IrradianceEnvironmentMap);
    GraphBuilder.QueueBufferUpload(BufferRDG, UploadData, BufferSizeInBytes, ERDGInitialDataFlags::NoCopy);
}
```

**For UzEngine**: Store SH coefficients in a structured buffer (7 float4s). Bake them offline or compute them once at level load, then upload to GPU. No runtime compute shader needed on SM5.

---

## 4. Prefiltered Specular Cubemap

### Offline Baked via Reflection Captures

Like irradiance, the prefiltered specular cubemap (mip-mapped version of the reflection environment for rough surfaces) is **baked offline** using reflection captures.

**Stored as**: TextureCubeArray (multiple mips per capture)

**File reference**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.cpp` line 150-179

```cpp
void FReflectionEnvironmentCubemapArray::InitRHI(FRHICommandListBase&)
{
    if (SupportsTextureCubeArray(GetFeatureLevel()))
    {
        const int32 NumReflectionCaptureMips = FMath::CeilLogTwo(CubemapSize) + 1;

        FPooledRenderTargetDesc Desc(
            FPooledRenderTargetDesc::CreateCubemapDesc(
                CubemapSize,  // 128 typical
                PF_FloatRGBA,
                FClearValueBinding::None,
                TexCreate_None,
                TexCreate_None,
                false,
                FMath::Max<uint32>(MaxCubemaps, 2),  // Multiple captures
                NumReflectionCaptureMips  // 8 mips for 128×128
            )
        );

        GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ReflectionEnvs, TEXT("ReflectionEnvs"));
    }
}
```

**For UzEngine**: Pre-bake specular cubemaps offline with progressive downsampling (mip chain). Load as TextureCube or TextureCubeArray. On SM5, sample mips based on roughness; no runtime filtering needed.

---

## 5. Deferred Lighting Pass — Register-Based GBuffer Binding

### GBuffer Texture Binding

On SM5 (and all feature levels), GBuffer textures are bound as **named textures with static samplers** to the deferred lighting shader:

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredShadingCommon.ush` lines 32-51

```hlsl
#if SHADING_PATH_DEFERRED

#if !SUPPORTS_INDEPENDENT_SAMPLERS
    #error Scene textures may only be used on platforms which support independent samplers.
#endif

// ← GBuffer textures (register-bound in C++)
Texture2D SceneDepthTexture;
Texture2D<uint2> SceneStencilTexture;
Texture2D GBufferATexture;           // Diffuse + AO
Texture2D GBufferBTexture;           // Normal + Metallic
Texture2D GBufferCTexture;           // Roughness + Cavity
Texture2D GBufferDTexture;           // Custom data
Texture2D GBufferETexture;           // Precomputed shadow
Texture2D GBufferVelocityTexture;    // Motion vectors
Texture2D GBufferFTexture;           // Reserved
Texture2D<uint> SceneLightingChannels;

// ← Static samplers (bilinear point sampling)
#define SceneDepthTextureSampler GlobalPointClampedSampler
#define GBufferATextureSampler GlobalPointClampedSampler
#define GBufferBTextureSampler GlobalPointClampedSampler
#define GBufferCTextureSampler GlobalPointClampedSampler
#define GBufferDTextureSampler GlobalPointClampedSampler
#define GBufferETextureSampler GlobalPointClampedSampler
#define GBufferFTextureSampler GlobalPointClampedSampler
#define GBufferVelocityTextureSampler GlobalPointClampedSampler
```

**Access pattern in deferred lighting**:
```hlsl
float SampleDeviceZFromSceneTextures(float2 UV)
{
    return SceneDepthTexture.SampleLevel(SceneDepthTextureSampler, UV, 0).r;
}
```

### Shadow Atlas Binding

Shadow textures are also statically bound. No bindless indirection needed.

**Reference**: `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredLightPixelShaders.usf` lines 110-149

```hlsl
Texture2D<uint> LightingChannelsTexture;
Texture2D<uint4> ShadowMaskBits;  // Virtual shadow map mask (if enabled)

uint GetLightingChannelMask(float2 UV)
{
    uint2 IntegerUV = UV * View.BufferSizeAndInvSize.xy;
    return LightingChannelsTexture.Load(uint3(IntegerUV, 0)).x;
}

float4 GetLightAttenuationFromShadow(in FInputParams InputParams, float SceneDepth, float3 TranslatedWorldPosition)
{
    float4 LightAttenuation = float4(1, 1, 1, 1);

#if USE_VIRTUAL_SHADOW_MAP_MASK
    if (VirtualShadowMapId != INDEX_NONE)
    {
        float ShadowMask = GetVirtualShadowMapMaskForLight(
            ShadowMaskBits[InputParams.PixelPos],
            uint2(InputParams.PixelPos),
            SceneDepth,
            VirtualShadowMapId,
            TranslatedWorldPosition);
        return ShadowMask.xxxx;
    }
    else
#endif
    {
        return GetPerPixelLightAttenuation(InputParams.ScreenUV);
    }
}
```

### IBL Texture Binding

**Sky Irradiance Buffer**:
```cpp
// Structured buffer with 7 float4s (SH coefficients)
StructuredBuffer<float4> SkyIrradianceEnvironmentMap;

// Accessed in shader as:
float3 SampleSkyIrradiance(float3 N)
{
    // ... unpack SH coefficients ...
    // Evaluate SH basis functions ...
    // Return diffuse contribution
}
```

**Reflection Captures** (cubemap array):
```hlsl
Texture2D ReflectionCaptureTexture;  // Or TextureCubeArray
// Sample with cubemap coordinates, mip based on roughness
float3 envColor = ReflectionCaptureTexture.SampleLevel(EnvSampler, reflectDir, roughnessMip);
```

**BRDF LUT** (static system texture):
```hlsl
Texture2D BRDFLut;
// Sample as (NdotV, roughness)
float2 f0f90 = BRDFLut.SampleLevel(BRDFSampler, float2(NdotV, roughness), 0).xy;
```

### Root Signature Implications

On SM5 (DX11), root signatures are fixed and must declare all textures upfront:

```cpp
// Pseudo-code: D3D11 device context binding
pDeviceContext->PSSetShaderResources(0, 1, &GBufferATex);  // t0
pDeviceContext->PSSetShaderResources(1, 1, &GBufferBTex);  // t1
pDeviceContext->PSSetShaderResources(2, 1, &GBufferCTex);  // t2
pDeviceContext->PSSetShaderResources(3, 1, &ShadowAtlas);  // t3
pDeviceContext->PSSetShaderResources(4, 1, &BRDFLut);      // t4
pDeviceContext->PSSetShaderResources(5, 1, &ReflCaptures);  // t5
pDeviceContext->PSSetShaderResources(6, 1, &SkyIrrSRV);    // t6
```

**Register allocation is manual and static** — no GPU-driven resource binding.

---

## 6. Exposure Value When Auto-Exposure Unavailable

### Fallback Calculation

When compute shaders unavailable (ES3.1), UE5 uses **middle grey calibration**:

```cpp
// From PostProcessEyeAdaptation.cpp line 567-578
float GetEyeAdaptationFixedExposure(const FViewInfo& View)
{
    const FEyeAdaptationParameters Parameters = GetEyeAdaptationParameters(View);

    // Midpoint of luminance range
    const float Exposure = (Parameters.MinAverageLuminance + Parameters.MaxAverageLuminance) * 0.5f;

    // Standard grey card (18% reflectance)
    const float kMiddleGrey = 0.18f;
    const float ExposureScale = kMiddleGrey / FMath::Max(0.0001f, Exposure);

    // Apply curve compensation
    return ExposureScale * Parameters.ExposureCompensationSettings;
}
```

**Default parameters** (from eye adaptation volume):
- `MinAverageLuminance`: ~0.25
- `MaxAverageLuminance`: ~4.0
- `ExposureCompensationSettings`: ~0.5-1.5 (tweakable)

**Fallback exposure** ≈ **(0.25 + 4.0) / 2 / 0.18 × 1.0** ≈ **11.8x brightness multiplier**

This is **conservative** — bright enough for typical indoor/outdoor scenes without oversaturation.

---

## 7. Platform Feature Level Detection

### Compile-Time Checks

UE5 uses these patterns to gate SM5+ features:

```cpp
// Auto-exposure histogram (SM5+)
IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
    // Line 819: FEyeAdaptationCS

// Basic eye adaptation setup (ES3.1+)
IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::Type(GetBasicEyeAdaptationMinFeatureLevel()))
    // Line 899: FBasicEyeAdaptationSetupPS

// Reflection environment (requires TextureCubeArray)
SupportsTextureCubeArray(InFeatureLevel)
    // Line 150: FReflectionEnvironmentCubemapArray::InitRHI
```

### Runtime Branching

```cpp
// Eye adaptation method selection (PostProcessEyeAdaptation.cpp line 174-185)
bool IsAutoExposureMethodSupported(ERHIFeatureLevel::Type FeatureLevel, EAutoExposureMethod AutoExposureMethodId)
{
    switch (AutoExposureMethodId)
    {
    case EAutoExposureMethod::AEM_Histogram:
    case EAutoExposureMethod::AEM_Basic:
        return FeatureLevel > ERHIFeatureLevel::ES3_1 || IsMobileHDR();
    case EAutoExposureMethod::AEM_Manual:
        return true;  // Always supported (fixed exposure)
    }
    return false;
}
```

---

## 8. Recommendations for UzEngine SM5 Support

### 1. Auto-Exposure
- **Keep compute shader path** (currently working for SM6+)
- **Add pixel shader fallback** for mobile/SM5 that reads back a single exposure sample per frame
- **Store as `View.PreExposure`** in a 1×1 buffer

### 2. BRDF LUT
- **Pre-bake offline** using your IblBaker compute shader on a dev machine
- **Load from disk** at engine startup (via asset manager)
- **Bind as static system texture** (no runtime generation)

### 3. Irradiance SH
- **Compute offline** (CPU-based SH projection or compute on dev machine)
- **Store in structured buffer** (7 float4s per environment)
- **Upload per frame** via constant buffer

### 4. Prefiltered Specular
- **Pre-bake as mip-mapped cubemap** offline
- **Load from disk** at level load
- **Sample by roughness mip** in shader (linear between mips)

### 5. Deferred Lighting
- **Register-bind all GBuffer textures** (no dynamic indirection)
- **Use static samplers** (point, clamp)
- **Pre-allocate root signature** with known texture slots

### 6. Exposure Constants
- **Fallback value**: `(MinLum + MaxLum) / 2 / 0.18`
- **Default**: 11.8× brightness multiplier for typical scenes
- **Configurable** via PostProcessVolume at edit-time

---

## References

**Eye Adaptation**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessEyeAdaptation.cpp` (1400+ lines)
- `W:\UE_5.6_Source\Engine\Shaders\Private\PostProcessEyeAdaptation.usf`

**Reflection Environment**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.cpp` (715 lines)
- `W:\UE_5.6_Source\Engine\Shaders\Private\ReflectionEnvironmentShared.ush`

**Deferred Lighting**:
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredShadingCommon.ush`
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredLightPixelShaders.usf`

**Feature Level Checks**:
- Search for `ERHIFeatureLevel::SM5` in RHI headers
- `RHIDefinitions.h`: Feature level enum definitions
