<!-- doc-meta version="1" -->

# UE5 TSR: Responsive Temporal Accumulation & Blend Factors

**Date**: 2026-03-18
**UE5 Version**: 5.6
**Focus**: Pixel-level temporal blending strategy

## Overview

This document details how UE5's TSR computes **per-pixel temporal blend factors** to achieve responsive, stable temporal accumulation. The core challenge: balance **temporal stability** (smooth appearance) against **responsiveness** (minimal ghosting when scene changes).

---

## Architecture: Three-Layer Blend Factor System

### Layer 1: Base Histeresis (History Contribution)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\TemporalSuperResolution.cpp` (lines 1189–1195)

**Parameter**: `HistorySampleCount` (CVar: `r.TSR.History.SampleCount`, default 16.0)

**Formula**:
```
HistoryAlpha = 1.0 / HistorySampleCount
```

| HistorySampleCount | HistoryAlpha | Meaning |
|-------------------|--------------|---------|
| 8 | 0.125 (1/8) | 12.5% new frame, 87.5% history (aggressive blend) |
| 16 | 0.0625 (1/16) | 6.25% new frame, 93.75% history (default, stable) |
| 32 | 0.03125 (1/32) | 3.125% new frame, 96.875% history (very conservative, slow convergence) |

**Rationale**: Higher sample count = more accumulated history = less per-frame contribution = smoother temporal filtering.

**Reference** (line 1797):
```cpp
const float MaxHistorySampleCount = FMath::Clamp(PassConfig.HistorySampleCount, 8.0f, 32.0f);
```

### Layer 2: Weight Clamping (Velocity-Responsive)

**File**: Same file (lines 285–305)

**Parameters**:
- `r.TSR.Velocity.WeightClampingPixelSpeed` (default 1.0, in output-space pixels)
- `r.TSR.Velocity.WeightClampingSampleCount` (default 4.0, in samples)

**Algorithm** (pseudo-code from UpdateHistory.usf):
```hlsl
float VelocityMagnitude = length(OutputPixelVelocity);  // pixels

if (VelocityMagnitude > WeightClampingPixelSpeed)
{
    // Clamp history contribution when pixel moves fast
    float ClampAlpha = 1.0 / WeightClampingSampleCount;  // 1/4 = 0.25
    float LerpFactor = saturate((VelocityMagnitude - WeightClampingPixelSpeed) / WeightClampingPixelSpeed);
    HistoryAlpha = lerp(HistoryAlpha, ClampAlpha, LerpFactor);
}
```

**Behavior**:
- **VelocityMagnitude < 1.0**: Use base alpha (0.0625)
- **VelocityMagnitude = 1.0**: Transition begins
- **VelocityMagnitude = 2.0+**: Clamp to 0.25 (1/4 samples)

**Interpretation**:
- Stationary pixels: accumulate history over 16 frames
- Fast-moving pixels: only accumulate over 4 frames (4× faster response, less ghosting)

**Source** (lines 300–305 of .cpp):
```cpp
TAutoConsoleVariable<float> CVarTSRWeightClampingPixelSpeed(
    TEXT("r.TSR.Velocity.WeightClampingPixelSpeed"), 1.0f,
    TEXT("Defines the output pixel velocity at which the the high frequencies of the history get's their contributing weight clamped. ")
    TEXT("It's basically to lerp the effect of r.TSR.Velocity.WeightClampingSampleCount when the pixel velocity get smaller than r.TSR.Velocity.WeightClampingPixelSpeed. ")
    TEXT("(Default = 1.0f)."),
    ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarTSRWeightClampingSampleCount(
    TEXT("r.TSR.Velocity.WeightClampingSampleCount"), 4.0f,
    TEXT("Number of sample to count to in history pixel to clamp history to when output pixel velocity reach r.TSR.Velocity.WeightClampingPixelSpeed. ")
    TEXT("Higher value means higher stability on movement, but at the expense of additional blur due to successive convolution of each history reprojection.\n")
    TEXT("\n")
    TEXT("Use \"r.TSR.Visualize 0\" command to see how many samples where accumulated in TSR history on areas of the screen.\n")
    TEXT("\n")
    TEXT("Please note this clamp the sample count in history pixel, not output pixel, and therefore lower values are by designed less ")
    TEXT("noticeable with higher r.TSR.History.ScreenPercentage. This is done so such that increasing r.TSR.History.ScreenPercentage uniterally & automatically ")
    TEXT("give more temporal stability and maintaining sharpness of the details reprojection at the expense of that extra runtime cost regardless of this setting.\n")
    TEXT("\n")
    TEXT("A story telling game might preferer to keep this 4.0 for a 'cinematic look' whereas a competitive game like Fortnite would preferer to lower that to 2.0. ")
    TEXT("(Default = 4.0f)."),
    ECVF_RenderThreadSafe);
```

**Game Genre Tuning** (from comment above):
- **Cinematic/Story**: WeightClampingSampleCount = 4.0 (more stable, ghosting acceptable)
- **Competitive (Fortnite)**: WeightClampingSampleCount = 2.0 (faster response, less ghosting)

### Layer 3: History Rejection (Disocclusion & Shading Changes)

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRRejectShading.usf`

**Output**: `HistoryRejectionOutput` (float4 per pixel)

**Channels**:
- **Channel 0 (R)**: Binary rejection mask (0 = use history, 1 = reject history)
- **Channel 1 (G)**: Clamping factor (0–1, soft rejection)
- **Channel 2 (B)**: Resurrection valid (0 = invalid, 1 = valid)
- **Channel 3 (A)**: Flickering detection active (0–1)

**Rejection Triggers**:
1. **Depth discontinuity** (disocclusion):
   - Reprojected pixel has different depth than current
   - Likely newly visible geometry

2. **Luma change**:
   - Scene color changed despite stable motion
   - Lighting change, material change, translucency

3. **Flickering** (if `r.TSR.ShadingRejection.Flickering=1`):
   - Current luma oscillates with period < `FlickeringFramePeriod`
   - Indicates sub-pixel sampling variance or material instability

4. **Thin geometry** (if `r.TSR.ThinGeometryDetection=1`):
   - Depth error high in neighborhood
   - Foliage, wires, hair → relax rejection slightly

**When Rejected**:
- Force alpha = 1.0 (100% new frame, 0% history)
- Apply spatial anti-aliasing (see next section)

---

## Blend Factor Computation: Detailed Walkthrough

### Input Data

```cpp
// From UpdateHistory shader parameters
Texture2D<float> HistoryRejectionTexture;        // Rejection mask
Texture2D<uint> ReprojectionVectorTexture;       // Dilated velocity
Texture2D<uint> ReprojectionBoundaryTexture;     // Anti-aliasing boundary (if bReprojectionField)
Texture2D<uint> ReprojectionJacobianTexture;     // Jacobian matrix (if bReprojectionField)
Texture2DArray<float4> PrevHistoryColorTexture;   // Reprojected history
Texture2D<float4> AntiAliasingTexture;            // Spatial AA offset (if rejected)

float HistorySampleCount;                         // CVar: r.TSR.History.SampleCount
float InvWeightClampingPixelSpeed;                // 1.0 / r.TSR.Velocity.WeightClampingPixelSpeed
float WeightClampingRejection;                    // r.TSR.Velocity.WeightClampingSampleCount
```

### Pseudo-Code Algorithm

```hlsl
// Step 1: Decode rejection mask
float RejectionMask = HistoryRejectionTexture[PixelPos].r;
bool bHistoryRejected = RejectionMask > 0.5;

// Step 2: Decode velocity (encoded as uint2 or uint for compression)
uint EncodedVelocity = ReprojectionVectorTexture[PixelPos];
float2 ReprojectionVelocity = DecodeVelocity(EncodedVelocity);  // Output-space pixels

// Step 3: Compute base alpha
float BaseAlpha = 1.0 / HistorySampleCount;  // Default: 1/16 = 0.0625

// Step 4: Apply velocity clamping
float VelocityMagnitude = length(ReprojectionVelocity);
if (VelocityMagnitude > WeightClampingPixelSpeed)
{
    float ClampAlpha = 1.0 / WeightClampingRejection;  // 1/4 = 0.25
    float BlendFactor = saturate((VelocityMagnitude - WeightClampingPixelSpeed)
                                 * InvWeightClampingPixelSpeed);
    BaseAlpha = lerp(BaseAlpha, ClampAlpha, BlendFactor);
}
// Now BaseAlpha is in [0.0625, 0.25] depending on velocity

// Step 5: Check rejection mask
float FinalAlpha;
if (bHistoryRejected)
{
    // Disocclusion or shading change: use current frame only
    FinalAlpha = 1.0;

    // If spatial anti-aliasing is enabled, sample offset
    if (AntiAliasMask[PixelPos] > 0)
    {
        float2 AAOffset = DecodeAntiAliasingOffset(AntiAliasingTexture[PixelPos]);
        CurrentColor = BilinearSample(InputTexture, PixelPos + AAOffset);
    }
}
else
{
    // History valid: use clamped alpha
    FinalAlpha = BaseAlpha;
}

// Step 6: Blend
float4 Result = lerp(ReprojectedHistory, CurrentFrameColor, FinalAlpha);

// Step 7: Store new history with updated sample count
float SampleCountAfterBlend = (1.0 - FinalAlpha) * PrevSampleCount + FinalAlpha;
HistoryColorOutput[PixelPos] = Result;
HistoryMetadataOutput[PixelPos] = EncodeMetadata(SampleCountAfterBlend, ...);
```

---

## CVar Tuning Guide

### Profile: Cinematic / Single-Player

**Goal**: Maximum temporal stability, ghosting acceptable.

```ini
[/Script/Engine.Engine]
r.TSR.History.SampleCount=32.0
r.TSR.Velocity.WeightClampingSampleCount=4.0
r.TSR.Velocity.WeightClampingPixelSpeed=2.0
r.TSR.History.ScreenPercentage=200
r.TSR.ShadingRejection.SampleCount=2.0
r.TSR.RejectionAntiAliasingQuality=3
r.TSR.ShadingRejection.Flickering=1
r.TSR.ReprojectionField=1
```

**Effect**:
- Stationary: 32-frame temporal filter (0.03125 alpha)
- Fast moving: 4-frame temporal filter (0.25 alpha, only at 2+ pixels/frame)
- History screen %: 200% (Nyquist sampling, max quality)
- Flickering detection: aggressive (stabilizes micro-animation)

### Profile: Competitive / Fast-Paced

**Goal**: Minimal ghosting, responsive to player input.

```ini
[/Script/Engine.Engine]
r.TSR.History.SampleCount=8.0
r.TSR.Velocity.WeightClampingSampleCount=2.0
r.TSR.Velocity.WeightClampingPixelSpeed=0.5
r.TSR.History.ScreenPercentage=100
r.TSR.ShadingRejection.SampleCount=2.0
r.TSR.RejectionAntiAliasingQuality=1
r.TSR.ShadingRejection.Flickering=0
r.TSR.ReprojectionField=0
```

**Effect**:
- Stationary: 8-frame temporal filter (0.125 alpha)
- Fast moving: 2-frame temporal filter (0.5 alpha, at 0.5+ pixels/frame)
- History screen %: 100% (minimal memory, match render resolution)
- Flickering detection: disabled (reduce ghosting artifacts)
- Reprojection field: disabled (faster, less precision needed)

### Profile: Quality-Balanced (Default)

```ini
[/Script/Engine.Engine]
r.TSR.History.SampleCount=16.0
r.TSR.Velocity.WeightClampingSampleCount=4.0
r.TSR.Velocity.WeightClampingPixelSpeed=1.0
r.TSR.History.ScreenPercentage=100
r.TSR.ShadingRejection.SampleCount=2.0
r.TSR.RejectionAntiAliasingQuality=2
r.TSR.ShadingRejection.Flickering=1
r.TSR.ReprojectionField=0
```

**Effect**:
- Stationary: 16-frame temporal filter (0.0625 alpha)
- Fast moving: 4-frame temporal filter (0.25 alpha, at 1+ pixel/frame)
- Balanced quality and responsiveness

---

## Metadata Encoding: Sample Count Tracking

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\TemporalSuperResolution.cpp` (lines 405–411, 1204–1215)

### History Metadata Array

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FTSRHistoryTextures, )
    SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, ColorArray)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, MetadataArray)   // Sample count + uncertainty
    SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, GuideArray)      // Reprojected history
    SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, MoireArray)      // Flickering luma
    SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, CoverageArray)   // Thin geometry flags
END_SHADER_PARAMETER_STRUCT()
```

### Sample Count Storage

**Encoding**: `uint` or `half` per pixel

```hlsl
// Encode: store sample count in [0, 32] range
uint EncodedSampleCount = uint(clamp(SampleCount, 0.0, 32.0));

// Decode: retrieve accumulated sample count
float SampleCount = float(EncodedSampleCount);

// Update: after blending new frame
float NewSampleCount = (1.0 - BlendAlpha) * PrevSampleCount + BlendAlpha * 1.0;
//  If prev had 16 samples and alpha = 0.0625:
//    NewSampleCount = 0.9375 * 16 + 0.0625 * 1 = 15.0625

// Clamp to max
NewSampleCount = min(NewSampleCount, HistorySampleCount);
```

**Metadata Channels**:
- **R**: Sample count (8-bit, 0–32)
- **G**: Uncertainty level (optional, 8-bit)
- **B**: Moire index (if enabled, 8-bit)
- **A**: Thin geometry flag (if enabled, 8-bit)

Or compressed as single `uint32`:
```hlsl
uint Metadata = (SampleCountBits << 0) | (UncertaintyBits << 8) | (MoireBits << 16) | (GeoTypeBits << 24);
```

---

## Velocity Clamping Mechanics: Deep Dive

### Why Clamp on Velocity?

Fast-moving pixels are **less stable** than static pixels because:
1. **Sub-pixel reprojection error**: Even 0.1-pixel error on 1000 pixel/sec motion = 10 pixels offset in reprojection
2. **Temporal aliasing**: High-frequency motion creates temporal moire if accumulated over too many frames
3. **Occlusion**: Moving objects reveal new areas frame-to-frame (disocclusion risk)

### Non-Linear Transition

```
Alpha

0.25 |                          ___________
     |                       __/
     |                    __/
     |                 __/
     |              __/
     | 0.0625 ____/
     |____________|________|_______________|_______ Velocity (pixels/frame)
                 1.0       2.0
```

- **Below 1.0 px/frame**: Use 1/16 samples (16-frame temporal filter)
- **At 1.0 px/frame**: Transition begins
- **At 2.0+ px/frame**: Clamp to 1/4 samples (4-frame temporal filter)

### Clamping Interaction with History.ScreenPercentage

**Important note** (from CVarTSRWeightClampingSampleCount docstring, lines 285–298):

```
"Please note this clamp the sample count in history pixel, not output pixel, and therefore
lower values are by designed less noticeable with higher r.TSR.History.ScreenPercentage."
```

**Implication**:
- If `History.ScreenPercentage = 200%`:
  - Clamping happens in 200% resolution space
  - Downsampling to 100% output adds implicit 2× temporal smoothing (Nyquist)
  - Clamping effect is less visible

- If `History.ScreenPercentage = 100%`:
  - Clamping happens in 100% space
  - No additional downsampling smoothing
  - Clamping effect is more visible → use smaller `WeightClampingSampleCount` (e.g., 2.0 instead of 4.0)

---

## Flickering Detection Impact on Blend Factor

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRMeasureFlickeringLuma.usf`

### Flickering Luminance Recording

Each frame, record per-pixel luminance:
```hlsl
float Luma = Luminance(SceneColor.rgb);
FlickeringLumaOutput[PixelPos] = tsr_half(Luma);
```

### Temporal Oscillation Detection

In `RejectShading` pass:
```hlsl
float CurrentLuma = InputMoireLumaTexture[PixelPos];
float PrevLuma = ReprojectedHistoryGuideMetadataTexture[PixelPos];

float LumaChange = abs(CurrentLuma - PrevLuma);
if (LumaChange > FlickeringThreshold && OscillationFreq > FlickeringFramePeriod)
{
    // Pixel is flickering: reject history to stabilize
    HistoryRejectionOutput[PixelPos].a = 1.0;  // Flag for flickering

    // Force alpha = 1.0 (current frame only)
    // or use soft rejection (alpha = 0.5) if amplitude < threshold
}
```

### Frame Rate Adjustment (Critical)

**File**: Lines 1793–1794 of TemporalSuperResolution.cpp

```cpp
const float RefreshRateToFrameRateCap =
    (View.Family->Time.GetDeltaRealTimeSeconds() > 0.0f &&
     PassConfig.ShadingRejectionFlickeringAdjustToFrameRate)
    ? View.Family->Time.GetDeltaRealTimeSeconds() * PassConfig.ShadingRejectionFlickeringFrameRateCap
    : 1.0f;

// Then, compute flickering frame period:
const float FlickeringFramePeriod = PassConfig.ShadingRejectionFlickering
    ? (PassConfig.ShadingRejectionFlickeringPeriod / FMath::Max(RefreshRateToFrameRateCap, 1.0f))
    : 0.0f;
```

**Example**:
- `ShadingRejectionFlickeringPeriod = 2.0` frames
- Rendering at 120 FPS (DeltaTime = 0.00833s)
- `FrameRateCap = 60` Hz

```
RefreshRateToFrameRateCap = 0.00833 * 60 = 0.5
FlickeringFramePeriod = 2.0 / 0.5 = 4.0 frames
```

At 60 FPS, period = 2.0 frames (as configured).
At 120 FPS, period = 4.0 frames (automatically adjusted).

**Rationale**: Prevent false positives at high frame rates where sub-frame temporal variation is expected.

---

## Thin Geometry Relaxation

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRDetectThinGeometry.usf`

### Purpose

When geometry is too thin to render consistently (wires, foliage), rejection is overly aggressive. Thin geometry detection **relaxes rejection** slightly:

```hlsl
float ThinGeometryRelaxationWeight = DetectThinGeometry(PixelPos);  // [0, MaxRelaxationWeight]

if (ThinGeometryRelaxationWeight > 0)
{
    // Soft rejection: blend history back in slightly
    HistoryRejectionOutput[PixelPos].g = ThinGeometryRelaxationWeight;  // Channel G = clamping factor
}
```

### Classification

- **Edge line** (red in visualization): Single-pixel line on non-foliage
  - Relaxation: full (MaxRelaxationWeight)

- **Cluster hole** (green): Foliage with partial coverage vs background
  - Relaxation: partial (0.5 * MaxRelaxationWeight)

- **Other** (yellow): Complex case
  - Relaxation: none (0.0)

### Parameter

```cpp
// Line 326–329
r.TSR.ThinGeometryDetection.Coverage.MaxRelaxationWeight = 0.037  // 3.7%
```

When applied:
```hlsl
HistoryAlpha = (1.0 - RelaxationWeight) * HistoryAlpha + RelaxationWeight * 0;
// Reduces rejection effect by up to 3.7%
```

---

## Quality Tiers: UpdateHistory Permutations

**File**: Lines 1160–1166 of TemporalSuperResolution.cpp

```cpp
enum class EQuality
{
    Low,      // No spatial filtering, basic reprojection
    Medium,   // Mid-tier jitter/reprojection
    High,     // Advanced jitter (more samples)
    Epic,     // Maximum quality (most samples)
    MAX
};
```

**Controlled by**: `r.TSR.History.UpdateQuality` (CVar, line 72–76)

**Permutation compilation** (lines 1220–1230):
```cpp
if (!FTSRShader::ShouldCompile32or16BitPermutation(Parameters.Platform, PermutationVector.Get<FTSRShader::F16BitVALUDim>()))
{
    return false;
}
return FTSRShader::ShouldCompilePermutation(Parameters);
```

---

## Anti-Aliasing Interaction

### When Spatial Anti-Aliasing Applies

From `TSRSpatialAntiAliasing.usf` (lines 102–107):

```hlsl
bool bAntiAlias[2];
bAntiAlias[0] = AntiAliasMaskTexture[InputPixelPos[0]] > 0;
bAntiAlias[1] = AntiAliasMaskTexture[InputPixelPos[1]] > 0;

bool bSkipAntiAliaser = all(!bAntiAlias);
```

Only pixels marked in `AntiAliasMaskOutput` from `RejectShading` pass receive spatial filtering.

### Offset Applied

```hlsl
// Lines 212–223
uint2 EncodedTexelOffset = EncodeSpatialAntiAliasingOffset(TexelOffset);
AntiAliasingOutput[OutputPixelPos] = EncodeAntiAliasingOutput(EncodedTexelOffset, NoiseFiltering);

// In UpdateHistory:
if (AntiAliasMask[PixelPos] > 0)
{
    float2 AAOffset = DecodeAntiAliasingOffset(AntiAliasingTexture[PixelPos]);
    InputColor = BilinearSample(InputTexture, PixelPos + AAOffset);  // Blur current frame
}
```

**Effect**: Softens aliasing on newly visible geometry (disoccluded areas).

---

## Summary: Blend Factor Decision Tree

```
┌─────────────────────────────────┐
│ Compute HistorySampleCount      │
│ = r.TSR.History.SampleCount     │
│ Alpha = 1.0 / SampleCount       │
└────────────┬────────────────────┘
             │
             ▼
┌─────────────────────────────────┐
│ Check Velocity Magnitude        │
└────────────┬────────────────────┘
             │
         ┌───┴──────────────────────┐
         │                          │
   < 1.0 px/frame           >= 1.0 px/frame
         │                          │
         │          ┌──────────────┴─────────┐
         │          │                        │
         │      [1, 2] px/frame        >= 2.0 px/frame
         │          │                        │
    Alpha = 1/16   Blend to 1/4       Alpha = 1/4
    (0.0625)      (0.25)             (0.25)
         │          │                        │
         └──────────┴────────────────────────┘
                    │
                    ▼
         ┌─────────────────────────────────┐
         │ Check History Rejection Mask    │
         └─────────────┬───────────────────┘
                       │
           ┌───────────┴───────────┐
           │                       │
       Rejected              Valid
           │                       │
      Alpha = 1.0           Use velocity
           │                 clamped alpha
           │                       │
    Apply spatial AA          Blend with
    (if enabled)              history
           │                       │
           └───────────┬───────────┘
                       │
                       ▼
            ┌──────────────────────┐
            │ Write new history    │
            │ with sample count    │
            └──────────────────────┘
```

---

## Key Takeaways for UzEngine Implementation

1. **Dual-axis alpha control**: Base (sample count) + velocity clamping
2. **Velocity threshold is critical**: Default 1.0 px/frame determines responsiveness
3. **Frame rate scaling**: Adjust flickering period when FPS < cap
4. **Metadata tracking**: Store and update sample count per pixel each frame
5. **Soft rejection**: Use float4 rejection mask, not binary yes/no
6. **Spatial AA integration**: Apply only to rejected pixels to hide aliasing
7. **Quality tiers**: Provide Low/Medium/High/Epic for scalability
8. **CVar exposure**: Allow per-project tuning for game genre

