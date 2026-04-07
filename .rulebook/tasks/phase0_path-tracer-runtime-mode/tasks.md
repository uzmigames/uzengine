## 1. Pipeline skeleton (Step 1 — solid magenta)

- [ ] 1.1 Create `runtime/engine/renderer/passes/pathtracing/path_tracer_pass.{h,cpp}` with Init/Resize/Execute/Shutdown/ResetAccumulation/IsEnabled API (PIMPL or direct, follow GTAOPass pattern).
- [ ] 1.2 Register CVars in `path_tracer_pass.cpp` (all 10 listed in proposal). Default off.
- [ ] 1.3 Add `PathTracerConstants` struct to `runtime/engine/renderer/core/params/constant_buffers.h` (matrices + scalars, 16-byte aligned).
- [ ] 1.4 Allocate ping-pong RGBA32F accumulation textures `m_accum[2]`, single-frame trace output `m_traceOut`. Allocate only when `cv_PtEnable` flips on; free on flip off.
- [ ] 1.5 Create `runtime/shaders/runtime/pathtracing/path_tracer_cs.hlsl` and `path_tracer_resolve_cs.hlsl`. Both with `[numthreads(8,8,1)]` entry point `main`. Trace shader writes `float4(1,0,1,1)`, resolve shader copies to `hdrSceneColor`.
- [ ] 1.6 Construct/destroy `PathTracerPass` in `runtime/engine/renderer/scene/scene_renderer_init.cpp` alongside other frame passes. Wire `Resize` into existing scene resize path.
- [ ] 1.7 Add the top-level `if (cv_PtEnable->value >= 0.5f)` branch in `runtime/engine/renderer/scene/scene_render_frame.cpp`: when true, skip shadow / GI / deferred-lighting dispatches and call `PathTracerPass::Execute`.
- [ ] 1.8 Force `r.TAA=0` and disable upscalers in `PathTracerPass::OnEnableChanged`. Restore previous values on disable.
- [ ] 1.9 Build + verify Step 1: `uzengine.exe --project sample/projects/pbr_showcase.uzproject --editor --cvar r.PathTracer.Enable=1 --screenshot-stdout --frames 8`. Expect a fully magenta frame.

## 2. Step 2 — GBuffer first hit visualization

- [ ] 2.1 Bind GBuffer depth/normal/material as SRVs in the path tracer root signature (`t0..t2`).
- [ ] 2.2 Trace shader: read GBuffer at the pixel, output `float4(normal*0.5+0.5, 1)`.
- [ ] 2.3 Build + verify: screenshot must match `--view-mode worldnormal` of the same scene.

## 3. Step 3 — One-bounce diffuse + accumulation

- [ ] 3.1 Implement Wang-hash RNG in `pt_common.hlsli` (`WangHash`, `Rand01`, `RandUnitVector`). Seed `(pix.x*1973 + pix.y*9277 + frameIndex*26699) | 1`.
- [ ] 3.2 Implement cosine-weighted hemisphere sampling helper (`pt_brdf.hlsli`) using existing `common/sampling.hlsli`.
- [ ] 3.3 From the GBuffer first hit, shoot one diffuse ray; if it escapes the scene, sample the sky cubemap × `EnvIntensity`. Multiply by albedo from GBuffer material.
- [ ] 3.4 Resolve pass: implement progressive accumulation with sample counter in alpha channel. Format: `out.rgb = lerp(hist.rgb, curr.rgb, 1/(hist.a+1))`, `out.a = hist.a + 1`.
- [ ] 3.5 Ping-pong `m_historyIdx` flip per frame.
- [ ] 3.6 Verify: `--frames 120` and `--frames 1200` — noise must visibly decrease, mean brightness stable. `--diag` reports zero NaN/Inf.

## 4. Step 4 — Multi-bounce + Russian roulette + direct sun

- [ ] 4.1 Bounce loop up to `cv_PtMaxBounces`. Throughput accumulator. Break on miss → environment.
- [ ] 4.2 Direct sun shadow ray at every bounce (analytic visibility through the analytic scene + sky-only at this stage).
- [ ] 4.3 Russian roulette past `cv_PtRRMinBounce` bounces, throughput compensation `1/p`.
- [ ] 4.4 Per-sample firefly clamp via `cv_PtClampFirefly`.
- [ ] 4.5 Verify: `--frames 2000`, soft shadows visible, no fireflies > 10× mean luminance.

## 5. Step 5 — GGX specular via existing brdf.hlsli

- [ ] 5.1 Create `pt_material.hlsli` mapping `PBRMaterial` (metallic/roughness/baseColor) → `{F0, albedo, specTint}` per the table in `docs/analysis/path-tracing/01-current-state.md` §1.6.
- [ ] 5.2 Replace the glossy-lerp hack with proper GGX VNDF sampling (Heitz 2018) using existing `D_GGX` / `G_SmithGGXCorrelated` / `F_Schlick` from `material/brdf.hlsli`.
- [ ] 5.3 Lobe selection: dielectric Fresnel-driven, metals always specular.
- [ ] 5.4 Verify: 5 spheres of varying roughness, screenshot at `--frames 2000`. Mirror sphere reflects sky, rough sphere matte, smooth interpolation.
- [ ] 5.5 Compare visually against rasterized PBR output of the same materials — material identity must be preserved.

## 6. Step 6 — Camera-cut accumulation reset

- [ ] 6.1 Store `m_prevView` in `PathTracerPass`. On `Execute`, compare against `frameConstants.view` (epsilon ~1e-5).
- [ ] 6.2 Reset history (`m_samplesAccum=0`, `m_historyValid=false`, clear accum textures) on view change, resolution change, enable toggle, or `cv_PtResetAccum` exec.
- [ ] 6.3 Implement `r.PathTracer.ResetAccum` exec CVar wired to `PathTracerPass::ResetAccumulation`.
- [ ] 6.4 Verify: editor camera fly — stationary frames re-converge cleanly, moving frames show 1-spp noise, no ghosting after camera stops.

## 7. Step 7 — Analytic scene primitives from ECS

- [ ] 7.1 Add `runtime/engine/ecs/pathtracing/pt_primitive_component.h` with `PtPrimitive` enum (Sphere/Quad/EmissiveQuad) + per-shape params + material index.
- [ ] 7.2 Gatherer in `SceneEnvironment` (or new `PathTracerSceneGatherer`) packs active components into a `StructuredBuffer<PtPrimitiveGPU>` each frame. Bind at `t4`.
- [ ] 7.3 Implement `IntersectSphere` and `IntersectQuad` in `pt_scene.hlsli` with full citations (Shirley RTOW Ch.6 / Ericson §5.4.5). `TraceScene` iterates the structured buffer.
- [ ] 7.4 Bounce rays test against the buffer; first hit still uses GBuffer.
- [ ] 7.5 Verify: hand-author a Cornell-box-like scene in the editor with primitive components. Toggle PT on, screenshot at `--frames 2000`. Color bleeding visible, direct shadow from emissive quad, smooth convergence.

## 8. Step 8 — Falcor PSNR parity (RULE 0)

- [ ] 8.1 Create `scripts/falcor/path_tracer_reference.py` — Mogwai render graph using `PathTracer.py` + `AccumulatePass` + `ToneMapper` on the same scene exported as `.pyscene`.
- [ ] 8.2 Create `scripts/falcor/compare_pt.py` — runs Mogwai for N frames, captures, then runs `uzengine.exe --diff` and prints PSNR.
- [ ] 8.3 Capture UzEngine reference: `--cvar r.PathTracer.Enable=1 --cvar r.PathTracer.MaxBounces=8 --frames 4000 --screenshot <path>`.
- [ ] 8.4 PSNR ≥ 35 dB pass gate (RULE 0). Below threshold: investigate BRDF / tonemap alignment / RNG correlation. Fix UzEngine, never Falcor.
- [ ] 8.5 Document the exact Mogwai version + Falcor commit SHA used in the comparison.

## 9. Lua API parity

- [ ] 9.1 Create `runtime/engine/scripting/api/lua_pathtracer.cpp` exposing `uz.pathtracer.{enable, is_enabled, set_max_bounces, set_samples_per_frame, set_env_intensity, reset_accum, samples_accumulated}`.
- [ ] 9.2 Register in `LuaVM::Init()` alongside other `uz.*` tables.
- [ ] 9.3 Add `runtime/tests/lua/test_pathtracer_api.lua` exercising every binding.

## 10. Tier 1 — CPU parity tests (mandatory)

- [ ] 10.1 Create `runtime/tests/engine/renderer/test_path_tracer_parity.cpp`.
- [ ] 10.2 Test `WangHash`: known seed → 5 hand-computed values, exact integer match.
- [ ] 10.3 Test `RandUnitVector`: 1e6 samples, ‖mean‖ < 3/√N, variance matches uniform sphere within 1%.
- [ ] 10.4 Test `IntersectSphere` (`pt_geom.h::IntersectSphere`): known-value, tangent boundary, miss, ray-from-inside.
- [ ] 10.5 Test `IntersectQuad`: axis-aligned hit, edge/corner, back-face normal flip semantics.
- [ ] 10.6 Test cosine-weighted hemisphere sampling: PDF integral over hemisphere → 2π within 1% at N=1e6.
- [ ] 10.7 Test Russian roulette throughput: 1e6 runs, mean throughput preservation within 1% (unbiased estimator check).

## 11. Tier 2 — Visual regression goldens

- [ ] 11.1 Capture and commit `.sandbox/golden/path_tracer/primary_rays.png` (Step 2 smoke).
- [ ] 11.2 Capture and commit `.sandbox/golden/path_tracer/cornell_1bounce.png` (Step 3, 500 frames).
- [ ] 11.3 Capture and commit `.sandbox/golden/path_tracer/cornell_4bounce.png` (Step 4, 2000 frames).
- [ ] 11.4 Capture and commit `.sandbox/golden/path_tracer/ggx_row.png` (Step 5, 2000 frames).
- [ ] 11.5 Capture and commit `.sandbox/golden/path_tracer/showcase.png` (Step 8 input, 4000 frames).
- [ ] 11.6 Add the 5 goldens to `scripts/visual_test.ps1` regression list with PSNR ≥ 40 dB pass / 30-40 dB warn / <30 dB fail.

## 12. Tier 5 — Determinism

- [ ] 12.1 Verify two cold-start runs of `--screenshot a.png --screenshot-frame 500` produce bit-identical PNGs (PSNR = ∞). Catches any hidden non-determinism (CB aliasing, uninitialized history).

## 13. Documentation

- [ ] 13.1 Promote `docs/analysis/path-tracing/` from `status: draft` to `status: approved` after sign-off.
- [ ] 13.2 Create `docs/specs/rendering/path-tracer.md` (formal spec, Given/When/Then scenarios).
- [ ] 13.3 Add path tracer section to `docs/specs/rendering/renderer.md` cross-referencing the new spec.
- [ ] 13.4 Add Lua reference to `docs/lua-api-reference.md` for `uz.pathtracer.*`.
- [ ] 13.5 Update `runtime/CHANGELOG.md` with the new feature entry.
- [ ] 13.6 Add screenshot gallery to `docs/analysis/path-tracing/README.md` showing each step's verification image.

## 14. Sign-off

- [ ] 14.1 Resolve open question Q2 (analytic-only vs future BVH for triangle meshes) — see `docs/analysis/path-tracing/07-risks-and-open-questions.md`.
- [ ] 14.2 Resolve Q3 (environment source — confirm sky cubemap reuse).
- [ ] 14.3 Renderer owner reviews R1 (TAA auto-disable) and R2 (upscaler auto-disable) safety logic.
- [ ] 14.4 All Tier 1 tests green, all 5 goldens within PSNR threshold, Falcor PSNR ≥ 35 dB.
- [ ] 14.5 Archive task via `mcp__rulebook__rulebook_task_archive`.

## 15. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 15.1 Update or create documentation covering the implementation
- [ ] 15.2 Write tests covering the new behavior
- [ ] 15.3 Run tests and confirm they pass
