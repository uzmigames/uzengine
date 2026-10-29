# Diffuse final-gather temporal reconstruction — kill the noise-floor crawl

## Why
With all GI/Indirect enabled, smooth curved diffuse surfaces (material_showcase Sphere_Diffuse and
Sphere_GreenPlastic) show lighting that slowly "crawls/swims" across the surface. Render-backed diagnosis
(2026-10-29, `_evidence/gi/sphere-crawl/`, [[reference/rendering/sphere-crawl-material-showcase-2026-10-29]])
isolated the cause to the diffuse SDF final gather, NOT a broken pass:
- The gather shoots 8 stochastic cosine-hemisphere rays/pixel; the temporal accumulation
  (`ssgi_temporal_accum_cs.hlsl`) uses `alpha = max(1/count, temporalAlpha)` with temporalAlpha=0.05 and a
  64-sample count cap, so it replaces >=~5% of the accumulated radiance EVERY frame even on a fully static,
  100%-accepted receiver. It converges to an exponential-average NOISE FLOOR, not a constant image.
- The residual, spatially filtered and with a hemisphere orientation that varies across the smooth sphere,
  reads as broad slowly-moving bright/dark patches.
- PROOF: freezing the converged gather output drops body temporal std 94.6% (Diffuse) / 64.8% (GreenPlastic);
  TemporalAlpha 0.01 roughly halves it, 0.5 doubles it. ReSTIR, valid-sample rejection, the surface cache and
  DDGI are NOT the cause (ReSTIR off ~doubles the crawl; the others leave it unchanged). The instability
  persists at frames 600-659 (not initial convergence).

## What changes
Make the diffuse-gather temporal weight variance/confidence dependent so a truly static, accepted,
low-variance receiver accumulates beyond the fixed 0.05 / 64-cap floor, while preserving fast response at
disocclusion, history rejection and lighting changes. Evaluate a better-decorrelated spatiotemporal sample
sequence for the per-pixel hemisphere rotation. Keep ReSTIR + valid-sample + the surface cache (they help).
Gate so OFF reproduces the current render byte-identical. Do NOT ship by freezing the signal or over-blurring.
