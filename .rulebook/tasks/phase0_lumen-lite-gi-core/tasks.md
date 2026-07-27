## 1. Reactivation & diag hygiene
- [x] 1.1 sponza.uzscene: add `r.SSGI: 1`, `r.GTAO: 1`, change `r.SSGI.Intensity` 1.5 → 1.0
- [x] 1.2 Diag targets: register `ssgi.*`, `gtao.*`, `rc.*`, `ddgi.*` unconditionally with status `SKIPPED` when the owning pass is disabled. Done via `RegisterDisabledTarget()` placeholders + `ValidationResult.skipped` + JSON status `SKIPPED` (also applied to the pre-existing depth/3D/oversized readback skips that previously reported a misleading OK). DDGI atlases now register as real targets (`ddgi.irradiance`/`ddgi.depth`) when `r.RayTracing.GI` is on.
- [x] 1.3 Fix `gbuffer.ao` diag target stats aliasing `gbuffer.albedo`. Root cause: Validate() aggregated ALL channels regardless of the target's `dumpChannel`; now honors R/G/B/A selectors — verified `gbuffer.ao`=1.0 (no AO texture), `gbuffer.roughness`=0.37–0.43 real per-channel stats.
- [x] 1.4 Fix D3D12MA shutdown assertion. Root cause: `VfxEditorOverlay::Shutdown()` never destroyed its BitmapFont GPU atlas ("FontAtlas", 128 KB) — found via new `D3D12MA_DEBUG_LOG` → stderr wiring + allocation `SetName()` propagation in buffer/texture creation. Verified 0 UNFREED / 0 assertion on minimal_test and sponza. Bonus hardening: leak report promoted to Error level (auto-flush), teardown log-flush checkpoints in uzengine.cpp.
- [x] 1.5 Build + `--diag` verify + A/B screenshot. Found and FIXED two extra regressions in the process: (a) `GTAOPass::IsEnabledCVar()` read the file-static CVar pointer instead of the registry → pass silently never scheduled; (b) gtao_cs.hlsl horizon march was missing the UV Y-flip (dropped in the bd4db199 rewrite) → every flat surface integrated to (2/π)(N·V)²≈0.07 = near-black AO, radius-independent (root-caused via shader-engineer agent with closed-form proof matching all measurements); also removed the spurious `×2/π` (1.571× uniform over-darkening present since April), replaced the hard screenRadius early-out with a graceful clamp, and capped screenRadius at 256 px. Verified: `gtao.result` open-floor avg 0.074 → **0.966**; `--diag` shows ssgi.* OK / gtao.result OK / rc.probes+ddgi.irradiance SKIPPED; A/B `.screenshots/gi-plan/00-baseline.png` → `02-gtao-fixed.png`. Also routed `--vis` in the uzengine tool (was silently ignored; only DemoApplication parsed it). NOTE: full unit suite has 68 pre-existing failures (VFX defaults/camera default/cross-backend harness drift, all pre-dating this task — none in files touched here).
- [x] 1.6 Verify `gbuffer.velocity` non-zero and `taa.history` populated under motion. Findings: velocity path WORKS (fbx_test animated character → non-zero velocity; Sponza zero is correct for a static scene/camera). `taa.history` is never written because TAA is disabled by default (revert ea42cc64 "TAA default... DLSS paused" — TaaUpscaler initializes but resolve never dispatches). NOT a Phase 2/3 blocker: SSGI carries its own temporal accumulation (r.SSGI.TemporalAlpha). TAA/temporal-foundation decision recorded for the Phase 4 screen-probe gather task, which requires reprojection.
- [ ] 1.7 Housekeeping: verify + check off + archive `phase1_fix-sponza-lighting-calibration` (items already in code)

## 2. Ground-truth harness
- [x] 2.1 Path-traced goldens: 3 poses captured at 2400 spp → `.screenshots/gi-plan/golden-{a,b,c}.png`. Enablers built along the way: `--camera-pos/--camera-look` CLI (deterministic poses, applied to the active CameraComponent post-load), `r.Show.Stats` now actually gates the stats HUD (flag existed but was never consulted in frame_overlay.cpp). Editor mode used for capture (scripts dormant → static camera).
- [x] 2.2 PSNR loop documented in plan doc §5 + baseline recorded: **7.91 dB** pose A (raster-a.png vs golden-a.png, same pose/flags). PSNR via sampled-MSE Python; NOTE (tooling debt): in-engine `--flip-ref/--flip-test` HANGS (>9 min, process killed) — needs its own fix before FLIP-based gating; PSNR loop is sufficient for phase gating meanwhile. Pose B/C raster pairs to be captured with the Phase 4 verification.

## 3. Surface-cache-lite hit lighting
- [ ] 3.1 Create shared `runtime/shaders/runtime/gi/gi_hit_lighting.hlsli` with the hit-radiance model (shadowed sun + albedo + prev-frame irradiance feedback), consumed by both probe tracers
- [ ] 3.2 radiance_cache_trace_cs.hlsl: real hit normal via GDF central-difference gradient (6 taps)
- [ ] 3.3 radiance_cache_trace_cs.hlsl: sun occlusion via GDF sphere-trace from hit toward light
- [ ] 3.4 ddgi_probe_update_cs.hlsl: geometric triangle normal + sun shadow via second RayQuery
- [ ] 3.5 Per-instance average albedo+emissive table: C++ build at scene load (material constants + albedo mip-tail average), StructuredBuffer upload, bindless SRV in probe constants
- [ ] 3.6 Consume the material table in the DXR path via `InstanceID()`; SDF path uses scene-average fallback (logged)
- [ ] 3.7 Multi-bounce feedback: sample previous-frame probe SH at hit × hit albedo, added to hit radiance (@ref Majercik 2019 §infinite bounces)
- [ ] 3.8 Enable `r.RadianceCache: 1` in sponza.uzscene; verify `r.RayTracing.GI=1` path on DXR hardware
- [ ] 3.9 Verify: `--vis` probe atlas shows colored shadow-aware bounce; `--pixel-debug` on enclosed pixel shows no unshadowed-sun energy; PSNR delta recorded

## 4. Composite correctness
- [ ] 4.1 deferred_lighting_ps.hlsl: SSGI joins the indirect cascade by screen-validity blend (replaces additive `diffuseAmbient += ssgi`)
- [ ] 4.2 Stop tinting SSGI/probe irradiance by `skyTintDiffuse` (scene radiance ≠ sky radiance; tint applies to sky SH fallback only)
- [ ] 4.3 GTAO applied exactly once to the unified indirect diffuse
- [ ] 4.4 Irradiance units audit across SSGI/RC/DDGI/sky; convention documented in cb-layouts.md
- [ ] 4.5 Verify: `--pass-dump` stage review; PSNR table update; screenshots at 3 poses per visual-verification rule

## 5. Tail (docs + tests — check or waive with tailWaiver)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
