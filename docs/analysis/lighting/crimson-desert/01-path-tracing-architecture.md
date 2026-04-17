# Crimson Desert · Path Tracing Architecture

Extended analysis of the path-tracing dispatch at the core of BlackSpace's
lighting. This document details the ray graph, reservoir structures, and
GPU-side data layout that Pearl Abyss likely uses. Where concrete detail is
unknown, we describe the *most plausible* implementation based on published
state-of-the-art (SIGGRAPH 2020–2024) that matches the observed behavior.

---

## 1 · The ray graph

At every shaded pixel, the path tracer builds a graph of rays. One full frame
at 4K (max quality):

```text
                   ┌── raster G-Buffer ──┐
                   │ depth, N, albedo,   │
                   │ roughness, metallic │
                   │ + motion vector     │
                   └──────────┬──────────┘
                              │
              ┌───────────────┴──────────────┐
              │                              │
      ReSTIR DI reservoir            ReSTIR GI reservoir
      (best analytic/emissive        (best indirect path,
       light to shadow)               spatially/temporally
              │                        resampled)
              │                              │
      1 shadow ray /                  1 indirect ray /
      pixel / frame                   pixel / frame
              │                              │
              ├─── hit? ────► direct lighting  (at hit point)
              │                              │    reuses DI
              │                              │    reservoir at
              │                              │    hit location
              │                              │
              ▼                              ▼
         shadow mask              indirect diffuse radiance
              └──────────────┬──────────────┘
                             │
              ┌──────────────▼──────────────┐
              │        Roughness gate        │
              │    r < 0.5 → reflection ray  │
              │    r ≥ 0.5 → sky / IBL only  │
              └──────────────┬──────────────┘
                             │
                       reflection ray
                             │
                             ▼
              ┌──────────────────────────────┐
              │   Material gate (transmit)   │
              │  ior > 1 AND α < 1 → glass   │
              │  SSS flag → thin-sheet       │
              └──────────────┬───────────────┘
                             │
                transmission / SSS ray
                             │
                             ▼
                  raw HDR (noisy, 1 spp)
                             │
                             ▼
              ┌──────────────────────────────┐
              │   Volumetric in-scattering   │
              │ march along primary dir      │
              │ N samples × froxel LUT       │
              └──────────────┬───────────────┘
                             │
                             ▼
                  raw HDR + volumetrics
                             │
                             ▼
                    Ray Reconstruction
                             │
                             ▼
                         clean HDR
```

### 1.1 Why G-Buffer still exists

In a "pure" path tracer, the primary ray would be the G-Buffer. Pearl Abyss
keeps the rasterizer because:

- Triangle setup for a visible-pixel primary hit is **2–4× faster** on current
  HW than a ray traversal, even with Nanite-style virtual geometry.
- MSAA/coverage information is available for free on raster.
- Motion vectors are trivially computed in the vertex shader.

So the path tracer **starts from the G-Buffer** and only traces secondary rays.

### 1.2 Ray ordering and coherence

Each secondary ray type is dispatched in a **separate compute pass** to keep
rays coherent:

| Pass | Rays | Work group |
|------|------|------------|
| RT_Shadow       | analytic shadow rays  | 8×8 tile (reservoir selects one light per pixel, then 64 coherent rays) |
| RT_IndirectDiff | GI rays               | 8×8, cosine-weighted lobes aligned to G-Buffer N |
| RT_Reflection   | glossy reflections    | 8×8, VNDF-sampled around view-reflected direction |
| RT_Transmission | glass/refract         | sparse — only gated-in pixels |

Each pass also writes its own **temporal moments** buffers (1st/2nd moment of
luminance) used downstream for denoising.

---

## 2 · Reservoir data layout

Both ReSTIR DI and ReSTIR GI need a persistent per-pixel reservoir buffer. The
typical layout (matching Bitterli 2020 & Ouyang 2021 papers):

```hlsl
struct ReservoirDI
{
    uint2  lightSample;   // packed: lightIndex + uv / triangle bary
    float  weight;        // target pdf * W
    float  W;             // Bitterli "W" = sum / (M * target)
    float  M;             // accumulated sample count (bounded to 20)
    uint   frameAge;      // how old this reservoir is (for invalidation)
};

struct ReservoirGI
{
    float3 hitPoint;      // world-space
    float3 hitNormal;     // world-space (octahedral-encoded in 32b in prod)
    float3 L_in;          // incoming radiance at hitPoint
    float  weight;
    float  W;
    float  M;
};
```

Memory budget at 4K: `3840 × 2160 × (32 B + 40 B) = ~600 MB`. Crimson Desert
likely uses **half-precision** (fp16) for `L_in` and packs normals into
octahedral 16-bit, giving ~300 MB at 4K. Two buffers are needed (front/back
ping-pong) → ~600 MB reserved. Significant — but the whole point of ReSTIR is
that this memory replaces tens of thousands of extra rays per second.

### 2.1 Temporal reuse

At frame N+1:

1. Motion-vector reproject previous-frame reservoirs into current-frame pixel coordinates.
2. Discard reservoirs where reprojected depth/normal mismatches by more than a threshold (disocclusion).
3. MIS-combine reprojected reservoir with a fresh (current-frame) candidate.
4. Bound M to prevent runaway weight (cap ≈ 20).

### 2.2 Spatial reuse

Single pass, ~5 neighbors in a Poisson-distributed disk kernel. Again MIS-combined.

### 2.3 Bias correction

ReSTIR's MIS weights require target-PDF evaluation at the destination pixel
(Talbot-style MIS). This is the ~30% of the cost of ReSTIR. Pearl Abyss almost
certainly uses the **optimized combine-style MIS** from GRIS (Lin et al. 2022)
rather than naive pairwise MIS.

---

## 3 · BVH management

For a per-pixel RTGI system to work at 4K/60, the BVH pipeline must be
aggressively engineered.

### 3.1 Two-level acceleration structure (TLAS + BLAS)

- **TLAS** = per-instance transform, rebuilt every frame for dynamic objects.
- **BLAS** = per-mesh geometry, rebuilt only when geometry changes.

### 3.2 Skeletal meshes

Skeletal BLAS is **refit** (not rebuilt) every frame: the triangle positions
are re-evaluated in a compute shader, then the BVH is refit (node AABBs
recomputed along the tree). Refit is O(nodes) vs O(n log n) full rebuild and
runs in ~0.3 ms for a typical skinned character.

Quality degrades over many refits (AABBs get loose); Pearl Abyss likely does a
full rebuild every K frames (K ≈ 30) on a rotating schedule.

### 3.3 Nanite-equivalent geometry

BlackSpace uses cluster-LOD virtual geometry (similar to UE5 Nanite). For ray
tracing this is a known challenge — you cannot put cluster LOD into a BLAS
directly (BLAS builder wants fixed triangles). Solutions:

1. **Proxy BLAS**: a lower-resolution (~10k triangle) mesh per instance, rebuilt when LOD bucket changes.
2. **Chunked BLAS**: the full cluster hierarchy is compiled into a fixed BLAS per LOD bucket, and the TLAS picks the right one per instance.

Recent NVIDIA papers (RTX Mega Geometry) use variant 2 with mesh shaders
driving BVH updates. Pearl Abyss's 2025 GDC showcase emphasized cluster LOD
working through path tracing, so they have solved this.

### 3.4 Foliage

Grass and dense foliage are the hardest case: millions of instances, short
lifetime, many triangles.

- Likely handled with **fixed proxy BLAS** per foliage type.
- TLAS grows with instance count → compressed instance format (UE5's FInstancedStaticMeshComponent equivalent).
- Distant foliage is often **not** in the BVH at all, falling back to screen-space + skylight.

---

## 4 · Light sampling

### 4.1 Light BVH

Emissive geometry is gathered into a separate light-BVH (Conty & Kulla 2018).
This structure weighs each node by its total emitted power, so that light
importance sampling correctly selects among 10k+ emissive triangles.

### 4.2 Analytic lights

Treated as "infinitely small emissive primitives" injected into the light BVH.
A point light is just a 1-triangle surface with a delta-flag in the material.

### 4.3 Sky / environment

The sky is a distant environment map (either baked from the sky atmosphere
LUT or the LUT directly). Importance-sampled via a **hierarchical warp map**
(Clarberg 2005). This is standard.

---

## 5 · Denoising signal flow

Even with ReSTIR, the 1 spp/pixel signal is noisy. The denoiser input/output:

```text
Input:
  - noisy diffuse GI       (HDR, fp16)
  - noisy specular         (HDR, fp16)
  - noisy shadow mask      (R8)
  - motion vectors         (RG16_SNORM)
  - depth                  (D32F)
  - world normal           (RG16F octahedral)
  - albedo                 (RGB8 sRGB)
  - roughness + metallic   (RG8)
  - demodulation bufs      (diffuse ÷ albedo, specular ÷ fresnel)
  - history (clean prev)   (HDR, fp16)

Output:
  - clean HDR at output resolution (HDR, fp16)
```

**Demodulation** is key: dividing out the albedo and fresnel before denoising
removes high-frequency material detail from the noisy signal, letting the
denoiser operate on a smoother, easier-to-denoise lighting-only signal. After
denoising, the material detail is multiplied back in. This is now standard
practice (Schied 2017 SVGF, Zwicker NRD).

---

## 6 · GPU dispatch timeline

Approximate 4K frame at 60 fps on RTX 4080 class GPU:

| Time | Pass | Duration |
|------|------|----------|
| 0.0 ms | G-Buffer (raster)              | 2.0 ms |
| 2.0 ms | Shadow maps (raster for fallback) | — (skipped in RT mode) |
| 2.0 ms | TLAS refit                     | 0.3 ms |
| 2.3 ms | Light BVH update               | 0.2 ms |
| 2.5 ms | ReSTIR DI initial + shadow ray | 1.5 ms |
| 4.0 ms | ReSTIR DI spatial + temporal   | 0.8 ms |
| 4.8 ms | ReSTIR GI (indirect ray)       | 3.0 ms |
| 7.8 ms | ReSTIR GI resampling           | 1.2 ms |
| 9.0 ms | Reflections (stochastic)       | 1.8 ms |
| 10.8 ms | Volumetric fog march          | 1.4 ms |
| 12.2 ms | DLSS Ray Reconstruction       | 2.5 ms |
| 14.7 ms | Tonemap + bloom + DoF         | 1.0 ms |
| 15.7 ms | UI / present overhead         | 0.9 ms |
| 16.6 ms | **Total (60 fps)**            | — |

This is tight. Every budget item above has an upper bound in the BlackSpace
design. Pearl Abyss's engineering bet is that **every future GPU generation
improves ray tracing and tensor-core throughput** — so this budget scales
down automatically.

---

## 7 · What UzEngine can borrow (and what it cannot)

| Technique | Can we adopt? | Where |
|-----------|---------------|-------|
| ReSTIR DI | **YES** — we already have primitives, just need the reservoir buffers | Replaces current per-pixel loop over all lights |
| ReSTIR GI | **YES** — after we have an RT fallback path | Complements SSGI in the diffuse path |
| Per-pixel RT primary | No — requires mandatory RT-capable HW | SSGI + probe fallback stays for SM6 path |
| DLSS-RR integration | Partial — NVIDIA NGX SDK is drop-in but requires full rewrite of denoiser inputs | Feasible in a second phase |
| Light BVH | **YES** — independent of path tracing, useful for many-light scenes | Implement once for both RT and non-RT paths |
| Stochastic reflections | **YES** — our current SSR is deterministic; stochastic-VNDF + denoise is strictly better | Rewrite SSR to stochastic |
| Volumetric fog froxel + stochastic sky | **YES** — we partially have this; improve sampling & temporal accum | Tune existing volumetric-fog pass |

Crimson Desert's architectural genius is not any one technique, it is the
*unification*. Every shading term is just "estimate radiance by resampling
paths." UzEngine's renderer, today, is a classic "direct + IBL + SSGI + probe"
split. Part of the new pipeline plan is to collapse these into a single
unified estimator.
