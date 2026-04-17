<!-- doc-meta
title: SSR-TAA Noise Bleeding Analysis
category: rendering
status: active
date: 2026-04-05
-->

# SSR-TAA Noise Bleeding: Cross-Surface Contamination Analysis

## Problem Statement

When SSR is active, it modifies the specular color of smooth surfaces (roughness < 0.35).
Adjacent rough surfaces (roughness >= 0.4) that do NOT receive SSR exhibit stipple/grain
noise caused by TAA's 3x3 neighborhood variance clipping. TAA reads colors from neighboring
SSR-modified pixels and these high-variance samples pollute the variance statistics for the
rough surface, widening the AABB clip box and allowing temporal ghosting or noise to persist.

## UzEngine Current Pipeline Order

```
GBuffer Pass
  |
DBuffer Apply
  |
GTAO
  |
DFS (Distance Field Shadows)
  |
SSGI
  |
SDF GI
  |
SSR_Trace  -->  SSR_Resolve  -->  SSR_Temporal    (separate history, own temporal filter)
  |
EnvMIS_Temporal
  |
Deferred Lighting  <-- SSR output composited HERE (lerp with IBL specular)
  |
Atmosphere / Fog / Clouds
  |
SSR_CopyPrevColor (copy HDR for next frame SSR input)
SSGI_CopyPrevColor
  |
TAA Resolve  <-- SSR is ALREADY baked into HDR scene color
  |
Bloom
  |
Tonemap
```

The critical issue: SSR output is composited into HDR scene color **inside**
`deferred_lighting_ps.hlsl` at lines 429-455. By the time TAA runs, SSR-modified and
non-SSR pixels are **indistinguishable** in the scene color buffer. TAA's 3x3 neighborhood
sampling crosses SSR boundaries blindly.

---

## Engine-by-Engine Analysis

### 1. Unreal Engine 5 (TemporalAA.usf / TSR)

**Pipeline order**: SSR runs BEFORE deferred lighting. The SSR result is composited into
the scene color during the `ReflectionEnvironmentAndSky` pass (indirect specular composite).
TAA/TSR then operates on the final lit HDR buffer that already contains SSR.

**How UE5 mitigates the cross-boundary problem**:

1. **SSR has its own temporal filter** (ScreenSpaceReflections.usf temporal resolve pass).
   The SSR signal is temporally accumulated and denoised BEFORE it enters the lighting
   composite. This reduces frame-to-frame SSR variance that would otherwise widen TAA's
   AABB clip box.

2. **Roughness-based SSR fade**: UE5 applies a smooth roughness fade (default max roughness
   ~0.4-0.6 depending on quality level). The SSR confidence/alpha smoothly fades to zero
   as roughness approaches the cutoff, reducing the step-function discontinuity between
   SSR-receiving and non-SSR-receiving pixels.

3. **TAA variance clip gamma**: UE5's TemporalAA.usf uses `VarianceClipGamma` (typically
   1.0-1.25) which controls how aggressively the variance AABB is expanded. A lower gamma
   clips more aggressively, which helps suppress SSR noise bleeding but can cause more
   ghosting. UE5 does NOT use depth-aware or material-aware neighborhood filtering in the
   main TAA pass.

4. **TSR (Temporal Super Resolution)** in UE5.1+ replaces TAA with a more sophisticated
   approach that includes: per-pixel rejection heuristics, moire detection, and a "flicker
   firefly" suppressor that specifically targets high-variance outlier pixels in the
   neighborhood. TSR does not directly detect SSR boundaries, but its outlier rejection
   is more aggressive than basic TAA variance clipping.

5. **Reactive mask (for FSR3/DLSS integration)**: When third-party upscalers are used, UE5
   generates a "reactive mask" that marks SSR-affected pixels. The upscaler uses this mask
   to reduce temporal accumulation weight on those pixels, preventing noise from spreading.
   This is the closest UE5 gets to an explicit SSR-TAA interaction mechanism.

**Key insight**: UE5 does NOT solve this problem with a special cross-boundary filter. It
relies on (a) pre-denoised SSR temporal output, (b) smooth roughness fade, and (c) the
inherent robustness of variance clipping. Residual noise at SSR boundaries is considered
acceptable.

@ref Karis 2014 "High Quality Temporal Supersampling"
@ref Salvi 2016 "An Excursion in Temporal Supersampling"

---

### 2. AMD FidelityFX SSSR + Denoiser

**Pipeline order**: SSSR operates as a completely independent pipeline with its own
multi-pass denoiser. The denoised SSR result is composited into the lit scene BEFORE
the main TAA pass.

**SSSR Denoiser Architecture** (4 passes, all independent of TAA):

```
Pass 1: Tile Classification
  - Classifies 8x8 tiles by ray count needed
  - Roughness determines ray budget (smoother = more rays)

Pass 2: Intersection (Ray March)
  - Hi-Z ray march, 1 ray/pixel (stochastic with blue noise)

Pass 3: DNSR Reproject (Temporal)
  - Reprojects SSR history using REFLECTION-SPECIFIC motion vectors
  - NOT surface motion vectors — uses hit point parallax correction
  - Maintains its OWN temporal history buffer (separate from TAA)
  - Neighborhood clamping within the SSR temporal pass
  - Disocclusion detection resets temporal accumulation per-pixel
  - Variance estimation per-pixel guides spatial filter kernel

Pass 4: DNSR Prefilter (Spatial)
  - Edge-aware spatial filter: 15 samples in 7x7 region
  - Bilateral weights based on: depth, normal, roughness
  - Variance-guided kernel: high variance areas get wider blur
  - Low variance (converged) areas get almost no blur
```

**How SSSR prevents TAA contamination**:

1. **Fully converged output**: By the time SSSR output reaches the lighting composite,
   it has been through its own temporal accumulation + spatial bilateral filter. The
   output is nearly noise-free, so TAA sees minimal frame-to-frame variance from SSR.

2. **Edge-aware spatial filter**: The DNSR Prefilter uses depth + normal + roughness
   bilateral weights. This means the spatial blur does NOT cross material boundaries.
   A rough surface adjacent to a smooth SSR surface will not receive SSR color bleed
   from the spatial denoiser.

3. **Roughness-based ray count reduction**: Rougher surfaces get fewer rays (or zero
   above the cutoff). Combined with the roughness bilateral weight in the spatial
   filter, this creates a clean boundary between SSR and non-SSR regions.

4. **Reflection-specific motion vectors**: SSSR reprojects using parallax-corrected
   hit point motion, not surface motion. This prevents temporal smearing of reflections
   under camera motion, which would otherwise create additional variance for TAA.

**Key insight**: SSSR's approach is the gold standard for SSR-TAA interaction. The SSR
denoiser is **completely independent** from TAA. It converges the SSR signal to near
noise-free BEFORE TAA ever sees it. This is the approach UzEngine should adopt.

@ref Stachowiak 2015 "Stochastic Screen-Space Reflections"
@ref AMD 2021 "FidelityFX SSSR" — GPUOpen

---

### 3. Frostbite (Stachowiak 2015, EA SEED)

**Pipeline order**: Frostbite applies SSR with a **dedicated temporal and spatial
denoiser** that runs independently before the main TAA pass. The denoised SSR result
is composited into the lit HDR buffer, which then goes through TAA.

**Stochastic SSR Denoiser Architecture**:

```
Phase 1: Ray March (half-res)
  - Importance-sampled rays based on GGX BRDF
  - 1 ray/pixel at half resolution
  - Blue noise temporal jitter (different pattern each frame)

Phase 2: Resolve (full-res)
  - Gathers 4 neighboring half-res ray results
  - BRDF-weighted resolve to full resolution
  - Effective 4 rays/pixel via spatial reuse

Phase 3: Temporal Filter (SSR-specific)
  - Parallax-corrected reflection reprojection
  - NOT standard surface motion vectors
  - Hit point depth determines reflection motion
  - Neighborhood clamping within SSR temporal pass
  - Variance tracking per-pixel

Phase 4: Spatial Filter
  - Bilateral filter guided by variance estimate
  - Wider kernel where variance is high (noisy)
  - Near-zero kernel where variance is low (converged)
  - Edge weights: depth, normal (prevents cross-boundary bleed)
```

**How Frostbite prevents cross-boundary noise**:

1. **Half-res trace + full-res resolve**: The resolve step gathers from 4 half-res
   neighbors, applying BRDF weights. This spatial reuse acts as an implicit low-pass
   filter on the SSR signal before temporal accumulation.

2. **Variance-guided spatial filter**: Stachowiak explicitly noted that bilateral
   filtering is "a dumb, blunt instrument" and should be variance-guided. The kernel
   width adapts: high noise areas get aggressive smoothing, converged areas get none.
   This prevents over-blurring while still eliminating noise before TAA.

3. **Reflection-specific temporal reprojection**: Standard TAA reprojection uses surface
   motion vectors. Reflections move differently (they depend on the reflected object's
   depth, not the reflecting surface's depth). Frostbite uses parallax-corrected motion
   vectors computed from the ray hit point, preventing temporal smearing that would
   increase variance at SSR boundaries.

4. **Roughness-based ray allocation**: More rays are allocated to smoother surfaces
   (lower roughness = more directional = more noise-sensitive). Rough surfaces above
   the cutoff get zero rays, creating a clean boundary.

**Key insight**: Frostbite pioneered the "independent SSR denoiser" approach that SSSR
later adopted. The critical innovation is parallax-corrected reflection reprojection,
which prevents temporal smearing at SSR boundaries.

@ref Stachowiak 2015 "Stochastic Screen-Space Reflections" — SIGGRAPH Advances in Real-Time Rendering

---

### 4. Google Filament

Filament supports both SSR and TAA as optional features (`setScreenSpaceReflectionsOptions()`
and `setTemporalAntiAliasingOptions()` in the View API). However, Filament is primarily a
mobile/web renderer and does NOT implement a dedicated SSR denoiser.

**Filament's approach**:

1. SSR is applied as a post-process effect on the lit HDR buffer.
2. TAA runs after SSR composite.
3. No dedicated SSR temporal or spatial filter.
4. Filament relies on TAA's variance clipping to suppress SSR noise.

**Filament does NOT solve the cross-boundary problem**. It accepts residual noise as
a tradeoff for simplicity. This is acceptable for Filament's target platforms (mobile,
WebGL) where SSR quality expectations are lower.

Filament is NOT a good reference for solving this problem.

---

### 5. NVIDIA NRD (Real-time Denoiser) — Bonus Reference

NVIDIA NRD provides a production-quality reflection denoiser (REBLUR/ReLAX) designed
for ray-traced reflections but applicable to SSR:

1. **Completely independent temporal accumulation** (separate from TAA).
2. **Specular-specific reprojection**: Uses virtual hit point motion, not surface motion.
3. **Per-pixel confidence/accumulation speed**: Disoccluded pixels get faster accumulation.
4. **Multi-stage spatial filter**: Hierarchical A-trous wavelet with edge-stopping functions
   based on depth, normal, roughness, and luminance.
5. **Explicit recommendation**: NRD documentation states "TAA can help dramatically and is
   highly recommended" but the denoiser output should be **already clean** before TAA.

NRD confirms the industry consensus: denoise reflections independently, then let TAA
provide additional stabilization on the already-clean signal.

@ref Zhdan 2022 "REBLUR: A Hierarchical Recurrent Denoiser" — Ray Tracing Gems II

---

## Root Cause Analysis for UzEngine

UzEngine's current pipeline has the SSR-TAA interaction problem because:

1. **SSR temporal filter exists but may not fully converge**: The `SSR_Temporal` pass uses
   a simple exponential moving average with neighborhood clamping. If the SSR signal has
   residual noise after temporal accumulation, that noise enters the HDR scene color.

2. **No spatial denoiser on SSR output**: Unlike SSSR/Frostbite/NRD, UzEngine has NO
   spatial bilateral filter on the SSR result. The temporal output goes directly into
   the lighting composite. Residual per-pixel noise is preserved.

3. **Hard roughness boundary**: The roughness gate in `deferred_lighting_ps.hlsl` uses
   `ssrRoughnessMask = saturate(1.0 - (roughness / 0.35))`. This creates a relatively
   sharp transition at roughness=0.35. Pixels at roughness=0.34 get full SSR; pixels at
   roughness=0.36 get none. The 3x3 TAA neighborhood crosses this boundary.

4. **TAA neighborhood is NOT depth/material-aware**: The `taa_resolve_cs.hlsl` samples all
   9 neighbors equally (HDR-weighted, but no depth or roughness rejection). A rough surface
   pixel next to an SSR-modified smooth surface pixel gets the SSR color included in its
   variance statistics.

---

## Concrete Solutions (Ranked by Impact and Complexity)

### Solution A: Add SSR Spatial Bilateral Filter (RECOMMENDED FIRST)

**What**: Add a compute pass `SSR_SpatialDenoise` between `SSR_Temporal` and the deferred
lighting composite. This pass applies a variance-guided edge-aware bilateral filter to the
SSR temporal output.

**Pipeline change**:
```
BEFORE: SSR_Trace -> SSR_Resolve -> SSR_Temporal -> [deferred lighting reads SSR]
AFTER:  SSR_Trace -> SSR_Resolve -> SSR_Temporal -> SSR_SpatialDenoise -> [deferred lighting reads SSR]
```

**Shader** (`ssr_spatial_denoise_cs.hlsl`):
```hlsl
// 15 samples in 7x7 region, bilateral weights:
//   w_depth   = exp(-abs(d_center - d_sample) * depthSigma)
//   w_normal  = max(0, dot(n_center, n_sample))^normalPower
//   w_luma    = exp(-abs(luma_center - luma_sample) * lumaSigma)
//   w_spatial = exp(-dist^2 * spatialSigma)
//   w_total   = w_depth * w_normal * w_luma * w_spatial
```

**C++ changes**:
- `frame_gi_passes.cpp`: Add `SSR_SpatialDenoise` pass after `SSR_Temporal`.
- `pipeline_library_impl.h`: Add `GetSsrSpatialDenoise()` PSO getter.
- `pipeline_gi.cpp`: Create the compute PSO.
- Need one additional UAV texture (or ping-pong with existing SSR resolve output).

**Visual tradeoff**: Slightly softer SSR reflections on smooth surfaces (acceptable for
roughness > 0.05). Near-mirror surfaces (roughness ~0) are barely affected because the
bilateral filter preserves edges.

**Impact**: HIGH. This is what SSSR and Frostbite do. It eliminates most SSR noise before
TAA sees it, directly addressing the root cause.

**Complexity**: MEDIUM. One new compute shader, one new render graph pass.

---

### Solution B: Depth-Aware TAA Neighborhood Sampling

**What**: Modify `taa_resolve_cs.hlsl` to weight 3x3 neighborhood samples by depth
similarity. Samples with very different depth values (different surfaces) get reduced
weight in the variance/min/max computation.

**Shader change** (`taa_resolve_cs.hlsl`):
```hlsl
// Current (line 163-169): all neighbors weighted equally
for (uint i = 0; i < 9; i++) {
    int2 coord = clamp(centerCoord + kOffsets3x3[i], ...);
    float3 rawColor = SceneColor.Load(int3(coord, 0)).rgb;
    neighbors[i] = RGBToYCoCg(rawColor);
    neighborWeights[i] = HdrWeightY(neighbors[i].x, FrameExposureScale);
}

// Proposed: add depth-based rejection
float centerZ = DepthTex.Load(int3(centerCoord, 0)).x;
for (uint i = 0; i < 9; i++) {
    int2 coord = clamp(centerCoord + kOffsets3x3[i], ...);
    float3 rawColor = SceneColor.Load(int3(coord, 0)).rgb;
    neighbors[i] = RGBToYCoCg(rawColor);

    float sampleZ = DepthTex.Load(int3(coord, 0)).x;
    float depthDiff = abs(centerZ - sampleZ);
    // Reject neighbors from different surfaces (depth discontinuity)
    float depthWeight = exp(-depthDiff * depthRejectionScale);
    neighborWeights[i] = HdrWeightY(neighbors[i].x, FrameExposureScale) * depthWeight;
}

// Then use neighborWeights in min/max computation:
// Only include neighbors with weight > threshold in AABB calculation
```

**Also modify the variance clip AABB** to use weighted mean/variance:
```hlsl
float3 weightedMu = float3(0, 0, 0);
float3 weightedM2 = float3(0, 0, 0);
float  totalW = 0;
for (uint i = 0; i < 9; i++) {
    float w = neighborWeights[i];
    weightedMu += neighbors[i] * w;
    weightedM2 += neighbors[i] * neighbors[i] * w;
    totalW += w;
}
weightedMu /= totalW;
weightedM2 /= totalW;
float3 neighborSigma = sqrt(max(weightedM2 - weightedMu * weightedMu, 0));
```

**Visual tradeoff**: Slightly more ghosting on depth edges (because the AABB is tighter,
history is rejected less aggressively at boundaries). This is the correct tradeoff: prefer
ghosting over noise on material boundaries.

**Impact**: MEDIUM. Helps at depth discontinuities (foreground/background) but does NOT
help at coplanar roughness boundaries (two materials on the same mesh at the same depth).

**Complexity**: LOW. Only shader changes in `taa_resolve_cs.hlsl`.

---

### Solution C: Widen SSR Roughness Fade Zone

**What**: Change the roughness fade in `deferred_lighting_ps.hlsl` from a sharp cutoff
at 0.35 to a wider smooth transition zone (0.15 to 0.5).

**Shader change** (`deferred_lighting_ps.hlsl` line 446):
```hlsl
// Current: hard cutoff at maxRoughness (0.35)
float ssrRoughnessMask = saturate(1.0 - (roughness / 0.35));

// Proposed: smooth hermite transition from fadeStart to fadeEnd
float ssrFadeStart = 0.15;  // Full SSR below this roughness
float ssrFadeEnd   = 0.50;  // Zero SSR above this roughness
float ssrRoughnessMask = 1.0 - smoothstep(ssrFadeStart, ssrFadeEnd, roughness);
```

**Visual tradeoff**: SSR visible on slightly rougher surfaces (roughness 0.35-0.50) at
reduced intensity. This smooths the energy transition and reduces the color step that
TAA's AABB clips against.

**Impact**: LOW-MEDIUM. Reduces the magnitude of the discontinuity but does not eliminate
the fundamental problem (SSR-modified pixels still enter TAA neighborhood).

**Complexity**: TRIVIAL. One line change in the lighting shader.

---

### Solution D: Post-TAA SSR Composite (NUCLEAR OPTION)

**What**: Remove SSR from the deferred lighting composite entirely. Instead, apply SSR
as a post-TAA fullscreen pass that composites the denoised SSR result onto the TAA-resolved
image. TAA never sees SSR-modified pixels.

**Pipeline change**:
```
BEFORE:
  SSR_Trace -> SSR_Resolve -> SSR_Temporal -> Deferred Lighting (with SSR) -> TAA

AFTER:
  SSR_Trace -> SSR_Resolve -> SSR_Temporal -> SSR_SpatialDenoise
  Deferred Lighting (NO SSR, IBL only) -> TAA -> SSR_Composite (post-TAA)
```

**New pass** (`ssr_composite_ps.hlsl`):
```hlsl
// Read TAA-resolved HDR, read SSR denoised, read GBuffer roughness/normal
float4 taaColor = TaaResolvedTex.Load(int3(px, 0));
float4 ssrData  = SsrDenoisedTex.Load(int3(px, 0));
float  roughness = GBufferRoughnessMetallic.Load(int3(px, 0)).r;

float ssrMask = 1.0 - smoothstep(0.15, 0.50, roughness);
ssrMask *= ssrData.a * ssrData.a;

// Fresnel-weighted blend (needs GBuffer normal + view direction)
float3 result = lerp(taaColor.rgb, ssrData.rgb, ssrMask);
```

**C++ changes**:
- `deferred_lighting_ps.hlsl`: Remove SSR composite (lines 429-455). IBL specular only.
- `frame_postprocess.cpp`: Add `SSR_Composite` pass AFTER TAA resolve.
- `frame_gi_passes.cpp`: SSR passes remain the same but output goes to post-TAA composite.
- Need to keep GBuffer roughness/normal SRVs alive through post-process.

**Visual tradeoff**:
- PRO: SSR noise COMPLETELY eliminated from TAA neighborhood. Clean rough surfaces.
- CON: SSR itself does not benefit from TAA temporal accumulation. SSR needs to be
  fully denoised by its own temporal + spatial filter. Any residual SSR noise will
  be visible as-is (no TAA smoothing).
- CON: SSR composite happens after TAA, so SSR edges may show sub-pixel aliasing
  that TAA would have smoothed. Mitigated by the SSR spatial denoiser.
- CON: Bloom sees non-SSR image (SSR specular highlights won't bloom). May need to
  reorder bloom to after SSR composite.

**Impact**: HIGHEST. Complete elimination of the cross-boundary problem.

**Complexity**: HIGH. Major pipeline restructuring, multiple shader and C++ changes.

---

### Solution E: Roughness-Aware TAA Neighborhood (ADVANCED)

**What**: Store roughness in a separate channel accessible to TAA. Use roughness difference
as an additional bilateral weight in the neighborhood AABB computation.

**Shader change** (`taa_resolve_cs.hlsl`):
```hlsl
// Sample roughness at center pixel
float centerRoughness = RoughnessTex.Load(int3(centerCoord, 0)).r;

for (uint i = 0; i < 9; i++) {
    int2 coord = ...;
    float sampleRoughness = RoughnessTex.Load(int3(coord, 0)).r;
    float roughnessDiff = abs(centerRoughness - sampleRoughness);
    // Reject neighbors with very different roughness (different material response)
    float roughnessWeight = exp(-roughnessDiff * roughnessRejectionScale);

    neighborWeights[i] = HdrWeightY(...) * depthWeight * roughnessWeight;
}
```

**C++ changes**: Bind GBuffer roughness as an additional SRV to the TAA compute pass.

**Visual tradeoff**: More ghosting at roughness boundaries (tighter AABB). This is
acceptable because roughness boundaries are typically material boundaries where ghosting
is preferred over noise.

**Impact**: MEDIUM-HIGH. Directly addresses the root cause at the roughness boundary.

**Complexity**: MEDIUM. TAA shader change + additional SRV binding.

---

## Recommended Implementation Order

1. **Solution C** (widen roughness fade) — 5 minutes, immediate improvement.
2. **Solution A** (SSR spatial bilateral filter) — 2-4 hours, major improvement.
3. **Solution B** (depth-aware TAA neighborhood) — 1-2 hours, helps depth boundaries.
4. **Solution E** (roughness-aware TAA) — 1-2 hours, helps coplanar roughness boundaries.
5. **Solution D** (post-TAA composite) — 4-8 hours, only if A+B+E are insufficient.

Solutions A+C together should eliminate 90%+ of the visible noise bleeding. Solution D
is the nuclear option if the first four are insufficient.

---

## Industry Consensus Summary

| Engine | SSR Denoiser | Independent from TAA? | Spatial Filter | Pipeline Position |
|--------|-------------|----------------------|---------------|-------------------|
| UE5 | Own temporal filter | Partially (SSR temporal is separate, but output enters TAA) | No dedicated spatial | SSR composited BEFORE TAA |
| SSSR (AMD) | 4-pass (temporal + spatial) | YES (fully independent history) | 7x7 bilateral, edge-aware | SSR composited BEFORE TAA, but fully denoised |
| Frostbite | Temporal + variance-guided spatial | YES (parallax-corrected reprojection) | Variance-adaptive bilateral | SSR composited BEFORE TAA, but fully denoised |
| NRD (NVIDIA) | Multi-stage hierarchical | YES (specular reprojection) | A-trous wavelet, edge-stopping | SSR composited BEFORE TAA, but fully denoised |
| Filament | None | No | None | SSR composited BEFORE TAA (noise accepted) |

**The universal pattern**: Every production engine applies SSR BEFORE TAA (it is part of
the indirect specular contribution that TAA should stabilize). The key differentiator is
whether the SSR output is **sufficiently denoised** before TAA sees it. SSSR, Frostbite,
and NRD all achieve this through independent temporal + spatial denoising. UE5 partially
achieves it through its SSR temporal filter but lacks a spatial denoiser (TSR compensates
with more aggressive outlier rejection).

No production engine applies SSR AFTER TAA as the default pipeline. The post-TAA composite
(Solution D) is theoretically clean but introduces other artifacts (no TAA on SSR edges,
no bloom on SSR highlights) that make it less practical than the "denoise before TAA" approach.

---

## Sources

- [Temporal AA and the Quest for the Holy Trail](https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/)
- [AMD FidelityFX SSSR](https://gpuopen.com/fidelityfx-sssr/)
- [AMD FidelityFX Denoiser](https://gpuopen.com/fidelityfx-denoiser/)
- [FidelityFX SSSR Manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)
- [Notes on SSR with FidelityFX SSSR (Interplay of Light)](https://interplayoflight.wordpress.com/2022/09/28/notes-on-screenspace-reflections-with-fidelityfx-sssr/)
- [Stochastic Screen-Space Reflections (Frostbite/EA)](https://www.ea.com/frostbite/news/stochastic-screen-space-reflections)
- [Stachowiak 2015 Slides (SlideShare)](https://www.slideshare.net/DICEStudio/stochastic-screenspace-reflections)
- [Temporal Antialiasing Starter Pack (Alex Tardif)](https://alextardif.com/TAA.html)
- [Intel TAA (GameTechDev)](https://github.com/GameTechDev/TAA)
- [NVIDIA NRD](https://github.com/NVIDIA-RTX/NRD)
- [REBLUR: A Hierarchical Recurrent Denoiser (Ray Tracing Gems II)](https://link.springer.com/content/pdf/10.1007/978-1-4842-7185-8_49.pdf)
- [Unreal Engine Rendering Passes](https://unrealartoptimization.github.io/book/profiling/passes/)
- [UE5 Rendering Pipeline Analysis](http://teres4enko.blogspot.com/2025/03/rendering-pipeline-ue5.html)
- [Filament SSR/TAA (GitHub Issue)](https://github.com/google/filament/issues/1164)
- [TAA Survey (Yang 2020)](https://onlinelibrary.wiley.com/doi/10.1111/cgf.14018)
- [TAA Survey Presentation Notes (Interplay of Light)](https://interplayoflight.wordpress.com/2020/05/30/a-survey-of-temporal-antialiasing-techniques-presentation-notes/)
- [Reprojecting Reflections (jpg's blog)](https://www.jpgrenier.org/reflections.html)
- [Stochastic SSR Implementation (Xerxes1138)](https://github.com/Xerxes1138/StochasticScreenSpaceReflection)
- [h3r2tic Stochastic SSR Grimoire](https://h3.gd/stochastic-ssr/)
