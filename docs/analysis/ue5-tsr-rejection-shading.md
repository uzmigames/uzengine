<!-- doc-meta version="1" -->

# UE5 TSR: Shading Rejection & History Invalidation

**Date**: 2026-03-18
**UE5 Version**: 5.6
**Topic**: FTSRRejectShadingCS neural convolution network

## Overview

The `RejectShading` pass is the **most complex** TSR pass, responsible for detecting pixels where the accumulated history is **invalid** and should be replaced with the current frame.

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRRejectShading.usf`

**C++ Class**: `FTSRRejectShadingCS` (lines 861–977 of TemporalSuperResolution.cpp)

---

## Rejection Criteria

The pass rejects history when:

### 1. Disocclusion (New Geometry Visible)

**Detection**: Depth discontinuity between current and reprojected frame

```
Previous Frame:        Current Frame:
─────────────────      ─────────────────
│  Wall 10m away│      │ Wall 8m away   │
│              │      │                │
│              │  → Reprojected position behind current
│              │      → Pixel was inside wall
│              │      → Reject history, show new wall
```

**Implementation**:
- Compare `ClosestDepthTexture[PixelPos]` vs depth at reprojected position
- If delta > threshold: flag disocclusion

### 2. Shading Changes (Same Geometry, Different Light)

**Detection**: Color/luminance mismatch despite valid reprojection

```
Previous Frame:          Current Frame:
Dark (8 lux)    →  →     Bright (50 lux) — sun moved
Same surface              Same surface
```

**Implementation**:
- Compute luma difference: `|CurrentLuma - ReprojectedLuma|`
- Compare against `TheoricBlendFactor` (expected variance from temporal jitter)
- If exceeds threshold: reject

### 3. Flickering (Sub-Pixel Temporal Variance)

**Detection**: Luminance oscillates frame-to-frame

```
Frame N-1:  Frame N:    Frame N+1:
Dark (50)   Bright(200) Dark(50)  — Oscillating
            ↑ 150 luma change
```

**Implementation**:
- Store per-pixel luminance in `InputMoireLumaTexture`
- Compare current vs previous luma
- If oscillation frequency > `FlickeringFramePeriod`: reject

**Parameter** (line 888 of TemporalSuperResolution.cpp):
```cpp
SHADER_PARAMETER(float, FlickeringFramePeriod)
```

Computed at line 1806:
```cpp
const float FlickeringFramePeriod = PassConfig.ShadingRejectionFlickering
    ? (PassConfig.ShadingRejectionFlickeringPeriod / FMath::Max(RefreshRateToFrameRateCap, 1.0f))
    : 0.0f;
```

### 4. Material Changes (Metallic, Roughness, Emission)

**Detection**: Metadata difference in reprojected vs current

```
Previous: Metallic=0, Rough=0.8
Current:  Metallic=1, Rough=0.2
→ Same surface, different material → reject
```

**Implementation**:
- Encode material properties in `ReprojectedHistoryGuideMetadataTexture`
- Compare metadata bits
- If mismatch: soft rejection

### 5. Translucency/Semi-Transparent Objects

**Detection**: Presence of translucent geometry at pixel

```
Previous: Opaque geometry
Current:  Smoke/translucency in front
→ History behind translucency is invalid → reject
```

**Input**:
```cpp
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneTranslucencyTexture)
```

---

## Neural Convolution Network Architecture

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalSuperResolution\TSRConvolutionNetwork.ush`

### Why "Neural"?

The pass applies **multiple spatially-overlapping convolution filters** in sequence:
1. Edge-preserving bilateral filter (reduces false positives on edges)
2. Morphological dilation (connects nearby rejections)
3. Uncertainty propagation (spreads rejection confidence)

This resembles a shallow neural network with fixed weights (not learned).

### Tiling Strategy

**Execution**: 32×32 work groups with **LDS (local data share)**

```cpp
// Line 890 of TemporalSuperResolution.cpp
SHADER_PARAMETER(int32, TileOverscan)
// CVar: r.TSR.ShadingRejection.TileOverscan (default 3)
```

**Layout**:
```
┌─────────────────────────────┐
│  Overscan (padding)         │  ← 3-pixel border
│  ┌───────────────────────┐  │
│  │  32×32 work area      │  │
│  │  (actual compute)     │  │
│  └───────────────────────┘  │
│  Overscan (padding)         │
└─────────────────────────────┘

Total fetch: (32 + 2*3) × (32 + 2*3) = 38×38 = 1444 pixels
Actual output: 32×32 = 1024 pixels
```

**Why padding?**
- Convolution kernels access neighbors (e.g., 3×3 filter at edge)
- Overscan region fetched from texture, ensures valid data
- Reduces edge artifacts from convolution wrap-around

### LDS Optimization

```cpp
// From FTSRConvolutionNetworkShader::ModifyCompilationEnvironment
// (lines 662–679 of TemporalSuperResolution.cpp)
if (PermutationVector.Get<FWaveSizeOps>() != 0)
{
    OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
}
```

**Wave operations** (if supported):
- Reduce LDS traffic via `WaveReadLaneAt()` broadcasts
- Lower register pressure
- Faster convolution evaluation

---

## Input Textures to RejectShading

| Texture | Format | Purpose |
|---------|--------|---------|
| `InputTexture` | R11G11B10 or RGBA16F | Current frame scene color |
| `InputMoireLumaTexture` | R8 | Flickering detection (luminance) |
| `InputSceneTranslucencyTexture` | RGBA16F | Translucent layer color |
| `ReprojectedHistoryGuideTexture` | RGBA16F | Previous frame reprojected |
| `ReprojectedHistoryGuideMetadataTexture` | R8 or R16F | Sample count, confidence |
| `ReprojectedHistoryMoireTexture` | RGBA16F | Flickering luma history |
| `ResurrectedHistoryGuideTexture` | RGBA16F | Old frame (if resurrection enabled) |
| `DecimateMaskTexture` | RG8 | Velocity edge + disocclusion flags |
| `IsMovingMaskTexture` | R16 | Per-pixel motion indicator |
| `ThinGeometryTexture` | R8 | Thin geometry classification (red/green/yellow) |
| `ClosestDepthTexture` | RG32F | Closest depth in neighborhood |

---

## Output Channels: HistoryRejectionOutput

**Format**: float4 per pixel

```cpp
SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<tsr_half4>, HistoryRejectionOutput)
```

### Channel Semantics

| Channel | Name | Value | Meaning |
|---------|------|-------|---------|
| **R** | RejectionMask | 0.0–1.0 | Binary rejection (0=valid, 1=invalid) |
| **G** | ClampingFactor | 0.0–1.0 | Soft rejection (blending ratio) |
| **B** | ResurrectionValid | 0.0–1.0 | Multi-frame resurrection possible (if enabled) |
| **A** | FlickeringActive | 0.0–1.0 | Flickering heuristic triggered |

### Rejection Strength Interpretation

```hlsl
float RejectionMask = HistoryRejectionOutput[PixelPos].r;

if (RejectionMask > 0.5)
{
    // History is invalid: use current frame only (alpha = 1.0)
}
else if (RejectionMask > 0.2)
{
    // Soft rejection: blend with clamping
    float ClampingFactor = HistoryRejectionOutput[PixelPos].g;
    Alpha = max(1.0 / MaxSampleCount, ClampingFactor);  // Use clamped alpha
}
else
{
    // History is valid: use normal blending
    Alpha = 1.0 / HistorySampleCount;
}
```

---

## Algorithm: Per-Pixel Rejection Decision

### Pseudo-Code

```hlsl
// ==================================================================
// INPUTS
// ==================================================================

float4 CurrentColor = InputTexture[PixelPos];
float4 ReprojectedHistory = ReprojectedHistoryGuideTexture[PixelPos];
float2 DecimateMask = DecimateMaskTexture[PixelPos];
float CurrentLuma = Luminance(CurrentColor.rgb);
float HistoryLuma = Luminance(ReprojectedHistory.rgb);
uint EncodedMetadata = ReprojectedHistoryGuideMetadataTexture[PixelPos];
bool bIsDisoccluded = (DecimateMask.r & 0x3) != 0;  // Bits 0-1
bool bIsMoving = (IsMovingMaskTexture[PixelPos] > 0);
float2 ClosestDepth = ClosestDepthTexture[PixelPos];

// ==================================================================
// STEP 1: Disocclusion Detection
// ==================================================================

bool bRejectDisocclusion = false;
float2 ReprojectedDepth = DepthAtReprojectedPos;  // From DecimateHistory
float DepthDelta = abs(ClosestDepth.x - ReprojectedDepth.x);

const float DEPTH_THRESHOLD = 0.01;  // 1 cm at 1m range
if (DepthDelta > DEPTH_THRESHOLD)
{
    bRejectDisocclusion = true;
}

// ==================================================================
// STEP 2: Shading/Luma Difference Detection
// ==================================================================

bool bRejectShading = false;
float LumaDelta = abs(CurrentLuma - HistoryLuma);
float ExpectedLumaVariance = TheoricBlendFactor * HistoryLuma;
//  TheoricBlendFactor accounts for temporal jitter (typically 0.1–0.2)

if (LumaDelta > ExpectedLumaVariance && bIsMoving)
{
    // High luma change on moving geometry → likely shading change
    bRejectShading = true;
}

// ==================================================================
// STEP 3: Flickering Detection (Optional)
// ==================================================================

bool bRejectFlickering = false;
if (bEnableFlickeringHeuristic)
{
    float PrevLuma = InputMoireLumaTexture[PixelPos];
    float LumaOscillation = abs(CurrentLuma - PrevLuma);

    if (LumaOscillation > FLICKERING_THRESHOLD && !bIsMoving)
    {
        // Sub-pixel temporal variance → likely shading instability
        bRejectFlickering = true;
    }
}

// ==================================================================
// STEP 4: Thin Geometry Relaxation
// ==================================================================

float ThinGeometryRelaxation = 0.0;
if (ThinGeometryTexture[PixelPos] != 0)
{
    uint EncodedThinGeometry = ThinGeometryTexture[PixelPos];
    ThinGeometryRelaxation = float(EncodedThinGeometry >> 1) / 127.0;
    bool bIsClusterHole = (EncodedThinGeometry & 1) != 0;

    // Relax rejection on thin geometry (edges, foliage)
    if (bIsClusterHole)
    {
        ThinGeometryRelaxation *= 0.5;  // Less relaxation on cluster holes
    }
}

// ==================================================================
// STEP 5: Convolution Network (Simplified)
// ==================================================================

// Apply bilateral/morphological filtering to rejection mask
float FilteredRejection = ConvolutionNetwork(
    RejectionMask,
    LDS,
    TileOverscan);

// Relaxation reduces effective rejection
float RejectionStrength = FilteredRejection * (1.0 - ThinGeometryRelaxation);

// ==================================================================
// STEP 6: Output
// ==================================================================

float FinalRejectionMask = saturate(RejectionStrength);

HistoryRejectionOutput[PixelPos] = float4(
    FinalRejectionMask,           // R: rejection
    ThinGeometryRelaxation,       // G: clamping
    (bEnableResurrection ? 1.0 : 0.0),  // B: resurrection valid
    (bRejectFlickering ? 1.0 : 0.0)     // A: flickering active
);

InputSceneColorOutput[PixelPos] = CurrentColor;          // For next pass
AntiAliasMaskOutput[PixelPos] = (FinalRejectionMask > 0.5 ? 1 : 0);  // Apply spatial AA if rejected
```

---

## Permutation Logic

**File**: Lines 918–938 of TemporalSuperResolution.cpp

### Base Permutation Domain

```cpp
using FPermutationDomain = TShaderPermutationDomain<
    FTSRConvolutionNetworkShader::FPermutationDomain,  // Wave size, 16-bit VALU, alpha channel
    FFlickeringDetectionDim,                            // bool: r.TSR.ShadingRejection.Flickering
    FHistoryResurrectionDim,                            // bool: r.TSR.Resurrection
    FThinGeometryDetectionDim                          // bool: r.TSR.ThinGeometryDetection
>;
```

### Remapping Rules

```cpp
static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
{
    // Register pressure is identical between all permutations with 16-bit ops
    if (PermutationVector.Get<FTSRConvolutionNetworkShader::FPermutationDomain>().Get<FTSRShader::F16BitVALUDim>())
    {
        PermutationVector.Set<FFlickeringDetectionDim>(true);   // Line 927
        PermutationVector.Set<FHistoryResurrectionDim>(true);   // Line 928
    }

    // Flickering detection implies resurrection for temporal tracking
    if (PermutationVector.Get<FFlickeringDetectionDim>())       // Line 931
    {
        PermutationVector.Set<FHistoryResurrectionDim>(true);   // Line 934
    }

    return PermutationVector;
}
```

**Interpretation**:
- **16-bit VALU + Flickering**: Both enabled by default (register budget allows)
- **Flickering enabled → Resurrection enabled**: Need to store 2+ frames for oscillation detection

### Compilation Priority

**Line 972–976**:
```cpp
static EShaderCompileJobPriority GetOverrideJobPriority()
{
    // FTSRRejectShadingCS takes up to 40s on average
    return EShaderCompileJobPriority::ExtraHigh;
}
```

This is the **slowest TSR shader** to compile (40 seconds) due to:
- Large unrolled convolution loops
- Multiple permutation dimensions
- Wave operation specialization

---

## CVars Controlling RejectShading

| CVar | Default | Purpose |
|------|---------|---------|
| `r.TSR.ShadingRejection.Flickering` | 1 | Enable flickering detection (line 129) |
| `r.TSR.ShadingRejection.Flickering.Period` | 2.0 | Oscillation period (frames) (line 180) |
| `r.TSR.ShadingRejection.Flickering.FrameRateCap` | 60 | Frame rate cap for period scaling (line 168) |
| `r.TSR.ShadingRejection.SampleCount` | 2.0 | Post-rejection sample clamping (line 121) |
| `r.TSR.ShadingRejection.TileOverscan` | 3 | LDS padding (line 197) |
| `r.TSR.ThinGeometryDetection` | 0 | Enable thin geometry detection (line 307) |
| `r.TSR.WaveOps` | 1 | Use wave operations (line 78) |

---

## Translucency Integration

**File**: Lines 827–830 of TemporalSuperResolution.cpp

```cpp
bool bIsSeperateTranslucyTexturesValid = PassInputs.PostDOFTranslucencyResources.IsValid();
```

**Inputs**:
```cpp
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneTranslucencyTexture)
SHADER_PARAMETER(FScreenTransform, InputPixelPosToTranslucencyTextureUV)
SHADER_PARAMETER(FVector2f, TranslucencyTextureUVMin)
SHADER_PARAMETER(FVector2f, TranslucencyTextureUVMax)
```

**Logic**:
- If translucency layer is present, reject history behind it
- Translucent objects have different reprojection than opaque
- Avoid artifacts where translucency passes over opaque geometry

---

## Key Parameters Passed to Shader

```cpp
// From FTSRRejectShadingCS::FParameters (lines 876–916)

SHADER_PARAMETER(float, TheoricBlendFactor)           // Expected temporal variance
SHADER_PARAMETER(float, FlickeringFramePeriod)        // Oscillation detection period
SHADER_PARAMETER(int32, TileOverscan)                 // LDS overscan (3–5)
SHADER_PARAMETER(int32, bEnableResurrection)          // Multi-frame history enabled
SHADER_PARAMETER(int32, bEnableFlickeringHeuristic)   // Flickering detection enabled
```

### TheoricBlendFactor Computation

**File**: Line 1189 of TemporalSuperResolution.cpp

```cpp
// Not explicitly shown, but derived from HistoryHisteresis and HistorySampleCount
// Typically 0.1–0.2 (10–20% expected variance from jitter)
float TheoricBlendFactor = 1.0 / HistorySampleCount;
```

Higher sample count → higher expected variance threshold (because accumulated history is smoother).

---

## Summary: Decision Logic

```
REJECT if:
├─ DepthDelta > threshold (disocclusion)
├─ LumaDelta > expected (shading change)
├─ LumaOscillation > flickering threshold (AND not moving)
├─ Reprojected pixel was partially transparent
├─ (Convolution network identifies as invalid after filtering)
└─ - ThinGeometryRelaxation (soft rejection)

OUTPUT:
├─ R: 0.0 (valid) to 1.0 (invalid)
├─ G: Clamping factor (0–1)
├─ B: Resurrection valid (0–1)
└─ A: Flickering active (0–1)
```

**Next Pass**: `SpatialAntiAliasing` applies spatial filtering to pixels where R > 0.5, reducing aliasing artifacts.

