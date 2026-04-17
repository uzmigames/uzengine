<!-- doc-meta
title: Screen-Space Reflections — Academic Survey Part 1: Traversal, Sampling, Denoising
subsystem: rendering/ssr
status: reference
date: 2026-04-05
-->

# Screen-Space Reflections — Academic Survey Part 1: Traversal, Sampling, Denoising

A comprehensive survey of foundational SSR papers covering ray traversal algorithms,
stochastic GGX sampling, temporal supersampling, and denoising pipelines.

**Part 2** (`ssr-academic-survey-part2.md`) covers blue-noise sampling, AMD SSSR
production pipeline, hybrid RT, NVIDIA NRD, and BibTeX references.

For UzEngine-specific implementation recommendations see `ssr-state-of-the-art-2025.md`.

---

## Table of Contents

1. [Foundations — DDA Ray Marching (2009–2014)](#1-foundations)
2. [Hi-Z Hierarchical Traversal (GPU Pro 5, 2014)](#2-hi-z-traversal)
3. [Stochastic GGX Sampling — Frostbite (SIGGRAPH 2015)](#3-stochastic-frostbite)
4. [GGX VNDF Importance Sampling (Heitz 2018)](#4-vndf-sampling)
5. [Bounded VNDF — Spherical Caps (Dupuy & Benyoub 2023)](#5-bounded-vndf)
6. [Temporal Supersampling & Neighborhood Clamping (Karis 2014)](#6-temporal-karis)
7. [SVGF — Spatiotemporal Variance-Guided Filtering (Schied 2017)](#7-svgf)
8. [Neural Denoising for Reflections (Chaitanya 2017)](#8-neural-denoising)

---

## 1. Foundations — DDA Ray Marching (2009–2014) {#1-foundations}

### 1.1 Origins

Screen-space reflections trace roots to Crytek's GDC 2009 demo of CryEngine 3 and
the independently released Crysis 2 (2011). The technique was described publicly by
Tiago Sousa at SIGGRAPH 2011 in "Secrets of CryENGINE 3 Graphics Technology".

The foundational academic treatment appeared in:

> **McGuire, M. and Mara, M.** "Efficient GPU Screen-Space Ray Tracing."
> *Journal of Computer Graphics Techniques* **3**(4), pp. 73–85, 2014.
> Available: https://jcgt.org/published/0003/04/04/

McGuire & Mara adapted the classical **perspective-correct DDA** rasterization
algorithm to ray march through the depth buffer as a height field. This gave the
first full production-quality implementation with well-defined failure modes.

### 1.2 Core DDA Algorithm

The ray is parameterized in **screen space** (NDC XY + depth Z) rather than world
space. The DDA walks pixel columns/rows, adjusting the step to maintain perspective
correctness.

```
// McGuire & Mara 2014 — DDA SSR (conceptual pseudocode)
// Input:  csOrigin (camera-space ray origin), csDirection (camera-space ray dir)
// Output: hitPixel, hitPoint, steps taken, bool intersection

Inputs:
  csOrigin      : float3   // camera-space origin (GBuffer world pos reconstructed)
  csDirection   : float3   // reflected direction in camera space
  jitter        : float    // [0,1] per-pixel noise to offset first step
  maxSteps      : int      // 128–256 typical
  stride        : float    // screen-space step size in pixels (1–4)
  maxDistance   : float    // world-space cutoff
  thickness     : float    // depth tolerance (prevents ray seeing through thin geo)

Algorithm:
  1. Clip csOrigin to near plane if negative Z
  2. Project csOrigin and csOrigin+csDirection into screen space → P0, P1
  3. Compute DDA step direction from P0→P1 in screen space
  4. Normalize step to 1 pixel in dominant axis, scale by `stride`
  5. Advance step.z proportionally (perspective correction via W coordinate)
  6. For each step i = 0..maxSteps:
     a. Sample depth buffer at current screen position → sceneDepth
     b. Compute ray depth at this screen position → rayDepth
     c. if (sceneDepth < rayDepth) and (rayDepth - sceneDepth < thickness):
           → INTERSECTION FOUND → binary refinement
     d. Advance position by DDA step
  7. Binary refinement (4–8 steps): bisect between last two positions to
     find precise intersection along the ray

Key perspective correction:
  The z component of the step is not constant — it must be linearly interpolated
  in homogeneous space (1/w is linearly interpolated across screen pixels):
    dz/dpixel = (p1.z/p1.w - p0.z/p0.w) / dist(p0, p1)
```

### 1.3 Key Constants (McGuire & Mara 2014)

| Constant | Published Value | Notes |
|----------|----------------|-------|
| `maxSteps` | 128–256 | Tradeoff: quality vs. perf |
| `stride` | 1–4 pixels | 1 = highest quality, 4 = 4× faster |
| `jitter` | `fract(sin(pixel.x*0.1+pixel.y)*127.1)` | Breaks step-banding artifacts |
| `thickness` | 0.1–1.0 (world-space cm) | Scene-dependent |
| `maxDistance` | 100–500m (world-space) | Scene-scale-dependent |

### 1.4 Screen-Edge Fade

The canonical fade function (used by McGuire & Mara and all successors):

```hlsl
// Compute UV distance from screen center [0,1] where 1 = at edge
float2 uvEdge  = abs(hitUV * 2.0 - 1.0);  // [-1,1] → [0,1] from edge
float  edgeFade = 1.0 - smoothstep(fadeStart, 1.0, max(uvEdge.x, uvEdge.y));
// fadeStart typically 0.75 → fade begins at 75% of screen from center
```

Parameter `fadeStart = 0.75` is the standard published default. Values closer to 1.0
give more abrupt edges; values closer to 0.0 give smoother but earlier fading.

### 1.5 Limitations of Linear DDA

| Limitation | Cause |
|-----------|-------|
| Step-banding artifacts at glancing angles | Constant step size in screen space |
| O(n) with step count — slow for long rays | Linear walk, no acceleration |
| Misses objects behind depth buffer | Single-layer depth only |
| Cannot handle transparent surfaces | Depth buffer is opaque only |
| Self-intersection acne | Ray origin inside depth surface |

---

## 2. Hi-Z Hierarchical Traversal (GPU Pro 5, 2014) {#2-hi-z-traversal}

### 2.1 Source

> **Uludag, Y.** "Hi-Z Screen-Space Cone-Traced Reflections."
> In *GPU Pro 5: Advanced Rendering Techniques*, ed. Wolfgang Engel,
> A K Peters/CRC Press, Chapter 4, pp. 149–192, 2014.
> DOI: 10.1201/b16721-13

Developed during early development of *Mirror's Edge* at DICE/EA. First use of a
hierarchical depth mipmap to accelerate screen-space ray traversal.

### 2.2 Depth Pyramid Construction

The Hi-Z pyramid (also called HZB — Hierarchical Z Buffer) stores the **minimum raw
depth** (maximum world-space depth, i.e., furthest from camera) at each mip level:

```
// Build Hi-Z mip chain (one compute pass per mip level)
// Mip 0 = full-resolution depth
// Mip k = max(child depth values over 2×2 region of mip k-1)
// NOTE: with reversed-Z (0=far, 1=near), max raw depth = min world depth
//       Use MIN for reversed-Z, MAX for classic Z (0=near, 1=far)

for each mip level k from 1 to MAX_MIPS:
  for each texel (x,y) at mip k:
    mip[k][x,y] = max(mip[k-1][2x,2y],   mip[k-1][2x+1,2y],
                      mip[k-1][2x,2y+1], mip[k-1][2x+1,2y+1])
```

The conservative property: if a ray does not intersect a coarse-level cell, it cannot
intersect any finer-level cell within it. This allows large empty regions to be skipped
in O(1) steps.

### 2.3 Hi-Z Traversal Algorithm

```
// Uludag 2014 — Hi-Z ray march (conceptual pseudocode)
// All coordinates in [0,1] NDC space

ray_pos  = screen_space_origin(reflect_pos, camera)
ray_dir  = project_direction(reflect_dir, camera)

current_level = MAX_MIP_LEVEL    // start coarse
max_steps     = 128

for step in 0..max_steps:
  // Size of one texel at the current mip level
  texel_size = 1.0 / (resolution >> current_level)

  // Intersect ray with axis-aligned cell boundaries at current_level
  plane_x = (floor(ray_pos.x / texel_size) + (ray_dir.x > 0 ? 1 : 0)) * texel_size
  plane_y = (floor(ray_pos.y / texel_size) + (ray_dir.y > 0 ? 1 : 0)) * texel_size

  // Time to cross each boundary
  t_x = (plane_x - ray_pos.x) / ray_dir.x
  t_y = (plane_y - ray_pos.y) / ray_dir.y
  t_cell = min(t_x, t_y)  // nearest boundary crossing

  // Advance ray to cell boundary
  new_pos = ray_pos + ray_dir * t_cell

  // Sample Hi-Z at current_level
  scene_depth = SampleHiZ(new_pos.xy, current_level)

  if (new_pos.z > scene_depth):  // ray is below surface (behind depth)
    if (current_level == 0):
      // Intersection confirmed at mip 0
      return hit(new_pos)
    else:
      // Descend: test at finer level
      current_level -= 1
  else:
    // No intersection at this level — ascend and advance
    ray_pos = new_pos
    current_level = min(current_level + 1, MAX_MIP_LEVEL)
```

### 2.4 Performance Characteristics

- **Best case** (mirror, perpendicular): ~10–20 steps (logarithmic convergence)
- **Worst case** (grazing angle, cluttered scene): ~128 steps (degrades to DDA)
- **Typical case** (indoor scene, 45° reflection): ~30–60 steps
- **Speedup vs. DDA**: 2–5× for long rays, similar for short rays

AMD SSSR v1.5 benchmarks show Hi-Z traversal takes ~0.88ms at 1080p on RDNA2
for a typical indoor scene with 1 ray per pixel at half roughness.

---

## 3. Stochastic GGX Sampling — Frostbite (SIGGRAPH 2015) {#3-stochastic-frostbite}

### 3.1 Source

> **Stachowiak, T.** "Stochastic Screen-Space Reflections."
> In *Advances in Real-Time Rendering in 3D Graphics and Games*,
> SIGGRAPH 2015 Course, EA Frostbite, 2015.
> Slides: https://www.slideshare.net/DICEStudio/stochastic-screenspace-reflections
> Blog post: https://www.ea.com/frostbite/news/stochastic-screen-space-reflections

First shipped in *Mirror's Edge Catalyst* and *Need for Speed* (2015).

### 3.2 Core Contributions

**1. Half-Resolution Tracing**: All rays are traced at half resolution. Adjacent
pixels sample different parts of the GGX lobe (due to different random seeds), so the
half-res result encodes the full lobe distribution via spatial correlation. A full-res
bilateral upsample reconstructs fine detail.

**2. GGX NDF Importance Sampling** (later upgraded to VNDF in SSSR v1.5):
Instead of a mirror ray, each pixel samples one direction from the GGX NDF:

```hlsl
// Stachowiak 2015 — GGX NDF importance sampling for SSR ray direction
// u: 2D uniform random variable [0,1)^2 (from blue noise texture)
// N: surface normal, V: view direction, alpha: roughness^2

float3 SampleGGX_NDF(float3 N, float3 V, float alpha, float2 u)
{
    // Sample isotropic GGX NDF — spherical coords
    float phi      = 2.0 * PI * u.x;
    float cosTheta = sqrt((1.0 - u.y) / (1.0 + (alpha*alpha - 1.0) * u.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Tangent-space half-vector
    float3 H_tangent = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // Rotate to world space using N as Z-axis
    float3 T, B;
    BuildTangentFrame(N, T, B);
    float3 H = normalize(H_tangent.x * T + H_tangent.y * B + H_tangent.z * N);

    // Reflect view over half-vector
    return reflect(-V, H);  // R = 2(V·H)H - V
}
```

**3. Virtual-Hit Reprojection**: stores the per-pixel hit distance along the ray to
compute the world-space position of the reflected point. This enables temporal
accumulation to track the reflected content (not just the reflecting surface):

```
// Frostbite 2015 — virtual position reprojection
hit_point_world  = camera_pos + R * hit_distance   // in world space
hit_point_prev   = mul(prev_VP_matrix, float4(hit_point_world, 1))
prev_uv          = hit_point_prev.xy / hit_point_prev.w * 0.5 + 0.5
history_sample   = SampleHistory(prev_uv)
```

**4. Spatial Filter Exploiting BRDF Correlation**: adjacent pixels sample different
parts of the same GGX lobe. A spatial filter over a k×k neighborhood performs
Monte Carlo integration by **reusing** each pixel's ray as a contribution to
neighboring pixels, weighted by the BRDF at each neighbor's microfacet orientation.

### 3.3 Parameters

| Parameter | Stachowiak 2015 Default | Notes |
|-----------|------------------------|-------|
| Trace resolution | Half-res | 4× cheaper, reconstructed bilaterally |
| Rays per pixel | 1 | Combined with spatial reuse across 5×5 |
| Roughness cutoff | ~0.6 | Fallback to IBL above this |
| Temporal alpha | 0.1 | 10% current, 90% history |
| Spatial filter radius | 5×5 | Bilateral, depth+normal weighted |

---

## 4. GGX VNDF Importance Sampling (Heitz 2018) {#4-vndf-sampling}

### 4.1 Source

> **Heitz, E.** "Sampling the GGX Distribution of Visible Normals."
> *Journal of Computer Graphics Techniques* **7**(4), pp. 1–13, 2018.
> Available: https://jcgt.org/published/0007/04/01/

### 4.2 Motivation: Why VNDF > NDF

NDF sampling (Section 3.2) generates half-vectors uniformly over the full
microfacet hemisphere, regardless of the viewing direction. Many of these samples
lie in directions occluded by the macrosurface (the reflected ray would hit the
back side of the surface), contributing zero energy. This wastes samples.

**VNDF** (Visible Normal Distribution Function) is the NDF conditioned on
the incident direction V — it describes only the distribution of normals visible
from V. Every sample from the VNDF contributes nonzero energy.

For low roughness (metallic highlights), VNDF reduces variance by 2–4× vs. NDF.
For high roughness (diffuse-like), the benefit is smaller but still present.

### 4.3 The Heitz 2018 Algorithm

The GGX VNDF is sampled by exploiting the fact that GGX is the distribution of
visible normals of a **truncated ellipsoid**, and sampling this projected ellipsoid
is equivalent to sampling a hemisphere:

```hlsl
/// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
/// @constants Uses exact math; no tunable constants.
float3 SampleGGX_VNDF(float3 Ve, float alpha_x, float alpha_y, float u1, float u2)
{
    // Ve: incident direction in tangent space (z is up, must have Ve.z > 0)
    // alpha_x, alpha_y: GGX roughness X and Y (isotropic: alpha_x = alpha_y = roughness)
    // u1, u2: uniform random [0,1)

    // Step 1: Stretch the incident direction
    float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));

    // Step 2: Orthonormal basis around Vh
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) / sqrt(lensq) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);

    // Step 3: Sample point on disk
    float r  = sqrt(u1);
    float phi = 2.0 * PI * u2;
    float t1  = r * cos(phi);
    float t2  = r * sin(phi);
    float s   = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    // Step 4: Reprojection to hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2)) * Vh;

    // Step 5: Unstretch the sampled normal
    float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
    return Ne;  // Half-vector in tangent space
}

// Caller: convert to world-space reflection ray
// float3 H = TangentToWorld(SampleGGX_VNDF(WorldToTangent(V), rough, rough, u.x, u.y));
// float3 R = reflect(-V, H);
```

### 4.4 Properties

- **Unbiased**: produces the exact GGX VNDF distribution
- **No rejection sampling**: every sample is valid (no backface rejection needed)
- **Time complexity**: O(1), no iterative rejection loops
- **Isotropy**: for isotropic GGX, use `alpha_x = alpha_y = roughness^2`

---

## 5. Bounded VNDF — Spherical Caps (Dupuy & Benyoub 2023) {#5-bounded-vndf}

### 5.1 Sources

> **Dupuy, J. and Benyoub, A.** "Sampling Visible GGX Normals with Spherical Caps."
> *Computer Graphics Forum* **42**(8), 2023.
> arXiv: 2306.05044. DOI: 10.1111/cgf.14867

> **Boksansky, J. et al.** "Bounded VNDF Sampling for Smith–GGX Reflections."
> *Proceedings of the ACM SIGGRAPH Symposium on Interactive 3D Graphics and Games*
> (I3D 2024), HPG 2023. DOI: 10.1145/3610543.3626163
> Supplementary: https://gpuopen.com/download/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections_Supplementary_Document.pdf

### 5.2 Key Insight

Heitz 2018's VNDF sampling uses a disk projected onto the hemisphere, but the
projection can sample directions outside the **visible hemispherical cap** centered
on the specular mirror direction. These samples are still valid but have lower
contribution, especially at high roughness.

Dupuy & Benyoub show that the GGX VNDF can be linked to a **spherical cap** centered
on the reflected view direction. The Boksansky et al. extension introduces an
**analytical bound** for the cap size that eliminates the out-of-bounds fraction:

```hlsl
/// @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps"
/// @ref Boksansky et al. 2023 "Bounded VNDF Sampling for Smith-GGX Reflections"
/// @constants s_bound derived from roughness; simpler routine than Heitz 2018
float3 SampleGGX_VNDF_SphericalCap(float3 wi, float alpha, float2 u)
{
    // wi: incident direction in tangent space (must have wi.z > 0)
    // alpha: GGX roughness (isotropic)
    // u: uniform [0,1)^2

    float phi = 2.0 * PI * u.x;
    float a   = saturate(min(alpha, 0.9999));   // avoid singularity at alpha=1

    // Compute the cap half-angle using the Dupuy & Benyoub bound
    float s = 1.0 + length(float2(wi.x, wi.y));
    float s2 = s * s;
    float k  = (1.0 - a * a) * s2 / (s2 + a * a * wi.z * wi.z);
    float b  = wi.z > 0 ? k * wi.z : wi.z;

    float z = mad(1.0 - u.y, 1.0 + b, -b);
    float sinTheta = sqrt(saturate(1.0 - z * z));
    float3 o_std = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    // Rotate to frame centered on wi
    float3 T1, T2;
    BuildTangentFrame(float3(wi.x, wi.y, 0), T1, T2);
    return normalize(o_std.x * T1 + o_std.y * T2 + o_std.z * wi);
}
```

### 5.3 Benefits vs. Heitz 2018

| Property | Heitz 2018 | Dupuy & Benyoub 2023 |
|---------|------------|---------------------|
| Valid samples | All valid | All valid |
| Out-of-hemisphere samples | Some (rejected implicitly) | None (bounded) |
| Code complexity | Moderate | Simpler |
| Speedup (benchmarked) | baseline | ~25–45% faster for rough surfaces |
| Used in AMD SSSR | v1.0–1.4 (Heitz) | v1.5 (Boksansky bounded) |

---

## 6. Temporal Supersampling & Neighborhood Clamping (Karis 2014) {#6-temporal-karis}

### 6.1 Source

> **Karis, B.** "High Quality Temporal Supersampling."
> *Advances in Real-Time Rendering in Games*, SIGGRAPH 2014 Course.
> Unreal Engine 4 / Epic Games, 2014.
> Slides: https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf

Karis presented the TAA algorithm that shipped in Unreal Engine 4 (awarded the 2025
ACM SIGGRAPH Practitioner Award). Its neighborhood clamping and variance-clipping
algorithms are the mathematical foundation of all modern SSR temporal filters.

### 6.2 History Blending

The basic exponential moving average:

```hlsl
float alpha = 0.1;   // weight of current frame (history weight = 0.9)
float3 result = lerp(history_color, current_color, alpha);
```

`alpha = 0.1` means the effective sample count accumulates over ~1/alpha = 10 frames.
Lower alpha = more temporal stability = more ghosting on moving content.

### 6.3 Neighborhood AABB Clamping (min-max)

The problem: when the reflected content changes (object moves, light changes), the
history contains an outdated color. Solution: clamp the history sample to the **color
AABB** of the local 3×3 neighborhood of the current frame:

```hlsl
// Karis 2014 — AABB neighborhood clamping
float3 color_min = INF, color_max = -INF;
for (int dx = -1; dx <= 1; dx++)
for (int dy = -1; dy <= 1; dy++) {
    float3 s = SampleCurrent(uv + float2(dx, dy) * texel_size);
    color_min = min(color_min, s);
    color_max = max(color_max, s);
}

float3 clamped_history = clamp(history_color, color_min, color_max);
float3 result = lerp(clamped_history, current_color, alpha);
```

**YCoCg color space**: Karis recommended computing the AABB in YCoCg (a luma-chroma
decomposition) rather than RGB. YCoCg creates tighter AABBs, reducing the ghosting
artifact where the clamped color slides along RGB cube edges:

```hlsl
float3 RGBtoYCoCg(float3 rgb) {
    return float3(
        rgb.x * 0.25 + rgb.y * 0.5 + rgb.z * 0.25,  // Y  = luma
        rgb.x * 0.5  - rgb.z * 0.5,                   // Co = orange-teal chroma
        -rgb.x * 0.25 + rgb.y * 0.5 - rgb.z * 0.25   // Cg = green-magenta chroma
    );
}
float3 YCoCgtoRGB(float3 ycocg) {
    float tmp = ycocg.x - ycocg.z;
    return float3(tmp + ycocg.y, ycocg.x + ycocg.z, tmp - ycocg.y);
}
```

### 6.4 Variance Clipping (vs. AABB Clamping)

AABB clamping can produce boxy artifacts. Variance clipping builds a tighter bounding
volume using the statistical mean and standard deviation:

```hlsl
// Variance clipping — Karis 2014 / Salvi 2016 refinement
float3 m1 = 0, m2 = 0;
for (each of 9 neighbors) {
    m1 += s / 9.0;
    m2 += s * s / 9.0;
}
float3 mu    = m1;
float3 sigma = sqrt(abs(m2 - m1 * m1));

float gamma = 1.0;  // published default — ghosting/noise tradeoff
float3 lo = mu - gamma * sigma;
float3 hi = mu + gamma * sigma;

float3 clamped = clamp(history_color, lo, hi);
```

**Recommended**: use variance clipping in YCoCg — this is what AMD SSSR, Unity HDRP,
and NVIDIA NRD all implement for their SSR temporal passes.

---

## 7. SVGF — Spatiotemporal Variance-Guided Filtering (Schied 2017) {#7-svgf}

### 7.1 Source

> **Schied, C. et al.** "Spatiotemporal Variance-Guided Filtering: Real-Time
> Reconstruction for Path-Traced Global Illumination."
> *Proceedings of High Performance Graphics* (HPG), 2017.
> ACM DL: https://dl.acm.org/doi/10.1145/3105762.3105770
> NVIDIA Research: https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/

### 7.2 Full Algorithm

SVGF is a 3-stage filter designed for 1 sample-per-pixel (spp) noisy input.
While designed for path-traced GI, its design is directly applicable to 1-spp SSR:

```
SVGF Input:
  color_t    : float3  — noisy radiance this frame (1 spp)
  depth_t    : float   — primary surface depth
  normal_t   : float3  — primary surface normal
  albedo_t   : float3  — primary surface albedo (for demodulation)
  motion_t   : float2  — screen-space motion vector
  history_t  : float3  — accumulated color from previous frame

SVGF Stage 1: Temporal Accumulation
  alpha         = 0.1    (fixed in original; A-SVGF makes adaptive)
  reproject_uv  = uv + motion_t
  history       = SampleHistory(reproject_uv)

  history_m1  = lerp(history_color, color_t, alpha)        // E[x]
  history_m2  = lerp(history_m2, color_t * color_t, alpha) // E[x^2]
  variance_t  = max(0, history_m2 - history_m1 * history_m1)

SVGF Stage 2: Variance Estimation
  variance_spatial = ComputeVariance(color_t, 7x7_neighborhood)
  variance = max(variance_t, variance_spatial * (1 - history_length / 4))

SVGF Stage 3: À-Trous Wavelet Filter (5 levels)
  // Level k uses dilation factor 2^k (1, 2, 4, 8, 16 pixel offsets)
  // 5×5 sparse kernel: [1/16, 1/4, 3/8, 1/4, 1/16] separable weights

  for k in 0..4:
    dilation = 2^k
    for each output pixel p:
      result = 0, weight_sum = 0
      for each tap q in 5×5 sparse kernel (step dilation):
        w = w_depth(p, q) * w_normal(p, q) * w_lum(p, q)
        result     += color[q] * w
        weight_sum += w
      filtered[p] = result / weight_sum

Edge-stopping functions:
  w_depth(p,q)  = exp(-|depth_p - depth_q| / (sigma_z * max(|grad_depth_p|, eps)))
  sigma_z       = 1.0  (published default)

  w_normal(p,q) = max(0, dot(N_p, N_q)) ^ sigma_n
  sigma_n       = 128.0  (published default — very sharp normal edge stopping)

  w_lum(p,q)    = exp(-|lum(color_p) - lum(color_q)| / (sigma_l * sqrt(variance_p) + eps))
  sigma_l       = 4.0  (published default)
  // sqrt(variance_p) makes the blur radius adaptive: noisy → blur more
```

### 7.3 Published Constants

| Constant | Published Value | Role |
|---------|----------------|------|
| `alpha` (temporal) | 0.1 | Blend weight for new frame |
| `sigma_z` | 1.0 | Depth edge-stopping scale |
| `sigma_n` | 128.0 | Normal edge-stopping exponent |
| `sigma_l` | 4.0 | Luminance edge-stopping scale |
| À-trous levels | 5 | Wavelet hierarchy depth |
| À-trous kernel | `[1/16, 1/4, 3/8, 1/4, 1/16]` | Gaussian-approximating kernel |
| Variance bootstrap frames | 4 | Frames before pure temporal variance |

### 7.4 A-SVGF (Adaptive SVGF, Schied 2018)

> **Schied, C. et al.** "Gradient Estimation for Real-Time Adaptive Temporal Filtering."
> *Proceedings of the ACM on Computer Graphics and Interactive Techniques*, 2018.
> DOI: 10.1145/3233301

Extension replaces the fixed `alpha` with an adaptive rate derived from a
"gradient image" encoding the rate of luminance change across frames. Reduces
ghosting on dynamic content while maintaining stability on static regions.

### 7.5 SVGF Limitations for Reflections

SVGF's temporal accumulation uses **primary surface** motion vectors. For reflections:
- The reflected content's motion does not match the surface motion
- Solution A: **Virtual hit reprojection** (store hit distance, compute virtual world
  position, reproject through previous VP matrix) — best quality, used in AMD SSSR
- Solution B: **Discard history on disocclusion** — simpler but noisier

---

## 8. Neural Denoising for Reflections (Chaitanya 2017) {#8-neural-denoising}

### 8.1 Source

> **Chaitanya, C. R. A. et al.** "Interactive Reconstruction of Monte Carlo Image
> Sequences using a Recurrent Denoising Autoencoder."
> *ACM Transactions on Graphics* **36**(4), Article 98, 2017.
> ACM DL: https://dl.acm.org/doi/10.1145/3072959.3073601
> NVIDIA Research: https://research.nvidia.com/publication/2017-07_interactive-reconstruction

### 8.2 Architecture

The paper introduces the first **recurrent convolutional neural network (RCNN)**
for real-time path-traced image reconstruction, including reflections:

```
Input channels (per pixel):
  - Noisy RGB (1 spp rendered)
  - Albedo (demodulated from noisy signal)
  - Normals
  - Depth
  - Previous frame accumulated output (recurrent connection)

Architecture:
  - Encoder: 5 convolution layers with max-pooling
  - Recurrent layer: connects prev-frame encoder output to current encoder
  - Decoder: 5 deconvolution layers (skip connections from encoder)
  - Output: denoised RGB

Training:
  - Loss: L1 between denoised output and 128-spp reference
  - Training data: 10,000+ frames from Cornell Box + Sponza scenes
  - Inference time: ~0.5ms per frame (dedicated GPU inference path)
```

### 8.3 Key Innovation: Albedo Demodulation

The paper introduced **albedo demodulation** as preprocessing before denoising:

```
noisy_untextured = noisy_rgb / max(albedo, 0.01)   // demodulate
denoised_untextured = Network(noisy_untextured, normals, depth)
denoised_final = denoised_untextured * albedo       // re-modulate
```

This removes most texture complexity from the noisy signal, dramatically reducing
required network capacity. Every subsequent neural denoiser (NVIDIA OptiX Denoiser,
NRD, Optix AI Denoiser) inherited this approach.

### 8.4 Temporal Stability

The recurrent connections allow the network to maintain temporal stability without
an explicit motion vector reprojection step, learning temporal coherence from the
accumulated hidden state. This was the first demonstration of temporally stable
neural denoising at interactive rates (>10 fps).

---

*Continue in `ssr-academic-survey-part2.md`: Blue Noise, AMD SSSR pipeline,
Hybrid RT, NVIDIA NRD, Comparison Matrix, and BibTeX references.*
