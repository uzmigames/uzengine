# Post-Processing Pipeline

<!-- doc-meta
title: Post-Processing Pipeline
category: spec
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

The post-processing pipeline converts HDR scene color to a display-ready LDR image.
It is implemented as a series of render graph passes registered by `AddPostProcessStack()`
(`postprocess_stack.h`). Each effect is independently toggleable. Auto-exposure
(`AutoExposure` class) runs before the stack via two compute passes, feeding an exposure
multiplier into `PerFrameCB` for the tonemap pass. A velocity pass (`VelocityPass`) runs
in the geometry phase to populate the velocity buffer for future TAA.

## Architecture

### Pipeline Execution Order

```
[Velocity Pass]          (geometry phase — writes motion vectors to velocity buffer)
         |
         v
[Auto-Exposure]          (compute — histogram + adapt, feeds exposureMultiplier to PerFrameCB)
         |
         v
[Tone Mapping]           (HDR → LDR via ACES filmic)
         |
         v
[Bloom]                  (downsample → blur H → blur V → composite)
         |
         v
[FXAA]                   (edge-based anti-aliasing on final LDR)
         |
         v
[Backbuffer output]
```

### Auto-Exposure

Two compute passes per frame, both using the global root signature with 8 root constants
at b0 and UAV tables at u0.

**Pass 1 — Histogram** (`exposure_histogram_cs.hlsl`):
- Builds a 64-bin log2 luminance histogram from the HDR scene color
- Thread groups: [16, 16, 1] with LDS accumulation (minimizes global atomics)
- Log2 range: -10 to +20 (UE5 defaults)

**Pass 2 — Average/Adapt** (`exposure_average_cs.hlsl`):
- Reads histogram, applies percentile filtering (ignores dark/bright tails)
- Exponential temporal adaptation with asymmetric up/down speeds
- Outputs exposure multiplier (2^-EV) to the exposure buffer

GPU buffers (persistent across frames):
- Histogram buffer: 64 x uint32 (256 bytes), UAV + SRV
- Exposure buffer: 2 x float (8 bytes), UAV

`AutoExposureSettings` parameters:

| Parameter | Default | UE5 Source |
|---|---|---|
| `exposureBias` | 0.0 EV | `AutoExposureBias=0.0` |
| `minBrightness` | 0.001 cd/m² | 2^-10 |
| `maxBrightness` | 1,048,576 cd/m² | 2^+20 |
| `histLogMin` | -10.0 | UE5 eye adaptation |
| `histLogMax` | +20.0 | UE5 eye adaptation |
| `manualExposure` | 0.0 (auto) | — |

When `manualExposure > 0`, the GPU dispatch is skipped and the fixed EV is used directly.
ISO 12232 normalization constant: 1.2 (`kExposureNorm`).

### Tone Mapping

- **Shader**: `tonemap_ps.hlsl`
- **Algorithm**: ACES filmic split-sum approximation
- **Input**: HDR scene color (RGBA16F), exposure multiplier from `PerFrameCB`
- **Output**: LDR scene color (RGBA8 or RGBA16F depending on backbuffer format)
- Configurable `tonemapIntensity` (global multiplier, default 1.0) and `exposureAdjust`
  (log2 EV offset, default 0.0)

### Bloom

Four pixel shaders implement bloom:

| Shader | Purpose |
|---|---|
| `bloom_downsample_ps.hlsl` | Luminance-threshold downsample to half-res |
| `bloom_blur_h_ps.hlsl` | Horizontal separable Gaussian blur |
| `bloom_blur_v_ps.hlsl` | Vertical separable Gaussian blur |
| `bloom_composite_ps.hlsl` | Additive composite of blurred bloom onto scene |

`BloomPassDesc` registers all four as sequential render graph passes. Configurable:
`bloomThreshold` (default 0.5), `bloomIntensity` (default 0.5), `blurIntensity`
(default 1.0), `bloomMipLevels` (default 3).

### FXAA

- **Shader**: `fxaa_ps.hlsl`
- **Input**: LDR scene color after bloom
- **Algorithm**: Luminance contrast edge detection + perpendicular blur
- Non-edge pixels pass through unchanged
- Configurable `edgeThreshold` (default 0.1, range [0.05, 0.2]) and `blurStrength`
  (default 1.0)

### Velocity Buffer

`VelocityPass` (`velocity_pass.h/cpp`) runs during the geometry phase (before HDR
lighting). It renders a screen-space velocity buffer by transforming each vertex through
the current and previous frame's view-projection matrices and outputting the difference.

- **Shader**: `velocity_ps.hlsl`
- **Output format**: RG16F (screen-space velocity in [-1, 1] per axis)
- **Consumer**: Future TAA pass (planned Phase 3)

### Debug View Modes

`debug_view_ps.hlsl` supports multiple debug visualizations selectable at runtime.
See `docs/specs/rendering/debug-view-modes.md` for the complete list of modes.

## Key Files

| File | Purpose |
|---|---|
| `runtime/engine/renderer/passes/postprocess/postprocess_stack.h` | Stack descriptor and `AddPostProcessStack()` entry point |
| `runtime/engine/renderer/passes/postprocess/auto_exposure.h/cpp` | Histogram + adapt compute passes |
| `runtime/engine/renderer/passes/postprocess/velocity_pass.h/cpp` | Velocity buffer pass |
| `runtime/engine/renderer/passes/postprocess/postprocess_math.h` | Shared math helpers (ACES, luminance) |
| `runtime/shaders/runtime/postprocess/tonemap_ps.hlsl` | ACES tonemapper |
| `runtime/shaders/runtime/postprocess/bloom_downsample_ps.hlsl` | Bloom downsample + threshold |
| `runtime/shaders/runtime/postprocess/bloom_blur_h_ps.hlsl` | Horizontal Gaussian blur |
| `runtime/shaders/runtime/postprocess/bloom_blur_v_ps.hlsl` | Vertical Gaussian blur |
| `runtime/shaders/runtime/postprocess/bloom_composite_ps.hlsl` | Bloom composite onto scene |
| `runtime/shaders/runtime/postprocess/exposure_histogram_cs.hlsl` | Luminance histogram compute |
| `runtime/shaders/runtime/postprocess/exposure_average_cs.hlsl` | Percentile filter + temporal adapt |
| `runtime/shaders/runtime/postprocess/fxaa_ps.hlsl` | FXAA anti-aliasing |
| `runtime/shaders/runtime/postprocess/velocity_ps.hlsl` | Screen-space velocity |
| `runtime/shaders/runtime/debug/debug_view_ps.hlsl` | Debug visualization modes |

## UE5 Reference

| UzEngine System | UE5 Equivalent |
|---|---|
| `AutoExposure` | `FRCPassPostProcessEyeAdaptation`, `EyeAdaptation.usf` |
| Histogram | `FRCPassPostProcessHistogram`, `PostProcessHistogram.usf` |
| ACES tonemap | `FilmToneMap.usf`, `ACESFitted` function |
| Bloom | `FRCPassPostProcessBloom*`, `PostProcessBloom.usf` |
| FXAA | `FRCPassPostProcessFXAA`, FXAA.usf (Nvidia reference) |
| Velocity buffer | `FRCPassPostProcessMotionBlurVelocitySetup`, `VelocityCommon.ush` |

## Status

| Feature | Status |
|---|---|
| Auto-exposure histogram (64-bin) | Done |
| Auto-exposure temporal adaptation | Done |
| Manual EV override | Done |
| ACES tonemap | Done |
| Bloom (4 shader variants) | Done |
| FXAA | Done |
| Velocity buffer pass | Done |
| Debug view modes | Done |
| TAA (Halton-jittered temporal accumulation) | Planned (Phase 3) |
| Motion blur | Planned (Phase 3) |
| Screen-space lens flares | Planned |
| Color grading (LUT-based) | Planned (Phase 2) |
