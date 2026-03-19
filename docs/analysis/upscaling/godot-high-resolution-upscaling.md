<!-- doc-meta
title: Godot 4 High-Resolution Rendering and Upscaling Architecture
category: analysis
engine: godot
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: godot-architect
-->

## Overview

Analysis of how Godot 4 handles 4K and high-resolution rendering through viewport-level resolution scaling and upscaling techniques (bilinear, FSR 1.0, FSR 2.0, MetalFX). This document extracts architectural patterns from Godot's `servers/rendering/renderer_viewport.cpp`, `scene/main/viewport.h`, and the RenderingDevice abstraction, providing UzEngine with a reference implementation for multi-mode upscaling support.

## Context

UzEngine needs a 4K-ready rendering pipeline. Godot's approach is clean and educational: separation of viewport scaling configuration from upscaler implementation, priority-driven fallback logic, and temporal jitter management for multi-frame upscaling. This analysis enables UzEngine to adopt similar patterns while maintaining DX12-first architecture.

---

## Scaling Modes: 5 Variants

Godot defines 5 scaling modes in `ViewportScaling3DMode` enum:

| Mode | Type | Hardware | Notes |
|------|------|----------|-------|
| **BILINEAR** | Spatial | All | Simple linear interpolation, fastest fallback, no special requirements |
| **FSR 1.0** | Spatial | All | AMD FidelityFX Super Resolution: EASU + RCAS (2-pass compute) |
| **FSR 2.0** | Temporal | All | FSR2 with motion vectors, 8–36 jitter phases, requires velocity buffer |
| **MetalFX Spatial** | Spatial | Apple Silicon | Apple hardware upscaling (spatial variant) |
| **MetalFX Temporal** | Temporal | Apple Silicon | Apple hardware upscaling (temporal variant, 1.1–1.5× scale range) |

### Key Distinction: Spatial vs Temporal

**Spatial upscalers** (Bilinear, FSR 1.0, MetalFX Spatial):
- Process single frame independently
- No jitter needed
- Can coexist with TAA
- Scale factor: any value 0.25–2.0

**Temporal upscalers** (FSR 2.0, MetalFX Temporal):
- Require motion vectors (per-pixel velocity)
- Require jitter across frames
- **Cannot coexist with TAA** — Godot auto-disables TAA when temporal upscaler active
- MetalFX Temporal: scale range 1.1–1.5 (hardware-limited)
- FSR 2.0: scale range 0.33–2.0 (software-defined)

---

## Viewport-Level Configuration

### GDScript API

```gdscript
# Per-viewport
viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
viewport.scaling_3d_scale = 0.66  # Render at 66% resolution, upscale to native

# Global defaults (project settings)
rendering/scaling_3d/mode              # int, 0 = Bilinear (default)
rendering/scaling_3d/scale             # float, 0.25–2.0 (default 1.0)
rendering/scaling_3d/fsr_sharpness     # float, 0–2.0 (default 0.2)
```

### Platform-Specific Defaults

```cpp
// rendering_server.cpp: platform-specific enum strings
rendering/scaling_3d/mode              // Bilinear,FSR 1.0,FSR 2.2,...
rendering/scaling_3d/mode.ios          // Includes MetalFX options
rendering/scaling_3d/mode.macos        // Includes MetalFX options
```

iOS and macOS default to Bilinear on older hardware (MetalFX not guaranteed).

### Internal Resolution Calculation

Godot computes internal render resolution in `_configure_3d_render_buffers()`:

```cpp
// Viewport struct stores these
Size2i internal_size;   // Resolution at which 3D is rendered
Size2i size;            // Target/output resolution (display size)
float scaling_3d_scale; // User-configured scale factor
RSE::ViewportScaling3DMode scaling_3d_mode; // Which upscaler to use

// Calculation
int target_width = viewport->size.width;      // e.g., 3840 for 4K
int target_height = viewport->size.height;    // e.g., 2160 for 4K
int render_width, render_height;

switch (scaling_3d_mode) {
    case BILINEAR:
        // Clamp to [1, 16384]
        render_width = CLAMP(target_width * scaling_3d_scale, 1, 16384);
        render_height = CLAMP(target_height * scaling_3d_scale, 1, 16384);
        break;
    case FSR:
    case FSR2:
    case METALFX_*:
        // Allow fractional, but clamp minimum
        render_width = MAX(target_width * scaling_3d_scale, 1.0);
        render_height = MAX(target_height * scaling_3d_scale, 1.0);
        break;
}

viewport->internal_size = Size2(render_width, render_height);
```

**Example: 4K Display, 66% Scale with FSR2**
- Target: 3840 × 2160 (native 4K)
- Render: 3840 × 0.66 = 2534, 2160 × 0.66 = 1426 → effectively 1440p
- Upscale: FSR2 reconstructs to 3840 × 2160

**VRAM Savings**: ~2.25× reduction (0.66² ≈ 0.44 of native pixel count)

---

## FSR 1.0: Spatial Upscaling

### Implementation Details (`servers/rendering/renderer_rd/effects/fsr.cpp`)

FSR 1.0 is a two-pass compute shader:

**Pass 1: EASU (Edge Adaptive Spatial Upsampling)**
- Input: low-res color
- Output: upsampled color (intermediate)
- Logic: adaptive filtering based on edge detection

**Pass 2: RCAS (Robust Contrast Adaptive Sharpening)**
- Input: EASU output
- Output: final upscaled + sharpened texture
- Sharpness tunable: `fsr_sharpness` parameter (0–2.0, default 0.2)

### GPU Dispatch

```cpp
int dispatch_x = (target_size.x + 15) / 16;  // 16×16 threadgroup
int dispatch_y = (target_size.y + 15) / 16;
// Dispatch full target resolution; each thread upsamples one output pixel
```

### Shader Variants

```cpp
enum FSRShaderVariant {
    FSR_SHADER_VARIANT_NORMAL,    // Uses half-float (faster, modern GPUs)
    FSR_SHADER_VARIANT_FALLBACK,  // Uses full-float (compatible, older GPUs)
};

// Auto-selected at init
variant = RD::get_singleton()->has_feature(RD::SUPPORTS_HALF_FLOAT)
    ? NORMAL : FALLBACK;
```

### Sharpness Control

`FSRUpscalePushConstant::sharpness` is passed per-frame:
- 0.0 = no additional sharpening (blur-friendly)
- 0.2 = default (balanced)
- 2.0 = maximum sharpening (crisp but can alias)

**Godot default: 0.2** (configurable via `rendering/scaling_3d/fsr_sharpness`)

### Why No Motion Vectors?

FSR 1.0 is purely spatial: each output pixel is computed from neighboring input pixels in the current frame. No temporal history needed → no jitter, no velocity buffer.

---

## FSR 2.0: Temporal Upscaling

### Implementation Details (`servers/rendering/renderer_rd/effects/fsr2.h`, `.cpp`)

FSR 2.0 is a complex temporal reconstruction algorithm from AMD's `ffx-fsr2` library. Godot wraps the AMD SDK with 9 compute pass shaders:

1. **Depth Clip Pass**: Reproject depth, detect disocclusions
2. **Reconstruct Previous Depth Pass**: Build history depth pyramid
3. **Lock Pass**: Mark pixels where temporal reprojection succeeded
4. **Accumulate Pass**: Blend current frame + reprojected history (with optional sharpening variant)
5. **RCAS Pass**: Final sharpness enhancement
6. **Compute Luminance Pyramid Pass**: Build log-luminance for tonemapping
7. **Autogen Reactive Pass**: Auto-detect reactive pixels (particle VFX, UI)
8. **TCR Autogen Pass**: Temporal coherence ratio for weight adjustment
9. **Additional passes**: Fine-grained history management

### Jitter Management

FSR 2.0 requires **camera jitter**: sub-pixel camera perturbation applied each frame to sample different pixels on reprojection. Godot automatically computes jitter phase count:

```cpp
if (scaling_type == VIEWPORT_SCALING_3D_TYPE_TEMPORAL) {
    // FFX_FSR2_CONTEXT_DESCRIPTION uses this formula
    jitter_phase_count = uint32_t(8.0f * std::pow(float(target_width) / render_width, 2.0f));
}
```

**Examples:**
- scale=0.66 (1440p→4K): 8 phases → 8 frames to fully sample
- scale=0.5 (1080p→4K): 16 phases → 16 frames
- scale=0.33 (720p→4K): 36 phases → 36 frames

Jitter is applied to the camera's projection matrix; game camera movement is not jittered (only rendering).

### Motion Vector Requirement

FSR 2.0 **requires per-pixel velocity vectors**:
- GPU writes motion vectors during depth/normal rendering
- FSR2 reads velocity for temporal reprojection
- Velocity precision: typically 2×16-bit normalized (RG16)

Godot auto-enables motion vector generation when FSR2 is active. If motion vectors missing → upscaling artifacts (ghosting, temporal instability).

### Ring Buffer System (UBO Management)

FSR 2.0 uses `FFX_FSR2_CONTEXT` which requires persistent state across frames:

```cpp
struct FSR2Context {
    Scratch scratch;                    // Frame-specific GPU resources
    FfxFsr2Context fsr_context;         // AMD SDK state
    FfxFsr2ContextDescription fsr_desc; // Config
};

// Ring buffer for uniform buffers
#define FSR2_MAX_QUEUED_FRAMES      4
#define FSR2_MAX_UNIFORM_BUFFERS    4
#define FSR2_UBO_RING_BUFFER_SIZE   (FFX_FSR2_PASS_COUNT * 4)
```

Godot manages a ring of UBOs to avoid GPU stalls (4 frames of latency). Each pass indexes into the ring.

### TAA Incompatibility

```cpp
if (use_taa && (scaling_type == VIEWPORT_SCALING_3D_TYPE_TEMPORAL)) {
    // Temporal upscaler already handles jitter/accumulation
    WARN_PRINT_ONCE("FSR 2 or MetalFX Temporal is not compatible with TAA. Disabling TAA internally.");
    use_taa = false;
}
```

**Why?** Both TAA and FSR2 apply camera jitter and accumulate temporal samples. Running both would double-jitter and produce visual artifacts (shimmer, blurring). Godot prefers FSR2 (better quality for upscaling) and disables TAA.

### MSAA Incompatibility

```cpp
if (scaling_3d_mode == VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) {
    if (msaa_3d != VIEWPORT_MSAA_DISABLED) {
        WARN_PRINT_ONCE("MetalFX temporal upscaling does not support 3D MSAA. Disabling 3D MSAA internally.");
        msaa_3d = VIEWPORT_MSAA_DISABLED;
    }
}
```

**Why?** MSAA breaks temporal reprojection: MSAA applies per-sample jitter during rasterization, conflicting with FSR2's jitter-based upscaling. Solution: disable MSAA, rely on FSR2's temporal AA.

---

## Automatic Fallback Logic

Godot implements **priority-driven cascading fallbacks** to ensure upscaling always works:

### Priority Chain

```cpp
// 1. User requests MetalFX Temporal
if (scaling_3d_mode == VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) {
    if (!RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_TEMPORAL)) {
        // 2. Try MetalFX Spatial (hardware more common than temporal)
        if (RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_SPATIAL)) {
            scaling_3d_mode = VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL;
        } else {
            // 3. Fall back to FSR2 (works on all platforms)
            scaling_3d_mode = VIEWPORT_SCALING_3D_MODE_FSR2;
        }
    }
}

// 4. User requests MetalFX Spatial (spatial)
if (scaling_3d_mode == VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL) {
    if (!RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_SPATIAL)) {
        // 5. Fall back to FSR (spatial)
        scaling_3d_mode = VIEWPORT_SCALING_3D_MODE_FSR;
    }
}

// 6. Downsampling check (FSR/MetalFX not designed for scale > 1.0)
if (scaling_3d_is_not_bilinear && (scaling_3d_scale >= 1.0 + EPSILON)) {
    WARN_PRINT_ONCE("FSR 3D resolution scaling is not designed for downsampling. Falling back to bilinear...");
    scaling_3d_mode = VIEWPORT_SCALING_3D_MODE_BILINEAR;
}

// 7. FSR availability check
if (scaling_3d_is_not_bilinear && !upscaler_available) {
    WARN_PRINT_ONCE("FSR 3D resolution scaling is not available. Falling back to bilinear...");
    scaling_3d_mode = VIEWPORT_SCALING_3D_MODE_BILINEAR;
}

// 8. Rendering method check (GL_COMPATIBILITY doesn't support advanced upscaling)
if (scaling_3d_mode != BILINEAR && OS::get_singleton()->get_current_rendering_method() == "gl_compatibility") {
    scaling_3d_mode = VIEWPORT_SCALING_3D_MODE_BILINEAR;
    WARN_PRINT_ONCE("MetalFX and FSR upscaling are not supported in the Compatibility renderer...");
}
```

### FSR Availability

```cpp
viewport->fsr_enabled = !RSG::rasterizer->is_low_end() && !viewport->disable_3d;
```

FSR is only enabled on non-low-end GPUs (excludes weak integrated graphics). This prevents performance regressions on mobile/weak systems.

---

## Texture Mipmap Bias: Auto-Sharpness Compensation

When rendering at sub-native resolution, texture lookups become less sharp. Godot automatically applies **negative mipmap bias** to compensate:

```cpp
const float texture_mipmap_bias = std::log2(MIN(scaling_3d_scale, 1.0)) + viewport->texture_mipmap_bias;
```

**Examples:**
- scale=0.66: log₂(0.66) ≈ -0.6 → bias = -0.6 (texture lookup at 0.6 mip offset, slightly sharper)
- scale=0.5: log₂(0.5) = -1.0 → bias = -1.0 (one mip sharper)
- scale=1.0: log₂(1.0) = 0 → no bias (use user's mipmap_bias setting)

This is passed to all shaders via the constant buffer, so texture sampling automatically adapts.

### Anisotropic Filtering

Godot also allows configurable anisotropic filtering levels (2x, 4x, 8x, 16x; default 4x) to improve texture quality at oblique angles during upscaling.

---

## Default 4K Strategy

Godot **does not prescribe a single 4K approach** in code comments, but the architecture implies:

### Recommended Scaling Profiles

| Use Case | Scale | Upscaler | VRAM | Performance | Quality |
|----------|-------|----------|------|-------------|---------|
| Native 4K | 1.0 | None | 100% | 100% | Best (no reconstruction) |
| Balanced 4K | 0.66 | FSR2 | 44% | 144% | Excellent (temporal reconstruction) |
| Performance 4K | 0.5 | FSR2 | 25% | 400% | Very Good (16-phase temporal) |
| Mobile 4K | 0.33 | FSR2 | 11% | 900% | Good (36-phase temporal, aliasing risk) |

**For console-like consistency**: Use scale=0.66 + FSR2 across all platforms (DX12, Vulkan, Metal). Delivers 1440p internal rendering with quality upscaling to native display.

### Platform-Specific Guidance

**Desktop (Windows/Linux, RTX/RDNA)**:
- Default: FSR2 + scale=0.66
- Fallback: FSR1 if FSR2 causes issues
- Native 4K: If GPU headroom > 60% at FSR2

**Console-like (PlayStation/Xbox emulation)**:
- Fixed: scale=0.66, FSR2
- Rationale: matches console frame pacing (temporal stability over variable)

**Mobile (iOS)**:
- Default: MetalFX Temporal if iOS 17+ (has GPU support)
- Fallback: Bilinear (MetalFX not available on older iPhone/iPad)
- Scale: 0.5–0.66 (power constraints)

**Laptop (iGPU)**:
- Default: FSR1 (lighter than FSR2)
- Scale: 0.5 (battery life)
- Disable if TDP heating observed

---

## Architecture Patterns for UzEngine Adaptation

### 1. Upscaler as Pluggable Strategy

Godot uses `SpatialUpscaler` abstract base class:

```cpp
class SpatialUpscaler {
public:
    virtual const Span<char> get_label() const = 0;
    virtual void ensure_context(Ref<RenderSceneBuffersRD> p_render_buffers) = 0;
    virtual void process(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_source_rd_texture, RID p_destination_texture) = 0;
};
```

UzEngine can follow this pattern:
- `uz::UpscalerBase` interface
- `uz::BilinearUpscaler`, `uz::FSRUpscaler`, `uz::FSR2Upscaler` implementations
- Runtime selection based on device capability

### 2. Fallback as Policy, Not Emergency

Godot checks capabilities at viewport init (`viewport_initialize()`) and fails over gracefully. UzEngine should:
- Detect upscaler support once at engine startup (not per-frame)
- Store capability flags in `uz::RendererCapabilities`
- Select upscaler mode before render begins (not mid-frame)

### 3. Temporal Data (Motion Vectors) as First-Class Citizen

FSR2 requires motion vectors. Godot auto-enables motion vector generation when FSR2 is active:

```cpp
num_viewports_with_motion_vectors += (fsr2_active ? 1 : -1);
// Only render motion vectors if at least one viewport needs them
```

UzEngine should:
- Track which render passes need motion vectors
- Only generate velocity buffer if needed
- Gate motion vector rendering with a feature flag (`enable_motion_vectors`)

### 4. Jitter Phase Count as Configuration

Godot computes jitter phases based on scale:

```cpp
jitter_phase_count = uint32_t(8.0f * (target_width / render_width) * (target_width / render_width));
viewport->jitter_phase_count = jitter_phase_count;
```

UzEngine should:
- Store jitter phase count in viewport/camera config
- Apply Halton jitter to camera matrices (jitter every frame, cycle every N frames)
- Pass jitter index to render pass for validation

### 5. Mipmap Bias as Implicit Quality Control

Godot auto-adjusts texture sharpness based on render scale. UzEngine should:
- Compute `auto_mipmap_bias = log2(rendering_scale)`
- Pass to shader CB
- Allow user override via `user_mipmap_bias` setting

---

## Key Takeaways

1. **Five upscaling modes**: Bilinear (fallback), FSR1.0 (fast spatial), FSR2.0 (quality temporal), MetalFX variants (Apple-only). Temporal modes conflict with TAA and MSAA.

2. **Internal resolution as viewport property**: Each viewport independently sets `scaling_3d_scale` (0.25–2.0). Target is display size; render size is `target * scale`.

3. **Automatic fallback chain**: User selects preferred upscaler; engine falls back (MetalFX Temporal → Spatial → FSR2 → FSR1 → Bilinear) if not supported, with feature detection at init time.

4. **Motion vectors required for temporal**: FSR2 and MetalFX Temporal need per-pixel velocity. Godot auto-enables generation; engine must not render motion vectors unnecessarily (cost).

5. **Jitter is algorithmic, not artistic**: Jitter phase count = 8 × scale_ratio². Camera gets Halton/random sub-pixel jitter; game camera NOT jittered.

6. **Mipmap bias compensates automatically**: Negative bias at lower scales keeps textures sharp. Formula: `log2(MIN(scale, 1.0))`.

7. **TAA and temporal upscalers are mutually exclusive**: Both apply jitter; running both causes shimmer. Godot prefers upscaler quality over TAA.

8. **FSR availability checked at init, not per-frame**: Low-end GPU detection prevents wasted compute. Fallback to Bilinear if FSR unavailable.

---

## References

- Godot 4.x source: `/servers/rendering/renderer_viewport.h`, `/renderer_viewport.cpp`
- Viewport configuration: `/scene/main/viewport.h`, `/viewport.cpp`
- FSR 1.0 implementation: `/servers/rendering/renderer_rd/effects/fsr.h`, `fsr.cpp`
- FSR 2.0 integration: `/servers/rendering/renderer_rd/effects/fsr2.h`, `fsr2.cpp`
- Spatial upscaler interface: `/servers/rendering/renderer_rd/effects/spatial_upscaler.h`
- AMD FSR2 SDK: `/thirdparty/amd-fsr2/`
- Rendering enums: `/servers/rendering/rendering_server_enums.h` (ViewportScaling3DMode, etc.)

---

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Initial creation — analysis of Godot 4 viewport scaling, FSR/MetalFX integration, fallback logic, and 4K strategy |
