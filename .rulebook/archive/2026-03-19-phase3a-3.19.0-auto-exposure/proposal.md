# Auto Exposure (Eye Adaptation)

## Summary

Implement histogram-based auto exposure matching UE5's Eye Adaptation system. Computes scene luminance distribution via GPU histogram, finds target exposure value (EV), and smoothly adapts over time — simulating how the human eye adjusts to brightness changes (dark room → bright outdoors).

## Motivation

Without auto exposure, HDR scenes either clip highlights (overexposed outdoors) or crush shadows (underexposed indoors). Auto exposure dynamically adjusts the exposure multiplier so the scene is always properly exposed, regardless of lighting conditions. Essential for any game with indoor/outdoor transitions.

## UE5 Reference Implementation

### Algorithm (UE5 EyeAdaptation.usf / PostProcessEyeAdaptation.cpp)
- **Luminance histogram**: Compute 64-bin histogram of log2 luminance across the frame via compute shader (atomic adds per bin).
- **Percentile filtering**: Find the luminance at the low/high percentile (default: ignore darkest 70% and brightest 2%). This prevents dark sky or bright sun from dominating exposure.
- **Target EV**: Average the remaining histogram bins → target log2 luminance → target EV100.
- **EV clamping**: Clamp target EV to [MinEV, MaxEV] range (prevents over/under exposure in extreme scenes).
- **Temporal adaptation**: Exponential convergence: `currentEV = lerp(currentEV, targetEV, 1 - exp(-deltaTime / adaptSpeed))`. Separate speeds for brightening (SpeedUp) and darkening (SpeedDown).
- **Exposure multiplier**: `exposure = 1.0 / (1.2 * pow(2, currentEV))`. Applied before tone mapping.

### UE5 Parameters (FPostProcessSettings — Eye Adaptation)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `AutoExposureMethod` | Histogram | Histogram (precise) or Basic (average luminance) |
| `AutoExposureBias` | 1.0 | EV offset (artistic control, +1 = brighter) |
| `AutoExposureMinBrightness` | 0.03 | Min target luminance (clamp dark scenes) |
| `AutoExposureMaxBrightness` | 8.0 | Max target luminance (clamp bright scenes) |
| `AutoExposureSpeedUp` | 3.0 | Seconds to adapt brighter (eye opening) |
| `AutoExposureSpeedDown` | 1.0 | Seconds to adapt darker (eye closing, faster) |
| `AutoExposureLowPercent` | 70.0 | Ignore darkest N% of histogram (skip shadows) |
| `AutoExposureHighPercent` | 98.0 | Ignore brightest N% (skip specular/sun) |
| `AutoExposureHistogramLogMin` | -8.0 | Log2 luminance range min |
| `AutoExposureHistogramLogMax` | 4.0 | Log2 luminance range max |
| Metering mask | None (uniform) | Optional texture to weight center of screen |

### UE5 Pipeline Position
Runs before tone mapping. Exposure multiplier is applied to HDR scene color in the tonemap pass.

## UzEngine Implementation Plan

### Shader Files
- `exposure_histogram_cs.hlsl` — Compute: build 64-bin log2 luminance histogram via atomic adds
- `exposure_average_cs.hlsl` — Compute: percentile filter histogram → target EV → temporal adapt → output exposure

### Constant Buffer (register b2)
```hlsl
cbuffer AutoExposureParams : register(b2) {
    float  exposureBias;         // 1.0 UE5 default (EV offset)
    float  minBrightness;        // 0.03 UE5 default
    float  maxBrightness;        // 8.0 UE5 default
    float  speedUp;              // 3.0 seconds (brighten)
    float  speedDown;            // 1.0 seconds (darken)
    float  lowPercent;           // 70.0 (ignore dark %)
    float  highPercent;          // 98.0 (ignore bright %)
    float  histLogMin;           // -8.0 log2 range
    float  histLogMax;           // 4.0 log2 range
    float  deltaTime;            // Frame delta for adaptation
    float  manualExposure;       // Override (0 = auto mode)
    float  pad;
};
```

### Resources
- **Histogram buffer**: UAV, 64 × uint32 (atomic counters)
- **Exposure buffer**: UAV, 1 × float (current adapted exposure value, persists across frames)

### Quality Levels
- **Basic**: Average luminance (single compute dispatch, no histogram) → ~0.05ms
- **Histogram**: Full 64-bin histogram + percentile → ~0.1ms

## Impact

- **Shaders**: 2 new compute shaders
- **Render graph**: Histogram pass + average/adapt pass, before tonemap
- **Buffers**: 64×uint32 histogram + 1×float exposure (persistent)
- **VRAM**: ~256 bytes
- **Performance**: 0.05-0.1ms

## Breaking Changes

None — exposure multiplier feeds into existing tonemap pass.

## Acceptance Criteria

1. Walking from dark room to bright outdoors shows smooth exposure transition (eye adaptation)
2. speedDown=1s adapts faster than speedUp=3s (dark→bright is faster than bright→dark, matching biology)
3. Percentile filtering: looking at sun doesn't make everything else black
4. MinEV/MaxEV clamp prevents total black or total white
5. Manual exposure override disables auto and uses fixed EV
6. Demo: indoor→outdoor transition showing smooth adaptation, histogram debug view
