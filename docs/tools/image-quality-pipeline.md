# Image Quality Pipeline Spec

## 1. Overview & Goals

### The Image Quality Problem

Native-resolution rendering is expensive. A single 4K frame at 3840x2160 pushes 8.3 million pixels through the G-Buffer, lighting, and post-process stack -- each pixel requiring dozens of texture fetches, BRDF evaluations, shadow lookups, and screen-space effect computations. Even on high-end GPUs, maintaining 60 fps at native 4K with full PBR, ray-traced shadows, SSAO, SSR, and volumetric lighting is not feasible without compromise.

Temporal techniques trade clarity for cost: render fewer pixels per frame, accumulate information across multiple frames, and reconstruct a full-resolution image through intelligent upscaling. But temporal techniques introduce their own artifacts -- ghosting on moving objects, shimmering on thin geometry, blur on rapid camera motion, and frame pacing inconsistencies from frame generation.

The **Image Quality Pipeline** is UzEngine's unified system for managing this entire quality-vs-performance tradeoff through a single, coherent configuration surface.

### Unified Quality Pipeline

Rather than scattering anti-aliasing, upscaling, frame generation, and sharpening across independent systems, UzEngine consolidates them into one pipeline:

```
Rendered Frame (internal resolution)
    |
    v
[Anti-Aliasing] --- MSAA resolve / FXAA / TAA / SMAA
    |
    v
[Temporal Upscaling] --- FSR 3 / XeSS / DLSS / TAA Upscale / None
    |
    v
[Denoising] --- RT shadow / GI denoiser (REBLUR / RELAX style)
    |
    v
[Sharpening] --- CAS (Contrast Adaptive Sharpening)
    |
    v
[Frame Generation] --- DLSS FG / FSR FG / Engine-native interpolation / None
    |
    v
Output Frame (display resolution)
```

One configuration object (`ImageQualitySettings`) controls every stage. Changing a scalability preset updates all stages simultaneously. Individual stages can be overridden for fine-tuning.

### Target Users

| User | Interaction |
|------|-------------|
| **Graphics Engineer** | Implements solvers, tunes defaults, writes new AA/upscale backends. Works with C++ classes and HLSL shaders directly. |
| **Game Designer** | Selects scalability presets (Low/Medium/High/Ultra) that ship with the game. Tests each preset on target hardware. |
| **End User** | Adjusts quality settings from the in-game Settings menu. Picks upscaling method from a dropdown. Drags a screen percentage slider. Toggles frame generation. |
| **Technical Artist** | Uses the Editor Comparison Tool to evaluate quality differences between methods. Validates that TAA ghosting is acceptable for character animations. |

### Key Principle: Console Variable Exposure

**ALL image quality parameters MUST be exposed as console variables** (prefixed `r.`) so they can be read and written from:

1. In-game Settings menu UI (Lua `ctx.console:get/set`)
2. Console command window (`~` key)
3. `.uzcfg` configuration files
4. IPC commands from the VSCode editor
5. Scalability preset files

No image quality setting should exist only in C++ code with no runtime access path.

---

## 2. Anti-Aliasing Methods

### Method Summary

| Method | Quality | Perf Cost (1080p) | Ghosting | Motion Artifacts | Best For |
|--------|---------|-------------------|----------|------------------|----------|
| MSAA 2x | Low-Medium | ~0.8ms (geometry bound) | None | None | Forward-rendered scenes, simple geometry |
| MSAA 4x | Medium | ~1.5ms | None | None | Forward-rendered scenes, quality priority |
| MSAA 8x | High | ~3.0ms | None | None | Offline / screenshot mode only |
| FXAA | Low | ~0.15ms | None | Blurs edges slightly | Performance fallback, mobile targets |
| TAA | High | ~0.5ms | Medium (mitigated) | Jitter visible on pause | Default for deferred pipeline |
| SMAA Low | Medium | ~0.3ms | None | None | Better than FXAA, no temporal |
| SMAA High | Medium-High | ~0.6ms | None | None | Quality non-temporal option |
| SMAA T2x | High | ~0.8ms | Low | Minor | Temporal SMAA, alternative to TAA |

### Selection Logic

```
IF deferred pipeline enabled:
    Default = TAA (best quality-to-cost for deferred)
    Fallback = SMAA High (if user reports ghosting issues)
    Budget mode = FXAA (lowest cost)
ELSE IF forward pipeline:
    Default = MSAA 4x (hardware MSAA, no temporal)
    Quality = MSAA 8x (screenshot / offline)
    Budget = FXAA
```

---

### 2A. MSAA (Multisample Anti-Aliasing)

**Mechanism**: Hardware multisampling at 2x/4x/8x. Each pixel stores N sub-samples at different positions within the pixel footprint. The pixel shader executes once per pixel, but coverage and depth are evaluated per sub-sample. A resolve pass averages the sub-samples into the final pixel color.

**DX12 Implementation**:
- Create render targets with `DXGI_SAMPLE_DESC { Count = N, Quality = 0 }`
- Depth buffer must also be created with matching sample count
- Resolve via `ID3D12GraphicsCommandList::ResolveSubresource()` or a custom resolve shader
- G-Buffer targets in deferred mode do NOT support MSAA efficiently (4x G-Buffer = 4x bandwidth)

**When to use**: Forward rendering path only. MSAA on the deferred G-Buffer is prohibitively expensive (4 render targets x 4x samples = 16x memory and bandwidth). If the user selects MSAA with the deferred pipeline, the engine falls back to MSAA on the forward transparency pass only and uses TAA for the deferred portion.

**HLSL notes**: No shader changes required. MSAA is transparent to the pixel shader. The resolve pass is either hardware (`ResolveSubresource`) or a custom compute shader for tone-mapped resolve:

```hlsl
// shaders/post/msaa_resolve.hlsl
// Custom MSAA resolve that applies tonemapping BEFORE averaging sub-samples
// (prevents bright sub-samples from dominating the average)

#include "common.hlsli"

cbuffer ResolveParams : register(b2) {
    uint   msaa_sample_count;   // 2, 4, or 8
    float3 _pad;
};

Texture2DMS<float4> g_msaa_color : register(t0);
RWTexture2D<float4> g_resolved   : register(u0);

// Reinhard tonemap for per-sample weighting
float3 tonemap(float3 c)   { return c / (1.0 + luminance(c)); }
float3 untonemap(float3 c) { return c / (1.0 - luminance(c)); }

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float3 sum = float3(0, 0, 0);

    for (uint s = 0; s < msaa_sample_count; ++s) {
        float3 sample_color = g_msaa_color.Load(tid, s).rgb;
        sum += tonemap(sample_color);
    }

    float3 avg = sum / float(msaa_sample_count);
    g_resolved[tid] = float4(untonemap(avg), 1.0);
}
```

**Parameters**:

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.MSAA.Samples` | int | 0 | 0/2/4/8 | 0 = disabled |

---

### 2B. FXAA (Fast Approximate Anti-Aliasing)

**Mechanism**: Single full-screen pass that detects edges via luminance contrast and smooths them with directional blurring. Operates entirely in screen space on the final LDR image. No temporal component -- purely spatial.

**Quality**: Removes most jaggies but introduces slight softness on all edges. Thin geometry (wires, hair, foliage) can lose detail. Text and UI elements should be rendered AFTER FXAA to avoid blurring.

**Implementation**: Port of Timothy Lottes' FXAA 3.11 algorithm.

```hlsl
// shaders/post/fxaa.hlsl
// FXAA 3.11 — Fast Approximate Anti-Aliasing
// Single full-screen pixel shader pass on tonemapped LDR output.
// Reference: Timothy Lottes, NVIDIA, 2011

#include "common.hlsli"

cbuffer FXAAParams : register(b2) {
    float2 fxaa_texel_size;       // 1.0 / screen dimensions
    float  fxaa_subpix_quality;   // sub-pixel quality: 0.0 (off) to 1.0 (full)
    float  fxaa_edge_threshold;   // minimum edge detection threshold (0.063 default)
    float  fxaa_edge_threshold_min; // edges below this luminance are skipped (0.0312)
    float3 _pad;
};

Texture2D<float4> g_input  : register(t0);
SamplerState      sampler_linear_clamp : register(s0);

RWTexture2D<float4> g_output : register(u0);

// Compute perceptual luminance for edge detection
float fxaa_luma(float3 rgb) {
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * fxaa_texel_size;

    // Sample center and 4 neighbors
    float3 rgbM  = g_input.SampleLevel(sampler_linear_clamp, uv, 0).rgb;
    float3 rgbN  = g_input.SampleLevel(sampler_linear_clamp, uv + float2(0, -1) * fxaa_texel_size, 0).rgb;
    float3 rgbS  = g_input.SampleLevel(sampler_linear_clamp, uv + float2(0,  1) * fxaa_texel_size, 0).rgb;
    float3 rgbW  = g_input.SampleLevel(sampler_linear_clamp, uv + float2(-1, 0) * fxaa_texel_size, 0).rgb;
    float3 rgbE  = g_input.SampleLevel(sampler_linear_clamp, uv + float2( 1, 0) * fxaa_texel_size, 0).rgb;

    float lumaM = fxaa_luma(rgbM);
    float lumaN = fxaa_luma(rgbN);
    float lumaS = fxaa_luma(rgbS);
    float lumaW = fxaa_luma(rgbW);
    float lumaE = fxaa_luma(rgbE);

    // Range check: skip pixels with low local contrast
    float range_max = max(max(lumaN, lumaS), max(lumaW, max(lumaE, lumaM)));
    float range_min = min(min(lumaN, lumaS), min(lumaW, min(lumaE, lumaM)));
    float range     = range_max - range_min;

    if (range < max(fxaa_edge_threshold_min, range_max * fxaa_edge_threshold)) {
        g_output[tid] = float4(rgbM, 1.0);
        return;
    }

    // Determine edge direction (horizontal vs vertical)
    float luma_horizontal = abs(lumaN + lumaS - 2.0 * lumaM);
    float luma_vertical   = abs(lumaW + lumaE - 2.0 * lumaM);
    bool  is_horizontal   = luma_horizontal >= luma_vertical;

    // Sub-pixel anti-aliasing amount
    float subpix_filter = saturate(abs(
        (lumaN + lumaS + lumaW + lumaE) * 0.25 - lumaM
    ) / range);
    float subpix_blend = smoothstep(0.0, 1.0, subpix_filter);
    subpix_blend = subpix_blend * subpix_blend * fxaa_subpix_quality;

    // Blend along edge direction
    float2 step_dir = is_horizontal ? float2(fxaa_texel_size.x, 0) : float2(0, fxaa_texel_size.y);

    float3 rgbA = 0.5 * (
        g_input.SampleLevel(sampler_linear_clamp, uv - step_dir * 0.5, 0).rgb +
        g_input.SampleLevel(sampler_linear_clamp, uv + step_dir * 0.5, 0).rgb
    );

    float3 rgbB = rgbA * 0.5 + 0.25 * (
        g_input.SampleLevel(sampler_linear_clamp, uv - step_dir * 2.0, 0).rgb +
        g_input.SampleLevel(sampler_linear_clamp, uv + step_dir * 2.0, 0).rgb
    );

    float lumaB = fxaa_luma(rgbB);
    float3 result = (lumaB < range_min || lumaB > range_max) ? rgbA : rgbB;

    // Sub-pixel blend with center
    result = lerp(result, rgbM, 1.0 - subpix_blend);

    g_output[tid] = float4(result, 1.0);
}
```

**Parameters**:

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.FXAA.SubpixQuality` | float | 0.75 | 0.0 - 1.0 | Sub-pixel smoothing amount |
| `r.FXAA.EdgeThreshold` | float | 0.063 | 0.031 - 0.25 | Edge detection sensitivity |

---

### 2C. TAA (Temporal Anti-Aliasing)

TAA is the **default and recommended** anti-aliasing method for UzEngine's deferred pipeline. It accumulates sub-pixel information across frames using projection jitter and blends the current frame with a reprojected history buffer.

The core TAA resolve shader and jitter system are fully specified in `docs/specs/screen-space-effects.md` Section C. This document focuses on the **ghost reduction** improvements layered on top of that base implementation.

**HLSL implementation**: `shaders/post/taa.hlsl` (see screen-space-effects.md for full source)

**Key parameters** (all exposed as console variables):

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.TAA.BlendFactor` | float | 0.1 | 0.02 - 0.3 | Current frame weight (lower = more stable, more ghosting) |
| `r.TAA.VarianceClipGamma` | float | 1.0 | 0.5 - 2.0 | Neighborhood clamp strictness |
| `r.TAA.SharpenStrength` | float | 0.3 | 0.0 - 1.0 | Post-TAA sharpening intensity |
| `r.TAA.JitterSamples` | int | 8 | 4/8/16 | Halton sequence length |

---

### 2D. SMAA (Subpixel Morphological Anti-Aliasing)

**Mechanism**: Three-pass morphological approach that detects edges, computes blending weights using a precomputed area texture, and applies neighborhood blending. Higher quality than FXAA with comparable performance.

**Three passes**:
1. **Edge Detection** (PS or CS): Detect edges via luminance or color difference. Output binary edge texture (R8G8).
2. **Blending Weight Calculation** (PS): For each edge pixel, compute blending weights using the precomputed area and search textures. Output RGBA8 blending weights.
3. **Neighborhood Blending** (PS): Blend neighbors based on computed weights. Output final anti-aliased image.

**Precomputed textures** (shipped as engine assets):
- `smaa_area.dds` -- 160x560 R8G8 lookup table
- `smaa_search.dds` -- 64x16 R8 lookup table

**Quality presets**:

| Preset | Edge Threshold | Max Search Steps | Max Diag Search | Corner Rounding | Cost (1080p) |
|--------|---------------|------------------|-----------------|-----------------|--------------|
| Low | 0.15 | 4 | 0 | 0% | ~0.3ms |
| Medium | 0.10 | 8 | 0 | 25% | ~0.5ms |
| High | 0.10 | 16 | 8 | 25% | ~0.6ms |
| Ultra | 0.05 | 32 | 16 | 100% | ~1.0ms |

**Temporal variant (SMAA T2x)**: Alternates between two sub-pixel jitter positions (frame N at +0.25 px, frame N+1 at -0.25 px) and blends with the previous frame using motion-vector-guided reprojection. This provides TAA-like temporal stability without TAA's full neighborhood clamp system, resulting in less ghosting but also less effective sub-pixel detail recovery.

```hlsl
// shaders/post/smaa_edge_detect.hlsl
// Pass 1: Luminance-based edge detection for SMAA.
// Outputs a two-channel edge texture: R = horizontal edge, G = vertical edge.

#include "common.hlsli"

cbuffer SMAAParams : register(b2) {
    float  smaa_edge_threshold;     // luminance threshold for edge detection
    float  smaa_local_contrast_adapt; // local contrast adaptation factor (2.0)
    float2 smaa_texel_size;         // 1.0 / screen dimensions
    float2 smaa_screen_size;        // screen dimensions in pixels
    float2 _pad;
};

Texture2D<float4> g_color_input : register(t0);
SamplerState      sampler_linear_clamp : register(s0);

RWTexture2D<float2> g_edges_out : register(u0);

float smaa_luma(float3 rgb) {
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * smaa_texel_size;

    // Sample luminance at center, left, top, right, bottom
    float L   = smaa_luma(g_color_input.SampleLevel(sampler_linear_clamp, uv, 0).rgb);
    float Ll  = smaa_luma(g_color_input.SampleLevel(sampler_linear_clamp, uv + float2(-1, 0) * smaa_texel_size, 0).rgb);
    float Lt  = smaa_luma(g_color_input.SampleLevel(sampler_linear_clamp, uv + float2(0, -1) * smaa_texel_size, 0).rgb);

    // Compute deltas
    float2 delta = abs(float2(L - Ll, L - Lt));

    // Threshold with local contrast adaptation
    float2 edges = step(smaa_edge_threshold, delta);

    g_edges_out[tid] = edges;
}
```

**Parameters**:

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.SMAA.Quality` | int | 2 | 0-3 | 0=Low, 1=Med, 2=High, 3=Ultra |
| `r.SMAA.Temporal` | int | 0 | 0/1 | Enable SMAA T2x temporal mode |

---

### Anti-Aliasing Method Console Variable

```
r.AA.Method
    0 = None (no anti-aliasing)
    1 = FXAA
    2 = TAA (default)
    3 = SMAA
    4 = MSAA 2x (forward only)
    5 = MSAA 4x (forward only)
    6 = MSAA 8x (forward only, screenshot mode)
```

---

## 3. TAA Deep Dive -- Ghost Reduction

Ghost reduction is the single most critical quality challenge for temporal anti-aliasing. Without robust rejection, moving objects leave visible trails, disoccluded regions show stale data, and lighting changes produce frame-lag smearing.

### 3A. Ghosting Causes

| Cause | Description | Severity |
|-------|-------------|----------|
| **Missing motion vectors** | Particles, volumetric effects, or transparent objects that do not write to the velocity buffer. History reprojection fails. | High |
| **Disocclusion** | Previously hidden geometry becomes visible (e.g., camera moves around a pillar). No valid history exists for the revealed pixels. | High |
| **Lighting changes** | A light turns on/off, shadow moves rapidly. History color no longer matches current illumination. | Medium |
| **Thin geometry** | Sub-pixel wires, fences, foliage stems. Jitter causes them to appear/disappear frame-to-frame. History shows them at wrong sub-pixel offset. | Medium |
| **Camera cut** | Teleport or cinematic cut. Entire history buffer is invalid. | Critical |

### 3B. Velocity Buffer

All opaque geometry writes per-pixel screen-space motion vectors to `GBuffer3` (`RG16F`):

```
velocity.xy = (curr_ndc.xy - prev_ndc.xy) * float2(0.5, -0.5)
```

Where:
- `curr_ndc` = `mul(curr_view_proj, mul(curr_model, float4(pos, 1.0)))` (perspective divide)
- `prev_ndc` = `mul(prev_view_proj, mul(prev_model, float4(pos, 1.0)))` (perspective divide)

Static geometry still writes velocity (from camera-only motion). Dynamic geometry writes velocity from both camera and object motion. Skinned meshes must use the skinned vertex position for both current and previous frames (requires double-buffering bone palettes).

### 3C. History Rejection Pipeline

The ghost reduction operates in 5 stages within the TAA resolve shader:

```
                      Current Frame (jittered)
                              |
                    [1. Neighborhood Stats]
                    Compute 3x3 min/max/mean/sigma
                    in YCoCg color space
                              |
                    [2. History Reprojection]
                    Sample prev frame at (uv - velocity)
                    using Catmull-Rom bicubic
                              |
                    [3. Variance Clipping]
                    Clip history color to AABB defined
                    by (mean +/- gamma * sigma) in YCoCg
                              |
                    [4. Disocclusion Detection]
                    Compare current depth vs reprojected depth.
                    If delta > threshold: mark disoccluded,
                    increase current-frame blend weight.
                              |
                    [5. Firefly Rejection]
                    Clamp history luminance to max
                    neighborhood luminance * 1.1
                              |
                    [6. Adaptive Blend]
                    blend = lerp(base_blend, 0.5, disocclusion_factor)
                    blend = lerp(blend, 0.8, velocity_magnitude_factor)
                    result = lerp(clipped_history, current, blend)
                              |
                         TAA Output
```

### 3D. Variance Clipping (AABB Clip in YCoCg)

This is the primary ghost rejection mechanism. It constrains the history sample to lie within the statistical range of the current frame's local neighborhood.

```hlsl
// Inside TAA resolve shader -- variance clipping section

// 1. Gather 3x3 neighborhood in YCoCg
float3 m1 = float3(0, 0, 0);  // first moment (mean)
float3 m2 = float3(0, 0, 0);  // second moment (mean of squares)
float3 nb_min = float3(1e6, 1e6, 1e6);
float3 nb_max = float3(-1e6, -1e6, -1e6);

[unroll]
for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
        int2 coord = clamp(int2(tid) + int2(x, y), int2(0,0), int2(screen_size) - 1);
        float3 s = rgb_to_ycocg(g_current_color[coord].rgb);
        m1 += s;
        m2 += s * s;
        nb_min = min(nb_min, s);
        nb_max = max(nb_max, s);
    }
}

m1 /= 9.0;
m2 /= 9.0;
float3 sigma = sqrt(abs(m2 - m1 * m1));

// 2. Build AABB from mean +/- gamma * sigma
//    gamma controls strictness: lower = more aggressive rejection
float3 aabb_min = m1 - variance_clip_gamma * sigma;
float3 aabb_max = m1 + variance_clip_gamma * sigma;

// 3. Clip history color toward the AABB center
//    Uses AABB clipping (not just clamping) -- clips along the line
//    from history toward the AABB center, stopping at the AABB boundary.
float3 history_ycocg = rgb_to_ycocg(history.rgb);
float3 aabb_center = 0.5 * (aabb_min + aabb_max);

// Vector from center to history point
float3 direction = history_ycocg - aabb_center;
float3 aabb_extent = 0.5 * (aabb_max - aabb_min);

// Compute the t parameter where the ray exits the AABB
float3 t_values = abs(aabb_extent / (direction + EPSILON));
float  t_clip = saturate(min(min(t_values.x, t_values.y), t_values.z));

// Clip toward center by t_clip
float3 clipped_ycocg = aabb_center + direction * t_clip;
float3 clipped_history = ycocg_to_rgb(clipped_ycocg);
```

### 3E. Disocclusion Detection

Detect pixels where the reprojected position falls on geometry that was not visible in the previous frame (e.g., camera revealed new surfaces behind an occluder).

```hlsl
// Sample depth at reprojected UV
float prev_depth_raw = g_prev_depth.SampleLevel(sampler_point_clamp, prev_uv, 0);
float curr_depth_raw = g_depth[tid];

// Linearize both depths for comparison
float prev_depth_linear = linearize_depth(prev_depth_raw, proj);
float curr_depth_linear = linearize_depth(curr_depth_raw, proj);

// Reproject current depth to previous frame for comparison
float4 curr_world = reconstruct_world_pos_h(uv, curr_depth_raw, inv_view_proj);
float4 prev_clip  = mul(prev_view_proj, curr_world);
float  expected_prev_depth = prev_clip.z / prev_clip.w;

// If the depth at the reprojected location differs significantly from
// what we expect, the pixel is disoccluded (history is invalid)
float depth_diff = abs(prev_depth_raw - expected_prev_depth);
float disocclusion = smoothstep(0.001, 0.01, depth_diff);
```

### 3F. Firefly Rejection

Prevent bright outlier pixels in the history from propagating indefinitely (e.g., specular highlights on a moving object):

```hlsl
// Clamp history luminance to 110% of the maximum neighborhood luminance
float max_nb_luma = luminance(ycocg_to_rgb(nb_max));
float history_luma = luminance(clipped_history);
if (history_luma > max_nb_luma * 1.1) {
    clipped_history *= (max_nb_luma * 1.1) / (history_luma + EPSILON);
}
```

### 3G. Anti-Flicker

Luminance-based weighting to reduce frame-to-frame flickering on near-static scenes:

```hlsl
// Weight current vs history based on luminance similarity
float luma_curr    = luminance(current.rgb);
float luma_history = luminance(clipped_history);
float luma_diff    = abs(luma_curr - luma_history) / (max(luma_curr, luma_history) + EPSILON);

// When luminance is very similar, prefer history (more stable)
// When luminance differs, prefer current (reject stale data)
float anti_flicker_weight = saturate(1.0 - luma_diff * 4.0);
float final_blend = lerp(blend, blend * 0.5, anti_flicker_weight);
```

### 3H. Camera Cut Detection

On camera cuts or teleports, the entire history buffer is invalid. The engine detects this on the CPU side:

```cpp
// src/renderer/post_process/taa.cpp
bool detect_camera_cut(const TAAState* taa, const FrameContext* ctx) {
    // If the camera moved more than 10 meters in one frame, treat as cut
    float3 delta = ctx->camera_pos - taa->prev_camera_pos;
    float distance = sqrtf(dot3(delta, delta));
    if (distance > 10.0f) return true;

    // If the camera rotated more than 45 degrees in one frame, treat as cut
    float dot_fwd = dot3(ctx->camera_forward, taa->prev_camera_forward);
    if (dot_fwd < 0.707f) return true;  // cos(45) = 0.707

    return false;
}
```

On cut detection, `taa_blend_factor` is temporarily set to 1.0 (use only current frame) for one frame, then exponentially decays back to the user's configured value over 4 frames.

### 3I. CAS + TAA Combination

AMD's Contrast Adaptive Sharpening applied after TAA provides a cleaner final result than TAA's built-in unsharp mask. The pipeline becomes: `TAA resolve (no built-in sharpen) -> CAS`. See Section 10 for CAS details.

---

## 4. Temporal Upscaling -- Screen Percentage

### Concept

Render the scene at X% of the output resolution, then upscale to native using a temporal upscaler. This reduces the pixel shader, texture sampling, and bandwidth cost proportionally to the render area reduction.

```
Output resolution:   1920 x 1080  (display)
Screen percentage:   67%
Internal resolution: 1286 x  724  (render)  <-- 45% fewer pixels
Upscale ratio:       1.49x
```

### Jitter Adaptation

When upscaling, the jitter pattern must cover a larger area relative to the internal pixel grid. At 50% screen percentage (2x upscale), each internal pixel maps to a 2x2 block of output pixels. The jitter pattern must sample all 4 sub-pixel positions within that block across consecutive frames.

| Screen Percentage | Upscale Ratio | Recommended Jitter Samples | Halton Base |
|-------------------|---------------|---------------------------|-------------|
| 100% (Native) | 1.0x | 8 | Halton(2,3) |
| 77% (Quality) | 1.3x | 8 | Halton(2,3) |
| 67% (Quality) | 1.5x | 16 | Halton(2,3) |
| 59% (Balanced) | 1.7x | 16 | Halton(2,3) |
| 50% (Performance) | 2.0x | 32 | Halton(2,3) |
| 33% (Ultra Perf) | 3.0x | 32 | Halton(2,3) |

### Internal Resolution Pipeline

When screen percentage < 100%, the following resources are allocated at internal resolution:

- All G-Buffer targets (GBuffer0-3)
- Depth buffer
- HDR color buffer
- SSAO buffers (at half of internal resolution)
- SSR buffers

The following resources remain at output resolution:

- TAA output / upscaler output
- Post-TAA sharpening output
- Final LDR output
- Frame generation buffers

---

## 5. AMD FSR 3 (FidelityFX Super Resolution)

### Architecture

FSR 3 consists of two independent modules:
1. **FSR 3 Upscaler** (temporal upscaling): Replaces TAA, takes jittered internal-resolution color + depth + motion vectors and produces native-resolution output.
2. **FSR 3 Frame Generation** (optical flow frame interpolation): Takes two consecutive upscaled frames + motion vectors and generates an intermediate frame. See Section 8.

### Quality Modes

| Mode | Screen % | Internal (at 1080p) | Internal (at 4K) | Visual Quality |
|------|----------|---------------------|-------------------|----------------|
| Native AA | 100% | 1920x1080 | 3840x2160 | Reference (AA only, no upscale) |
| Quality | 67% | 1280x720 | 2560x1440 | Near-native, minor softness |
| Balanced | 59% | 1130x636 | 2259x1271 | Good, some detail loss on thin geometry |
| Performance | 50% | 960x540 | 1920x1080 | Acceptable, visible upscale artifacts |
| Ultra Performance | 33% | 640x360 | 1280x720 | Aggressive, noticeable quality loss |

### Integration (DX12)

FSR 3 ships as part of the FidelityFX SDK. UzEngine integrates it as a vcpkg dependency (`ffx-sdk`).

```cpp
// src/renderer/upscale/fsr3_solver.cpp

#include <ffx_api/ffx_upscale.h>

struct FSR3Solver {
    ffxContext             upscale_context;
    ffxCreateContextDescUpscale create_desc;

    uint32_t render_width;   // internal resolution
    uint32_t render_height;
    uint32_t display_width;  // output resolution
    uint32_t display_height;
};

void fsr3_init(FSR3Solver* solver, ID3D12Device* device,
               uint32_t render_w, uint32_t render_h,
               uint32_t display_w, uint32_t display_h)
{
    solver->render_width  = render_w;
    solver->render_height = render_h;
    solver->display_width  = display_w;
    solver->display_height = display_h;

    solver->create_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    solver->create_desc.maxUpscaleSize = { display_w, display_h };
    solver->create_desc.maxRenderSize  = { render_w, render_h };
    solver->create_desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE
                              | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE
                              | FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    ffxCreateContext(&solver->upscale_context, &solver->create_desc.header, NULL);
}

void fsr3_dispatch(FSR3Solver* solver, ID3D12GraphicsCommandList* cmd,
                   ID3D12Resource* color,          // internal-res jittered HDR color
                   ID3D12Resource* depth,           // internal-res depth
                   ID3D12Resource* motion_vectors,  // internal-res R16G16 velocity
                   ID3D12Resource* output,          // display-res upscaled output
                   float delta_time_ms,
                   float jitter_x, float jitter_y,
                   float sharpness)
{
    ffxDispatchDescUpscale dispatch = {};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = cmd;

    dispatch.color         = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.depth         = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.motionVectors = ffxApiGetResourceDX12(motion_vectors, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.output        = ffxApiGetResourceDX12(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    dispatch.renderSize      = { solver->render_width, solver->render_height };
    dispatch.upscaleSize     = { solver->display_width, solver->display_height };
    dispatch.jitterOffset    = { jitter_x, jitter_y };
    dispatch.frameTimeDelta  = delta_time_ms;
    dispatch.sharpness       = sharpness;
    dispatch.enableSharpening = (sharpness > 0.0f);
    dispatch.reset           = false;  // set true on camera cut

    ffxDispatch(&solver->upscale_context, &dispatch.header);
}

void fsr3_shutdown(FSR3Solver* solver) {
    ffxDestroyContext(&solver->upscale_context, NULL);
}
```

**Editor preset dropdown**:
```
[FSR 3 Quality] dropdown: Native AA | Quality | Balanced | Performance | Ultra Performance
```

### Console Variables

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.FSR.Enable` | int | 0 | 0/1 | Enable FSR 3 upscaler |
| `r.FSR.QualityMode` | int | 1 | 0-4 | 0=NativeAA, 1=Quality, 2=Balanced, 3=Perf, 4=UltraPerf |
| `r.FSR.Sharpness` | float | 0.5 | 0.0-1.0 | RCAS sharpening after upscale |

---

## 6. Intel XeSS (Xe Super Sampling)

### Architecture

XeSS 1.3 is a motion-vector-based temporal upscaler. On Intel Arc GPUs, it uses dedicated XMX matrix hardware for the AI inference network. On other GPUs (NVIDIA, AMD), it falls back to DP4a (dot-product 4-element accumulate) shader instructions, which are widely supported on DX12 Shader Model 6.4+ hardware.

### Quality Modes

| Mode | Screen % | Internal (at 1080p) | Internal (at 4K) |
|------|----------|---------------------|-------------------|
| Ultra Quality Plus (AA) | 98% | 1882x1058 | 3764x2117 |
| Ultra Quality | 77% | 1478x832 | 2957x1663 |
| Quality | 67% | 1280x720 | 2560x1440 |
| Balanced | 57% | 1094x616 | 2189x1231 |
| Performance | 43% | 826x464 | 1651x929 |
| Ultra Performance | 29% | 557x313 | 1114x626 |

### Integration (DX12)

XeSS ships as a DLL (`libxess.dll`) distributed with the Intel XeSS SDK.

```cpp
// src/renderer/upscale/xess_solver.cpp

#include <xess/xess_d3d12.h>

struct XeSSSolver {
    xess_context_handle_t context;
    xess_d3d12_init_params_t init_params;

    uint32_t render_width;
    uint32_t render_height;
    uint32_t display_width;
    uint32_t display_height;
};

void xess_init(XeSSSolver* solver, ID3D12Device* device,
               uint32_t display_w, uint32_t display_h,
               xess_quality_settings_t quality)
{
    xess_result_t result = xessD3D12CreateContext(device, &solver->context);
    if (result != XESS_RESULT_SUCCESS) {
        log_error("XeSS: Failed to create context (error %d)", result);
        return;
    }

    solver->init_params.outputResolution = { display_w, display_h };
    solver->init_params.qualitySetting   = quality;
    solver->init_params.initFlags        = XESS_INIT_FLAG_MOTION_VECTORS_HIGH_RES
                                         | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE
                                         | XESS_INIT_FLAG_JITTERED_MV;

    result = xessD3D12Init(solver->context, &solver->init_params);
    if (result != XESS_RESULT_SUCCESS) {
        log_error("XeSS: Failed to initialize (error %d)", result);
        return;
    }

    // Query the recommended internal resolution for this quality mode
    xess_2d_t render_res;
    xessGetInputResolution(solver->context, &solver->init_params.outputResolution,
                           quality, &render_res);
    solver->render_width  = render_res.x;
    solver->render_height = render_res.y;
    solver->display_width  = display_w;
    solver->display_height = display_h;
}

void xess_dispatch(XeSSSolver* solver, ID3D12GraphicsCommandList* cmd,
                   ID3D12Resource* color, ID3D12Resource* depth,
                   ID3D12Resource* motion_vectors, ID3D12Resource* output,
                   float jitter_x, float jitter_y)
{
    xess_d3d12_execute_params_t exec = {};
    exec.pColorTexture         = color;
    exec.pVelocityTexture      = motion_vectors;
    exec.pDepthTexture         = depth;
    exec.pOutputTexture        = output;
    exec.jitterOffsetX         = jitter_x;
    exec.jitterOffsetY         = jitter_y;
    exec.inputWidth            = solver->render_width;
    exec.inputHeight           = solver->render_height;
    exec.resetHistory          = false;  // set true on camera cut

    xessD3D12Execute(solver->context, cmd, &exec);
}

void xess_shutdown(XeSSSolver* solver) {
    if (solver->context) {
        xessDestroyContext(solver->context);
        solver->context = NULL;
    }
}
```

### Console Variables

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.XeSS.Enable` | int | 0 | 0/1 | Enable XeSS upscaler |
| `r.XeSS.QualityMode` | int | 2 | 0-5 | 0=UQ+, 1=UQ, 2=Quality, 3=Balanced, 4=Perf, 5=UltraPerf |

---

## 7. NVIDIA DLSS 3.5

### Components

DLSS is a suite of three AI-powered features:

| Component | DLSS Version | Minimum GPU | Description |
|-----------|-------------|-------------|-------------|
| **Super Resolution** | 2.x | RTX 20 series | AI temporal upscaling using Tensor Cores |
| **Frame Generation** | 3.0 | RTX 40 series | Generates intermediate frames using optical flow |
| **Ray Reconstruction** | 3.5 | RTX 20 series | AI denoiser for ray-traced effects (replaces traditional denoisers) |

### Quality Modes (Super Resolution)

| Mode | Screen % | Internal (at 4K) | Notes |
|------|----------|-------------------|-------|
| DLAA | 100% | 3840x2160 | AI anti-aliasing at native res (no upscale) |
| Quality | 67% | 2560x1440 | Best balance |
| Balanced | 58% | 2227x1253 | Moderate quality |
| Performance | 50% | 1920x1080 | Aggressive upscale |
| Ultra Performance | 33% | 1280x720 | Maximum performance |

### Integration (DX12)

DLSS integrates through NVIDIA's NGX (NVIDIA Graphics Extensions) framework. The SDK includes `nvngx_dlss.dll` which must be distributed with the game.

```cpp
// src/renderer/upscale/dlss_solver.cpp

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

struct DLSSSolver {
    NVSDK_NGX_Handle*    dlss_handle;
    NVSDK_NGX_Parameter* ngx_params;

    uint32_t render_width;
    uint32_t render_height;
    uint32_t display_width;
    uint32_t display_height;

    bool  available;       // GPU supports DLSS
    bool  frame_gen_available;  // GPU supports Frame Generation
};

void dlss_check_support(DLSSSolver* solver, ID3D12Device* device) {
    // Initialize NGX
    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init(
        /* app_id */ 0,
        /* app_data_path */ L"./dlss_cache",
        device,
        /* feature_info */ NULL,
        NVSDK_NGX_Version_API
    );

    solver->available = NVSDK_NGX_SUCCEED(result);

    if (solver->available) {
        NVSDK_NGX_D3D12_GetCapabilityParameters(&solver->ngx_params);

        int dlss_supported = 0;
        solver->ngx_params->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlss_supported);
        solver->available = (dlss_supported != 0);

        int frame_gen_supported = 0;
        solver->ngx_params->Get(NVSDK_NGX_Parameter_FrameGeneration_Available, &frame_gen_supported);
        solver->frame_gen_available = (frame_gen_supported != 0);
    }
}

void dlss_init(DLSSSolver* solver, ID3D12GraphicsCommandList* cmd,
               uint32_t render_w, uint32_t render_h,
               uint32_t display_w, uint32_t display_h,
               NVSDK_NGX_PerfQuality_Value quality)
{
    if (!solver->available) return;

    solver->render_width   = render_w;
    solver->render_height  = render_h;
    solver->display_width  = display_w;
    solver->display_height = display_h;

    NVSDK_NGX_DLSS_Create_Params dlss_params = {};
    dlss_params.Feature.InWidth   = render_w;
    dlss_params.Feature.InHeight  = render_h;
    dlss_params.Feature.OutWidth  = display_w;
    dlss_params.Feature.OutHeight = display_h;
    dlss_params.Feature.Quality   = quality;
    dlss_params.InEnableOutputSubrects = false;

    NVSDK_NGX_D3D12_CreateFeature(cmd, NVSDK_NGX_Feature_SuperSampling,
                                   solver->ngx_params, &solver->dlss_handle);
}

void dlss_dispatch(DLSSSolver* solver, ID3D12GraphicsCommandList* cmd,
                   ID3D12Resource* color, ID3D12Resource* depth,
                   ID3D12Resource* motion_vectors, ID3D12Resource* output,
                   float jitter_x, float jitter_y,
                   float delta_time_ms, float sharpness,
                   bool reset)
{
    if (!solver->available || !solver->dlss_handle) return;

    NVSDK_NGX_D3D12_DLSS_Eval_Params eval_params = {};
    eval_params.Feature.pInColor            = color;
    eval_params.Feature.pInOutput           = output;
    eval_params.pInDepth                    = depth;
    eval_params.pInMotionVectors            = motion_vectors;
    eval_params.InJitterOffsetX             = jitter_x;
    eval_params.InJitterOffsetY             = jitter_y;
    eval_params.Feature.InSharpness         = sharpness;
    eval_params.InRenderSubrectDimensions   = { solver->render_width, solver->render_height };
    eval_params.InReset                     = reset;
    eval_params.InMVScaleX                  = (float)solver->render_width;
    eval_params.InMVScaleY                  = (float)solver->render_height;
    eval_params.InFrameTimeDeltaInMsec      = delta_time_ms;

    NVSDK_NGX_D3D12_EvaluateFeature(cmd, solver->dlss_handle, solver->ngx_params, &eval_params);
}

void dlss_shutdown(DLSSSolver* solver) {
    if (solver->dlss_handle) {
        NVSDK_NGX_D3D12_ReleaseFeature(solver->dlss_handle);
        solver->dlss_handle = NULL;
    }
    NVSDK_NGX_D3D12_Shutdown();
}
```

### DLSS Ray Reconstruction (3.5)

DLSS Ray Reconstruction replaces the traditional spatial+temporal denoiser for ray-traced effects (shadows, GI, reflections). Instead of separate REBLUR/RELAX denoising passes, the DLSS AI network jointly denoises and upscales in a single pass, producing cleaner results with fewer temporal artifacts.

**When available**: On RTX 20+ GPUs with DLSS 3.5 SDK, Ray Reconstruction can replace the engine-native denoiser described in Section 9. The engine falls back to the native denoiser on non-RTX hardware.

### Console Variables

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.DLSS.Enable` | int | 0 | 0/1 | Enable DLSS Super Resolution |
| `r.DLSS.QualityMode` | int | 1 | 0-4 | 0=DLAA, 1=Quality, 2=Balanced, 3=Perf, 4=UltraPerf |
| `r.DLSS.Sharpness` | float | 0.0 | 0.0-1.0 | DLSS sharpening |
| `r.DLSS.FrameGen` | int | 0 | 0/1 | Enable Frame Generation (RTX 40+ only) |
| `r.DLSS.RayReconstruction` | int | 0 | 0/1 | Enable Ray Reconstruction (RTX 20+ only) |

### Editor Controls

```
[DLSS] checkbox: Enable
[Mode] dropdown: DLAA | Quality | Balanced | Performance | Ultra Performance
[Sharpness] slider: 0.0 -------- 1.0
[Frame Generation] checkbox: Enable (grayed out if not RTX 40)
[Ray Reconstruction] checkbox: Enable (grayed out if not RTX 20+)
```

---

## 8. Engine-Native Frame Interpolation (Fallback)

### Purpose

For GPUs that do not support DLSS Frame Generation or FSR 3 Frame Generation, UzEngine provides a **universally compatible** frame interpolation fallback based on optical flow. Quality is lower than vendor-specific solutions, but it provides a meaningful frame rate boost on any DX12 GPU.

### Optical Flow Pipeline

```
Frame N-1 (previous upscaled output)
    |                                       Frame N (current upscaled output)
    |                                           |
    +----------+                     +----------+
               |                     |
         [CS_OpticalFlow.hlsl]
         Lucas-Kanade pyramid optical flow
         4-level pyramid, 8x8 blocks, 4 iterations
               |
          Motion Field (R16G16_FLOAT, quarter-res)
               |
         [CS_FrameInterpolation.hlsl]
         Forward + backward warp to generate N+0.5
         Disocclusion fill from nearest valid pixel
         Blend weights from flow confidence
               |
         Interpolated Frame N+0.5
```

### Optical Flow Shader

```hlsl
// shaders/post/optical_flow.hlsl
// Lucas-Kanade pyramid optical flow for frame interpolation.
// Computes dense motion field between two consecutive frames.
// Output: R16G16_FLOAT motion vectors at quarter resolution.

#include "common.hlsli"

cbuffer OpticalFlowParams : register(b2) {
    float2 of_texel_size;        // 1.0 / quarter-res dimensions
    float2 of_full_texel_size;   // 1.0 / full-res dimensions
    uint   of_pyramid_level;     // current pyramid level (0 = finest)
    uint   of_iteration;         // current iteration within this level
    float  of_lambda;            // regularization weight (smoothness, default 10.0)
    float  _pad;
};

// Input frames (full resolution, converted to grayscale)
Texture2D<float> g_frame_prev   : register(t0);  // previous frame luminance
Texture2D<float> g_frame_curr   : register(t1);  // current frame luminance

// Previous level's flow (upscaled, for pyramid propagation)
Texture2D<float2> g_flow_prev_level : register(t2);

// Output
RWTexture2D<float2> g_flow_out : register(u0);

SamplerState sampler_linear_clamp : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * of_texel_size;

    // Start from coarser level's flow (upscaled) or zero at coarsest level
    float2 flow;
    if (of_pyramid_level == 3) {
        flow = float2(0, 0);  // coarsest level: no prior estimate
    } else {
        flow = g_flow_prev_level.SampleLevel(sampler_linear_clamp, uv, 0);
    }

    // Lucas-Kanade: compute image gradients and temporal derivative
    // at the warped position (current frame warped by current flow estimate)
    float2 warped_uv = uv + flow * of_full_texel_size;

    // Spatial gradients (central differences on current frame)
    float Ix = (g_frame_curr.SampleLevel(sampler_linear_clamp, warped_uv + float2(of_full_texel_size.x, 0), 0)
              - g_frame_curr.SampleLevel(sampler_linear_clamp, warped_uv - float2(of_full_texel_size.x, 0), 0)) * 0.5;
    float Iy = (g_frame_curr.SampleLevel(sampler_linear_clamp, warped_uv + float2(0, of_full_texel_size.y), 0)
              - g_frame_curr.SampleLevel(sampler_linear_clamp, warped_uv - float2(0, of_full_texel_size.y), 0)) * 0.5;

    // Temporal derivative
    float It = g_frame_curr.SampleLevel(sampler_linear_clamp, warped_uv, 0)
             - g_frame_prev.SampleLevel(sampler_linear_clamp, uv, 0);

    // Accumulate Ix*Ix, Ix*Iy, Iy*Iy, Ix*It, Iy*It over a local window (5x5)
    float Ixx = 0, Ixy = 0, Iyy = 0, Ixt = 0, Iyt = 0;

    [unroll]
    for (int wy = -2; wy <= 2; ++wy) {
        [unroll]
        for (int wx = -2; wx <= 2; ++wx) {
            float2 offset = float2(wx, wy) * of_texel_size;
            float2 sample_uv = uv + offset;
            float2 sample_warped = sample_uv + flow * of_full_texel_size;

            float sIx = (g_frame_curr.SampleLevel(sampler_linear_clamp, sample_warped + float2(of_full_texel_size.x, 0), 0)
                        - g_frame_curr.SampleLevel(sampler_linear_clamp, sample_warped - float2(of_full_texel_size.x, 0), 0)) * 0.5;
            float sIy = (g_frame_curr.SampleLevel(sampler_linear_clamp, sample_warped + float2(0, of_full_texel_size.y), 0)
                        - g_frame_curr.SampleLevel(sampler_linear_clamp, sample_warped - float2(0, of_full_texel_size.y), 0)) * 0.5;
            float sIt = g_frame_curr.SampleLevel(sampler_linear_clamp, sample_warped, 0)
                       - g_frame_prev.SampleLevel(sampler_linear_clamp, sample_uv, 0);

            Ixx += sIx * sIx;
            Ixy += sIx * sIy;
            Iyy += sIy * sIy;
            Ixt += sIx * sIt;
            Iyt += sIy * sIt;
        }
    }

    // Solve 2x2 system with Tikhonov regularization:
    //   [Ixx + lambda, Ixy       ] [du]   [-Ixt]
    //   [Ixy,          Iyy + lambda] [dv] = [-Iyt]
    float det = (Ixx + of_lambda) * (Iyy + of_lambda) - Ixy * Ixy;
    float2 delta_flow = float2(0, 0);
    if (abs(det) > EPSILON) {
        delta_flow.x = ((Iyy + of_lambda) * (-Ixt) - Ixy * (-Iyt)) / det;
        delta_flow.y = ((Ixx + of_lambda) * (-Iyt) - Ixy * (-Ixt)) / det;
    }

    flow += delta_flow;
    g_flow_out[tid] = flow;
}
```

### Frame Interpolation Shader

```hlsl
// shaders/post/frame_interpolation.hlsl
// Generates an intermediate frame at t=0.5 between frame N-1 and frame N
// using the optical flow motion field.

#include "common.hlsli"

cbuffer FrameInterpParams : register(b2) {
    float2 fi_texel_size;       // 1.0 / output dimensions
    float2 fi_flow_texel_size;  // 1.0 / flow field dimensions
    float  fi_blend_t;          // interpolation parameter (0.5 for midpoint)
    float3 _pad;
};

Texture2D<float4> g_frame_prev  : register(t0);  // frame N-1
Texture2D<float4> g_frame_curr  : register(t1);  // frame N
Texture2D<float2> g_flow        : register(t2);  // optical flow (prev -> curr)

SamplerState sampler_linear_clamp : register(s0);

RWTexture2D<float4> g_output : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * fi_texel_size;

    // Sample flow at this pixel (bilinear from quarter-res flow field)
    float2 flow = g_flow.SampleLevel(sampler_linear_clamp, uv, 0);

    // Forward warp: where this pixel in frame N-1 ends up in frame N
    // Backward warp: where this pixel in frame N came from in frame N-1
    float2 uv_from_prev = uv - flow * fi_texel_size * fi_blend_t;
    float2 uv_from_curr = uv + flow * fi_texel_size * (1.0 - fi_blend_t);

    // Sample both frames at warped positions
    float4 color_prev = g_frame_prev.SampleLevel(sampler_linear_clamp, uv_from_prev, 0);
    float4 color_curr = g_frame_curr.SampleLevel(sampler_linear_clamp, uv_from_curr, 0);

    // Flow confidence: if flow is consistent (forward-backward check would go here),
    // blend equally. Otherwise, prefer the nearer frame.
    float flow_magnitude = length(flow);

    // Simple confidence: small flow = high confidence, large flow = lower confidence
    float confidence = saturate(1.0 - flow_magnitude * 0.05);

    // Blend with t parameter
    float4 result = lerp(color_prev, color_curr, fi_blend_t);

    // On very large flow (disocclusions), fall back to current frame
    if (flow_magnitude > 40.0) {
        result = color_curr;
    }

    g_output[tid] = result;
}
```

### Quality and Limitations

| Aspect | Quality |
|--------|---------|
| Slow camera pans | Good -- optical flow handles uniform motion well |
| Fast character animation | Moderate -- limb edges show ghosting |
| Disocclusions | Poor -- holes filled with fallback blend |
| Particle effects | Poor -- optical flow cannot track particles |
| HUD/UI elements | Must be composited AFTER frame interpolation |

### Activation Threshold

Engine-native frame interpolation is only activated when the GPU frametime exceeds the target:

```cpp
// src/renderer/framegen/native_frame_gen.cpp
bool should_activate_native_framegen(const DynamicResolutionState* drs) {
    // Only interpolate when we are consistently below target FPS
    // and no vendor frame gen is available
    float avg_frametime_ms = drs->avg_gpu_time_ms;
    float target_ms = 1000.0f / drs->target_fps;
    return avg_frametime_ms > target_ms * 1.1f;  // 10% margin
}
```

### Console Variables

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.FrameGen.Enable` | int | 0 | 0/1 | Enable frame generation |
| `r.FrameGen.Method` | int | 0 | 0-2 | 0=Auto (vendor if available), 1=DLSS FG, 2=FSR FG, 3=Native |
| `r.FrameGen.NativeThreshold` | float | 1.1 | 1.0-2.0 | Activation threshold multiplier for native fallback |

---

## 9. Denoising (GI / Shadow / Reflections)

### Overview

When UzEngine renders ray-traced effects (shadows, global illumination, reflections) at low sample counts for performance, the resulting images are noisy. A real-time denoiser reconstructs a clean signal from the noisy input using spatial filtering (bilateral/atrous wavelet) and temporal accumulation (motion-guided reprojection).

The denoiser design is inspired by NVIDIA's Real-time Denoiser (NRD) library, using two filter modes:

| Filter | Target Signal | Approach |
|--------|--------------|----------|
| **REBLUR** | Diffuse/specular indirect (GI) | Recurrent spatial blur + temporal accumulation |
| **RELAX** | Shadows | Spatiotemporal filtering optimized for binary/penumbra signals |

### Denoiser Architecture

```
Noisy Input Signal (1 spp ray-traced shadow / GI / reflection)
    |
    +--- Normal buffer (GBuffer1)
    +--- Depth buffer (D32_FLOAT)
    +--- Motion vectors (GBuffer3)
    |
    v
[Temporal Accumulation]
    Reproject previous denoised output using motion vectors.
    Blend with current noisy sample (accumulation factor 4-32 frames).
    Disocclusion detection resets accumulation where history is invalid.
    |
    v
[Spatial Filter: A-Trous Wavelet]
    5 iterations at increasing step sizes (1, 2, 4, 8, 16 pixels).
    Edge-stopping functions based on:
      - Normal similarity (dot product threshold)
      - Depth similarity (exponential falloff)
      - Luminance similarity (prevent over-blur on color edges)
    |
    v
Denoised Output (clean shadow / GI / reflection buffer)
```

### Spatial Filter: A-Trous Wavelet

```hlsl
// shaders/post/denoise_spatial.hlsl
// A-trous wavelet spatial filter for denoising ray-traced signals.
// Run N times with increasing step_size: 1, 2, 4, 8, 16.

#include "common.hlsli"

cbuffer DenoiseParams : register(b2) {
    float2 dn_texel_size;          // 1.0 / buffer dimensions
    uint   dn_step_size;           // current wavelet step: 1, 2, 4, 8, or 16
    float  dn_normal_weight;       // edge-stopping strength for normals (32.0)
    float  dn_depth_weight;        // edge-stopping strength for depth (1.0)
    float  dn_luminance_weight;    // edge-stopping strength for luminance (4.0)
    float2 _pad;
};

Texture2D<float4> g_noisy_input : register(t0);  // noisy or partially denoised signal
Texture2D<float4> g_normal      : register(t1);  // GBuffer1 (normals)
Texture2D<float>  g_depth       : register(t2);  // depth buffer

SamplerState sampler_point_clamp : register(s0);

RWTexture2D<float4> g_denoised : register(u0);

// 5x5 B3-spline wavelet kernel weights (separable approximation)
static const float kernel[3] = { 1.0 / 4.0, 1.0 / 8.0, 1.0 / 16.0 };
static const int offsets[5] = { -2, -1, 0, 1, 2 };
static const float weights[5] = { 1.0/16.0, 4.0/16.0, 6.0/16.0, 4.0/16.0, 1.0/16.0 };

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * dn_texel_size;

    float4 center_color  = g_noisy_input[tid];
    float3 center_normal = oct_decode(g_normal.SampleLevel(sampler_point_clamp, uv, 0).rg);
    float  center_depth  = g_depth.SampleLevel(sampler_point_clamp, uv, 0);
    float  center_luma   = luminance(center_color.rgb);

    float4 sum    = center_color * weights[2];
    float  w_sum  = weights[2];

    [unroll]
    for (int j = 0; j < 5; ++j) {
        [unroll]
        for (int i = 0; i < 5; ++i) {
            if (i == 2 && j == 2) continue;  // skip center (already accumulated)

            int2 offset = int2(offsets[i], offsets[j]) * (int)dn_step_size;
            int2 coord = int2(tid) + offset;
            float2 sample_uv = (float2(coord) + 0.5) * dn_texel_size;

            // Bounds check
            if (any(sample_uv < 0.0) || any(sample_uv > 1.0)) continue;

            float4 sample_color  = g_noisy_input[coord];
            float3 sample_normal = oct_decode(g_normal.SampleLevel(sampler_point_clamp, sample_uv, 0).rg);
            float  sample_depth  = g_depth.SampleLevel(sampler_point_clamp, sample_uv, 0);
            float  sample_luma   = luminance(sample_color.rgb);

            // Edge-stopping function: normal similarity
            float normal_w = pow(max(dot(center_normal, sample_normal), 0.0), dn_normal_weight);

            // Edge-stopping function: depth similarity
            float depth_diff = abs(center_depth - sample_depth);
            float depth_w = exp(-depth_diff * dn_depth_weight / (abs(center_depth) + EPSILON));

            // Edge-stopping function: luminance similarity
            float luma_diff = abs(center_luma - sample_luma);
            float luma_w = exp(-luma_diff * dn_luminance_weight);

            float w = weights[i] * weights[j] * normal_w * depth_w * luma_w;

            sum   += sample_color * w;
            w_sum += w;
        }
    }

    g_denoised[tid] = sum / (w_sum + EPSILON);
}
```

### Temporal Accumulation

```hlsl
// shaders/post/denoise_temporal.hlsl
// Temporal accumulation pass for denoising.
// Reprojects previous denoised output and blends with current noisy sample.

#include "common.hlsli"

cbuffer DenoiseTemporalParams : register(b2) {
    float2 dt_texel_size;
    float  dt_accumulation_speed;  // 0.0-1.0, lower = more frames accumulated
    float  dt_disocclusion_threshold; // depth difference for disocclusion (0.01)
};

Texture2D<float4> g_noisy_current   : register(t0);
Texture2D<float4> g_denoised_prev   : register(t1);  // previous frame denoised
Texture2D<float2> g_velocity        : register(t2);
Texture2D<float>  g_depth_current   : register(t3);
Texture2D<float>  g_depth_previous  : register(t4);

SamplerState sampler_linear_clamp : register(s0);
SamplerState sampler_point_clamp  : register(s1);

RWTexture2D<float4> g_accumulated : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * dt_texel_size;

    float4 current = g_noisy_current[tid];

    // Reproject to previous frame
    float2 velocity = g_velocity.SampleLevel(sampler_point_clamp, uv, 0);
    float2 prev_uv = uv - velocity;

    // Check for disocclusion
    bool disoccluded = false;
    if (any(prev_uv < 0.0) || any(prev_uv > 1.0)) {
        disoccluded = true;
    } else {
        float curr_depth = g_depth_current.SampleLevel(sampler_point_clamp, uv, 0);
        float prev_depth = g_depth_previous.SampleLevel(sampler_point_clamp, prev_uv, 0);
        float depth_diff = abs(curr_depth - prev_depth);
        disoccluded = (depth_diff > dt_disocclusion_threshold);
    }

    if (disoccluded) {
        // No valid history: use current sample only
        g_accumulated[tid] = current;
    } else {
        // Blend with history
        float4 history = g_denoised_prev.SampleLevel(sampler_linear_clamp, prev_uv, 0);
        float blend = dt_accumulation_speed;
        g_accumulated[tid] = lerp(history, current, blend);
    }
}
```

### Console Variables

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.Denoiser.Enable` | int | 0 | 0/1 | Enable denoiser (auto-activates with RT effects) |
| `r.Denoiser.SpatialIterations` | int | 5 | 1-5 | A-trous wavelet iterations |
| `r.Denoiser.AccumulationSpeed` | float | 0.1 | 0.02-0.5 | Temporal blend (lower = more accumulation) |
| `r.Denoiser.NormalWeight` | float | 32.0 | 1.0-128.0 | Normal edge-stopping strength |
| `r.Denoiser.DepthWeight` | float | 1.0 | 0.1-10.0 | Depth edge-stopping strength |

---

## 10. Sharpening -- Contrast Adaptive Sharpening (CAS)

### Overview

AMD FidelityFX CAS (Contrast Adaptive Sharpening) restores high-frequency detail lost by temporal upscaling or TAA-induced blur. Unlike naive unsharp masking, CAS analyzes local contrast to sharpen soft areas while leaving already-sharp edges untouched, preventing halo artifacts.

CAS runs as the **final post-process pass** before frame generation and UI compositing.

### Algorithm

1. Sample a cross-shaped pattern (center + 4 neighbors at +/-1 pixel)
2. Compute the minimum and maximum luminance in the local neighborhood
3. Compute a sharpening weight inversely proportional to local contrast (low contrast = sharpen more, high contrast = leave alone)
4. Apply the sharpening as a weighted blend of the center pixel with a high-pass filtered version

```hlsl
// shaders/post/cas.hlsl
// AMD FidelityFX Contrast Adaptive Sharpening (CAS)
// Applied after TAA/upscaling to restore high-frequency detail.

#include "common.hlsli"

cbuffer CASParams : register(b2) {
    float  cas_sharpness;       // user-facing sharpness [0.0 - 1.0]
    float2 cas_texel_size;      // 1.0 / output resolution
    float  _pad;
};

Texture2D<float4> g_input : register(t0);
SamplerState sampler_point_clamp : register(s0);

RWTexture2D<float4> g_output : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    // Sample center + 4 cardinal neighbors
    float3 c = g_input[tid].rgb;
    float3 n = g_input[tid + int2( 0, -1)].rgb;
    float3 s = g_input[tid + int2( 0,  1)].rgb;
    float3 w = g_input[tid + int2(-1,  0)].rgb;
    float3 e = g_input[tid + int2( 1,  0)].rgb;

    // Compute min/max luminance of the cross pattern
    float luma_c = luminance(c);
    float luma_n = luminance(n);
    float luma_s = luminance(s);
    float luma_w = luminance(w);
    float luma_e = luminance(e);

    float mn = min(min(luma_n, luma_s), min(luma_w, min(luma_e, luma_c)));
    float mx = max(max(luma_n, luma_s), max(luma_w, max(luma_e, luma_c)));

    // CAS filter weight: sharpen more where contrast is low
    // peak = sqrt(min / max) -- normalized contrast ratio
    // The less contrast, the more sharpening is applied
    float peak = sqrt(mn / (mx + EPSILON));

    // Map user sharpness [0,1] to internal weight
    // At sharpness=0: weight=0 (no sharpening)
    // At sharpness=1: weight = peak * 0.5 (maximum safe sharpening)
    float w_sharp = peak * cas_sharpness * 0.5;

    // Apply sharpening: enhance the center relative to neighbors
    float3 result = c + (c * 4.0 - (n + s + w + e)) * w_sharp;

    // Clamp to prevent negative values from overshoot
    result = max(result, 0.0);

    g_output[tid] = float4(result, 1.0);
}
```

### Console Variables

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.Sharpness` | float | 0.5 | 0.0-1.0 | Global CAS sharpening intensity |
| `r.Sharpness.Enable` | int | 1 | 0/1 | Toggle CAS pass |

---

## 11. Image Quality Editor Tool

### Purpose

The Image Quality Editor is a panel in the VSCode extension that provides real-time controls for all quality settings, visual comparison tools, and automated quality metrics. It is the primary interface for technical artists and graphics engineers to evaluate and configure the image quality pipeline.

### Editor Panel Layout

```
+-----------------------------------------------------------------------+
|  IMAGE QUALITY PIPELINE                                    [x] Close  |
+-----------------------------------------------------------------------+
|                                                                       |
|  ANTI-ALIASING                     UPSCALING                          |
|  +---------------------------+     +---------------------------+      |
|  | Method: [TAA         v]   |     | Method: [FSR 3       v]   |     |
|  | TAA Blend:  [====|===] 0.1|     | Quality: [Quality    v]   |     |
|  | Variance:   [====|===] 1.0|     | Screen%: [====|=======]67%|     |
|  | Sharpen:    [=====|==] 0.3|     | Sharpness:[====|======]0.5|     |
|  | Jitter:     [8 samples v] |     +---------------------------+      |
|  +---------------------------+                                        |
|                                                                       |
|  FRAME GENERATION                  DENOISING                          |
|  +---------------------------+     +---------------------------+      |
|  | [x] Enable               |     | [x] Enable               |      |
|  | Method: [Auto        v]   |     | Iterations: [===|====] 5  |     |
|  | Status: DLSS FG active    |     | Accum Speed:[====|===] 0.1|     |
|  +---------------------------+     +---------------------------+      |
|                                                                       |
|  SHARPENING                        DYNAMIC RESOLUTION                 |
|  +---------------------------+     +---------------------------+      |
|  | [x] CAS Enable           |     | [x] Enable               |      |
|  | Strength: [=====|==] 0.5  |     | Target FPS: [60     v]   |     |
|  +---------------------------+     | Min Screen%:[====|==] 50% |      |
|                                    | Max Screen%:[========] 100%|     |
|  PRESET                           | Current:      67%          |     |
|  +---------------------------+     +---------------------------+      |
|  | [Low][Med][High][Ultra]   |                                        |
|  | [Custom]                  |                                        |
|  +---------------------------+                                        |
|                                                                       |
+-----------------------------------------------------------------------+
|                                                                       |
|  STATS OVERLAY                                                        |
|  Render: 1280x720 | Output: 1920x1080 | Scale: 1.50x                 |
|  AA: TAA | Upscale: FSR3 Quality | FrameGen: OFF | Denoise: ON       |
|  GPU Time: 8.3ms | FPS: 120 (60 rendered + 60 interpolated)          |
|                                                                       |
+-----------------------------------------------------------------------+
```

### A/B Comparison Viewport

Split-screen comparison between two quality configurations. A draggable vertical divider separates the left (config A) and right (config B) halves.

```
+-----------------------------------------------------------------------+
|  A/B COMPARISON                                            [x] Close  |
+-----------------------------------------------------------------------+
|  Config A: [TAA + Native    v]  |  Config B: [DLSS Quality   v]      |
+--------------------------------+--------------------------------------+
|                                |                                      |
|                                |                                      |
|                                |                                      |
|    Config A renders here       |    Config B renders here             |
|    (native TAA, 1080p)         |    (DLSS Quality, 720p->1080p)      |
|                                |                                      |
|                          <--||-->                                     |
|                       drag divider                                    |
|                                |                                      |
|                                |                                      |
|                                |                                      |
+--------------------------------+--------------------------------------+
|  A: 16.1ms GPU | B: 8.3ms GPU | Delta: -48% | PSNR: 32.1 dB        |
+-----------------------------------------------------------------------+
```

**Implementation**: The engine renders the scene twice per frame (once per config), each to a separate render target. The VSCode extension composites them client-side with a CSS clip-path on the divider position. For performance, A/B mode halves the streaming frame rate (30 fps per side at 60 fps total).

### Pixel Peeper Widget

Magnified view of an 8x8 pixel region with per-pixel RGB values displayed.

```
+-------------------------------+
|  PIXEL PEEPER (8x zoom)      |
+-------------------------------+
|  Click viewport to select     |
|                               |
|  +--+--+--+--+--+--+--+--+   |
|  |.2|.2|.3|.3|.3|.2|.2|.1|   |
|  |.1|.1|.2|.2|.2|.1|.1|.1|   |
|  +--+--+--+--+--+--+--+--+   |
|  |.2|.3|.5|.6|.6|.5|.3|.2|   |
|  |.1|.2|.3|.4|.4|.3|.2|.1|   |
|  +--+--+--+--+--+--+--+--+   |
|  |.3|.5|.8|.9|.9|.8|.5|.3|   |  <- each cell shows
|  |.2|.3|.5|.7|.7|.5|.3|.2|   |     R value / G value
|  +--+--+--+--+--+--+--+--+   |
|  |.3|.6|.9|1.|1.|.9|.6|.3|   |
|  |.2|.4|.7|.8|.8|.7|.4|.2|   |
|  +--+--+--+--+--+--+--+--+   |
|  |.3|.5|.9|1.|1.|.9|.5|.3|   |
|  |.2|.4|.6|.8|.8|.6|.4|.2|   |
|  +--+--+--+--+--+--+--+--+   |
|  |.3|.5|.8|.9|.9|.8|.5|.3|   |
|  |.2|.3|.5|.7|.7|.5|.3|.2|   |
|  +--+--+--+--+--+--+--+--+   |
|  |.2|.3|.5|.6|.6|.5|.3|.2|   |
|  |.1|.2|.3|.4|.4|.3|.2|.1|   |
|  +--+--+--+--+--+--+--+--+   |
|  |.2|.2|.3|.3|.3|.2|.2|.1|   |
|  |.1|.1|.2|.2|.2|.1|.1|.1|   |
|  +--+--+--+--+--+--+--+--+   |
|                               |
|  Pixel [640, 360]:            |
|  R: 0.892  G: 0.731  B: 0.214|
|  Luminance: 0.743             |
|  Depth: 0.9987 (linear: 12.3m)|
|  Velocity: (0.002, -0.001)   |
+-------------------------------+
```

**Implementation**: The engine's `renderer.readPixel` IPC command returns the pixel data for a given coordinate. The extension requests an 8x8 block and renders it as an HTML table with color-coded cells.

### Ghosting Test Mode

Automated test that moves a bright sphere back and forth at configurable speed while recording frames. The extension overlays a "ghost trail" visualization showing where the TAA history leaves visible artifacts.

```json
// IPC command to start ghosting test
quality.ghostTest {
    "enabled": true,
    "speed": 5.0,          // oscillation speed in m/s
    "object": "test_sphere", // engine spawns a test object
    "duration": 3.0,        // test duration in seconds
    "record_frames": true   // capture every frame for analysis
}
```

### Temporal Stability Score

Automated metric computed over a stationary camera view (no movement). Measures per-pixel variance across N consecutive frames. Lower variance = more temporally stable.

```
Temporal Stability Score = 1.0 - mean(per_pixel_variance_over_32_frames)
```

| Score | Interpretation |
|-------|---------------|
| 0.95+ | Excellent -- virtually no flickering |
| 0.90-0.95 | Good -- minor shimmer on sub-pixel detail |
| 0.80-0.90 | Acceptable -- visible flickering on thin geometry |
| <0.80 | Poor -- significant temporal instability |

### IPC Handlers

```json
quality.setMethod      { "aa": 2, "upscale": 1, "quality": 1 }
quality.setScreenPct   { "value": 67 }
quality.setPreset      { "name": "High" }
quality.abCompare      { "configA": {...}, "configB": {...} }
quality.readPixel      { "x": 640, "y": 360 }
quality.ghostTest      { "enabled": true, "speed": 5.0 }
quality.stabilityTest  { "frames": 32 }
quality.getStats       {}
```

---

## 12. In-Game Settings Exposure

### Console Variable Registry

All image quality parameters are registered as typed console variables with metadata for UI generation:

```cpp
// src/engine/console/quality_cvars.cpp

// Anti-Aliasing
CVAR_INT("r.AA.Method", 2, 0, 6,
    "0=None, 1=FXAA, 2=TAA, 3=SMAA, 4=MSAA2x, 5=MSAA4x, 6=MSAA8x");
CVAR_FLOAT("r.TAA.BlendFactor", 0.1f, 0.02f, 0.3f,
    "TAA history blend factor (lower = more stable, more ghosting)");
CVAR_FLOAT("r.TAA.VarianceClipGamma", 1.0f, 0.5f, 2.0f,
    "Variance clamp strictness for TAA ghost reduction");
CVAR_FLOAT("r.TAA.SharpenStrength", 0.3f, 0.0f, 1.0f,
    "Post-TAA sharpening intensity");
CVAR_INT("r.TAA.JitterSamples", 8, 4, 16,
    "Halton jitter sequence length (4, 8, or 16)");

// Upscaling
CVAR_INT("r.Upscale.Method", 0, 0, 4,
    "0=None, 1=FSR3, 2=XeSS, 3=DLSS, 4=TAAUpscale");
CVAR_INT("r.Upscale.Quality", 1, 0, 5,
    "Vendor-specific quality mode (0=highest quality, 4-5=ultra performance)");
CVAR_INT("r.ScreenPercentage", 100, 25, 200,
    "Render resolution as percentage of output (50 = half res, 200 = supersampling)");

// Frame Generation
CVAR_INT("r.FrameGen.Enable", 0, 0, 1, "Enable frame generation");
CVAR_INT("r.FrameGen.Method", 0, 0, 3,
    "0=Auto, 1=DLSS FG, 2=FSR FG, 3=Native");

// Sharpening
CVAR_FLOAT("r.Sharpness", 0.5f, 0.0f, 1.0f, "CAS sharpening strength");
CVAR_INT("r.Sharpness.Enable", 1, 0, 1, "Toggle CAS sharpening pass");

// Denoiser
CVAR_INT("r.Denoiser.Enable", 0, 0, 1, "Enable ray-traced signal denoiser");
CVAR_FLOAT("r.Denoiser.AccumulationSpeed", 0.1f, 0.02f, 0.5f,
    "Temporal blend for denoiser accumulation");
CVAR_INT("r.Denoiser.SpatialIterations", 5, 1, 5,
    "A-trous wavelet filter iterations");

// Dynamic Resolution
CVAR_INT("r.DynamicResolution.Enable", 0, 0, 1, "Enable dynamic resolution scaling");
CVAR_INT("r.DynamicResolution.TargetFPS", 60, 30, 240, "Target frame rate for DRS");
CVAR_INT("r.DynamicResolution.MinScreenPct", 50, 25, 100,
    "Minimum screen percentage for DRS");
CVAR_INT("r.DynamicResolution.MaxScreenPct", 100, 50, 200,
    "Maximum screen percentage for DRS");

// MSAA
CVAR_INT("r.MSAA.Samples", 0, 0, 8, "MSAA sample count (0, 2, 4, or 8)");

// FXAA
CVAR_FLOAT("r.FXAA.SubpixQuality", 0.75f, 0.0f, 1.0f, "FXAA sub-pixel quality");
CVAR_FLOAT("r.FXAA.EdgeThreshold", 0.063f, 0.031f, 0.25f, "FXAA edge threshold");

// SMAA
CVAR_INT("r.SMAA.Quality", 2, 0, 3, "SMAA quality (0=Low, 1=Med, 2=High, 3=Ultra)");
CVAR_INT("r.SMAA.Temporal", 0, 0, 1, "Enable SMAA T2x temporal mode");

// FSR
CVAR_INT("r.FSR.Enable", 0, 0, 1, "Enable FSR 3 upscaler");
CVAR_INT("r.FSR.QualityMode", 1, 0, 4, "FSR quality mode");
CVAR_FLOAT("r.FSR.Sharpness", 0.5f, 0.0f, 1.0f, "FSR RCAS sharpening");

// XeSS
CVAR_INT("r.XeSS.Enable", 0, 0, 1, "Enable XeSS upscaler");
CVAR_INT("r.XeSS.QualityMode", 2, 0, 5, "XeSS quality mode");

// DLSS
CVAR_INT("r.DLSS.Enable", 0, 0, 1, "Enable DLSS Super Resolution");
CVAR_INT("r.DLSS.QualityMode", 1, 0, 4, "DLSS quality mode");
CVAR_FLOAT("r.DLSS.Sharpness", 0.0f, 0.0f, 1.0f, "DLSS sharpening");
CVAR_INT("r.DLSS.FrameGen", 0, 0, 1, "Enable DLSS Frame Generation");
CVAR_INT("r.DLSS.RayReconstruction", 0, 0, 1, "Enable DLSS Ray Reconstruction");
```

### Scalability Presets

| Setting | Low | Medium | High | Ultra |
|---------|-----|--------|------|-------|
| `r.AA.Method` | 1 (FXAA) | 2 (TAA) | 2 (TAA) | 2 (TAA) |
| `r.Upscale.Method` | 0 (None) | 0 (None) | Auto | Auto |
| `r.Upscale.Quality` | - | - | 1 (Quality) | 0 (DLAA/NativeAA) |
| `r.ScreenPercentage` | 50 | 75 | 100 | 100 |
| `r.FrameGen.Enable` | 0 | 0 | 0 | 1 |
| `r.Sharpness` | 0.0 | 0.3 | 0.5 | 0.5 |
| `r.Denoiser.Enable` | 0 | 0 | 1 | 1 |
| `r.DynamicResolution.Enable` | 1 | 1 | 0 | 0 |
| `r.SMAA.Quality` | 0 | 1 | 2 | 3 |
| `r.TAA.BlendFactor` | 0.15 | 0.1 | 0.1 | 0.08 |
| `r.Denoiser.SpatialIterations` | 1 | 3 | 5 | 5 |

**Auto upscale method selection**: When `r.Upscale.Method` is set to `Auto`, the engine selects the best available upscaler based on GPU vendor and capabilities:
1. NVIDIA RTX -> DLSS
2. Intel Arc -> XeSS
3. AMD / Other -> FSR 3
4. Fallback -> TAA Upscale (engine-native)

### Preset Configuration Files

```ini
; res://config/quality_low.uzcfg
[quality]
r.AA.Method = 1
r.ScreenPercentage = 50
r.FrameGen.Enable = 0
r.Sharpness = 0.0
r.DynamicResolution.Enable = 1
r.DynamicResolution.MinScreenPct = 33
r.DynamicResolution.TargetFPS = 60
```

---

## 13. Dynamic Resolution Scaling (DRS)

### Overview

Dynamic Resolution Scaling automatically adjusts the internal render resolution to maintain a target frame rate. When the GPU is under heavy load (many objects, complex materials, volumetric effects), DRS reduces the screen percentage. When the GPU has headroom, DRS increases it back toward the maximum.

### Architecture

```
                    +-------------------+
                    | GPU Timestamp     |
                    | Query (per frame) |
                    +--------+----------+
                             |
                             v
                    +-------------------+
                    | EMA Filter        |
                    | (smooth GPU time) |
                    +--------+----------+
                             |
                             v
                    +-------------------+
                    | PID Controller    |  target_ms = 1000 / target_fps
                    | error = target_ms |  output = screen_pct adjustment
                    |       - gpu_ms    |
                    +--------+----------+
                             |
                             v
                    +-------------------+
                    | Clamp to          |
                    | [min%, max%]      |
                    +--------+----------+
                             |
                             v
                    New r.ScreenPercentage
                    (applied next frame)
```

### GPU Timestamp Measurement

```cpp
// src/renderer/drs/dynamic_resolution.h

struct DynamicResolutionState {
    // Configuration
    bool     enabled;
    float    target_fps;           // 60.0 or 30.0
    int      min_screen_pct;       // minimum allowed (e.g., 50)
    int      max_screen_pct;       // maximum allowed (e.g., 100)

    // State
    float    avg_gpu_time_ms;      // exponential moving average of GPU frame time
    float    current_screen_pct;   // current render scale
    float    ema_alpha;            // smoothing factor (0.1 = slow, 0.5 = fast)

    // GPU timestamp query resources
    ID3D12QueryHeap* timestamp_heap;
    ID3D12Resource*  timestamp_readback;
    uint64_t         gpu_frequency;    // ticks per second from command queue
};

void drs_init(DynamicResolutionState* drs, ID3D12Device* device,
              ID3D12CommandQueue* queue, float target_fps)
{
    drs->target_fps = target_fps;
    drs->min_screen_pct = 50;
    drs->max_screen_pct = 100;
    drs->ema_alpha = 0.15f;
    drs->current_screen_pct = (float)drs->max_screen_pct;

    // Create timestamp query heap (2 queries per frame: begin + end)
    D3D12_QUERY_HEAP_DESC heap_desc = {};
    heap_desc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heap_desc.Count = 2;
    device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&drs->timestamp_heap));

    // Readback buffer for resolving timestamp queries
    D3D12_RESOURCE_DESC buf_desc = {};
    buf_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Width            = sizeof(uint64_t) * 2;
    buf_desc.Height           = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels        = 1;
    buf_desc.SampleDesc.Count = 1;
    buf_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_READBACK;

    device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
        &buf_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
        IID_PPV_ARGS(&drs->timestamp_readback));

    queue->GetTimestampFrequency(&drs->gpu_frequency);
}

void drs_begin_frame(DynamicResolutionState* drs, ID3D12GraphicsCommandList* cmd) {
    cmd->EndQuery(drs->timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void drs_end_frame(DynamicResolutionState* drs, ID3D12GraphicsCommandList* cmd) {
    cmd->EndQuery(drs->timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    cmd->ResolveQueryData(drs->timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                          drs->timestamp_readback, 0);
}

void drs_update(DynamicResolutionState* drs) {
    if (!drs->enabled) return;

    // Read back GPU timestamps from previous frame
    uint64_t* timestamps = NULL;
    D3D12_RANGE read_range = { 0, sizeof(uint64_t) * 2 };
    drs->timestamp_readback->Map(0, &read_range, (void**)&timestamps);

    uint64_t gpu_ticks = timestamps[1] - timestamps[0];
    float gpu_time_ms = (float)(gpu_ticks * 1000.0 / (double)drs->gpu_frequency);

    D3D12_RANGE written_range = { 0, 0 };
    drs->timestamp_readback->Unmap(0, &written_range);

    // Exponential moving average
    drs->avg_gpu_time_ms = lerp(drs->avg_gpu_time_ms, gpu_time_ms, drs->ema_alpha);

    // Target frame time
    float target_ms = 1000.0f / drs->target_fps;

    // PID-like adjustment (proportional only for simplicity)
    float error = target_ms - drs->avg_gpu_time_ms;

    // Scale adjustment: positive error = headroom, negative = over budget
    // Scale factor: 2% per millisecond of error
    float adjustment = error * 2.0f;

    drs->current_screen_pct += adjustment;
    drs->current_screen_pct = fmaxf((float)drs->min_screen_pct,
                               fminf((float)drs->max_screen_pct,
                                     drs->current_screen_pct));
}
```

### Dynamic Resolution Graph (Editor Visualization)

```
+-----------------------------------------------------------------------+
|  DYNAMIC RESOLUTION                                                   |
+-----------------------------------------------------------------------+
|                                                                       |
|  100% ----+                        +------+          +-----           |
|           |                       /        \        /                 |
|   90% ----+                      /          +------+                  |
|           |                     /                                     |
|   80% ----+                    /                                      |
|           |         +---------+                                       |
|   70% ----+        /                                                  |
|           |       /                                                   |
|   60% ----+------+                                                    |
|           |                                                           |
|   50% ----+.............................................. min bound   |
|           |                                                           |
|           +---+---+---+---+---+---+---+---+---+---+---+---+---> t   |
|           0   1   2   3   4   5   6   7   8   9   10  11  12 sec    |
|                                                                       |
|  --- Screen %    ... Min bound    --- GPU Time (ms, right axis)      |
|                                                                       |
|  Current: 87%  |  GPU: 14.2ms  |  Target: 16.7ms (60fps)            |
+-----------------------------------------------------------------------+
```

### Console Variables

| Console Variable | Type | Default | Range | Description |
|------------------|------|---------|-------|-------------|
| `r.DynamicResolution.Enable` | int | 0 | 0/1 | Enable DRS |
| `r.DynamicResolution.TargetFPS` | int | 60 | 30-240 | Target frame rate |
| `r.DynamicResolution.MinScreenPct` | int | 50 | 25-100 | Minimum render scale |
| `r.DynamicResolution.MaxScreenPct` | int | 100 | 50-200 | Maximum render scale |
| `r.DynamicResolution.EMAAlpha` | float | 0.15 | 0.05-0.5 | Smoothing factor |

---

## 14. Implementation Notes

### Resource Formats

| Resource | Format | Resolution | Lifetime |
|----------|--------|------------|----------|
| Velocity buffer | `R16G16_FLOAT` | Internal | Per-frame (GBuffer3) |
| TAA history (A/B) | `R16G16B16A16_FLOAT` | Output | Persistent (ping-pong) |
| Upscaler output | `R16G16B16A16_FLOAT` | Output | Per-frame |
| Optical flow field | `R16G16_FLOAT` | Quarter-res | Per-frame |
| Frame interp output | `R8G8B8A8_UNORM` | Output | Per-frame |
| Denoiser accumulation | `R16G16B16A16_FLOAT` | Internal | Persistent (ping-pong) |
| CAS output | `R8G8B8A8_UNORM` | Output | Per-frame |
| SMAA edge texture | `R8G8_UNORM` | Output | Per-frame |
| SMAA blend weights | `R8G8B8A8_UNORM` | Output | Per-frame |

### Shader Files

| Shader | File | Entry | Type |
|--------|------|-------|------|
| MSAA Resolve | `shaders/post/msaa_resolve.hlsl` | `CSMain` | Compute |
| FXAA | `shaders/post/fxaa.hlsl` | `CSMain` | Compute |
| TAA Resolve | `shaders/post/taa.hlsl` | `CSMain` | Compute |
| SMAA Edge Detect | `shaders/post/smaa_edge_detect.hlsl` | `CSMain` | Compute |
| SMAA Blend Weights | `shaders/post/smaa_blend_weights.hlsl` | `CSMain` | Compute |
| SMAA Neighborhood Blend | `shaders/post/smaa_blend.hlsl` | `CSMain` | Compute |
| CAS Sharpening | `shaders/post/cas.hlsl` | `CSMain` | Compute |
| Optical Flow | `shaders/post/optical_flow.hlsl` | `CSMain` | Compute |
| Frame Interpolation | `shaders/post/frame_interpolation.hlsl` | `CSMain` | Compute |
| Denoise Spatial | `shaders/post/denoise_spatial.hlsl` | `CSMain` | Compute |
| Denoise Temporal | `shaders/post/denoise_temporal.hlsl` | `CSMain` | Compute |

All compute shaders use `[numthreads(8, 8, 1)]` and entry point `CSMain` per project convention.

### Upscaler Comparison Table

```
+-------------------+----------+----------+----------+----------+----------+
|                   |  FSR 3   |  XeSS    |  DLSS    |  TAA Up  |  Native  |
+-------------------+----------+----------+----------+----------+----------+
| Vendor            | AMD      | Intel    | NVIDIA   | Engine   | Engine   |
| GPU Requirement   | Any DX12 | Any DX12 | RTX 20+  | Any DX12 | Any DX12 |
| AI/ML Hardware    | No       | XMX opt. | Tensor   | No       | No       |
| Open Source       | Yes      | No       | No       | Yes      | N/A      |
| Frame Generation  | Yes      | No       | Yes      | No       | Yes*     |
| Ray Reconstruction| No       | No       | Yes      | No       | No       |
| Quality (4K)      | High     | High     | Highest  | Medium   | Reference|
| Latency Impact    | Low      | Low      | Low      | None     | None     |
| SDK Dependency    | ffx-sdk  | libxess  | NGX      | None     | None     |
| License           | MIT      | Binary   | Binary   | N/A      | N/A      |
+-------------------+----------+----------+----------+----------+----------+
* Native frame interpolation is lower quality than vendor solutions
```

### C++ Class Hierarchy

```
ImageQualityPipeline                    (top-level coordinator)
    |
    +-- AASolver                        (anti-aliasing method selection)
    |       +-- FXAASolver              (FXAA 3.11)
    |       +-- TAASolver               (temporal AA with ghost reduction)
    |       +-- SMAASolver              (SMAA + optional T2x)
    |       +-- MSAASolver              (hardware MSAA resolve)
    |
    +-- UpscaleSolver                   (temporal upscaling method selection)
    |       +-- FSR3Solver              (AMD FidelityFX SDK)
    |       +-- XeSSSolver              (Intel XeSS SDK)
    |       +-- DLSSSolver              (NVIDIA NGX SDK)
    |       +-- TAAUpscaleSolver        (engine-native TAA upscale)
    |
    +-- FrameGenSolver                  (frame generation method selection)
    |       +-- DLSSFrameGenSolver      (NVIDIA DLSS 3 FG)
    |       +-- FSRFrameGenSolver       (AMD FSR 3 FG)
    |       +-- NativeFrameGenSolver    (engine optical flow)
    |
    +-- DenoiseSolver                   (ray-traced signal denoising)
    |       +-- REBLURSolver            (diffuse/specular GI)
    |       +-- RELAXSolver             (shadows)
    |       +-- DLSSRayReconSolver      (NVIDIA Ray Reconstruction)
    |
    +-- SharpenSolver                   (post-upscale sharpening)
    |       +-- CASSolver               (AMD CAS)
    |
    +-- DynamicResolutionScaler         (DRS controller)
    |
    +-- ImageQualitySettings            (unified settings struct)
    +-- ScalabilityPresetManager        (preset loading/application)
```

```cpp
// src/renderer/image_quality/image_quality_pipeline.h

struct ImageQualitySettings {
    // Anti-aliasing
    int    aa_method;                // r.AA.Method
    float  taa_blend_factor;         // r.TAA.BlendFactor
    float  taa_variance_clip_gamma;  // r.TAA.VarianceClipGamma
    float  taa_sharpen_strength;     // r.TAA.SharpenStrength
    int    taa_jitter_samples;       // r.TAA.JitterSamples
    int    smaa_quality;             // r.SMAA.Quality
    int    smaa_temporal;            // r.SMAA.Temporal
    int    msaa_samples;             // r.MSAA.Samples
    float  fxaa_subpix_quality;      // r.FXAA.SubpixQuality
    float  fxaa_edge_threshold;      // r.FXAA.EdgeThreshold

    // Upscaling
    int    upscale_method;           // r.Upscale.Method
    int    upscale_quality;          // r.Upscale.Quality
    int    screen_percentage;        // r.ScreenPercentage

    // Frame generation
    int    frame_gen_enable;         // r.FrameGen.Enable
    int    frame_gen_method;         // r.FrameGen.Method

    // Sharpening
    int    sharpen_enable;           // r.Sharpness.Enable
    float  sharpness;               // r.Sharpness

    // Denoiser
    int    denoiser_enable;          // r.Denoiser.Enable
    int    denoiser_spatial_iters;   // r.Denoiser.SpatialIterations
    float  denoiser_accum_speed;     // r.Denoiser.AccumulationSpeed

    // Dynamic resolution
    int    drs_enable;               // r.DynamicResolution.Enable
    int    drs_target_fps;           // r.DynamicResolution.TargetFPS
    int    drs_min_screen_pct;       // r.DynamicResolution.MinScreenPct
    int    drs_max_screen_pct;       // r.DynamicResolution.MaxScreenPct

    // DLSS-specific
    int    dlss_enable;              // r.DLSS.Enable
    int    dlss_quality_mode;        // r.DLSS.QualityMode
    float  dlss_sharpness;          // r.DLSS.Sharpness
    int    dlss_frame_gen;           // r.DLSS.FrameGen
    int    dlss_ray_reconstruction;  // r.DLSS.RayReconstruction

    // FSR-specific
    int    fsr_enable;               // r.FSR.Enable
    int    fsr_quality_mode;         // r.FSR.QualityMode
    float  fsr_sharpness;           // r.FSR.Sharpness

    // XeSS-specific
    int    xess_enable;              // r.XeSS.Enable
    int    xess_quality_mode;        // r.XeSS.QualityMode
};

struct ImageQualityPipeline {
    ImageQualitySettings settings;

    // Solver instances
    AASolver*                aa_solver;
    UpscaleSolver*           upscale_solver;
    FrameGenSolver*          frame_gen_solver;
    DenoiseSolver*           denoise_solver;
    SharpenSolver*           sharpen_solver;
    DynamicResolutionScaler* drs;

    // Intermediate buffers
    ID3D12Resource* internal_color;      // internal-res HDR color
    ID3D12Resource* upscaled_output;     // display-res upscaled result
    ID3D12Resource* sharpened_output;    // display-res after CAS
    ID3D12Resource* frame_gen_output;    // display-res interpolated frame

    // State
    bool upscaler_needs_reinit;    // set when screen% or upscale method changes
};

// Lifecycle
void iq_pipeline_init(ImageQualityPipeline* pipeline, ID3D12Device* device,
                      uint32_t display_width, uint32_t display_height);

void iq_pipeline_apply_settings(ImageQualityPipeline* pipeline,
                                const ImageQualitySettings* new_settings);

void iq_pipeline_execute(ImageQualityPipeline* pipeline,
                         ID3D12GraphicsCommandList* cmd,
                         const FrameContext* frame_ctx);

void iq_pipeline_resize(ImageQualityPipeline* pipeline, ID3D12Device* device,
                        uint32_t display_width, uint32_t display_height);

void iq_pipeline_shutdown(ImageQualityPipeline* pipeline);
```

### Post-Process Stack Integration

The image quality pipeline integrates into the existing post-process stack order defined in `screen-space-effects.md`:

```
Deferred Lighting (HDR, internal resolution)
    |
    v
SSAO Pass (half of internal res)
    |
    v
SSAO Blur (bilateral, 2 passes)
    |
    v
Apply SSAO (composite)
    |
    v
Hi-Z Generation
    |
    v
SSR Ray March
    |
    v
SSR Resolve
    |
    v
Bloom (existing)
    |
    v
Tonemapping (ACES Filmic)
    |                                         <-- INTERNAL RESOLUTION ends here
    v
[Image Quality Pipeline begins]
    |
    +-- Anti-Aliasing (FXAA / SMAA)           <-- if spatial AA selected
    |   OR
    +-- Temporal Upscaling (TAA / FSR / XeSS / DLSS)
    |       (includes AA + upscale in one pass)
    |                                         <-- OUTPUT RESOLUTION begins here
    v
Denoising (if RT effects enabled)
    |
    v
CAS Sharpening
    |
    v
Frame Generation (if enabled)
    |
    v
UI Compositing (rendered at output resolution, exempt from all IQ passes)
    |
    v
Final Output (LDR, RGBA8 -> readback -> JPEG)
```

### SDK Dependencies (vcpkg)

```json
{
    "dependencies": [
        { "name": "ffx-sdk", "features": ["upscale", "framegen"], "platform": "windows" },
        { "name": "xess-sdk", "platform": "windows" }
    ]
}
```

DLSS is not available via vcpkg. The NVIDIA NGX SDK is downloaded separately and referenced via CMake:

```cmake
# cmake/FindNGX.cmake
if(UZ_ENABLE_DLSS)
    find_path(NGX_INCLUDE_DIR nvsdk_ngx.h PATHS "${UZ_DLSS_SDK_PATH}/include")
    find_library(NGX_LIBRARY nvsdk_ngx_d PATHS "${UZ_DLSS_SDK_PATH}/lib/Windows_x86_64/x86_64")
    if(NGX_INCLUDE_DIR AND NGX_LIBRARY)
        add_library(ngx::ngx IMPORTED SHARED)
        set_target_properties(ngx::ngx PROPERTIES
            IMPORTED_LOCATION "${UZ_DLSS_SDK_PATH}/lib/Windows_x86_64/x86_64/nvngx_dlss.dll"
            IMPORTED_IMPLIB "${NGX_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${NGX_INCLUDE_DIR}"
        )
        message(STATUS "DLSS SDK found at ${UZ_DLSS_SDK_PATH}")
    else()
        message(WARNING "DLSS SDK not found, DLSS features disabled")
        set(UZ_ENABLE_DLSS OFF)
    endif()
endif()
```

### IPC Handlers (quality.*)

```json
quality.setSettings     { "aa_method": 2, "upscale_method": 1, "screen_percentage": 67 }
quality.getSettings     {} -> { "aa_method": 2, "upscale_method": 1, ... }
quality.setPreset       { "name": "High" }
quality.getPresets      {} -> ["Low", "Medium", "High", "Ultra", "Custom"]
quality.getCapabilities {} -> { "dlss_available": true, "dlss_frame_gen": true,
                                "fsr_available": true, "xess_available": true }
quality.abCompare       { "configA": {...}, "configB": {...} }
quality.readPixel       { "x": 640, "y": 360 }
quality.ghostTest       { "enabled": true, "speed": 5.0, "duration": 3.0 }
quality.stabilityTest   { "frames": 32 }
quality.getStats        {} -> { "render_res": [1280, 720], "output_res": [1920, 1080],
                                "screen_pct": 67, "gpu_time_ms": 8.3,
                                "upscaler": "FSR3", "aa": "TAA", "frame_gen": false }
```

---

## References

- [Karis 2014] Brian Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014 (UE4 TAA)
- [Salvi 2016] Marco Salvi, "An Excursion in Temporal Supersampling", GDC 2016 (Variance Clipping)
- [Lottes 2009] Timothy Lottes, "FXAA", NVIDIA white paper
- [Jimenez 2012] Jorge Jimenez, "SMAA: Enhanced Subpixel Morphological Antialiasing", Eurographics 2012
- [AMD FidelityFX] AMD FidelityFX SDK, https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- [Intel XeSS] Intel XeSS SDK documentation
- [NVIDIA DLSS] NVIDIA DLSS SDK (NGX), https://developer.nvidia.com/dlss
- [NRD] NVIDIA Real-time Denoiser, https://github.com/NVIDIAGameWorks/RayTracingDenoiser
- [Lucas-Kanade 1981] Lucas & Kanade, "An Iterative Image Registration Technique", IJCAI 1981
