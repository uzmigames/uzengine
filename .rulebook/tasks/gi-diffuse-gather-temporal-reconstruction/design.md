# Design — diffuse final-gather temporal reconstruction

## Mechanism (file+symbol, from the diagnosis)
- `runtime/shaders/runtime/gi/sdf_diffuse_gather.hlsli:33` `SdfDiffuseRotation`: per-pixel+per-frame
  Hammersley rotation (8 rays). Changing orientation across a smooth curved surface spreads residual noise.
- `runtime/shaders/runtime/ssgi/ssgi_restir_initial_cs.hlsl` + `ssgi_restir_temporal_cs.hlsl`: reservoir
  reuse (history 20, 8 candidates) — reduces but does not integrate the hemisphere exactly.
- `runtime/shaders/runtime/ssgi/ssgi_spatial_denoise_cs.hlsl`: 5x5 half-res receiver-plane filter (correlates
  residual spatially).
- `runtime/shaders/runtime/ssgi/ssgi_temporal_accum_cs.hlsl` `ReprojectGatherHistory`/`main`: the noise-floor
  source — `count = min(historyCount+1, 64)`, `alpha = max(1/count, temporalAlpha)`, temporalAlpha=0.05.

## Approach
1. Track a per-pixel luminance variance / 2nd moment of the gather estimate + a history-confidence signal
   (reuse the existing reproject/validity path; keep sample-sequence indexing separate from receiver validity).
2. Replace the fixed alpha floor with a variance-aware weight: converged + accepted + low-variance -> smaller
   alpha (more history, below 0.05); low confidence / high variance / disocclusion / history-rejection / light
   change -> keep the current fast alpha. The existing r.SSGI.TemporalAlpha=0.01 run is a first benchmark but
   is still clamped to 1/64 by the count cap — raise/relax the cap under the confidence gate.
3. Evaluate a well-distributed (blue-noise / rank-1 / owen-scrambled) spatiotemporal sequence for the gather
   rotation vs the current parity-correlated radical inverse (the Kronecker alternative alone did NOT help).

## Gating + safety
New CVar (e.g. `r.GI.FinalGather.StableTemporal`, default OFF initially) so OFF is byte-identical to the
current render (reflection-reference frame-95 max 0). Keep the raster material model, ReSTIR, valid-sample and
surface cache untouched in behaviour. Default-flip only after the validation passes.

## Risks
- Over-lengthening history introduces ghosting/lag on camera or light motion -> the confidence gate MUST keep
  the fast path; validate on a moving-camera + light-change scenario, not just the fixed camera.
- Darkening / over-smoothing: check per-component means + detail retention, not just the std drop.

## Validation harness
Reuse `_evidence/gi/sphere-crawl/capture.py`/`analyze.py` (material_showcase, all-GI-ON CVar set incl. the
easily-missed `r.RadianceCache=1`), fixed camera, frames 90-149 AND 600-659, the 5 nested sphere ROIs, fixed
exposure. Target: Diffuse + GreenPlastic body temporal std toward the Glass/Chrome control range.
