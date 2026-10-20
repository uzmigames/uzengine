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
- [ ] 4.3 Implement cascade compositor — confidence-weighted blending: SSR (Layer 1) -> Reflection Captures (Layer 2) -> IBL (Layer 3), with per-pixel confidence merge. **Partly done in phase0_lighting-block-b-05-layered-reflections (2026-10-07): SSR blended before the BRDF over the SDF gather or the IBL by confidence; the capture-probe layer stays open.**
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

## HWRT Reflections Stage 1 (2026-10-20, opt-in `r.RayTracing.Reflections`, default off)

Spec: `docs/specs/rendering/rt-reflections.md`. Proof dir: `E:/UzmiEngine/rt-reflections-stage1-proof` (harness `scripts/rendering/test_rt_reflections_stage1.py`, cases default/ssr/rt/pt/metrics, every run < 10 s; `metrics.json`, `montage_ssr_rt_pt.{png,jpg}`).

- [x] R1.1 Compute shader `shaders/runtime/reflections/rt_reflection_cs.hlsl` (embedded; shader integrity 152 -> 153): per pixel, `ReflectionNeedsRT` on the GBuffer depth/roughness + THIS frame's SSR trace confidence (`m_ssrTraceOutput.a`, not the history), mirror ray via `ReflectionDirection` from `worldPos + N * kReflectionSurfaceBias` with `TMin = kReflectionRayTMin`, inline `RayQuery<RAY_FLAG_FORCE_OPAQUE>` against `ResourceDescriptorHeap[tlasSrv]`. Hit shaded as the reference path tracer shades a bounce vertex (`gi/gi_trace_common.hlsli`): interpolated vertex normal (inverse-transpose via `CommittedWorldToObject3x4`) + material-table albedo by TLAS InstanceID, GDF gradient + reflectance for a mesh without a table entry, sun x shadow ray (`RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`), Lambert `albedo * sun * NdL / pi`; miss = `GITrace_SkyRadiance` (the PT's sky SRV). `ReflectionHitUsable` false (hit outside every GDF cascade without table geometry / no usable normal) leaves the texel to the lighting's prefiltered probe. Writes `(radiance, SSR roughness fade x r.SSR.Intensity)` into `m_ssrResolvedOutput`, the buffer the deferred lighting already blends before the BRDF (no composite change). GDF-only normals were tried first and rejected by the proof: clipmap 0 voxels are 78 cm, the 100 cm test block's field gradient is radial, so faces the sun cannot see lit up red (RT RMSE 18.7 > SSR 15.9).
- [x] R1.2 Pass `SceneRenderer::RegisterRTReflectionPass` (`frame/frame_gi_passes.cpp`, called from `RegisterLightingPasses` right after `RegisterSSRPasses`): root constants `RTReflectionRootConstants` = 13 DWORDs at b0 (<= 20; `passes/raytracing/rt_reflection_pass.h`), PerFrameCB at b1, the shared `RadianceCacheTraceConstants` at b2 via the CB ring (`FillRTReflectionTraceConstants`: GDF clipmaps + sun + skylight, built at execution time like the path tracer's), bindless output UAV; declared render-graph reads depth/normal/velocity (the TLAS and the SSR textures are not graph resources: TLAS built in `RTManager::BeginFrame`, SSR textures transitioned through the state registry exactly as the SSR passes do); pipeline `PipelineLibrary::GetRtReflection()` (`pipeline_gi.cpp`, created on first use only, released in `pipeline_lifecycle.cpp`); CVar `r.RayTracing.Reflections.MaxDistance` (ray TMax, cm). `r.RayTracing.Reflections` was already registered in `rt_manager.cpp`.
- [x] R1.3 Gate: `RTManager::IsReflectionsEnabled() && IsReady() && GetTLASInstanceCount() > 0` and the SSR gate (`ShowFlag::SSR`, `ctx.ssrActive`, `m_ssrHasPrevColor`); off = the pass is never registered (nothing compiled, bound or dispatched). Default (`r.RayTracing.Reflections 0`, no `--path-trace`) frame 95 vs `E:/UzmiEngine/f005-after/frames/f_95.png`: `default_max_code_delta 0`, `default_png_bytes_identical true`.
- [x] R1.4 Proof vs the converged DXR path tracer (shared ROI 250,190-390,350; PT 256->512 frame RMSE 0.15 codes): `ssr_reflection_rmse_codes 15.92`, `rt_reflection_rmse_codes 2.90` (rt - ssr = -13.02); full frame SSR 17.09 vs RT 4.25. Visually: SSR-only leaves the whole mirror black (the block is outside the frustum, the raster sky/IBL is black here); with RT the mirror shows the reflected sky (17 codes, = the PT's 17) and the off-screen block as a dark silhouette at x 296-343 / y 229-291 (analytic mirror projection of the block AABB x 300-400 / y 350-450 / z 300-400: x 296.0-344.0 / y 229.5-293.3). The mirror-visible faces (bottom, -X, -Y) are not sunlit (sun = `rotation * Forward(+X)`), exactly as the PT (black at `r.PathTracer.MaxBounces 2`, dim sky-lit red at 4). Note: the PT's own reflected silhouette is 5-12 px smaller/higher than the analytic projection; the RT silhouette matches it to the pixel.
- [x] R1.5 Verification: `zig build -Doptimize=ReleaseFast` exit 0, `Shader integrity OK (153 shaders verified)`; `npm run test:layouts` exit 0 (Tier A `rt_reflection_cs.hlsl PerFrameCB`, Tier B `RTReflectionRootConstants` DXC 52 bytes = sizeof); `GTEST_FILTER=-RuntimeViewportTest.* zig build test -Doptimize=ReleaseFast`: 7942 ran, 7920 passed, only the 3 pre-existing `VfxEditorSlate.*` failures (no TDR); `RTReflectionStage1.*` 8/8 (5 contract + 3 new: root budget/offsets, shared b2 rows, trace-constant fill). run.logs: no `compile failed`, no device removal. Harness fix: the `pt` case passed `--screenshot-frames 255,511`, which the engine rejects (N, A-B or A-B:step only) -> `255-511:256`.
- [ ] R2 Stage 2 (deferred): GGX-VNDF glossy rays for roughness > mirror, radiance-cache SH indirect at the hit (the block's sky-lit faces read 0 here vs the PT's dim 11-27 codes at 4 bounces), denoise / temporal accumulation (NRD specular reproject or the SSR denoiser), RT without the SSR gate (`PerFrameCB::ssrSrvIndex` is only published while SSR is active).

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
