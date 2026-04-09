<!-- doc-meta
title: Screen-Space Reflections — Academic Survey Part 2: Blue Noise, Production Pipelines, Hybrid RT
subsystem: rendering/ssr
status: reference
date: 2026-04-05
-->

# Screen-Space Reflections — Academic Survey Part 2: Blue Noise, Production Pipelines, Hybrid RT

Continuation of the SSR academic survey. Covers blue-noise sampling techniques,
AMD FidelityFX SSSR production pipeline, hybrid ray-tracing integration, NVIDIA NRD,
the full algorithm comparison matrix, and complete BibTeX references.

**Part 1** (`ssr-academic-survey-part1.md`) covers DDA traversal, Hi-Z, GGX VNDF,
Karis temporal, and SVGF.

---

## Table of Contents

9. [Blue Noise Sampling (Heitz 2019)](#9-blue-noise-heitz)
10. [Screen-Space Blue Noise via Pixel Ordering (Ahmed 2020)](#10-ahmed-blue-noise)
11. [Spatiotemporal Blue Noise Masks — STBN (Wolfe 2022)](#11-stbn)
12. [AMD FidelityFX SSSR — Production Pipeline (2021–2024)](#12-fidelityfx-sssr)
13. [Hybrid SSR + Ray Tracing](#13-hybrid-ssr-rt)
14. [NVIDIA NRD — Real-Time Denoiser](#14-nrd)
15. [Algorithm Comparison Matrix](#15-comparison)
16. [Quick Reference — Constants Cheat Sheet](#16-constants)
17. [BibTeX References](#17-bibtex)

---

## 9. Blue Noise Sampling (Heitz 2019) {#9-blue-noise-heitz}

### 9.1 Sources

> **Heitz, E., Belcour, L., Ostromoukhov, V., Coeurjolly, D., and Iehl, J.-C.**
> "A Low-Discrepancy Sampler that Distributes Monte Carlo Errors as a Blue Noise
> in Screen Space."
> *ACM SIGGRAPH 2019 Talks*, 2019.
> ACM DL: https://dl.acm.org/doi/10.1145/3306307.3328191

> Extended journal version:
> **Heitz, E. and Belcour, L.** "Distributing Monte Carlo Errors as a Blue Noise
> in Screen Space by Permuting Pixel Seeds Between Frames."
> *Computer Graphics Forum* **38**(4), 2019. DOI: 10.1111/cgf.13778

### 9.2 Core Idea

Per-pixel Monte Carlo estimators independently produce errors with **white noise**
spatial distribution. The human visual system perceives white noise (low-frequency
component present) more negatively than blue noise (only high-frequency components).

The Heitz 2019 sampler achieves blue-noise error distribution by:

1. Each pixel uses an **Owen-scrambled Sobol sequence** (low-discrepancy, fast
   convergence rate)
2. The seeds for each pixel's Sobol sequence are **permuted between frames** to
   minimize low-frequency correlation across neighboring pixels

```
Algorithm (per-frame update):
  Input:  seed_buffer[W×H]  — per-pixel integer seeds for Sobol generator
  Output: permuted seeds for next frame

  For each pixel p:
    // Compute energy = sum of squared pairwise correlations in neighborhood
    E = sum_{q in n, q != p} Correlation(seed_buffer[p], seed_buffer[q])

    q = RandomNeighbor(p, radius=8)
    E_new = ComputeEnergy_after_swap(p, q)
    if (E_new < E):
        swap(seed_buffer[p], seed_buffer[q])

// Sample using the seed:
float2 u = OwenScrambledSobol(dimension=0..1, seed=seed_buffer[pixel])
```

Implementation in practice: the full per-frame permutation is too expensive for
real-time. AMD and NVIDIA precompute the permutation schedule as a **volume texture**
(see Section 11 — STBN).

### 9.3 Implementation: Two XOR Operations

The paper describes a lightweight implementation:

```hlsl
// Heitz 2019 — Fast blue noise sampler
uint sobol_index = SobolIndex(pixel_idx, frame_idx);  // deterministic
uint seed        = BlueNoiseSeedTexture.Load(pixel.xy & 127).r;
uint scrambled   = sobol_index ^ seed;                // XOR scramble
float u = float(scrambled) / float(0xFFFFFFFF);       // normalize to [0,1)
```

---

## 10. Screen-Space Blue Noise via Pixel Ordering (Ahmed 2020) {#10-ahmed-blue-noise}

### 10.1 Source

> **Ahmed, A. G. M. and Wonka, P.** "Screen-Space Blue-Noise Diffusion of
> Monte Carlo Sampling Error via Hierarchical Ordering of Pixels."
> *ACM Transactions on Graphics* **39**(6), Article 244, 2020.
> ACM DL: https://dl.acm.org/doi/10.1145/3414685.3417881
> Project page: http://abdallagafar.com/publications/zsampler/

### 10.2 Algorithm

Instead of per-pixel seed permutation, Ahmed & Wonka achieve blue-noise error
distribution by **ordering pixels along a space-filling curve** and assigning
successive sub-sequences of a single low-discrepancy sequence:

```
// Ahmed & Wonka 2020 — Z-sampler algorithm
// 1. Order pixels along Morton Z-curve (space-filling, preserves locality)
// 2. Scramble the Z-order to induce isotropy (prevents axis-aligned patterns)
// 3. Assign pixel p (at position i in the ordering) the sub-sequence [i*N, (i+1)*N)
//    of a global Sobol-Owen sequence

// Runtime sample for pixel p:
uint z_order_idx = MortonCode(pixel.x, pixel.y)         // 2D → 1D space-filling
uint scrambled   = z_order_idx ^ ScrambleLUT(pixel.xy)   // isotropic scramble
float u = OwenSobol(scrambled, sampleIndex)              // low-discrepancy
```

### 10.3 Advantages over Heitz 2019

- Scalable to arbitrary resolution (no texture size constraint)
- Supports progressive and adaptive sampling natively
- No per-frame permutation update needed (deterministic from pixel position)
- Better high-dimensional uniformity (single sequence rather than per-pixel seeding)

Both approaches produce visually equivalent results — STBN (based on Heitz 2019) is
more practical for real-time due to its simple 3D texture lookup.

---

## 11. Spatiotemporal Blue Noise Masks — STBN (Wolfe 2022) {#11-stbn}

### 11.1 Sources

> **Wolfe, A., Morrical, N. et al.** "Scalar Spatiotemporal Blue Noise Masks."
> *Eurographics Symposium on Rendering* (EGSR), 2022.
> arXiv: 2112.09629. DOI: 10.2312/sr.20221161
> PDF: https://cseweb.ucsd.edu/~ravir/stbn.pdf
> Code: https://github.com/NVIDIA-RTX/STBN

### 11.2 Algorithm

STBN pre-bakes a 3D texture `STBN[x, y, t]` where:
- The spatial slice `STBN[·, ·, t]` exhibits blue-noise spectrum for each frame `t`
- The temporal sequence `STBN[x, y, ·]` also exhibits blue-noise spectrum per pixel

```
// STBN generation algorithm (offline, GPU compute)
// Based on "void and cluster" algorithm extended to 3D

void GenerateSTBN(int W, int H, int T):
  stbn[W][H][T] = RandomInit()

  // Iterate: swap random pixel seeds to minimize low-frequency energy
  // Energy function considers both spatial neighbors (same frame)
  // AND temporal neighbors (same pixel, different frame)

  for iteration in 0..MAX_ITERS:
    p = RandomPixel3D()
    q = RandomPixel3D()
    if E_after_swap(p, q) < E_before_swap(p, q):
      swap(stbn[p], stbn[q])

  // Result: stbn[x][y][t] is blue-noise in XY and in T independently
```

### 11.3 Runtime Usage in SSR

```hlsl
// STBN usage in AMD FidelityFX SSSR v1.5
Texture3D<float2> g_stbn_128_64;  // 128×128×64, two channels (u1, u2 for GGX VNDF)

float2 SampleBlueNoise(int2 pixel, int frame_index)
{
    int3 coords = int3(pixel.x & 127, pixel.y & 127, frame_index & 63);
    return g_stbn_128_64.Load(int4(coords, 0));
}

// In intersection shader:
float2 u  = SampleBlueNoise(pixel, g_frame_index);
float3 H  = SampleGGX_VNDF(N, V, roughness, u);
float3 R  = reflect(-V, H);
```

### 11.4 Published Convergence (Wolfe 2022 benchmarks)

For 1-spp SSR at roughness = 0.3, TAA accumulation to stable image:

| Noise Source | Frames to Stability |
|-------------|--------------------:|
| White noise (rand) | ~80 frames |
| IGN (Interleaved Gradient Noise) | ~65 frames |
| Spatial-only blue noise | ~50 frames |
| STBN (spatiotemporal) | ~30 frames |

---

## 12. AMD FidelityFX SSSR — Production Pipeline (2021–2024) {#12-fidelityfx-sssr}

### 12.1 Sources

> **AMD GPUOpen.** "FidelityFX Stochastic Screen-Space Reflections 1.5."
> https://gpuopen.com/fidelityfx-sssr/
> GitHub (MIT license): https://github.com/GPUOpen-Effects/FidelityFX-SSSR

> **AMD GPUOpen Manual:**
> https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/

> **Interplay of Light analysis:**
> https://interplayoflight.wordpress.com/2022/09/28/notes-on-screenspace-reflections-with-fidelityfx-sssr/

### 12.2 Complete Pass Pipeline

```
Input: GBuffer (depth, normals, roughness, metallic), HDR color, prev frame outputs
Output: Denoised reflection radiance texture (ready for composite)

Pass 1: DepthDownsample [CS, fullscreen → N mips]
  Builds Hi-Z pyramid: each mip = max of 2×2 children
  (reversed-Z: max raw depth = furthest from camera)
  Output: HZB texture (R32_FLOAT, N = log2(max(W,H)) mip levels)

Pass 2: ClassifyTiles [CS, 8×8 tiles]
  Per-tile roughness analysis:
  - roughness > 0.7 → TILE_SKIP (use IBL fallback only)
  - roughness 0.3–0.7 → TILE_HALF_RATE (1 ray per 4 pixels, checkerboard)
  - roughness 0.1–0.3 → TILE_FULL_RATE (1 ray per pixel)
  - roughness < 0.1 → TILE_MIRROR (1 ray per pixel, no denoiser needed)
  Output: ray counter buffer + indirect dispatch arguments

Pass 3: PrepareBlueNoise [CS, trivial]
  Advance STBN frame index in a constant buffer.

Pass 4: IntersectRays [CS, indirect dispatch — only tiles needing rays]
  For each pixel requiring a ray:
    u         = SampleBlueNoise(pixel, frame_index)        // STBN 128×128×64
    H         = SampleGGX_VNDF_Bounded(N, V, roughness, u) // Boksansky 2023
    R         = reflect(-V, H)
    hit, dist = HiZRayMarch(world_pos, R, max_steps=128, thickness=depth_buffer_thickness)
    radiance  = SampleSceneColor(hit.uv)
    Store(hit_radiance_buffer, radiance)
    Store(hit_distance_buffer, dist)   // needed for virtual reprojection

Pass 5: DNSR_Reproject [CS, fullscreen]
  For each pixel:
    hit_world   = camera_pos + R * hit_distance       // virtual hit world position
    prev_uv     = Project(hit_world, prev_VP_matrix)  // reproject to prev frame
    history     = SampleHistory(prev_uv)
    disoccluded = (abs(depth_here - depth_at_prev_uv) > threshold)
    blend_factor = disoccluded ? 1.0 : (1.0 - min(1.0, valid_samples / 50))
    m1_new = lerp(prev_m1, radiance, blend_factor)        // E[x] moment
    m2_new = lerp(prev_m2, radiance * radiance, blend_factor)  // E[x^2] moment
    variance = max(0, m2_new - m1_new * m1_new)
    Store(reprojected_history, lerp(history, radiance, blend_factor))
    Store(variance_buffer, variance)

Pass 6: DNSR_Prefilter [CS, indirect dispatch — same tiles as Pass 4]
  Bilateral spatial filter, radius 8 pixels:
    w = exp(-|depth_a - depth_b| / sigma_depth) *
        pow(max(0, dot(N_a, N_b)), sigma_normal) *
        exp(-|roughness_a - roughness_b| / sigma_rough)
  sigma_depth = 1.0, sigma_normal = 8 (exponent), sigma_rough = 0.01

Pass 7: DNSR_TemporalAccumulate [CS, indirect dispatch]
  Variance-clamped blend:
    mean   = LocalMean(prefiltered, 3×3)
    sigma  = sqrt(LocalVariance(prefiltered, 3×3))
    clamped_history = clamp(history, mean - 1.5*sigma, mean + 1.5*sigma)
    output          = lerp(clamped_history, prefiltered, alpha_adaptive)

Pass 8: Composite [PS or CS]
  result = scene_color + ssr_radiance * Fresnel(NdotV, F0) * ssr_fade
  ssr_fade = screen_edge_fade * roughness_fade * hit_confidence
```

### 12.3 All Published Constants

| Constant | Value | Source |
|---------|-------|--------|
| Tile size | 8×8 pixels | AMD SSSR 1.5 |
| Roughness skip threshold | 0.7 | AMD SSSR 1.5 |
| Max Hi-Z traversal steps | 128 | AMD SSSR 1.5 |
| `depth_buffer_thickness` | 0.005–0.05 (scene-dependent) | AMD SSSR 1.5 |
| STBN texture size | 128×128×64 | AMD SSSR 1.5 |
| STBN frame index wrap | 64 frames | AMD SSSR 1.5 |
| Temporal blend target | 0.05 (at 50 valid samples) | AMD SSSR 1.5 |
| Variance clamp N | 1.5 sigma | AMD SSSR DNSR |
| Spatial filter radius | 8 pixels | AMD SSSR DNSR |
| Depth sigma (spatial) | 1.0 | AMD SSSR DNSR |
| Normal sigma (exponent) | 8 | AMD SSSR DNSR |
| Roughness sigma (spatial) | 0.01 | AMD SSSR DNSR |
| Screen edge fade start | 0.75 | Standard (McGuire 2014) |

### 12.4 Performance (RDNA2, 1080p, typical indoor scene)

| Pass | GPU Time |
|------|----------|
| DepthDownsample | ~0.05ms |
| ClassifyTiles | ~0.12ms |
| PrepareBlueNoise | <0.02ms |
| IntersectRays | ~0.88ms |
| DNSR_Reproject | ~0.22ms |
| DNSR_Prefilter | ~0.34ms |
| DNSR_TemporalAccumulate | ~0.18ms |
| Composite | ~0.10ms |
| **Total** | **~1.9ms** |

---

## 13. Hybrid SSR + Ray Tracing {#13-hybrid-ssr-rt}

### 13.1 Sources

> **AMD GPUOpen.** "FidelityFX Hybrid Stochastic Reflections."
> https://gpuopen.com/fidelityfx-hybrid-reflections/
> GitHub: https://github.com/GPUOpen-Effects/Hybrid-Stochastic-Reflections

> **Halén, H.** "Hybrid Rendering for Real-Time Ray Tracing."
> In *Ray Tracing Gems*, Apress, Chapter 25, 2019.
> DOI: 10.1007/978-1-4842-4427-2_25

> **Unity HDRP:** Mixed tracing mode SSR documentation.
> https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Override-Screen-Space-Reflection.html

### 13.2 Motivation

Screen-space methods fail when the reflected geometry is off-screen. Hardware ray
tracing (DXR/VK_KHR_ray_tracing) can find off-screen geometry by traversing the
scene BVH, but is 4–10× more expensive per ray than SSR.

Hybrid approach: SSR as primary, ray tracing as fallback only for misses:

```
// AMD Hybrid Stochastic Reflections — layered architecture
float3 ComputeReflection(float3 pos, float3 R, float roughness)
{
    // Layer 1: Screen-space Hi-Z traversal (SSSR)
    HitResult ss_hit = HiZRayMarch(pos, R, max_steps=128);
    if (ss_hit.found) {
        return SampleColor(ss_hit.uv) * ss_hit.confidence;
    }

    // Layer 2: DXR hardware BVH traversal (indirect dispatch, miss pixels only)
    HitResult rt_hit = TraceRayDXR(pos, R, max_t=100.0);
    if (rt_hit.found) {
        return EvaluateMaterialAtHit(rt_hit) * rt_hit.confidence;
    }

    // Layer 3: IBL fallback (always available)
    return SamplePrefilteredIBL(R, roughness);
}
```

### 13.3 Per-Pixel Feedback Classification

AMD HSR uses an 8×8 tile predictor with per-frame feedback:

```
// Updated per-frame based on actual miss rates
if (prev_frame_miss_rate[tile] > 0.3):
    // 30% of rays missed SSR → submit those pixels to DXR
    DXR_dispatch_args[tile].count = miss_pixels_count
else:
    SSR_dispatch_args[tile].count = all_pixels
```

### 13.4 Hit Confidence Weighting

```hlsl
// SSR confidence: penalize grazing-angle hits (likely self-intersection)
float ssr_confidence = saturate(1.0 - hit_distance / max_trace_distance)
                     * saturate(dot(hit_normal, -R) * 4.0)
                     * screen_visibility_weight;

// DXR confidence: always 1.0 for valid geometry hits
float rt_confidence = 1.0;
```

The denoiser receives the **composited** single-ray result — it does not need to
know which layer produced each sample.

---

## 14. NVIDIA NRD — Real-Time Denoiser {#14-nrd}

### 14.1 Source

> **NVIDIA RTX.** "NVIDIA Real-Time Denoisers (NRD)."
> https://developer.nvidia.com/rtx/ray-tracing/rt-denoisers/
> GitHub (MIT license): https://github.com/NVIDIA-RTX/NRD

### 14.2 Denoiser Architecture

NRD provides three denoisers:

| Denoiser | Signal | Technique |
|---------|--------|-----------|
| **REBLUR** | Diffuse + Specular radiance | Recurrent, self-stabilizing blur |
| **RELAX** | Diffuse + Specular radiance | Sparse spatiotemporal accumulation |
| **SIGMA** | Shadows | Fast shadow-specific denoiser |

For reflections (SSR or RT), **REBLUR** is the recommended denoiser.

### 14.3 REBLUR Algorithm (Reflection-Specific)

REBLUR's key innovation for specular reflections is **virtual motion**:

```
// REBLUR specular reprojection
float3 hit_world  = pos + R * hit_distance          // virtual hit world pos
float3 prev_hit   = Transform(hit_world, prev_world_to_screen)

// Blur along reflection footprint (not screen space)
// Footprint radius scales with roughness × hit distance
float footprint_radius = roughness * hit_distance / max(hit_distance, 1.0)
                       * screen_pixels_per_unit

float3 blurred    = RadiusBlur(current_radiance, footprint_radius, normal_weights)
float3 history    = SampleVirtualReprojection(hit_world, prev_frame)
float  alpha      = ComputeAdaptiveAlpha(disocclusion, motion_vectors, frame_count)
float3 output     = lerp(history, blurred, alpha)
```

### 14.4 REBLUR vs. SVGF for Reflections

| Property | SVGF | REBLUR |
|---------|------|--------|
| Spatial filter | À-trous wavelet (5 levels) | Adaptive radius blur (1 pass) |
| Temporal reprojection | Screen-space MV | Virtual motion for specular |
| Dynamic content | A-SVGF needed | Built-in adaptive alpha |
| Performance (1080p) | ~10ms (all 5 levels) | ~3–5ms |
| Quality on specular | Good | Better (virtual motion) |
| Open source | Yes (many impls) | Yes (NRD SDK, MIT) |

---

## 15. Algorithm Comparison Matrix {#15-comparison}

| Feature | McGuire 2014 | Uludag 2014 | Stachowiak 2015 | Heitz 2018 | AMD SSSR 1.5 | NRD REBLUR |
|---------|:-----------:|:-----------:|:---------------:|:----------:|:------------:|:----------:|
| Traversal | DDA linear | Hi-Z pyramid | DDA half-res | — | Hi-Z pyramid | — |
| Max steps | 128–256 | 128 | 128 | — | 128 | — |
| Ray sampling | Mirror only | Mirror only | GGX NDF | GGX VNDF | Bounded VNDF | — |
| Blue noise source | Analytical jitter | White noise | Not specified | — | STBN 128×128×64 | — |
| Spatial filter | None | None | 5×5 bilateral | — | 8px bilateral | Adaptive radius |
| Temporal filter | None | None | TAA α=0.1 | — | Variance-clamp | Virtual motion |
| Reprojection | Surface MV | Surface MV | Virtual hit | — | Virtual hit | Virtual motion |
| Half-res option | No | No | Yes (default) | — | Optional | — |
| Roughness cutoff | Manual | Manual | ~0.6 | — | 0.7 (configurable) | — |
| RT hybrid | No | No | No | — | Yes (HSR) | Yes |
| License | Academic | Book | Proprietary | JCGT (open) | MIT | MIT |

---

## 16. Quick Reference — Constants Cheat Sheet {#16-constants}

Every published numeric constant across all papers, for direct use in shader implementation:

| Constant | Value | Paper |
|---------|-------|-------|
| DDA max steps | 128–256 | McGuire 2014 |
| DDA stride (pixels) | 1–4 | McGuire 2014 |
| DDA binary refinement steps | 4–8 | McGuire 2014 |
| Screen edge fade start | 0.75 (fraction of half-width) | McGuire 2014 |
| Depth thickness | 0.005–0.05 (scene-dependent) | AMD SSSR 1.5 |
| Hi-Z max mip levels | log2(max(W,H)) | Uludag 2014 |
| Hi-Z max steps | 128 | AMD SSSR 1.5 |
| GGX VNDF — no tunable constants | exact math | Heitz 2018 |
| Bounded VNDF — no tunable constants | exact math | Dupuy & Benyoub 2023 |
| Roughness skip threshold | 0.7 | AMD SSSR 1.5 |
| Half-rate roughness range | 0.3–0.7 | AMD SSSR 1.5 |
| Tile size | 8×8 pixels | AMD SSSR 1.5 |
| STBN texture size | 128×128×64 | AMD SSSR 1.5 |
| STBN frame index wrap | 64 | AMD SSSR 1.5 |
| Temporal alpha (fixed, Frostbite) | 0.1 | Stachowiak 2015 |
| Temporal max accumulation | 50 frames → alpha 0.02 | AMD SSSR 1.5 |
| Variance clamp N | 1.5 sigma | AMD SSSR / SVGF |
| SVGF temporal alpha | 0.1 | Schied 2017 |
| SVGF depth sigma | 1.0 | Schied 2017 |
| SVGF normal sigma (exponent) | 128.0 | Schied 2017 |
| SVGF luminance sigma | 4.0 | Schied 2017 |
| SVGF atrous levels | 5 | Schied 2017 |
| SVGF kernel weights | `[0.0625, 0.25, 0.375, 0.25, 0.0625]` | Schied 2017 |
| SVGF variance bootstrap frames | 4 | Schied 2017 |
| SSSR spatial filter radius | 8 pixels | AMD SSSR DNSR |
| SSSR bilateral depth sigma | 1.0 | AMD SSSR DNSR |
| SSSR bilateral normal sigma (exp) | 8 | AMD SSSR DNSR |
| SSSR bilateral roughness sigma | 0.01 | AMD SSSR DNSR |
| Variance clip gamma | 1.0 | Karis 2014 |
| White noise convergence | ~80 frames | Wolfe 2022 |
| STBN convergence | ~30 frames | Wolfe 2022 |

---

## 17. BibTeX References {#17-bibtex}

```bibtex
@article{mcguire2014efficient,
  author  = {McGuire, Morgan and Mara, Michael},
  title   = {Efficient {GPU} Screen-Space Ray Tracing},
  journal = {Journal of Computer Graphics Techniques (JCGT)},
  year    = {2014},
  volume  = {3},
  number  = {4},
  pages   = {73--85},
  url     = {https://jcgt.org/published/0003/04/04/}
}

@incollection{uludag2014hiz,
  author    = {Uludag, Yasin},
  title     = {Hi-{Z} Screen-Space Cone-Traced Reflections},
  booktitle = {{GPU} Pro 5: Advanced Rendering Techniques},
  editor    = {Engel, Wolfgang},
  publisher = {A K Peters/CRC Press},
  year      = {2014},
  pages     = {149--192},
  doi       = {10.1201/b16721-13}
}

@inproceedings{stachowiak2015stochastic,
  author    = {Stachowiak, Tomasz},
  title     = {Stochastic Screen-Space Reflections},
  booktitle = {Advances in Real-Time Rendering in 3D Graphics and Games,
               SIGGRAPH 2015 Course},
  year      = {2015},
  publisher = {ACM},
  url       = {https://www.ea.com/frostbite/news/stochastic-screen-space-reflections}
}

@inproceedings{karis2014high,
  author    = {Karis, Brian},
  title     = {High Quality Temporal Supersampling},
  booktitle = {Advances in Real-Time Rendering in Games, SIGGRAPH 2014 Course},
  year      = {2014},
  publisher = {ACM},
  url       = {https://advances.realtimerendering.com/s2014/}
}

@inproceedings{schied2017svgf,
  author    = {Schied, Christoph and Kaplanyan, Anton and Wyman, Chris and
               Patney, Anjul and Chaitanya, Chakravarty R. Alla and
               Burgess, John and Liu, Shiqiu and Dachsbacher, Carsten and
               Lefohn, Aaron and Salvi, Marco},
  title     = {Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction
               for Path-Traced Global Illumination},
  booktitle = {Proceedings of High Performance Graphics (HPG)},
  year      = {2017},
  publisher = {ACM},
  doi       = {10.1145/3105762.3105770}
}

@article{schied2018asvgf,
  author  = {Schied, Christoph and Peters, Christoph and Dachsbacher, Carsten},
  title   = {Gradient Estimation for Real-Time Adaptive Temporal Filtering},
  journal = {Proceedings of the ACM on Computer Graphics and Interactive Techniques},
  year    = {2018},
  volume  = {1},
  number  = {2},
  doi     = {10.1145/3233301}
}

@article{chaitanya2017interactive,
  author  = {Chaitanya, Chakravarty R. Alla and Kaplanyan, Anton S. and
             Schied, Christoph and Salvi, Marco and Lefohn, Aaron and
             Nowrouzezahrai, Derek and Aila, Timo},
  title   = {Interactive Reconstruction of {Monte Carlo} Image Sequences
             using a Recurrent Denoising Autoencoder},
  journal = {ACM Transactions on Graphics},
  year    = {2017},
  volume  = {36},
  number  = {4},
  articleno = {98},
  doi     = {10.1145/3072959.3073601}
}

@article{heitz2018vndf,
  author  = {Heitz, Eric},
  title   = {Sampling the {GGX} Distribution of Visible Normals},
  journal = {Journal of Computer Graphics Techniques (JCGT)},
  year    = {2018},
  volume  = {7},
  number  = {4},
  pages   = {1--13},
  url     = {https://jcgt.org/published/0007/04/01/}
}

@inproceedings{heitz2019bluenoise,
  author    = {Heitz, Eric and Belcour, Laurent and Ostromoukhov, Victor and
               Coeurjolly, David and Iehl, Jean-Claude},
  title     = {A Low-Discrepancy Sampler that Distributes {Monte Carlo} Errors
               as a Blue Noise in Screen Space},
  booktitle = {ACM SIGGRAPH 2019 Talks},
  year      = {2019},
  publisher = {ACM},
  doi       = {10.1145/3306307.3328191}
}

@article{heitz2019bluenoise_cgf,
  author  = {Heitz, Eric and Belcour, Laurent},
  title   = {Distributing {Monte Carlo} Errors as a Blue Noise in Screen Space
             by Permuting Pixel Seeds Between Frames},
  journal = {Computer Graphics Forum},
  year    = {2019},
  volume  = {38},
  number  = {4},
  doi     = {10.1111/cgf.13778}
}

@article{ahmed2020screenspace,
  author  = {Ahmed, Abdalla G. M. and Wonka, Peter},
  title   = {Screen-Space Blue-Noise Diffusion of {Monte Carlo} Sampling Error
             via Hierarchical Ordering of Pixels},
  journal = {ACM Transactions on Graphics},
  year    = {2020},
  volume  = {39},
  number  = {6},
  articleno = {244},
  doi     = {10.1145/3414685.3417881}
}

@inproceedings{wolfe2022stbn,
  author    = {Wolfe, Alan and Morrical, Nathan and Akenine-M{\"o}ller, Tomas and
               Ramamoorthi, Ravi},
  title     = {Scalar Spatiotemporal Blue Noise Masks},
  booktitle = {Eurographics Symposium on Rendering (EGSR)},
  year      = {2022},
  doi       = {10.2312/sr.20221161},
  eprint    = {2112.09629},
  archivePrefix = {arXiv}
}

@article{dupuy2023sphericalcaps,
  author  = {Dupuy, Jonathan and Benyoub, Anis},
  title   = {Sampling Visible {GGX} Normals with Spherical Caps},
  journal = {Computer Graphics Forum},
  year    = {2023},
  volume  = {42},
  number  = {8},
  doi     = {10.1111/cgf.14867},
  eprint  = {2306.05044},
  archivePrefix = {arXiv}
}

@inproceedings{boksansky2023bounded,
  author    = {Boksansky, Jakub and Ureche, Tomas and Streit, Petr and Pesic, Miljan},
  title     = {Bounded {VNDF} Sampling for {Smith--GGX} Reflections},
  booktitle = {ACM SIGGRAPH Symposium on Interactive 3D Graphics and Games (I3D 2024)},
  year      = {2023},
  doi       = {10.1145/3610543.3626163}
}

@misc{amd2022sssr,
  author = {{AMD GPUOpen}},
  title  = {{FidelityFX} Stochastic Screen-Space Reflections 1.5},
  year   = {2022},
  url    = {https://gpuopen.com/fidelityfx-sssr/},
  note   = {MIT license. https://github.com/GPUOpen-Effects/FidelityFX-SSSR}
}

@misc{amd2022hsr,
  author = {{AMD GPUOpen}},
  title  = {{FidelityFX} Hybrid Stochastic Reflections},
  year   = {2022},
  url    = {https://gpuopen.com/fidelityfx-hybrid-reflections/},
  note   = {MIT license. https://github.com/GPUOpen-Effects/Hybrid-Stochastic-Reflections}
}

@incollection{halen2019hybrid,
  author    = {Hal{\'e}n, Henrik},
  title     = {Hybrid Rendering for Real-Time Ray Tracing},
  booktitle = {Ray Tracing Gems},
  editor    = {Haines, Eric and Akenine-M{\"o}ller, Tomas},
  publisher = {Apress},
  year      = {2019},
  chapter   = {25},
  doi       = {10.1007/978-1-4842-4427-2_25}
}

@misc{nvidia2021nrd,
  author = {{NVIDIA RTX}},
  title  = {{NVIDIA} Real-Time Denoisers ({NRD})},
  year   = {2021},
  url    = {https://developer.nvidia.com/rtx/ray-tracing/rt-denoisers/},
  note   = {MIT license. https://github.com/NVIDIA-RTX/NRD}
}

@inproceedings{sousa2011cryengine,
  author    = {Sousa, Tiago},
  title     = {Secrets of {CryENGINE} 3 Graphics Technology},
  booktitle = {SIGGRAPH 2011 Course: Advances in Real-Time Rendering},
  year      = {2011},
  publisher = {ACM}
}
```
