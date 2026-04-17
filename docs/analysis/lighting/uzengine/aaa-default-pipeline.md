# UzEngine — AAA Default Lighting Pipeline (Redesign)

**Status**: Design proposal, not yet implemented.
**Supersedes**: The current deferred-lighting PS stacking approach.
**Cross-refs**: [gap-analysis.md](./gap-analysis.md), [../crimson-desert/00-blackspace-engine.md](../crimson-desert/00-blackspace-engine.md), [../lumen/00-architecture.md](../lumen/00-architecture.md).

This document is the concrete plan for UzEngine's new default light and
shadow pipeline. It is organized in **three quality tiers** (SM6 /
RT-capable / RT-high-end) and a **phased implementation schedule** that
delivers visible Sponza improvements at every milestone.

---

## 1 · Design principles

1. **One unified integration**, not N stacked add-ops. All indirect
   contributions (SSGI + DDGI + IBL) fold into one source-of-truth
   irradiance buffer before being shaded.

2. **Demodulate before denoising, remodulate after**. Every indirect
   buffer is `radiance / albedo` or `radiance / fresnel` at the denoise
   stage.

3. **Hit distance is a required output** of every RT pass. Denoisers key
   off it.

4. **Fallback cascade**, not switch-on-type. Every sample tries:
   screen-space → world-space SDF → DDGI probe → sky LUT. Each step
   strictly degrades quality gracefully.

5. **One CVar per tier, not N per feature**. Players pick a quality
   preset; artists pick per-material overrides; the tier selector wires
   everything.

6. **Path-tracer as ground-truth**. A reference path tracer runs in an
   offline mode; we use its output to validate the real-time pipeline
   (PSNR ≥ 35 dB vs path-traced, as the project rule requires).

---

## 2 · Three quality tiers

### Tier 0 — Low (fallback; runs on anything)

For compatibility fallback and integrated GPUs. Sacrifices quality to run.

- Analytic lights + CSM (4 cascades).
- PCF 3×3 shadow filter.
- SSAO (or GTAO medium-quality preset).
- IBL cubemap indirect.
- No SSGI, no DDGI, no stochastic SSR.
- Classic SSR (screen-space, deterministic).
- No RT.

Target frame time budget: 8 ms lighting + post at 1080p.

### Tier 1 — Medium (default for no-RT HW)

The new default. No RT HW required. Everything wired correctly.

- Analytic lights + CSM (or **VSM** in Phase 2) with PCSS.
- GTAO (current).
- SSGI (denoised, demodulated).
- **DDGI probe grid** (SW SDF trace) — primary distant GI source.
- IBL as fallback for rays that miss DDGI (sky visibility).
- Contact shadows on by default.
- Stochastic SSR with VNDF sampling + hit distance.
- Volumetric fog (current + extensions).

Target frame time budget: 10 ms lighting + post at 1440p.

### Tier 2 — High (RT-capable)

All Tier 1 + ray tracing.

- **HWRT reflections** — SSR first, HWRT fallback.
- **HWRT sun shadows** — replaces CSM for directional.
- **ReSTIR DI** — many-lights shadowing at fixed cost.
- **ReSTIR GI** as path-reuse for the DDGI probe update.
- **Classical denoiser stack** (NRD-inspired) for diffuse + specular.

Target frame time budget: 12 ms lighting + post at 1440p on RTX 3080.

### Tier 3 — Ultra (RT-high-end, neural)

All Tier 2 + full path tracing primary.

- **Per-pixel GI primary** (BlackSpace-style).
- **DLSS Ray Reconstruction** or FSR Ray Regen as denoiser+upscaler.
- Everything in Tier 2 still present as fallback.

Target: 4K with DLSS Performance on RTX 4080, 60 fps.

---

## 3 · Pass graph (Tier 1, the new default)

```text
CPU — culling, light gather, BVH updates (CPU side only in Tier 1)
   ▼
┌── Early-Z Prepass ──────────────────────┐
│ Depth + motion vectors                   │
└──────────────────┬──────────────────────┘
                   ▼
┌── G-Buffer Pass ────────────────────────┐
│ albedo (RGB) + material ID (A)          │
│ world normal (RG16F octahedral)         │
│ roughness + metallic (RG8)              │
│ emissive (RGB) + depth (R32)            │
└──────────────────┬──────────────────────┘
                   ▼
┌── Shadow Maps (CSM / VSM) ──────────────┐
│ Sun: 4 cascades or VSM pages            │
│ Local: atlas pages                       │
└──────────────────┬──────────────────────┘
                   ▼
┌── GTAO ────────────────────────────────┐
│ Horizon-based AO + bent normals         │
└──────────────────┬──────────────────────┘
                   ▼
┌── SSGI Trace ───────────────────────────┐
│ Screen-space hemispherical gather       │
│ 8 rays per pixel, 16-step march         │
│ Output: demodulated radiance + hitDist  │
└──────────────────┬──────────────────────┘
                   ▼
┌── DDGI Probe Update ────────────────────┐
│ 12% of probes per frame (rotating)      │
│ 256 rays per probe (SDF trace)          │
│ Output: octahedral irradiance+visibility│
└──────────────────┬──────────────────────┘
                   ▼
┌── SSGI Denoise ─────────────────────────┐
│ À-trous 5-stage variance-guided filter  │
│ Temporal accum with motion vector       │
└──────────────────┬──────────────────────┘
                   ▼
┌── SSR Trace ────────────────────────────┐
│ Stochastic GGX VNDF, 1 ray/pixel        │
│ Hi-Z march, hit distance output         │
└──────────────────┬──────────────────────┘
                   ▼
┌── SSR Denoise ──────────────────────────┐
│ Parallax-corrected temporal + bilateral │
└──────────────────┬──────────────────────┘
                   ▼
┌── Indirect Integration ─────────────────┐
│ diffuse = SSGI (where valid) blended    │
│           with DDGI (fallback)          │
│           blended with IBL (sky fallback)│
│ specular = SSR (where valid) blended    │
│            with probe / sky             │
│ Single buffer: IndirectDiffuse + Spec   │
└──────────────────┬──────────────────────┘
                   ▼
┌── Deferred Lighting (simplified) ───────┐
│ For each pixel:                         │
│   direct = Σ analytic_lights * shadow   │
│   indirect = IndirectDiffuse (pre-int)  │
│   reflection = IndirectSpecular (pre-int)│
│   out = direct + indirect * GTAO +       │
│         reflection + emissive            │
└──────────────────┬──────────────────────┘
                   ▼
┌── Volumetric Fog Integration ───────────┐
│ Composite froxel fog over opaque        │
└──────────────────┬──────────────────────┘
                   ▼
┌── Transparent Pass ─────────────────────┐
│ Forward-shaded, samples same probes     │
└──────────────────┬──────────────────────┘
                   ▼
┌── Post ─────────────────────────────────┐
│ Bloom, tonemap, DoF, motion blur        │
│ Final upscale (TAA / TSR / DLSS)        │
└─────────────────────────────────────────┘
```

Key change from today: the **Deferred Lighting** PS is dramatically
simpler. It no longer samples DDGI / SSGI / Brixelizer / IBL directly —
those are pre-integrated into one `IndirectDiffuse` and one
`IndirectSpecular` buffer by the **Indirect Integration** pass.

---

## 4 · Data flow for indirect integration

```text
                      ┌─ SSGI radiance (demodulated)
                      │  + SSGI valid mask (in-screen, not clipped)
                      │  + SSGI hit distance
                      │
DDGI probe atlas ─────┼─ DDGI irradiance (8 nearest probes, Chebyshev-weighted)
                      │  + DDGI confidence (probe validity)
                      │
Sky LUT ──────────────┼─ IBL irradiance (diffuse convolution)
                      │
                      ▼
          ┌──────────────────────────────────┐
          │  Weighted blend per-pixel:       │
          │    if SSGI valid → take SSGI     │
          │    else if DDGI valid → take DDGI│
          │    else fall back to IBL         │
          │                                  │
          │  Compute blend weights with:     │
          │    - depth-aware feather         │
          │    - normal-aware feather        │
          │    - GTAO modulation             │
          │                                  │
          │  Output: IndirectDiffuse buffer  │
          └──────────────────────────────────┘

For specular (reflections):
                      ┌─ SSR radiance (demodulated)
                      │  + SSR valid mask
                      │  + SSR hit distance
                      │
Radiance cache (once implemented) ─ specular lookup in reflection dir
                      │
Sky LUT ──────────────┼─ SkyBox sample at reflection dir
                      │
                      ▼
                  (same blend logic)
                      │
                      ▼
              IndirectSpecular buffer
```

---

## 5 · SW RT vs HWRT decision

The `r.RT.Mode` CVar selects:

| Value | Mode | Tiers |
|-------|------|-------|
| 0 (disabled) | All GI via SSGI + DDGI (SDF tracing) | Tier 0, 1 |
| 1 (hybrid) | SSGI + DDGI (SDF tracing) + HWRT reflections | Tier 2 |
| 2 (full RT) | Full RT stack: HWRT shadows + HWRT DDGI + HWRT reflections + ReSTIR | Tier 2 high / Tier 3 |

Same pass graph, different backends. The `TraceRay()` function in shaders
is abstracted (either SDF or HWRT under the hood).

---

## 6 · Volumetric fog extensions

Per [../crimson-desert/03-volumetric-fog-god-rays.md](../crimson-desert/03-volumetric-fog-god-rays.md) §5:

1. **3D density texture source** (CVolumetricFogVolume ECS component).
2. **Wind advection** via Perlin noise 3D texture.
3. **Multiple-light froxel sampling** (reservoir-based).
4. **Deeper temporal history** (16 frames).
5. **HWRT sun-visibility per froxel** (Tier 2+).

---

## 7 · ReSTIR DI integration (Tier 2)

Replace the `for each light` loop in the direct-lighting shader with a
ReSTIR DI dispatch:

1. **Candidate pass**: 32 candidates per pixel from the global light
   list, streamed into reservoir via WRS.
2. **Visibility ray**: 1 shadow ray per pixel (HWRT).
3. **Temporal reuse**: reproject prev-frame reservoir.
4. **Spatial reuse**: 5 neighbors via Poisson disk.
5. **Shade**: evaluate BRDF × selected-light contribution.

Budget: 2.1 ms at 1440p on RTX 4080.

---

## 8 · Denoiser strategy

Two-track approach:

### 8.1 Classical (all tiers)

Build an NRD-inspired in-house denoiser:

- REBLUR-style diffuse + specular separation.
- Hit-distance-aware kernel size.
- Demodulated input / output.
- À-trous + temporal integration.

Estimate: 2 weeks of focused work for the diffuse; another week for
specular.

### 8.2 Neural (Tier 3)

Integration slot for DLSS RR (NGX SDK) + FSR RR (FidelityFX SDK) + XeSS.
Behind CVar `r.Denoise.Neural = 0|1`. When on, bypasses the classical
denoiser and feeds RR directly.

Feasibility depends on SDK integration; plan for 4+ weeks end-to-end.

---

## 9 · Phased implementation

### Phase 0 — Sponza P0 fix (1 week)

Goal: Sponza shadowed areas visibly receive warm bounce light.

- Enable DDGI by default in `DefaultProject.uzproj`.
- Fix DDGI probe update to sample directional light + skylight.
- Wire DDGI into deferred lighting PS.
- Apply GTAO to full indirect sum.
- Calibrate PCSS light size.

Deliverable: Sponza screenshot comparison pre/post showing soft colored indirect.

### Phase 1 — Indirect Integration pass (2 weeks)

Goal: unify SSGI + DDGI + IBL into one source-of-truth pre-integration
buffer.

- New pass `IndirectIntegration` runs after SSGI/DDGI/SSR.
- Outputs `IndirectDiffuse` and `IndirectSpecular` buffers.
- Deferred lighting PS simplifies to read pre-integrated buffers.
- Blend weights tuned per Sponza / outdoor / indoor.

Deliverable: deferred lighting PS ≤ 400 lines; removed DDGI/SSGI sample
blocks; tests pass.

### Phase 2 — Stochastic SSR + world-space fallback (2 weeks)

Goal: proper reflections with roughness-aware quality.

- SSR rewritten to GGX VNDF stochastic sampling.
- Hit distance output.
- World-space SDF ray continuation when SSR misses.
- Parallax-corrected temporal denoise.

Deliverable: reflections look correct at grazing angles.

### Phase 3 — Denoiser rewrite (2–3 weeks)

Goal: NRD-grade denoising quality.

- À-trous 5-stage + temporal for both diffuse and specular.
- Demodulation infrastructure.
- Hit-distance-aware kernel.

Deliverable: PSNR ≥ 35 dB vs path-traced ground truth on Sponza and one
outdoor scene.

### Phase 4 — VSM (4 weeks)

Goal: better shadows, scene-scale detail.

- Page table + physical atlas.
- Mark, allocate, render, sample passes.
- Replace CSM for directional.

Deliverable: VSM enabled via CVar; CSM still present as fallback.

### Phase 5 — HWRT hybrid backend (4 weeks)

Goal: Tier 2 enabled for RTX/RDNA2.

- TLAS + BLAS management, skeletal BLAS refit.
- HWRT replaces SDF in DDGI probe update (CVar-selectable).
- HWRT reflections as SSR fallback.

Deliverable: `r.RT.Mode 1` works end-to-end.

### Phase 6 — ReSTIR DI (3 weeks)

Goal: many-lights scenes don't tank performance.

- Light BVH.
- Per-pixel reservoir buffer.
- WRS candidate pass + visibility + temporal + spatial.

Deliverable: 256-light test scene runs at same frame time as 8-light
test scene.

### Phase 7 — VSM Nanite interop (if we have Nanite)

Currently no Nanite; defer.

### Phase 8 — Ray Reconstruction plug-in (4 weeks)

Goal: Tier 3 enabled.

- NGX SDK integration.
- FSR RR SDK integration.
- Bypass-classical-denoiser codepath.

Deliverable: `r.Denoise.Neural 1` on RTX GPU gives visibly higher quality.

---

## 10 · CVar schema (externally visible)

| CVar | Default | Range | Notes |
|------|---------|-------|-------|
| `r.Quality.Preset` | 1 | 0..3 | Tier selector. Sets the below as a bundle. |
| `r.GI.Mode` | 2 | 0..3 | 0=IBL only, 1=SSGI, 2=SSGI+DDGI, 3=ReSTIR GI |
| `r.Reflect.Mode` | 2 | 0..3 | 0=IBL, 1=SSR, 2=SSR+RT fallback, 3=full RT |
| `r.RT.Mode` | 0 | 0..2 | 0=SW, 1=hybrid HWRT, 2=full HWRT |
| `r.Shadow.Mode` | 1 | 0..2 | 0=CSM, 1=VSM, 2=RT |
| `r.Shadow.PCSS.LightSize` | 0.01 | 0..0.1 | Sun disc radius |
| `r.DDGI.Enabled` | 1 | 0/1 | DDGI on/off override |
| `r.DDGI.RaysPerProbe` | 256 | 32..1024 | per-probe ray count |
| `r.DDGI.UpdateFraction` | 0.12 | 0.01..1 | per-frame probe update fraction |
| `r.SSGI.Enabled` | 1 | 0/1 | — |
| `r.SSR.Stochastic` | 1 | 0/1 | deterministic vs stochastic |
| `r.Denoise.Spatial` | 1 | 0/1 | à-trous on/off |
| `r.Denoise.Temporal` | 1 | 0/1 | — |
| `r.Denoise.Neural` | 0 | 0/1 | neural denoiser (Tier 3) |
| `r.ReSTIR.DI.Enabled` | 0 | 0/1 | reservoir DI (Tier 2+) |
| `r.ReSTIR.GI.Enabled` | 0 | 0/1 | reservoir GI (Tier 3) |
| `r.VolFog.LightSampleMode` | 1 | 0..2 | 0=sun only, 1=stochastic reservoir, 2=all lights (dev) |

All CVars are framed as **runtime-adjustable** per the project rule that
every parameter is a CVar.

---

## 11 · Success metrics

Binary: Sponza looks right.

Quantitative:
- **PSNR ≥ 35 dB** vs the reference path tracer on Sponza (sun only).
- **PSNR ≥ 30 dB** on Sponza with two point-lights added.
- **Frame time ≤ 12 ms** for lighting+post at 1440p on RTX 3080 in Tier 1.
- **Frame time ≤ 10 ms** in Tier 0 on GTX 1080 at 1080p.

Qualitative:
- Shadowed areas show warm bounce light (Sponza).
- Contact shadows visible on small objects (Sponza vase on pillar).
- Reflections on polished stone look correct at grazing angles.
- No cascade seams on directional shadows (Phase 4+).
- No GI boiling / flicker in stationary shots.

---

## 12 · Risks & mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| DDGI probes land inside geometry | Empty probes → dark spots | Probe classifier pass; skip probes with no valid rays. |
| SSGI + DDGI blend discontinuity at screen edge | Visible seam | Wide feather zone; SSGI and DDGI must be in equivalent radiance units. |
| Denoiser over-blur on sharp contact | Lose detail | Hit-distance-adaptive kernel; demodulate material detail. |
| VSM page miss | Un-shadowed flash | Conservative pre-allocation; CSM fallback for one frame. |
| HWRT BLAS for skeletal too expensive | Frame stutter | Refit-only updates; periodic full rebuild budget. |
| ReSTIR temporal divergence | Firefly / glitter | M clamp 20; validity mask on reproject. |

---

## 13 · Non-goals

We are **not** shipping:

- Cluster-LOD virtual geometry (Nanite). Out of scope. GI works without it.
- Offline-baked lightmaps. Purely dynamic.
- Hair-specific shading (Marschner / Walter). Phase beyond this plan.
- Offline path-traced light baking. Path tracer stays as runtime-optional.

---

## 14 · How to start

Day 1: open this file next to [gap-analysis.md](./gap-analysis.md) and
begin Phase 0. Every Sponza screenshot taken between now and "Phase 0
done" should be one of the post-test evidence points.

The full plan takes ~5–6 months for a single engineer at ~60% availability
or ~3 months focused. Phases 0, 1, 2, 3 deliver the bulk of the visible
improvement and are all SM6-compatible (no RT HW needed).

---

## 15 · References

See the per-technique docs in [../modern-techniques/](../modern-techniques/),
the Crimson Desert analyses in [../crimson-desert/](../crimson-desert/), and
the Lumen deep-dive in [../lumen/](../lumen/). This document is the
synthesis of those findings into an actionable plan.
