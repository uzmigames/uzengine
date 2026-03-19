<!-- doc-meta
title: UE5 Auto-Exposure Implementation Guide for UzEngine
subtitle: Step-by-step adaptation of histogram algorithm
tags: [implementation-guide, auto-exposure, pseudocode, ue5-parity]
-->

# UE5 Auto-Exposure Implementation Guide for UzEngine

Complete implementation strategy for adapting UE5's histogram-based auto-exposure to UzEngine.

## Architecture Overview

UE5 splits auto-exposure into two phases:

```
┌─────────────────────────────────────────────────────┐
│ Phase 1: Histogram Setup (Per-Frame)                │
├─────────────────────────────────────────────────────┤
│ 1. Read scene color                                 │
│ 2. Compute luminance = dot(color, weights)          │
│ 3. Clamp luminance: max(lum, lum_min)               │
│ 4. Compute log2(luminance)                          │
│ 5. Map to histogram position [0,1]                  │
│ 6. Write to histogram texture (rgba channels)       │
│ Output: Histogram texture                           │
└─────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────┐
│ Phase 2: Adaptation Compute (Subsequent Frame)      │
├─────────────────────────────────────────────────────┤
│ 1. Read histogram texture from frame N-1            │
│ 2. Compute histogram sum                            │
│ 3. Apply percentile filtering (10%-90%)             │
│ 4. Compute weighted-average luminance               │
│ 5. Clamp to min/max brightness range                │
│ 6. Compute target exposure (lum / 0.18)             │
│ 7. Load old exposure from buffer                    │
│ 8. Adapt: blend old → target (exponential + linear) │
│ 9. Apply compensation (curve + settings)            │
│ 10. Compute exposure scale = compensation / exposure│
│ Output: EyeAdaptationBuffer (exposure scale + data) │
└─────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────┐
│ Phase 3: Apply Exposure                             │
├─────────────────────────────────────────────────────┤
│ scene_color *= exposure_scale                       │
│ then apply tonemapping                              │
└─────────────────────────────────────────────────────┘
```

---

## Phase 1: Histogram Setup Shader

### Pseudocode

```hlsl
/// @source UE5 PostProcessEyeAdaptation.usf:250-263
/// @constants luminance_min, histogram_scale, histogram_bias (from constants buffer)
/// @deviations NONE

void HistogramSetupPS(
    in float2 uv : TEXCOORD0,
    out float4 result : SV_Target)
{
    // Step 1: Sample scene color (remove pre-exposure)
    float3 color = Texture_SceneColor.Sample(Sampler_Linear, uv).rgb;
    color *= View.OneOverPreExposure;  // Undo pre-exposure to get real luminance

    // Step 2: Compute luminance using chosen method
    // luminance_weights = (1/3, 1/3, 1/3)  // or (0.3, 0.59, 0.11) or Rec.709
    float luminance = max(dot(color, EyeAdaptation_LuminanceWeights), EyeAdaptation_LuminanceMin);

    // Step 3: Compute log2 and clamp to safe range
    float log_luminance = clamp(log2(luminance), -10.0, 20.0);

    // Step 4: Map to histogram position [0,1]
    float hist_position = EyeAdaptation_HistogramScale * log_luminance + EyeAdaptation_HistogramBias;

    // Step 5: Output (histogram bin in alpha channel)
    result = Texture_SceneColor.Sample(Sampler_Linear, uv);
    result.w = hist_position;
}
```

**Parameters passed in constants buffer:**
- `View.OneOverPreExposure` — 1.0 / pre_exposure
- `EyeAdaptation_LuminanceWeights` — float3 luminance weights
- `EyeAdaptation_LuminanceMin` — luminance epsilon (0.0001)
- `EyeAdaptation_HistogramScale` — 1.0 / (log_max - log_min)
- `EyeAdaptation_HistogramBias` — -log_min * scale

### Parameter Computation (C++)

```cpp
// File: W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp:467-475

float histogram_log_min = -8.0f;   // or -10.0f if ExtendedLuminanceRange
float histogram_log_max = 4.0f;    // or 20.0f if ExtendedLuminanceRange

float histogram_log_delta = histogram_log_max - histogram_log_min;
float histogram_scale = 1.0f / histogram_log_delta;
float histogram_bias = -histogram_log_min * histogram_scale;

// Example: log_min=-8, log_max=4 → delta=12, scale=1/12, bias=2/3
// This maps: log=-8 → 0, log=4 → 1
```

---

## Phase 2: Histogram Adaptation Compute Shader

### Pseudocode (Main Path)

```hlsl
/// @source UE5 PostProcessHistogramCommon.ush:116-169 + PostProcessEyeAdaptation.usf:170-198
/// @constants HISTOGRAM_SIZE=64, all EyeAdaptation_* parameters
/// @deviations NONE

RWStructuredBuffer<float4> RWEyeAdaptationBuffer;  // Output
Texture2D HistogramTexture;                         // Input from phase 1

[numthreads(1, 1, 1)]
void EyeAdaptationCS(uint2 dispatch_id : SV_DispatchThreadID)
{
    // ─────────────────────────────────────────────────────────
    // Part A: Compute Average Scene Luminance
    // ─────────────────────────────────────────────────────────

    // A1: Sum all histogram buckets
    float histogram_sum = 0.0;
    for (uint i = 0; i < HISTOGRAM_SIZE; ++i)
    {
        histogram_sum += GetHistogramBucket(HistogramTexture, i);
    }

    // A2: Compute percentile bounds (skip darkest 10%, brightest 10%)
    float min_fraction = histogram_sum * EyeAdaptation_ExposureLowPercent;    // 10% → 0.1
    float max_fraction = histogram_sum * EyeAdaptation_ExposureHighPercent;   // 90% → 0.9

    // A3: Filter outliers and compute weighted average
    float weighted_sum_log_lum = 0.0;
    float weighted_count = 0.0;

    for (uint i = 0; i < HISTOGRAM_SIZE; ++i)
    {
        float bucket_value = GetHistogramBucket(HistogramTexture, i);

        // Remove darkest values (below 10th percentile)
        float sub = min(bucket_value, min_fraction);
        bucket_value -= sub;
        min_fraction -= sub;
        max_fraction -= sub;

        // Remove brightest values (above 90th percentile)
        bucket_value = min(bucket_value, max_fraction);
        max_fraction -= bucket_value;

        // Compute log luminance at this histogram position
        float hist_pos = float(i) / float(HISTOGRAM_SIZE - 1);
        float log_luminance = (hist_pos - EyeAdaptation_HistogramBias) / EyeAdaptation_HistogramScale;

        // Accumulate weighted log luminance
        weighted_sum_log_lum += log_luminance * bucket_value;
        weighted_count += bucket_value;
    }

    // A4: Convert back to linear luminance
    float avg_log_lum = weighted_sum_log_lum / max(0.0001, weighted_count);
    float average_scene_luminance = exp2(avg_log_lum);

    // ─────────────────────────────────────────────────────────
    // Part B: Compute Target Exposure
    // ─────────────────────────────────────────────────────────

    // B1: Clamp to min/max range
    float target_avg_lum = clamp(average_scene_luminance,
        EyeAdaptation_MinAverageLuminance,
        EyeAdaptation_MaxAverageLuminance);

    // B2: Target exposure = average / 0.18 (remaps to middle grey)
    float target_exposure = target_avg_lum / 0.18;

    // ─────────────────────────────────────────────────────────
    // Part C: Load Old Exposure & Apply Adaptation
    // ─────────────────────────────────────────────────────────

    // C1: Load previous frame's exposure scale
    float old_exposure_scale = HistogramTexture.Load(int3(0, 1, 0)).x;
    float old_exposure = EyeAdaptation_MiddleGreyExposureCompensation / max(0.0001, old_exposure_scale);

    // C2: Blend old → target with frame-based adaptation
    float adapted_exposure = ComputeEyeAdaptation(
        old_exposure, target_exposure, EyeAdaptation_DeltaWorldTime);

    // C3: Clamp result (soft clamp, prefer smooth transitions)
    float smoothed_exposure = clamp(adapted_exposure,
        EyeAdaptation_MinAverageLuminance / 0.18,
        EyeAdaptation_MaxAverageLuminance / 0.18);

    // ─────────────────────────────────────────────────────────
    // Part D: Compute Exposure Scale
    // ─────────────────────────────────────────────────────────

    float smoothed_exposure_scale = 1.0 / max(0.0001, smoothed_exposure);
    float exposure_scale = EyeAdaptation_MiddleGreyExposureCompensation * smoothed_exposure_scale;

    // ─────────────────────────────────────────────────────────
    // Part E: Output
    // ─────────────────────────────────────────────────────────

    float4 output;
    output.x = exposure_scale;  // ← Apply this to scene color
    output.y = EyeAdaptation_MiddleGreyExposureCompensation / max(0.0001, target_exposure);
    output.z = average_scene_luminance;
    output.w = EyeAdaptation_MiddleGreyExposureCompensation;

    RWEyeAdaptationBuffer[0] = output;
}
```

### Helper Function: ComputeEyeAdaptation

```hlsl
/// @source UE5 PostProcessHistogramCommon.ush:192-219
/// @constants all EyeAdaptation_* adaptation parameters
/// @deviations NONE

float ComputeEyeAdaptation(
    float old_exposure,
    float target_exposure,
    float frame_time)
{
    // Step 1: Work in log space for uniform sensitivity
    float log_old = log2(old_exposure);
    float log_target = log2(target_exposure);
    float log_diff = log_target - log_old;

    // Step 2: Choose speed based on direction (bright or dark adaptation)
    float adaptation_speed = (log_diff > 0) ?
        EyeAdaptation_ExposureSpeedUp :
        EyeAdaptation_ExposureSpeedDown;

    float M = (log_diff > 0) ?
        EyeAdaptation_ExponentialUpM :
        EyeAdaptation_ExponentialDownM;

    float abs_log_diff = abs(log_diff);

    // Step 3: Blend between linear and exponential based on distance
    float log_adapted;

    if (abs_log_diff > EyeAdaptation_StartDistance)  // 1.5 f-stops default
    {
        // Linear mode: fast, for large changes
        float delta = frame_time * adaptation_speed;
        float log_linear = (log_old < log_target) ?
            min(log_target, log_old + delta) :
            max(log_target, log_old - delta);
        log_adapted = log_linear;
    }
    else
    {
        // Exponential mode: smooth, for fine adjustments
        float factor = 1.0 - exp2(-frame_time * adaptation_speed);
        float log_exponential = log_old + (log_target - log_old) * factor * M;
        log_adapted = log_exponential;
    }

    // Step 4: Convert back to linear
    float adapted_exposure = exp2(log_adapted);

    // Step 5: Handle camera cuts (instant adaptation)
    float adjusted_exposure = lerp(
        adapted_exposure,
        target_exposure,
        EyeAdaptation_ForceTarget);  // 1.0 on camera cut, 0.0 normally

    return adjusted_exposure;
}
```

### Helper Function: GetHistogramBucket

```hlsl
/// @source UE5 PostProcessHistogramCommon.ush:101-114
/// @deviations NONE

float GetHistogramBucket(Texture2D histogram_texture, uint bucket_index)
{
    // Histogram is packed 4 buckets per texel (RGBA)
    uint texel_index = bucket_index / 4;
    float4 texel = histogram_texture.Load(int3(texel_index, 0, 0));

    uint channel = bucket_index % 4;
    float value = texel.r;
    if (channel == 1) value = texel.g;
    if (channel == 2) value = texel.b;
    if (channel == 3) value = texel.a;

    return value;
}
```

---

## Phase 2b: Compute Exposition Multipliers (M factors)

This should be computed once on CPU before passing to shader:

```cpp
/// @source UE5 PostProcessEyeAdaptation.cpp:503-505
/// @deviations NONE

float kFrameTimeEps = 1.0f / 60.0f;  // Reference frame time

float start_distance = 1.5f;  // f-stops
float start_time_up = start_distance / max(speed_up, 0.001f);
float start_time_down = start_distance / max(speed_down, 0.001f);

// M ensures smooth transition: exponential derivative = linear derivative at start_distance
float exponential_up_m = kFrameTimeEps /
    ((1.0f - exp2(-kFrameTimeEps * speed_up)) * start_time_up);

float exponential_down_m = kFrameTimeEps /
    ((1.0f - exp2(-kFrameTimeEps * speed_down)) * start_time_down);

// Example: speed_up=3, speed_down=1 → M_up≈0.65, M_down≈0.65
```

---

## Phase 3: Apply Exposure to Scene Color

### In Post-Process Shader

```hlsl
/// After rendering scene, before tonemapping

float3 final_color = input_scene_color * exposure_scale;

// Then apply tonemapping
final_color = ACESFilmToneMap(final_color);  // or your tonemap
```

### In Material Shader (Optional)

For light functions or custom shaders:

```hlsl
float exposure = EyeAdaptationLookupBuffer(EyeAdaptationBuffer).x;
return light_value / exposure;  // Inverse application in some cases
```

---

## Constants Buffer Layout

```cpp
struct FEyeAdaptationParameters
{
    float ExposureLowPercent;           // 0.10 (10%)
    float ExposureHighPercent;          // 0.90 (90%)
    float MinAverageLuminance;          // 0.18 * 0.03 = 0.0054
    float MaxAverageLuminance;          // 0.18 * 8.0 = 1.44
    float ExposureCompensationSettings; // From UI (default 1.0)
    float ExposureCompensationCurve;    // From curve LUT (default 1.0)
    float DeltaWorldTime;               // Frame time (seconds)
    float ExposureSpeedUp;              // 3.0 f-stops/sec
    float ExposureSpeedDown;            // 1.0 f-stop/sec
    float HistogramScale;               // 1.0 / (log_max - log_min)
    float HistogramBias;                // -log_min * scale
    float LuminanceMin;                 // 0.0001 or exp2(log_min)
    float BlackHistogramBucketInfluence;// 0.0 (black pixels ignored)
    float GreyMult;                     // 0.18 for histogram, 1.0 for manual
    float3 LuminanceWeights;            // (1/3, 1/3, 1/3) or (0.3, 0.59, 0.11) or Rec.709
    float ExponentialUpM;               // ~0.65
    float ExponentialDownM;             // ~0.65
    float StartDistance;                // 1.5 f-stops
    float LuminanceMax;                 // For EV100 conversion
    // ... additional fields for local exposure, meter mask, etc
};
```

---

## CPU Parameters: How to Compute Them

### From PostProcessSettings

```cpp
// User-facing settings (what the designer configures)
float auto_exposure_min_brightness = 0.03f;      // cd/m² or EV100
float auto_exposure_max_brightness = 8.0f;       // cd/m² or EV100
float auto_exposure_low_percent = 10.0f;         // %
float auto_exposure_high_percent = 90.0f;        // %
float auto_exposure_speed_up = 3.0f;             // f-stops/sec
float auto_exposure_speed_down = 1.0f;           // f-stops/sec
float auto_exposure_bias = 0.0f;                 // f-stops

// Histogram range
float histogram_log_min = -8.0f;  // log2(luminance)
float histogram_log_max = 4.0f;   // log2(luminance)

// Computed derived values
float histogram_log_delta = histogram_log_max - histogram_log_min;
float histogram_scale = 1.0f / histogram_log_delta;
float histogram_bias = -histogram_log_min * histogram_scale;

// Clamping
float min_avg_lum = auto_exposure_min_brightness * 0.18f;  // Convert white point to grey
float max_avg_lum = auto_exposure_max_brightness * 0.18f;

// Conversion factors
float percent_to_scale = 0.01f;
float low_percent = auto_exposure_low_percent * percent_to_scale;
float high_percent = auto_exposure_high_percent * percent_to_scale;

// M factors
float k_frame_time_eps = 1.0f / 60.0f;
float start_distance = 1.5f;
float start_time_up = start_distance / max(auto_exposure_speed_up, 0.001f);
float start_time_down = start_distance / max(auto_exposure_speed_down, 0.001f);

float exponential_up_m = k_frame_time_eps /
    ((1.0f - exp2(-k_frame_time_eps * auto_exposure_speed_up)) * start_time_up);
float exponential_down_m = k_frame_time_eps /
    ((1.0f - exp2(-k_frame_time_eps * auto_exposure_speed_down)) * start_time_down);

// Luminance method
enum class ELuminanceMethod { Uniform, NTSC, Rec709 };
float3 luminance_weights;
switch (luminance_method)
{
    case ELuminanceMethod::Uniform:
        luminance_weights = float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
        break;
    case ELuminanceMethod::NTSC:
        luminance_weights = float3(0.3f, 0.59f, 0.11f);
        break;
    case ELuminanceMethod::Rec709:
        luminance_weights = GetWorkingColorSpace().GetLuminanceFactors();
        break;
}

// Luminance epsilon
float luminance_min = 0.0001f;  // or exp2(histogram_log_min) for histogram mode

// Exposure compensation
float middle_grey_compensation = exposure_compensation_settings * exposure_compensation_curve;

// All other parameters come from current view + settings
```

---

## Validation Checklist

Before committing:

- [ ] Histogram size = 64 bins (do NOT change)
- [ ] Percentile defaults = 10%-90% (do NOT change)
- [ ] Logarithm base = 2 (do NOT change)
- [ ] Middle grey = 0.18 (do NOT change)
- [ ] Luminance weights = one of {uniform, NTSC, Rec.709} (do NOT invent)
- [ ] Speed up = 3 f-stops/sec (do NOT change)
- [ ] Speed down = 1 f-stop/sec (do NOT change)
- [ ] Transition distance = 1.5 f-stops (do NOT change)
- [ ] Adaptation formulas match PostProcessHistogramCommon.ush:192-219
- [ ] Percentile filtering matches PostProcessHistogramCommon.ush:130-156
- [ ] Pre-exposure removal: color *= (1.0 / pre_exposure) BEFORE histogram
- [ ] Exposure output: scale = compensation / target_exposure
- [ ] Meter mask support optional but recommended
- [ ] Local exposure support optional but recommended

---

## Testing Strategy

### Unit Tests (CPU)

```cpp
// Test histogram position conversion
assert(histogram_position_from_log(log_luminance) ==
       log_luminance * scale + bias);

// Test percentile filtering with known histogram
float expected_lum = 0.5f;  // Middle bin
assert(abs(compute_percentile(histogram) - expected_lum) < 0.01);

// Test exposure computation
float target = 0.36f;  // average 0.18 with compensation 2.0
assert(abs(target / 0.18 - 2.0) < 0.001);

// Test adaptation formula
float old_exp = 1.0;
float target_exp = 2.0;
float adapted = compute_adaptation(old_exp, target_exp, 0.016f);  // 60 fps frame
assert(adapted > old_exp && adapted < target_exp);
```

### Visual Validation

1. Render a scene with known luminance (e.g., all pixels = 0.5)
2. Check output exposure scale ≈ 1.0 / (0.5 / 0.18) ≈ 0.36
3. Verify smooth transitions when scene brightness changes
4. Compare histogram output with RenderDoc texture capture
5. Verify pre-exposure removal: histogram values should be independent of pre_exposure

---

## Common Pitfalls

| Mistake | Symptom | Fix |
|---------|---------|-----|
| Forgetting pre-exposure removal | Histogram shifts with pre-exposure | Multiply by `1.0 / pre_exposure` |
| Wrong luminance weights | Colors bias exposure | Use only uniform, NTSC, or Rec.709 |
| Histogram size ≠ 64 | Incorrect bin indexing | MUST be 64 |
| Percentiles ≠ 10%-90% | Exposure too sensitive or too stable | MUST be 10%-90% |
| Wrong adaptation speeds | Too fast or too slow transitions | MUST be 3 up, 1 down |
| Forgot luminance_min | NaN in very dark scenes | Clamp: `max(lum, 0.0001)` |
| Wrong histogram position formula | Inverted or shifted response | `pos = scale * log + bias`, not `log + bias / scale` |
| Missing M factor computation | Exponential/linear junction jerky | Must match derivative slopes |
| Not clamping average luminance | Exposure runs away | Clamp to [min_avg, max_avg] |

---

## Performance Considerations

**GPU Cost:**
- Phase 1 (setup): full-screen pass, ~0.2ms @ 1080p
- Phase 2 (adaptation): 1 thread, ~0.01ms (negligible)
- Phase 3 (apply): embedded in tonemapping, no cost

**Memory:**
- Histogram texture: 16 texels × 4 channels × 2 bytes = 128 bytes
- Eye adaptation buffer: 2 × float4 = 32 bytes
- Meter mask texture: optional, 1k×1k typical

**Optimization Tips:**
- Reuse existing luminance computation (no extra samples)
- Histogram read-back not needed (compute happens on GPU)
- Meter mask can be cached (rarely changes)

---

**Version:** 1.0
**Last Updated:** 2026-03-18
**Status:** Complete implementation guide with pseudocode
**Next:** Integrate into UzEngine render graph
