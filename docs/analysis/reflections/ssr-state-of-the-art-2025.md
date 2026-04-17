<!-- doc-meta
title: Screen-Space Reflections — State of the Art 2025
subsystem: rendering/ssr
status: reference
date: 2026-04-05
-->

# Screen-Space Reflections — State of the Art 2025

A comprehensive technical analysis of modern SSR techniques, noise reduction strategies,
and denoising pipelines from AMD, NVIDIA, Unity, EA Frostbite, CryEngine, and Google Filament.

---

## 1. Historical Baseline — Where Classic SSR Stands

The classic SSR pipeline, canonicalized by Morgan McGuire and Mike Mara in
**"Efficient GPU Screen-Space Ray Tracing" (JCGT 2014)**, marches a 3D reflection ray across
the depth buffer using a perspective-correct DDA algorithm. This shipped in Assassin's Creed 4
and Killzone 4. The core limitations have been known since then:

| Problem | Cause | Classic Mitigation |
|---------|-------|--------------------|
| Step-aliasing / banding | Constant step size in world space | Smaller steps, binary refinement |
| Screen-edge cutoff | Ray exits view frustum | Fade-out at screen border |
| Missing off-screen objects | Not in depth buffer | Fallback to reflection probes |
| Single-ray noise for glossy surfaces | Under-sampling of GGX lobe | Blur or ignore roughness |
| Self-intersection | Incorrect depth comparison | Depth bias / thickness parameter |
| Temporal flickering | Different ray each frame on stochastic variants | No history = white noise |

Everything since 2015 is an answer to one or more of these problems.

---

## 2. The Foundational Algorithm Upgrade — Hi-Z Hierarchical Traversal

### 2.1 Standard DDA vs. Hi-Z

Standard DDA marches in uniform screen-space steps. Hi-Z traversal, described by
**Yasin Uludag in GPU Pro 5 (2014) "Hi-Z Screen-Space Cone-Traced Reflections"**,
walks a mip-mapped depth pyramid (Hi-Z):

1. Start at the coarsest mip whose texel safely encloses the current ray segment.
2. If the ray does not intersect that texel's depth range → advance by one full texel width.
3. If it does intersect → descend one mip level to test a smaller region.
4. Repeat until mip 0 (full resolution) → intersection confirmed.

**Result**: far fewer depth samples for long rays, especially in large open scenes.
AMD FidelityFX SSSR v1.5 uses this as its core traversal kernel, adjusting between
full-rate (mirror), half-rate, and quarter-rate traversal based on surface roughness.

### 2.2 Depth Pyramid Construction

The Hi-Z mip chain is built differently from a normal color mipmap:
- Each texel stores the **maximum** depth of its 2×2 child region (in reversed-Z, that is the
  **minimum** raw depth value — closest to camera).
- This ensures a ray cannot pass through the coarse level without the detailed level also
  containing a valid intersection.

---

## 3. AMD FidelityFX SSSR (Stochastic Screen-Space Reflections)

**Source**: `github.com/GPUOpen-Effects/FidelityFX-SSSR`
**Version**: 1.5 (integrated into FidelityFX SDK)
**License**: MIT

SSSR is the most fully documented open-source production-grade SSR implementation available.
Its architecture is the best public reference for a complete modern SSR pipeline.

### 3.1 Full Pass Pipeline

```
DepthDownsample → ClassifyTiles → PrepareBlueNoise → (indirect) IntersectRays
    → DNSR_Reproject → DNSR_Prefilter → DNSR_TemporalAccumulate → Resolve
```

| Pass | GPU Time (approx 1080p) | Purpose |
|------|------------------------|---------|
| DepthDownsample | ~0.05ms | Build Hi-Z mip pyramid |
| ClassifyTiles | ~0.12ms | Sort 8×8 tiles by roughness; fill indirect args |
| PrepareBlueNoise | <0.02ms | Advance STBN frame index |
| IntersectRays (indirect) | ~0.88ms | Hi-Z ray march, GGX VNDF sampling |
| DNSR_Reproject | ~0.22ms | Virtual-hit reprojection into previous frame |
| DNSR_Prefilter | ~0.34ms | Bilateral spatial blur |
| DNSR_TemporalAccumulate | ~0.18ms | Variance-clamped blend with history |
| Resolve | ~0.10ms | Composite into scene color |

Total ~1.9ms @ 1080p on RDNA2 for typical indoor scene.

### 3.2 Tile Classification

The 8×8 tile classifier reads the roughness G-Buffer and categorizes each tile:
- **Roughness > threshold (default ~0.7)**: no rays needed — evaluate with fallback
  environment map only. Mark as `TILE_SKIP`.
- **Mixed tiles**: some pixels need rays, some don't. Mark with ray count per pixel.
- **Mirror tiles (roughness ≈ 0)**: full-rate traversal, 1 ray per pixel.
- **Glossy tiles (roughness 0.3–0.7)**: half-rate or quarter-rate traversal.

`ExecuteIndirect` dispatches only the tiles that need ray marching, skipping flat/sky regions
entirely. This is a significant optimization over full-screen dispatches.

### 3.3 Blue Noise Ray Generation

SSSR samples the GGX VNDF (Visible Normal Distribution Function, Heitz 2018) to generate
reflection ray directions. The random seed comes from a **Spatiotemporal Blue Noise (STBN)
texture** that is advanced each frame.

```hlsl
// Conceptual SSSR ray generation
float2 u = SampleBlueNoise(pixel, frameIndex); // 64×64×64 STBN volume
float3 H = SampleGGX_VNDF(N, V, roughness, u); // importance-sampled half-vector
float3 R = reflect(-V, H);                      // reflection direction
```

**Why VNDF over NDF sampling?**
- NDF sampling (sampling the microfacet normal distribution directly) can generate half-vectors
  behind the surface, wasting samples. VNDF conditions on the visible hemisphere — every sample
  is valid and contributes energy. For glossy surfaces, VNDF reduces variance by ~2× vs NDF.

**Bounded VNDF (GPUOpen 2023)**:
AMD published "Bounded VNDF Sampling for Smith-GGX Reflections" (HPG 2023), which further
clips the sampling domain to the visible hemisphere of the shading point, giving another ~45%
speedup in the sampling routine for rough surfaces. This is used in SSSR v1.5.

### 3.4 Hi-Z Intersection

The ray marches through the depth pyramid. Key parameters:

| Parameter | Typical Value | Effect |
|-----------|--------------|--------|
| `depth_buffer_thickness` | 0.005–0.05 | Prevents thin geometry (grass, hair) from occluding too much |
| `roughness_threshold` | 0.7 | Cutoff above which no rays are cast |
| `max_traversal_intersections` | 128 | Cap on Hi-Z steps per ray |
| `min_traversal_occupancy` | 0.5 (50%) | Warp-level early-out threshold |

The `depth_buffer_thickness` is the most content-sensitive parameter in the whole pipeline —
tuning it per-scene is often necessary.

### 3.5 DNSR Reprojection — Virtual Hit Position

Standard TAA reprojection fails for reflections because the motion vector at the reflection
surface point describes how the *surface* moved, not how the *reflected object* moved.

SSSR's reprojection stores the **hit distance** (ray travel length) per pixel, then:

```
P_hit = camera_position + R * hit_distance       // world-space hit point
UV_prev = ProjectToPreviousFrame(P_hit)           // reproject through prev VP matrix
history = SampleHistoryBuffer(UV_prev)            // bilinear sample of previous result
```

This is called "virtual position reprojection" or "parallax-corrected reprojection",
first described in Stachowiak's Frostbite paper (SIGGRAPH 2015).

When no valid history exists (disocclusion, first frame), the blend weight is set to 0
(full current frame) and builds up to ~0.98 over the subsequent ~50 frames.

### 3.6 Spatial Prefilter (DNSR_Prefilter)

A bilateral filter across 8-pixel neighborhoods. Weights combine:
- **Depth similarity**: `exp(-|z_a - z_b| / sigma_z)` — rejects samples across depth edges
- **Normal similarity**: `max(0, dot(N_a, N_b))^k` — rejects samples across normal edges
- **Roughness similarity**: `exp(-|r_a - r_b| / sigma_r)` — avoids cross-blending between
  glossy and mirror regions

**Not a separable Gaussian** — the bilateral weights prevent blurring reflections onto
non-reflective surfaces. AMD specifically warns that naive separable Gaussian will bleed
reflections across depth edges.

### 3.7 Temporal Accumulate — Variance Clamping

The temporal pass blends `current_filtered` with `history_reprojected`:

```
blend_factor = lerp(1.0, 0.05, saturate(valid_samples / 50))  // adaptive
```

To prevent ghosting when the reflected scene changes, the history sample is **variance
clamped** into the local neighborhood distribution of the current frame:

```
mean    = LocalMean(current_filtered, 3x3_neighborhood)
variance = LocalVariance(current_filtered, 3x3_neighborhood)
sigma   = sqrt(variance)
history_clamped = clamp(history, mean - N*sigma, mean + N*sigma)  // N ≈ 1.5
```

This is the same pattern as SVGF's moment-based clamping. The magic constant N controls
the ghosting/noise tradeoff: smaller N → more ghosting rejection but more temporal noise.

---

## 4. EA Frostbite — Stochastic Screen-Space Reflections (SIGGRAPH 2015)

**Authors**: Tomasz Stachowiak
**First shipped**: Mirror's Edge Catalyst, Need for Speed (2015)
**Reference**: EA Frostbite blog post + SIGGRAPH 2015 "Advances in Real-Time Rendering"

The Frostbite paper is the **conceptual origin** of all modern stochastic SSR. Key contributions:

### 4.1 Half-Resolution Tracing

Frostbite introduced tracing at **half resolution** as best practice:
- Reduces ray intersection cost by 4× (quarter the pixels, same algorithmic complexity)
- The per-pixel blue-noise jitter provides different ray directions across adjacent pixels,
  so the half-res result contains high-frequency detail that the denoiser can reconstruct
- Full-resolution upsampling via **bilateral upsample** using the full-res depth buffer

Half-res tracing remains the standard approach. Even in 2025, SSSR and Unity HDRP both
offer half-res tracing as the performance tier.

### 4.2 Importance-Sampled Reflection Cone

Instead of a single mirror ray, Frostbite samples the GGX NDF (later improved to VNDF) to
generate a jittered ray per pixel. The jitter radius scales with roughness:
- **roughness = 0**: perfect mirror, no jitter
- **roughness = 0.3**: moderate cone, noticeable stochastic noise
- **roughness = 0.7**: wide cone, very noisy without denoiser

The spatial filter exploits the fact that adjacent pixels sample **different parts of the same
lobe**, performing a Monte Carlo integration by spatial reuse.

### 4.3 Parallax-Corrected Motion Vectors

Stachowiak identified that standard TAA motion vectors move the reflection surface but not
the reflected content. He introduced storing the **hit distance** to compute the virtual world
position of the reflected point for reprojection. This innovation appears in every modern SSR
denoiser including SSSR and Unity HDRP.

---

## 5. NVIDIA SVGF Applied to Reflections

**Paper**: Schied et al., "Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction
for Path-Traced Global Illumination", NVIDIA / KIT, HPG 2017.

SVGF was designed for path-traced GI denoising, but the same framework applies directly to
SSR denoising. It is the mathematical foundation underneath SSSR's DNSR passes.

### 5.1 Core Algorithm

```
Input:  1 spp noisy radiance + depth + normals (+ motion vectors) per frame
Output: Temporally stable denoised radiance

1. Temporal Accumulation:
   - Reproject using motion vectors (SSR: use virtual hit reprojection)
   - Accumulate weighted history: E[x_t] = alpha * E[x_{t-1}] + (1-alpha) * x_t
   - Accumulate squared history for variance: E[x_t^2]
   - Variance estimate: Var = E[x^2] - E[x]^2

2. Spatial Atrous Wavelet Filter (à-trous):
   - 5-level hierarchical 5×5 filter with holes (dilation 1, 2, 4, 8, 16)
   - Bilateral weights from depth, normals, luminance variance
   - Large radius filter possible with sparse sampling (O(log n) passes)
   - Guided by variance: high variance → blur more, low variance → preserve edges

3. Per-pass weight function:
   w = w_depth * w_normal * w_luminance
   w_depth   = exp(-|z_a - z_b| / (sigma_z * |grad_z| + eps))
   w_normal  = max(0, dot(n_a, n_b))^sigma_n
   w_lum     = exp(-|lum_a - lum_b| / (sigma_l * sqrt(Var) + eps))
```

The key insight is that `sqrt(Var)` in the luminance weight adapts the blur radius automatically:
- High variance (noisy) → large blur radius
- Low variance (converged) → sharp edges preserved

### 5.2 SVGF Limitations for Reflections

SVGF's temporal accumulation uses **primary surface** motion vectors. For reflections this
fails: the reflected content's motion does not match the surface motion. Two approaches exist:

1. **Virtual hit reprojection** (SSSR approach): store hit distance, compute virtual world
   position, reproject through previous VP. Best quality, higher memory cost.
2. **Discard history on disocclusion** (simpler): detect when the reprojected sample comes
   from a different surface and reset alpha to 1.0 (no history). Cheaper but noisier.

### 5.3 A-SVGF (Adaptive SVGF)

A-SVGF (Schied 2018) extends the original with a **Moment Buffer** encoding luminance
variance and motion derivatives. This improves handling of:
- Dynamic lights (light that moves rapidly changes the reflection content each frame)
- Glossy mirrors with moving objects
- High-frequency specular (sparkle effects)

---

## 6. Unity HDRP — SSR Implementation

### 6.1 Two Algorithms

Unity HDRP offers two distinct SSR modes:

**Approximation (default, real-time)**:
- Standard DDA ray march in screen space
- Temporal accumulation via TAA-style history with `alpha ≈ 0.1`
- Simple bilateral spatial filter
- Works on transparent objects (mandatory mode for transparents)

**PBR Accumulation (high quality)**:
- Multiple samples per pixel accumulated over many frames
- Designed for final-frame screenshots and cutscenes
- Not real-time (converges over ~50–100 frames)
- Provides ground-truth SSR with full BRDF importance sampling

**Key distinction**: PBR Accumulation is the Unity equivalent of a path tracer reference;
Approximation is the real-time SSR meant for gameplay.

### 6.2 Tracing Methods in HDRP

HDRP SSR supports three tracing configurations:
- **Ray Marching only**: pure screen-space DDA, no RT hardware
- **Ray Tracing only**: hardware BVH traversal for off-screen geometry
- **Mixed**: ray march first (cheap, screen-space), fall back to HW RT for misses

The Mixed mode is the best quality/performance balance available on RT-capable hardware.
This is analogous to AMD's "Hybrid Reflections" sample which combines SSSR + DXR.

### 6.3 Key Parameters

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| `Min Smoothness` | 0–1 | 0.9 | Roughness cutoff (smoothness = 1 - roughness) |
| `Smoothness Fade Start` | 0–1 | 0.9 | Begin fade to fallback above this roughness |
| `Max Ray Length` | 0–∞ | 100m | Stop ray march after this world-space distance |
| `Object Thickness` | 0–∞ | 0.01m | Depth test thickness (same as SSSR's thickness param) |
| `Accumulation Factor` | 0–1 | 0.95 | TAA blend weight (history retention) |
| `Screen Weight Distance` | 0–1 | 0.1 | Fade width at screen edge |

### 6.4 Limitations HDRP Acknowledges

- Animated surfaces with vertex animation are not reflected unless using RT tracing
- Particles require the `BeforeRefraction` pass to appear in reflections
- SSR Transparent always uses Approximation, never PBR Accumulation

---

## 7. CryEngine — SSR Heritage

**Reference**: Tiago Sousa, "Secrets of CryENGINE 3 Graphics Technology", SIGGRAPH 2011.

CryEngine's original SSR (first GDC demo 2009, documented at SIGGRAPH 2011) established
the pattern that became the industry standard:

1. Ray march through depth buffer in screen space
2. Binary refinement at intersection (bisect the last two steps)
3. Fresnel-based fade: reflections fade as viewing angle approaches grazing
4. Screen-edge fade: reflections fade as ray approaches screen border
5. Reflection probe fallback for missed rays

CryEngine 5 and Lumberyard both inherit this design. The implementation in Lumberyard
explicitly supports all surface types, with the `Thickness` parameter to handle thin geometry.

**Neon Noir (2019)**: CryEngine demonstrated real-time mesh ray-traced reflections without
hardware RT by using the CPU-side BVH with multi-threaded intersection queries. Impressive but
impractical; superseded by DXR.

---

## 8. Google Filament — SSR

**Source**: `github.com/google/filament`

Filament implements a straightforward screen-space DDA ray march optimized for mobile and
mid-range desktop. It is not a stochastic system — there is no denoiser.

### 8.1 API Parameters

```cpp
ScreenSpaceReflectionsOptions {
    float thickness  = 0.1f;     // geometry thickness for depth test
    float bias       = 0.01f;    // depth bias to prevent self-intersection
    float maxDistance = 3.0f;    // max world-space ray distance
    float stride     = 2.0f;     // pixel step size (larger = faster, lower quality)
}
```

Filament's SSR is deliberately simple — the engine targets a range of devices including
mobile, so no denoising pipeline is present. The roughness cutoff is handled by disabling
SSR on materials above a certain roughness threshold (material-level flag).

### 8.2 Design Philosophy

Filament's approach documents the **minimum viable SSR** for a production PBR renderer:
constant-stride DDA march + Fresnel weighting + screen-fade. This is the correct baseline
for engines that cannot afford a full SSSR-style denoising pipeline.

---

## 9. Blue Noise for SSR — Why It Matters

### 9.1 White Noise vs. Blue Noise

When casting a single jittered reflection ray per pixel, the noise pattern has a critical
perceptual impact:

- **White noise**: equal energy at all frequencies. Looks like TV static — the human visual
  system cannot integrate it well. Temporal filters struggle because adjacent frames have
  uncorrelated noise at the same frequency.
- **Blue noise**: low-frequency energy suppressed, high-frequency only. Looks like film grain.
  The human visual system filters out high-frequency noise much more easily. Temporal filters
  converge faster because the signal is more structured.

### 9.2 Heitz 2019 — Distributing Monte Carlo Errors as Blue Noise

**Paper**: Eric Heitz, Laurent Belcour, Victor Ostromoukhov, David Coeurjolly, Jean-Claude
Iehl, "A Low-Discrepancy Sampler that Distributes Monte Carlo Errors as a Blue Noise in
Screen Space", EGSR 2019.

The key technique: instead of generating independent random seeds per pixel per frame,
**permute the pixel seeds between frames** such that the temporal sequence of errors forms
a blue-noise pattern. The algorithm:

1. Render frame N with seeds s_0, s_1, ..., s_N (stored in a seed buffer)
2. After rendering, look at the neighborhood of each pixel
3. Swap seeds between pixel pairs to reduce low-frequency error correlation
4. Use the permuted seeds for frame N+1

Result: errors appear as blue noise both spatially (within a frame) and temporally (across
frames). The temporal denoiser (TAA/SVGF) can integrate the blue-noise signal faster.
Implemented in Unity for SSR.

### 9.3 STBN — Spatiotemporal Blue Noise Masks (NVIDIA 2021/2022)

**Paper**: Alan Wolfe, Nathan Morrical et al., "Scalar Spatiotemporal Blue Noise Masks",
arXiv:2112.09629, SIGGRAPH 2022.
**Code**: `github.com/NVIDIA-RTX/STBN`

STBN pre-bakes a 64×64×64 blue-noise **volume texture** where:
- XY dimensions are spatially blue-noise
- Z dimension (frame index) is **also** blue-noise

Key property: both `STBN[x,y,frame]` (spatial) and `STBN[x,y,0..N]` (temporal) exhibit
blue-noise frequency spectra. Under TAA, STBN converges measurably faster than white noise
or spatially-only blue noise.

AMD FidelityFX SSSR uses a 128×128×64 STBN-style texture for its ray generation seeds.
The texture is shipped as part of the SSSR SDK.

**Implementation in a shader**:
```hlsl
// Advance frame: wrap z dimension by frame index
float2 u = STBN_Texture.Load(int3(pixel.x & 127, pixel.y & 127, frameIdx & 63)).rg;
float3 H = SampleGGX_VNDF(N, V, roughness, u);
```

### 9.4 Practical Gains

For a 1-spp SSR system on a rough surface (roughness = 0.3):
- White noise: TAA converges to stable image in ~80 frames
- Spatially blue noise (IGN / screen-space blue noise): ~50 frames
- STBN (temporal blue noise): ~30 frames

Under camera or object motion, STBN's temporal coherence also reduces the visible noise
during disocclusion events.

---

## 10. Half-Resolution Tracing + Bilateral Upsample — Is It Still Best Practice?

### 10.1 The Standard Approach

Half-resolution tracing + full-resolution bilateral upsample is still the performance-tier
standard in 2025. Used by:
- Frostbite (2015, origin)
- Unity HDRP (optional half-res mode)
- Most mobile SSR implementations

### 10.2 Bilateral Upsample Algorithm

Standard depth-aware bilateral upsample from half-res to full-res:

```hlsl
float4 BilateralUpsampleSSR(float2 uv, float fullResDepth)
{
    // Sample the 4 half-res neighbors (bilinear quad)
    float2 halfUV = uv * 0.5;
    float4 s[4]; // SSR radiance at 4 half-res texels
    float d[4];  // depth at 4 half-res texels

    float w[4];
    for (int i = 0; i < 4; i++) {
        float depthDiff = abs(d[i] - fullResDepth);
        w[i] = exp(-depthDiff / sigma_depth);  // depth similarity weight
        // Optionally: multiply by normal similarity
    }

    // Renormalize weights (sum to 1.0)
    float sumW = w[0] + w[1] + w[2] + w[3];
    if (sumW < 0.001) sumW = 1.0; // fallback: equal weights

    return (w[0]*s[0] + w[1]*s[1] + w[2]*s[2] + w[3]*s[3]) / sumW;
}
```

**Critical implementation note**: the half-resolution depth must use **max (closest-to-camera)**
downsampling, not average. A min-depth downsample ensures rays don't pass through geometry
that the coarser depth doesn't represent.

### 10.3 When Half-Res Breaks Down

Half-res fails visibly in two scenarios:
1. **Thin geometry** (grass, fences, hair): depth discontinuities at every pixel cause the
   bilateral upsample to have no good reference → aliased edges
2. **Tight reflective corridors**: adjacent half-res pixels sample very different geometry,
   and upsampling introduces structured artifacts

In these cases, full-resolution tracing (at lower quality) or a screen-space neighborhood
reuse strategy (RESTIR-style spatial reuse without actual RIS) produces better results.

### 10.4 Alternative: Checkerboard Tracing

Trace every other pixel in a checkerboard pattern (same cost as half-res, different spatial
distribution). Reconstruct with the neighboring traced pixel as a bilateral fallback.
Checkerboard tracing avoids the systematic aliasing of half-res when combined with a good
temporal filter. Used in some console implementations.

---

## 11. Comprehensive Noise Cause & Solution Table

| Noise Type | Root Cause | Frequency | Solution |
|-----------|------------|-----------|----------|
| **Step banding** | Constant DDA step size | Low-freq | Hi-Z traversal, binary refinement |
| **Glossy stochastic noise** | 1 ray/pixel GGX sampling | High-freq | STBN seeds, spatial bilateral filter |
| **Temporal ghosting** | Old reflected content in history | Low-freq / frame-persistent | Variance clamping, correct hit reprojection |
| **Temporal shimmer (miss-reprojection)** | Using surface motion vectors for reflection | High-freq temporal | Virtual-position reprojection (store hit distance) |
| **Disocclusion flash** | History invalid after camera move | Spatial pattern | Blend weight reset on disocclusion detection |
| **Screen-edge hard cutoff** | Ray exits NDC cube | Edge-aligned | Smooth fade-out over last 15% of screen border |
| **Self-intersection acne** | Ray origin inside depth surface | Specular highlight at origin | Depth bias, normal-offset ray origin |
| **Thin geometry through-seeing** | No depth thickness → background visible through grass | Splattering artifacts | `depth_buffer_thickness` parameter |
| **Wrong reflection from rough surfaces** | High variance in wide GGX cone not integrated | Low-frequency blur | High-sample accumulation or lower roughness cutoff |
| **Reflection probe seam** | Sharp transition from SSR to fallback probe | Edge/silhouette | Fade SSR contribution by `hit_confidence` (NDF lobe vs. actual hit) |
| **Metallic over-brightness** | Not factoring Fresnel properly | Global | Apply `F(NdotV) * reflectance` correctly at composite step |

---

## 12. AMD FidelityFX Hybrid Reflections

AMD's Hybrid Reflections sample (2022) demonstrates the layered reflection strategy that
represents current best practice on DXR-capable hardware:

```
Layer 1: Screen-space Hi-Z traversal (SSSR)
    → Hit found: use screen-space color, weight = 1.0
    → Miss: proceed to Layer 2

Layer 2: DXR hardware BVH traversal  
    → Hit found: evaluate material at hit + shade with cached lighting
    → Miss: proceed to Layer 3

Layer 3: Fallback IBL (environment probe / sky IBL)
    → Always available, lowest quality
```

The three layers are blended using **hit confidence**: a per-ray scalar indicating how
well the screen-space result matches the actual surface normal and BRDF. Rays that hit a
surface at a grazing angle get low confidence (likely a self-intersection error) and are
downweighted toward the HW-RT result.

The denoiser (SSSR's DNSR) operates on the **composited** single-ray result — it does not
need to know which layer produced each sample.

---

## 13. UzEngine Implementation Recommendations

Based on the analysis above, the recommended SSR implementation for UzEngine prioritizes
quality and correctness, following the SSSR architecture as the reference:

### 13.1 Pipeline Passes (in order)

```
1. DepthDownsample_CS        — build Hi-Z pyramid (max-depth, N mip levels)
2. ClassifyTiles_CS           — 8×8 tiles, sort by roughness, write indirect args
3. IntersectReflections_CS   — Hi-Z ray march, GGX VNDF + STBN seeds, half-res output
4. ReprojectHistory_CS        — virtual hit reprojection into prev frame color buffer
5. SpatialFilter_CS           — bilateral filter, 8px radius, depth+normal+roughness weights
6. TemporalAccumulate_CS      — variance-clamped history blend, adaptive factor
7. Composite                  — apply to scene color with Fresnel weighting + fallback probe
```

### 13.2 Key Constants to Match Published References

| Constant | Value | Source |
|----------|-------|--------|
| Tile size | 8×8 pixels | SSSR v1.5 |
| Roughness cutoff | 0.7 | SSSR v1.5 (configurable) |
| Max Hi-Z steps | 128 | SSSR v1.5 |
| STBN texture size | 128×128×64 | SSSR v1.5 |
| Temporal blend factor | 0.95 (stable) / 1.0 (disocclusion) | SVGF / SSSR |
| Variance clamp N | 1.5 sigma | SVGF |
| Depth sigma | 1.0 * |grad_z| | SVGF |
| Normal sigma | 128 (exponent) | SVGF |
| Min depth sample (Hi-Z) | max-Z per cell | Uludag GPU Pro 5 |

### 13.3 Inputs Required from GBuffer

| Input | Format | Source Pass |
|-------|--------|-------------|
| Depth (full-res) | R32_FLOAT | GBuffer |
| World normals | RGB10A2_UNORM (octahedral) | GBuffer |
| Roughness | R8_UNORM | GBuffer |
| Metallic | R8_UNORM | GBuffer (for Fresnel F0) |
| Previous frame color | R11G11B10_FLOAT | Previous TAA/composite |
| Previous frame depth | R32_FLOAT | Previous GBuffer |
| Previous VP matrix | CPU constant | Per-frame CB |
| STBN volume | R8G8_UNORM 3D | Static asset |

### 13.4 Fallback Strategy

When SSR ray misses (exits screen, exceeds max steps), blend to:
1. Scene's IBL prefiltered environment map sampled at `roughness` mip level
2. Weight blend using `hit_confidence = 1 - saturate(miss_distance / fallback_radius)`

This matches the approach in SSSR, Unity HDRP, and Frostbite.

---

## 14. Summary Comparison Matrix

| Feature | Classic DDA | Frostbite 2015 | AMD SSSR 1.5 | Unity HDRP | Filament |
|---------|-------------|----------------|--------------|------------|---------|
| Traversal | Linear DDA | Linear DDA half-res | Hi-Z pyramid | DDA or DXR | Linear DDA |
| Ray sampling | Mirror only | GGX NDF stochastic | GGX VNDF bounded | GGX NDF/VNDF | Mirror only |
| Noise source | VNDF | White noise | STBN spatiotemporal | White/STBN | None (mirror) |
| Spatial filter | None | Bilateral | Bilateral (8px) | Bilateral | None |
| Temporal filter | None | TAA-style | SVGF-style variance-clamp | TAA/SVGF | None |
| Reprojection | Surface MV | Virtual hit | Virtual hit | Virtual hit | Surface MV |
| Half-res | No | Yes | Optional | Optional | No |
| Roughness cutoff | Manual | Yes | 0.7 default | Smoothness-based | Manual |
| Fallback IBL | Optional | Yes | Yes | Yes | Yes |
| Open source | N/A | No | Yes (MIT) | Source-available | Yes (Apache 2) |
| DXR hybrid | No | No | Yes (sample) | Yes (mixed mode) | No |

---

## References

- McGuire, Mara. "Efficient GPU Screen-Space Ray Tracing." JCGT 3(4), 2014.
- Uludag. "Hi-Z Screen-Space Cone-Traced Reflections." GPU Pro 5, 2014.
- Stachowiak. "Stochastic Screen-Space Reflections." SIGGRAPH Advances, EA Frostbite, 2015.
- Schied et al. "Spatiotemporal Variance-Guided Filtering." HPG 2017.
- Heitz et al. "Sampling the GGX Distribution of Visible Normals." JCGT 7(4), 2018.
- Heitz, Belcour et al. "A Low-Discrepancy Sampler That Distributes Monte Carlo Errors as Blue Noise." EGSR 2019.
- Wolfe et al. "Scalar Spatiotemporal Blue Noise Masks." arXiv:2112.09629, SIGGRAPH 2022.
- Boksansky et al. "Bounded VNDF Sampling for Smith-GGX Reflections." HPG 2023, AMD GPUOpen.
- AMD GPUOpen. "FidelityFX Stochastic Screen-Space Reflections 1.5." https://gpuopen.com/fidelityfx-sssr/
- AMD GPUOpen. "FidelityFX Hybrid Reflections Sample." https://gpuopen.com/fidelityfx-hybrid-reflections/
- Unity Technologies. "Screen Space Reflection — HDRP Documentation." https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Override-Screen-Space-Reflection.html
- Sousa. "Secrets of CryENGINE 3 Graphics Technology." SIGGRAPH 2011.
- Google Filament. "Physically Based Rendering in Filament." https://google.github.io/filament/Filament.md.html
- NVIDIA RTX. "STBN source code." https://github.com/NVIDIA-RTX/STBN
- Interplay of Light. "Notes on screenspace reflections with FidelityFX SSSR." 2022. https://interplayoflight.wordpress.com/2022/09/28/notes-on-screenspace-reflections-with-fidelityfx-sssr/
