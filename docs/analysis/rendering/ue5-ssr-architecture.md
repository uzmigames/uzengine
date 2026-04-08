<!-- doc-meta
title: UE5 Screen-Space Reflections Architecture Analysis
category: rendering
engine: UE5.6
date: 2026-04-04
-->

# UE5 Screen-Space Reflections — Complete Architecture Analysis

Source: `W:\UE_5.6_Source\Engine\`

## 1. High-Level Pipeline Overview

UE5's SSR pipeline has 3 distinct stages:

```
Stage 1: Ray March (SSRTReflections.usf)
    → Traces rays in screen space against HZB (Hierarchical Z-Buffer)
    → Outputs: SSR color (RGBA) + optional hit distance for denoiser

Stage 2: Denoise (ScreenSpaceDenoise/SSD*.usf)
    → Spatial reconstruction (directional ellipse kernel)
    → Temporal accumulation (variance-clipped history blending)
    → Post-filtering (optional)

Stage 3: Composite
    → SSR result blended into reflection environment
```

**Key C++ files:**
- `Engine/Source/Runtime/Renderer/Private/ScreenSpaceRayTracing.cpp` — pass orchestration
- `Engine/Source/Runtime/Renderer/Private/ScreenSpaceRayTracing.h` — types, enums
- `Engine/Source/Runtime/Renderer/Private/ScreenSpaceDenoise.cpp` — denoiser dispatch

**Key shader files:**
- `Engine/Shaders/Private/SSRT/SSRTReflections.usf` — ray march main shader
- `Engine/Shaders/Private/SSRT/SSRTRayCast.ush` — HZB ray casting core
- `Engine/Shaders/Private/ScreenSpaceDenoise/SSDSpatialAccumulation.usf` — spatial filter
- `Engine/Shaders/Private/ScreenSpaceDenoise/SSDTemporalAccumulation.usf` — temporal filter


## 2. SSR Ray Tracing (Stage 1)

### 2.1 Quality Levels

From `ScreenSpaceRayTracing.cpp:14-25` and `SSRTReflections.usf:198-218`:

| r.SSR.Quality | ESSRQuality | NumSteps | NumRays | Glossy | Notes |
|---------------|-------------|----------|---------|--------|-------|
| 0             | Off         | —        | —       | —      | SSR disabled |
| 1             | Low         | 8        | 1       | false  | Mirror-only |
| 2             | Medium      | 16       | 1       | true*  | Glossy only if denoiser active |
| 3 (default)   | High        | 8        | 4       | true   | Few samples, uses importance sampling |
| 4             | Epic        | 12       | 12      | true   | Too slow for real-time |

*At Quality 2, `bGlossy` is true only when `SSR_OUTPUT_FOR_DENOISER` is defined (line 205-208).

Quality selection also respects `PostProcessSettings.ScreenSpaceReflectionQuality`:
- Quality >= 80 AND CVar >= 4 → Epic
- Quality >= 60 AND CVar >= 3 → High
- Quality >= 40 AND CVar >= 2 → Medium
- Otherwise → Low

(See `GetSSRQualityForView()` at line 713-742)

### 2.2 Ray Direction — Stochastic vs Mirror

UE5 uses TWO distinct ray direction strategies depending on quality and ray count:

**Single-ray path (NumRays == 1):**
- If `bGlossy == false`: Pure mirror reflection `L = reflect(-V, N)` (line 374)
- If `bGlossy == true`: Single stochastic ray via `ImportanceSampleVisibleGGX` (line 369)
  - Random numbers from `Rand1SPPDenoiserInput(PixelPos)` — uses `Rand3DPCG16(int3(PixelPos, View.StateFrameIndexMod8))` to get 16-bit random per pixel, varying per temporal frame
  - When denoiser is active: `E.y *= 1 - GGX_IMPORTANT_SAMPLE_BIAS` where `GGX_IMPORTANT_SAMPLE_BIAS = 0.1` — this truncates the GGX tail to reduce denoiser noise

**Multi-ray path (NumRays > 1):**
- Uses Hammersley sequence: `Hammersley16(i, NumRays, Random)` (line 250)
- Random seed: `Rand3DPCG16(int3(PixelPos, View.StateFrameIndexMod8)).xy` (line 227)
- Importance sampling: `ImportanceSampleVisibleGGX(E, a, TangentV)` (line 253) — VNDF sampling, NOT standard GGX
- Low roughness optimization: if `Roughness < 0.1`, collapses to mirror with more steps: `NumSteps = min(NumSteps * NumRays, 24)` and `NumRays = 1` (line 234-238)
- Also if `Roughness < 0.1` inside the loop: forces `L = reflect(-V, N)` (line 288)

### 2.3 Temporal Jitter

**Step offset jitter (ray starting position along ray):**
- Multi-ray: `InterleavedGradientNoise(SvPosition.xy, View.StateFrameIndexMod8)` offset by -0.5 (line 223, 247-248)
- Single-ray: same `InterleavedGradientNoise(SvPosition.xy, View.StateFrameIndexMod8)` offset by -0.5 (line 348, 352)
- This is spatial AND temporal — `StateFrameIndexMod8` cycles 0..7, giving 8 temporal phases

**Ray direction jitter:**
- Multi-ray: Hammersley + PCGR16 per pixel per frame (temporally varying)
- Single-ray glossy: `Rand3DPCG16(PixelPos, StateFrameIndexMod8)` (temporally varying)
- Single-ray mirror: No direction jitter (pure reflect)

**Frame random for SSRParams.a:**
- If TAA is on: `TemporalAASampleIndex * 1551` (line 303)
- If TAA is off: `FrameIndex(8) * 1551` (line 309)

### 2.4 HZB Ray Casting

From `SSRTRayCast.ush`, the `CastScreenSpaceRay()` function (line 255-627):

**Algorithm:** Hierarchical screen-space ray march against the Hierarchical Z-Buffer.

**Key parameters:**
- `StartMipLevel = 1.0` (line 661)
- Batch size: `SSRT_SAMPLE_BATCH_SIZE = 4` — evaluates 4 samples simultaneously per iteration
- MIP level increases during march: `Level += (8.0 / NumSteps) * Roughness` per 2 steps (line 425-426)
- Compare tolerance: `max(abs(RayStepScreen.z), (RayStartScreen.z - RayDepthScreen.z) * 4.0)` for SSR, `* 2.0` for SSGI (line 163-164)

**Hit detection:**
```hlsl
MultipleSampleDepthDiff = SamplesZ - SampleDepth;
bMultipleSampleHit = abs(MultipleSampleDepthDiff + CompareTolerance) < CompareTolerance
                     && SampleDepth != FarDepthValue;
```
This means a hit is detected when the ray Z is within `[SampleDepth - 2*CompareTolerance, SampleDepth]`.

**Hit refinement (SSR path, line 544-607):**
Linear interpolation between the last non-hit and first hit samples:
```hlsl
float TimeLerp = saturate(DepthDiff0 / (DepthDiff0 - DepthDiff1));
float IntersectTime = Time0 + TimeLerp;
OutHitUVz = RayUVz + RayStepUVz * IntersectTime;
```
Binary search refinement code exists but is disabled (`#if 0`, lines 575-598).

**Uncertainty tracking:**
The ray tracker distinguishes between "hit" and "uncertain" (ray went behind geometry). If `bStopWhenUncertain` is set, the ray terminates early on uncertainty.

### 2.5 Hit Color Sampling

From `SSRTReflections.usf` line 314-344:

**Reprojection:** The hit position is reprojected to previous frame UV using `ReprojectHit()`:
1. Transform hit screen pos to clip space
2. Multiply by `View.ClipToPrevClip` matrix
3. If velocity buffer has data at hit position, use per-object motion vectors instead
4. Apply `PrevScreenPositionScaleBias` to get UV

**Vignette:** Screen-edge fade applied via `ComputeHitVignetteFromScreenPos()`:
```hlsl
float2 Vignette = saturate(abs(ScreenPos) * 5 - 4);
return SafeSaturate(1.0 - dot(Vignette, Vignette));
```
This creates a fade-to-zero in the outer 20% of the screen.

**Color sampling:** `SampleScreenColor(SceneColor, SceneColorSampler, SampleUV)` — point-sampled from the bound `SceneColor` texture.

**Karis tonemap for multi-ray accumulation:**
When `NumRays > 1`, UE5 applies Karis averaging (lines 320, 344):
```hlsl
// Before accumulation — compress HDR
SampleColor.rgb *= rcp(1 + Luminance(SampleColor.rgb));
// ... accumulate ...
// After accumulation — decompress
OutColor.rgb *= rcp(1 - Luminance(OutColor.rgb));
```
This prevents firefly artifacts from bright specular highlights dominating the average.

**Final modulation:**
```hlsl
OutColor *= RoughnessFade;    // fade to 0 at MaxRoughness
OutColor *= SSRParams.r;      // Intensity (0..1, from PostProcessSettings * 0.01)
OutColor.rgb *= PrevSceneColorPreExposureCorrection;  // exposure correction between frames
```


## 3. Scene Color Input — CRITICAL: Previous Frame, NOT Current Frame

### 3.1 Input Priority

From `RenderScreenSpaceReflections()` at line 983-1022:

UE5's SSR reads from the **PREVIOUS FRAME's** scene color. The priority chain is:

```
1. View.PrevViewInfo.CustomSSRInput     — custom SSR input (plugin/override)
2. PrevViewInfo.HalfResTemporalAAHistory — half-res TAA history (if r.SSR.HalfResSceneColor)
3. PrevViewInfo.TemporalAAHistory.RT[0]  — PREVIOUS FRAME's TAA output
4. CurrentSceneColor                     — CURRENT frame (fallback only)
```

**The critical design decision:** SSR reads **previous frame TAA output**, NOT current frame scene color. This means:
- SSR reflections show what was visible LAST FRAME
- No feedback loop — SSR doesn't read its own output
- One frame of latency, masked by temporal accumulation
- Reprojection via `ClipToPrevClip` + velocity buffer compensates for camera/object motion

### 3.2 Previous Frame Storage

From `DeferredShadingRenderer.cpp:3501-3502`:
```cpp
GraphBuilder.QueueTextureExtraction(SceneTextures.Depth.Resolve,
    &ViewState->PrevFrameViewInfo.DepthBuffer);
GraphBuilder.QueueTextureExtraction(SceneTextures.Color.Resolve,
    &ViewState->PrevFrameViewInfo.ScreenSpaceRayTracingInput);
```
The current frame's resolved scene color and depth are saved for NEXT frame's SSR/SSGI.

### 3.3 Exposure Correction

When reading previous frame color, exposure may differ. Line 1145:
```cpp
PrevSceneColorPreExposureCorrection = (InputColor != CurrentSceneColor)
    ? View.PreExposure / View.PrevViewInfo.SceneColorPreExposure
    : 1.0f;
```

### 3.4 Velocity Buffer Safety

If `InputColor` is the same as `CurrentSceneColor` (fallback path) OR there is no velocity buffer, UE5 replaces the velocity texture with `MidGreyDummy` (line 1052-1056). This prevents motion vector decoding from producing garbage when reading current-frame color that hasn't been through TAA yet.


## 4. Roughness Handling

### 4.1 MaxRoughness Fade

From `ComputeRoughnessMaskScale()` at line 279-289:

```cpp
float MaxRoughness = Clamp(PostProcessSettings.ScreenSpaceReflectionMaxRoughness, 0.01f, 1.0f);
float RoughnessMaskScale = -2.0f / MaxRoughness;
// For quality < 3 (non-glossy): multiply by 2.0 for sharper cutoff
RoughnessMaskScale *= (quality < 3) ? 2.0f : 1.0f;
```

In shader (`SSRTReflections.usf:45-49`):
```hlsl
float GetRoughnessFade(float Roughness) {
    // SSRParams.y = RoughnessMaskScale
    return min(Roughness * SSRParams.y + 2, 1.0);
}
```

So the fade function is: `f(r) = min(r * (-2/MaxRoughness) + 2, 1.0)`
- At `r = 0`: f = 2 → clamped to 1.0 (full SSR)
- At `r = MaxRoughness/2`: f = 1.0 (still full SSR)
- At `r = MaxRoughness`: f = 0.0 (no SSR)

Default `ScreenSpaceReflectionMaxRoughness` = 0.6 in PostProcessSettings.

### 4.2 Clear Coat Handling

`GetRoughness()` (line 29-43): For clear coat materials, roughness is lerped between base roughness and clear coat roughness based on clear coat intensity:
```hlsl
Roughness = lerp(Roughness, ClearCoatRoughness, ClearCoat);
```

### 4.3 Anisotropy

For anisotropic materials (line 158-166), UE5 calls `ModifyGGXAnisotropicNormalRoughness()` to adjust the normal and roughness before SSR ray generation. Clear coat anisotropy is excluded (blended out by `ClearCoat` factor).

### 4.4 HZB MIP Level vs Roughness

During the ray march, the HZB mip level increases proportionally to roughness:
```hlsl
Level += (8.0 / NumSteps) * Roughness;  // per 2 march steps
```
Rougher surfaces trace at coarser HZB levels, which is both a performance optimization and matches the expected blur of rough reflections.


## 5. SSR Denoiser — Temporal Accumulation

### 5.1 Configuration for Reflections

From `SSDTemporalAccumulation.usf:81-101`:

```
SIGNAL_PROCESSING_REFLECTIONS:
  History rejection:     HISTORY_REJECTION_VAR_BOUNDARIES (variance-based)
  Rejection color space: COLOR_SPACE_LCOCG | COLOR_SPACE_UNPREMULTIPLY
  Rejection sample set:  SAMPLE_SET_3X3_PLUS (5 taps: center + 4 cardinal)
  History kernel:        SAMPLE_SET_2X2_BILINEAR (default)
  Bilateral preset:      BILATERAL_PRESET_REFLECTIONS_TAA
  Target sample count:   1024
  Input textures:        2 (color + hit distance)
  History textures:      2
```

### 5.2 Temporal Blend Factor

UE5 does NOT use a fixed alpha blend. Instead it uses **sample-count-based accumulation** (line 1059-1121):

1. Compute `TargetedSampleCount` based on the GGX specular lobe projected to screen space:
   ```hlsl
   ProjectSpecularLobeToScreenSpace(RefSceneMetadata,
       NormalizedScreenMajorAxis, InifinityMajorViewportRadius, InifinityMinorViewportRadius);
   TargetedSampleCount = PI * MajorRadius * MinorRadius;
   TargetedSampleCount = clamp(TargetedSampleCount, 1, 1024);
   ```
   Mirror-like surfaces get ~1 target sample. Rough surfaces get up to 1024.

2. Compute `PreviousFrameWeight`:
   ```hlsl
   PreviousFrameWeight = min(HistorySample.SampleCount,
                             TargetedSampleCount - CurrentFrameSample.SampleCount);
   PreviousFrameMultiplier = PreviousFrameWeight / HistorySample.SampleCount;
   ```

3. Blend in Karis-weighted color space:
   ```hlsl
   HistorySample = HistorySample * PreviousFrameMultiplier + CurrentFrameSample;
   ```

**Effective blend alpha:** For a mirror surface (target ~1), history weight is nearly 0 — mostly current frame. For a rough surface (target ~1024), history weight approaches `1 - 1/SampleCount` — heavy temporal averaging. This is adaptive and self-regulating.

### 5.3 Variance Clipping (Anti-Ghosting)

From `SSDTemporalAccumulation.usf:898-988`:

UE5 uses **variance-based clipping** (not simple min/max AABB):

1. Gather neighborhood statistics using 3x3+ kernel (5 taps):
   ```hlsl
   NeighborMoment1 = mean of neighbors
   NeighborMoment2 = mean of squared neighbors
   ```

2. Compute standard deviation:
   ```hlsl
   StdDev = sqrt(abs(NeighborMoment2 - NeighborMoment1 * NeighborMoment1));
   ```

3. Build clipping box:
   ```hlsl
   const float StdDevMultiplier = 1.25;  // for reflections (6.0 for AO)
   NeighborMin = NeighborMoment1 - StdDev * StdDevMultiplier;
   NeighborMax = NeighborMoment1 + StdDev * StdDevMultiplier;
   ```
   **StdDevMultiplier = 1.25** for reflections is TIGHT — aggressive ghosting rejection.

4. Clamp history to the box (in LCoCg color space, unpremultiplied):
   ```hlsl
   ClampedHistory = clamp(History, NeighborMin, NeighborMax);
   ```

5. Compute rejection factor (how much the clamp changed the history):
   ```hlsl
   // For reflections, measured in LCoCg space:
   RejectionFactor = abs(Luma_To_LumaLog(ClampedLuma) - Luma_To_LumaLog(OriginalLuma));
   RejectionFactor = max(RejectionFactor, max(ChormaDiff.y, ChromaDiff.z));
   RejectionFactor = max(RejectionFactor, AlphaDiff);
   ```

6. Reduce history weight proportionally to rejection:
   ```hlsl
   FinalHistoryWeight = HistorySample.SampleCount * saturate(1 - RejectionFactor);
   ```


## 6. SSR Denoiser — Spatial Reconstruction

From `SSDSpatialAccumulation.usf:155-184`:

```
SIGNAL_PROCESSING_REFLECTIONS spatial config:
  Sample set:    SAMPLE_SET_DIRECTIONAL_ELLIPSE (anisotropic kernel)
  Color accumulation: COLOR_SPACE_RGB | COLOR_SPACE_KARIS_WEIGHTING
  Bilateral preset: BILATERAL_PRESET_REFLECTIONS
```

### 6.1 Directional Ellipse Kernel

From line 953-985, the spatial reconstruction uses an **anisotropic elliptical kernel** shaped by the GGX specular lobe:

1. Project the specular lobe into screen space to get major/minor axes
2. Kernel radius scales with `ConfusionFactor` (from hit distance) and viewport size
3. Kernel is elliptical: `MajorPixelRadius` along the specular stretch direction, `MinorPixelRadius` perpendicular
4. Maximum radius capped at `MaxPixelDiameter` which depends on `TargetSampleCount / AspectRatio`

The `SAMPLE_SET_DIRECTIONAL_ELLIPSE` distributes samples along the ellipse axes, preferring the major axis direction.

### 6.2 Karis Weighting

The spatial accumulation uses `COLOR_SPACE_KARIS_WEIGHTING`:
```hlsl
weight *= rcp(1.0 + Luminance(color));
```
This prevents bright specular highlights from dominating the spatial filter, avoiding visible kernel patterns in high-contrast areas.


## 7. Flickering Prevention Techniques

UE5 uses MULTIPLE techniques simultaneously:

### 7.1 Previous Frame Color (Primary Anti-Flicker)
Reading previous frame TAA output means the input to SSR is already temporally stable. TAA has smoothed out per-frame jitter before SSR reads it.

### 7.2 Temporal Noise (Step Offset)
`InterleavedGradientNoise(SvPosition.xy, View.StateFrameIndexMod8)` varies BOTH spatially and temporally (8-frame cycle). This distributes HZB marching artifacts across frames for temporal accumulation to clean up.

### 7.3 Karis Inverse Tonemap (Multi-Ray)
For multi-ray paths, HDR color is compressed before averaging and decompressed after:
```hlsl
color *= rcp(1 + Luminance(color));    // before accumulation
result *= rcp(1 - Luminance(result));  // after accumulation
```
Prevents bright pixels from causing firefly flicker.

### 7.4 Variance Clipping with Tight StdDev
`StdDevMultiplier = 1.25` is very tight — aggressively clips history that deviates from the current neighborhood. Prevents ghosting which appears as temporal smearing/flicker.

### 7.5 GGX Tail Truncation
`GGX_IMPORTANT_SAMPLE_BIAS = 0.1` — truncates the 10% most extreme GGX samples, which would produce the noisiest ray directions. Reduces per-frame variance at the cost of slightly incorrect GGX distribution.

### 7.6 Roughness-Adaptive Temporal Weight
Mirror surfaces (low roughness) get minimal temporal blending (fresh every frame). Rough surfaces get heavy temporal averaging (1024 target samples). This prevents rough reflections from flickering while keeping mirror reflections responsive.

### 7.7 Screen-Edge Vignette
`ComputeHitVignetteFromScreenPos()` fades SSR to zero in the outer 20% of the screen, preventing temporal instability at screen edges where reprojection is unreliable.

### 7.8 Velocity-Aware Reprojection
Hit positions are reprojected using per-object velocity vectors when available, not just camera motion. This prevents dynamic objects from causing temporal discontinuities.


## 8. CVars and Post-Process Settings

| CVar / Setting | Default | Purpose |
|----------------|---------|---------|
| `r.SSR.Quality` | 3 | Quality level 0-4 |
| `r.SSR.Temporal` | 0 | Enable SSR-specific temporal (separate from TAA) |
| `r.SSR.Stencil` | 0 | Stencil prepass optimization |
| `r.SSR.Compute` | 0 | 0=PS, 1=sync compute, 2=async compute |
| `r.SSR.HalfResSceneColor` | 0 | Use half-res input |
| `PostProcess.ScreenSpaceReflectionIntensity` | 100 | 0-100, maps to 0-1 in shader |
| `PostProcess.ScreenSpaceReflectionMaxRoughness` | 0.6 | Roughness cutoff |
| `PostProcess.ScreenSpaceReflectionQuality` | 50 | Quality threshold for level selection |


## 9. UzEngine Adaptation Notes

### 9.1 Scene Color Input Strategy

**CRITICAL:** UzEngine MUST read previous frame's scene color (post-TAA if available) for SSR, NOT current frame. This is the single most important anti-flicker technique. Store the resolved scene color at end of frame for next frame's SSR.

### 9.2 Simplified Pipeline for UzEngine

For a first implementation matching UE5 Quality 2 (Medium):
1. Single ray per pixel, importance-sampled via `ImportanceSampleVisibleGGX`
2. HZB march with 16 steps, starting at MIP 1
3. Step offset = `InterleavedGradientNoise(pixel, frameIndex % 8) - 0.5`
4. Read previous frame TAA output
5. Apply roughness fade: `min(roughness * (-2/maxRoughness) + 2, 1.0)`
6. Apply Karis tonemap for accumulation if multi-ray
7. Screen-edge vignette

For temporal:
1. Variance clipping with `StdDevMultiplier = 1.25`
2. 3x3+ neighborhood (5 taps) for statistics
3. Rejection in LCoCg color space (unpremultiplied)
4. Sample-count-based blend factor, target = `PI * majorRadius * minorRadius` of projected specular lobe

### 9.3 Key Constants to Replicate

| Constant | Value | Source |
|----------|-------|--------|
| HZB start mip | 1.0 | `SSRTRayCast.ush:661` |
| Step offset | -0.5 | `SSRTReflections.usf:248` |
| Compare tolerance scale (SSR) | 4.0 | `SSRTRayCast.ush:164` |
| Batch size | 4 | `SSRTRayCast.ush:6` |
| GGX sample bias | 0.1 | `SSDPublic.ush:12` |
| Vignette factor | `abs(pos) * 5 - 4` | `SSRTRayCast.ush:676` |
| StdDev multiplier (reflections) | 1.25 | `SSDTemporalAccumulation.usf:903` |
| Targeted sample count (reflections) | 1024 max | `SSDCommon.ush:32` |
| Roughness collapse threshold | 0.1 | `SSRTReflections.usf:234,286` |
| Max step collapse | 24 | `SSRTReflections.usf:237` |
| MIP increase rate | `(8.0 / NumSteps) * Roughness` per 2 steps | `SSRTRayCast.ush:425` |
| Rejection color space | LCoCg unpremultiplied | `SSDDefinitions.ush:328` |
| IGN magic | `{0.06711056, 0.00583715, 52.9829189}` | `RandomInterleavedGradientNoise.ush:16` |
| IGN frame offset | `frameId * float2(47, 17) * 0.695` | `RandomInterleavedGradientNoise.ush:14` |
