<!-- doc-meta
title: Exposure Defaults Analysis — UE5, Falcor, DLSS Interaction
author: Claude Code
date: 2026-04-06
status: complete
-->

# Exposure Defaults Analysis

Comparison of exposure systems in UE5, NVIDIA Falcor, and UzEngine, with analysis of how auto-exposure interacts with DLSS temporal upscaling.

## 1. UE5 Default Exposure Settings

UE5 uses the `PostProcessSettings` struct and `UPostProcessComponent` to control exposure. The system is EV100-based (ISO 100 standard).

### 1.1 Default Values (UE5 5.x)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `AutoExposureMethod` | `AEM_Histogram` | Histogram-based (not basic average) |
| `AutoExposureBias` | `1.0` | **EV100 bias. +1 EV = 2x brighter than neutral.** |
| `AutoExposureLowPercent` | `10.0` | Histogram low percentile cutoff |
| `AutoExposureHighPercent` | `90.0` | Histogram high percentile cutoff |
| `AutoExposureMinBrightness` | `0.03` | Min average luminance (cd/m2) |
| `AutoExposureMaxBrightness` | `8.0` | Max average luminance (cd/m2) |
| `AutoExposureSpeedUp` | `3.0` | Bright adaptation speed (f-stops/sec) |
| `AutoExposureSpeedDown` | `1.0` | Dark adaptation speed (f-stops/sec) |
| `AutoExposureCalibrationConstant` | `0.18` | 18% gray calibration (Reinhard key) |

### 1.2 UE5 Exposure Formula

UE5 computes exposure as:

```
avgLuminance = histogram percentile-filtered average
targetExposure = calibrationConstant / avgLuminance
finalExposure = targetExposure * pow(2, exposureBias)
```

With `AutoExposureBias = 1.0` (UE5 default), the final exposure is:
```
finalExposure = (0.18 / avgLum) * pow(2, 1.0) = (0.18 / avgLum) * 2.0
```

This means **UE5's default scene is 2x brighter than the pure Reinhard formula** because of the +1 EV bias.

### 1.3 Manual Exposure Mode

When auto-exposure is disabled, UE5 uses:

```
manualExposure = pow(2, exposureBias) = pow(2, 1.0) = 2.0
```

The manual exposure multiplier is `2.0` by default.

### 1.4 Pre-Exposure

UE5 applies "pre-exposure" before the HDR buffer to prevent FP16 overflow on extremely bright scenes. The pre-exposure value equals the previous frame's exposure. This is applied:
- In the base pass (vertex/pixel shaders multiply output by pre-exposure)
- Reversed in post-processing before tonemapping

For typical scenes (no extreme dynamic range), pre-exposure is close to the auto-exposure value and does not change the visual result.

## 2. NVIDIA Falcor Default Exposure

Falcor's `ToneMapper` pass uses a simpler model.

### 2.1 Falcor Defaults

| Parameter | Default | Source |
|-----------|---------|--------|
| `exposureCompensation` | `0.0` | EV offset on top of auto-exposure |
| `autoExposure` | `false` | Disabled by default |
| `exposureValue` (manual) | `0.0` | When auto-exposure off, EV100 = 0 |
| `filmSpeed` | `100` | ISO 100 |
| `whiteBalance` | `6500K` | D65 illuminant |
| `operator` | `Aces` | ACES filmic by default |

### 2.2 Falcor Manual Exposure Formula

When auto-exposure is off (default):

```cpp
// Falcor ToneMapping.ps.slang:
float exposure = 1.0 / computeLuminousExposure(ev100);
// where ev100 = exposureValue + exposureCompensation = 0.0 + 0.0 = 0.0

float computeLuminousExposure(float ev100) {
    return pow(2.0, ev100) * 12.5;  // ISO 12232 Ssat formula
}
// computeLuminousExposure(0) = 1.0 * 12.5 = 12.5
// exposure = 1.0 / 12.5 = 0.08
```

So **Falcor's default exposure multiplier is 0.08** (equivalent to exposing for a sunlit outdoor scene).

### 2.3 Falcor Auto-Exposure

When enabled, Falcor uses a luminance histogram (same approach as UE5):
- Key value: `0.042` (darker than Reinhard's 0.18 — tuned for Falcor's HDR range)
- Formula: `exposure = key / avgLuminance`
- Then `exposure *= pow(2, exposureCompensation)`

## 3. UzEngine Current Exposure State

### 3.1 Code Paths

**With auto-exposure active** (`frame_constants.cpp:353-357`):
```cpp
float autoExp = m_autoExposure->Execute(...);
perFrame.exposure = autoExp * cv_Exposure->value;
// cv_Exposure default = 2.0f  (r.Exposure CVar)
```

The auto-exposure compute shader (`exposure_average_cs.hlsl`) computes:
```hlsl
targetExposure = exposureBiasLinear * 0.18 / avgLum;
```

With `r.AutoExposure.Bias = 0.0` (default), `exposureBiasLinear = 1.0`, so:
```
autoExp = 0.18 / avgLum  (temporally adapted)
perFrame.exposure = (0.18 / avgLum) * 2.0
```

This matches UE5's default behavior (Reinhard key * 2x bias).

**Without auto-exposure** (`frame_constants.cpp:365-367`):
```cpp
perFrame.exposure = 1.0f;  // Neutral, ignores cv_Exposure CVar!
```

This is a **bug**: when auto-exposure is not running (init failure, first frames before HDR content, etc.), the fallback ignores the `r.Exposure` CVar entirely. The scene receives an exposure of 1.0 instead of 2.0.

### 3.2 CVar Defaults

| CVar | Default | Notes |
|------|---------|-------|
| `r.Exposure` | `2.0` | Manual exposure bias multiplied on top of auto-exposure |
| `r.Exposure.KeyValue` | `0.18` | Forwarded to CB but not used by auto-exposure shader |
| `r.Exposure.SpeedUp` | `3.0` | Forwarded to CB but hardcoded in shader |
| `r.Exposure.SpeedDown` | `1.0` | Same |
| `r.Exposure.LowPercent` | `10.0` | Same |
| `r.Exposure.HighPercent` | `90.0` | Same |
| `r.AutoExposure` | `true` | Master toggle |
| `r.AutoExposure.Bias` | `0.0` | EV offset (0 = neutral) |
| `r.AutoExposure.MinBrightness` | `0.1` | |
| `r.AutoExposure.MaxBrightness` | `4.0` | |

### 3.3 Constant Buffer Defaults

| Field | Default | File |
|-------|---------|------|
| `PerFrameCB::exposure` | `1.0f` | `constant_buffers.h:69` |
| `PerFrameParams::exposure` | `1.0f` | `per_frame_params.h:111` |

### 3.4 How Exposure is Consumed

**Tonemap shader** (`tonemap_ps.hlsl:164`):
```hlsl
color *= exposure;
```

The exposure multiplier is applied after bloom compositing but before the tone curve. This is the standard post-process exposure application point.

**Deferred lighting** does NOT apply exposure. The HDR buffer contains raw linear luminance values. Exposure is only applied at tonemap time.

## 4. DLSS and Exposure Interaction

### 4.1 DLSS Pre-Exposure Parameter

DLSS accepts three exposure-related parameters:

| NGX Parameter | Purpose | UzEngine Value |
|---------------|---------|----------------|
| `DLSS.Pre.Exposure` | Linear pre-exposure already applied to HDR input | `1.0` |
| `DLSS.Exposure.Scale` | Scale factor for internal exposure handling | `1.0` |
| `ExposureTexture` | 1x1 texture with current exposure (optional) | `nullptr` |

**`DLSS.Pre.Exposure`** tells DLSS what pre-exposure multiplier was applied to the HDR color input. DLSS divides by this value internally to recover the original HDR values before temporal accumulation, then re-applies it after. This prevents ghosting artifacts when exposure changes between frames.

### 4.2 UzEngine's Current DLSS Setup

From `dlss_upscaler.cpp:610-617`:
```cpp
setF("DLSS.Pre.Exposure", 1.0f);
setF("DLSS.Exposure.Scale", 1.0f);
setRes(NVSDK_NGX_Parameter_ExposureTexture, nullptr);
```

This is correct **only if** the HDR input to DLSS has no pre-exposure applied. In UzEngine, the deferred lighting pass writes raw HDR values (no exposure multiplication), so `PreExposure = 1.0` is correct.

### 4.3 When Pre-Exposure Matters

Pre-exposure is critical when:
1. The scene has extreme dynamic range (sun disk + dark interiors in same frame)
2. FP16 HDR buffers would overflow without pre-exposure
3. Auto-exposure changes rapidly between frames (DLSS needs to know the scale to avoid ghosting)

UE5 applies pre-exposure in the base pass (before HDR buffer write), then passes the value to DLSS/TSR via `PreExposure` parameter. This is why UE5 sets `DLSS.Pre.Exposure = previousFrameExposure`.

### 4.4 Recommended DLSS Pre-Exposure for UzEngine

Since UzEngine does NOT apply pre-exposure to the HDR buffer:
- **Keep `DLSS.Pre.Exposure = 1.0`** (current behavior is correct)
- If pre-exposure is added in the future (for FP16 overflow prevention), update this to match

However, providing the `ExposureTexture` (a 1x1 texture containing the current frame's exposure value) can improve DLSS quality by helping it handle exposure changes. This is optional but recommended.

## 5. Is UzEngine's Exposure Too Low?

### 5.1 With Auto-Exposure Active

When auto-exposure is working:
```
perFrame.exposure = (0.18 / avgLum) * 2.0
```

This matches UE5's default behavior. The scene brightness should be correct.

### 5.2 Without Auto-Exposure (Fallback)

When auto-exposure is not active (init failure, no HDR content yet):
```
perFrame.exposure = 1.0  // BUG: ignores r.Exposure CVar
```

This is **half as bright as it should be**. The correct fallback should be:
```cpp
perFrame.exposure = cv_Exposure->value;  // = 2.0 by default
```

### 5.3 Comparison Table

| Engine | Default Exposure Multiplier | Notes |
|--------|----------------------------|-------|
| UE5 | ~2.0 | `pow(2, ExposureBias=1.0)` |
| Falcor | ~0.08 | `1.0 / (pow(2,0) * 12.5)` — ISO 12232 normalization |
| UzEngine (auto on) | ~2.0 * (0.18/avgLum) | Matches UE5 |
| UzEngine (auto off) | 1.0 | **Too dark** — should be 2.0 |

Note: Falcor's 0.08 is not directly comparable because Falcor's light intensities are calibrated differently (physically-based cd/m2 values). The ratio between scene luminance and exposure is what matters, not the absolute exposure value.

## 6. Identified Issues

### 6.1 Fallback Exposure Ignores CVar (Bug)

**File**: `runtime/engine/renderer/frame/frame_constants.cpp:365-367`
```cpp
} else {
    perFrame.exposure = 1.0f;  // Should use cv_Exposure->value
}
```

**Fix**:
```cpp
} else {
    perFrame.exposure = cv_Exposure->value;
}
```

### 6.2 Auto-Exposure Shader Hardcodes Adaptation Speeds

The adaptation speeds (`g_SpeedUp`, `g_SpeedDown`, `g_LowPercent`, `g_HighPercent`) are hardcoded in `exposure_average_cs.hlsl` as `static const` values, despite CVars existing for them (`r.Exposure.SpeedUp`, etc.). The CVars are forwarded to the CB but never read by the shader.

**Impact**: Low. The hardcoded values match the CVar defaults. But CVar overrides will not take effect.

### 6.3 DLSS ExposureTexture Not Provided

Providing a 1x1 exposure texture to DLSS improves temporal stability during exposure transitions. Currently `nullptr`.

**Impact**: Low-medium. Only affects scenes with rapid brightness changes (entering/exiting tunnels).

## 7. Recommendations

### 7.1 Fix Fallback Exposure (Priority: High)

Change `frame_constants.cpp` line 367 from `1.0f` to `cv_Exposure->value`. This ensures the scene is correctly exposed even when auto-exposure is not yet active.

### 7.2 Wire CVar Adaptation Speeds to Shader (Priority: Low)

Pass `r.Exposure.SpeedUp/Down/LowPercent/HighPercent` as root constants to the average shader instead of using hardcoded values.

### 7.3 Provide DLSS ExposureTexture (Priority: Low)

Create a 1x1 R32_FLOAT texture, write `perFrame.exposure` to it each frame, and pass it as `NVSDK_NGX_Parameter_ExposureTexture`. This helps DLSS handle exposure transitions.

### 7.4 Keep r.Exposure Default at 2.0 (No Change)

The current default of `r.Exposure = 2.0` matches UE5's `AutoExposureBias = 1.0` (which is `pow(2, 1.0) = 2.0`). This is correct.

## 8. Summary

| Question | Answer |
|----------|--------|
| Is `exposure=1.0` causing dark output? | Yes, but only when auto-exposure is inactive (fallback path). The bug is in the fallback, not the default. |
| What should the default exposure be? | The CVar `r.Exposure=2.0` is correct. The CB field default of `1.0` is a reasonable initial value before auto-exposure kicks in. |
| Does DLSS need exposure changes? | No immediate changes needed. `PreExposure=1.0` is correct since UzEngine has no pre-exposure. Optional: provide ExposureTexture. |
| Does UzEngine match UE5? | Yes, when auto-exposure is active. The formula `(0.18 / avgLum) * 2.0` matches UE5's histogram method with +1 EV bias. |
