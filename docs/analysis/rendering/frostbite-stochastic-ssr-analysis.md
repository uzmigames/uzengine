<!-- doc-meta
title: Frostbite Stochastic Screen-Space Reflections — Complete Technical Analysis
author: research
date: 2026-04-05
tags: ssr, reflections, stochastic, frostbite, dice, hi-z, ggx, denoiser
-->

# Frostbite Stochastic Screen-Space Reflections — Complete Technical Analysis

## Reference Sources

| Source | Authors | Year | Type |
|--------|---------|------|------|
| "Stochastic Screen-Space Reflections" | Tomasz Stachowiak (Frostbite/EA) | SIGGRAPH 2015 | Advances in Real-Time Rendering course |
| "Hi-Z Screen-Space Cone-Traced Reflections" | Yasin Uludag (Frostbite/EA) | GPU Pro 5, 2014 | Book chapter |
| "Efficient GPU Screen-Space Ray Tracing" | Morgan McGuire, Mike Mara | JCGT Vol. 3 No. 4, 2014 | Journal paper |
| "Sampling the GGX Distribution of Visible Normals" | Eric Heitz | JCGT Vol. 7 No. 4, 2018 | Journal paper |
| AMD FidelityFX SSSR source code | AMD GPUOpen | 2021 | Open source (MIT) |

The FidelityFX SSSR is the canonical open-source implementation of Stachowiak's technique,
co-credited to both Stachowiak and Uludag. All shader code in this document is from that
open-source reference (MIT licensed), available at:
https://github.com/GPUOpen-Effects/FidelityFX-SSSR

---

## 1. Motivation and Requirements

Classic mirror SSR computes a single reflection ray per pixel by reflecting the view direction
about the surface normal. This only produces sharp mirror reflections. Stachowiak 2015 set
four hard requirements:

1. **Sharp AND blurry reflections** — metallic mirrors must be sharp; rough diffuse-ish surfaces blur
2. **Contact hardening** — reflections of nearby geometry (contact) are sharper than distant geometry
3. **Specular elongation** — anisotropic BRDFs produce streaked highlights correctly
4. **Per-pixel roughness and normal** — spatial variation from normal maps must be respected

Meeting all four requires tracing rays drawn from the GGX NDF/VNDF — one stochastic sample
per pixel per frame, then reconstructing a low-noise image via spatial + temporal filtering.

---

## 2. Algorithm Pipeline (7 Passes)

```
[1] Hi-Z Depth Pyramid Construction  — 7-mip pyramid from scene depth (min reduction)
[2] Tile Classification              — per-8x8 tile: which pixels need a ray? env-map fallback?
[3] Blue Noise Texture Generation    — 128x128 animated stochastic sampling pattern
[4] Indirect Argument Preparation    — fill DispatchIndirect args based on ray count
[5] Intersection Pass                — GGX VNDF ray generation + Hi-Z traversal + env-map blend
[6] Reproject Pass (Denoiser)        — history buffer build, temporal reprojection
[7] Prefilter + Temporal Resolve     — spatial bilateral filter, then temporal accumulation
```

All passes are compute shaders (8x8 threadgroups). The pipeline is tile-based: only tiles
with glossy surfaces (roughness < `roughnessThreshold`) execute the full pipeline.

---

## 3. Hi-Z Depth Pyramid Construction

### Purpose
A min-depth mip pyramid allows the ray to skip over empty regions. At mip N, each texel
stores the MINIMUM depth across the 2^N × 2^N texels of the full-resolution depth buffer.
Skipping a mip-N tile means skipping 2^N × 2^N depth samples at once.

### Construction
AMD uses FidelityFX SPD (Single Pass Downsampler) to build 7 mip levels from scene depth.
Each mip reduces by min (not average) so the Hi-Z bound is always conservative.

```
mip 0: full resolution depth
mip 1: 1/2  resolution, min of 2x2
mip 2: 1/4  resolution, min of 4x4
...
mip 6: 1/64 resolution, min of 64x64
```

### Depth Convention
The algorithm supports both standard depth (near=0, far=1) and reversed-Z (near=1, far=0)
via compile-time define `FFX_SSSR_INVERTED_DEPTH_RANGE`.

---

## 4. Tile Classification and Ray Allocation

This is the core of Stachowiak's "stochastic" approach. Rather than tracing one ray per
pixel (expensive) or one ray per NxN tile (aliased), it traces **1 ray per quad (2x2 pixels)**
and then copies the result to the other 3 pixels in the quad.

### Samples Per Quad Options

```hlsl
// From ClassifyTiles.hlsl
bool IsBaseRay(uint2 dispatch_thread_id, uint samples_per_quad) {
    switch (samples_per_quad) {
    case 1:
        // 1 ray per 2x2 quad: only pixel at (even,even) traces a ray
        return ((dispatch_thread_id.x & 1) | (dispatch_thread_id.y & 1)) == 0;
    case 2:
        // 2 rays per 2x2 quad: diagonal pixels trace rays
        return (dispatch_thread_id.x & 1) == (dispatch_thread_id.y & 1);
    default: // case 4:
        // Full rate: every pixel traces a ray
        return true;
    }
}
```

With `samples_per_quad = 1`, 75% of pixels are "copies" — the base ray pixel's result is
written to 3 neighbors using wave-intrinsic quad operations. Each copy pixel has its own
independent GGX sample (different blue noise offset) so the 4 pixels form a 2x2 Monte Carlo
pattern over the specular lobe.

### Copy Direction Encoding

The base ray encodes which of its 3 neighbors need copies in a packed 32-bit ray_list entry:

```hlsl
// Flip last bit to find the mirrored coords within a 2x2 quad
uint2 copy_target = coords ^ 0b1; // XOR with 1 flips bit 0 in both x and y
if (copy_horizontal) g_intersection_output[uint2(copy_target.x, coords.y)] = new_sample;
if (copy_vertical)   g_intersection_output[uint2(coords.x, copy_target.y)] = new_sample;
if (copy_diagonal)   g_intersection_output[copy_target]                    = new_sample;
```

### Temporal Variance Guided Tracing

If `g_temporal_variance_guided_tracing_enabled`, non-base-ray pixels that had HIGH temporal
variance in the previous frame are promoted to fire their own ray. This adaptively allocates
extra samples where the signal is unstable.

### Mirror Surface Fast Path

Mirror reflections (roughness < 0.0001) always use full-rate tracing (every pixel fires a ray)
and skip the denoiser — the signal is deterministic so no noise to filter.

### Roughness Threshold

Surfaces with roughness >= `g_roughness_threshold` are skipped entirely for ray tracing. They
sample the pre-blurred environment cubemap at `mip = roughness * (mip_count - 1)` directly.

---

## 5. Blue Noise Texture Generation

### Technique
Per-frame, a 128x128 R8G8 texture is filled with a correlated blue noise pattern. The blue
noise ensures that the stochastic samples have low discrepancy spatially, which is critical
for the bilateral filter to work well (neighboring pixels have uncorrelated but
well-distributed samples).

### Algorithm: Owen-Scrambled Sobol Sequence with Optimized Ranking

```hlsl
#define GOLDEN_RATIO 1.61803398875f

float SampleRandomNumber(uint pixel_i, uint pixel_j, uint sample_index, uint sample_dimension) {
    // Wrap to 128x128 tile, 256 samples, 256 dimensions
    pixel_i      = pixel_i      & 127u;
    pixel_j      = pixel_j      & 127u;
    sample_index = sample_index & 255u;
    sample_dimension = sample_dimension & 255u;

    // Optimized ranking XOR (Eric Heitz, Laurent Belcour 2019)
    const uint ranked_sample_index = sample_index ^
        g_ranking_tile_buffer[sample_dimension + (pixel_i + pixel_j * 128u) * 8u];

    // Sobol sequence value
    uint value = g_sobol_buffer[sample_dimension + ranked_sample_index * 256u];

    // Optimized scrambling XOR
    value = value ^ g_scrambling_tile_buffer[
        (sample_dimension % 8u) + (pixel_i + pixel_j * 128u) * 8u];

    return (value + 0.5f) / 256.0f;
}

float2 SampleRandomVector2D(uint2 pixel) {
    // Golden ratio animation: shift blue noise per frame by irrational number
    // This decorrelates temporal samples while preserving spatial blue noise structure
    return float2(
        fmod(SampleRandomNumber(pixel.x, pixel.y, 0, 0u) + (g_frame_index & 0xFFu) * GOLDEN_RATIO, 1.0f),
        fmod(SampleRandomNumber(pixel.x, pixel.y, 0, 1u) + (g_frame_index & 0xFFu) * GOLDEN_RATIO, 1.0f)
    );
}
```

Key: the frame index is multiplied by the golden ratio (1.618...) and wrapped via fmod.
This is "golden ratio modular arithmetic" — consecutive frames produce a low-discrepancy
1D sequence in the [0,1] range, ensuring full lobe coverage across ~256 frames before
repeating. The blue noise spatial distribution means each frame's pattern is visually smooth.

---

## 6. GGX VNDF Importance Sampling

### Theory

Stachowiak uses GGX VNDF (Visible Normal Distribution Function) sampling, introduced in
Heitz 2018. The original NDF sampling (sample theta/phi from D(h)) produces samples in the
entire upper hemisphere, many of which contribute zero energy (shadowed by the microsurface).
VNDF sampling instead draws normals from D_Ve(h) = G1(Ve) * max(0, dot(Ve, h)) * D(h) / Ve.z,
which concentrates samples in the visible lobe — dramatically lower variance.

### Implementation (from Intersect.hlsl)

```hlsl
/// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
/// @constants none — pure math, no empirical constants
float3 SampleGGXVNDF(float3 Ve, float alpha_x, float alpha_y, float U1, float U2) {
    // Section 3.2: transform view direction to hemisphere configuration
    float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));

    // Section 4.1: orthonormal basis (degenerate case: Vh parallel to Z)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);

    // Section 4.2: sample projected area using disk-to-hemisphere mapping
    float r   = sqrt(U1);
    float phi = 2.0 * M_PI * U2;
    float t1  = r * cos(phi);
    float t2  = r * sin(phi);
    // Blend between hemisphere top and full disk
    float s   = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    // Section 4.3: reproject onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    // Section 3.4: transform normal back to ellipsoid (anisotropic scaling)
    float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
    return Ne;
}
```

The function handles anisotropic roughness (alpha_x != alpha_y) natively. For isotropic
surfaces, call with `alpha_x = alpha_y = roughness`.

### Ray Direction from Sampled Normal

```hlsl
float3 SampleReflectionVector(float3 view_direction, float3 normal, float roughness, int2 coords) {
    // Build TBN from surface normal
    float3x3 tbn = CreateTBN(normal);

    // Transform view into tangent space for VNDF sampling
    float3 view_tbn = mul(-view_direction, tbn);

    // Sample VNDF (isotropic: alpha = roughness)
    float2 u = SampleRandomVector2D(coords);  // blue noise [0,1)x[0,1)
    float3 sampled_normal_tbn = Sample_GGX_VNDF_Hemisphere(view_tbn, roughness, u.x, u.y);

    // Reflect view around sampled microfacet normal -> reflection direction
    float3 reflected_tbn = reflect(-view_tbn, sampled_normal_tbn);

    // Transform back to world/view space
    float3x3 inv_tbn = transpose(tbn);
    return mul(reflected_tbn, inv_tbn);
}
```

Note: for perfect mirrors, `sampled_normal_tbn` is forced to `float3(0,0,1)` (macro normal),
producing a deterministic reflection.

### Why VNDF over NDF

| Technique | Variance | Notes |
|-----------|----------|-------|
| Uniform hemisphere sampling | Very high | Naive |
| NDF sampling (classic GGX IS) | Moderate | theta = acos(sqrt((1-xi) / ((a^2-1)*xi + 1))) |
| VNDF sampling (Heitz 2018) | Low | Samples only the visible lobe |
| Bounded VNDF (Dupuy 2023) | Lowest | Further improvement for grazing angles |

For SSR specifically, VNDF dramatically helps for grazing-angle views where classic GGX NDF
would produce many samples pointing behind the surface.

---

## 7. Hi-Z Hierarchical Ray Traversal

### Overview

The ray is parameterized in screen UV + depth space: `position = origin + t * direction`
where all three components are in [0,1] screen space. Traversal marches this ray through
the Hi-Z mip pyramid. At each step:

1. Sample depth at `current_mip` — this is the minimum depth in a 2^mip x 2^mip footprint
2. Compare ray depth against sampled depth
3. If ray is above surface (`skipped_tile = true`): advance to next XY cell boundary, **increase mip** (coarser)
4. If ray is at or below surface: **decrease mip** (finer) — we may have found a hit
5. Stop when `current_mip < most_detailed_mip` (went below finest mip — confirmed hit candidate)

### Initial Advance

```hlsl
void FFX_SSSR_InitialAdvanceRay(
    float3 origin, float3 direction, float3 inv_direction,
    float2 current_mip_resolution, float2 current_mip_resolution_inv,
    float2 floor_offset, float2 uv_offset,
    out float3 position, out float current_t)
{
    float2 current_mip_position = current_mip_resolution * origin.xy;

    // Intersect with cell boundary that the ray is pointing away from
    float2 xy_plane = floor(current_mip_position) + floor_offset;
    xy_plane = xy_plane * current_mip_resolution_inv + uv_offset;

    // t = (p - o) / d  — parametric intersection with boundary planes
    float2 t = xy_plane * inv_direction.xy - origin.xy * inv_direction.xy;
    current_t = min(t.x, t.y);   // nearest XY boundary
    position = origin + current_t * direction;
}
```

A small `uv_offset = 0.005 * exp2(most_detailed_mip) / screen_size` is added to push the
position just past the cell boundary, preventing infinite loops at exact boundaries.

### Core Advance Step

```hlsl
bool FFX_SSSR_AdvanceRay(
    float3 origin, float3 direction, float3 inv_direction,
    float2 current_mip_position, float2 current_mip_resolution_inv,
    float2 floor_offset, float2 uv_offset,
    float surface_z,                        // depth sampled from Hi-Z at current_mip
    inout float3 position, inout float current_t)
{
    // Boundary planes for current cell (XY) and depth surface (Z)
    float2 xy_plane = floor(current_mip_position) + floor_offset;
    xy_plane = xy_plane * current_mip_resolution_inv + uv_offset;
    float3 boundary_planes = float3(xy_plane, surface_z);

    // t = (p - o) / d for each boundary plane
    float3 t = boundary_planes * inv_direction - origin * inv_direction;

    // If shooting away from depth buffer, ignore Z plane (t.z = +inf)
    // (supports both standard and reversed-Z via #define)
    t.z = direction.z > 0 ? t.z : FFX_SSSR_FLOAT_MAX;

    float t_min = min(min(t.x, t.y), t.z);

    // Is ray still above the depth surface?
    bool above_surface = surface_z > position.z;  // standard Z convention

    // skipped_tile: did we cross an XY boundary (not Z)? => can step to coarser mip
    bool skipped_tile = asuint(t_min) != asuint(t.z) && above_surface;

    current_t = above_surface ? t_min : current_t;
    position = origin + current_t * direction;

    return skipped_tile;  // true = increase mip, false = decrease mip
}
```

### Main Traversal Loop

```hlsl
float3 FFX_SSSR_HierarchicalRaymarch(
    float3 origin, float3 direction, bool is_mirror,
    float2 screen_size, int most_detailed_mip,
    uint min_traversal_occupancy, uint max_traversal_intersections,
    out bool valid_hit)
{
    const float3 inv_direction = direction != 0 ? 1.0 / direction : FFX_SSSR_FLOAT_MAX;

    int   current_mip             = most_detailed_mip;
    float2 current_mip_resolution     = FFX_SSSR_GetMipResolution(screen_size, current_mip);
    float2 current_mip_resolution_inv = rcp(current_mip_resolution);

    // Offset to nudge past cell boundary (prevents self-intersection + boundary loops)
    float2 uv_offset = 0.005 * exp2(most_detailed_mip) / screen_size;
    uv_offset = direction.xy < 0 ? -uv_offset : uv_offset;
    float2 floor_offset = direction.xy < 0 ? 0 : 1;

    float  current_t;
    float3 position;
    FFX_SSSR_InitialAdvanceRay(origin, direction, inv_direction,
        current_mip_resolution, current_mip_resolution_inv,
        floor_offset, uv_offset, position, current_t);

    // Wave-level early exit: if most lanes in the wave have finished,
    // stop remaining lanes early (reduces GPU divergence latency)
    bool exit_due_to_low_occupancy = false;

    int i = 0;
    while (i < max_traversal_intersections
        && current_mip >= most_detailed_mip
        && !exit_due_to_low_occupancy)
    {
        float2 current_mip_position = current_mip_resolution * position.xy;
        float  surface_z            = FFX_SSSR_LoadDepth(current_mip_position, current_mip);

        // Low-occupancy exit: don't run non-mirror rays when wave is mostly done
        exit_due_to_low_occupancy = !is_mirror
            && WaveActiveCountBits(true) <= min_traversal_occupancy;

        bool skipped_tile = FFX_SSSR_AdvanceRay(
            origin, direction, inv_direction,
            current_mip_position, current_mip_resolution_inv,
            floor_offset, uv_offset, surface_z, position, current_t);

        // Mip increment/decrement and matching resolution update
        current_mip             += skipped_tile ? 1 : -1;
        current_mip_resolution  *= skipped_tile ? 0.5 : 2.0;
        current_mip_resolution_inv *= skipped_tile ? 2.0 : 0.5;
        ++i;
    }

    valid_hit = (i <= max_traversal_intersections);
    return position;
}
```

**Key insight on mip logic:**
- `skipped_tile = true` (above surface, crossed XY boundary): increase mip (0.5x resolution) → take larger steps
- `skipped_tile = false` (may have hit surface): decrease mip (2x resolution) → refine
- Loop terminates when `current_mip < most_detailed_mip` — this means the ray hit at the
  finest level and confirmed an intersection

**Traversal rate vs roughness:**
- Mirror surfaces: start at mip 0 (finest) → full traversal resolution
- Glossy surfaces: start at a higher `most_detailed_mip` (e.g., 2 or 3) → coarser start, fewer iterations

### Constants and Parameters

| Parameter | Typical Value | Notes |
|-----------|--------------|-------|
| `max_traversal_intersections` | 64–128 | Per-ray iteration cap |
| `min_traversal_occupancy` | 4 | Wave lanes still active before early-exit |
| `most_detailed_mip` | 0 (mirror) / 1-2 (glossy) | Starting mip level |
| `uv_offset` | `0.005 * exp2(mip) / screen_size` | Self-intersection avoidance |
| `depth_buffer_thickness` | 0.015–0.05 | World-space depth tolerance for hit validation |

### Hit Validation

After the loop, the hit position must pass several sanity checks:

```hlsl
float FFX_SSSR_ValidateHit(float3 hit, float2 uv, float3 world_space_ray,
    float2 screen_size, float depth_buffer_thickness)
{
    // Reject if outside screen
    if (any(hit.xy < 0) || any(hit.xy > 1)) return 0;

    // Reject if ray didn't advance meaningfully (prevents self-reflection)
    float2 manhattan_dist = abs(hit.xy - uv);
    if (all(manhattan_dist < (2.0 / screen_size))) return 0;

    // Reject background hits (depth == far plane)
    float surface_z = FFX_SSSR_LoadDepth(texel_coords / 2, 1);
    if (surface_z == 1.0) return 0;   // or == 0.0 for reversed-Z

    // Reject backface hits (normal facing away from ray)
    float3 hit_normal = FFX_SSSR_LoadWorldSpaceNormal(texel_coords);
    if (dot(hit_normal, world_space_ray_direction) > 0) return 0;

    // Depth thickness test: how far is the hit below the GBuffer surface?
    float3 view_surface = ScreenSpaceToViewSpace(float3(hit.xy, surface_z));
    float3 view_hit     = ScreenSpaceToViewSpace(hit);
    float  distance     = length(view_surface - view_hit);

    // Screen-edge vignette fade
    float2 fov    = 0.05 * float2(screen_size.y / screen_size.x, 1.0);
    float2 border = smoothstep(0, fov, hit.xy) * (1 - smoothstep(1 - fov, 1, hit.xy));
    float  vignette = border.x * border.y;

    // Confidence: 1.0 at surface, fades to 0 at depth_buffer_thickness below surface
    float confidence = 1 - smoothstep(0, depth_buffer_thickness, distance);
    confidence *= confidence;  // squared — faster falloff

    return vignette * confidence;
}
```

---

## 8. Environment Map Fallback and Blending

When a hit is valid, the reflected radiance comes from the lit scene buffer at `hit.xy`.
When there is no valid hit (off-screen, too rough, etc.), the algorithm falls back to the
environment cubemap, sampled in the reflection direction.

```hlsl
// From Intersect.hlsl
float confidence = valid_hit ? FFX_SSSR_ValidateHit(...) : 0.0;

float3 reflection_radiance = 0;
if (confidence > 0) {
    // Screen-space hit: read from lit scene color
    reflection_radiance = g_lit_scene.Load(int3(screen_size * hit.xy, 0)).xyz;
}

// Always compute env-map as fallback
float3 world_reflected = mul(g_inv_view, float4(view_space_reflected_direction, 0)).xyz;
float3 environment     = SampleEnvironmentMap(world_reflected);

// Lerp between env-map (confidence=0) and screen hit (confidence=1)
reflection_radiance = lerp(environment, reflection_radiance, confidence);
```

For the env-map fallback for rough surfaces (skipped in ClassifyTiles), roughness maps to
mip level: `mip = roughness * (mip_count - 1)` where `mip_count = 10` in the sample.
This matches the split-sum IBL prefiltered environment approach.

---

## 9. Denoiser: Reproject Pass

The denoiser is the AMD DNSR (Denoiser for Reflections), which ships alongside SSSR.

### Reprojection Strategy

Standard motion-vector reprojection moves a pixel from current UV to previous-frame UV using
surface velocity. For reflections this is wrong: the reflected view is view-dependent, so
the "parallax" of the reflection is different from the surface parallax.

SSSR uses hit-point reprojection:

```
For pixel P at current frame:
  - reflection hit point H (in world space) was stored as part of the intersection output
  - reproject H into previous frame's view to get prev_H_screen
  - fetch history radiance at prev_H_screen
```

This correctly handles camera panning: the reflection hit moves with the camera relative to
the surface, so surface motion vectors alone underestimate the change.

The actual implementation in ffx_denoiser_reflections_reproject.h (submodule) computes
a "reprojection confidence" based on:
- Depth similarity: is the surface at prev_H_screen close to expected depth?
- Normal similarity: is the previous-frame normal close to current normal?
- Roughness similarity: did the surface roughness change?

### History Accumulation Weight

The temporal blend factor uses an exponential moving average controlled by
`g_temporal_stability_factor`. A higher value blends more strongly with history (more stable,
more ghosting risk). A lower value responds faster but is noisier.

The sample count is tracked per-pixel:
- On successful reprojection: `num_samples = prev_num_samples + 1`, blend factor = `1 / num_samples` (unbiased EMA)
- On disocclusion: `num_samples = 1`, start fresh
- `num_samples` is clamped to `32` to prevent infinite lag

### Variance Tracking

The reproject pass also maintains:
- `average_radiance` (running average of reflected color)
- `variance` (second moment − mean^2)

High variance pixels trigger adaptive extra tracing in the next frame
(if `g_temporal_variance_guided_tracing_enabled`).

---

## 10. Denoiser: Prefilter Pass (Spatial)

The prefilter is a bilateral filter over a 3x3 (or larger) neighborhood in the SSR buffer.
Key bilateral weights:

```
roughness_weight = exp(-abs(center_roughness - neighbor_roughness) / sigma_roughness)
depth_weight     = exp(-abs(center_depth - neighbor_depth) / sigma_depth)
combined_weight  = roughness_weight * depth_weight * spatial_gaussian(distance)
```

Constants from Common.hlsl:
```hlsl
static const float g_roughness_sigma_min = 0.01f;
static const float g_roughness_sigma_max = 0.02f;
static const float g_depth_sigma         = 0.02f;
```

The filter is variance-guided: the kernel radius and weights adapt based on the local
variance estimate from the reproject pass. High-variance regions use a wider kernel.

It also incorporates `average_radiance` from the reprojection pass as a stable anchor:
if a neighbor's radiance is far from the running average, its weight is reduced.

---

## 11. Denoiser: Temporal Resolve Pass

The final pass blends the prefiltered current frame against the reprojected history:

```
out_radiance = lerp(history_radiance, current_filtered, blend_factor)
```

where `blend_factor = 1 / (num_samples + 1)` (decays as history accumulates).

The resolve also outputs:
- Updated `variance` (for next frame's adaptive tracing)
- Updated `sample_count` (capped at 32)
- Final `out_radiance` to be composited into the scene

---

## 12. Composite / Apply Pass

After the denoiser, `ApplyReflections.hlsl` composites SSR radiance into the scene using
the split-sum IBL formula:

```hlsl
float3 getIBLContribution(
    float perceptualRoughness, float3 specularColor,
    float3 specularLight, float3 n, float3 v)
{
    float NdotV = clamp(dot(n, v), 0.0, 1.0);
    float2 brdf = brdfLUT.Sample(sampler, float2(NdotV, perceptualRoughness)).rg;
    return specularLight * (specularColor * brdf.x + brdf.y);
}
```

The `brdf.x` term scales F0 and `brdf.y` is the environment BRDF bias term, from the
pre-integrated BRDF LUT (same as IBL specular split-sum). This ensures the reflection
energy is correctly weighted by the GGX BRDF — not just applied flat.

The composite factor `2.0 * radiance` in the sample is a scene-specific exposure adjustment.

---

## 13. McGuire & Mara 2014: DDA-Based Linear Ray March

The SSSR Hi-Z approach replaces the simpler DDA linear march from McGuire & Mara 2014
(JCGT). Understanding the DDA approach clarifies why Hi-Z is needed.

### DDA Linear March

The DDA extends the classic Bresenham line algorithm with perspective-correct depth
interpolation. The key insight is that in screen space, the ray travels along a line
(after perspective division), so it can be stepped one screen pixel at a time.

```
Parameters:
  csOrigin     -- camera-space ray origin
  csDirection  -- camera-space ray direction (normalized)
  projectToPixelMatrix -- transforms camera-space to pixel space
  csZBufferTexture -- camera-space depth (linear, not NDC)
  zThickness   -- assumed depth "thickness" of surfaces
  stride       -- step size in pixels (1 = full resolution, 4 = quarter res)
  jitter       -- [0,1) random value to break step banding artifacts
  maxSteps     -- maximum steps before giving up
  maxDistance  -- maximum camera-space distance
```

The algorithm:
1. Project origin + (origin + direction) to find the screen-space ray direction
2. Walk along the major axis (max |dx|, |dy|) in pixel steps of `stride`
3. At each step, compare the ray's interpolated camera-space depth against the depth buffer
4. Accept a hit when the ray depth exceeds the buffer depth (and is within `zThickness`)

```
q0 = origin projected to screen
q1 = (origin + direction * maxDistance) projected to screen
dt = normalize(q1 - q0)  // screen-space step direction

for step in 0..maxSteps:
  ray_pos = q0 + dt * step * stride + jitter
  depth_at_ray = interpolate(origin.z, (origin + direction * maxDistance).z, t)
  depth_in_buffer = csZBuffer.Load(ray_pos.xy)
  if depth_at_ray < depth_in_buffer - zThickness:
    continue  // ray still in front of surface, step forward
  if depth_at_ray <= depth_in_buffer:
    return hit at ray_pos  // ray intersects the surface slab
```

### DDA Limitations vs Hi-Z

| Aspect | DDA (McGuire 2014) | Hi-Z (Uludag / SSSR) |
|--------|-------------------|----------------------|
| Complexity | O(N) where N = max steps | O(log N) |
| Miss detection | Limited — misses thin geometry between steps | Better — min depth prevents false misses |
| Long rays | Requires many steps for distant reflections | Handles any length efficiently |
| Implementation | Simple | Complex (mip pyramid + state machine) |
| GPU occupancy | Good (coherent step sizes) | Complex (adaptive mip causes divergence) |
| Stride jitter | Required to hide banding | Not needed |

McGuire 2014 recommends `stride = 2` (half-resolution march) with jitter to hide the
stride artifacts, then a binary search refinement step near the hit for accurate UV.

The SSSR technique uses Hi-Z for performance but is conceptually the same problem: march
a ray through the depth buffer until it intersects. The main difference is the O(log N)
acceleration for long rays.

---

## 14. Key Differences: Stochastic vs Mirror-Only SSR

| Feature | Simple Mirror SSR | Stochastic SSR (Stachowiak 2015) |
|---------|-------------------|----------------------------------|
| Rays per pixel | 1 (deterministic) | 0.25–1 (stochastic, amortized via quad) |
| Ray direction | reflect(view, normal) | GGX VNDF importance sample |
| Roughness handling | None (always mirror) | Per-pixel, drives lobe width |
| Contact hardening | No | Yes (naturally emerges from local hit distances) |
| Specular elongation | No | Yes (anisotropic alpha_x, alpha_y) |
| Noise | None (deterministic) | Present — needs denoiser |
| Denoiser | Not needed | Bilateral prefilter + temporal accumulation |
| Env-map fallback | Blended at screen edges | Confidence-weighted per-pixel |
| Temporal history | N/A or simple average | Variance-tracked EMA (capped 32 frames) |
| GPU cost (1080p) | ~0.5ms | ~1.7ms (downsampled) to ~4.15ms (high quality) |

---

## 15. Specular Elongation and Contact Hardening

### Contact Hardening
When a surface reflects a nearby object, the reflected image is sharp because the reflection
ray is nearly perpendicular to the surface and the object subtends a large solid angle.
For distant reflections, the reflected image is wider (blurrier for rough surfaces).

In stochastic SSR, contact hardening emerges naturally:
- Near-contact hit: `world_ray_length` is small → the ray hit close → confidence is high
- The denoiser stores `world_ray_length` in the alpha channel of the intersection output
- The denoiser's spatial filter uses this to modulate kernel width: short ray length = tighter filter

### Specular Elongation
Anisotropic materials have different roughness along tangent (alpha_x) and bitangent (alpha_y)
directions. VNDF sampling takes `alpha_x` and `alpha_y` separately, so the sampled normal
distribution is ellipsoidal rather than circular → reflection rays concentrate in a streak
along the low-roughness axis → natural specular elongation.

---

## 16. Implementation Checklist for UzEngine SSR

Based on this analysis, a full Stachowiak-parity SSR implementation requires:

```
Pass 1: Hi-Z Construction
  - FidelityFX SPD (Single Pass Downsampler) or manual min-reduction mip chain
  - 7 mip levels from scene depth
  - Support reversed-Z (UzEngine uses reversed-Z)

Pass 2: Tile Classification (8x8 threadgroups)
  - roughness threshold check (skip if too rough → use env-map only)
  - IsBaseRay() per samples_per_quad config (1/2/4)
  - copy_horizontal / copy_vertical / copy_diagonal wave quad operations
  - temporal variance guided tracing (optional, major quality improvement)
  - output: ray_list (indirect dispatch args), intersection_output pre-filled with env-map

Pass 3: Blue Noise Generation (128x128, per frame)
  - Owen-scrambled Sobol sequence (Heitz & Belcour 2019)
  - Golden ratio temporal animation per frame_index

Pass 4: Indirect Argument Preparation
  - Divide ray_counter[0] by 64, round up

Pass 5: Intersection (indirect dispatch)
  - GGX VNDF sampling via SampleGGXVNDF() [Heitz 2018]
  - Hi-Z traversal via FFX_SSSR_HierarchicalRaymarch()
  - Hit validation via FFX_SSSR_ValidateHit()
  - Env-map fallback: lerp(env, screen_color, confidence)
  - Output: float4(radiance.xyz, world_ray_length)

Pass 6: Reproject (DNSR)
  - Motion vector reprojection for hit-point, not just surface
  - Normal + depth + roughness similarity checks
  - Sample count tracking (max 32)
  - Variance + average_radiance output

Pass 7a: Prefilter (spatial, 3x3 bilateral)
  - roughness_sigma = [0.01, 0.02]
  - depth_sigma = 0.02
  - variance-guided kernel width

Pass 7b: Temporal Resolve
  - EMA with 1/(num_samples+1) blend factor
  - Output final denoised radiance + updated variance + sample_count

Pass 8: Apply/Composite
  - Split-sum IBL weighting: radiance * (F0 * brdf.x + brdf.y)
  - brdf.xy from pre-integrated BRDF LUT at (NdotV, perceptualRoughness)
```

---

## 17. Shader Citations for Implementation

Every shader function must carry the mandatory citation header per RULE 3:

```hlsl
/// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
/// JCGT Vol. 7 No. 4 — VNDF sampling for isotropic GGX
/// @constants alpha_x, alpha_y (roughness), U1, U2 (blue noise)
float3 SampleGGXVNDF(...) { ... }

/// @ref Uludag 2014 "Hi-Z Screen-Space Cone-Traced Reflections" GPU Pro 5
/// @ref FidelityFX SSSR 2021 (MIT) — production Hi-Z traversal implementation
/// @constants uv_offset = 0.005 * exp2(mip) / screen_size
float3 HierarchicalRaymarch(...) { ... }

/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" SIGGRAPH
/// Advances in Real-Time Rendering — stochastic lobe sampling + temporal filter
float3 SampleReflectionVector(...) { ... }
```

---

## Sources

- [Stochastic Screen-Space Reflections (Frostbite)](https://www.ea.com/frostbite/news/stochastic-screen-space-reflections)
- [SIGGRAPH 2015 Advances in Real-Time Rendering](https://advances.realtimerendering.com/s2015/)
- [Stochastic Screen-Space Reflections slides (SlideShare)](https://www.slideshare.net/DICEStudio/stochastic-screenspace-reflections)
- [AMD FidelityFX SSSR (GPUOpen)](https://gpuopen.com/fidelityfx-sssr/)
- [FidelityFX SSSR source code (GitHub, MIT)](https://github.com/GPUOpen-Effects/FidelityFX-SSSR)
- [FidelityFX SSSR 1.5 Manual (GPUOpen)](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)
- [Efficient GPU Screen-Space Ray Tracing — McGuire & Mara 2014 (JCGT)](https://jcgt.org/published/0003/04/04/)
- [Sampling the GGX Distribution of Visible Normals — Heitz 2018 (JCGT)](https://jcgt.org/published/0007/04/01/)
- [Hi-Z Screen-Space Cone-Traced Reflections — Uludag (GPU Pro 5)](https://www.taylorfrancis.com/chapters/edit/10.1201/b16721-13/hi-screen-space-cone-traced-re%EF%AC%82ections-yasin-uludag)
- [Reprojecting Reflections — JPGrenier](https://www.jpgrenier.org/reflections.html)
- [Screen Space Reflections Hi-Z — Sakib Saikia](https://sakibsaikia.github.io/graphics/2016/12/26/Screen-Space-Reflection-in-Killing-Floor-2.html)
- [Notes on screenspace reflections with FidelityFX SSSR — Interplay of Light](https://interplayoflight.wordpress.com/2022/09/28/notes-on-screenspace-reflections-with-fidelityfx-sssr/)
