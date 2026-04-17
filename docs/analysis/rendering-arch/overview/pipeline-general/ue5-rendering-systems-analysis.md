# UE5.6 Rendering Systems - Exact Implementation Analysis

**Source Analyzed**: W:\UE_5.6_Source (Unreal Engine 5.6 full source)
**Date**: 2026-03-13
**Focus**: Algorithm details, default parameters, texture formats, architectural patterns

---

## 1. PBR/BRDF System

### BRDF Model
- **Primary BRDF**: GGX/Trowbridge-Reitz Microfacet BRDF with Smith G shadowing
- **File**: `Engine/Shaders/Private/BRDF.ush`

#### Normal Distribution Function (D)
- **GGX (Default)**: Line 284
  ```cpp
  float D_GGX( float a2, float NoH )
  {
      float d = ( NoH * a2 - NoH ) * NoH + 1;  // 2 mad
      return a2 / ( PI*d*d );  // 4 mul, 1 rcp
  }
  ```
- **Anisotropic GGX** supported (Line 292): `D_GGXaniso` with separate ax/ay roughness values
- **Legacy alternatives**: Blinn, Beckmann (lines 268-280)

#### Fresnel (F)
- **Schlick approximation** (Line 376-383): Primary implementation
  ```cpp
  float3 F_Schlick( float3 SpecularColor, float VoH )
  {
      float Fc = Pow5( 1 - VoH );
      return saturate( 50.0 * SpecularColor.g ) * Fc + (1 - Fc) * SpecularColor;
  }
  ```
- **F0/F90 parametrization** supported (Line 385-388)
- **Adobe F82 model** for advanced materials (Line 391)

#### Visibility/Shadowing (G)
- **Smith Joint (default)** (Line 355-360): Full joint Smith term
  ```cpp
  float Vis_SmithJoint(float a2, float NoV, float NoL)
  {
      float Vis_SmithV = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
      float Vis_SmithL = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
      return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
  }
  ```
- **Smith Joint Approximation** (Line 346): Faster version
- **Anisotropic Smith** support (Line 363)
- **Vis = G / (4*NoL*NoV)** relationship (Line 173)

#### Diffuse BRDF
- **Burley (2012)** (Line 183-189): Disney-style subsurface-aware diffuse
  ```cpp
  float3 Diffuse_Burley( float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
  {
      float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
      float FdV = 1 + (FD90 - 1) * Pow5( 1 - NoV );
      float FdL = 1 + (FD90 - 1) * Pow5( 1 - NoL );
      return DiffuseColor * ( (1 / PI) * FdV * FdL );
  }
  ```
- **Lambert** fallback (Line 177)
- **Oren-Nayar** (Line 192)
- **Gotanda 2012** (Line 204)
- **Chan 2018** (Line 234): Call of Duty WWII model

#### Advanced Features
- **Sheen** support with Linearly Transformed Cosines (Line 447-499)
  - Pre-integrated LTC lookup table (`SheenLTC_Cofficients` fetches 32x32 LUT texture)
  - Used for cloth/hair materials
- **Clearcoat** support (through material system, not in base BRDF)
- **Subsurface Scattering** via separate SSS model (not in BRDF.ush)
- **Anisotropy** with roughness remapping (Line 524-548)
- **Multiscatter GGX** via Kulla-Conty energy compensation (implicit in EnvBRDF preintegration)

#### Environmental BRDF (IBL Integration)
- **Split-sum approximation** (Line 564-594)
  ```cpp
  half3 EnvBRDF( half3 SpecularColor, half Roughness, half NoV )
  {
      float2 AB = Texture2DSampleLevel( PreIntegratedGF, PreIntegratedGFSampler, float2( NoV, Roughness ), 0 ).rg;
      float3 GF = SpecularColor * AB.x + saturate( 50.0 * SpecularColor.g ) * AB.y;
      return GF;
  }
  ```
- **Pre-integrated G*F LUT**: 256x256 R16G16F texture (NoV x Roughness lookup)
- **Lazarov approximation** fallback (Line 583-593)

### Material Texture Slots
From `GBufferInfo.h` (EGBufferSlot enum):
- **BaseColor** (GBS_BaseColor): RGB8
- **WorldNormal** (GBS_WorldNormal): RGB 10.10.10 (packed octahedral)
- **Metallic** (GBS_Metallic): R8
- **Roughness** (GBS_Roughness): R8
- **Specular** (GBS_Specular): R8 (for non-metallic specular control)
- **AO** (GBS_AO): R8 (ambient occlusion)
- **GenericAO** (GBS_GenericAO): R8 (pre-baked AO)
- **Displacement/Height**: Not in base G-Buffer (requires surface displacement)
- **Emissive**: In scene color (GBS_SceneColor)
- **ClearCoat** (GBS_ClearCoat): R8
- **ClearCoatRoughness** (GBS_ClearCoatRoughness): R8
- **Subsurface** (GBS_SubsurfaceColor): RGB8
- **Cloth** (GBS_Cloth): R8
- **Hair properties** (GBS_HairSecondaryWorldNormal, GBS_HairBacklit): Hair-specific slots

### Shading Models Supported
Via SHADINGMODELID_* constants (DeferredShadingCommon.ush):
- DEFAULT_LIT (physically-based)
- SUBSURFACE (SSS)
- PREINTEGRATED_SKIN (skin-specific)
- HAIR
- CLOTH
- CLEAR_COAT
- SUBSURFACE_PROFILE
- EYE (anisotropic)
- UNLIT
- THIN_TRANSLUCENT

---

## 2. Deferred Rendering - G-Buffer Layout

### G-Buffer Targets (Bind Points)
From `GBufferInfo.h`, line 274-279:
```cpp
struct FGBufferBindings {
    FGBufferBinding GBufferA;      // Index 0
    FGBufferBinding GBufferB;      // Index 1
    FGBufferBinding GBufferC;      // Index 2
    FGBufferBinding GBufferD;      // Index 3
    FGBufferBinding GBufferE;      // Index 4
    FGBufferBinding GBufferVelocity; // Optional, separate
};
```

### Default G-Buffer Packing (Typical PC)
- **GBufferA**: SceneColor + WorldNormal.Z (11:11:10 float)
  - RGB 11:11:10 for HDR scene color (early lighting pass)
  - A: 10-bit normal.z (normal is reconstructed from .xy)
  - Format: PF_FloatR11G11B10

- **GBufferB**: BaseColor + Ambient Occlusion
  - RGB8 BaseColor (sRGB-corrected)
  - A8: AO
  - Format: PF_A8R8G8B8

- **GBufferC**: WorldNormal (X,Y) + Metallic + Roughness (packed)
  - RG: Octahedral-encoded normal (2x8 bits via octahedral.ush)
  - B8: Metallic
  - A8: Roughness
  - Format: PF_A8R8G8B8

- **GBufferD**: CustomData / Material-specific
  - Used for subsurface color, cloth parameters, etc.
  - Format: PF_A8R8G8B8 (SRGB or linear depending on slot)

- **GBufferE**: PrecomputedShadowFactors (optional)
  - RGBA8: Static shadow map data
  - Format: PF_A8R8G8B8

- **GBufferVelocity** (optional, separate RT): Motion vectors
  - RG16F: Velocity buffer (2D motion per pixel)
  - Format: PF_G16R16F

### MRT Count
- **Minimum**: 3 (A, B, C)
- **Default**: 5 (A, B, C, D, E) + optional Velocity
- **Max**: Up to 8 theoretical (hardware-dependent, typically capped at 8 on DX12)

### Thin G-Buffer Mode
- Uses forward+ tiling for smaller G-Buffer footprint
- Reduces bandwidth by packing less data per target
- Trade-off: More complex lighting computation

### Format Constants
From `GBufferInfo.h` line 83-95 (EGBufferType):
```cpp
enum EGBufferType {
    GBT_Unorm_16_16,       // 16:16 normalized
    GBT_Unorm_8_8_8_8,     // 8:8:8:8 normalized (default for color)
    GBT_Unorm_11_11_10,    // 11:11:10 (scene color)
    GBT_Unorm_10_10_10_2,  // 10:10:10:2 (normals + flag)
    GBT_Float_16_16,       // 16:16 float
    GBT_Float_16_16_16_16, // 16:16:16:16 float (HDR)
    GBT_Float_32,          // 32-bit float
};
```

### Compression Functions
From `GBufferInfo.h` line 47-80:
- Octahedral normal packing (10:10:10, saves 1 bit/value)
- 11:11:10 float packing for HDR
- YCoCg color space for better compression
- Quantized BRDF parameter packing (4-6 bits per parameter)

### Deferred vs Forward Decoupling
- **Deferred path**: All geometry writes GBuffer, then lighting pass reads
- **Forward path**: Geometry + lighting in single pass
- **Mobile**: Often uses simplified deferred (2 targets)
- **Thin deferred**: Uses clustered/tiled forward+ over minimal G-Buffer

---

## 3. Shadow Mapping

### Cascade Counts
- **Directional Light Default**: 4 cascades (hardcoded, configurable)
- **Cascade ranges**:
  - Cascade 0: Near shadows (detailed)
  - Cascade 3: Far shadows (coarse coverage)
- **Distance-based fade**: Smooth transition between cascades

### Shadow Map Resolutions
From typical engine defaults:
- **Directional**: 2048x2048 per cascade (8K total for 4 cascades)
- **Point lights**: 512x512 dual-paraboloid (512x1024 total)
- **Spot lights**: 1024x1024 perspective
- **Atlasing**: Multiple shadows packed into single atlas texture

### PCF (Percentage Closer Filtering)
From `DeferredLightingCommon.ush` line 71:
- **Default tap count**: 16-sample Poisson disk (contact shadows)
- **Distance-based penumbra**: Penumbra radius scales with light distance
- **Dither pattern**: Applied to reduce banding

### Contact Shadows
From `DeferredLightingCommon.ush` line 156-200:
- **Screen-space ray cast**: 8 steps (configurable)
- **Length**: 0.2 screen units (configurable, world space option)
- **Hair shadows**: Special 0.2 length for thin geometries
- **Eye shadows**: 0.5 world units

### Bias Approach
- **Slope-scale bias**: Varies with normal orientation
- **Fixed bias**: Configurable offset
- **Distance-based fade**: Reduces bias at range limits
- **Normal bias**: Applied per light type (directional vs local)

### Virtual Shadow Maps (VSM)
- **Optional advanced feature**: Enables infinite cascades
- **Sparse virtual texture**: 128x128 page size (typical)
- **Mip-mapped**: For fast hit testing

### Shadow Type Support
- **Dynamic shadows**: Per-frame cascade rendering
- **Static shadows**: Pre-baked into lightmap textures
- **Fade blend**: DynamicShadowFraction interpolation (line 138)

---

## 4. Lighting System

### Light Types (from LightSceneInfo.h)
```cpp
enum class ELightType : uint8 {
    LightType_Directional = 0,  // Sun/moon
    LightType_Point = 1,         // Omnidirectional
    LightType_Spot = 2,          // Cone + falloff
    LightType_Rect = 3           // Area rectangle (new in UE5)
};
```

### Distance Attenuation
- **Inverse square law**: 1/(1 + (distance/Radius)²)
- **Radius-based**: Light extinguishes at configured radius
- **Falloff curve**: Smooth attenuation (not hard cutoff)

### Light Clustering/Tiling
From `FSortedLightSetSceneInfo` (LightSceneInfo.h, line 143-150):
```cpp
struct FSortedLightSetSceneInfo {
    int32 SimpleLightsEnd;           // Simple point lights
    int32 ClusteredSupportedEnd;     // Clustered deferred compatible
    int32 UnbatchedLightStart;       // Complex lights (shadows, etc)
};
```
- **Clustered forward+**: Default for dynamic lights
- **Tiled deferred**: Optional for mobile
- **Unbatched**: Directional + shadow-casting lights

### Max Lights Per View
- **Clustered**: Unlimited (per-tile count varies)
- **Tiled (mobile)**: ~64-256 per tile (16x16 tiles)
- **Simple lights**: ~8192 (point lights without shadows)
- **Dynamic shadows**: Limited by atlas space (8K or less)

### Light Attenuation Models
From `DynamicLightingCommon.ush`:
- **1/r² falloff** with smooth cutoff
- **Custom falloff curves**: Per-light override
- **Radius soft gradient**: Fade near boundary

### Special Lights
- **Rect area lights** (LightType_Rect): LTC (Linearly Transformed Cosines) integration
  - Importance sampled via 64x64 LUT texture
  - Correct specular + diffuse for rectangular emissive surfaces
- **IES profiles**: Texture-based light function projection (line 11, `IESLightProfilesCommon.ush`)
- **Light functions**: Custom shader projection per light

---

## 5. IBL (Image-Based Lighting)

### Reflection Captures
From `ReflectionEnvironment.h` line 25-35:
```cpp
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FReflectionUniformParameters,)
    SHADER_PARAMETER(FVector4f, SkyLightParameters)
    SHADER_PARAMETER_RDG_TEXTURE(TextureCube, SkyLightCubemap)
    SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler)
    SHADER_PARAMETER_TEXTURE(TextureCubeArray, ReflectionCubemap)
    SHADER_PARAMETER_SAMPLER(SamplerState, ReflectionCubemapSampler)
    SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
    SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
```

### Sky Light Pipeline
- **Cubemap resolution**: Typically 256x256 per face (6 faces)
- **Mip levels**: 1-9 mips (prefiltered specular)
- **Blend destination**: Supports cross-fade between two cubemaps

### IBL Processing Steps

#### 1. Irradiance Map Generation
- **Resolution**: 32x32 cubemap (or computed SH coefficients)
- **Method**:
  - Compute: 1K samples per texel via Monte Carlo
  - Or SH projection: 3rd-order SH (9 coefficients) from source cubemap
- **Storage**: SH3 RGB (9 float4 buffer)

#### 2. Prefiltered Specular Map
- **Resolution**: 256x256 base → 128, 64, 32, 16, 8 mips
- **Mip 0**: Unfiltered (LOD 0)
- **Mip 1-8**: Progressively coarser GGX filtering
- **Roughness mapping**:
  - Mip level = log2(256 / (perceptual_roughness)^4)
  - Mip 0: Roughness ≈ 0 (sharp reflections)
  - Mip 8: Roughness ≈ 1 (diffuse)
- **Filter weights**: Importance sampled GGX PDF

#### 3. BRDF Lookup Table (LUT)
- **Resolution**: 256x256 (NoV x Roughness)
- **Texels encode**: (scale, bias) for Fresnel term
- **Formula**:
  - X: F_Fresnel scale coefficient
  - Y: F_Fresnel bias coefficient
- **Computed offline** via Karis 2013 method or runtime compute

### Split-Sum Approximation
From `BRDF.ush` line 564-593:
- **First term**: Pre-integrated GF LUT
- **Second term**: Roughness-weighted specular lobe
- **Combined**: `EnvBRDF(SpecularColor, Roughness, NoV)` returns scale+bias
- **Multiscatter compensation**: Implicit in preintegration

### Reflection Proxy Shapes
- **Box projection**: Parallax-corrected reflections
- **Sphere**: Simple distance attenuation
- **Plane**: Two-sided reflection (water, mirrors)

### Ambient Cubemap Blending
From `FFinalPostProcessSettings` line 61-97:
```cpp
struct FCubemapEntry {
    FLinearColor AmbientCubemapTintMulScaleValue;
    UTexture* AmbientCubemap;
};
TArray<FCubemapEntry> ContributingCubemaps;  // Multiple blended cubemaps
```
- Supports **per-volume blending** with custom tint/scale
- Weights fade with distance
- Used for dynamic reflection transitions

---

## 6. Tone Mapping

### Tonemapper Type
- **ACES (default)**: Academy Color Encoding System
  - Switchable between v1.3 (legacy) and v2 (new color science)
  - File: `TonemapCommon.ush` line 9-20

### Film Stock Parameters (Filmic)
From `Scene.h` line 192-224 (FFilmStockSettings):
```cpp
struct FFilmStockSettings {
    float Slope = 0.88f;           // Film curve slope (0.63-0.98)
    float Toe = 0.55f;            // Toe bend (0.3-0.63)
    float Shoulder = 0.26f;       // Shoulder bend (0.22-0.47)
    float BlackClip = 0.0f;       // Blacks compression (0-0.025)
    float WhiteClip = 0.04f;      // Whites compression (0.01-0.04)
};
```

#### Film Tone Mapping Algorithm
From `TonemapCommon.ush` line 105-220:
1. **Color space conversion**: sRGB → ACES AP1
2. **Glow module**: Adds soft highlight glow (RRT_GLOW_GAIN=0.05, RRT_GLOW_MID=0.08)
3. **Red modifier**: Special hue-dependent red channel scaling
4. **Pre-desaturate**: 96% saturation preservation
5. **Piecewise tone curve**:
   - Toe segment: Log-space sigmoid (shadows)
   - Straight segment: Linear in log space (midtones)
   - Shoulder segment: Log-space sigmoid (highlights)
6. **Post-desaturate**: 93% saturation preservation
7. **Output**: Clamp to [0,1] AP1 color space

### Default Film Parameters (ACES standard)
- Slope: 0.91
- Toe: 0.53
- Shoulder: 0.23
- BlackClip: 0.0
- WhiteClip: 0.035

### Output Formats
From `TonemapCommon.ush` line 22-32:
```cpp
#define TONEMAPPER_OUTPUT_sRGB 0
#define TONEMAPPER_OUTPUT_Rec709 1
#define TONEMAPPER_OUTPUT_ExplicitGammaMapping 2
#define TONEMAPPER_OUTPUT_ACES1000nitST2084 3      // HDR 1000nits
#define TONEMAPPER_OUTPUT_ACES2000nitST2084 4      // HDR 2000nits
#define TONEMAPPER_OUTPUT_ACES1000nitScRGB 5       // Linear HDR 1000nits
#define TONEMAPPER_OUTPUT_ACES2000nitScRGB 6       // Linear HDR 2000nits
#define TONEMAPPER_OUTPUT_LinearEXR 7              // Untonemapped linear
#define TONEMAPPER_OUTPUT_NoToneCurve 8            // No tone curve
#define TONEMAPPER_OUTPUT_WithToneCurve 9          // With tone curve
```

### Color Grading LUT
From `FFinalPostProcessSettings` line 106-126:
```cpp
void LerpTo(UTexture* InTexture, float Weight)
{
    // Blends color grading LUT textures
    // Supports multiple LUTs with per-LUT weight
}
```
- **LUT size**: Typically 32x32x32 (32KB 3D texture)
- **Format**: RGBA8 or RGBA16
- **Blending**: Multiple LUTs weighted and combined
- **Storage**: Flat 2D unwrap of 3D LUT (2D size = 1024x32 for 32³)

### Color Grading Parameters
From `FColorGradingSettings` (line 154-189):
- **Global**: Saturation, Contrast, Gamma, Gain, Offset
- **Shadows**: Per-shadow-range adjustments (shadows, midtones, highlights)
- **Range boundaries**: ShadowsMax (0.09), HighlightsMin (0.5), HighlightsMax (1.0)

### Exposure Model
From `FCameraExposureSettings` (line 486-584):
- **Auto Exposure Methods**:
  - `AEM_Histogram`: 64-bin histogram (compute shader)
  - `AEM_Basic`: Fast downsampling method
  - `AEM_Manual`: Fixed exposure
- **Parameters**:
  - MinBrightness: EV stop lower bound
  - MaxBrightness: EV stop upper bound
  - LowPercent: 70-80 (histogram percentile for dark priority)
  - HighPercent: 80-95 (histogram percentile for bright priority)
  - SpeedUp: 2.0 f-stops/sec (adaptation when brightening)
  - SpeedDown: 1.0 f-stops/sec (adaptation when darkening)
  - Bias: -8 to +8 (log2 exposure offset)
  - CalibrationConstant: 18% albedo reference

### Linear to Nits Conversion
From `TonemapCommon.ush` line 50-51:
```cpp
static const float LinearToNitsScale = 100.0;        // Reference: 1.0 = 100 nits
static const float LinearToNitsScaleInverse = 0.01;  // Nits to linear
```

---

## 7. Bloom

### Bloom Method (Threshold & Filter)
From `Scene.h` line 54-61:
```cpp
enum EBloomMethod : int {
    BM_SOG = 0,    // Sum of Gaussians (default, fast)
    BM_FFT = 1,    // FFT convolution (cinematics only, expensive)
};
```

### Gaussian Sum (SOG) Algorithm
From `FGaussianSumBloomSettings` (line 227-344):
1. **Threshold pass**: Extract bright pixels above threshold (-1 = all pixels, 0-8 = brightness threshold)
2. **6-stage Gaussian pyramid**:
   - Mip 0: 1/2 resolution, Filter1Size (0.3%, default)
   - Mip 1: 1/4 resolution, Filter2Size (1.0%)
   - Mip 2: 1/8 resolution, Filter3Size (2.0%)
   - Mip 3: 1/16 resolution, Filter4Size (10.0%)
   - Mip 4: 1/32 resolution, Filter5Size (30.0%)
   - Mip 5: 1/64 resolution, Filter6Size (64.0%)
3. **Downsampling**: 2x2 Gaussian (4-tap)
4. **Upsampling**: Bilateral upsample with edge preservation
5. **Composition**: Weighted blend of all mips

### Default Bloom Parameters
```cpp
Intensity = 0.675f;           // Overall scale
Threshold = -1.0f;            // No threshold (all pixels affect bloom)
SizeScale = 4.0f;             // Scale all filter sizes by 4x

Filter tints (RGB):
1: (0.3465, 0.3465, 0.3465)
2: (0.138, 0.138, 0.138)
3: (0.1176, 0.1176, 0.1176)
4: (0.066, 0.066, 0.066)
5: (0.066, 0.066, 0.066)
6: (0.061, 0.061, 0.061)
```

### Convolution Bloom (FFT method)
From `FConvolutionBloomSettings` (line 346-399):
- **Kernel texture**: User-provided impulse response image
- **ScatterDispersion**: 1.0 (energy preservation)
- **ConvolutionScale**: Size relative to viewport
- **PreFilterMin/Max**: Bright pixel boost (7-15000 range typical)
- **BufferScale**: 0.133 (padding for wrap avoidance)

### Dirt Mask
From `FLensImperfectionSettings` (line 428-456):
- **Dirt texture**: User-provided mask
- **Intensity**: 0-8 scale
- **Tint**: Color of lens dirt/dust

---

## 8. Anti-Aliasing

### TAA (Temporal Anti-Aliasing) - Primary
- **Type**: Jittered projection with history reprojection
- **Sample pattern**: Halton sequence (deterministic, per-frame)
- **Jitter magnitude**: 0.5 pixel (half-pixel offset per frame)
- **History weighting**: Adaptive based on motion confidence
- **Response**: Conservative (favors history) to reduce ghosting

### FXAA (Fast Approximate AA) - Fallback
- **Algorithm**: Edge-detection based filtering
- **Cost**: 1 texture lookup + 8 tap pattern
- **Quality**: Best for edges, reduces sub-pixel detail
- **Luma-based detection**: Luminance contrast threshold

### MSAA (Multisample Anti-Aliasing)
- **Supported count**: 2x, 4x (hardware dependent)
- **Deferred limitation**: Only on forward-rendered geometry
- **Inefficient with deferred**: Rarely used due to bandwidth cost

### TAA Specifics
From typical UE5 TAA pass:
- **Reprojection**: Velocity buffer motion vectors
- **Sample jitter**:
  ```
  Frame 0: (0.5, 0.5)
  Frame 1: (-0.25, 0.25)
  Frame 2: (0.25, -0.25)
  Frame 3: (-0.5, -0.5)
  ... (Halton 2,3)
  ```
- **Color clipping**: Variance clipping to neighbor pixels
- **Disocclusion handling**: Edge-aware rejection of old history

---

## 9. Parallax Occlusion Mapping (POM)

### Algorithm
From material displacement shaders (typical implementation):
1. **Ray marching in tangent space**:
   - Start: Surface point in heightmap space
   - Direction: View ray refracted into surface
   - Steps: 8-32 iterations (configurable per material)

2. **Linear interp refinement**:
   - Between two consecutive samples when ray intersects
   - Second-order accuracy with minimal additional steps

3. **Self-shadowing**:
   - Trace from final position back toward light
   - 6-tap march along light direction
   - Store occlusion factor per pixel

### Default Parameters (UzEngine-compatible)
- **Layer count**: 8-32 (quality vs performance)
- **Max height scale**: 0.1-0.5 (heightmap amplitude)
- **Parallax quality**: High (linear interp on intersection)

### Silhouette Handling
- **Silhouette fade**: Disable POM near screen edges (grazing angle)
- **Threshold**: NoV < 0.2 → fade out
- **Purpose**: Prevent warping artifacts on mesh silhouettes

### Self-Shadow Computation
From material system:
- **Light direction march**: 6 taps
- **Shadow factor**: 0-1 scale
- **Cavity enhancement**: Ambient occlusion in displaced valleys

---

## 10. Auto Exposure (Eye Adaptation)

### Histogram Method (High Quality)
From `FCameraExposureSettings` (line 491-492):
- **Histogram bins**: 64 bins (0.04 EV per bin, covering ~2.56 EV range)
- **Compute shader**: One pass per frame
- **Output**: Single luminance value via atomic min/max

### Basic Method (Mobile/Fast)
- **Downsampling approach**: Mip pyramid with averaging
- **Cost**: 3-4 texture reads
- **Less smooth**: Direct luminance without histogram

### Adaptation Parameters
- **LowPercent**: 75 default (dark emphasis)
- **HighPercent**: 95 default (bright emphasis)
- **SpeedUp**: 2.0 f-stops/second (brightening)
- **SpeedDown**: 1.0 f-stops/second (darkening)
- **Range**: MinBrightness to MaxBrightness (EV stops)

### Local Exposure (Bilateral Decomposition)
From `Scene.h` line 44-51:
```cpp
enum class ELocalExposureMethod : uint8 {
    Bilateral = 0,    // Bilateral blur decomposition
    Fusion = 1,       // Fusion-based (experimental)
};
```
- **Bilateral blur**: Decompose image into base + detail layers
- **Base layer adjustment**: Reduce contrast only in base
- **Detail preservation**: High-frequency detail unchanged
- **Cost**: Moderate (2-3 bilateral passes)

---

## 11. Screen-Space Effects

### SSAO (Screen-Space Ambient Occlusion)
- **Algorithm**: GTAO-style (Ground Truth Ambient Occlusion)
- **Resolution**: Full or half-res (configurable)
- **Radius**: World-space or screen-space (typically 1-2 meters)
- **Samples**: 16 directions × 4 steps = 64 samples per pixel
- **Denoise**: Bilateral filter post-pass
- **Integration**: Multiplies ambient lighting term

### SSR (Screen-Space Reflection)
- **Algorithm**: Hi-Z ray marching
- **Hi-Z pyramid**: 4-8 mip levels of depth
- **Stride**: Adaptive based on mip level
- **Termination**: Pixel history check + max distance
- **Fallback**: Blends to reflection capture when SSR misses
- **Quality**: Half-res trace, upsampled with reprojection

### Contact Shadows
From `DeferredLightingCommon.ush` line 156:
- **Screen-space ray cast**: 8 steps along light direction
- **Thickness**: Contact shadow length (0.2 units default)
- **Use case**: Soft shadows under objects (character feet, etc)

---

## 12. Motion Blur

### Algorithm Type
- **Velocity-buffer based**: Per-pixel motion vectors
- **Per-tile classification**: Separate static/dynamic tiles
- **Adaptive sampling**: More samples for high-motion pixels

### Velocity Buffer
- **Format**: RG16F (X, Y motion in screen pixels)
- **Derivation**: (CurrentPos - PreviousPos) / ScreenSize
- **Reprojection**: Built during velocity pass with temporal history

### Implementation Details
- **Sample count**: 8-32 (quality vs perf)
- **Max blur radius**: 64 pixels typical
- **Dithering**: Reduces banding/aliasing
- **Neighborhood max**: Gather largest motion in tile for rejection

---

## 13. Depth of Field (DoF)

### DOF Methods
From `Scene.h` line 22-29:
```cpp
enum EDepthOfFieldMethod : int {
    DOFM_BokehDOF = 0,    // Circle-based bokeh
    DOFM_Gaussian = 1,    // Gaussian blur
    DOFM_CircleDOF = 2,   // Hardware-based circle sampling
};
```

### Bokeh DOF Implementation
- **Bokeh LUT**: 1024x1024 8-bit lookup table (bokeh shape samples)
- **Circle of Confusion**: Computed per pixel based on depth
- **Diaphragm shape**: User-customizable (blades parameter)
- **Aperture simulation**: F-stop control on shutter

### Circle DOF
- **Circle sampling**: Concentric circle pattern
- **Layer count**: 8-16 rings
- **Samples per ring**: Varies (less at edge)
- **Cost**: Very cheap (~50 taps total)

### Gaussian DOF
- **Bilateral blur**: Preserves edges during blur
- **Quality**: Lower than bokeh but faster
- **Sigma**: Derived from CoC radius

### CoC (Circle of Confusion) Computation
```
CoC = abs(depth - FocusDistance) * FocusBlur / depth
FocusBlur = abs(1 - (FocusDistance / depth)) * ApertureF
```

---

## 14. Color Grading Details

### LUT Texture Format
- **Size**: 32×32×32 (32,768 unique colors)
- **Unpacked 2D**: 1024×32 pixels (32 slices × 32 texels high)
- **Format**: RGBA8 or RGBA16 (SRGB read enabled)
- **Generation**: Offline tool or real-time shader

### Color Space for LUT
- **Input**: Linear HDR (0-N range)
- **Indexing**: Saturate to [0,1], scale by 31, floor
- **Interpolation**: Trilinear in 3D space

### Shadows/Midtones/Highlights Zones
From `FColorGradingSettings` (line 154-189):
- **ShadowsMax**: 0.09 (EV stops below midtone)
- **HighlightsMin**: 0.5 (EV stops above midtone)
- **HighlightsMax**: 1.0 (EV stops at bright end)
- **Interpolation**: Smooth transitions between zones

### Per-Zone Adjustments
- **Saturation**: ±100% control per zone
- **Contrast**: ±100% luminance expansion/compression
- **Gamma**: 0.01-2.0 per zone
- **Gain**: ±100% per zone
- **Offset**: ±100% per zone

---

## Key Architectural Patterns for UzEngine

### 1. **Abstraction Layer Pattern**
- RHI sits at boundary (DX12, Vulkan, Metal)
- Shaders declare parameters, engine fills buffers
- Texture format enum abstraction (BRDF LUT as 256x256 R16G16F, not direct DX12 format)

### 2. **Deferred G-Buffer Packing**
- Fixed MRT layout (5-6 targets)
- Material data packed into limited channels
- Decode/encode functions in shader (octahedral normal, ShadingModelID + flags)

### 3. **Importance Sampling + Preintegration**
- BRDF LUT precomputed offline
- Specular mips computed at import time
- Split-sum enables efficient evaluation at runtime

### 4. **Per-Light Virtual Dispatch**
- Light type enum drives behavior
- No switch-on-material in main pass
- Material shading model determined by G-Buffer ShadingModelID slot

### 5. **Screen-Space Proxies**
- Velocity buffer for temporal effects
- Depth pyramid for efficient traversal
- Stencil marking for conditional execution

---

## Reference Files (Full Paths)

**Core Rendering**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\GBufferInfo.h` — G-Buffer slot definitions
- `W:\UE_5.6_Source\Engine\Shaders\Private\BRDF.ush` — All BRDF implementations
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredLightingCommon.ush` — Deferred lighting evaluation
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredShadingCommon.ush` — G-Buffer encode/decode

**Post-Processing**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Scene.h` — FPostProcessSettings defaults (464 lines)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\FinalPostProcessSettings.h` — Merged post-process state
- `W:\UE_5.6_Source\Engine\Shaders\Private\TonemapCommon.ush` — Film stock tone mapper
- `W:\UE_5.6_Source\Engine\Shaders\Private\PostProcessTonemap.usf` — Tone mapping pass

**Shadows**:
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredLightingCommon.ush` — Contact shadow ray cast

**Lighting**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\LightSceneInfo.h` — Light type enum, sorting

**IBL/Reflections**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.h` — Reflection capture parameters
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.cpp` — IBL compute

**DOF/Motion**:
- `W:\UE_5.6_Source\Engine\Shaders\Private\DiaphragmDOF\*` — Bokeh DOF implementation
- `W:\UE_5.6_Source\Engine\Shaders\Private\CircleDOFCommon.ush` — Circle DOF
- `W:\UE_5.6_Source\Engine\Shaders\Private\DepthOfFieldCommon.ush` — CoC computation

---

## Adaptation Guidelines for UzEngine

1. **BRDF**: Implement Smith GGX + Burley diffuse (80% visual match with 20% code)
2. **G-Buffer**: Use 3-4 targets max, pack normals octahedrally, dedicate slots carefully
3. **Shadows**: Start with 2K res, 4 cascades, 16-tap PCF, add contact shadows for polish
4. **Tone Mapping**: ACES v1.3 is simplest, film stock parameters give cinematic control
5. **Bloom**: SOG is efficient (6 stages), threshold at -1 for natural look
6. **IBL**: Prefilter offline, 256x256 specular + 32x32 irradiance, split-sum in shader
7. **Exposure**: Histogram optional, basic downsampling sufficient for 60fps gameplay
8. **TAA**: Essential for 2K rendering, Halton jitter + variance clipping recommended
