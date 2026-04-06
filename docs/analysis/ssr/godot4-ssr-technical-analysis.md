<!-- doc-meta
title: Godot 4 Screen-Space Reflections — Technical Analysis
author: research
date: 2026-04-05
tags: ssr, screen-space, reflections, godot, rendering, reference
-->

# Godot 4 Screen-Space Reflections — Technical Analysis

This document is a complete technical reference of Godot 4's SSR implementation,
sourced directly from the Godot engine repository (branch `master` and `4.2`).
It covers two distinct generations: the **4.2 linear march** and the **master Hi-Z**
implementation (PR #111210, targeting Godot 4.5/4.6).

## Source Files

| File | Purpose |
|------|---------|
| `servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl` | Main SSR compute shader (ray march + output) |
| `servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_filter.glsl` | Roughness blur — two-pass bilateral Gaussian |
| `servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_scale.glsl` | Half-res downscale — pre-pass for half-resolution path |
| `servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_inc.glsl` | Shared include — `reconstructCSPosition`, scene data UBO |
| `servers/rendering/renderer_rd/effects/ss_effects.cpp` | C++ driver — pipeline init, push constant fill, dispatch |
| `scene/resources/environment.cpp` / `.h` | Environment resource — user-facing properties and defaults |
| `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` | Scene fragment shader — SSR composite with indirect specular |

---

## 1. Algorithm

### Generation 1 — Godot 4.2: Perspective-Correct Linear Screen March

The 4.2 shader performs a **perspective-correct linear march in screen space**,
not a Hi-Z traversal. It projects both the ray origin and ray endpoint into NDC,
then marches pixel-by-pixel along the projected line segment.

Key characteristics:
- Ray is constructed as `reflect(view_dir, normal)` in view space.
- Ray endpoint is clipped to the near plane: if the ray would pass behind the camera,
  it is clamped so `ray_end.z = -camera_z_near`.
- The 2D screen-space line is normalized so the step advances at least 1 pixel per
  iteration in the dominant axis (`advance_angle_adj = 1 / max(|dx|, |dy|)`).
- Z and W are interpolated per-step using perspective-correct lerp (homogeneous
  coordinates: `z_begin * w_begin`, `w_begin` carried separately and divided each step).

```glsl
// From screen_space_reflection.glsl (4.2)
float ray_len = (vertex.z + ray_dir.z * params.camera_z_far) > -params.camera_z_near
    ? (-params.camera_z_near - vertex.z) / ray_dir.z
    : params.camera_z_far;
vec3 ray_end = vertex + ray_dir * ray_len;

// Project start/end to NDC, extract pixel positions
vec2 vp_line_begin = view_to_screen(vertex, w_begin);
vec2 vp_line_end   = view_to_screen(ray_end, w_end);

// Perspective-correct Z interpolation:
w_begin = 1.0 / w_begin;
w_end   = 1.0 / w_end;
float z_begin = vertex.z * w_begin;
float z_end   = ray_end.z * w_end;
```

### Generation 2 — Godot master (4.5+): Hi-Z Parametric T Tracing

The master branch replaces linear march with **hierarchical Z-buffer (Hi-Z) traversal**.
The algorithm is based on the technique described at:
`https://hacksoflife.blogspot.com/2020/10/a-tip-for-hiz-ssr-parametric-t-tracing.html`

Key insight: the screen-space ray direction is normalized so `|ray_dir.z| = 1.0`.
This means the parametric T value directly encodes depth, eliminating a divide per step.

```glsl
// From screen_space_reflection.glsl (master)
// Normalize Z to exactly -1 or +1 for parametric T tracing.
vec3 screen_ray_dir = screen_end_pos - screen_pos;
screen_ray_dir /= abs(screen_ray_dir.z);

bool facing_camera = screen_ray_dir.z >= 0.0;
```

The Hi-Z traversal loop:

```glsl
while (cur_level >= 0 && cur_iteration > 0 && t < t_max) {
    vec3 cur_screen_pos = screen_pos + screen_ray_dir * t;

    // Sample the min-depth mip at current level.
    vec2 cell_count = compute_cell_count(cur_level);
    vec2 cell_index = floor(cur_screen_pos.xy * cell_count);
    float cell_depth = texelFetch(source_hiz, ivec2(cell_index), cur_level).x;
    float depth_t = (cell_depth - screen_pos.z) * screen_ray_dir.z; // Z is ±1, no divide

    // Compute the t value at which the ray exits this cell.
    vec2 new_cell_index = cell_index + clamp(cell_step, vec2(0.0), vec2(1.0));
    vec2 new_cell_pos   = (new_cell_index / cell_count) + cell_step * 0.000001;
    vec2 pos_t          = (new_cell_pos - screen_pos.xy) / screen_ray_dir.xy;
    float edge_t        = min(pos_t.x, pos_t.y);

    // Hit detection differs for rays facing vs away from camera.
    bool hit = facing_camera ? (t <= depth_t) : (depth_t <= edge_t);
    int mip_offset = hit ? -1 : +1;  // step down on hit, up on miss

    // Level 0: apply depth tolerance as a confidence test.
    if (cur_level == 0) {
        float z0 = linearize_depth(cell_depth);
        float z1 = linearize_depth(cur_screen_pos.z);
        if ((z0 - z1) > params.depth_tolerance) {
            hit = false;
            mip_offset = 0; // prevent oscillation
        }
    }

    if (hit) {
        if (!facing_camera) { t = max(t, depth_t); }
    } else {
        t = edge_t;
    }

    cur_level = min(cur_level + mip_offset, params.mipmaps - 1);
    --cur_iteration;
}
```

This allows rays to skip large empty regions in a single step when at high mip levels,
making 64 steps cover the same effective range as ~512 steps in the linear path.

---

## 2. Compute Shader Setup

Both generations use compute shaders dispatched at `local_size_x = 8, local_size_y = 8`.

### 4.2 Input/Output Bindings

| Binding | Format | Role |
|---------|--------|------|
| `source_diffuse` (set=0, b=0) | rgba16f | Source color for final sample |
| `source_depth` (set=0, b=1) | r32f | Scene depth |
| `ssr_image` (set=1, b=0) | rgba16f | Output reflections (rgb=color, a=fade) |
| `blur_radius_image` (set=1, b=1) | r8 | Roughness blur radius (MODE_ROUGH only) |
| `source_normal_roughness` (set=2, b=0) | rgba8 | GBuffer normals (xyz) + roughness (w) |
| `source_metallic` (set=3, b=0) | sampler2D | Metallic channel for Fresnel |

### Master Input/Output Bindings

| Binding | Format | Role |
|---------|--------|------|
| `source_last_frame` (set=0, b=0) | sampler2D | Previous frame color (reprojected) |
| `source_hiz` (set=0, b=1) | sampler2D | Hi-Z depth pyramid (mip chain) |
| `source_normal_roughness` (set=0, b=2) | sampler2D | GBuffer normals + roughness |
| `output_color` (set=0, b=3) | rgba16f write | SSR color output |
| `output_mip_level` (set=0, b=4) | r8 write | Roughness blur mip level |

The master version adds a `reprojection` matrix to the scene data UBO for reprojecting
the hit position to the previous frame's UV.

---

## 3. Push Constants and Parameters

### 4.2 Push Constant Layout

```glsl
layout(push_constant, std430) uniform Params {
    vec4  proj_info;           // projection reconstruction params
    ivec2 screen_size;
    float camera_z_near;
    float camera_z_far;
    int   num_steps;           // default: 64 (range: 32–512 in editor)
    float depth_tolerance;     // default: 0.2 (early), 0.5 (later)
    float distance_fade;       // default: 2.0 (maps to ssr_fade_out)
    float curve_fade_in;       // default: 0.15 (maps to ssr_fade_in)
    bool  orthogonal;
    float filter_mipmap_levels;
    bool  use_half_res;
    uint  view_index;
} params;
```

### Master Push Constant Layout

```glsl
layout(push_constant, std430) uniform Params {
    ivec2 screen_size;
    int   mipmaps;             // Hi-Z mip levels available
    int   num_steps;           // default: 64
    float distance_fade;       // ssr_fade_out
    float curve_fade_in;       // ssr_fade_in
    float depth_tolerance;     // default: 0.5
    bool  orthogonal;
    int   view_index;
    int   pad1, pad2, pad3;
} params;
```

### Environment Resource Defaults (scene/resources/environment.h, master)

```cpp
bool  ssr_enabled        = false;
int   ssr_max_steps      = 64;     // HINT_RANGE 32..512
float ssr_fade_in        = 0.15;   // EXP_EASING positive_only
float ssr_fade_out       = 2.0;    // EXP_EASING positive_only
float ssr_depth_tolerance = 0.5;   // HINT_RANGE 0.01..128
```

---

## 4. Roughness Handling

### 4.2: Roughness Cutoff + Blur Radius Pass

- Hard cutoff: roughness > 0.6 → early return, no SSR written.
- A separate `MODE_ROUGH` compile-time define enables writing a blur radius to `blur_radius_image`.
- Blur radius uses a cone geometry approximation:

```glsl
// From screen_space_reflection.glsl (4.2)
float cone_angle = min(roughness, 0.999) * M_PI * 0.5;
float cone_len   = length(final_pos - line_begin);  // screen-space distance
float op_len     = 2.0 * tan(cone_angle) * cone_len; // isoceles triangle base

// Fit sphere inside cone, sphere radius = blur_radius:
float a  = op_len;
float h  = cone_len;
float a2 = a * a;
float fh2 = 4.0 * h * h;
blur_radius = (a * (sqrt(a2 + fh2) - a)) / (4.0 * h);

imageStore(blur_radius_image, ssC, vec4(blur_radius / 255.0));
```

The blur radius (r8, 0..255 pixels) is consumed by the bilateral filter pass.

- Additionally at composite time, roughness fade uses:

```glsl
float roughness_fade = smoothstep(0.4, 0.7, 1.0 - normal_roughness.w);
```

### Master: Roughness Cutoff + Mip Bias

The master version eliminates the separate blur radius image. Instead:

1. Hard cutoff: roughness >= 0.7 → early return.
2. Roughness value is decoded from the packed normal_roughness buffer with a
   piecewise encoding: values above 0.5 are remapped as `roughness = 1.0 - roughness`,
   then divided by `127.0 / 255.0` to recover the original scale.
3. A mip level is computed for the hit point and written to `output_mip_level`:

```glsl
// From screen_space_reflection.glsl (master)
float cone_angle = min(roughness, 0.999) * M_PI * 0.5;
float cone_len   = ray_len; // parametric T in screen space
float op_len     = 2.0 * tan(cone_angle) * cone_len;

float a   = op_len;
float h   = cone_len;
float a2  = a * a;
float fh2 = 4.0 * h * h;
float blur_radius = (a * (sqrt(a2 + fh2) - a)) / (4.0 * h);

// Convert screen-space blur radius to mip level:
// log2(blur_radius * max_screen_dim / 16.0 + 1.0)
// +1 required: mip level is log2 of sampling region diameter,
// which is 1 pixel at mip 0 (log2(1) = 0).
mip_level = clamp(
    log2(blur_radius * max(params.screen_size.x, params.screen_size.y) / 16.0 + 1.0),
    0, params.mipmaps - 1);

// Fade mip level near end of ray to reduce pyramid artifact at rough cutoff:
mip_level *= pow(clamp(1.25 - ray_len, 0.0, 1.0), 0.2);
```

The mip level is sampled using the Gaussian-blurred mip chain of the SSR output buffer
when it is read back in the scene shader.

---

## 5. Noise and Temporal Jitter

### 4.2

**No noise or temporal jitter.** The 4.2 shader performs a deterministic, per-pixel
linear march. There is no blue noise texture, no Halton sequence, and no temporal
accumulation. The aliasing that results (jagged edges on reflections) is a known
limitation. The only anti-aliasing benefit comes from TAA applied globally to the
scene *after* SSR is composited, not from within the SSR pass itself.

Issue #80892 confirms: "MSAA isn't usable on SSR by design, as there are no polygon
edges to smooth out." TAA is the recommended workaround.

### Master (4.5+)

**No explicit per-pixel noise injection.** The Hi-Z implementation also does not add
noise or jitter at ray origin. The temporal component comes from reading `source_last_frame`
(the previous frame's color buffer) and using the reprojection matrix to remap hit
positions. This is a form of **temporal reuse** but not a full TAA accumulation loop.

The PR #111210 explicitly defers motion vector support (artifacts on moving objects)
and full TAA integration to follow-up work.

---

## 6. Denoising and Spatial Filtering

### 4.2: Two-Pass Bilateral Gaussian Filter

A separate `screen_space_reflection_filter.glsl` compute shader runs two passes
(horizontal then vertical) — a separable bilateral Gaussian blur.

The filter is **edge-aware**: it breaks sampling when the angular difference between
the center normal and the neighbor normal, or between the depth-derived positions,
exceeds `params.edge_tolerance`.

The blur radius is driven by the per-pixel `blur_radius_image` written by `MODE_ROUGH`.
Only samples within `d < radius` pixels contribute to the filter.

**Gaussian kernel** (GAUSS_TABLE_SIZE = 15, 16 taps):

```glsl
const float gauss_table[16] = float[](
    0.1847392078702266,   // tap 0 (center weight, used as divisor seed)
    0.16595854345772326,
    0.12031364177766891,
    0.07038755277896766,
    0.03322925565155569,
    0.012657819729901945,
    0.0038903040680094217,
    0.0009646503390864025,
    0.00019297087402915717,
    0.000031139936308099136,
    0.000004053309048174758,
    4.255228059965837e-7,
    3.602517634249573e-8,
    2.4592560765896795e-9,
    1.3534945386863618e-10,
    0.0                   // sentinel
);
```

Weight lookup uses linear interpolation between table entries:
`gauss_weight(p_val) = mix(table[idx], table[idx+1], fract_part)`

The filter runs two dispatch passes. After the horizontal pass, the output `dest_radius`
is the weighted-average blur radius (used by vertical pass). After the vertical pass,
only the color result is needed and radius output is skipped (`#ifndef VERTICAL_PASS`).

### Master: Mip Chain Blur

The master implementation eliminates the separate filter passes. Instead:

1. The SSR output buffer is placed into a **Gaussian-blurred mip chain** (generated
   as part of the rendering pipeline, not a dedicated SSR filter shader).
2. A tone-mapping step is applied to SSR color **before** blur to handle high-variance
   luminance transitions:

```glsl
// In the trace shader (master), before writing output:
const vec3 rec709_luminance_weights = vec3(0.2126, 0.7152, 0.0722);
color.rgb /= 1.0 + dot(color.rgb, rec709_luminance_weights);
```

3. In the scene shader composite, the inverse tone-map is applied after reading the mip:

```glsl
// In scene_forward_clustered.glsl (master):
const vec3 rec709_luminance_weights = vec3(0.2126, 0.7152, 0.0722);
ssr.rgb /= 1.0 - dot(ssr.rgb, rec709_luminance_weights);
```

The mip level to sample is read from `ssr_mip_level_buffer`, multiplied by 14.0
(the stored value is normalized to [0,1] with max 14 mips).

---

## 7. Hit Validation

### 4.2 Depth Test

```glsl
// Ray hit condition: sampled depth is behind the ray's current z.
if (depth > z_to) {
    // Check normal faces toward ray (prevent back-face hits).
    if (dot(ray_dir, test_normal) < 0.001) {
        // Depth tolerance: ensure hit is within a slab.
        if (depth <= max(z_to, z_from) + params.depth_tolerance
            && -depth < params.camera_z_far * 0.95) {
            found = true;
        }
        break;
    }
}
```

Additional validation: the starting pixel is skipped if the next step would land on
the same pixel as the origin (prevents immediate self-intersection).

### Master Hit Validation

The master shader references the FidelityFX-SSSR (NVIDIA/AMD) validation logic:
`https://github.com/GPUOpen-Effects/FidelityFX-SSSR/blob/master/ffx-sssr/ffx_sssr.h`

Three conditions set `validity = 0.0` (soft reject via alpha, not hard discard):

1. **Out of bounds**: `t >= t_max` or `hit_depth == 0.0` (sky pixel).
2. **Back-face hit** (only when the hit is very close — within 2 pixels of origin):
   `dot(ray_dir, hit_normal) >= 0.0` rejects back-facing surfaces.
3. **Depth mismatch confidence**:

```glsl
vec3 cur_pos = compute_view_pos(cur_screen_pos);
vec3 hit_pos = compute_view_pos(vec3(cur_screen_pos.xy, hit_depth));
float delta = length(cur_pos - hit_pos);
float confidence = 1.0 - smoothstep(0.0, params.depth_tolerance, delta);
validity *= clamp(confidence * confidence, 0.0, 1.0);
```

Soft rejection (validity in alpha) allows invalid pixels to still contribute
blur mip levels so that valid neighboring pixels have smoother roughness transitions.

---

## 8. Screen-Edge Fade

Both generations use the same margin strategy: a **product-of-distances** fade in
pixel space. The margin is 5% of the sum of screen dimensions:

```glsl
vec2 margin = vec2((params.screen_size.x + params.screen_size.y) * 0.05);

// Distance to each edge, taking the smaller of (distance-from-left, distance-from-right):
vec2 margin_grad = mix(
    params.screen_size - pixel_pos,
    pixel_pos,
    lessThan(pixel_pos, params.screen_size * 0.5));

float margin_blend = smoothstep(0.0, margin.x * margin.y, margin_grad.x * margin_grad.y);
```

This creates a smooth fade that is strongest near all four edges and corners.
The fade value multiplies the final alpha output.

---

## 9. Distance Fade (Fade In / Fade Out)

Both generations use the same pair of power-law fades applied to the normalized
ray travel distance `grad` (0 = start of ray, 1 = full step budget exhausted):

```glsl
// 4.2: grad = steps_taken / num_steps
// Master: grad = ray_len (parametric T in screen space)

float fade_in  = (params.curve_fade_in == 0.0)  ? 1.0 : pow(clamp(grad, 0.0, 1.0), params.curve_fade_in);
float fade_out = (params.distance_fade == 0.0)   ? 1.0 : pow(clamp(1.0 - grad, 0.0, 1.0), params.distance_fade);
float fade     = fade_in * fade_out;

// Clamp precision error: if fade > 0.999, snap to 1.0
// Prevents specular light leaking through near-perfect reflections.
if (fade > 0.999) { fade = 1.0; }
```

Default values: `fade_in = 0.15` (slow in), `fade_out = 2.0` (fast out).

---

## 10. Fresnel (4.2 only)

The 4.2 shader computes a Schlick Fresnel term per-hit and bakes it into the output
RGB before writing, using metallic from the GBuffer (sampled at half resolution):

```glsl
// source_metallic sampled at ssC << 1 (half-res coordinate)
float metallic = texelFetch(source_metallic, ssC << 1, 0).w;
float f0 = mix(0.04, 1.0, metallic);     // dielectric F0 = 0.04
float m  = clamp(1.0 - dot(normal, -view_dir), 0.0, 1.0);
float m2 = m * m;
m = m2 * m2 * m;                         // m^5
float fresnel_term = f0 + (1.0 - f0) * m; // Schlick approximation

// Applied to color:
vec4 final_color = vec4(color.rgb * fresnel_term, fade * margin_blend * roughness_fade);
```

The master version does **not** apply Fresnel in the SSR shader. Fresnel is handled
entirely in the deferred lighting / scene fragment shader as part of the PBR BRDF.

---

## 11. SSR Composite with Environment Reflections

### 4.2: Alpha-Over Blend

The SSR result (rgba16f, alpha = validity * fade * margin * roughness_fade) is
read back in the scene fragment shader and composited over the existing
`indirect_specular_light` using a standard alpha-over blend:

```glsl
// From scene_forward_clustered.glsl (master, same logic in 4.2):

float ssr_mip_level = 0.0;
if (resolve_ssr) {
    ssr_mip_level = textureLod(ssr_mip_level_buffer, screen_uv, 0).x * 14.0;
}
vec4 ssr = textureLod(ssr_buffer, screen_uv, ssr_mip_level);

if (resolve_ssr) {
    // Inverse of the Rec.709 tone map applied in the SSR trace shader:
    const vec3 rec709_luminance_weights = vec3(0.2126, 0.7152, 0.0722);
    ssr.rgb /= 1.0 - dot(ssr.rgb, rec709_luminance_weights);
}

// Roughness fade: smooth transition to avoid hard cutoff at roughness=0.7
ssr *= smoothstep(0.0, 1.0, 1.0 - clamp((roughness - 0.6) / (0.7 - 0.6), 0.0, 1.0));

// Alpha is premultiplied: SSR blends over environment/ReflectionProbe specular.
indirect_specular_light = indirect_specular_light * (1.0 - ssr.a) + ssr.rgb;
```

**SSR occludes** environment reflections wherever it has valid data (high alpha).
Where SSR has no coverage (alpha = 0), the environment probe / sky reflection shows through.

The `resolve_ssr` flag controls whether the mip-level-based blur is applied.
In half-resolution mode, the SSR buffer is upsampled using a surface-aware bilinear
filter before this blend step.

---

## 12. Half-Resolution Path

Both generations support running SSR at half resolution. The `screen_space_reflection_scale.glsl`
shader downscales color, depth, and normals to half resolution before the trace pass.

Downscale modes:
- **Filtered** (`params.filtered = true`): 2x2 box filter average for color, averaged normals,
  averaged linearized depth.
- **Unfiltered**: point sample the top-left of each 2x2 block.

The depth in the scale pass is linearized (perspective or orthographic), not raw NDC,
because the SSR trace uses view-space Z for intersection testing.

---

## 13. Quality Settings (User-Facing)

| Parameter | Property | Default | Range | Description |
|-----------|----------|---------|-------|-------------|
| Enabled | `ssr_enabled` | `false` | bool | Master switch |
| Steps | `ssr_max_steps` | `64` | 32–512 | Ray march budget |
| Fade In | `ssr_fade_in` | `0.15` | ≥0 (exp easing) | Power curve for near-origin fade-in |
| Fade Out | `ssr_fade_out` | `2.0` | ≥0 (exp easing) | Power curve for far-end fade-out |
| Depth Tolerance | `ssr_depth_tolerance` | `0.5` | 0.01–128 | Slab thickness for hit acceptance |

Project setting (4.2): `rendering/environment/screen_space_reflection/roughness_quality`
controls the blur filter quality (disabled in the master branch — mip chain replaces it).

---

## 14. Geometric Normal Reconstruction (Master Only)

The master shader reconstructs a **geometric normal from depth** to use as a bias
and self-intersection guard. This avoids mismatches between the shading normal (which
may be perturbed by normal maps) and the actual surface geometry. The algorithm
samples depth at ±1 and ±2 pixels in both axes and selects the best-matching neighbor:

```glsl
// From screen_space_reflection.glsl (master)
// Reference: https://habr.com/ru/articles/744336/
vec3 compute_geometric_normal(ivec2 pixel_pos, float depth_c, vec3 view_c, float pixel_offset) {
    vec4 H = vec4(
        texelFetch(source_hiz, pixel_pos + ivec2(-1, 0), 0).x,
        texelFetch(source_hiz, pixel_pos + ivec2(-2, 0), 0).x,
        texelFetch(source_hiz, pixel_pos + ivec2( 1, 0), 0).x,
        texelFetch(source_hiz, pixel_pos + ivec2( 2, 0), 0).x);
    vec4 V = vec4(...); // same pattern vertically

    // Select the neighbor with smaller depth discontinuity:
    vec2 he = abs((2.0 * H.xz - H.yw) - depth_c);
    vec2 ve = abs((2.0 * V.xz - V.yw) - depth_c);

    int h_sign = (he.x < he.y) ? -1 : 1;
    int v_sign = (ve.x < ve.y) ? -1 : 1;

    // Cross product of view-space tangent vectors:
    vec3 h_der = h_sign * (view_h - view_c);
    vec3 v_der = v_sign * (view_v - view_c);
    return cross(v_der, h_der); // unnormalized
}
```

The geometric normal is used to:
1. Add a positional bias: `pos += geom_normal * (1.0 - pow(dot(normal, geom_normal), 8.0))`
   — pushes the origin off the surface proportionally to normal map deviation.
2. Detect self-intersection: if `dot(ray_dir, geom_normal) < 0.0`, the reflected ray
   is itself reflected off the geometric normal: `ray_dir = normalize(reflect(ray_dir, geom_normal))`.

---

## 15. Known Limitations

### Both Generations

- **Transparent geometry not reflected.** SSR reads the depth buffer; transparent
  objects do not write depth, so they are invisible to SSR. This is a fundamental
  screen-space limitation (GitHub issue #79549, proposals #7274).
- **Off-screen geometry gaps.** Any reflected surface outside the view frustum
  has no data; SSR silently returns zero and falls through to environment probes.
- **No noise/jitter = aliased edges.** Without per-pixel noise injection, reflection
  edges are perfectly sharp and alias at object boundaries. The recommended mitigation
  is TAA (GitHub issue #80892).

### 4.2-Specific

- **Horizontal blur streaks.** With `MODE_ROUGH`, the bilateral filter is separable
  (H then V passes). Under certain normal map + roughness combinations this produces
  directional streaks rather than isotropic blur (GitHub issue #67245). Fixed in a
  later PR (72838, render buffer refactor).
- **Roughness semi-transparency bug.** The alpha channel baking (Fresnel * fade)
  produces reflections that appear semi-transparent on dark objects (GitHub issue #79549).
- **Roughness quality is expensive.** The bilateral filter adds two additional
  compute dispatches. Project setting `roughness_quality` can be set to OFF/LOW/MEDIUM/HIGH.

### Master-Specific

- **Moving object artifacts.** The use of `source_last_frame` for reflected color
  means moving objects in the reflection have ghosting until motion vector integration
  is added (deferred in PR #111210).
- **No sky in SSR.** Sky pixels have `depth == 0.0`, which causes immediate validity
  rejection. Sky reflections fall through to the environment background. The PR
  explicitly notes sky reflection support as future work.
- **Pyramid artifact at rough cutoff.** At roughness values near the 0.7 cutoff,
  the mip level fade (`mip_level *= pow(clamp(1.25 - ray_len, 0.0, 1.0), 0.2)`)
  is a partial mitigation, but a visible pyramid-like shape can still appear at
  very rough surfaces near screen edges.

---

## 16. Performance

RTX 3070 Ti benchmarks from PR #111210 at 1080p:

| Mode | FPS (master Hi-Z) | FPS (4.2 linear) |
|------|------------------|-----------------|
| Half-res, 64 steps | 323 | 268 |
| Full-res, 64 steps | 233 | N/A (not available in 4.2) |

The Hi-Z algorithm achieves significantly better coverage per step due to hierarchical
skipping, making 64 steps qualitatively comparable to ~512 steps in the linear path.

---

## 17. Algorithm Flow Summary

### 4.2 Flow

```
GBuffer (depth, normal_roughness, metallic)
  │
  ▼
[scale.glsl] — Optional half-res downscale (2x2 box or point)
  │
  ▼
[screen_space_reflection.glsl, MODE_ROUGH=0/1]
  — perspective-correct linear screen march
  — Schlick Fresnel baked into output RGB
  — blur_radius written per-pixel (MODE_ROUGH)
  → ssr_image (rgba16f)
  → blur_radius_image (r8, MODE_ROUGH only)
  │
  ▼
[filter.glsl — horizontal pass]
  — bilateral Gaussian, blur radius driven by blur_radius_image
  → ssr_filtered_h, blur_radius_h
  │
  ▼
[filter.glsl — vertical pass]
  — bilateral Gaussian
  → ssr_filtered (rgba16f, final)
  │
  ▼
[scene_forward_clustered.glsl]
  — ssr_filtered.a = premultiplied alpha
  — indirect_specular = indirect_specular * (1 - ssr.a) + ssr.rgb
```

### Master Flow

```
GBuffer (normal_roughness)
Hi-Z depth pyramid (pre-computed mip chain of depth)
Previous frame color buffer
  │
  ▼
[screen_space_reflection.glsl]
  — Hi-Z parametric T tracing
  — reprojection to previous frame UV
  — tone-map applied to output (Reinhard-like, Rec.709 weights)
  — mip level computed from cone angle
  → output_color (rgba16f, tone-mapped, validity in alpha)
  → output_mip_level (r8, normalized mip 0..14)
  │
  ▼
[Gaussian-blurred mip chain generation]  ← "at no extra cost" (shared with other effects)
  │
  ▼
[scene_forward_clustered.glsl]
  — read mip_level, sample output_color at computed mip
  — inverse tone-map (undo Reinhard)
  — roughness smoothstep fade (0.6–0.7)
  — indirect_specular = indirect_specular * (1 - ssr.a) + ssr.rgb
```

---

## References

- [Godot SSR Shader (4.2)](https://github.com/godotengine/godot/blob/4.2/servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl)
- [Godot SSR Shader (master)](https://github.com/godotengine/godot/blob/master/servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl)
- [Godot SSR Filter Shader (4.2)](https://github.com/godotengine/godot/blob/4.2/servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_filter.glsl)
- [Godot SSR Scale Shader (4.2)](https://github.com/godotengine/godot/blob/4.2/servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_scale.glsl)
- [PR #111210 — Overhaul screen-space reflections](https://github.com/godotengine/godot/pull/111210)
- [Issue #80892 — SSR not anti-aliased](https://github.com/godotengine/godot/issues/80892)
- [Issue #70526 — SSR roughness quality + TAA](https://github.com/godotengine/godot/issues/70526)
- [Issue #67245 — SSR roughness horizontal streaks](https://github.com/godotengine/godot/issues/67245)
- [Issue #79549 — SSR semi-transparency](https://github.com/godotengine/godot/issues/79549)
- [hacksoflife — Hi-Z SSR Parametric T Tracing](http://hacksoflife.blogspot.com/2020/10/a-tip-for-hiz-ssr-parametric-t-tracing.html)
- [Habr — Geometric Normal from Depth](https://habr.com/ru/articles/744336/)
- [FidelityFX-SSSR Hit Validation Reference](https://github.com/GPUOpen-Effects/FidelityFX-SSSR/blob/master/ffx-sssr/ffx_sssr.h)
