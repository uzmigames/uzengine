<!-- doc-meta
title: Next-Gen Reflections Analysis: Beyond Standalone SSR (2025)
subsystem: renderer/reflections
status: research
date: 2026-04-05
author: Andre Ferreira
-->

# Next-Gen Reflections Analysis: Beyond Standalone SSR (2025)

A comprehensive analysis of where screen-space reflections are heading, how modern AAA
engines have evolved beyond standalone SSR, and the recommended architecture for a DX12
engine targeting 2025-2026.

---

## 1. The Problem With Standalone SSR

Traditional SSR (linear or Hi-Z raymarched) has one hard constraint: it can only reflect
what is currently on screen. Rays that exit the viewport, hit an occluder behind geometry
already on screen, or point toward the sky return no hit, and the fallback (usually the
skybox or a reflection capture cubemap) is jarring. For rough/glossy surfaces with wide
reflection cones, many rays fail, requiring many samples and expensive temporal denoising.

**Core limitations of standalone SSR:**

| Failure Mode | Cause | Visible Artifact |
|---|---|---|
| Off-screen geometry | Ray exits viewport | Hard cutoff at screen edge |
| Backface geometry | Ray hits back of mesh | Black or wrong-color band |
| Self-occlusion | Ray blocked by same mesh | Contact shadow-like darkening |
| Glossy noise | Many jittered rays needed | Temporal shimmer / fireflies |
| Mirror surfaces | Need near-zero-radius cone | Extremely noisy without 32+ spp |
| Moving objects | History invalidation | Ghosting or boiling |

These are not implementation quality issues — they are fundamental to the screen-space
approximation. Every major AAA engine circa 2023-2026 uses SSR only as **one layer of a
multi-tier system**, never as the sole reflection path.

---

## 2. UE5 Lumen Reflections: The Layered Architecture

Lumen is the most thoroughly documented next-gen reflection system. It defines the
industry pattern that everyone else follows or approximates.

### 2.1 The Four-Layer Tracing Hierarchy

Lumen reflections operate as a priority cascade — each layer fills in what the previous
layer misses:

```
Layer 1 (fastest):  Screen-Space Trace
                       Hit? → use screen radiance (full material fidelity, free)
                       Miss? → fall through

Layer 2 (fast):     Mesh Signed Distance Field (SDF) Trace
                       Hit? → look up Surface Cache lighting
                       Miss? → fall through

Layer 3 (moderate): Voxel / Global Distance Field Trace
                       Used for large-scale light transfer, sky-facing rays

Layer 4 (fallback): World Radiance Cache
                       Pre-integrated distant lighting from irradiance probes
```

When hardware RT is enabled, layers 2 and 3 are replaced by triangle-accurate BVH traces.
With hardware RT + Hit Lighting, the Surface Cache lookup (a pre-baked low-res scene
representation) is replaced with full shader evaluation at the hit point.

### 2.2 Screen Trace (Layer 1)

The screen trace in Lumen is **not standard SSR**. Key distinctions:

- Uses a Hi-Z (hierarchical depth) mip pyramid for fast long-distance skipping
- Traces at **half resolution** for most surfaces, reconstructed with temporal
- Captures sub-pixel geometry detail (nanite meshes render at full res into depth)
- Returns not just a color but a **hit radiance sample** from the current GBuffer
- Integrates **screen-space occlusion** for the first bounce

The CVar controlling this is `r.Lumen.Reflections.ScreenTraces` (0 = off, 1 = on).

### 2.3 Surface Cache (Layer 2 output)

The Surface Cache is Lumen's pre-baked, dynamically updated **low-resolution scene atlas**.
Think of it as a 256x256 per-object "mini render" that stores direct + indirect lighting
per-texel. When a software SDF trace hits a mesh, the cache provides lighting without
re-evaluating the full material shader.

- Avoids re-running expensive material shaders for every reflection ray
- Updated at a limited rate (a few hundred objects per frame), not every-frame
- Quality limitation: the simplified representation reveals inaccuracy on smooth mirrors
- Sharp mirrors require hardware RT + Hit Lighting to bypass the Surface Cache entirely

### 2.4 Screen-Space Radiance Cache

For rough surfaces (roughness > ~0.4), Lumen uses a **screen-space radiance cache**
(screen probes) rather than per-pixel tracing:

- Screen probes are placed at intervals across the visible surface grid (sparse)
- Each probe traces a set of short rays into the scene (SDF or hardware BVH)
- Probe results are spatially interpolated and temporally accumulated across frames
- **Rough-surface reflections reuse the same probes as indirect diffuse GI** — zero
  marginal cost for the second diffuse-in-reflection bounce

This is how Lumen achieves clear-coat / double-layer reflections without doubling cost.

### 2.5 World Radiance Cache

A separate, low-spatial-resolution (1/256 scene resolution) world-space irradiance probe
grid captures distant lighting at each probe position. This is the final fallback for:

- Sky-facing rays (horizon and upward)
- Rays that escape the mesh SDF cascade
- Secondary/tertiary bounce lighting for diffuse probes

The World Radiance Cache has **higher directional resolution but lower spatial resolution**
than the Screen Radiance Cache. It uses importance sampling to project the previous frame's
screen radiance onto the current frame's rays, providing temporal stability for the probe grid.

### 2.6 Hit Lighting vs. Surface Cache

| Mode | Quality | Performance | Use Case |
|---|---|---|---|
| Surface Cache | Low (simplified scene rep) | Very fast | All hardware, all roughness |
| Software Hit Lighting | Medium | Moderate | SDF hit, full material eval |
| Hardware RT + Surface Cache | High | Moderate | RTX cards, smooth surfaces |
| Hardware RT + Hit Lighting | Highest (triangle precise) | Expensive | Epic scalability / high-end |

Key fact: cost of Lumen reflections scales with screen-space coverage of smooth surfaces.
Roughness > 0.4 routes to the probe path (cheap). Roughness < 0.4 traces per-pixel (expensive).

### 2.7 Temporal Stability

Lumen's temporal stability for reflections comes from three mechanisms:

1. **Screen Radiance Cache temporal accumulation** — probe results blend across frames
2. **TSR (Temporal Super Resolution)** — jitter-aware upsampling smooths reflection noise
3. **Importance sampling from previous frame** — projects last frame's lighting onto current
   rays, reducing the variance of new ray directions

The result: Lumen reflections have dramatically less temporal noise than standalone SSR
with equivalent per-frame ray budgets. The noise floor is managed at the probe level,
not per-pixel.

---

## 3. AMD FidelityFX Hybrid Reflections: The Open Alternative

AMD published the **Hybrid Stochastic Reflections** sample as part of FidelityFX SDK,
demonstrating the canonical hybrid SSR + RT architecture without proprietary tooling.

### 3.1 Architecture

```
Step 1: Classify pixels
        - Build roughness/metallic masks
        - Identify pixels where RT is needed vs SSR is sufficient
        - Very smooth (roughness < 0.05) → RT
        - Moderately rough → SSSR (stochastic SSR)
        - Very rough → skip reflections or use IBL only

Step 2: AMD SSSR (Stochastic Screen Space Reflections)
        - Hi-Z hierarchical depth traversal kernel
        - Ray direction jittered by roughness cone
        - Returns hit color + hit confidence mask (1.0 = confident hit, 0.0 = miss)

Step 3: For SSSR misses: Ray tracing fallback
        - Dispatch RT ray for each pixel where SSSR hit confidence < threshold
        - Ray traces against scene BVH (DXR / Vulkan RT)
        - Returns hit color from ray tracing

Step 4: Composite
        - Blend SSSR and RT results using hit confidence mask
        - Apply temporal denoiser (AMD FidelityFX DENOISER or NRD)

Step 5: Temporal denoising
        - Spatio-temporal filter accumulates history
        - Per-pixel history weight based on motion vectors + depth continuity
```

### 3.2 Why This Is Better Than Pure SSR

The critical insight: **screen space already contains the best quality lighting data for
nearby surfaces**. Re-tracing those rays with hardware BVH would waste RT budget to get
the same answer. The hybrid pattern extracts maximum value from both:

- SSR handles the ~70-80% of rays that find a valid on-screen hit (free, full material quality)
- RT handles the ~20-30% of rays that escape the screen (accurate, but lower frequency)

Result: similar or better visual quality than pure RT at roughly 20-30% of the RT cost.

### 3.3 SSSR Technical Details

AMD SSSR distinguishes itself from naive Hi-Z SSR through:

- **Hierarchical depth traversal**: starts at coarse mip, descends only on near-hits
  → larger steps on flat/distant surfaces, precise steps near intersections
- **Denoiser specifically optimized for stochastic reflection noise**: separates
  specular radiance from hit distance, uses hit distance to guide the spatial filter
- **Roughness-scaled ray count**: mirror surfaces dispatch fewer, more coherent rays;
  rough surfaces dispatch many jittered rays but rely heavier on denoising

---

## 4. NRD (NVIDIA Real-Time Denoiser) for RT Reflections

When RT reflections are used (either pure or hybrid), denoising is non-optional.
NRD is the industry-standard open-source denoising library for DX12/Vulkan.

### 4.1 NRD Reflection Denoisers

NRD provides specialized denoisers for reflections:

| Denoiser | Input | Use Case |
|---|---|---|
| `REBLUR` | Radiance + hit distance | Best for rough/glossy reflections |
| `RELAX` | Radiance + hit distance | Better for mirror-like surfaces |
| `SIGMA` | Shadow | Not for reflections |

For specular/reflections, **hit distance is a critical input** — the denoiser uses it to
distinguish near-surface noise from distant-geometry noise, improving the spatial filter's
edge-stopping function.

### 4.2 Primary Surface Replacement (PSR)

For near-perfect mirrors, NRD supports a PSR mode where the denoiser "sees" the first
non-mirror bounce point after a chain of perfect mirror reflections. This avoids denoising
the mirror surface itself (which would blur the reflection) and instead denoises the
terminal hit point, producing cleaner mirror reflections.

### 4.3 DLSS 3.5 Ray Reconstruction

NVIDIA's DLSS 3.5 introduced **Ray Reconstruction** — an AI-based denoiser that replaces
NRD's hand-tuned spatio-temporal filters with a trained neural network. Cyberpunk 2077
with Overdrive mode uses this for path-traced reflections. Results: lower blur at edges,
better temporal stability, no parameter tuning.

Not recommended for engines targeting broad hardware (requires RTX 2000+ and DLSS 3.5
integration) but represents the direction for high-end rendering.

---

## 5. ReSTIR / RTXDI for Reflections

ReSTIR (Spatiotemporal Reservoir Importance Resampling, Bitterli et al. 2020) is the
Monte Carlo sampling breakthrough underlying modern RT rendering:

- Maintains reservoirs (weighted sample pools) per pixel
- **Temporal reuse**: the previous frame's reservoir is resampled as a candidate for the
  current frame → effectively multiplies sample count across frames
- **Spatial reuse**: neighboring pixels share reservoir candidates → correlated noise
  averages out, dramatically lowering variance

RTXDI extends ReSTIR to direct illumination at scale (millions of lights). For reflections
specifically, **ReSTIR GI** applies the same reservoir reuse to indirect/specular paths:

- Each pixel traces 1 ray, populates a reservoir
- Spatial + temporal resampling produces the equivalent of 8-64 rays of quality
- Works best with hardware RT (BVH traversal for each reservoir candidate)

### ReSTIR for non-RT engines

The conceptual core of ReSTIR (temporal reservoir accumulation) applies even without
hardware RT. The spatial-temporal denoiser in AMD SSSR and NRD both exploit the same
principle: **reuse work from neighboring pixels and prior frames to amplify the apparent
sample count**.

---

## 6. Probe-Based Systems: DDGI and AMD Brixelizer GI

### 6.1 NVIDIA DDGI

DDGI (Dynamic Diffuse GI, Morgan McGuire et al. 2019) places irradiance probes in a
regular world-space grid. Each probe stores:

- Spherical harmonic irradiance (diffuse incoming light from all directions)
- Mean distance (for visibility testing — avoids light leaking through walls)

DDGI probes are updated via ray tracing each frame (sparse updates). They provide
continuous dynamic indirect diffuse lighting without baking.

**For reflections**: DDGI itself is diffuse — it does not replace SSR for glossy/mirror
reflections. However, it provides accurate diffuse color for surfaces seen in reflections,
improving the quality of what SSR/RT captures at hit points.

### 6.2 AMD Brixelizer GI (FidelityFX, GDC 2024)

Brixelizer GI is AMD's open-source compute-based GI system, released in FidelityFX SDK
1.1. It uses:

- **Sparse voxel distance fields** around the camera (cascaded, 64x64x64 per brick)
- **Screen-space probes** (two-level caching: screen + world space irradiance/radiance)
- **Brixelizer SDF traces** to find lighting hits without hardware BVH
- Previous frame's composited output as a radiance input → multi-bounce GI "for free"

Brixelizer GI targets **diffuse + specular GI** (the "specular" here = glossy indirect,
not sharp mirror). It runs entirely on compute shaders — no hardware RT required.

**Performance**: ~3.5-4.2ms at 1080p on an RX 6900 XT for a complete diffuse+specular
GI pass including denoising.

**Architecture alignment with Lumen**: Almost identical two-level probe caching concept:
screen probes feed a world-space radiance cache, which is the fallback for distant rays.
AMD published this openly as FidelityFX, making it the non-Lumen open-source analog.

### 6.3 AMD GI-1.1 (Glossy Reflections)

GI-1.1 (SIGGRAPH Asia 2023) extends GI-1.0 specifically to glossy reflections:

- Fires reflection rays from screen probes (already placed for diffuse)
- Importance samples the reflection cone given surface roughness
- Caches results in a specular radiance cache per probe
- **Crucially: no extra ray budget** for rough reflections — they reuse probe positions
  already computed for diffuse GI

This is the open research equivalent of Lumen's "rough-surface reflections reuse the
diffuse probe path."

---

## 7. Production Case Studies

### 7.1 Cyberpunk 2077 (CD Projekt Red / REDengine)

- **Base rendering**: SSR + planar reflections + reflection captures for non-RT
- **RT reflections mode**: Per-pixel ray traced reflections against full scene BVH
- **Path Tracing Overdrive**: Full multi-bounce path tracing using ReSTIR GI + DLSS 3.5
  Ray Reconstruction
- Even in Overdrive mode, SSR data is composited in (was temporarily disabled in patch 2.1)
  to leverage screen-space detail quality
- RT reflections fixed "awful SSR artifacts" visible in Death Stranding-style water/tar

### 7.2 God of War: Ragnarok (Santa Monica Studio)

- GDC 2023 (Stephen McAuley): SSR as primary reflection path on PS5
- Cubemaps as fallback where SSR fails
- No hardware RT on PS5 (as of launch) — pure rasterized pipeline
- SSR mostly on water, mirrors, metallic surfaces
- Setting up "reflections and refractions to help environment artists" = artist-driven
  reflection capture placement workflow alongside SSR

Ragnarok demonstrates that **a polished AAA rasterized game in 2022 still ships SSR as
its primary technique** — but only on a platform (PS5) with no RT support, and even there
it required careful art direction and reflection capture authoring to fill SSR gaps.

### 7.3 Death Stranding 2 (Kojima Productions / Decima Engine)

- **DS1 (rasterized)**: SSR-only, with documented "awful artifacts" on water/tar surfaces
- **DS2 (PC, 2024)**: First Decima game with hardware RT on PC
- RT reflections applied specifically to water and tar surfaces where SSR fails worst
- Confirms: studios add RT reflections **where SSR artifacts are most visible**, not
  as a wholesale replacement

### 7.4 Avowed (Obsidian / UE5 with Lumen)

- Ships with Lumen reflections enabled (the UE5 default)
- Screen Space Reflections option available in settings for performance
- Some players note SSR visible even with Lumen enabled (Lumen uses SSR as its Layer 1)
- Confirms UE5's design: Lumen subsumes SSR into itself; you don't choose between them

---

## 8. Industry Trend Analysis

### 8.1 Is Standalone SSR Being Abandoned?

**Not abandoned, but demoted.** The pattern across every major engine in 2025:

```
2018-2020: SSR is primary, cubemaps are fallback
2021-2023: SSR is Layer 1, SDF/probe/RT fill misses (Lumen, DDGI, Brixelizer)
2024-2026: SSR is still Layer 1, but engines have 3-4 more layers above it
```

No AAA engine ships without SSR. Every engine that ships Lumen-class reflections still
includes SSR as the first trace in the cascade. The reason is fundamental: screen-space
data is the highest-fidelity lighting source available (full material + dynamic lighting),
and ray tracing it costs zero GPU budget if the data is already there.

What has changed: SSR is no longer the **only** layer. The question for an engine design
is not "use SSR or not" but "what do you do when the SSR ray misses?"

### 8.2 Recommended Reflection Architecture for a DX12 Engine (2025)

A DX12 engine without hardware RT support should implement this cascade:

```
Tier 1 (immediate):  Screen-Space Trace (Hi-Z SSR)
                       - Hi-Z hierarchical depth mip pyramid
                       - Roughness-scaled jitter
                       - Returns hit color + hit confidence
                       - Cost: O(log2(max_steps)) per ray

Tier 2 (world):      Parallax-Corrected Reflection Captures (artist-placed)
                       - Box-projected environment cubemaps
                       - Applied where screen trace misses
                       - Low cost, correct for box-shaped rooms/objects
                       - Blend multiple captures by distance + weight

Tier 3 (sky/distant): Sky + IBL (split-sum BRDF LUT + prefiltered envmap)
                       - Handles sky-facing rays, open environments
                       - Already present in any PBR renderer

Tier 4 (optional upgrade): Software SDF Trace (Brixelizer-style)
                       - Compute-based sparse voxel trace
                       - No hardware RT required
                       - Provides the "misses" that screen trace can't catch

Tier 5 (future RT):  DXR Ray Tracing overlay
                       - Dispatch RT only for SSR-miss pixels
                       - Drop into same composite pipeline as Tier 1
                       - Requires no redesign of Tiers 1-3
```

### 8.3 Key Design Decisions for Future-Proofing

**1. Separate hit confidence from hit color**

Every tier should output `(color, confidence)` pairs. The compositor merges tiers using
confidence as blend weight. This makes adding new tiers additive — a new tier 4.5 can
be inserted between existing tiers without changing their logic.

**2. Design the denoiser interface upfront**

The denoiser needs: reflection radiance, hit distance, roughness, normal, depth, motion
vectors. Even if tier 1 (SSR) never uses a denoiser, design the pass to output these
GBuffer-adjacent channels so a future NRD integration requires only adding the denoiser
pass, not restructuring the reflection pass.

**3. Roughness routing**

Route at roughness boundaries, not as a global switch:
- roughness > 0.75: environment probe + IBL only (diffuse-dominant, no SSR noise needed)
- 0.4 < roughness < 0.75: SSR with high jitter + strong temporal denoising
- 0.1 < roughness < 0.4: SSR with moderate jitter + temporal denoising
- roughness < 0.1: SSR treated as near-mirror (no jitter, temporal accumulation)

**4. Never replace SSR with RT — replace RT with SSR first, then extend**

The Hybrid Reflections pattern is: trace screen first, trace RT only for misses. An
engine that has SSR + the miss-detection infrastructure can integrate RT by adding a
`if (sssr_miss) { dispatch_rt(); }` branch. An engine that skips SSR and does only RT
will be slower and miss the high-frequency screen-space detail.

**5. Temporal history is the denoiser**

For an engine without NRD, good temporal accumulation (with disocclusion detection via
depth reprojection and stencil marks for moving objects) provides most of the denoising
benefit. Per-pixel hit distance clamping (reject history when distance > threshold)
prevents ghosting from moving reflectors. This is implementable in HLSL without any
external library.

---

## 9. UzEngine Implementation Roadmap

Based on the above analysis, the recommended implementation order for UzEngine is:

### Phase 1: High-Quality Hi-Z SSR (immediate value)

Improve the existing SSR with the AMD SSSR pattern:
1. Build Hi-Z depth mip pyramid (compute pass, max-reduction)
2. Replace linear raymarch with Hi-Z traversal
3. Add roughness-scaled jitter (importance sample the reflection cone)
4. Add hit confidence mask output
5. Add temporal denoising with history invalidation on disocclusion

This directly addresses current SSR noise and cutoff artifacts without requiring any
new systems.

### Phase 2: Reflection Capture System (artist workflow)

1. `ReflectionCaptureComponent` with box volume (parallax correction) and sphere capture
2. Per-frame cubemap composite: SSR (confidence 1.0) -> nearest capture (fallback)
3. Blend multiple overlapping captures by weight/distance
4. Editor: capture-to-disk workflow, cubemap viewer

This fills SSR gaps for interiors and local environments — the most visible failure case.

### Phase 3: Probe-Based World Radiance Cache

1. Sparse irradiance probe grid (DDGI-style or Brixelizer GI-style)
2. Probes updated via compute-based SDF trace (Brixelizer) or screen-space probe sampling
3. Screen probes on visible surfaces → world irradiance cache for distant lighting
4. Rough-surface reflections (roughness > 0.4) route to probe cache, not per-pixel SSR

This is the step that removes the hard "sky is black at edges" artifact for rough metals.

### Phase 4: DXR Hybrid Integration (RT hardware required)

1. Add `--cvar r.Reflections.UseRT=1` toggle at engine startup
2. Dispatch DXR ray generation only for pixels where SSR hit confidence < 0.5
3. Blend RT color with SSR color using confidence weight
4. Integrate NRD `REBLUR` or `RELAX` denoiser for RT contribution only
5. The SSR path is unchanged — RT is purely additive

The architecture from Phases 1-3 is designed so Phase 4 requires no refactoring of
existing code — only a new parallel ray generation pass and a denoiser.

---

## 10. Key Published References

| Technique | Reference | URL |
|---|---|---|
| Lumen technical details | Wright et al., SIGGRAPH 2022 | https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf |
| AMD SSSR | AMD FidelityFX GPUOpen | https://gpuopen.com/fidelityfx-sssr/ |
| AMD Hybrid Reflections | AMD FidelityFX GPUOpen | https://gpuopen.com/fidelityfx-hybrid-reflections/ |
| AMD GI-1.0 two-level caching | arXiv 2023 | https://arxiv.org/abs/2310.19855 |
| AMD GI-1.1 glossy reflections | SIGGRAPH Asia 2023 | https://dl.acm.org/doi/abs/10.1145/3610543.3626167 |
| AMD Brixelizer GI | GDC 2024 GPUOpen | https://gpuopen.com/fidelityfx-brixelizer/ |
| NVIDIA NRD denoiser | GitHub NVIDIA-RTX | https://github.com/NVIDIA-RTX/NRD |
| NVIDIA DDGI probes | NVIDIA Research 2019 | https://research.nvidia.com/publication/2019-05_dynamic-diffuse-global-illumination-ray-traced-irradiance-fields |
| ReSTIR spatiotemporal resampling | Bitterli et al. 2020 | https://research.nvidia.com/labs/rtr/publication/bitterli2020spatiotemporal/ |
| Hybrid SSR + RT pattern | Interplay of Light, 2019 | https://interplayoflight.wordpress.com/2019/09/07/hybrid-screen-space-reflections/ |
| GoW Ragnarok rendering | McAuley, GDC 2023 | https://sms.playstation.com/media/documents/GOWR_Stephen_Mcauley_RenderingGOWR_GDC23.pdf |
| DLSS 3.5 Ray Reconstruction | NVIDIA Blog 2023 | https://blogs.nvidia.com/blog/ai-decoded-ray-reconstruction/ |
| UE5 Lumen documentation | Epic Developer Community | https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine |

---

## 11. Summary

The clearest trend in 2025 is the **cascade model** — not "use SSR or use RT" but
"use SSR first, then add more layers for what it misses." The exact layers vary by engine
but the pattern is universal:

- **Lumen** (UE5): screen trace → SDF trace → World Radiance Cache
- **FidelityFX Hybrid** (AMD): SSSR → RT for misses → NRD denoiser
- **Cyberpunk Overdrive** (CD Projekt): SSR data composited with path tracing
- **Brixelizer GI** (AMD): screen probes → world radiance cache → SDF trace

For an engine **without hardware RT** the best-in-class approach in 2025 is:
Hi-Z SSSR + temporal denoising + reflection captures + a world-space probe/SDF
fallback for distant/sky reflections. This gives visual quality close to hardware RT
reflections for all but the cleanest mirror surfaces, and the architecture extends
naturally to add RT as an overlay when the hardware is available.

**SSR is not dying. It is becoming the first tier of a deeper system.**
