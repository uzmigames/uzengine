# Temporal Upscaling Guide

<!-- doc-meta
title: Temporal Upscaling and Dynamic Resolution
category: guide
engine: uzengine
theme: rendering
created: 2026-03-18
updated: 2026-03-18
status: active
author: docs-operator
-->

## Overview

UzEngine's temporal upscaling system decouples render resolution from display resolution, enabling variable-resolution rendering with seamless temporal reconstruction. Two algorithms are available:

- **TAA (Temporal Anti-Aliasing)**: Single-pass lightweight approach, compatible with native resolution only
- **TSR (Temporal Super Resolution)**: Multi-pass algorithm with adaptive blending, supports 50–200% screen percentage

Combined with the optional dynamic resolution controller, temporal upscaling enables responsive frame rate management on budget-constrained hardware without sacrificing visual quality.

This guide covers the complete system architecture, configuration, use cases, and scripting API.

## Architecture Overview

### Resolution Terminology

| Term | Definition | Example |
|------|-----------|---------|
| **Screen Percentage** | Render resolution relative to display resolution | 50% = 1280×720 render, 2560×1440 display |
| **Render Resolution** | Internal geometry rendering target | 1280×720 |
| **Display Resolution** | Backbuffer / presentation target | 2560×1440 |
| **Mipmap Bias** | Texture LOD adjustment to match render scale | log₂(screen%/100) |

When screen percentage < 100, geometry renders at a smaller size. The upscaler reconstructs to the display resolution using temporal history and motion vectors.

### ITemporalUpscaler Interface

The temporal upscaler is abstracted behind a C++ interface located at `runtime/engine/renderer/passes/postprocess/temporal_upscaler.h`:

```cpp
enum class UpscalerType : uint32_t {
    None = 0,  // No upscaling
    TAA  = 1,  // Single-pass TAA
    TSR  = 2,  // Multi-pass TSR (default)
    FSR2 = 3,  // AMD FSR2 (future)
    DLSS = 4,  // NVIDIA DLSS (future)
    XeSS = 5,  // Intel XeSS (future)
};

class ITemporalUpscaler {
public:
    virtual UpscalerType GetType() const noexcept = 0;
    virtual const char* GetName() const noexcept = 0;
    virtual float GetMinScreenPercentage() const noexcept = 0;
    virtual float GetMaxScreenPercentage() const noexcept = 0;

    virtual void Init(IRHIDevice* device, PipelineLibrary* pipelines,
                      CBRingAllocator* cbRing) = 0;
    virtual void Shutdown() = 0;
    virtual void OnResize(uint32_t displayWidth, uint32_t displayHeight) = 0;
    virtual void Execute(IRHICommandList* cmd, const UpscalerInput& input) = 0;
};

// Factory function
std::unique_ptr<ITemporalUpscaler> CreateTemporalUpscaler(UpscalerType type);
```

### Upscaler Input Structure

Each frame, the caller fills an `UpscalerInput` struct containing:

```cpp
struct UpscalerInput {
    // Bindless descriptor indices (heap handles)
    uint32_t sceneColorSrv;     // HDR scene color at render resolution
    uint32_t depthSrv;          // Depth buffer (R32F, reversed-Z)
    uint32_t velocitySrv;       // Velocity buffer (RG16F, screen-space pixels)
    uint32_t historyReadSrv;    // Previous frame's history (history resolve output)
    uint32_t historyWriteUav;   // Current frame's history target (output)

    // Texture handles (for resource barriers)
    RHITextureHandle historyReadTexture;
    RHITextureHandle historyWriteTexture;

    // Sub-pixel jitter (for stochastic sampling)
    float jitterX, jitterY;     // Range: [-0.5, 0.5] pixels

    // Resolution
    float renderWidth, renderHeight;    // Render target dimensions
    float outputWidth, outputHeight;    // History/display dimensions

    // Frame state
    bool firstFrame;            // True when no valid history exists
    float exposure;             // Auto-exposure value (1.0 if disabled)
};
```

The upscaler reads scene color and motion vectors at render resolution, applies temporal reconstruction logic, and writes to the history texture at display resolution.

## Screen Percentage System

### Configuration

Set the render resolution as a percentage of the display resolution:

```cpp
renderer->SetScreenPercentage(50.0f);   // Render at 50% of display resolution
renderer->SetScreenPercentage(100.0f);  // Native resolution (no upscaling)
renderer->SetScreenPercentage(200.0f);  // Supersampling (4× pixel count)
```

Valid range: **10–200%** (clamped).

### Resolution Calculation

Given a 2560×1440 display and 67% screen percentage:

```
renderWidth  = 2560 * 0.67 = 1715
renderHeight = 1440 * 0.67 = 965
```

### Mipmap Bias Compensation

Textures sampled in the geometry pass should have their LOD bias adjusted to account for the render scale. Without adjustment, a 50% render will sample LOD0 textures meant for full resolution, causing aliasing and wasted memory bandwidth.

**Mipmap Bias Formula:**
```hlsl
float mipmapBias = log2(screenPercentage / 100.0);  // Negative for undersampling

// Usage in pixel shader:
float3 albedo = diffuseTexture.SampleBias(linearSampler, uv, mipmapBias);
```

| Screen % | Mipmap Bias | Effect |
|----------|-------------|--------|
| 50% | −1.0 | Sample 1 mip level finer (higher frequency details) |
| 67% | −0.58 | Sample 0.58 mips finer |
| 100% | 0.0 | No adjustment (native resolution) |
| 150% | 0.58 | Sample 0.58 mips coarser (more blur, less bandwidth) |
| 200% | 1.0 | Sample 1 mip level coarser (more blur) |

Source: @source UE5 SceneRenderTargets.cpp:2500-2550 — texture LOD adjustment via `CVarMipmapBiasOffset`.

### Engine Comparison

| Engine | Approach | Support | Use Case |
|--------|----------|---------|----------|
| **UE5** | CVar-driven (r.ScreenPercentage, r.DynamicResolution) | 50–200% | Production AAA standard |
| **Unity** | Dynamic Resolution Scaler preset | 70–100% | Mobile-first |
| **Godot** | FSR2/DLSS via plugins | Depends on plugin | Extensible, not built-in |
| **UzEngine** | SceneRenderer API + ITemporalUpscaler | 10–200%, TSR/TAA | Developer-friendly, UE5-parity |

## TAA vs TSR Comparison

### TAA (Temporal Anti-Aliasing)

**Single-pass lightweight algorithm** designed for jitter-based anti-aliasing at native resolution.

| Property | Value |
|----------|-------|
| **Pass Count** | 1 |
| **GPU Time** | ~0.3 ms |
| **Supported Screen %** | 100% only (no upscaling) |
| **Blend Factor** | Fixed 0.04 (4% current frame, 96% history) |
| **History Clamping** | Basic YCoCg per-channel min-max |
| **Shading Rejection** | None (luminance history assumed valid) |
| **Spatial AA** | None |
| **Output** | Anti-aliased history texture at display resolution |

**Use TAA when:**
- Target hardware cannot afford multi-pass compute (mobile, old desktop GPUs)
- Running at native resolution only (no dynamic scaling)
- Rendering requires minimal temporal artifacts (fast motion acceptable)
- GPU budget is tight (~0.3 ms is critical)

**Limitations:**
- Cannot reconstruct from lower resolutions
- Fixed blend ratio leads to ghosting on fast-moving objects
- Minimal rejection of invalid history (bright pixels can persist 1+ frames)

### TSR (Temporal Super Resolution)

**Multi-pass compute pipeline** based on @source UE5 TemporalSuperResolution.cpp. Implements velocity dilation, shading rejection, and adaptive blending for robust reconstruction across variable screen percentages.

| Property | Value |
|----------|-------|
| **Pass Count** | 3 compute dispatches |
| **GPU Time** | ~0.8 ms |
| **Supported Screen %** | 50–200% |
| **Blend Factor** | Adaptive: `TheoricBlendFactor = 1 / (1 + 16 / resFractionSquared)` |
| **History Clamping** | YCoCg neighborhood min-max with HDR weighting |
| **Shading Rejection** | Luminance-based detection of new geometry/lighting |
| **Spatial AA** | Per-pixel spatial AA for rejected samples |
| **RCAS Sharpening** | Optional (controllable via tsrSharpness parameter) |
| **Output** | Reconstructed history texture at display resolution |

**Use TSR when:**
- Running at screen percentage ≠ 100% (upscaling or supersampling)
- Dynamic resolution is enabled (variable render scale each frame)
- Minimizing temporal artifacts is important (adaptive blend reduces ghosting)
- GPU can afford ~0.8 ms (standard for modern GPUs) |

**Three-Pass Pipeline:**

#### Pass 1: Dilate Velocity
```hlsl
// Location: tsr_resolve_cs.hlsl (TSR_PASS_DILATE_VELOCITY)
```

Searches a 3×3 neighborhood around each render pixel to find the closest surface (largest reversed-Z depth). Outputs the velocity vector from that closest pixel. This ensures foreground objects properly dilate their motion onto background edges, preventing silhouette artifacts.

**Algorithm:**
1. For each output pixel, load the center depth
2. Search all 9 neighbors in the 3×3 window
3. Track the pixel with the largest depth (reversed-Z = closest to camera)
4. Output that pixel's velocity in UV space (converted from screen-space pixels)

**Constant Buffer:** `TsrDilateCB` at b2
- Depth and velocity input SRV indices
- Output UAVs: dilated velocity (RG16F), closest depth (R32F)

Source: @source UE5 TSRDilateVelocity.usf:148-417 — MainCS.

#### Pass 2: Reject Shading
```hlsl
// Location: tsr_resolve_cs.hlsl (TSR_PASS_REJECT_SHADING)
```

Compares the current frame's luminance against the reprojected history using dilated velocity. Outputs a per-pixel rejection weight: 0 = history is a perfect match (trust it), 1 = history is fully rejected (use current frame).

**Algorithm:**
1. Read current frame RGB and convert to YCoCg color space
2. Gather 3×3 neighborhood, compute min/max AABB in YCoCg
3. Reproject history by subtracting dilated velocity UV from current position
4. Sample reprojected history and compute distance from neighborhood AABB
5. Normalize distance to produce rejection weight ∈ [0, 1]

**Key Formulas:**

Adaptive blend factor (TheoricBlendFactor):
```glsl
float resFraction = screenPercentage / 100.0;
float TheoricBlendFactor = 1.0 / (1.0 + 16.0 / (resFraction * resFraction));
```

When screen % = 67%, resFraction = 0.67, TheoricBlendFactor ≈ 0.018 (bias strongly toward history).
When screen % = 100%, resFraction = 1.0, TheoricBlendFactor ≈ 0.059 (more blend toward current).

Source: @source UE5 TemporalSuperResolution.cpp:2858 — TheoricBlendFactor formula.

#### Pass 3: Update History
```hlsl
// Location: tsr_resolve_cs.hlsl (TSR_PASS_UPDATE_HISTORY)
```

Performs the final temporal resolve: combines current and reprojected history using adaptive blending, neighborhood clamping, and optional sharpening.

**Algorithm:**
1. Reproject history via dilated velocity to get best-match history color
2. Gather 4-neighborhood of history samples via bicubic filtering
3. Clamp history colors to current frame's YCoCg AABB
4. Blend: `output = lerp(history, current, blendFactor)`
5. If rejection weight is high, apply spatial AA from current frame's neighbors
6. Optional: Apply Contrast Adaptive Sharpening (RCAS) if tsrSharpness > 0

**Constant Buffer:** `TsrResolveCB` at b2
- Jitter offset (sub-pixel per-frame offset for stochastic sampling)
- TheoricBlendFactor (from pass 2)
- RCAS sharpness parameter ∈ [0, 1]

Source: @source UE5 TSRUpdateHistory.usf:142-148 — HistorySampleCount, HistoryHisteresis, WeightClamping.

### When to Switch

```lua
-- Low-end hardware (Intel HD, GTX 960)
uz.renderer.set_upscaler("taa")           -- 0.3 ms, no scaling

-- Mid-range (RTX 2060)
uz.renderer.set_upscaler("tsr")           -- 0.8 ms, 67-150% adaptive
uz.renderer.set_screen_percentage(67)

-- High-end (RTX 4080)
uz.renderer.set_upscaler("tsr")           -- 0.8 ms, 50% upscale for visual polish
uz.renderer.set_screen_percentage(50)
```

## Pipeline Order

The temporal upscaler sits **after deferred lighting** and **before post-processing**:

```
Geometry (render scale)
  ↓ (velocity buffer generated here)
Velocity Buffer Output
  ↓
GTAO / Ambient Occlusion (render scale)
  ↓
Deferred Lighting / Composition (render scale)
  ↓
Bloom Pyramid (render scale input)
  ↓
┌─────────────────────────────────────────┐
│  TAA / TSR Temporal Upscaler            │  ← Motion vectors applied here
│  Input: scene color (render scale)      │
│  Output: history (display scale)        │
└─────────────────────────────────────────┘
  ↓ (now at display resolution)
Motion Blur (display scale, uses velocity)
  ↓
Depth of Field (display scale)
  ↓
Color Grading LUT Lookup
  ↓
ACES Tonemap
  ↓
CAS Sharpening (optional, display scale)
  ↓
FXAA (optional, display scale)
  ↓
Backbuffer / Swapchain Present
```

**Key Ordering Rules:**

1. **Velocity** must be available (generated during geometry pass)
2. **Upscaler is first temporal pass** (motion blur after it uses velocity at display scale)
3. **Post-processing** happens at display resolution (cheaper than render scale)
4. **Bloom reads render-scale depth** (must happen before upscaling)

Source: Frame graph structure in `runtime/engine/renderer/core/frame_graph_passes.h`.

## Dynamic Resolution

The optional `DynamicResolutionController` automatically adjusts screen percentage each frame to maintain target FPS when GPU load changes.

### Algorithm

```cpp
// Pseudocode from DynamicResolutionController
class DynamicResolutionController {
    static constexpr float kScaleDownRatio = 0.95f;   // Scale down at 95% budget
    static constexpr float kScaleUpRatio = 0.85f;     // Scale up at 85% budget
    static constexpr float kScaleStep = 5.0f;         // 5% per step
    static constexpr uint32_t kScaleUpThreshold = 120; // 120 frames required

    float Update(float gpuFrameTimeSeconds) {
        if (gpuTime > targetFrameTime * kScaleDownRatio) {
            // GPU overloaded → immediate scale-down (5%)
            m_currentPct = max(m_minPct, m_currentPct - kScaleStep);
            m_framesUnderBudget = 0;
        }
        else if (gpuTime < targetFrameTime * kScaleUpRatio) {
            // GPU headroom → conservative scale-up (after 120 frames)
            m_framesUnderBudget++;
            if (m_framesUnderBudget >= kScaleUpThreshold) {
                m_currentPct = min(m_maxPct, m_currentPct + kScaleStep);
                m_framesUnderBudget = 0;
            }
        }
        else {
            // Dead zone (85%-95%) → no change, reset counter
            m_framesUnderBudget = 0;
        }
        return m_currentPct;
    }
};
```

**Scale-Down Behavior (Immediate):**
- When GPU > 95% of target frame time, reduce render scale by 5%
- Responds in 1 frame to prevent frame rate collapse
- Example: At 60 FPS target (16.67 ms), if GPU takes 16 ms, scale down next frame

**Scale-Up Behavior (Conservative):**
- When GPU < 85% of target frame time, increment counter
- After **120 consecutive under-budget frames**, increase render scale by 5%
- At 60 FPS, this is 2 seconds of headroom before attempting scale-up
- Prevents oscillation on GPU busy cycles

**Dead Zone:**
- GPU time between 85%-95% of target → no action
- Prevents thrashing when GPU is near the target

Source: @source UE5 DynamicRenderScaling.h — FDynamicRenderScaling, @source Unity DynamicResolutionHandler — scale-up hysteresis logic.

### Configuration

```cpp
DynamicResolutionController dr;
dr.SetEnabled(true);
dr.SetTargetFPS(60.0f);             // 16.67 ms target
dr.SetMinScreenPercentage(50.0f);   // Never below 50%
dr.SetMaxScreenPercentage(100.0f);  // Never above 100%

// Each frame in render thread:
float gpuTime = GetPreviousFrameGPUTime();  // From GPU timestamp
float nextScreenPct = dr.Update(gpuTime);
renderer->SetScreenPercentage(nextScreenPct);
```

Note: Dynamic resolution integration with SceneRenderer is pending (tracked as a future task).

## Quality Settings Guide

### TSR Parameters

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| **tsrSharpness** | [0, 1] | 0.5 | Contrast Adaptive Sharpening (RCAS) strength. 0 = disabled, 1 = maximum sharpening. Reduces blur from temporal blending. |
| **screenPercentage** | [50, 200] | 100 | Render resolution as % of display. Affects memory bandwidth, geometry workload, and blend factor. |
| **maxScreenPercentage** | [50, 200] | 100 | Upper bound for dynamic resolution controller. Prevents excessive supersampling. |
| **minScreenPercentage** | [10, 200] | 50 | Lower bound for dynamic resolution. Should match upscaler minimum. |

### Quality Levels (Future Enhancement)

Future releases will introduce quality presets via `r.TSR.History.UpdateQuality`:

```cpp
enum class TsrQuality : uint8_t {
    Low    = 0,   // Reduced sample count, simpler clamping (mobile)
    Medium = 1,   // Standard (current default)
    High   = 2,   // Extra history samples, tighter clamping
    Epic   = 3,   // Maximum quality (slower, highest VRAM)
};
```

For now, all modes use the standard quality level.

## Lua API

All temporal upscaling features are accessible via the Lua renderer API under the `uz.renderer` namespace.

### Set Screen Percentage

```lua
uz.renderer.set_screen_percentage(pct)
```

Sets the render resolution scale. Valid range: 10–200%. Clamped automatically.

**Arguments:**
- `pct` (number) — Screen percentage (e.g., 67.0 for 67%)

**Example:**
```lua
-- 67% upscale (balanced quality/performance)
uz.renderer.set_screen_percentage(67)

-- 50% strong upscale (aggressive optimization)
uz.renderer.set_screen_percentage(50)

-- 150% supersampling (visual quality over performance)
uz.renderer.set_screen_percentage(150)
```

### Get Screen Percentage

```lua
local pct = uz.renderer.get_screen_percentage()
```

Returns the current render scale percentage.

**Returns:**
- `pct` (number) — Current screen percentage

**Example:**
```lua
local current = uz.renderer.get_screen_percentage()
print("Current render scale: " .. current .. "%")
```

### Set Upscaler

```lua
uz.renderer.set_upscaler(name)
```

Switches the temporal upscaler algorithm. Takes effect on the next frame.

**Arguments:**
- `name` (string) — One of: `"none"`, `"taa"`, `"tsr"`

**Valid Upscalers:**
- `"none"` — Disable temporal upscaling (raw scene color at render resolution, scaled to display via nearest-neighbor)
- `"taa"` — Temporal Anti-Aliasing (single-pass, native resolution only)
- `"tsr"` — Temporal Super Resolution (multi-pass, 50–200% screen %)

**Example:**
```lua
-- Automatic quality selection based on GPU vendor
if uz.gpu.get_vendor() == "nvidia" then
    uz.renderer.set_upscaler("tsr")
    uz.renderer.set_screen_percentage(67)
elseif uz.gpu.get_vendor() == "amd" then
    uz.renderer.set_upscaler("tsr")
    uz.renderer.set_screen_percentage(75)
else
    -- Intel/mobile: TAA only
    uz.renderer.set_upscaler("taa")
end
```

### Get Upscaler

```lua
local name = uz.renderer.get_upscaler()
```

Returns the current upscaler name.

**Returns:**
- `name` (string) — Current upscaler: `"none"`, `"taa"`, or `"tsr"`

**Example:**
```lua
local upscaler = uz.renderer.get_upscaler()
if upscaler == "none" then
    print("Temporal upscaling disabled")
else
    print("Using " .. upscaler .. " upscaler")
end
```

### Set Dynamic Resolution

```lua
uz.renderer.set_dynamic_resolution(enabled)
```

Enables or disables GPU-driven adaptive resolution scaling. Currently a stub; full integration is pending.

**Arguments:**
- `enabled` (boolean) — True to enable, false to disable

**Note:** Returns a warning in the log. Integration with SceneRenderer is planned for a future release.

### Get Render Resolution

```lua
local width, height = uz.renderer.get_render_resolution()
```

Returns the internal render resolution (after screen percentage applied).

**Returns:**
- `width` (integer) — Render target width in pixels
- `height` (integer) — Render target height in pixels

**Example:**
```lua
local rw, rh = uz.renderer.get_render_resolution()
local dw, dh = uz.renderer.get_display_resolution()
local scale = rw / dw
print("Render: " .. rw .. "x" .. rh .. ", Display: " .. dw .. "x" .. dh)
print("Scale: " .. (scale * 100) .. "%")
```

### Get Display Resolution

```lua
local width, height = uz.renderer.get_display_resolution()
```

Returns the display/backbuffer resolution (window or swapchain size).

**Returns:**
- `width` (integer) — Display width in pixels
- `height` (integer) — Display height in pixels

**Example:**
```lua
local dw, dh = uz.renderer.get_display_resolution()
if dw >= 3840 then
    -- 4K: use aggressive upscaling
    uz.renderer.set_screen_percentage(50)
elseif dw >= 2560 then
    -- 1440p: balanced
    uz.renderer.set_screen_percentage(67)
else
    -- 1080p: minimal upscaling
    uz.renderer.set_screen_percentage(90)
end
```

## Plugin Integration (Future)

Vendor-specific upscalers (FSR2, DLSS, XeSS) will be added as plugins via the factory pattern:

```cpp
std::unique_ptr<ITemporalUpscaler> CreateTemporalUpscaler(UpscalerType::FSR2);
```

Each vendor will subclass `ITemporalUpscaler` and implement:
- GPU resource initialization (vendor SDK setup)
- Per-frame dispatch with vendor-specific constant buffers
- Shader parameter tuning (jitter patterns, sample counts, etc.)

Plugins are reserved for future integration; currently, `CreateTemporalUpscaler` returns a warning and falls back to TSR.

## Common Issues and Solutions

| Issue | Cause | Solution |
|-------|-------|----------|
| Ghosting (motion trails) | Blend factor too high (trusting history too much) | Increase `tsrSharpness`, lower screen %, or switch to TAA if at 100% |
| Flickering/temporal noise | Rejection weight oscillating | Ensure velocity buffer is correct; check for camera jitter inconsistencies |
| Blurry image at low screen % | Adaptive blend biasing toward history | Increase `tsrSharpness` (0.0→1.0) to apply RCAS sharpening |
| Black corners/off-screen reprojection | History sampled outside bounds | Normal at screen edges; clamp to texture bounds (handled in shader) |
| Banding in sky | Temporal blend quantizing color gradients | Use dithering in tonemap; consider 10-bit backbuffer format |

## Reference Implementation

TSR implementation files in UzEngine:

| File | Purpose |
|------|---------|
| `runtime/engine/renderer/passes/postprocess/temporal_upscaler.h` | Interface definition |
| `runtime/engine/renderer/passes/postprocess/tsr_upscaler.h/cpp` | TSR concrete implementation |
| `runtime/engine/renderer/passes/postprocess/taa_upscaler.h/cpp` | TAA concrete implementation |
| `runtime/shaders/runtime/postprocess/tsr_resolve_cs.hlsl` | TSR compute shader (3 passes) |
| `runtime/engine/renderer/core/dynamic_resolution.h` | Dynamic resolution controller |
| `runtime/engine/scripting/api/renderer/lua_renderer_api.cpp` | Lua bindings (set_screen_percentage, etc.) |

