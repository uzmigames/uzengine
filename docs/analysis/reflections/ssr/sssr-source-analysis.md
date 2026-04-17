<!-- doc-meta
title: AMD FidelityFX SSSR Source Code Analysis
created: 2026-04-05
status: complete
source: github.com/GPUOpen-Effects/FidelityFX-SSSR (MIT license)
-->

# AMD FidelityFX SSSR — Source Code Analysis

Research from 4 agents analyzing the full SSSR pipeline source code.

## Pipeline Overview

```
ClassifyTiles → PrepareIndirectArgs → Intersect(VNDF+HiZ) → Reproject → ResolveTemporal → Prefilter(spatial)
```

All denoiser passes dispatch via ExecuteIndirect on tile lists built by ClassifyTiles.

## Key Architecture Decisions

### Tile Classification
- 8x8 tiles, `[numthreads(8,8,1)]`
- Ray compaction via `WavePrefixCountBits` + `InterlockedAdd`
- Packed ray coords: `x[14:0] | y[28:15] | copy_h[29] | copy_v[30] | copy_d[31]`
- Sub-sampling: `g_samples_per_quad` = 1/2/4 rays per 2x2 quad
- Non-traced pixels copy results from traced neighbors
- Mirror reflections always trace regardless of sub-sampling
- Variance-guided re-trace: high-variance pixels forced to trace even if culled

### Ray Counter Buffer Layout
- `[0]`: Ray count (classify writes, PrepareIndirectArgs reads/resets)
- `[1]`: Ray count copy (for Intersect to read)
- `[2]`: Denoiser tile count
- `[3]`: Denoiser tile count copy

### Intersect (VNDF + Hi-Z)
- VNDF: Heitz 2018 JCGT original method (hemisphere projection, T1/T2 basis)
- Blue noise: 128x128 RG Texture2D, `pixel % 128`, no temporal offset in lookup
- Hi-Z march in screen UV+depth [0,1]^3 with hierarchical mip stepping
- UV offset: `0.005 * exp2(mip) / screen_size` to avoid cell boundary sticking
- Wave occupancy early exit: `WaveActiveCountBits(true) <= g_min_traversal_occupancy`
- Output: `float4(radiance.rgb, world_ray_length)` — ray length in `.w`

### Hit Validation
- Float confidence [0,1], not boolean
- Reject: outside [0,1] UV, manhattan < 2px (self-hit), background depth, backface
- Thickness fade: `(1 - smoothstep(0, depth_buffer_thickness, distance))^2`
- Border vignette: 5% screen edge fade

### Temporal Denoise (Two-Pass)
**Pass 1 — Reproject:**
- Virtual hit reprojection: surface_depth + ray_length → extend along view ray → reproject via prevVP
- Surface reprojection fallback via motion vectors
- Selection: hit normal similarity > 0.9999 beats surface method
- Disocclusion: `exp(-|1-dot(N,N_hist)|*1.4) * exp(-|d_hist-d|/d)`, threshold 0.9
- 3x3 neighborhood search on disocclusion failure

**Pass 2 — ResolveTemporal:**
- Linear RGB variance clip (NOT YCoCg), Gaussian-weighted 9x9 (radius=4)
- Per-pixel sample count accumulation, capped: `max(8, 32 * (1-exp(-r*100)))`
- Blend weight: `1 / max(num_samples, 1)`
- AABB clip via Playdead method (center → sample intersection)

### Spatial Prefilter
- 15+1 Halton samples in fixed 3px radius, loaded via 16x16 groupshared
- Normal weight: `pow(dot, 512)` — extremely sharp edge preservation
- Depth weight: `exp(-|d_center - d_neighbor| * d_center * 4.0)`
- Radiance weight: `max(exp(-(0.6 + variance*0.1) * |rad_center - rad_neighbor|), 0.01)`
- Variance-adaptive: `max(0.1, 1 - exp(-variance * 4.4))`
- NO Karis tonemap — uses average radiance texture instead
- Mirror surfaces skip entirely (roughness < 0.0001)
- Roughness does NOT modulate kernel radius (fixed 3px)

## Constants Reference

| Constant | Value | Pass |
|----------|-------|------|
| Mirror threshold | 0.0001 | All |
| PREFILTER_NORMAL_SIGMA | 512.0 | Spatial |
| PREFILTER_DEPTH_SIGMA | 4.0 | Spatial |
| PREFILTER_VARIANCE_WEIGHT | 4.4 | Spatial |
| PREFILTER_VARIANCE_BIAS | 0.1 | Spatial |
| RADIANCE_WEIGHT_BIAS | 0.6 | Spatial |
| RADIANCE_WEIGHT_VARIANCE_K | 0.1 | Spatial |
| DISOCCLUSION_NORMAL_WEIGHT | 1.4 | Temporal |
| DISOCCLUSION_DEPTH_WEIGHT | 1.0 | Temporal |
| DISOCCLUSION_THRESHOLD | 0.9 | Temporal |
| VARIANCE_CLIP_GAMMA (implied) | ~1.4× via history_clip_weight | Temporal |
| REPROJECTION_NORMAL_SIMILARITY | 0.9999 | Temporal |
| max_traversal_intersections | ~128 (cbuffer) | Intersect |
| min_traversal_occupancy | ~4 (cbuffer) | Intersect |
| max_samples | 32 | Temporal |
| Blue noise tile | 128x128 RG | Intersect |

## CBuffer Layout (shared across all passes)

```
float4x4 g_inv_view_proj;           // 0
float4x4 g_proj;                    // 64
float4x4 g_inv_proj;                // 128
float4x4 g_view;                    // 192
float4x4 g_inv_view;                // 256
float4x4 g_prev_view_proj;          // 320
uint2    g_buffer_dimensions;        // 384
float2   g_inv_buffer_dimensions;    // 392
float    g_temporal_stability_factor;// 400
float    g_depth_buffer_thickness;   // 404
float    g_roughness_threshold;      // 408
float    g_temporal_variance_threshold;// 412
uint     g_frame_index;              // 416
uint     g_max_traversal_intersections;// 420
uint     g_min_traversal_occupancy;  // 424
uint     g_most_detailed_mip;        // 428
uint     g_samples_per_quad;         // 432
uint     g_temporal_variance_guided_tracing_enabled;// 436
// Total: 440 bytes
```

## UzEngine Implementation Status

| SSSR Feature | UzEngine Status | Notes |
|-------------|----------------|-------|
| VNDF sampling | Done (Dupuy 2023 spherical caps) | Different method, same result |
| Blue noise | Done (temporal IGN) | Procedural, no texture needed |
| Hi-Z march | Pending (3.10) | Currently flat linear march |
| Tile classify | Pending (3.7) | Currently dispatches all pixels |
| Virtual hit reproject | Done | Via prevVP CBV |
| Per-pixel sample count | Not implemented | Using roughness proxy |
| Variance-guided re-trace | Not implemented | Enhancement |
| Samples per quad | Not implemented | Enhancement |
| Wave occupancy exit | Not implemented | Optimization |
