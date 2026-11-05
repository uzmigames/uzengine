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
- [x] Gate behind a CVar (r.GI.FinalGather.StableTemporal, default OFF). OFF is a logical NO-OP: the stable
      path lives in a separate shader (ssgi_stable_temporal_accum_cs.hlsl; legacy DXIL byte-identical) + separate
      no-inline CPU methods, all gated by m_stableTemporal. NOTE: reflection-reference OFF shows <=3 codes vs a
      clean same-toolchain baseline, but that scene disables SSGI/FinalGather/RadianceCache so the feature code
      never runs there - the <=3 codes are non-feature global binary codegen (exe +5.6KB), not an OFF regression.
- [x] Preserve disocclusion + camera/light-motion response: validated on a moving-camera scene (frames
      600-790); OFF vs ON montage (_evidence/gi/diffuse-stable/motion_montage.png) shows NO new ghosting/lag -
      the stationary detection keeps the fast alpha during motion.
- [x] CPU gtest for the variance-weight math (4 SsgiStableTemporal.* tests pass).
- [x] Validate + report: material_showcase (linear-HDR, frames 90-149) Diffuse std -44.0%, GreenPlastic -20.3%
      (toward the Glass/Chrome range; means preserved <=0.001%); OFF logical no-op; pinned-zig build exit 0 (166
      shaders), test:layouts exit 0, gtests 8148 pass / only 3 pre-existing VfxEditorSlate. Committed aa250510.

## STAGE-1 COMPLETE (aa250510). Stage-2 follow-up
Stage-1 lowers the temporal NOISE FLOOR (variance/confidence-weighted history) - it reduces the crawl ~25-44%
but does NOT reach the Glass/Chrome control range because it does not reduce the per-frame noise SOURCE (8
gather rays + rotation). Stage-2 (not started): attack the source - more effective samples via stronger
spatiotemporal reuse, and/or a normal-aware spatial denoise on curved surfaces - to bring the std to the
control range; then consider default ON.
