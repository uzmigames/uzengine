# HDR Pipeline & Auto-Exposure

<!-- doc-meta
title: HDR Pipeline & Auto-Exposure
category: reference
engine: uzengine
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: docs-operator
-->

## Overview

This document describes UzEngine's HDR (High Dynamic Range) rendering pipeline with dynamic auto-exposure (eye adaptation). The system renders internally in HDR (floating-point) and uses GPU-computed exposure values to tonemap to LDR (8-bit) for display.

## Architecture

The HDR pipeline operates in three stages:

```
┌─────────────────┐
│  HDR Rendering  │ (all passes render to 16F/32F targets)
│ Cook-Torrance   │ (can produce values 0.0 to +∞)
│ IBL + Direct    │
└────────┬────────┘
         │
┌────────▼────────────────────┐
│ Auto-Exposure Computation   │ (GPU histogram + percentile filter)
│ Two-pass system:            │ (outputs exposure multiplier)
│  1. exposure_histogram_cs   │ (64-bin log2 luminance histogram)
│  2. exposure_average_cs     │ (percentile filtering + adaptation)
└────────┬────────────────────┘
         │
┌────────▼────────────┐
│ Tonemap Pass        │ (ACES filmic operator)
│ HDR → LDR           │ (applies exposure, maps to 0–1)
└────────┬────────────┘
         │
┌────────▼──────┐
│  8-bit Output │ (swapchain, display)
└───────────────┘
```

## Auto-Exposure System

### Two-Pass GPU Histogram

Auto-exposure computes a temporally-stable exposure value (EV) via histogram-based metering.

#### Pass 1: Exposure Histogram

**File**: `engine/renderer/exposure/exposure_histogram_cs.hlsl`

Computes a **64-bin histogram** of scene luminance (log2 scale):

```cpp
struct ExposureHistogramDesc {
    rg::RgResourceId sceneColor;       // Input HDR scene
    rg::RgResourceId histogramUAV;     // Output: RWStructuredBuffer<uint> [64]
    uint32_t         histogramSrvIdx;  // SM6.6 bindless index
    RHIBufferHandle  histogramBuffer;  // Physical histogram buffer (SM5)
};
```

**Kernel overview**:

```hlsl
// exposure_histogram_cs.hlsl
[numthreads(16, 16, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchId.xy;

    // 1. Fetch scene color
    float3 color = sceneColor.Load(pixelPos);

    // 2. Convert to luminance (rec709)
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    lum = max(lum, 0.0001);  // Prevent log(0)

    // 3. Quantize to log2 histogram bin
    // histLogMin = -10 (luminance 2^-10 ≈ 0.001)
    // histLogMax = 20  (luminance 2^20 ≈ 1M)
    float logLum = log2(lum);
    int bin = clamp(int((logLum - histLogMin) / (histLogMax - histLogMin) * 64), 0, 63);

    // 4. Accumulate via atomic increment
    InterlockedAdd(histogram[bin], 1);
}
```

**Histogram layout**:

| Bin | log2 Luminance Range | Physical Luminance | Represents |
|-----|----------------------|-------------------|-----------|
| 0 | -10.0 to -9.4 | 0.001 to 0.0014 | Darkest (black) |
| 16 | -4.5 to -3.9 | 0.044 to 0.062 | Dark shadows |
| 32 | **1.0 to 1.6** | **2 to 3** | **Mid-gray (target)** |
| 48 | 6.5 to 7.1 | 90 to 140 | Bright (white walls) |
| 63 | 20.0+ | 1M+ | Overexposed (sky) |

**Thread optimization** (LDS accumulation):
- Thread group size: 16×16 = 256 threads
- Each thread group accumulates into shared LDS buffer (64 uints)
- Synchronize (`GroupMemoryBarrier`) before atomic write to global histogram
- Reduces contention on global UAV

#### Pass 2: Exposure Average

**File**: `engine/renderer/exposure/exposure_average_cs.hlsl`

Converts histogram to exposure value with temporal adaptation:

```cpp
struct ExposureAverageDesc {
    rg::RgResourceId exposureBuffer;     // RWStructuredBuffer<ExposureData> [2] (double-buffered)
    uint32_t         exposureSrvIdx;     // Bindless SRV index
    uint32_t         histogramSrvIdx;    // Bindless SRV index
    RHIBufferHandle  exposurePhysical;   // Physical buffer
    RHIBufferHandle  histogramPhysical;  // Physical histogram
    float            adaptationRate;     // Temporal smoothing (0.0–1.0)
};

struct ExposureData {
    float currentEV;           // Current exposure value
    float prevEV;              // Previous frame EV (for adaptation)
    float exposureMultiplier;  // 2^(-currentEV), applied in tonemap
};
```

**Compute kernel**:

```hlsl
// exposure_average_cs.hlsl
[numthreads(1, 1, 1)]
void main() {
    // 1. Percentile filtering: ignore darkest/brightest tails
    uint sampleCount = histogramSampleCount;
    uint darkTailCount = sampleCount / 25;    // Ignore bottom 4% (pure black)
    uint brightTailCount = sampleCount / 25;  // Ignore top 4% (overexposed)
    uint targetCount = sampleCount - darkTailCount - brightTailCount;

    // 2. Walk histogram, find luminance where cumulative = targetCount
    uint cumulative = 0;
    float targetLogLum = -10.0;  // default: darkest
    for (int bin = 0; bin < 64; ++bin) {
        cumulative += histogram[bin];
        if (cumulative >= targetCount) {
            // Interpolate within bin for finer precision
            float binLogLum = histLogMin + (bin + 0.5) / 64.0 * (histLogMax - histLogMin);
            targetLogLum = binLogLum;
            break;
        }
    }

    // 3. Compute target exposure (EV) to map targetLogLum to mid-gray (0.18)
    const float midGrayLogLum = log2(0.18);
    float targetEV = targetLogLum - midGrayLogLum;

    // 4. Temporal adaptation (exponential smoothing)
    float prevEV = exposureBuffer[0].prevEV;
    float adaptRate = 0.1 * deltaTime;  // 0.1 per second (configurable)
    float currentEV = lerp(prevEV, targetEV, adaptRate);

    // 5. Clamp to reasonable range and store
    currentEV = clamp(currentEV, -20.0, 20.0);  // EV -20 to +20
    exposureBuffer[1].currentEV = currentEV;
    exposureBuffer[1].exposureMultiplier = pow(2.0, -currentEV);
}
```

**Percentile filtering**: Ignores darkest 4% and brightest 4% to prevent outliers (pure black objects, sun) from dominating exposure. This is the "metering mode" — standard in photography.

**Temporal adaptation**: Exponential smoothing prevents eye adaptation from flickering frame-to-frame.

### AutoExposureSettings

Artist-facing exposure configuration:

```cpp
/// Controls dynamic auto-exposure (eye adaptation) behavior.
struct AutoExposureSettings {
    /// Manual exposure offset (EV). If > -99, auto-exposure is disabled.
    float manualExposure;           // Default: -99.0 (auto-exposure enabled)

    /// Minimum metered luminance (prevents boosting dark frames too much).
    /// Lower = brighter in shadows, higher = more contrast
    float minBrightness;            // Default: 0.001 (very dark shadows)

    /// Maximum metered luminance (prevents crushing bright areas).
    /// Lower = more aggressive clipping, higher = retains more highlights
    float maxBrightness;            // Default: 1048576.0 (very bright: log2 = 20)

    /// Histogram lower bound (log2 scale). Luminances below are ignored.
    float histLogMin;               // Default: -10.0

    /// Histogram upper bound (log2 scale). Luminances above are ignored.
    float histLogMax;               // Default: +20.0

    /// Speed of adaptation (temporal smoothing). Higher = faster response.
    /// Units: EV per second
    float adaptationRate;           // Default: 0.1 (moderate response)
};
```

**Field details**:

| Field | Default | Range | Purpose |
|-------|---------|-------|---------|
| `manualExposure` | -99.0 | -99.0 (auto) or -10 to +10 | Override EV; -99 = auto |
| `minBrightness` | 0.001 | 0.0001–0.1 | Floor luminance (shadows) |
| `maxBrightness` | 1M | 1.0–10M | Ceiling luminance (highlights) |
| `histLogMin` | -10 | -20 to 0 | Histogram lower bound |
| `histLogMax` | +20 | 0 to 20 | Histogram upper bound |
| `adaptationRate` | 0.1 | 0.01–1.0 | EV/second smoothing |

## Exposure Pipeline Integration

### Frame Update Flow

```cpp
void Renderer::RenderFrame() {
    // 1. Clear exposure buffer (readback from previous frame)
    auto exposure_data = exposureBuffer.Readback();  // Current EV from GPU

    // 2. Render all HDR geometry/lighting
    RenderGeometry(...);  // Cook-Torrance, IBL, shadows

    // 3. Compute histogram (1 compute dispatch)
    if (settings.manualExposure < -50) {  // Auto-exposure enabled
        Dispatch(exposure_histogram_pass, scene_color_srv);
    }

    // 4. Average histogram → compute EV (1 compute dispatch)
    Dispatch(exposure_average_pass, histogram_uav, exposure_buffer_uav);

    // 5. Upload exposure to PerFrameCB
    auto exposure = settings.manualExposure;
    if (exposure < -50) {
        exposure = exposure_data.currentEV;  // Use GPU-computed EV
    }
    perFrameCB.exposure = exposure;
    perFrameCB.exposureMultiplier = pow(2.0, -exposure);

    // 6. Tonemap pass (uses exposure from CB)
    RenderTonemap(...);  // ACES with exposure applied

    // 7. Present to screen
    swapchain.Present();
}
```

### Constant Buffer Integration

**PerFrameCB** receives computed exposure:

```cpp
struct PerFrameCB {
    // ... camera, lighting, etc ...
    float  exposure;             // Current EV (computed or manual)
    float  exposureMultiplier;   // 2^(-exposure), for efficiency
    float  tonemapWhitePoint;    // ACES whitepoint (typically 4.0–11.2)
};
```

### CPU Readback

Exposure buffer is double-buffered to avoid GPU stalls:

```cpp
struct ExposureDoubleBuffer {
    ExposureData frames[2];      // frame[0] = current (GPU reads), frame[1] = next
};

// After exposure_average_pass:
//   GPU writes to frames[1] (next frame's data)
// Before tonemap_pass:
//   CPU reads frames[0] (computed exposure from PREVIOUS frame)
// Swap:
//   frames[0] ← frames[1]; frames[1] reset to 0
```

This ensures no GPU stall (CPU never waits for GPU).

## Tonemap Pass

**File**: `engine/renderer/tonemap/aces_tonemap_cs.hlsl`

Single point in the pipeline where HDR → LDR conversion happens.

### ACES Tone Mapping

ACES (Academy Color Encoding System) filmic tone mapping operator:

```hlsl
// Simplified ACES (RRT + ODT combined)
float3 ACESTonemapped(float3 color, float exposure) {
    // 1. Apply exposure
    color *= pow(2.0, -exposure);

    // 2. ACES RRT (Reference Rendering Transform)
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    float3 numerator = color * (color * a + b);
    float3 denominator = color * (color * c + d) + e;
    float3 toneMapped = numerator / max(denominator, 0.0001);

    // 3. Clamp to valid range [0, 1]
    return saturate(toneMapped);
}
```

**Why ACES?**
- Industry standard (film, VFX, games)
- Graceful highlight rolloff (no hard clipping)
- Stable across color ranges (works in shadow, mid, highlight)
- No visible posterization

### Whitepoint Control

ACES has an adjustable whitepoint for creative control:

```cpp
struct AutoExposureSettings {
    float tonemapWhitePoint;  // Default: 4.0 EV (bright white target)
};
```

| Whitepoint | Effect | Use Case |
|-----------|--------|----------|
| 2.0 | Aggressive (dark, cinematic) | Moody/dark scenes |
| **4.0** | **Balanced (default)** | **General-purpose** |
| 8.0 | Bright (high-key) | Bright outdoor scenes |
| 11.2 | Very bright (bleached) | Blown-out artistic look |

**Integration**:

```hlsl
// Modify ACES to use whitepoint
float3 ACESWithWhitepoint(float3 color, float exposure, float whitepoint) {
    color *= pow(2.0, -exposure);

    // Scale ACES by whitepoint (higher = retain more highlights)
    float3 scaledColor = color / whitepoint;
    float3 tonemapped = ACES(scaledColor);

    return saturate(tonemapped);
}
```

## Manual Exposure Override

For scenes that don't need dynamic metering:

```cpp
AutoExposureSettings settings;

// Disable auto-exposure
settings.manualExposure = 2.0;  // Fixed EV = +2.0 (2× brighter than default)

// Or: auto-expose with different sensitivity
settings.manualExposure = -99.0;  // Enable auto-exposure
settings.adaptationRate = 0.5;    // Faster response
```

**When to use manual**:
- Menu screens (constant lighting)
- Cutscenes (cinematic control)
- Specific visual style (fixed moody lighting)

## Performance Characteristics

| Component | GPU Time | Bandwidth |
|-----------|----------|-----------|
| Histogram pass | 0.5–1.0ms | Full scene color read |
| Average pass | 0.1ms | 64 uint histogram read |
| Tonemap pass | 1.0–2.0ms | Scene color read + write |
| **Total** | **~2.0–3.5ms** | **Minimal cached** |

**Optimization notes**:
- Histogram uses LDS to reduce atomics contention
- Exposure buffer is single-pass (no ping-pong needed)
- Tonemap dispatch is standard full-screen compute

## Quality Settings

| Setting | CPU | Quality | Recommendation |
|---------|-----|---------|-----------------|
| **64-bin histogram** (default) | 0.5ms | ✓ Adequate | Standard |
| 256-bin histogram | 1.0ms | ✓ Better | High-quality |
| Discard outliers 4% | Standard | ✓ Good metering | Keep enabled |
| Adaptation rate 0.1 EV/s | Standard | ✓ Smooth | Typical |
| Adaptation rate 0.5 EV/s | Standard | ✓ Responsive | Action games |

## Common Issues

### Scene Too Bright (Overexposed)

**Symptom**: Entire scene is washed out, highlights blown out.

**Root cause**: Exposure value too negative (making scene brighter).

**Debug**:
```cpp
UZ_LOG(Rendering, Info, "Current EV = {}", exposure_data.currentEV);
// If value > 0, exposure is making scene brighter (wrong direction)
```

**Solution**:
```cpp
settings.manualExposure = -1.0;  // Lock to darker EV
// Or: increase minBrightness to cap histogram
settings.minBrightness = 0.01;  // Clamp min luminance higher
```

### Scene Too Dark (Underexposed)

**Symptom**: Scene is dark; auto-exposure doesn't brighten it.

**Root cause**: Exposure value too positive, or histogram ignoring valid luminance range.

**Solution**:
```cpp
settings.maxBrightness = 100.0;  // Allow brighter metering target
settings.adaptationRate = 0.5;    // Respond faster to lighting changes
```

### Exposure Flickering

**Symptom**: EV value oscillates frame-to-frame (visible flicker).

**Root cause**: `adaptationRate` too high (no temporal smoothing).

**Solution**:
```cpp
settings.adaptationRate = 0.05;  // Reduce from 0.1 to 0.05 EV/s (slower)
```

### Exposure Not Adapting in Transitions

**Symptom**: Moving from dark room to bright outdoor scene, exposure takes forever to adjust.

**Root cause**: `adaptationRate` too low.

**Solution**:
```cpp
settings.adaptationRate = 0.2;  // Increase from 0.1 to 0.2 EV/s
```

## Phase Roadmap

| Phase | Feature | Status |
|-------|---------|--------|
| 1.3 | Basic auto-exposure (64-bin histogram, temporal adaptation) | ✅ Done |
| 2.0 | Manual EV override for cutscenes | Planned |
| 2.5 | Metering mode selection (center-weighted, spot, etc.) | Planned |
| 3.0 | Eye adaptation curves (realistic pupil response) | Planned |
| 3.5 | Glow/bloom interaction with tonemap | Planned |

## Key Takeaways

- **HDR rendering** produces values 0.0 to +∞ from Cook-Torrance + IBL
- **Auto-exposure** uses GPU histogram (64 bins) + percentile filtering for stable metering
- **Temporal adaptation** smooths EV changes (typically 0.1 EV/second)
- **ACES tonemap** maps HDR to 8-bit LDR with graceful highlight rolloff
- **Manual override** disables auto-exposure for artistic control
- **Performance**: ~2–3ms GPU time (histogram + average + tonemap)

## References

- [21-renderer-passes.md](21-renderer-passes.md) — Complete pass listing
- [10-cook-torrance-brdf.md](10-cook-torrance-brdf.md) — PBR lighting math (generates HDR values)
- [ACES Documentation](https://acescentral.com) — Academy Color Encoding System standards

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Initial creation — comprehensive HDR pipeline and auto-exposure reference |
