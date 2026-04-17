<!-- doc-meta
title: id Tech & Industry Engine SSR — Technical Analysis
subsystem: rendering/ssr
status: reference
date: 2026-04-05
-->

# id Tech & Industry Engine SSR — Comprehensive Technical Analysis

This document covers screen-space reflection (SSR) techniques from id Software (id Tech 6 and 7),
Guerrilla Games (Decima/Killzone), EA DICE (Frostbite), Naughty Dog, Insomniac Games, CD Projekt
Red (Red Engine), and Ubisoft Massive (Snowdrop). The focus is on noise reduction strategies and
architectural decisions for each approach.

---

## 1. id Tech 6 — DOOM 2016

**Pipeline**: Hybrid forward + thin deferred (clustered forward+)
**Presentation**: Tiago Sousa & Jean Geffroy, SIGGRAPH 2016, "The Devil is in the Details: idTech 666"
**Reference**: [advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf](https://www.advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf)

### Architecture

DOOM 2016 used a clustered forward renderer divided into **3072 clusters** (16×8×24 grid, logarithmic
Z-slices) to manage up to 256 lights, decals, and reflection probes per cluster. The engine is
"mostly forward rendered" with a **thin G-Buffer written in parallel** specifically for screen-space
effects — not a full deferred GBuffer.

Two thin G-Buffers were generated during the forward pass:
- **Normal buffer**: R16G16 float (2-component normal, octahedral or spheremap encoding)
- **Specular buffer**: R8G8B8A8, alpha = smoothness factor

These buffers exist solely to serve deferred screen-space effects (SSR, AO, etc.).

### SSR Pass

The SSR pass is a **pixel shader** (not compute) operating after the forward color pass:

```
Inputs:
  - Depth buffer (reconstructed world position)
  - R16G16 Normal buffer (reflection direction)
  - R8G8B8A8 Specular buffer (alpha = smoothness, gates SSR intensity)
  - Previous rendered frame (color source for reflection hits)

Algorithm:
  1. Ray origin = pixel world position (from depth reconstruction)
  2. Ray direction = reflect(-V, N)
  3. Hi-Z hierarchy built from depth buffer (each mip = maximum depth of 4 children)
  4. Ray march: advance in steps, skip using Hi-Z mip to accelerate traversal
  5. On hit: read previous frame color at hit UV
  6. Blend SSR contribution by specular alpha (smoothness)
```

### Hi-Z Acceleration (Key Technical Detail)

id Tech 6 builds a hierarchical max-depth mip chain. Each mip stores the **maximum** depth of 4
children (not the average, unlike a typical mipmap). When the ray is in empty space, the traversal
can step to a coarser mip level and take larger steps. This is the same technique described in:

> @ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing" — Hi-Z ray traversal

Performance budget: SSR pass ran within ~1ms at 1080p on PS4 (60fps target).

### Noise Strategy

DOOM 2016 used **mirror reflections** (no stochastic sampling), so per-pixel noise was not a
problem. The main artifact was **missing information** (off-screen geometry), handled by:
- Blending with the clustered reflection probe cubemaps when SSR hits screen edge
- Specular smoothness fade: low smoothness surfaces get near-zero SSR weight
- Edge screen vignette: reflections fade to probe contribution near borders

### Clustered Reflection Probes

Every cluster can index up to 256 local reflection probes. The SSR result is additively blended
on top of the probe-based IBL in the forward pass. This hybrid layering is critical: SSR handles
dynamic geometry visible on screen, while probes handle off-screen content.

---

## 2. id Tech 7 — DOOM Eternal

**Pipeline**: Fully forward (no G-Buffer at all)
**Presentation**: Jean Geffroy, Yixin Wang, Axel Gneiting, SIGGRAPH 2020, "Rendering the Hellscape of Doom Eternal"
**Reference**: [advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf](https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf)
**Reverse engineering**: Simon Coenen, "DOOM Eternal — Graphics Study" (simoncoenen.com)

### The Fundamental Architectural Shift

DOOM Eternal **eliminated the thin G-Buffer entirely** compared to DOOM 2016. The forward pass is
a single uber-shader pass, and SSR is computed **inline inside the uber forward shader**, not in a
separate compute or pixel shader pass.

```
DOOM 2016:
  Forward pass (color) → G-Buffer write (normals + specular)
  → Deferred SSR compute/pixel pass (reads G-Buffer)
  → Composite

DOOM Eternal:
  Uber forward shader (color + SSR + lighting, all in one pass)
```

### The Tradeoff: Bandwidth vs Register Pressure

| Metric | DOOM 2016 | DOOM Eternal |
|--------|-----------|--------------|
| Render targets written | Forward color + 2 G-Buffers | Forward color only |
| SSR shader | Separate deferred pass | Inline in uber forward shader |
| Memory bandwidth | Higher (G-Buffer writes) | Lower (no G-Buffer) |
| GPU register pressure | Lower (separate shaders) | Higher (combined uber shader) |

This tradeoff became viable in 2020 because GPU register file sizes had grown substantially
(AMD RDNA, Nvidia Turing/Ampere). The bandwidth savings from eliminating the G-Buffer write
outweighed the cost of increased register pressure.

### SSR Inside the Uber Shader

The uber shader has direct access to the reconstructed GBuffer-equivalent data (normals, roughness,
depth) as shader variables, rather than reading from textures. SSR ray marching is performed in the
same shader thread that lit the pixel:

```hlsl
// Conceptual uber shader structure (not literal id Software code)
float4 main(FSInput input) {
    // 1. Evaluate PBR material (albedo, normals, roughness, metallic)
    MaterialData mat = EvaluateMaterial(input);

    // 2. Perform SSR ray march using current-frame depth (not G-Buffer)
    float4 ssrHit = MarchSSR(input.worldPos, mat.normal, mat.roughness);

    // 3. Apply lighting (clustered forward light loop)
    float3 color = ClusteredLighting(mat, ssrHit);

    return float4(color, 1.0);
}
```

**Critical implication**: Since SSR operates within the forward pass, there is no separate depth
buffer or G-Buffer to read. The depth hierarchy (Hi-Z) must be built from the **previous frame's**
depth buffer. This means SSR in DOOM Eternal has a one-frame latency on depth, but this is
acceptable because depth changes very little frame-to-frame at 60fps.

### Noise Strategy in id Tech 7

DOOM Eternal still used **mirror reflections** for SSR (no stochastic GGX sampling in the base
SSR pass). The reflection direction is `reflect(-V, N)` with no Monte Carlo sampling. Noise
avoidance comes from:

1. **Roughness cutoff**: Surfaces above a roughness threshold (~0.3–0.4) fall back entirely to
   reflection probes — no SSR contribution
2. **No temporal denoising for SSR**: The mirror-ray approach produces deterministic results, no
   noise to denoise
3. **Screen-edge fade**: Reflections fade as the hit UV approaches screen borders
4. **Ray tracing fallback**: DOOM Eternal added optional DXR ray-traced reflections in a later
   patch, which replaced SSR on supported hardware entirely

### Ray Tracing Integration (Post-Launch)

DOOM Eternal received DXR hardware ray tracing post-launch (PC only). When RT is enabled, the
SSR pass is partially or fully replaced by DXR reflected rays:
- RT rays are traced at full resolution for smooth surfaces
- Rougher surfaces continue using SSR or probe blending
- Denoising uses NVIDIA's NRD (Not Reliable Denoiser) or a proprietary temporal filter

This hybrid model — RT for high-quality smooth mirrors, SSR for mid-roughness, probes for high
roughness — is now the industry standard ("hybrid reflections").

---

## 3. Guerrilla Games — Killzone Shadow Fall + Horizon Zero Dawn (Decima Engine)

### 3a. Killzone Shadow Fall (2013) — The Stochastic SSR Pioneer

**Presentation**: Michal Valient, GDC 2014, "Taking Killzone Shadow Fall Image Quality into the Next Generation"
**Reference**: [guerrilla-games.com](https://www.guerrilla-games.com/read/taking-killzone-shadow-fall-image-quality-into-the-next-generation-1)

Killzone Shadow Fall introduced **stochastic importance sampling** to SSR, which became the
foundational approach for all modern SSR implementations:

```
Per-pixel ray generation:
  1. Sample GGX VNDF (visible normal distribution function) using a random seed
  2. Computed reflection direction = reflect(-V, N_sampled)  [N_sampled ≠ geometric normal]
  3. This produces a DIFFERENT ray direction per pixel → Monte Carlo noise

Denoising:
  1. Spatial pass: 4-tap Poisson disk bilateral filter
  2. Temporal pass: exponential moving average with AABB color clamping
  3. Reprojection: velocity buffer reproject history to current pixel UV

Secondary bounces:
  - Reflection hit color is used as input for a second SSR trace
  - This gives approximate "reflections in reflections" at near-zero cost
  - Depth mip hierarchy accelerates secondary traces too
```

Guerrilla also used **localized reflection cubemaps** as fallback, blended with SSR by confidence.

### 3b. Frostbite (DICE) — Stochastic SSR (SIGGRAPH 2015)

**Presentation**: Tomasz Stachowiak, SIGGRAPH 2015, "Stochastic Screen-Space Reflections"
**Reference**: [ea.com/frostbite/news/stochastic-screen-space-reflections](https://www.ea.com/frostbite/news/stochastic-screen-space-reflections)
**First shipped in**: Mirror's Edge Catalyst + Need for Speed (2015–2016)

This is the most-cited SSR paper in the industry and directly influenced FidelityFX SSSR. Key
contributions:

#### Half-Resolution Ray Tracing + Full-Resolution Resolve

```
Trace pass (half resolution):
  - Shoot 1 ray per pixel at half res
  - Ray direction = ImportanceSampleGGX(roughness, random_seed)
  - Hi-Z hierarchical traversal (same as id Tech 6)
  - Output: hit UV + confidence (half res RGBA16F)

Resolve pass (full resolution):
  - For each full-res pixel, gather 4-8 neighboring half-res hit results
  - Weight by: GGX PDF, normal similarity, depth similarity
  - This effectively gives 4-8 samples per full-res pixel at the cost of 1
  - Output: glossy reflection color (full res)

Temporal pass (full resolution):
  - Reproject history using velocity buffer
  - AABB variance clamping (3×3 neighborhood min/max)
  - Blend: 90–95% history, 5–10% current
```

#### The Blue Noise Trick (Critical for Noise Quality)

Frostbite uses a **tiled blue noise texture** as the random seed for importance sampling, rather
than white noise or a fixed low-discrepancy sequence. Blue noise has the critical property that
its error distributes **spatially** (perceptual noise looks like grain, not structured patterns),
which makes the temporal denoiser converge faster.

Blue noise tile: typically 64×64 or 128×128 RGBA8, 1 channel per effect (SSR, GTAO, shadows share
different channels). The sequence is also **temporally offset** by the frame number using a
temporal blue noise or a Halton sequence offset so that consecutive frames never repeat the same
sample pattern.

#### Contact Hardening Reflections (Specular Elongation)

Frostbite introduced **contact-hardening SSR**: the reflected blur kernel adapts based on the
distance to the reflection hit. Very close hits produce mirror-like reflections; distant hits
produce spread-out blurry reflections. This matches physical behavior (a puddle reflects nearby
geometry sharply but distant geometry blurily).

The implementation: store **hit distance** in the trace output. In the resolve pass, scale the
Poisson disk kernel radius by `f(hitDistance, roughness)`.

---

## 4. Naughty Dog — The Last of Us Part 2

**Presentation**: Nathan Vanbrocklin, SIGGRAPH 2020, "The Last of Us Part II: A Spatial-Temporal
Post-Processing Pipeline"
**Reference**: naughtydog.com/blog/naughty_dog_at_siggraph_2020

### Hybrid SSR + Cube Maps

For The Last of Us Part 2, Naughty Dog used a **tight integration** between SSR and box-projected
cubemaps:

```
For each reflective pixel:
  1. Try SSR trace. If hit found (confidence > threshold): use SSR color
  2. If SSR misses (off-screen): read from carefully aligned box-projected cubemap
  3. The cubemap is precisely placed by artists to match the geometry → seamless blending

Key insight: cubemaps are so carefully aligned that SSR and cubemap outputs are nearly identical
in the overlap region, eliminating seam artifacts at the SSR/cubemap boundary.
```

### Wet Surface Reflections

For wet pavement and rain-affected surfaces (prominent in TLOU2), Naughty Dog used:
- SSR for direct water/puddle reflections on flat surfaces
- Roughness perturbation from a rain normal map to smear the SSR ray direction
- Temporal accumulation in a short-lived history buffer (puddle reflections are noisy but
  settle quickly because wet surfaces are nearly flat → high SSR confidence)

### Render Graph Pipeline Position

Naughty Dog's SSR ran **after** the GBuffer pass and **before** the final deferred lighting
composite. The resolve pass blended SSR contribution into the specular lighting term:

```
Lighting = Diffuse + Specular_IBL * (1 - SSR_weight) + SSR_color * SSR_weight
```

This ensures energy conservation: SSR replaces IBL rather than adding to it.

---

## 5. Insomniac Games — Spider-Man / Ratchet & Clank: Rift Apart

### Spider-Man PS4 (2018)

Spider-Man used a traditional deferred SSR pipeline similar to UE4's approach:
- Hi-Z ray march at half resolution
- Bilateral resolve at full resolution
- AABB-clamped temporal accumulation

Notably, Spider-Man had a large proportion of **flat reflective surfaces** (wet streets, glass
buildings) which are ideal for SSR. The game was reported to use ~1.5ms for the full SSR pass
at 1080p.

### Ratchet & Clank: Rift Apart (2021) — Ray Traced Reflections

Rift Apart took a fundamentally different approach: **dedicated hardware ray tracing for
reflections** rather than SSR. The GDC 2022 talk "Recalibrating Our Limits: Lighting on Ratchet
and Clank: Rift Apart" revealed:

```
Strategy: Spend the PS5 RT budget entirely on reflections
  Reason: Rift Apart has large curved glass surfaces, futuristic metals → high reflection coverage

Implementation:
  - RT reflected rays, 1 ray per pixel at half resolution
  - NVIDIA NRD / in-house temporal denoiser
  - "Checkerboard" RT upsampling: alternate pixels between frames, reconstruct full-res
    (this optimization was later ported to PC Spider-Man)
  
Fallback on non-RT platforms:
  - SSR (same Hi-Z + bilateral + temporal pipeline as Spider-Man PS4)
  - Combined with reflection probes for off-screen coverage
```

The key optimization (**checkerboard RT upsampling**) is worth noting: instead of tracing 1 ray
per pixel every frame, trace 1 ray for every **other** pixel, alternating in a checkerboard
pattern between frames. Temporal reprojection fills in the missing pixels from the previous frame.
This halves RT cost with minimal quality loss at 60fps (motion blur hides the interleaving).

---

## 6. CD Projekt Red — Cyberpunk 2077 (Red Engine 4)

CDPR has not published a dedicated GDC or SIGGRAPH talk specifically on their SSR implementation.
The available technical information comes from community analysis and the well-documented problems
with Cyberpunk's SSR quality at lower settings.

### Known Architecture

Cyberpunk 2077 used a **deferred rendering pipeline** with SSR as a post-process pass:

```
Deferred GBuffer pass
→ Deferred lighting pass
→ SSR trace pass (reads depth + normals + specular from GBuffer)
→ SSR temporal denoise
→ Composite (SSR blended into final scene)
```

### The Graininess Problem

Cyberpunk's SSR became notorious for **severe graininess** at non-Psycho quality settings. The
root cause was a stochastic SSR implementation (GGX importance sampling) with **insufficient
sample count and denoiser quality**:

- **Low/Medium/High settings**: 1 ray per pixel, minimal temporal accumulation → visible grain
- **Ultra/Psycho settings**: increased sample count + aggressive temporal denoiser → clean
- Community mods (e.g., "Grainy SSR Begone" via ReShade): apply bilateral post-process denoise
  as a workaround

### Technical Root Causes of Noise

For a stochastic SSR implementation, noise manifests when:

| Cause | Effect | Mitigation |
|-------|--------|-----------|
| Low samples per pixel (1 ray) | High variance → grain | Increase to 2–4 rays, or use blue noise |
| Poor temporal filter (weak history weight) | Noise flickers every frame | Increase history weight to 90–95% |
| Missing velocity buffer for transparent geometry | Transparent surface history fails | Include transparents in velocity buffer |
| AABB clamp too loose | Ghosting/trailing artifacts | Tighten to std-dev-based AABB |
| AABB clamp too tight | Flicker on rough surfaces | Use per-roughness adaptive clamping |

### Path Tracing Migration (2023)

In 2023, CDPR shipped the "Overdrive" path-tracing update. With PT enabled, SSR is completely
replaced by hardware-traced full-path reflections. The SSR system is now the fallback for
players without DXR hardware.

---

## 7. Ubisoft Massive — Snowdrop Engine (The Division)

**GDC reference**: "Efficient Rendering in The Division 2" (GDC Vault, 2019)
**Engine**: Snowdrop, a fully deferred renderer with extensive use of screen-space effects

### SSR Architecture

The Division used SSR as the **primary real-time reflection technique**, critical to the game's
"NYC wet streets" aesthetic:

```
Key features of Snowdrop SSR:
  1. Full deferred pipeline — all material data available in GBuffer
  2. Hi-Z ray marching in a dedicated compute pass
  3. Cone-trace resolve: instead of a fixed kernel, trace a "cone" whose aperture
     grows with roughness and hit distance (similar to contact hardening)
  4. Temporal accumulation with velocity buffer reprojection
  5. Reflection probe fallback for off-screen misses
```

The Division emphasized **wet surface SSR** heavily. Key technique: the GBuffer stores a
**wetness parameter** per-pixel (driven by rain simulation). When wetness > 0, the effective
roughness for SSR is reduced (wet surfaces become more specular), and the SSR weight increases.
This makes puddles visually distinct from dry pavement without requiring separate render targets.

### The Division 2 — Raytracing in Snowdrop (GDC 2024)

Ubisoft later shipped a Snowdrop RT update (GDC 2024: "Raytracing in Snowdrop: An Optimized
Lighting Pipeline for Consoles"), adding hardware RT reflections that replaced the SSR pass.
The Snowdrop RT system targets consoles (PS5, Xbox Series) specifically, using aggressive
denoising to stay within console bandwidth budgets.

---

## 8. AMD FidelityFX SSSR — Open-Source Reference Implementation

**Reference**: [gpuopen.com/fidelityfx-sssr](https://gpuopen.com/fidelityfx-sssr/)

FidelityFX SSSR is the open-source evolution of Frostbite's stochastic SSR technique, now the
most-used SSR implementation across the industry. It represents the current state of the art.

### 5-Pass Pipeline

```
Pass 1: Classify tiles
  - Mark tiles as: all-sky (skip), all-mirror (fast path), stochastic
  - Output: tile classification buffer

Pass 2: Prepare blue noise + temporal seed
  - Combine frame index + spatial position to generate per-pixel random seed
  - Uses spatially-varying blue noise texture (64×64 tiled)

Pass 3: Intersect (Hi-Z ray march)
  - Input: depth pyramid, GBuffer normals + roughness, temporal random seed
  - GGX VNDF importance sampling: ray direction = ImportanceSampleGGX(roughness, seed)
  - Hi-Z hierarchical traversal: start at coarse mip, refine on hit
  - Output: (hit_UV, hit_T, pdf) — where pdf is the GGX probability density

Pass 4: Reproject + Temporal Accumulation
  - Reproject history using velocity buffer
  - AABB variance clamping (compute 3×3 neighborhood statistics)
  - Adaptive blend weight: function of sample count, motion magnitude, roughness
  - Store accumulated sample count (for variance estimation)
  - Output: temporally stable reflection color + variance estimate

Pass 5: Prefilter (Spatial Denoise)
  - Cross-bilateral filter using normal + depth edge-stopping functions
  - Kernel radius = f(roughness, variance estimate from pass 4)
  - Poisson disk with 8–12 taps
  - Output: final clean SSR color
```

### Key Innovations in FidelityFX SSSR

**1. Adaptive Sample Count Accumulation**

Instead of a fixed blend weight (e.g., 5% current / 95% history), SSSR tracks a per-pixel
**sample count N**. The blend weight is `1/N` (after N frames of stable accumulation, each new
frame contributes 1/(N+1)). This ensures:
- First frame after disocclusion: full current frame (N=1, weight=1.0)
- After 20 stable frames: very low weight (1/20 = 5%)
- Natural convergence curve without tuning a fixed blend constant

**2. PDF-Weighted Spatial Resolve**

When spatially gathering neighboring traces for the resolve, neighbors are weighted by their
**GGX PDF** (the probability density of the sample direction). A sample taken along the specular
lobe peak (high PDF) contributes more than a low-probability grazing-angle sample. This reduces
variance in the resolve without requiring more traces.

**3. Variance-Driven AABB Clamping**

Instead of a fixed ±N standard deviation clamp for the temporal AABB, SSSR computes the actual
variance estimate from the accumulated history:

```hlsl
float3 variance = historyVariance; // stored from previous frame
float3 stdDev = sqrt(variance);
float3 aabbMin = historyMean - varianceClampScale * stdDev;
float3 aabbMax = historyMean + varianceClampScale * stdDev;
float3 clampedHistory = clamp(history, aabbMin, aabbMax);
```

When the surface is stable (low variance), the AABB is tight → strong ghosting rejection.
When the surface is noisy (high variance, e.g., rough material with few samples), the AABB
is loose → preserves accumulated history to reduce flicker.

---

## 9. Cross-Engine Noise Reduction Comparison Table

| Strategy | id Tech 6 | id Tech 7 | Killzone/Decima | Frostbite | FidelityFX SSSR | TLOU2 |
|----------|-----------|-----------|-----------------|-----------|-----------------|-------|
| Stochastic GGX sampling | No | No | Yes | Yes | Yes | Unknown |
| Blue noise seed | No | No | Yes | Yes | Yes | Unknown |
| Half-res trace | No | No | Yes | Yes | Yes | Unknown |
| Spatial bilateral filter | No | No | Yes (4-tap) | Yes (8-tap) | Yes (8–12-tap) | Yes |
| AABB temporal clamping | No | No | Yes | Yes | Yes (variance-driven) | Yes |
| Velocity reprojection | No | No | Yes | Yes | Yes | Yes |
| Contact hardening | No | No | No | Yes | Yes | Unknown |
| Adaptive blend weight | No | No | No | No | Yes (sample count) | Unknown |
| RT fallback (DXR/RT) | No | Yes (later) | No | No | Hybrid variant | No |
| Probe fallback | Yes (clustered) | Yes (clustered) | Yes (local probes) | Yes | Yes | Yes (box-projected) |

---

## 10. Key Lessons for UzEngine SSR

Based on this survey, the following design decisions are well-validated across multiple engines:

### What Every Production SSR Does

1. **Hi-Z ray traversal** — universal. Non-hierarchical linear stepping is too slow for production.
2. **Half-resolution trace** + full-resolution spatial resolve — universal in stochastic pipelines.
3. **Velocity-buffer reprojection** for temporal accumulation — universal.
4. **AABB color clamping** for history rejection — universal (Karis 2014 technique).
5. **Reflection probe / IBL fallback** for off-screen misses — universal.

### What the Best Systems Add

6. **Blue noise temporal seed** (Frostbite, SSSR) — replaces white noise for visually superior
   distribution.
7. **Contact hardening** (Frostbite, SSSR) — kernel radius scales with hit distance and roughness.
8. **Variance-driven AABB** (SSSR) — outperforms fixed std-dev clamping for mixed-roughness scenes.
9. **Adaptive sample count blend** (SSSR) — outperforms fixed blend weight in disocclusion
   recovery and convergence.
10. **PDF-weighted spatial gather** (SSSR) — reduces variance in resolve without extra traces.

### What to Avoid

- **Stochastic sampling without a denoiser** (Cyberpunk's lower settings) → grain artifacts
- **Fixed AABB clamp** on rough surfaces → either ghosting (loose) or flicker (tight)
- **Mirror-only reflections** (id Tech 6/7) → misses physically correct rough specular reflections
- **Separate deferred compute pass** when using fully-forward rendering (id Tech 7 solved this with
  inline uber shader SSR at the cost of register pressure)

---

## Sources

- [SIGGRAPH 2016: The Devil is in the Details — idTech 666](https://www.advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf)
- [SIGGRAPH 2020: Rendering the Hellscape of Doom Eternal (PDF)](https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf)
- [DOOM Eternal — Graphics Study (Simon Coenen)](https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy)
- [DOOM 2016 — Graphics Study (Adrian Courrèges)](https://www.adriancourreges.com/blog/2016/09/09/doom-2016-graphics-study/)
- [Guerrilla Games: Taking Killzone Shadow Fall Image Quality into the Next Generation (GDC 2014)](https://www.guerrilla-games.com/read/taking-killzone-shadow-fall-image-quality-into-the-next-generation-1)
- [Frostbite: Stochastic Screen-Space Reflections (EA, SIGGRAPH 2015)](https://www.ea.com/frostbite/news/stochastic-screen-space-reflections)
- [Stochastic SSR Slides (DICE, SlideShare)](https://www.slideshare.net/DICEStudio/stochastic-screenspace-reflections)
- [AMD FidelityFX SSSR (GPUOpen)](https://gpuopen.com/fidelityfx-sssr/)
- [FidelityFX SSSR 1.5 Technical Manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)
- [Notes on FidelityFX SSSR (Interplay of Light blog)](https://interplayoflight.wordpress.com/2022/09/28/notes-on-screenspace-reflections-with-fidelityfx-sssr/)
- [Naughty Dog at SIGGRAPH 2020](https://www.naughtydog.com/blog/naughty_dog_at_siggraph_2020)
- [GDC Vault: Recalibrating Our Limits — Lighting on Ratchet and Clank: Rift Apart](https://www.gdcvault.com/play/1027792/)
- [GDC Vault: Efficient Rendering in The Division 2](https://gdcvault.com/play/1026293/Advanced-Graphics-Techniques-Tutorial-Efficient)
- [GDC Vault: Raytracing in Snowdrop (GDC 2024)](https://gdcvault.com/play/1034763/Advanced-Graphics-Summit-Raytracing-in)
- [Stochastic SSR — h3r2tic's grimoire (detailed breakdown)](https://h3.gd/stochastic-ssr/)
- [id Tech 7 — Doom Wiki](https://doomwiki.org/wiki/Id_Tech_7)
