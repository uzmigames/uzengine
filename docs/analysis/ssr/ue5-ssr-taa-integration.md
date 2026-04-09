<!-- doc-meta
title: UE5 SSR + TAA Integration — Exact Pass Ordering & Noise Prevention
created: 2026-04-05
status: complete
sources: UE5.6 source (W:\UE_5.6_Source), codebase analysis, published papers
-->

# UE5 SSR + TAA Integration — Exact Pass Ordering & Noise Prevention

This document answers five specific questions about how UE5 integrates SSR with TAA,
grounded in UE5.6 source analysis (already in `docs/analysis/rendering/ue5-ssr-architecture.md`)
and corroborated by published references.

---

## Question 1: Exact Pass Order — Deferred Lighting, SSR, TAA, Post-Process

**Verified from UE5.6 `DeferredShadingRenderer.cpp` call sequence:**

```
1.  PrePass / Depth prepass
2.  HZB (Hierarchical Z-Buffer) pyramid build
3.  GBuffer base pass (opaque / masked)
4.  Velocity buffer
5.  Shadows (CSM, VSM, distance field)
6.  Deferred lighting (RenderLights → direct + sky + point/spot)
7.  Reflection environment (IBL probes + sky reflection)
8.  SSR passes — INSIDE RenderDeferredReflections():
      a. SSR Trace (SSRTReflections.usf — Hi-Z ray march, 8-12 steps, 1-12 rays)
      b. SSR Spatial Denoise (SSDSpatialAccumulation.usf — ellipse kernel, Karis)
      c. SSR Temporal Accumulation (SSDTemporalAccumulation.usf — variance clip)
      d. SSR Composite → blended INTO HDR scene color
9.  Height fog / atmosphere
10. Translucency
11. Bloom
12. TAA / TSR resolve (PostProcessTemporalAA.usf / TSR)
13. Tonemap → LDR output
```

**Key fact:** SSR (all 4 sub-passes) runs BEFORE TAA. TAA receives HDR scene color with SSR
already composited into it.

---

## Question 2: Does UE5 Apply SSR Inside Deferred Lighting, or After TAA?

**SSR is applied INSIDE `RenderDeferredReflections()`, which runs AFTER deferred lighting
but BEFORE TAA.**

The exact position in the pipeline:
- After `RenderLights()` (direct lighting complete)
- Inside the reflection phase (reflection captures → SSR composite)
- Before post-processing begins (bloom, TAA, tonemap)

SSR composites into the HDR scene color buffer during step 8d above. TAA then processes
this complete HDR image (lighting + reflections + atmosphere + fog) in step 12.

---

## Question 3: Does UE5 Have a Dedicated SSR Denoiser Separate from TAA?

**Yes — UE5 has a fully separate SSR denoiser pipeline that is ENTIRELY independent of TAA.**

The SSR denoiser (`ScreenSpaceDenoise/`) has two sub-passes:

### SSR Spatial Reconstruction (SSDSpatialAccumulation.usf)
- Anisotropic directional ellipse kernel shaped by the GGX specular lobe
- Karis inverse tonemap weighting (prevents firefly dominance)
- Color space: RGB with Karis weighting
- Purpose: denoise within the CURRENT FRAME across neighboring pixels

### SSR Temporal Accumulation (SSDTemporalAccumulation.usf)
- Variance clipping at `StdDevMultiplier = 1.25 sigma` in LCoCg space
- Sample-count-based blend (NOT fixed alpha): target = `PI * majorRadius * minorRadius`
  projected specular lobe area → mirrors get ~1 sample target, rough surfaces get 1024
- History: per-pixel accumulated sample count, stored in dedicated SSR history textures
- Bilateral preset: `BILATERAL_PRESET_REFLECTIONS_TAA`
- This is SEPARATE from the main TAA history buffer

The console variable `r.SSR.Temporal` controls this SSR-specific temporal pass.
When `r.SSR.ExperimentalDenoiser = 1`, the SSR TAA pass is REPLACED with a third-party
denoiser (e.g., NRD), controlled by `CVarDenoiseSSR = "Replace SSR's TAA pass with denoiser"`.

**TAA does NOT know about SSR. It receives the fully composited HDR color and treats SSR
contributions as part of the scene like any other lighting.**

---

## Question 4: How Does UE5's TAA Handle SSR's Frame-to-Frame Specular Color Changes?

TAA does NOT have SSR-specific logic. The way UE5 prevents SSR from contaminating TAA
is by denoising SSR BEFORE compositing it into scene color. By the time TAA sees the
HDR buffer, SSR has already been:

1. **Spatially filtered** (anisotropic ellipse kernel, Karis tonemap)
2. **Temporally accumulated** (variance clip, 1-1024 sample accumulation)
3. **Fade-masked** (roughness gate, screen-edge vignette)

The denoised SSR is then a temporally STABLE contribution to the scene color. TAA's
neighborhood variance clamping does not need to specially handle SSR because the SSR
signal has already been stabilized by its own temporal pass.

**However, there is a latent risk:** if SSR noise is not fully suppressed before
compositing, TAA's variance clip WILL reject SSR history when the reflected color
changes faster than the TAA neighborhood box allows. This manifests as "reflections
that never stabilize" — the variance clip keeps rejecting the SSR contribution.

The SSR-specific prevention mechanisms that operate BEFORE TAA:

| Mechanism | Where | What it Prevents |
|-----------|-------|-----------------|
| Previous frame TAA color as SSR input | SSR Trace | Feedback loop; SSR reads stable, not noisy, color |
| Temporal step offset jitter (IGN mod 8) | SSR Trace | Same sample every frame; distributes across 8 frames |
| Karis inverse tonemap | SSR Spatial + Resolve | Firefly specular spikes dominating accumulation |
| Variance clip 1.25 sigma in LCoCg | SSR Temporal | Ghosting from disoccluded or disoccluding surfaces |
| Sample-count-based blend | SSR Temporal | Fixed alpha under/over-blending by roughness |
| GGX tail truncation (0.1 bias) | SSR Trace | Extreme samples causing per-frame variance spikes |
| Screen-edge vignette | SSR Trace | Reprojection artifacts at screen boundaries |

---

## Question 5: Does UE5 Use Stencil or Material ID to Prevent TAA Mixing SSR/Non-SSR Pixels?

**No stencil or material ID separation at the TAA stage.**

However, UE5 does use an OPTIONAL stencil optimization at the SSR TRACE stage:
- `r.SSR.Stencil` (default 0) — when enabled, a pre-pass writes stencil 1 on pixels
  that need SSR (passes roughness threshold check). The SSR trace dispatch then uses
  that stencil to skip pixels where SSR is not needed.

This is a PERFORMANCE optimization (skipping dispatch on rough pixels), not a TAA
contamination-prevention mechanism.

At the TAA stage, UE5 makes no distinction between SSR and non-SSR pixels. The TAA
algorithm processes the entire HDR image uniformly. The theory is:

1. SSR has been denoised by its own temporal pass before compositing
2. The denoised SSR is temporally stable enough that TAA's variance clip does not reject it
3. If SSR was poorly denoised and shows high variance, TAA's variance clip will reject the
   history for those pixels — but this manifests as blur/instability, not a catastrophic failure

There is NO mechanism to prevent TAA neighborhood sampling from mixing SSR and non-SSR
pixels. In practice, this is acceptable because:
- SSR boundaries are always at GBuffer normal/roughness discontinuities
- At those discontinuities, depth dilation for velocity selection already handles TAA
- The SSR roughness fade and screen-edge vignette smooth the SSR boundary

---

## Summary Table

| Question | Answer |
|----------|--------|
| Where does SSR run in the pipeline? | After deferred lighting, inside `RenderDeferredReflections()`, BEFORE TAA |
| Is SSR composited before TAA? | Yes — TAA sees HDR with SSR already blended in |
| Dedicated SSR denoiser? | Yes — spatial (ellipse kernel) + temporal (variance clip) passes, entirely separate from TAA |
| What scene color does SSR read? | Previous frame's TAA output (PrevViewInfo.TemporalAAHistory) — NOT current frame |
| How does TAA handle SSR noise? | It doesn't — SSR noise is eliminated by the SSR denoiser BEFORE compositing |
| Stencil/material ID for TAA? | No — only an optional stencil for SSR trace performance optimization |
| How is SSR feedback loop prevented? | SSR reads PREV frame TAA color, never its own output |

---

## UzEngine Current vs UE5 — Gap Analysis

| Feature | UE5 | UzEngine | Gap |
|---------|-----|----------|-----|
| SSR scene color input | Prev TAA frame (`PrevViewInfo.TemporalAAHistory`) | Prev HDR copy (`m_ssrPrevSceneColor`) | Functionally equivalent — UzEngine already uses prev-frame color |
| SSR trace jitter | IGN + temporal frame rotation (mod 8) | IGN spatial only, NO temporal offset | CRITICAL — static jitter converges to noise pattern |
| Spatial denoiser | Anisotropic ellipse kernel, Karis-weighted | 12-tap Poisson disk, Karis-weighted | Missing anisotropic lobe shape; Karis is present |
| Temporal denoiser | Variance clip 1.25 sigma, LCoCg, sample-count blend | Variance clip 1.25 sigma, YCoCg, fixed/roughness blend | LCoCg vs YCoCg; missing sample-count accumulation |
| Hi-Z tracing | Mip pyramid (`StartMipLevel=1`) | Raw depth (`hizSrvIdx = depthSRV`) | CRITICAL — full-res linear march instead of hierarchical |
| SSR separate from TAA | Fully separate pipeline, SSR composited before TAA | Same architecture — SSR composited before TAA | Correct architecture |
| Roughness threshold | 0.6 default, PostProcess-driven | 0.35 CVar default | Different default; CVar exists |
| Exposure correction | `PreExposure / PrevPreExposure` on prev color | None | Missing; matters with auto-exposure |

---

## References

- `docs/analysis/rendering/ue5-ssr-architecture.md` — full UE5.6 source analysis
- `docs/analysis/ssr/ssr-master-report.md` — multi-engine comparison + fix plan
- Stachowiak 2015 "Stochastic Screen-Space Reflections" — SIGGRAPH Advances in RTR
- Karis 2014 "High Quality Temporal Supersampling" — SIGGRAPH Advances in RTR
- Schied 2017 "Spatiotemporal Variance-Guided Filtering" — HPG
