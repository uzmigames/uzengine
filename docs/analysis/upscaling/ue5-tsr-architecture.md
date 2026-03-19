<!-- doc-meta version="1" -->

# UE5 Temporal Super Resolution (TSR) Architecture Analysis

**Date**: 2026-03-18
**UE5 Version**: 5.6
**Source Root**: `W:\UE_5.6_Source`

## Executive Summary

This document analyzes UE5 5.6's Temporal Super Resolution (TSR) system to extract architectural patterns for UzEngine adaptation. TSR is a complex multi-pass temporal upsampling system that combines:

1. **Velocity dilation** with spatial anti-aliasing to handle sub-pixel geometric movement
2. **History reprojection** using dilated velocities and optional reprojection fields (jacobians)
3. **Shading rejection** via neural convolution network to detect disoccluded/changed surfaces
4. **Flickering detection** via luminance analysis across frames to prevent erratic temporal accumulation
5. **Spatial anti-aliasing** when history is rejected, reducing aliasing from low render resolution
6. **Responsive history accumulation** that clamps temporal weights based on pixel velocity

---

## Architecture Overview

### Pass Execution Order

TSR executes **11 compute passes** in sequence (with async compute opportunities):

| Order | Pass Name | Purpose | Key CVars |
|-------|-----------|---------|-----------|
| 0 | **ClearPrevTextures** | Reset atomic counters for reprojection field | `r.TSR.AsyncCompute` |
| 1 | **MeasureFlickeringLuma** | Record current frame luminance for flickering detection | `r.TSR.ShadingRejection.Flickering` |
| 2 | **MeasureThinGeometryCoverage** | Detect sub-pixel geometry edges | `r.TSR.ThinGeometryDetection` |
| 3 | **DilateVelocity** | Dilate velocity vectors to cover sub-pixel movement | `r.TSR.ReprojectionField` |
| 4 | **DecimateHistory** | Reproject and decimate previous frame's history | `r.TSR.History.ScreenPercentage` |
| 5 | **RejectShading** | Neural convolution network to detect history rejection masks | `r.TSR.ShadingRejection.*` |
| 6 | **DetectThinGeometry** | Relax history rejection on thin geometry edges | `r.TSR.ThinGeometryDetection.*` |
| 7 | **WeightRelaxation** | Further relax rejection on sky and foliage | `r.TSR.ShadingRejection.Flickering` |
| 8 | **SpatialAntiAliasing** | Blur rejected pixels to reduce aliasing | `r.TSR.RejectionAntiAliasingQuality` |
| 9 | **UpdateHistory** | Blend current frame with reprojected history, write new history | `r.TSR.History.UpdateQuality`, `r.TSR.Velocity.WeightClamping*` |
| 10 | **ResolveHistory** | Downscale 200% history to output resolution (optional) | `r.TSR.History.ScreenPercentage` |

---

## 1. ITemporalUpscaler Interface

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Public\TemporalUpscaler.h`

### Class Hierarchy

```cpp
// Line 11-84
class ITemporalUpscaler : public ISceneViewFamilyExtention
{
public:
    class IHistory : public IRefCountedObject
    {
        virtual const TCHAR* GetDebugName() const = 0;
        virtual uint64 GetGPUSizeBytes() const = 0;
    };

    struct FInputs
    {
        FIntRect OutputViewRect;
        FVector2f TemporalJitterPixels;
        float PreExposure = 1.0f;
        FScreenPassTexture SceneColor;
        FScreenPassTexture SceneDepth;
        FScreenPassTexture SceneVelocity;
        FRDGTextureRef EyeAdaptationTexture = nullptr;
        TRefCountPtr<IHistory> PrevHistory;
    };

    struct FOutputs
    {
        FScreenPassTexture FullRes;
        TRefCountPtr<IHistory> NewHistory;
    };

    virtual const TCHAR* GetDebugName() const = 0;
    virtual FOutputs AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FInputs& Inputs) const = 0;
    virtual float GetMinUpsampleResolutionFraction() const = 0;
    virtual float GetMaxUpsampleResolutionFraction() const = 0;
    virtual ITemporalUpscaler* Fork_GameThread(const class FSceneViewFamily& ViewFamily) const = 0;
};
```

**Key Points**:
- TSR is one implementation of `ITemporalUpscaler` (third-party plugins can implement other upscalers)
- `FInputs` contains: scene color, depth, velocity, eye adaptation, temporal jitter, previous history
- `FOutputs` returns: upscaled scene color + new history to persist to next frame
- History is **ref-counted opaque object** to allow multiple upscaler implementations
- Pre-exposure is external (passed in, not part of TSR)

---

## 2. TSR Compute Shader Classes

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\TemporalSuperResolution.cpp`

Lines 682–1400 declare 11 compute shader classes:

### 2.1 FTSRMeasureFlickeringLumaCS

**Lines**: 682–692
**Shader File**: `TSRMeasureFlickeringLuma.usf`
**Purpose**: Record per-pixel luminance for temporal flickering detection

```cpp
class FTSRMeasureFlickeringLumaCS : public FTSRShader
{
    DECLARE_GLOBAL_SHADER(FTSRMeasureFlickeringLumaCS);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, InputInfo)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, FlickeringLumaOutput)
    END_SHADER_PARAMETER_STRUCT()
};
```

**Output**: `FlickeringLumaOutput` → RG8 texture array (red = current luma, alpha slot reserved for thin geometry coverage)

### 2.2 FTSRDilateVelocityCS

**Lines**: 730–786
**Shader File**: `TSRDilateVelocity.usf`
**Purpose**: Dilate velocity vectors spatially to cover sub-pixel geometry edges

**Permutation Dimensions** (line 735–737):
- `FMotionBlurDirectionsDim`: 0, 1, 2, 3 motion blur direction modes
- `FThinGeometryEdgeReprojectionDim`: bool

```cpp
SHADER_PARAMETER(FMatrix44f, RotationalClipToPrevClip)
SHADER_PARAMETER(FVector2f, PrevOutputBufferUVMin)
SHADER_PARAMETER(FVector2f, PrevOutputBufferUVMax)
SHADER_PARAMETER(float, InvFlickeringMaxParralaxVelocity)  // CVar: r.TSR.ShadingRejection.Flickering.MaxParallaxVelocity
SHADER_PARAMETER(float, ReprojectionFieldAntiAliasVelocityThreshold)  // CVar: r.TSR.ReprojectionField.AntiAliasPixelSpeed
SHADER_PARAMETER(int32, bReprojectionField)
SHADER_PARAMETER(int32, bOutputIsMovingTexture)
SHADER_PARAMETER(int32, ReprojectionVectorOutputIndex)
```

**Key Outputs**:
- `ClosestDepthOutput`: Closest depth in 3x3 neighborhood
- `ReprojectionFieldOutput`: Jacobian 2x2 matrix encoding geometry-induced distortion
- `ReprojectionBoundary`: Spatial anti-aliasing mask for reprojection field dilation

**Algorithm Sketch** (lines 58–143, `ComputeReprojectionBoundary` function):
1. Fetch 3×3 depth neighborhood using butterfly sampling pattern
2. Compare velocity at center vs dilated velocity
3. If velocity difference > `ReprojectionFieldAntiAliasVelocityThreshold`:
   - Browse depth discontinuities using edge-following algorithm (3 iterations)
   - Compute boundary offset (half-pixel anti-aliasing)
4. Otherwise: fully dilate boundary (no edge detection)

### 2.3 FTSRDecimateHistoryCS

**Lines**: 788–859
**Shader File**: `TSRDecimateHistory.usf`
**Purpose**: Reproject previous frame's history to current frame using dilated reprojection vectors

**Permutation Dimensions** (line 793–796):
- `FMoireReprojectionDim`: bool (moire/flickering luma reprojection)
- `FResurrectionReprojectionDim`: bool (multi-frame history resurrection)
- `FThinGeometryCoverageDim`: bool (thin geometry coverage factor)

**Key Parameters**:
```cpp
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DilatedReprojectionVectorTexture)
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ClosestDepthTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DilateMaskTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthErrorTexture)
SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, PrevAtomicTextureArray)
SHADER_PARAMETER(FMatrix44f, RotationalClipToPrevClip)
```

### 2.4 FTSRRejectShadingCS

**Lines**: 861–977
**Shader File**: `TSRRejectShading.usf`
**Purpose**: Neural convolution network to detect pixels where history should be rejected

**Permutation Dimensions** (line 870–874):
- Base: `FTSRConvolutionNetworkShader::FPermutationDomain` (wave size, 16-bit VALU, alpha channel)
- `FFlickeringDetectionDim`: bool
- `FHistoryResurrectionDim`: bool
- `FThinGeometryDetectionDim`: bool

**Key Logic** (lines 918–938):
```cpp
// Register pressure is identical between all permutations with 16bit
if (PermutationVector.Get<FTSRConvolutionNetworkShader::FPermutationDomain>().Get<FTSRShader::F16BitVALUDim>())
{
    PermutationVector.Set<FFlickeringDetectionDim>(true);
    PermutationVector.Set<FHistoryResurrectionDim>(true);
}

// Flickering detection implies resurrection (line 931–935)
if (PermutationVector.Get<FFlickeringDetectionDim>())
{
    PermutationVector.Set<FHistoryResurrectionDim>(true);
}
```

**Shader Inputs**:
```cpp
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputMoireLumaTexture)  // Flickering.Luminance
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneTranslucencyTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ReprojectedHistoryGuideTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ReprojectedHistoryGuideMetadataTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, IsMovingMaskTexture)  // From DecimateHistory
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ThinGeometryTexture)  // From DetectThinGeometry
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ClosestDepthTexture)
SHADER_PARAMETER(float, TheoricBlendFactor)
SHADER_PARAMETER(float, FlickeringFramePeriod)  // Computed from r.TSR.ShadingRejection.Flickering.Period
SHADER_PARAMETER(int32, bEnableResurrection)
SHADER_PARAMETER(int32, bEnableFlickeringHeuristic)
```

**Outputs**:
- `HistoryGuideOutput`: De-ghosted history color
- `HistoryRejectionOutput`: Float4 mask (channels: rejection, clamping, resurrection, flickering detection active)
- `AntiAliasMaskOutput`: 1 = apply spatial anti-aliasing

### 2.5 FTSRSpatialAntiAliasingCS

**Lines**: 1118–1153
**Shader File**: `TSRSpatialAntiAliasing.usf`
**Purpose**: Apply spatial anti-aliasing filter to pixels where history was rejected

**Permutation Dimensions** (line 1123):
- `FQualityDim`: 0, 1, 2 (0=disabled, 1=3 iterations, 2=8 iterations)

**Quality Settings** (lines 10–18 of .usf):
```hlsl
#if DIM_QUALITY_PRESET == 1
    #define CONFIG_ITERATIONS 3
#elif DIM_QUALITY_PRESET == 2
    #define CONFIG_ITERATIONS 8
#endif
```

**Algorithm** (lines 56–210 of .usf):
1. Read 3×3 neighborhood of LDR luminance
2. Find edge direction via `FindBrowsingDirection()`
3. Browse edge perpendicular to edge direction (CONFIG_ITERATIONS = 3 or 8 iterations)
4. Compute distance to edge boundary
5. Offset texture sampling accordingly to blur across boundary

**Key Parameter** (line 163 of .usf):
```hlsl
bool2 WorthBrowsing = LumaDelta > SPATIAL_ANTI_ALIASER_MIN_LUMIMANCE;
tsr_half2 BrowseDelta = saturate(LumaDelta * rcp(tsr_half(SPATIAL_ANTI_ALIASER_MIN_LUMIMANCE)));
```

Threshold is defined in included header `TSRSpatialAntiAliasing.ush`.

### 2.6 FTSRUpdateHistoryCS

**Lines**: 1155–1243
**Shader File**: `TSRUpdateHistory.usf`
**Purpose**: Blend current frame with reprojected history, write new history

**Quality Levels** (line 1160–1166):
```cpp
enum class EQuality
{
    Low,       // 0
    Medium,    // 1
    High,      // 2
    Epic,      // 3
    MAX
};
```

Controlled by CVar: `r.TSR.History.UpdateQuality` (line 72–76 of .cpp)

**Permutation Dimensions** (line 1171):
- `FQualityDim`: EQuality (Low, Medium, High, Epic)
- `F16BitVALUDim`: bool
- `FAlphaChannelDim`: bool

**Key Parameters** (lines 1173–1203):
```cpp
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryRejectionTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ReprojectionBoundaryTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ReprojectionJacobianTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ReprojectionVectorTexture)
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AntiAliasingTexture)
SHADER_PARAMETER(float, HistorySampleCount)        // CVar: r.TSR.History.SampleCount (8–32)
SHADER_PARAMETER(float, HistoryHisteresis)         // 1.0 - blend factor
SHADER_PARAMETER(float, WeightClampingRejection)   // CVar: r.TSR.Velocity.WeightClampingSampleCount
SHADER_PARAMETER(float, WeightClampingPixelSpeedAmplitude)
SHADER_PARAMETER(float, InvWeightClampingPixelSpeed)  // CVar: r.TSR.Velocity.WeightClampingPixelSpeed
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray, PrevHistoryColorTexture)
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray, PrevHistoryMetadataTexture)
SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, HistoryColorOutput)
SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, HistoryMetadataOutput)
```

### 2.7 FTSRResolveHistoryCS

**Lines**: 1245–1337
**Shader File**: `TSRResolveHistory.usf`
**Purpose**: Downscale 200% history to output resolution (when `r.TSR.History.ScreenPercentage=200`)

**Permutation Dimensions** (line 1250):
- `FNyquistDim`: 0, 16, 32 (wave size for optimal Nyquist downsampling)

---

## 3. Key CVars (Console Variables)

All CVars are in namespace anonymous in TemporalSuperResolution.cpp, lines 24–371.

### History Configuration

| CVar | Default | Range | Purpose |
|------|---------|-------|---------|
| `r.TSR.History.ScreenPercentage` | 100.0 | 100–200 | Resolution multiplier of history buffer. 200 = Nyquist sampling by Shannon theorem |
| `r.TSR.History.SampleCount` | 16.0 | 8–32 | Max accumulated samples per pixel. Higher = more stable, but ghosting on transients |
| `r.TSR.History.UpdateQuality` | 3 | 0–3 | Permutation quality (Low, Medium, High, Epic) |
| `r.TSR.History.R11G11B10` | 1 | bool | Store history in R11G11B10 (16 bpp) vs RGBA32 (saves bandwidth) |

**Source**: Lines 43–70

### Shading Rejection Configuration

| CVar | Default | Purpose |
|------|---------|---------|
| `r.TSR.ShadingRejection.SampleCount` | 2.0 | Max samples in history AFTER rejection. Lower = clearer but less stable |
| `r.TSR.ShadingRejection.Flickering` | 1 | Enable flickering detection heuristic |
| `r.TSR.ShadingRejection.Flickering.Period` | 2.0 | Frame period at which luma oscillation is considered flickering |
| `r.TSR.ShadingRejection.Flickering.FrameRateCap` | 60 | Frame rate cap for flickering.Period adjustment |
| `r.TSR.ShadingRejection.Flickering.AdjustToFrameRate` | 1 | Auto-adjust period when FPS < FrameRateCap |
| `r.TSR.ShadingRejection.Flickering.MaxParallaxVelocity` | 10.0 | Max parallax velocity (1080p pixels) before disabling flickering check |
| `r.TSR.ShadingRejection.TileOverscan` | 3 | Overlap padding for convolution network tiling (3–5 typical) |
| `r.TSR.RejectionAntiAliasingQuality` | 3 | 0–3: spatial anti-aliasing quality when history rejected |

**Source**: Lines 121–220

### Velocity & Weight Clamping

| CVar | Default | Purpose |
|------|---------|---------|
| `r.TSR.Velocity.WeightClampingSampleCount` | 4.0 | Clamp to this many history samples if velocity > threshold |
| `r.TSR.Velocity.WeightClampingPixelSpeed` | 1.0 | Pixel speed threshold (output space) for weight clamping |

**Source**: Lines 285–305

**Semantics** (line 1191–1195 of .cpp):
```cpp
float WeightClampingRejection = CVarTSRWeightClampingSampleCount.GetValueOnRenderThread();
float WeightClampingPixelSpeedAmplitude = CVarTSRWeightClampingPixelSpeed.GetValueOnRenderThread();
float InvWeightClampingPixelSpeed = CVarTSRWeightClampingPixelSpeed.GetValueOnRenderThread() != 0.0f
    ? rcp(CVarTSRWeightClampingPixelSpeed.GetValueOnRenderThread()) : 0.0f;
```

### Reprojection Field

| CVar | Default | Purpose |
|------|---------|---------|
| `r.TSR.ReprojectionField` | 0 | Enable jacobian 2×2 matrix for precise reprojection on geometric edges |
| `r.TSR.ReprojectionField.AntiAliasPixelSpeed` | 0.125 | Output pixel velocity above which reprojection field dilation is anti-aliased |

**Source**: Lines 265–283

### Thin Geometry Detection

| CVar | Default | Purpose |
|------|---------|---------|
| `r.TSR.ThinGeometryDetection` | 0 | Enable sub-pixel geometry detection |
| `r.TSR.ThinGeometryDetection.Coverage.Hair` | true | Include hair as thin geometry |
| `r.TSR.ThinGeometryDetection.Coverage.MaxRelaxationWeight` | 0.037 | Relaxation factor for thin geometry rejection |
| `r.TSR.ThinGeometryDetection.ErrorMultiplier` | 200.0 | Depth error threshold for thin geometry detection |
| `r.TSR.ThinGeometryDetection.WeightRelaxation` | 1 | Apply adaptive relaxation to avoid ghosting |

**Source**: Lines 307–345

### Advanced Features

| CVar | Default | Purpose |
|------|---------|---------|
| `r.TSR.Resurrection` | 0 | Enable multi-frame history resurrection (experimental) |
| `r.TSR.Resurrection.PersistentFrameCount` | 2 | Number of persistent frames stored |
| `r.TSR.Resurrection.PersistentFrameInterval` | 31 | Record persistent frame every N frames |
| `r.TSR.AsyncCompute` | 2 | 0=disabled, 1=independent only, 2=independent+depth-dependent, 3=all |
| `r.TSR.WaveOps` | 1 | Use wave operations for convolution optimization |
| `r.TSR.16BitVALU` | 1 | Use 16-bit floating-point math (platform-dependent) |
| `r.TSR.LensDistortion` | 1 | Apply lens distortion correction in history update |

**Source**: Lines 222–263, 97–119

---

## 4. Velocity Dilation Algorithm

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRDilateVelocity.usf`

### Dilation Strategy

The dilation happens in two scenarios:

#### A. Reprojection Field Disabled (default)

When `bReprojectionField = 0`:
1. Fetch closest depth in 3×3 neighborhood
2. Lookup velocity at closest-depth pixel
3. Use that velocity for all pixels in neighborhood (full dilation)

#### B. Reprojection Field Enabled

When `bReprojectionField = 1` (lines 175–183 of .usf):
1. Fetch 3×3 depth + velocity neighborhoods using butterfly sampling
2. Convert velocities to pixel space
3. Compute pixel velocity at center vs. dilation velocity
4. If velocity difference > `ReprojectionFieldAntiAliasVelocityThreshold`:
   - **Browse depth edges** (line 115–128):
     - Use `FindBrowsingDirection()` to identify dominant edge
     - Call `BrowseNeighborhoodBilinearOptimized()` with 3 iterations
     - Compute boundary offsets (half-pixel anti-aliasing)
   - Store boundary as `tsr_half2` encoding
5. Otherwise: set boundary to `kFullDilateBoundary`

### Search Pattern

**File Includes**: `TSRClosestOccluder.ush`, `TSRDepthVelocityAnalysis.ush`

The dilation uses **butterfly/z-order curve sampling**:
- Center + N/S/E/W + NE/NW/SE/SW = 9 samples per pixel
- Fastest depth becomes the reference for velocity lookup

---

## 5. History Rejection & Shading Rejection Pass

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRRejectShading.usf`

### Rejection Pipeline

The `RejectShading` pass runs a **neural convolution network** to identify:
1. **Disocclusion**: Newly visible geometry (reprojection missed it)
2. **Shading changes**: Same geometry but different lighting
3. **Translucency**: Semi-transparent objects moving
4. **Flickering**: High-frequency temporal luma variation

### Flickering Detection Logic

**Input**: `InputMoireLumaTexture` (recorded in `MeasureFlickeringLuma` pass)

**Algorithm**:
1. Compare current frame luminance vs. previous frame luminance
2. Track if luminance oscillates > `FlickeringFramePeriod` frames apart
3. If oscillation > threshold: mark pixel for rejection (allow ghosting to stabilize)
4. **Frame rate adjustment** (line 1793–1794 of .cpp):
   ```cpp
   const float RefreshRateToFrameRateCap = (View.Family->Time.GetDeltaRealTimeSeconds() > 0.0f && PassConfig.ShadingRejectionFlickeringAdjustToFrameRate)
       ? View.Family->Time.GetDeltaRealTimeSeconds() * PassConfig.ShadingRejectionFlickeringFrameRateCap : 1.0f;
   ```
   - If frame time > (1 / FrameRateCap), scale period accordingly to avoid false positives at low FPS

### Neural Convolution Network

- **LDS tiling**: 32×32 work group with edge padding (overscan = 3–5 pixels)
- **Wave optimizations**: Optional wave operations on RDNA/RDNA2 (CVarTSRWaveOps)
- **Input**: Current frame + reprojected history + metadata (sample count)
- **Output**: Rejection mask (float4 with per-channel semantics)

---

## 6. Spatial Anti-Aliasing for Rejected Pixels

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRSpatialAntiAliasing.usf`

### Purpose

When history is rejected, the current low-resolution frame is presented alone. This causes aliasing. The spatial anti-aliaser blurs edges in the current frame to reduce aliasing artifacts.

### Algorithm (lines 136–210 of .usf)

1. **Fetch 3×3 neighborhood** of LDR luminance
2. **Find edge direction**: Call `FindBrowsingDirection()` which returns:
   - Dominant edge direction (4-cardinal or 4-diagonal)
   - Edge luminance value
3. **Browse perpendicular to edge**: Trace in N/S/E/W directions for CONFIG_ITERATIONS (3 or 8):
   - Line 170–183: Call `BrowseNeighborhoodBilinearOptimized()`
   - Return edge lengths in +/- directions
4. **Compute offset**:
   ```hlsl
   TexelOffset = ComputeDistanceToEdge(...)  // Returns [-0.5, 0.5] texel offset
   ```
5. **Apply noise filtering**: Scale offset by `BrowseDelta` (edge strength)

### Quality Tiers

- **Quality 0**: Disabled (no pass runs)
- **Quality 1**: 3 browse iterations → fast, visible aliasing on jagged edges
- **Quality 2**: 8 browse iterations → slower, smoother edges

---

## 7. Blend Factor & Responsive Accumulation

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\TemporalSuperResolution.cpp` (lines 1762–1900+)

### Blend Factor Computation

The `UpdateHistory` pass computes per-pixel alpha blend factor:

```cpp
// Line 1189–1195
SHADER_PARAMETER(float, HistorySampleCount)        // Max samples (8–32)
SHADER_PARAMETER(float, HistoryHisteresis)         // 1.0 - alpha
SHADER_PARAMETER(float, WeightClampingRejection)   // Clamp when high velocity
SHADER_PARAMETER(float, InvWeightClampingPixelSpeed)
```

**Pseudo-algorithm in UpdateHistory.usf**:
```hlsl
// Base histeresis (line ~1189)
float HistoryAlpha = HistoryHisteresis;  // Typically 0.9 (samples=16)

// Weight clamping based on velocity (adapted from lines 1191–1195)
float VelocityMagnitude = length(ReprojectionVelocity);  // Output space pixels
if (VelocityMagnitude > r.TSR.Velocity.WeightClampingPixelSpeed)
{
    // Clamp history contribution: reduce alpha when moving fast
    float ClampFactor = min(VelocityMagnitude * InvWeightClampingPixelSpeed, 1.0);
    HistoryAlpha = lerp(HistoryHisteresis, 1.0 / WeightClampingRejection, ClampFactor);
}

// Final blend
float4 Result = lerp(ReprojectedHistory, CurrentFrame, HistoryAlpha);
```

### Inverse Relationship: Sample Count

Higher `HistorySampleCount` → lower `HistoryHisteresis`:
- 16 samples: alpha ≈ 1/16 = 0.0625 (aggressive)
- 32 samples: alpha ≈ 1/32 = 0.03125 (very conservative)

**Source** (line 1797):
```cpp
const float MaxHistorySampleCount = FMath::Clamp(PassConfig.HistorySampleCount, 8.0f, 32.0f);
```

Actual alpha computation is in UpdateHistory.usf and depends on quality tier (Low/Medium/High/Epic).

---

## 8. Thin Geometry Detection

**Files**:
- C++: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\TemporalSuperResolution.cpp` (lines 307–345)
- Shader: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRDetectThinGeometry.usf`

### Purpose

Detect sub-pixel geometry (wires, foliage, hair) that renders inconsistently across frames due to sampling differences.

### Detection Method

**Pass Order**:
1. `MeasureThinGeometryCoverage` (after DilateVelocity)
   - Uses depth coverage to flag potential thin geometry
2. `DetectThinGeometry` (after RejectShading)
   - Analyzes depth discontinuities
   - Returns classification: edge line, cluster hole, other
3. `WeightRelaxation` (optional)
   - Further relaxes rejection on foliage/sky

### Parameters

- `r.TSR.ThinGeometryDetection.ErrorMultiplier` = 200.0 (line 342)
  - Depth difference multiplier to classify thin geometry
  - Higher = more strict (larger depth difference required)

- `r.TSR.ThinGeometryDetection.Coverage.MaxRelaxationWeight` = 0.037 (line 327)
  - Relaxation factor: how much to relax rejection on thin edges
  - Range: 0–1

---

## 9. Multi-Frame History Resurrection (Experimental)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\TemporalSuperResolution.cpp` (lines 222–254)

### Concept

When enabled, TSR stores multiple historical frames (not just 1) and resurrects previously discarded details from older frames.

### Configuration

- `r.TSR.Resurrection` = 0 (disabled by default, experimental)
- `r.TSR.Resurrection.PersistentFrameCount` = 2 (even, 2–GMaxTextureArrayLayers)
- `r.TSR.Resurrection.PersistentFrameInterval` = 31 (odd, record every N frames)

### History Slice Sequence

**Struct**: `FTSRHistorySliceSequence` (lines 1441–1549 of .cpp)

```cpp
int32 FrameStorageCount = 1;      // Number of frames in Texture2DArray
int32 FrameStoragePeriod = 1;     // Recording interval
int32 GetRollingIndexCount();     // Total rolling indices
int32 RollingIndexToSliceIndex(); // Map frame index to array slice
```

**Memory Layout**:
- Transient slices: 2 (current + previous, always recorded)
- Persistent slices: FrameStorageCount - 2 (recorded every FrameStoragePeriod frames)

---

## 10. History Formats & Encoding

**File**: Lines 420–434 of .cpp

```cpp
enum class ETSRHistoryFormatBits : uint32
{
    None = 0,
    Moire = 1 << 0,        // Flickering luma (if r.TSR.ShadingRejection.Flickering=1)
    AlphaChannel = 1 << 1, // Scene color alpha (if r.PostProcessing.PropagateAlpha=1)
};
```

### History Texture Array Layout

History is stored as `Texture2DArray`:
- **Slice 0**: High-frequency color (16–32 bpp)
- **Slice 1**: Metadata (sample count, uncertainty, moire data)
- **Optional Slice 2**: Moire/flickering luminance
- **Optional Slice 3+**: Resurrectable historical frames

### Quantization Error

**Line 1888 of .cpp**:
```cpp
SHADER_PARAMETER(FVector3f, HistoryQuantizationError)
```

Encodes per-channel precision loss due to reduced bit depth (R11G11B10 vs RGBA32).

---

## 11. Async Compute Strategy

**File**: Lines 256–263 of .cpp

```cpp
r.TSR.AsyncCompute (default: 2)
    0: Disabled
    1: ClearPrevTextures + ForwardScatterDepth only (independent)
    2: + DecimateHistory + RejectShading (can overlap with GFX) [DEFAULT]
    3: All passes on async compute
```

**Rationale**:
- Passes 0–1 have zero dependencies on graphics queue (can run fully async)
- Passes 3–4 depend on depth/velocity (can overlap with translucency/DOF on graphics queue)
- Passes 5+ depend on earlier TSR passes (stay on graphics queue to avoid sync points)

---

## 12. Key Numerical Constants & Formulas

### Nyquist Sampling (History ScreenPercentage = 200)

**Citation**: Lines 52–59 of .cpp

```cpp
// "Setting to 200 brings on a very particular property relying on NyQuist-Shannon sampling theorem
// that establishes a sufficient condition for the sample rate of the accumulated details in the
// history. As a result only values between 100 and 200 are supported."
```

At 200% resolution history:
- History accumulates 2× the detail bandwidth of output
- Downsampling to 100% output preserves high frequencies (per Nyquist)

### Sample Count Range

**Citation**: Lines 43–50 of .cpp
- Minimum: 8.0 (legacy 5.0/5.1)
- Maximum: 32.0 (encoding limit in metadata)
- Default: 16.0

### Flickering Period

**Citation**: Lines 180–184 of .cpp
```
Period = 2.0 frames (default)
FrameRateCap = 60 Hz (default)
AdjustToFrameRate = 1 (enabled, line 175)
```

At 30 FPS (2× frame time of 60 FPS):
```
AdjustedPeriod = Period * (1 / (FrameTime * FrameRateCap))
               = 2.0 * (1 / (0.0333 * 60))
               ≈ 4.0 frames
```

---

## Architecture Adaptation Recommendations for UzEngine

### 1. **Decouple History from Upsampler**

UE5's `ITemporalUpscaler::IHistory` is opaque. For UzEngine:
- Consider `struct TSRHistory { Texture2DArray colors; Texture2DArray metadata; ... }`
- Store in persistent memory between frames (not RDG builder)

### 2. **Replicate Convolution Network Tiling**

The `RejectShading` pass uses 32×32 tiles with 3–5 pixel overscan:
- LDS (shared memory) for kernel evaluation
- Wave operations for convolution acceleration
- Apply this pattern to all spatial filtering passes

### 3. **Implement Velocity Dilation with Butterfly Sampling**

- Fetch 3×3 neighborhood using Z-order curve (faster cache coherence)
- Implement `FindBrowsingDirection()` to identify dominant depth edge
- Implement `BrowseNeighborhoodBilinearOptimized()` for edge tracing

### 4. **Use Jacobian Matrices for Geometric Reprojection**

- Compute 2×2 jacobian in `DilateVelocity` pass
- Store as `uint` per pixel (4×4-bit or 5×5-bit precision per component)
- Apply in `UpdateHistory` to scale history samples when object zoom/scale detected

### 5. **Implement Flickering Detection as Frame Analyzer**

- Record per-pixel luminance every frame (minimal memory: 1–2 bpp)
- Compare against previous frame's luminance
- Flag pixels where luma oscillates faster than `FlickeringFramePeriod`

### 6. **Use Neural Convolution for Rejection** (Optional)

- Complexity: High (requires DXC compilation of massive unrolled loops)
- Simpler alternative: Implement hand-coded rejection heuristics:
  - Depth difference > threshold → disocclusion
  - Luma difference > threshold → shading change
  - Metadata mismatch → material change

### 7. **Separate Quality Tiers**

Following UE5's pattern:
- **Low**: Disabled spatial anti-aliasing, basic rejection, 1–2 iterations
- **Medium**: 3-iteration spatial anti-aliasing, mid-tier rejection
- **High**: 8-iteration spatial anti-aliasing, full thin geometry detection
- **Epic**: + reprojection field, + resurrection

### 8. **Use 16-bit Float Conditionally**

- Check GPU capability (AMD RDNA/RDNA2, Intel Arc, some Nvidia)
- Condition compilation via permutation dimensions
- Default to 32-bit for safety

### 9. **Metadata Encoding Strategy**

Store in single `uint32` per pixel:
- Bits 0–4: Sample count (5 bits = 32 values)
- Bits 5–7: Uncertainty (3 bits = confidence level)
- Bits 8–12: Moire/flickering indicator (5 bits = frame oscillation phase)
- Bits 13–15: Thin geometry classification (3 bits = edge/hole/other)
- Bits 16–31: Reserved

### 10. **CVar Parity**

Mirror UE5's CVar structure:
- `uz_tsr.history.screen_percentage` [100, 200]
- `uz_tsr.history.sample_count` [8, 32]
- `uz_tsr.velocity.weight_clamping_*`
- `uz_tsr.flickering.*`
- `uz_tsr.rejection.antialiasing_quality`

---

## Critical Implementation Differences from Other Upscalers

### TSR vs. DLSS / XeSS / FSR

| Aspect | TSR | DLSS | FSR | Notes |
|--------|-----|------|-----|-------|
| **History** | 1–N frames | Implicit (inside DNN) | 1 frame | TSR is explicit frame storage |
| **Reprojection** | Velocity-based | Optical flow inside | Velocity | UE5 dilatates for geometric edges |
| **Rejection** | Neural convolution | DNN implicit | Edge-based | TSR is hybrid: heuristic + convolution |
| **Flickering** | Temporal luma tracking | DNN implicit | Not explicit | UE5 explicitly detects per-frame |
| **Thin Geometry** | Dedicated pass | DNN implicit | Not explicit | Unique to TSR |

---

## Summary

UE5's TSR is a **multi-pass temporal upsampler** designed for **high temporal stability** on complex scenes with:
1. **Explicit velocity dilation** to handle geometric movement
2. **Neural rejection network** to detect history invalidity
3. **Spatial anti-aliasing** when history is rejected
4. **Responsive accumulation** that clamps temporal weights based on pixel velocity
5. **Flickering detection** to prevent erratic temporal changes
6. **Optional reprojection field** for precise geometric reprojection

Key architectural patterns for UzEngine:
- **Async compute-friendly design** (11 independent compute passes)
- **LDS tiling with overscan** for convolution kernels
- **Permutation-driven quality tiers** (Low/Medium/High/Epic)
- **Opaque history interface** allowing future upscaler alternatives
- **CVar-driven tunability** for per-content optimization

