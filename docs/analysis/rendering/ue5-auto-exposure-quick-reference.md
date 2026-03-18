<!-- doc-meta
title: UE5 Auto-Exposure Quick Reference
subtitle: Constants, formulas, and parameters at a glance
tags: [ue5-parity, auto-exposure, reference, checklists]
-->

# UE5 Auto-Exposure Quick Reference

## Constants (DO NOT INVENT)

| Name | Value | Source | Unit |
|------|-------|--------|------|
| HISTOGRAM_SIZE | 64 | PostProcessHistogramCommon.ush:87 | bins |
| HISTOGRAM_TEXEL_SIZE | 16 | PostProcessHistogramCommon.ush:90 | 64÷4 |
| Percentile Low | 10.0 | Scene.cpp:459 | % |
| Percentile High | 90.0 | Scene.cpp:460 | % |
| Middle Grey | 0.18 | PostProcessEyeAdaptation.usf:174 | (unitless) |
| LuminanceMin (Histogram mode) | exp2(-8.0) ≈ 0.00391 | PostProcessEyeAdaptation.cpp:474 | cd/m² |
| LuminanceMin (Basic mode) | 0.0001 | PostProcessEyeAdaptation.cpp:474 | cd/m² |
| Transition Distance | 1.5 | PostProcessEyeAdaptation.cpp:491 | f-stops |
| Frame Time Epsilon | 1/60 | PostProcessEyeAdaptation.cpp:503 | seconds |
| ISO Saturation (lens attenuation) | 0.78 | PostProcessEyeAdaptation.cpp:231 | ISO 12232:2006 |

## Histogram Defaults

### Non-Extended Mode (Legacy cd/m²)

| Parameter | Value | Source |
|-----------|-------|--------|
| HistogramLogMin | -8.0 | Scene.cpp:476 |
| HistogramLogMax | 4.0 | Scene.cpp:477 |
| AutoExposureMinBrightness | 0.03 | Scene.cpp:474 |
| AutoExposureMaxBrightness | 8.0 | Scene.cpp:475 |

### Extended Mode (EV100)

| Parameter | Value | Source |
|-----------|-------|--------|
| HistogramLogMin | -10.0 | Scene.cpp:469 |
| HistogramLogMax | 20.0 | Scene.cpp:470 |
| AutoExposureMinBrightness | -10.0 | Scene.cpp:467 |
| AutoExposureMaxBrightness | 20.0 | Scene.cpp:468 |

## Adaptation Defaults

| Parameter | Value | Unit | Source |
|-----------|-------|------|--------|
| SpeedUp | 3.0 | f-stops/sec | Scene.cpp:484 |
| SpeedDown | 1.0 | f-stops/sec | Scene.cpp:485 |
| ExposureBias | 0.0 | f-stops | Scene.cpp:483 |
| AutoExposureMethod | AEM_Histogram | — | Scene.cpp:458 |

## Formulas (FROM UE5 SOURCE ONLY)

### Histogram Position ↔ Log Luminance

```glsl
// histogram_position is 0..1
log_luminance = (histogram_position - bias) / scale
histogram_position = log_luminance * scale + bias
```
**Source:** PostProcessHistogramCommon.ush:57-76

### Luminance Calculation

```glsl
// Three methods available:
luminance = max(dot(color, weights), luminance_min)

// weights options:
// 0: (1/3, 1/3, 1/3)          // uniform (default)
// 1: (0.3, 0.59, 0.11)        // NTSC
// 2: rec709_factors()          // Rec.709 from color space
```
**Source:** PostProcessHistogramCommon.ush:49-52, PostProcessEyeAdaptation.cpp:547-562

### Target Exposure

```glsl
target_exposure = average_scene_luminance / 0.18
```
**Source:** PostProcessEyeAdaptation.usf:174

### Exposure Scale

```glsl
exposure_scale = (compensation_settings * compensation_curve) / target_exposure
```
**Source:** PostProcessEyeAdaptation.usf:191

### Adaptation (Blended Exponential + Linear)

If |log(target) - log(old)| > start_distance (1.5 f-stops):
```glsl
// Linear mode (for large differences)
delta = frame_time * speed
new = old < target ? min(target, old + delta) : max(target, old - delta)
```

Else:
```glsl
// Exponential mode (for small differences)
factor = 1.0 - exp2(-frame_time * speed)
new = old + (target - old) * factor * M
```

Where M = frame_time_eps / ((1.0 - exp2(-frame_time_eps * speed)) * start_time)

**Source:** PostProcessHistogramCommon.ush:176-213, PostProcessEyeAdaptation.cpp:503-505

### Percentile Filtering

```glsl
// sum all histogram buckets
hist_sum = sum(all_buckets)

// compute bounds
min_fraction = hist_sum * low_percent
max_fraction = hist_sum * high_percent

// accumulate luminance-weighted buckets in range
for i = 0..HISTOGRAM_SIZE-1:
    value = bucket[i]
    value = clamp(value, 0, min_fraction)    // skip darkest
    value = clamp(value, 0, max_fraction)    // skip brightest
    weighted_sum += log_luminance[i] * value

// average in log space
avg_log_luminance = weighted_sum / total_weight
return exp2(avg_log_luminance)
```

**Source:** PostProcessHistogramCommon.ush:130-156

## Luminance Weighting Methods

### Uniform (r.AutoExposure.LuminanceMethod = 0)
```
weights = (1/3, 1/3, 1/3)
```

### NTSC (r.AutoExposure.LuminanceMethod = 1)
```
weights = (0.3, 0.59, 0.11)
```

### Rec.709 / Working Color Space (r.AutoExposure.LuminanceMethod = 2)
```
weights = GetWorking().GetLuminanceFactors()
```

**Source:** PostProcessEyeAdaptation.cpp:547-562

## CVars (Tuneable)

| CVar | Default | Range | Purpose | Source |
|------|---------|-------|---------|--------|
| r.EyeAdaptation.MethodOverride | -1 | -2..3 | Override auto-exposure method | PostProcessEyeAdaptation.cpp:35 |
| r.EyeAdaptation.ExponentialTransitionDistance | 1.5 | any | f-stops before switching linear | PostProcessEyeAdaptation.cpp:46 |
| r.EyeAdaptation.LensAttenuation | 0.78 | any | ISO 12232:2006 constant | PostProcessEyeAdaptation.cpp:62 |
| r.EyeAdaptation.BlackHistogramBucketInfluence | 0.0 | [0,1] | Weight for black pixels | PostProcessEyeAdaptation.cpp:68 |
| r.AutoExposure.LuminanceMethod | 0 | 0..2 | Luminance weighting method | PostProcessEyeAdaptation.cpp:83 |
| r.AutoExposure.IgnoreMaterials | false | bool | Ignore material reflectance | PostProcessEyeAdaptation.cpp:76 |
| r.EyeAdaptation.VisualizeDebugType | 0 | 0..2 | Debug visualization mode | PostProcessEyeAdaptation.cpp:53 |

## Output Buffer Layout

Structured Buffer<float4>, 2 elements:

**[0] Main Exposure Data:**
- x: ExposureScale (apply to scene color before tonemapping)
- y: TargetExposureScale
- z: AverageSceneLuminance
- w: MiddleGreyExposureCompensation

**[1] Local Exposure:**
- x: AverageLocalExposure (for tone-mapping local contrast)
- y,z,w: unused

**Source:** PostProcessEyeAdaptation.usf:245-246

## Shader Entry Points

| Shader | Function | Threads | Purpose | Source |
|--------|----------|---------|---------|--------|
| PostProcessEyeAdaptation.usf | EyeAdaptationCS | [1,1,1] | Histogram adaptation (main path) | usf:230 |
| PostProcessEyeAdaptation.usf | BasicEyeAdaptationCS | [16,16,1] | Basic adaptation (fallback) | usf:306 |
| PostProcessEyeAdaptation.usf | BasicEyeAdaptationSetupPS | fullscreen | Compute luminance to alpha | usf:250 |

## Key Implementation Checklist

- [ ] Histogram bin count: 64 (do NOT change)
- [ ] Percentile filtering: 10th-90th (do NOT change)
- [ ] Middle grey: 0.18 (do NOT change)
- [ ] Luminance min: exp2(HistogramLogMin) or 0.0001 (do NOT invent)
- [ ] Adaptation speed up: 3 f-stops/sec (do NOT change)
- [ ] Adaptation speed down: 1 f-stop/sec (do NOT change)
- [ ] Transition distance: 1.5 f-stops (do NOT change)
- [ ] Luminance calculation: use one of 3 standard methods (do NOT invent)
- [ ] Pre-exposure: remove before histogram capture (do NOT forget)
- [ ] Exposure output: ExposureScale = compensation / target_exposure
- [ ] Meter mask: optional spatial weighting (low priority)
- [ ] Local exposure: optional histogram-based contrast (low priority)

---

**Version:** 1.0
**Last Updated:** 2026-03-18
**Companion Document:** [ue5-auto-exposure-histogram.md](ue5-auto-exposure-histogram.md)
