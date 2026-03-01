# Screen-Space Effects Spec

## Overview

UzEngine provides three screen-space post-process effects that run after the deferred lighting pass: **SSAO** (Screen-Space Ambient Occlusion), **SSR** (Screen-Space Reflections), and **TAA** (Temporal Anti-Aliasing). All three are compute-shader-based and operate on the G-Buffer and HDR color buffer produced by the deferred pipeline.

These effects share a common dependency on the G-Buffer (normals, depth, roughness/metallic) and introduce a new velocity buffer for temporal reprojection. They are designed to work together: TAA stabilizes both SSAO and SSR, and SSR leverages temporal accumulation to reduce ray-marching noise.

---

## G-Buffer Layout (Updated)

TAA requires per-pixel motion vectors for reprojection. The existing G-Buffer layout gains a velocity channel:

| Target | Format | Contents | Notes |
|--------|--------|----------|-------|
| GBuffer0 | `RGBA8_UNORM` | Albedo (RGB) + Baked AO (A) | Unchanged |
| GBuffer1 | `RGBA16F` | Normal (RG oct-encoded) + Roughness (B) + Metallic (A) | Unchanged |
| GBuffer2 | `RGBA16F` | Emissive (RGB) + Unused (A) | Unchanged |
| GBuffer3 | `RG16F` | Velocity (screen-space motion vector XY) | **NEW** |
| Depth | `D32_FLOAT` | Hardware depth | Unchanged |

### Velocity Buffer Details

The velocity buffer stores screen-space motion vectors in normalized device coordinates (NDC). Each pixel records the offset between its current-frame screen position and its previous-frame screen position.

**Static geometry**: Velocity is computed from camera motion only.
```
velocity = (curr_ndc.xy - prev_ndc.xy)
curr_ndc = project(world_pos, curr_view_proj)
prev_ndc = project(world_pos, prev_view_proj)
```

**Dynamic geometry** (moving nodes): Velocity accounts for both camera and object motion.
```
velocity = (curr_ndc.xy - prev_ndc.xy)
curr_ndc = project(world_pos, curr_model * curr_view_proj)
prev_ndc = project(prev_world_pos, prev_model * prev_view_proj)
```

The engine must store the previous frame's view-projection matrix and the previous frame's model matrix for every visible dynamic node. This data is managed by `FrameContext` and the scene node system.

### G-Buffer Pass Update

The G-Buffer pixel shader is extended to write GBuffer3:

```hlsl
// shaders/builtin/gbuffer.hlsl (updated)

struct PSOut {
    float4 gbuffer0 : SV_TARGET0;  // albedo + AO
    float4 gbuffer1 : SV_TARGET1;  // normal + roughness + metallic
    float4 gbuffer2 : SV_TARGET2;  // emissive
    float2 gbuffer3 : SV_TARGET3;  // velocity
};

PSOut PSMain(VSOut i) {
    PSOut o;
    o.gbuffer0 = float4(albedo, ao);
    o.gbuffer1 = float4(oct_encode(normal), roughness, metallic);
    o.gbuffer2 = float4(emissive, 0.0);

    // Velocity: current clip pos vs previous clip pos (both in NDC)
    float2 curr_ndc = i.clip_pos.xy / i.clip_pos.w;
    float2 prev_ndc = i.prev_clip_pos.xy / i.prev_clip_pos.w;
    o.gbuffer3 = (curr_ndc - prev_ndc) * float2(0.5, -0.5);  // to UV space

    return o;
}
```

The vertex shader must output both current and previous clip positions:

```hlsl
struct VSOut {
    float4 pos           : SV_POSITION;
    float3 normal        : NORMAL;
    float2 uv            : TEXCOORD0;
    float3 world         : TEXCOORD1;
    float4 clip_pos      : TEXCOORD2;
    float4 prev_clip_pos : TEXCOORD3;
};

VSOut VSMain(VSIn v) {
    VSOut o;
    float4 world = mul(model, float4(v.pos, 1.0));
    o.pos           = mul(view_proj, world);
    o.clip_pos      = o.pos;

    // Previous frame transform for velocity
    float4 prev_world = mul(prev_model, float4(v.pos, 1.0));
    o.prev_clip_pos   = mul(prev_view_proj, prev_world);

    o.normal = mul((float3x3)model, v.normal);
    o.uv     = v.uv;
    o.world  = world.xyz;
    return o;
}
```

### Per-Frame CBV Update

The per-frame constant buffer (b1) gains previous-frame matrices:

```hlsl
cbuffer PerFrame : register(b1) {
    float4x4 view;
    float4x4 proj;
    float4x4 view_proj;
    float4x4 inv_view_proj;
    float4x4 prev_view_proj;      // NEW: previous frame's view-proj
    float3   camera_pos;
    float    time;
    uint     light_count;
    float2   jitter_offset;       // NEW: TAA jitter in pixel units
    float    _pad0;
};
```

The per-object constant buffer (b0) gains a previous-frame model matrix:

```hlsl
cbuffer PerObject : register(b0) {
    float4x4 model;
    float4x4 prev_model;         // NEW: previous frame's model matrix
    uint     material_index;
    float3   _pad0;
};
```

---

## Post-Process Stack Order

The complete post-process pipeline runs in this order after deferred lighting:

```
Deferred Lighting (HDR color buffer, R11G11B10F)
    |
    v
SSAO Pass (half-res compute)
    |
    v
SSAO Blur Pass (bilateral, 2 passes: horizontal + vertical)
    |
    v
Apply SSAO (multiply ambient term in composite pass)
    |
    v
Hi-Z Mip Chain Generation (compute, from depth buffer)
    |
    v
SSR Ray March (compute, full-res, RGBA16F output)
    |
    v
SSR Resolve (blend SSR result + IBL fallback, apply to HDR buffer)
    |
    v
Bloom (existing: dual-threshold downsample + additive upsample)
    |
    v
Tonemapping (ACES Filmic, existing)
    |
    v
TAA Resolve (compute, jitter reprojection + variance clamp + sharpen)
    |
    v
Final Output (LDR, RGBA8_UNORM -> readback -> JPEG)
```

Each effect can be independently toggled. When disabled, the pipeline skips that pass entirely (no GPU cost).

---

## A. SSAO -- Horizon-Based Ambient Occlusion Plus (HBAO+)

### Algorithm

HBAO+ computes ambient occlusion by estimating visibility in screen space. For each pixel, rays are cast along multiple directions in a 2D slice defined by the view-space normal. The algorithm measures the horizon angle in each direction and compares it against the tangent angle to determine occlusion.

HBAO+ is preferred over basic SSAO (random hemisphere sampling) because:
- Fewer samples required for the same quality (horizon-based integration converges faster)
- View-dependent sampling reduces under-sampling artifacts
- Better stability under camera rotation (no temporal flickering from random samples)

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `enabled` | bool | `true` | - | Toggle effect |
| `quality` | enum | `Medium` | Low/Medium/High | Sample count: 4/8/16 directions |
| `radius` | float | `0.5` | 0.1 - 5.0 | World-space sampling radius (meters) |
| `bias` | float | `0.025` | 0.0 - 0.1 | Angle bias to prevent self-occlusion on flat surfaces |
| `intensity` | float | `1.0` | 0.0 - 3.0 | Occlusion multiplier (higher = darker AO) |
| `max_distance` | float | `100.0` | 10.0 - 1000.0 | Depth beyond which AO fades to zero (meters) |

### Quality Levels

| Level | Directions | Steps/Dir | Total Samples | Approx Cost (1080p) |
|-------|-----------|-----------|---------------|---------------------|
| Low | 4 | 4 | 16 | ~0.3ms |
| Medium | 8 | 4 | 32 | ~0.6ms |
| High | 16 | 4 | 64 | ~1.0ms |

### Render Resources

| Resource | Format | Resolution | Description |
|----------|--------|------------|-------------|
| `ao_raw` | `R8_UNORM` | Half-res (640x360 at 1080p) | Raw occlusion output |
| `ao_blur_temp` | `R8_UNORM` | Half-res | Intermediate for 2-pass blur |
| `ao_final` | `R8_UNORM` | Half-res | Final blurred AO |

Half-resolution rendering reduces cost by 4x with minimal quality loss (bilateral blur upsamples edge-aware).

### HLSL: HBAO Compute Shader

```hlsl
// shaders/post/hbao.hlsl
//
// Horizon-Based Ambient Occlusion Plus (HBAO+)
// Reads: G-Buffer depth + normals
// Writes: half-res occlusion buffer (R8_UNORM)

#include "common.hlsli"
#include "noise.hlsli"
#include "global_cbuffer.hlsli"

// SSAO parameters (bound via root constants or CBV)
cbuffer SSAOParams : register(b2) {
    float  ssao_radius;         // world-space radius in meters
    float  ssao_bias;           // angle bias to prevent self-occlusion
    float  ssao_intensity;      // occlusion multiplier
    float  ssao_max_distance;   // AO fade distance

    float2 ssao_texel_size;     // 1.0 / half-res dimensions
    float2 ssao_full_texel_size;// 1.0 / full-res dimensions

    uint   ssao_num_directions; // 4 / 8 / 16 depending on quality
    uint   ssao_num_steps;      // steps per direction (always 4)
    float2 _pad;

    float4x4 ssao_proj;        // projection matrix (for screen-space radius)
};

// Input textures (G-Buffer)
Texture2D<float>  g_depth   : register(t0);  // D32_FLOAT depth buffer
Texture2D<float4> g_normal  : register(t1);  // GBuffer1: oct-encoded normal

// Output
RWTexture2D<float> g_ao_out : register(u0);

// Sampler for depth (point, clamp)
SamplerState sampler_point_clamp : register(s0);

// Reconstruct view-space position from depth + UV
float3 reconstruct_view_pos(float2 uv, float depth) {
    // Unproject from NDC to view space using projection matrix inverse
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;  // DX12 convention
    float4 view_pos = float4(
        ndc.x / ssao_proj[0][0],
        ndc.y / ssao_proj[1][1],
        1.0,
        1.0
    );
    return view_pos.xyz * depth;  // linearize: view_z = depth (after linearization)
}

// Linearize hardware depth to view-space Z
float linearize_depth(float d) {
    // For standard DX12 projection: z_ndc = (z_view * proj[2][2] + proj[3][2]) / z_view
    // Solving for z_view: z_view = proj[3][2] / (d - proj[2][2])
    return ssao_proj[3][2] / (d - ssao_proj[2][2]);
}

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    // Half-res: map to full-res UV
    float2 uv = (float2(tid) + 0.5) * ssao_texel_size;

    // Sample depth and early-out for sky pixels
    float raw_depth = g_depth.SampleLevel(sampler_point_clamp, uv, 0);
    if (raw_depth >= 1.0) {
        g_ao_out[tid] = 1.0;  // no occlusion in sky
        return;
    }

    // Linearize depth to view-space Z
    float view_z = linearize_depth(raw_depth);

    // Distance-based fade: no AO beyond max_distance
    if (view_z > ssao_max_distance) {
        g_ao_out[tid] = 1.0;
        return;
    }

    // Reconstruct view-space position
    float3 view_pos = reconstruct_view_pos(uv, view_z);

    // Decode world normal from GBuffer1, transform to view space would be ideal
    // but for HBAO we work in screen/view space
    float4 gbuf1 = g_normal.SampleLevel(sampler_point_clamp, uv, 0);
    float3 world_normal = oct_decode(gbuf1.rg);

    // Random rotation angle per pixel (temporal noise via frame index from g_Time)
    float noise_angle = hash21(float2(tid) + frac(g_Time) * 100.0) * TWO_PI;

    // Convert world-space radius to screen-space radius (in pixels)
    // Approximate: screen_radius = world_radius * proj[0][0] / view_z
    float screen_radius = ssao_radius * ssao_proj[0][0] / view_z;

    // Clamp screen radius to avoid excessive sampling near camera
    screen_radius = clamp(screen_radius, 2.0, 256.0);

    // Step size in screen space (pixels -> UV)
    float step_size = screen_radius / (float)ssao_num_steps;

    float occlusion = 0.0;

    // March along each direction in 2D screen space
    for (uint dir = 0; dir < ssao_num_directions; ++dir) {
        // Direction angle: evenly spaced + random offset
        float angle = (float(dir) / float(ssao_num_directions)) * TWO_PI + noise_angle;
        float2 march_dir = float2(cos(angle), sin(angle));

        // Track the maximum horizon angle along this direction
        float max_horizon = -1.0;  // sin of the maximum elevation angle found

        for (uint s = 1; s <= ssao_num_steps; ++s) {
            // Sample position in UV space
            float2 sample_uv = uv + march_dir * float(s) * step_size * ssao_full_texel_size;

            // Bounds check
            if (any(sample_uv < 0.0) || any(sample_uv > 1.0)) break;

            // Sample depth at this location
            float sample_depth = g_depth.SampleLevel(sampler_point_clamp, sample_uv, 0);
            if (sample_depth >= 1.0) continue;  // sky

            float sample_z = linearize_depth(sample_depth);
            float3 sample_pos = reconstruct_view_pos(sample_uv, sample_z);

            // Vector from current pixel to sample
            float3 horizon_vec = sample_pos - view_pos;
            float  horizon_dist = length(horizon_vec);

            // Skip samples outside the world-space radius
            if (horizon_dist > ssao_radius) continue;

            // Compute the sine of the horizon angle
            // sin(elevation) = (sample_z - view_z) / horizon_dist
            float horizon_sin = (horizon_vec.z) / (horizon_dist + EPSILON);

            // Apply bias to prevent self-occlusion
            horizon_sin -= ssao_bias;

            // Track maximum horizon
            max_horizon = max(max_horizon, horizon_sin);
        }

        // Compare the tangent angle (from normal) against the horizon angle
        // Tangent angle in this direction: dot(normal_view, march_dir_3d)
        // Simplified: we assume the tangent plane is at sin = 0
        // Occlusion contribution = max(0, max_horizon)
        // Weight by distance attenuation (closer samples contribute more)
        occlusion += saturate(max_horizon);
    }

    // Average over all directions
    occlusion /= float(ssao_num_directions);

    // Apply intensity and output
    float ao = 1.0 - saturate(occlusion * ssao_intensity);

    // Distance fade: smoothly reduce AO at large distances
    float distance_fade = 1.0 - saturate((view_z - ssao_max_distance * 0.8) /
                                          (ssao_max_distance * 0.2));
    ao = lerp(1.0, ao, distance_fade);

    g_ao_out[tid] = ao;
}
```

### HLSL: Bilateral Blur

```hlsl
// shaders/post/ssao_blur.hlsl
//
// Cross-bilateral blur for SSAO denoising.
// Preserves depth edges to prevent AO bleeding across object boundaries.
// Run twice: once horizontal (#define BLUR_HORIZONTAL), once vertical.

#include "common.hlsli"

cbuffer BlurParams : register(b2) {
    float2 blur_texel_size;   // 1.0 / half-res dimensions
    float  blur_sharpness;    // depth edge preservation (higher = sharper edges, default 8.0)
    float  _pad;
};

Texture2D<float> g_ao_input  : register(t0);
Texture2D<float> g_depth     : register(t1);
SamplerState     sampler_point_clamp : register(s0);

RWTexture2D<float> g_ao_output : register(u0);

// 4-tap kernel weights (Gaussian sigma ~= 2.0)
static const int   BLUR_RADIUS = 4;
static const float kernel[5] = { 0.153170, 0.144893, 0.122649, 0.092902, 0.062970 };

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * blur_texel_size;

    float center_ao    = g_ao_input.SampleLevel(sampler_point_clamp, uv, 0);
    float center_depth = g_depth.SampleLevel(sampler_point_clamp, uv, 0);

    float total_ao     = center_ao * kernel[0];
    float total_weight = kernel[0];

    // Blur direction: set via shader define
    #ifdef BLUR_HORIZONTAL
    float2 blur_dir = float2(blur_texel_size.x, 0.0);
    #else
    float2 blur_dir = float2(0.0, blur_texel_size.y);
    #endif

    [unroll]
    for (int i = 1; i <= BLUR_RADIUS; ++i) {
        float w = kernel[i];

        // Positive offset
        float2 offset_uv = uv + blur_dir * float(i);
        float  sample_ao    = g_ao_input.SampleLevel(sampler_point_clamp, offset_uv, 0);
        float  sample_depth = g_depth.SampleLevel(sampler_point_clamp, offset_uv, 0);

        // Bilateral weight: reduce contribution from samples across depth edges
        float depth_diff = abs(center_depth - sample_depth);
        float bilateral  = exp(-depth_diff * blur_sharpness);

        total_ao     += sample_ao * w * bilateral;
        total_weight += w * bilateral;

        // Negative offset
        offset_uv = uv - blur_dir * float(i);
        sample_ao    = g_ao_input.SampleLevel(sampler_point_clamp, offset_uv, 0);
        sample_depth = g_depth.SampleLevel(sampler_point_clamp, offset_uv, 0);

        depth_diff = abs(center_depth - sample_depth);
        bilateral  = exp(-depth_diff * blur_sharpness);

        total_ao     += sample_ao * w * bilateral;
        total_weight += w * bilateral;
    }

    g_ao_output[tid] = total_ao / (total_weight + EPSILON);
}
```

### Integration into Deferred Lighting

The blurred AO buffer is sampled in the deferred lighting pass and applied as a multiplicative factor on the ambient/IBL term only. Direct lighting is not affected by SSAO (this is physically more correct: direct light should not be occluded by local geometry that the shadow map already handles).

```hlsl
// In deferred_lighting.hlsl, after computing ambient/IBL:
float ao = g_ao_buffer.SampleLevel(sampler_linear_clamp, uv, 0);  // half-res, bilinear upsample
float3 ambient = (irradiance * kD * albedo + specular_ibl) * ao;
```

### IPC Handlers

```json
renderer.setSSAO {
    "enabled": true,
    "quality": "Medium",
    "radius": 0.5,
    "intensity": 1.0,
    "bias": 0.025,
    "max_distance": 100.0
}
```

### Performance Target

Less than 1ms total at 1080p (Medium quality): ~0.6ms HBAO compute + ~0.2ms bilateral blur (2 passes).

---

## B. SSR -- Screen-Space Reflections (Hi-Z Ray Marching)

### Algorithm

Screen-Space Reflections trace reflection rays against the depth buffer to find reflected geometry visible on screen. UzEngine uses **Hierarchical Z-Buffer (Hi-Z) ray marching**, which traverses a mip chain of the depth buffer for efficient large-step intersection testing.

The Hi-Z approach works by:
1. Building a min-depth mip chain from the depth buffer (each mip level stores the minimum depth of its 2x2 parent region)
2. Starting the ray march at the coarsest mip level (large steps)
3. When a potential intersection is detected, dropping to the next finer mip level (smaller steps)
4. Converging to a pixel-accurate hit at mip level 0

This is significantly faster than linear ray marching because it skips large empty regions in a single step.

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `enabled` | bool | `true` | - | Toggle effect |
| `max_distance` | float | `50.0` | 5.0 - 200.0 | Maximum ray distance (world-space meters) |
| `max_steps` | uint | `64` | 16 - 128 | Maximum Hi-Z traversal steps |
| `thickness` | float | `0.1` | 0.01 - 1.0 | Depth thickness for hit detection (meters) |
| `stride` | uint | `8` | 1 - 32 | Initial stride in pixels at mip 0 (for linear phase) |
| `fade_start` | float | `0.8` | 0.0 - 1.0 | Screen-edge fade start (fraction of half-screen) |
| `fade_end` | float | `1.0` | 0.0 - 1.0 | Screen-edge fade end |
| `roughness_cutoff` | float | `0.4` | 0.0 - 1.0 | Skip SSR for pixels rougher than this (use IBL only) |

### Render Resources

| Resource | Format | Resolution | Description |
|----------|--------|------------|-------------|
| `hiz_mips` | `R32_FLOAT` | Full-res + N mip levels | Hierarchical Z-buffer (min-depth pyramid) |
| `ssr_result` | `RGBA16F` | Full-res | Ray-march output: hit color (RGB) + confidence (A) |
| `ssr_history` | `RGBA16F` | Full-res | Previous frame SSR for temporal accumulation |

Number of Hi-Z mip levels: `floor(log2(max(width, height)))` -- typically 10-11 levels for 1080p.

### Pass 1: Hi-Z Mip Chain Generation

```hlsl
// shaders/post/hiz_generate.hlsl
//
// Generates a hierarchical Z-buffer (min-depth mip pyramid).
// Each texel stores the MINIMUM depth of the 2x2 region from the previous level.
// Mip 0 = full-res depth buffer copy.
// Used by SSR for efficient ray traversal.

#include "common.hlsli"

Texture2D<float>   g_depth_src  : register(t0);  // source mip level
RWTexture2D<float> g_depth_dst  : register(u0);  // destination mip level

cbuffer HiZParams : register(b2) {
    uint2 dst_dimensions;  // width, height of destination mip
    uint2 _pad;
};

SamplerState sampler_point_clamp : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    if (any(tid >= dst_dimensions)) return;

    // Sample 2x2 region from source mip
    uint2 src_coord = tid * 2;

    // Gather the 4 source texels (using Load for exact texel fetch)
    float d00 = g_depth_src.Load(int3(src_coord + uint2(0, 0), 0));
    float d10 = g_depth_src.Load(int3(src_coord + uint2(1, 0), 0));
    float d01 = g_depth_src.Load(int3(src_coord + uint2(0, 1), 0));
    float d11 = g_depth_src.Load(int3(src_coord + uint2(1, 1), 0));

    // Store MINIMUM depth (closest to camera) for conservative ray traversal
    // In standard (non-reversed) Z: min = closest
    // For reversed-Z: use max instead
    float min_depth = min(min(d00, d10), min(d01, d11));

    g_depth_dst[tid] = min_depth;
}
```

Dispatch once per mip level, from mip 1 down to the smallest mip. Mip 0 is a direct copy of the depth buffer.

### Pass 2: SSR Ray March

```hlsl
// shaders/post/ssr_raymarch.hlsl
//
// Screen-Space Reflections using Hierarchical Z-Buffer ray marching.
// For each pixel, reflects the view direction around the surface normal,
// then marches the reflection ray through the Hi-Z pyramid to find intersections.
//
// Output: RGBA16F -- reflected color (RGB) + confidence (A)
// Confidence: 1.0 = solid hit, 0.0 = miss or faded

#include "common.hlsli"
#include "global_cbuffer.hlsli"

cbuffer SSRParams : register(b2) {
    float4x4 ssr_view;           // current view matrix
    float4x4 ssr_proj;           // current projection matrix
    float4x4 ssr_view_proj;      // current view * projection
    float4x4 ssr_inv_view_proj;  // inverse of view * projection

    float  ssr_max_distance;     // max ray distance in world space (meters)
    float  ssr_thickness;        // depth comparison thickness (meters)
    uint   ssr_max_steps;        // max Hi-Z traversal steps
    uint   ssr_stride;           // initial stride in pixels

    float  ssr_fade_start;       // screen-edge fade start fraction
    float  ssr_fade_end;         // screen-edge fade end fraction
    float  ssr_roughness_cutoff; // skip SSR above this roughness
    float  ssr_temporal_blend;   // blend with previous frame (0.0 = off, 0.9 = heavy)

    float2 ssr_screen_size;      // viewport dimensions in pixels
    float2 ssr_texel_size;       // 1.0 / screen_size
};

// Input textures
Texture2D<float>  g_depth       : register(t0);  // hardware depth
Texture2D<float4> g_gbuffer1    : register(t1);  // normal + roughness + metallic
Texture2D<float4> g_hdr_color   : register(t2);  // lit HDR scene color
Texture2D<float>  g_hiz         : register(t3);  // Hi-Z mip pyramid
Texture2D<float2> g_velocity    : register(t4);  // velocity buffer
Texture2D<float4> g_ssr_history : register(t5);  // previous frame SSR result

SamplerState sampler_point_clamp  : register(s0);
SamplerState sampler_linear_clamp : register(s1);

// Output
RWTexture2D<float4> g_ssr_out : register(u0);  // reflected color (RGB) + confidence (A)

// Project a view-space position to screen UV
float2 view_to_uv(float3 view_pos) {
    float4 clip = mul(ssr_proj, float4(view_pos, 1.0));
    float2 ndc = clip.xy / clip.w;
    return ndc * float2(0.5, -0.5) + 0.5;
}

// Screen-edge fade: reduce confidence near viewport borders
float screen_edge_fade(float2 uv) {
    float2 d = abs(uv * 2.0 - 1.0);  // distance from center, [0,1] range
    float fade_x = 1.0 - smoothstep(ssr_fade_start, ssr_fade_end, d.x);
    float fade_y = 1.0 - smoothstep(ssr_fade_start, ssr_fade_end, d.y);
    return fade_x * fade_y;
}

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * ssr_texel_size;

    // Read G-Buffer
    float raw_depth = g_depth.SampleLevel(sampler_point_clamp, uv, 0);
    float4 gbuf1 = g_gbuffer1.SampleLevel(sampler_point_clamp, uv, 0);

    float roughness = gbuf1.b;
    float metallic  = gbuf1.a;

    // Early out: sky pixels or rough surfaces
    if (raw_depth >= 1.0 || roughness > ssr_roughness_cutoff) {
        g_ssr_out[tid] = float4(0, 0, 0, 0);
        return;
    }

    // Decode world normal
    float3 world_normal = oct_decode(gbuf1.rg);

    // Reconstruct world position
    float3 world_pos = reconstruct_world_pos(uv, raw_depth, ssr_inv_view_proj);

    // View direction: camera to pixel
    float3 cam_pos = float3(
        ssr_inv_view_proj[3][0],
        ssr_inv_view_proj[3][1],
        ssr_inv_view_proj[3][2]
    );
    float3 view_dir = normalize(world_pos - cam_pos);

    // Reflection direction
    float3 reflect_dir = reflect(view_dir, world_normal);

    // Reflection ray in screen space
    float3 ray_start = world_pos;
    float3 ray_end   = world_pos + reflect_dir * ssr_max_distance;

    // Project both endpoints to screen UV
    float4 start_clip = mul(ssr_view_proj, float4(ray_start, 1.0));
    float4 end_clip   = mul(ssr_view_proj, float4(ray_end, 1.0));

    float2 start_uv = (start_clip.xy / start_clip.w) * float2(0.5, -0.5) + 0.5;
    float2 end_uv   = (end_clip.xy / end_clip.w) * float2(0.5, -0.5) + 0.5;

    // Screen-space ray direction
    float2 ray_uv = end_uv - start_uv;
    float  ray_length = length(ray_uv * ssr_screen_size);  // in pixels

    // Clamp to max steps
    uint num_steps = min(ssr_max_steps, (uint)ray_length);
    if (num_steps == 0) {
        g_ssr_out[tid] = float4(0, 0, 0, 0);
        return;
    }

    // Hi-Z ray march
    float2 step_uv = ray_uv / float(num_steps);
    float  start_z = start_clip.w;
    float  end_z   = end_clip.w;
    float  step_z  = (end_z - start_z) / float(num_steps);

    float2 curr_uv = start_uv;
    float  curr_z  = start_z;
    bool   hit     = false;
    float2 hit_uv  = float2(0, 0);

    for (uint i = 1; i <= num_steps; ++i) {
        curr_uv += step_uv;
        curr_z  += step_z;

        // Bounds check
        if (any(curr_uv < 0.0) || any(curr_uv > 1.0)) break;

        // Sample scene depth at ray position
        float scene_depth_raw = g_depth.SampleLevel(sampler_point_clamp, curr_uv, 0);

        // Convert raw depth to linear view-space Z for comparison
        // Approximate: use the ray's interpolated Z vs the depth buffer
        float scene_z = ssr_proj[3][2] / (scene_depth_raw - ssr_proj[2][2]);

        float depth_diff = curr_z - scene_z;

        // Hit: ray is behind surface, within thickness tolerance
        if (depth_diff > 0.0 && depth_diff < ssr_thickness * curr_z) {
            hit = true;
            hit_uv = curr_uv;
            break;
        }
    }

    if (hit) {
        // Fetch reflected color from HDR buffer
        float3 reflected_color = g_hdr_color.SampleLevel(sampler_linear_clamp, hit_uv, 0).rgb;

        // Confidence based on screen-edge fade
        float confidence = screen_edge_fade(hit_uv);

        // Roughness fade: smoother surfaces get cleaner reflections
        float roughness_fade = 1.0 - saturate(roughness / ssr_roughness_cutoff);
        confidence *= roughness_fade;

        // Temporal blending with previous frame
        if (ssr_temporal_blend > 0.0) {
            float2 velocity = g_velocity.SampleLevel(sampler_point_clamp, uv, 0);
            float2 prev_uv = uv - velocity;

            if (all(prev_uv >= 0.0) && all(prev_uv <= 1.0)) {
                float4 history = g_ssr_history.SampleLevel(sampler_linear_clamp, prev_uv, 0);
                reflected_color = lerp(reflected_color, history.rgb, ssr_temporal_blend);
                confidence = lerp(confidence, history.a, ssr_temporal_blend * 0.5);
            }
        }

        g_ssr_out[tid] = float4(reflected_color, confidence);
    } else {
        g_ssr_out[tid] = float4(0, 0, 0, 0);
    }
}
```

### Pass 3: SSR Resolve

```hlsl
// shaders/post/ssr_resolve.hlsl
//
// Blends SSR result with IBL fallback reflections.
// SSR has priority where confidence > 0; IBL fills in where SSR misses.

#include "common.hlsli"

Texture2D<float4> g_hdr_color  : register(t0);  // current HDR color (has IBL already)
Texture2D<float4> g_ssr_result : register(t1);  // SSR: reflected color (RGB) + confidence (A)
Texture2D<float4> g_gbuffer1   : register(t2);  // normal + roughness + metallic

SamplerState sampler_linear_clamp : register(s0);

RWTexture2D<float4> g_output : register(u0);

cbuffer SSRResolveParams : register(b2) {
    float ssr_strength;      // global SSR intensity multiplier [0,1]
    float ssr_roughness_max; // roughness above which SSR = 0
    float2 _pad;
};

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = float2(tid) + 0.5;  // pixel center

    float4 hdr = g_hdr_color[tid];
    float4 ssr = g_ssr_result[tid];

    float roughness = g_gbuffer1[tid].b;
    float metallic  = g_gbuffer1[tid].a;

    // SSR contribution scaled by confidence and global strength
    float ssr_weight = ssr.a * ssr_strength;

    // Fresnel-weighted blend: metals reflect more strongly
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), hdr.rgb, metallic);

    // Blend SSR on top of existing color
    // The HDR buffer already contains IBL specular.
    // We replace the IBL specular portion with SSR where confident.
    float3 final_color = lerp(hdr.rgb, ssr.rgb, ssr_weight);

    g_output[tid] = float4(final_color, hdr.a);
}
```

### IPC Handlers

```json
renderer.setSSR {
    "enabled": true,
    "max_steps": 64,
    "max_distance": 50.0,
    "thickness": 0.1,
    "roughness_cutoff": 0.4,
    "temporal_blend": 0.8
}
```

### Performance Target

Less than 2ms total at 1080p:
- Hi-Z generation: ~0.2ms (10-11 dispatches, tiny at upper mips)
- Ray march: ~1.2ms (full-res, 64 steps, roughness early-out helps)
- Resolve: ~0.1ms (simple blend)
- Temporal: ~0.1ms (reprojection + blend)

---

## C. TAA -- Temporal Anti-Aliasing

### Algorithm

Temporal Anti-Aliasing eliminates geometric aliasing by accumulating sub-pixel samples across multiple frames. Each frame, a sub-pixel jitter offset is applied to the projection matrix (shifting the rendered image by a fraction of a pixel). The TAA resolve pass reprojects the previous frame's result to the current frame using motion vectors, then blends the two.

Key components:
1. **Halton jitter**: 8-sample Halton(2,3) sequence applied to the projection matrix
2. **Motion vector reprojection**: Uses the velocity buffer to find the previous frame's corresponding pixel
3. **Variance clamping (neighborhood clipping)**: Prevents ghosting by constraining the history color to the min/max range of the current frame's 3x3 neighborhood
4. **Sharpening**: Catmull-Rom kernel to counter TAA-induced blur

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `enabled` | bool | `true` | - | Toggle effect |
| `blend_factor` | float | `0.1` | 0.02 - 0.3 | Weight of current frame (lower = more temporal stability, more ghosting) |
| `variance_clip_gamma` | float | `1.0` | 0.5 - 2.0 | Variance clamp strictness (lower = more aggressive ghost rejection) |
| `sharpen_strength` | float | `0.3` | 0.0 - 1.0 | Post-TAA sharpening intensity |
| `jitter_samples` | uint | `8` | 4/8/16 | Halton sequence length before repeating |

### Halton Jitter Sequence

The jitter is applied to the projection matrix before any geometry rendering. The jitter offset shifts by a sub-pixel amount (in NDC) each frame.

```cpp
// CPU-side: compute jitter and apply to projection matrix
// src/renderer/taa.h

struct TAAState {
    uint32_t frame_index;           // incremented each frame, mod jitter_samples
    float    jitter_x, jitter_y;    // current jitter in pixel units
    float4x4 jittered_proj;         // projection with jitter applied
    float4x4 prev_view_proj;        // previous frame's jittered view * proj
};

// Halton sequence base 2 and base 3
float halton(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    uint32_t i = index;
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

// 8-sample Halton(2,3) jitter table (pre-computed)
static const float2 HALTON_8[8] = {
    { halton(1,2) - 0.5, halton(1,3) - 0.5 },  // { 0.0,    -0.167 }
    { halton(2,2) - 0.5, halton(2,3) - 0.5 },  // { -0.25,   0.167 }
    { halton(3,2) - 0.5, halton(3,3) - 0.5 },  // { 0.25,   -0.389 }
    { halton(4,2) - 0.5, halton(4,3) - 0.5 },  // { -0.375,  -0.056 }
    { halton(5,2) - 0.5, halton(5,3) - 0.5 },  // { 0.125,   0.278 }
    { halton(6,2) - 0.5, halton(6,3) - 0.5 },  // { -0.125, -0.278 }
    { halton(7,2) - 0.5, halton(7,3) - 0.5 },  // { 0.375,   0.056 }
    { halton(8,2) - 0.5, halton(8,3) - 0.5 },  // { -0.438,  0.389 }
};

void apply_jitter(TAAState* taa, float4x4* proj, uint32_t screen_w, uint32_t screen_h) {
    uint32_t idx = taa->frame_index % taa->jitter_samples;
    float2 jitter = HALTON_8[idx];

    // Convert pixel jitter to NDC offset
    taa->jitter_x = jitter.x;
    taa->jitter_y = jitter.y;

    // Apply jitter to projection matrix (offset in NDC = pixel offset / screen_size * 2)
    taa->jittered_proj = *proj;
    taa->jittered_proj[2][0] += jitter.x * 2.0f / (float)screen_w;
    taa->jittered_proj[2][1] += jitter.y * 2.0f / (float)screen_h;

    taa->frame_index++;
}
```

### Render Resources

| Resource | Format | Resolution | Description |
|----------|--------|------------|-------------|
| `taa_history` | `RGBA16F` or `R11G11B10F` | Full-res | Previous frame's TAA output (ping-pong with current) |
| `taa_output` | Same as history | Full-res | Current frame's TAA result |

Two history buffers are ping-ponged each frame. The TAA resolve reads from one and writes to the other.

### HLSL: TAA Resolve

```hlsl
// shaders/post/taa.hlsl
//
// Temporal Anti-Aliasing resolve pass.
// Blends the current (jittered) frame with the reprojected history buffer.
// Uses variance clamping (min/max neighborhood) to reject ghosting artifacts.
// Applies Catmull-Rom sharpening to counter TAA-induced blur.

#include "common.hlsli"

cbuffer TAAParams : register(b2) {
    float  taa_blend_factor;         // weight of current frame (0.1 = 10% new, 90% history)
    float  taa_variance_clip_gamma;  // variance clamp strictness
    float  taa_sharpen_strength;     // post-TAA sharpening
    float  _pad0;

    float2 taa_screen_size;          // viewport dimensions in pixels
    float2 taa_texel_size;           // 1.0 / screen_size

    float2 taa_jitter;              // current frame jitter in pixel units
    float2 _pad1;
};

// Input
Texture2D<float4> g_current_color : register(t0);  // current frame (tonemapped LDR or HDR)
Texture2D<float4> g_history       : register(t1);  // previous TAA output
Texture2D<float2> g_velocity      : register(t2);  // motion vectors

SamplerState sampler_point_clamp  : register(s0);
SamplerState sampler_linear_clamp : register(s1);

// Output
RWTexture2D<float4> g_taa_out : register(u0);

// Convert RGB to YCoCg for better variance clamping
// YCoCg has decorrelated luminance channel, improving clamp quality
float3 rgb_to_ycocg(float3 rgb) {
    float y  = dot(rgb, float3(0.25, 0.5, 0.25));
    float co = dot(rgb, float3(0.5, 0.0, -0.5));
    float cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(y, co, cg);
}

float3 ycocg_to_rgb(float3 ycocg) {
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return float3(y + co - cg, y + cg, y - co - cg);
}

// Catmull-Rom bicubic sample (5-tap approximation)
// Provides sharper sampling than bilinear without excessive ringing
float4 sample_catmull_rom(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 texel_size) {
    float2 position = uv / texel_size;
    float2 center   = floor(position - 0.5) + 0.5;
    float2 f        = position - center;
    float2 f2       = f * f;
    float2 f3       = f2 * f;

    // Catmull-Rom weights
    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    float2 w3 = 0.5 * (f3 - f2);

    // Combine into 5-tap pattern (2 bilinear samples + center)
    float2 w12 = w1 + w2;
    float2 tc12 = (center + w2 / w12) * texel_size;
    float2 tc0  = (center - 1.0) * texel_size;
    float2 tc3  = (center + 2.0) * texel_size;

    float4 result =
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0) * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc0.x,  tc12.y), 0) * (w0.x  * w12.y) +
        tex.SampleLevel(samp, float2(tc3.x,  tc12.y), 0) * (w3.x  * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y),  0) * (w12.x * w0.y)  +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y),  0) * (w12.x * w3.y);

    return result / (w12.x * w12.y + w0.x * w12.y + w3.x * w12.y + w12.x * w0.y + w12.x * w3.y);
}

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * taa_texel_size;

    // ---- Current frame color (unjitter the sample) ----
    // The current frame was rendered with jitter, so we sample at the jittered position
    float3 current = g_current_color[tid].rgb;

    // ---- Neighborhood min/max for variance clamping ----
    // Sample a 3x3 neighborhood around the current pixel in YCoCg space
    float3 m1 = float3(0, 0, 0);  // mean
    float3 m2 = float3(0, 0, 0);  // mean of squares (for variance)

    float3 neighborhood_min = float3(1e6, 1e6, 1e6);
    float3 neighborhood_max = float3(-1e6, -1e6, -1e6);

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            int2 coord = int2(tid) + int2(x, y);
            coord = clamp(coord, int2(0, 0), int2(taa_screen_size) - 1);

            float3 sample_rgb   = g_current_color[coord].rgb;
            float3 sample_ycocg = rgb_to_ycocg(sample_rgb);

            m1 += sample_ycocg;
            m2 += sample_ycocg * sample_ycocg;

            neighborhood_min = min(neighborhood_min, sample_ycocg);
            neighborhood_max = max(neighborhood_max, sample_ycocg);
        }
    }

    // Compute mean and standard deviation for variance clipping
    m1 /= 9.0;
    m2 /= 9.0;
    float3 sigma = sqrt(abs(m2 - m1 * m1));

    // Expand the AABB by gamma * sigma for softer clipping
    float3 aabb_min = m1 - taa_variance_clip_gamma * sigma;
    float3 aabb_max = m1 + taa_variance_clip_gamma * sigma;

    // ---- Reprojection: find previous frame's pixel ----
    float2 velocity = g_velocity.SampleLevel(sampler_point_clamp, uv, 0);
    float2 prev_uv  = uv - velocity;

    // ---- Sample history using Catmull-Rom for sharper results ----
    float3 history;
    if (all(prev_uv >= 0.0) && all(prev_uv <= 1.0)) {
        history = sample_catmull_rom(g_history, sampler_linear_clamp,
                                     prev_uv, taa_texel_size).rgb;
    } else {
        // History sample is off-screen: use current frame only
        g_taa_out[tid] = float4(current, 1.0);
        return;
    }

    // ---- Variance clamp: clip history to neighborhood AABB ----
    float3 history_ycocg = rgb_to_ycocg(history);

    // Clip the history towards the center of the AABB
    float3 clipped = clamp(history_ycocg, aabb_min, aabb_max);
    history = ycocg_to_rgb(clipped);

    // ---- Blend current and history ----
    // Higher blend_factor = more responsive (less ghosting, more aliasing)
    // Lower blend_factor = more stable (more ghosting, less aliasing)
    float blend = taa_blend_factor;

    // Increase blend factor when there is large disocclusion (velocity magnitude)
    float velocity_mag = length(velocity * taa_screen_size);
    blend = lerp(blend, 0.5, saturate(velocity_mag * 0.1));

    float3 result = lerp(history, current, blend);

    // ---- Post-TAA sharpening (unsharp mask) ----
    if (taa_sharpen_strength > 0.0) {
        // Simple 4-tap Laplacian for sharpening
        float3 blur = float3(0, 0, 0);
        blur += g_current_color[tid + int2( 1, 0)].rgb;
        blur += g_current_color[tid + int2(-1, 0)].rgb;
        blur += g_current_color[tid + int2( 0, 1)].rgb;
        blur += g_current_color[tid + int2( 0,-1)].rgb;
        blur *= 0.25;

        float3 sharp = current - blur;  // high-frequency detail
        result += sharp * taa_sharpen_strength;
    }

    // Clamp to valid range (prevent negative from sharpening overshoot)
    result = max(result, 0.0);

    g_taa_out[tid] = float4(result, 1.0);
}
```

### TAA Jitter in Geometry Passes

The jittered projection matrix must be used for ALL geometry rendering (G-Buffer pass, depth prepass, shadow passes should NOT use jitter). Only the main camera projection is jittered.

```
Passes that USE jittered projection:
  - G-Buffer pass (vertex shader: view_proj = jittered_proj * view)
  - Forward transparent pass

Passes that do NOT use jittered projection:
  - Shadow map passes (shadow cameras are unjittered)
  - Depth prepass (if used for Hi-Z generation before TAA)
  - Post-process passes (operate on screen-space data)
```

### IPC Handlers

```json
renderer.setTAA {
    "enabled": true,
    "blend": 0.1,
    "sharpen": 0.3,
    "variance_clip": 1.0
}
```

### Performance Target

Less than 0.5ms at 1080p: single compute dispatch with 3x3 neighborhood gather + history sample + blend + sharpen.

---

## Shared HLSL Include Files

The screen-space effects add the following shared include to `shaders/include/`:

### `screen_space.hlsli`

```hlsl
#ifndef UZ_SCREEN_SPACE_HLSLI
#define UZ_SCREEN_SPACE_HLSLI

#include "common.hlsli"

// ---- Depth Utilities ----

// Linearize hardware depth to view-space Z
// proj = projection matrix
float linearize_depth(float raw_depth, float4x4 proj) {
    return proj[3][2] / (raw_depth - proj[2][2]);
}

// Reconstruct view-space position from UV + linear depth
float3 uv_to_view_pos(float2 uv, float linear_depth, float4x4 proj) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float3(
        ndc.x / proj[0][0],
        ndc.y / proj[1][1],
        1.0
    ) * linear_depth;
}

// ---- Color Space Conversion (for TAA variance clamping) ----

float3 rgb_to_ycocg(float3 rgb) {
    float y  = dot(rgb, float3(0.25, 0.5, 0.25));
    float co = dot(rgb, float3(0.5, 0.0, -0.5));
    float cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(y, co, cg);
}

float3 ycocg_to_rgb(float3 ycocg) {
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return float3(y + co - cg, y + cg, y - co - cg);
}

// ---- Screen-Space Ray Utilities ----

// Project world-space position to screen UV
float2 world_to_screen_uv(float3 world_pos, float4x4 view_proj) {
    float4 clip = mul(view_proj, float4(world_pos, 1.0));
    float2 ndc  = clip.xy / clip.w;
    return ndc * float2(0.5, -0.5) + 0.5;
}

// Compute screen-edge fade (for SSR and other screen-space effects)
// Returns 0 at edges, 1 at center
float compute_screen_edge_fade(float2 uv, float fade_start, float fade_end) {
    float2 d = abs(uv * 2.0 - 1.0);
    float fade_x = 1.0 - smoothstep(fade_start, fade_end, d.x);
    float fade_y = 1.0 - smoothstep(fade_start, fade_end, d.y);
    return fade_x * fade_y;
}

// ---- Halton Sequence (for TAA jitter, importance sampling) ----

// Compute Halton sequence value at index for given base
float halton(uint index, uint base) {
    float f = 1.0;
    float r = 0.0;
    uint i = index;
    while (i > 0) {
        f /= float(base);
        r += f * float(i % base);
        i /= base;
    }
    return r;
}

// Pre-computed 8-sample Halton(2,3) jitter offsets centered at [-0.5, 0.5]
static const float2 HALTON_JITTER_8[8] = {
    float2( 0.0,     -0.16667),
    float2(-0.25,     0.16667),
    float2( 0.25,    -0.38889),
    float2(-0.375,   -0.05556),
    float2( 0.125,    0.27778),
    float2(-0.125,   -0.27778),
    float2( 0.375,    0.05556),
    float2(-0.4375,   0.38889),
};

#endif // UZ_SCREEN_SPACE_HLSLI
```

---

## `.uzmat` Post-Process Settings

Post-process settings are stored in a material file used by the post-process volume:

```ini
; res://materials/post_process.uzmat
[uzmaterial version=1]
shader = "builtin://post_process"

[ssao]
enabled = true
quality = Medium       ; Low / Medium / High
radius = 0.5           ; world-space meters
intensity = 1.0
bias = 0.025
max_distance = 100.0

[ssr]
enabled = true
max_steps = 64
max_distance = 50.0
thickness = 0.1
roughness_cutoff = 0.4
temporal_blend = 0.8

[taa]
enabled = true
blend = 0.1
sharpen = 0.3
variance_clip = 1.0
jitter_samples = 8
```

These can be overridden per-scene via the scene file or at runtime via IPC.

---

## CPU-Side Architecture

### PostProcessStack Manager

```cpp
// src/renderer/post_process/post_process_stack.h

struct PostProcessSettings {
    // SSAO
    bool   ssao_enabled;
    int    ssao_quality;         // 0=Low, 1=Medium, 2=High
    float  ssao_radius;
    float  ssao_bias;
    float  ssao_intensity;
    float  ssao_max_distance;

    // SSR
    bool   ssr_enabled;
    uint32_t ssr_max_steps;
    float  ssr_max_distance;
    float  ssr_thickness;
    float  ssr_roughness_cutoff;
    float  ssr_temporal_blend;

    // TAA
    bool   taa_enabled;
    float  taa_blend_factor;
    float  taa_sharpen_strength;
    float  taa_variance_clip_gamma;
    uint32_t taa_jitter_samples;
};

struct PostProcessStack {
    PostProcessSettings settings;

    // SSAO resources
    ID3D12Resource* ao_raw;         // half-res R8
    ID3D12Resource* ao_blur_temp;   // half-res R8
    ID3D12Resource* ao_final;       // half-res R8

    // SSR resources
    ID3D12Resource* hiz_mips;       // full-res + mip chain, R32F
    ID3D12Resource* ssr_result;     // full-res RGBA16F
    ID3D12Resource* ssr_history[2]; // ping-pong RGBA16F

    // TAA resources
    ID3D12Resource* taa_history[2]; // ping-pong RGBA16F or R11G11B10F
    uint32_t        taa_history_idx;// current ping-pong index (0 or 1)
    TAAState        taa_state;

    // PSOs
    ID3D12PipelineState* pso_hbao;
    ID3D12PipelineState* pso_ssao_blur_h;
    ID3D12PipelineState* pso_ssao_blur_v;
    ID3D12PipelineState* pso_hiz_gen;
    ID3D12PipelineState* pso_ssr_march;
    ID3D12PipelineState* pso_ssr_resolve;
    ID3D12PipelineState* pso_taa;
};

// Initialize all post-process resources and PSOs
void post_process_init(PostProcessStack* stack, ID3D12Device* device,
                       uint32_t width, uint32_t height);

// Resize all resources when viewport changes
void post_process_resize(PostProcessStack* stack, ID3D12Device* device,
                         uint32_t width, uint32_t height);

// Execute the full post-process pipeline
// cmd_list: the graphics command list to record into
// frame_ctx: per-frame data (matrices, time, etc.)
void post_process_execute(PostProcessStack* stack,
                          ID3D12GraphicsCommandList* cmd_list,
                          const FrameContext* frame_ctx);

// Cleanup
void post_process_shutdown(PostProcessStack* stack);
```

### Resource Barriers

Each post-process pass transitions resources between SRV (read) and UAV (write) states. The barrier schedule for a full post-process frame:

```
Depth:      SRV (read by SSAO + SSR)
GBuffer1:   SRV (read by SSAO + SSR + TAA)
Velocity:   SRV (read by SSR + TAA)
HDR Color:  SRV -> UAV (read by SSR, written by SSR resolve)

SSAO:
  ao_raw:       UAV (write) -> SRV (read by blur)
  ao_blur_temp: UAV (write) -> SRV (read by blur v)
  ao_final:     UAV (write) -> SRV (read by lighting composite)

SSR:
  hiz_mips:     UAV (write per mip) -> SRV (read by ray march)
  ssr_result:   UAV (write) -> SRV (read by resolve)
  ssr_history:  SRV (read) / UAV (write) -- ping-pong

TAA:
  taa_history:  SRV (read) / UAV (write) -- ping-pong
  taa_output:   UAV (write) -> SRV (read by final output / readback)
```

---

## Phase Scheduling

| Effect | Phase | Rationale |
|--------|-------|-----------|
| **SSAO** | Phase 2 | Highest visual quality-to-cost ratio. Works standalone. Required for convincing ambient lighting. |
| **TAA** | Phase 3 | Requires velocity buffer (G-Buffer update). Also serves as temporal denoiser for SSAO and SSR. |
| **SSR** | Phase 3 | Depends on TAA for temporal stability. Requires Hi-Z infrastructure. Most complex of the three. |

### Phase 2 Implementation (SSAO)
- Implement HBAO compute shader and bilateral blur
- Integrate into deferred lighting ambient term
- Add quality levels and IPC handlers
- No velocity buffer needed yet (SSAO is per-frame)

### Phase 3 Implementation (TAA + SSR)
- Add velocity buffer to G-Buffer (GBuffer3)
- Update per-object CBV with `prev_model`
- Update per-frame CBV with `prev_view_proj` and `jitter_offset`
- Implement TAA (jitter + resolve + history)
- Implement Hi-Z generation and SSR ray march
- Wire SSR temporal accumulation through TAA history
- Add temporal denoising to SSAO via TAA feedback

---

## Debug Visualization

All screen-space effects support debug visualization modes via IPC:

```json
renderer.debugView { "mode": "ssao" }         // show raw AO buffer
renderer.debugView { "mode": "velocity" }      // show velocity vectors (color-coded)
renderer.debugView { "mode": "ssr_confidence" } // show SSR confidence map
renderer.debugView { "mode": "ssr_result" }     // show SSR hits only (no IBL fallback)
renderer.debugView { "mode": "hiz" }            // show Hi-Z mip level
renderer.debugView { "mode": "none" }           // disable debug view
```

---

## References

- HBAO+: [Bavoil & Sainz, "Horizon-Based Ambient Occlusion", NVIDIA 2008]
- Hi-Z SSR: [Uludag, "Hi-Z Screen-Space Cone-Traced Reflections", GPU Pro 5]
- TAA: [Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014 (UE4)]
- Variance Clamping: [Salvi, "An Excursion in Temporal Supersampling", GDC 2016]
- Catmull-Rom Filtering: [Jimenez, "Filmic SMAA", SIGGRAPH 2012]
