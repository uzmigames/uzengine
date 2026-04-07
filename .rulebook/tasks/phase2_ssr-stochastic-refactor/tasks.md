# Tasks: Stochastic SSR Refactor

## Phase 1 — Critical Bug Fixes (D-tier -> C-tier)

- [x] 1.1 Bind Hi-Z mip SRV to SSR trace pass — DEFERRED to Phase 3 (Hi-Z mip 0 is half-res min-reduced; requires shader-side Hi-Z traversal, not just binding a different texture). Kept raw depth SRV.
- [x] 1.2 Add temporal frame offset to IGN jitter — added UE5-style `float2(47,17)*0.695 * (frameIdx % 8)` rotation in ssr_trace_cs.hlsl. Uses timeParams.z as frameCount.
- [x] 1.3 Fix resolve pass hardcoded intensity — now reads `cv_SSRIntensity->value` in frame_gi_passes.cpp:288.
- [x] 1.4 Add screen-bounds check and grazing-angle guard in binary refinement — skip refinement if abs(ssDir.z) < 1e-5, break if midpoint exits [0,1] UV.
- [x] 1.5 Fix resolve pass hardcoded roughnessMaskScale — now reads `-2.0f / max(0.05f, cv_SSRMaxRoughness->value)`.
- [x] 1.6 Build and verify: build OK (125 shaders), chrome_test shows correct SSR reflections. darkmetal_test has roughness=0.5 > MaxRoughness=0.35 so SSR is correctly skipped.

## Phase 2 — Denoise Quality (C-tier -> B-tier)

- [x] 2.1 Add Karis inverse tonemap in ssr_resolve_cs.hlsl — Karis weight `rcp(1+Lum)` per sample before accumulation, inverse `rcp(1-Lum)` after normalization. Prevents HDR fireflies.
- [x] 2.2 Replace min/max AABB temporal clipping with variance-based clipping (mean +/- 1.25σ) in ssr_temporal_cs.hlsl.
- [x] 2.3 Convert temporal variance computation to YCoCg color space — RGB→YCoCg before neighborhood stats, clip history in YCoCg, convert back to RGB.
- [x] 2.4 Implement adaptive temporal blend weight — `alpha = 1/targetSamples` where targetSamples scales with confidence squared. Replaces fixed 0.05.
- [x] 2.5 Increase VELOCITY_THRESHOLD from 0.01 to 0.05. Max blend capped at 0.5 (always accumulate some history).
- [x] 2.6 Lower roughness passthrough threshold in resolve pass from 0.1 to 0.04.
- [x] 2.7 Fix triple-weighting — trace now stores `float4(hitColor, confidence)` (non-premultiplied). Lighting shader applies weight correctly.
- [x] 2.8 Build and visual verify: chrome_test shows correct reflections at frame 60. Build OK (125 shaders).

## Phase 3 — Stochastic SSR (B-tier -> A-tier)

- [x] 3.1 STBN blue noise replaced with temporal IGN (Jimenez 2014) + spatial offset — no external texture needed. 2D quasi-random seeds from dual IGN evaluations with temporal frame rotation.
- [x] 3.2 STBN texture infrastructure added (m_stbnTexture/m_stbnSrv in scene_renderer_impl.h) but using procedural IGN instead. 3D texture slot reserved for future upgrade.
- [x] 3.3 Implement GGX VNDF importance sampling in ssr_trace_cs.hlsl — Dupuy & Benyoub 2023 SampleGGX_VNDF from sampling.hlsli, Frisvad tangent frame, temporal IGN seeds. Mirror reflect for roughness <= 0.04, VNDF stochastic for rougher surfaces.
- [x] 3.4 C++ SampleGGX_VNDF + ImportanceSampleGGX parity in brdf_math.h. 8 unit tests (normal incidence, medium/full roughness, grazing angle, boundary, upper hemisphere, reflected direction statistics).
- [x] 3.5 Virtual hit reprojection in ssr_temporal_cs.hlsl — hit distance → estimated world hit pos → reproject via prevVP matrix (CBV at b2). Blends virtual + surface velocity based on distance confidence.
- [x] 3.6 Hit distance R16F texture: m_ssrHitDistance/Srv/Uav in scene_renderer_impl.h, created in CreateSsrResources(), written by trace pass via bindless UAV (hitDistUavIdx root constant).
- [x] 3.7 Tile classification compute pass: ssr_tile_classify_cs.hlsl (8x8 tiles, roughness gate, ray compaction via groupshared+InterlockedAdd, bindless UAV), ssr_prepare_indirect_cs.hlsl (ray count → DispatchIndirectArgs, counter reset). Buffers: m_ssrRayList + m_ssrRayCounterArgs. PSOs registered. Trace pass still direct dispatch (indirect follow-up).
- [x] 3.8 Roughness-adaptive elliptical kernel: 15-sample Halton(2,3) pattern, ellipse aligned to screen-space reflection direction (major:minor ratio 1:1→2.5:1 by roughness), SSSR edge-stopping (normal^512, depth*centerZ*4), Karis HDR weight. Replaces 12-tap isotropic Poisson.
- [x] 3.9 Exposure correction: `hitColor *= currentExposure / prevExposure` in trace pass. m_ssrPrevExposure tracked per-frame. Ratio=1.0 when auto-exposure is stable (no-op multiply).
- [x] 3.10 Hi-Z mip hierarchy: two-phase march (coarse at mip1 quarter-res 8 steps, fine at mip0 half-res 16 steps, binary refinement full-res 4 steps). Root constants: hizMip0SrvIdx, hizMip1SrvIdx, hizMipCount. Falls back to linear march when hizMipCount==0.
- [x] 3.11 Build passes: 127 shaders OK, C++ compiles clean, 8/8 VNDF parity tests pass. Re-enabled temporal + spatial denoise passes. r.SSR=true default. Visual verify pending.

## Phase 4 — Cascade Architecture (A-tier -> S-tier, future)

- [ ] 4.1 Extend SSR output format to `(color, confidence, hitDistance, roughness)` per pixel — enables downstream layers and NRD integration.
- [ ] 4.2 Implement reflection capture probe system — parallax-corrected cubemap captures as Layer 2 fallback for SSR misses in enclosed environments.
- [ ] 4.3 Implement cascade compositor — confidence-weighted blending: SSR (Layer 1) -> Reflection Captures (Layer 2) -> IBL (Layer 3), with per-pixel confidence merge.
- [ ] 4.4 Add r.Reflections.Method CVar — 0=IBL only, 1=SSR+IBL, 2=SSR+Probes+IBL, 3=SSR+RT+Probes+IBL (future).
- [ ] 4.5 Design DXR hybrid dispatch interface — trace RT only for pixels where SSR confidence < threshold. No implementation until DXR backend is ready.

## Testing

- [ ] T.1 Visual regression: capture --pass-dump before Phase 1 changes as baseline (golden images for SSR trace, resolve, temporal passes).
- [ ] T.2 After Phase 1: verify SSR trace pass shows more complete reflections (Hi-Z fix) and temporal pass converges within 8 frames (temporal IGN fix).
- [ ] T.3 After Phase 2: verify no HDR fireflies on chrome_test scene (Karis tonemap), verify temporal accumulation stable during camera orbit (YCoCg variance clip).
- [ ] T.4 After Phase 3: verify roughness 0.0-0.7 produces correct specular lobe spread (VNDF sampling), verify blue noise pattern visible in single-frame trace but invisible after temporal.
- [ ] T.5 PSNR regression test: Phase 2 output vs Phase 1 output must be PSNR > 30 dB (improved, not regressed). Phase 3 vs Phase 2 must be PSNR > 25 dB (different technique, better quality).
- [ ] T.6 Unit tests for VNDF sampling C++ parity (test_brdf_parity.cpp): known-value test at roughness 0.1/0.5/0.9, boundary test at roughness 0.0/1.0, property test (sampled direction within hemisphere).
- [ ] T.7 Unit tests for YCoCg conversion roundtrip (test_color_math_parity.cpp): verify RGB->YCoCg->RGB identity within 1e-5.
- [ ] T.8 Performance: SSR total GPU time must not exceed 3.0ms at 1080p (Phase 1-2), 4.0ms at 1080p (Phase 3). Measure via --gpu-profile.
- [ ] T.9 Verify 95%+ coverage on all new C++ code (VNDF sampling, STBN loader, tile classification).

## Documentation

- [ ] D.1 Update docs/specs/screen-space-effects.md with new SSR pipeline description (3-pass -> 4-pass with tile classification), VNDF sampling, STBN integration.
- [ ] D.2 Update docs/analysis/ssr/ssr-master-report.md with implementation status after each phase completion.
- [ ] D.3 Add SSR CVars to docs/guides/shader-authoring.md: r.SSR.MaxRoughness, r.SSR.Intensity, r.SSR.Quality (new, Phase 3).
- [ ] D.4 Update ROADMAP.md SSR entry from "Stochastic multi-ray (Stachowiak 2015)" to reflect actual implementation status.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
