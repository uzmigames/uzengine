# Diffuse final-gather temporal reconstruction (2026-10-29)

Kills the "crawling" GI on smooth curved diffuse surfaces (material_showcase Diffuse + GreenPlastic),
root-caused to the gather's 0.05/64-cap temporal noise floor. Diagnosis:
[[reference/rendering/sphere-crawl-material-showcase-2026-10-29]] + `_evidence/gi/sphere-crawl/`.

- [x] Reproduce the fixed-camera baseline (material_showcase, all GI ON, frames 90-149 + 600-659, 5 sphere
      ROIs) and record Diffuse/GreenPlastic body linear-HDR temporal std as the regression target.
- [x] Add a per-pixel variance / 2nd-moment + history-confidence signal to the gather temporal accumulation
      (ssgi_temporal_accum_cs.hlsl), reusing the existing reproject/validity path.
- [x] Replace the fixed `alpha = max(1/count, temporalAlpha)` (0.05) floor with a variance/confidence-aware
      weight: converged+accepted+low-variance uses more history; keep the fast alpha at disocclusion /
      history rejection / light change. Relax the 64-count cap under the confidence gate.
- [x] Evaluate a well-decorrelated spatiotemporal sample sequence for the gather rotation
      (sdf_diffuse_gather.hlsli SdfDiffuseRotation); keep sample indexing separate from receiver validity.
- [ ] Gate behind a CVar (r.GI.FinalGather.StableTemporal, default OFF) so OFF reproduces the current render
      byte-identical (reflection-reference frame-95 max 0).
- [ ] Preserve disocclusion + camera/light-motion response: validate a moving-camera + a light-change
      scenario shows NO new ghosting/lag (no fixed-camera-only win).
- [ ] CPU gtest for the variance-weight math (converged-low-variance -> long history; disocclusion / high
      variance -> fast response).
- [ ] Validate + report: Diffuse + GreenPlastic body temporal std toward the Glass/Chrome range at frames
      90-149 AND 600-659; per-component means + detail retained (no darkening/over-blur); OFF byte-identical;
      pinned-zig build exit 0, npm run test:layouts exit 0, gtests no regression. Then consider default ON.
