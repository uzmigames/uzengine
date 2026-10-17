## Stage 1: Opt-in directional-light DXR milestone (2026-10-17)

This bounded milestone integrates initial RIS and static-camera temporal reuse
in the existing `PipelinePathTracerPass`, not the deferred-lighting roadmap
below. Primary DXR hits supply surfaces instead of raster GBuffer hits.
The proposal's spatial reuse, pairwise bias correction, moving-camera
reprojection, general point/emissive-light sampling, light-BVH integration,
and deferred-lighting integration remain deferred.

- [x] Initial RIS: 32 directional-source candidates, streaming weighted selection,
  zero-target handling, and `W = weightSum / (M * target)`.
- [x] Temporal reuse: ping-pong buffers; exact unjittered camera/object/material/light
  signature gate; same-pixel history; normal dot >= 0.9 and scale-aware
  point-to-plane surface validation. Re-evaluate the selected history sample at
  the current hit; retain at most 19 old frame reservoirs plus the new one.
  Rescale history weight with its count; temporal M counts frames, not the 32
  initial candidates.
- [x] Build: `cd runtime && zig build -Doptimize=ReleaseFast` exited 0.
  `test_restir_di.cpp` compiled after removing stale `objectRow0/objectRow2`
  assertions and matching actual CPU strides (material 96, directional light 32).
  Both compute shaders compiled `cs_6_6` and all three DXR libraries compiled
  `lib_6_6`; no `compile failed` in fresh smoke, variance, or reset render logs.
- [x] Default safety: without either opt-in flag, reflection-reference frame 95
  is PNG-byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`;
  decoded `max_code_delta = 0`.
- [x] Equal-sample variance proof: both captures use `--path-trace --pt-bounces 1`,
  `r.PathTracer.NEESamples=1`, TAA off, identical 960x540 directional-light
  scene with 90-degree source angle, and one shadow ray per primary hit/frame.
  ReSTIR additionally uses `--restir-di` (32 unshadowed candidate evaluations).
  On 2,954 interior surface pixels, RGB code-error variance against the NEE
  200-sample image at 5 samples is 131.181779 NEE versus 23.238415 ReSTIR:
  **82.2853% lower**. At 10/20 samples the reductions are 68.8280%/31.7836%.
  At 200 samples the mean RGB code values are 63.365832 NEE and 63.392462
  ReSTIR: **+0.04203%**; inverse-sRGB display-linear mean differs by +0.06215%.
  These are PNG measurements, not a claim of general dynamic-scene unbiasedness.
  The existing low-discrepancy NEE overtakes this temporal estimator's error
  variance at 50+ samples; stage 1 claims low-sample improvement only.
- [x] Temporal proof: prior `smoke-verified` probe 14 stayed at M=1; the subsequent
  `smoke-depth` probe plateaued at M=8. Fresh `smoke-finish` probe 14 grows
  M=1,2,...,20 and stays capped at 20. In `reset-finish`, camera movement at
  engine frame 44 resets accumulated history after 43 samples; the next
  probe-14 readback is M=1, then 2,3,... . Projection and object changes also
  invalidate history.
- [x] Layout gate: `npm run test:layouts` exited 0; Tier A 88/88, Tier B 28/28,
  Tier C 9/9. No shared graphics root or shared PerFrameCB grew. The pass-private
  root is 96 bytes / 24 DWORDs.
- [x] Regression gate: `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast` ran 7,844 tests: **7,824 passed**, 17 existing skips,
  and the same three pre-existing `VfxEditorSlate` failures
  (`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
  `StatusBarBindingsReflectTelemetry`). Command exit 1 is solely those failures;
  delta versus the supplied 7,818-pass baseline is **+6 passes, zero new failures**.
  No device-removal/TDR markers occurred, so no transient retry was needed.
  All six `RestirDIStage1` tests passed:
  `StructuredBufferLayouts`, `StreamingRISSelectsProportionalToWeights`,
  `ZeroWeightNeverReplacesSelectedSample`,
  `InitialWeightIsUnbiasedForDiscreteCandidates`,
  `TemporalCombinationReevaluatesCurrentTarget`, and
  `HistoryCapPreservesContributionWeight`.

**Stage 1 is complete within the directional-light DXR/static-camera boundary.**
The expanded roadmap below remains separate and is not claimed complete.

Scoped changed files:

- `.rulebook/tasks/phase1_falcor-restir-di/tasks.md`
- `runtime/engine/app/application_args.cpp`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.cpp`
- `runtime/engine/renderer/passes/pathtracing/pipeline_path_tracer_pass.h`
- `runtime/shaders/runtime/include/restir_di.hlsli` (new)
- `runtime/shaders/runtime/include/restir_math.hlsli` (new)
- `runtime/shaders/runtime/pathtracing/restir_initial_cs.hlsl` (new)
- `runtime/shaders/runtime/pathtracing/restir_temporal_cs.hlsl` (new)
- `runtime/shaders/runtime/pathtracing/pathtracer_closesthit.hlsl`
- `runtime/shaders/runtime/pathtracing/pathtracer_raygen.hlsl`
- `runtime/shaders/runtime/pathtracing/pathtracer_shared.hlsli`
- `runtime/tests/engine/renderer/test_restir_di.cpp` (new)
- `runtime/tests/engine/renderer/test_pipeline_path_tracer.cpp`
- `runtime/tools/uzengine.cpp`
- `scripts/rendering/test_cbuffer_layouts.py`

The private root's shader consumers are raygen, closesthit, miss (unchanged),
initial RIS, and temporal RIS. Its CPU owner is `PipelinePathTracerPass`;
the layout checker and both renderer test files cover the contract.
No shared graphics-root, shared constant-buffer, or deferred-lighting consumer
changes are required. Unrelated VFX-editor work is untouched.

Evidence outside the repository:
`E:/UzmiEngine/restir-stage1-{default,smoke,reset,variance-nee,variance-restir}-finish/`
contains the fresh PNGs, manifests, and render logs.
`restir-stage1-build-final.log`, `restir-stage1-layouts-final.log`,
`restir-stage1-gtests-finish.log`, and `restir-stage1-metrics-final.json`
are in `E:/UzmiEngine/`. The capture and PNG-analysis scripts are in
`E:/UzmiEngine/restir-stage1-evidence/`.

## Expanded roadmap: Reservoir Data Structure + Initial Sampling

- [ ] 1.1 Create `include/restir_di.hlsli` — `struct LightSample { uint lightIndex; float3 position; float3 normal; float3 emission; float pdf; }`. `struct Reservoir { LightSample sample; float weightSum; uint M; float W; }`. `void UpdateReservoir(inout Reservoir r, LightSample s, float weight, float u)` — streaming RIS update (Talbot et al. 2005 Eq. 3). @ref Bitterli et al. 2020
- [ ] 1.2 Create `restir_initial_cs.hlsl` — `[numthreads(8,8,1)]` entry `main`. For each pixel: read GBuffer (world pos, normal, material). Iterate N=32 candidate lights from `StructuredBuffer<LightData>`. For each candidate, compute unshadowed target function `p_hat = Le * BRDF * |cosTheta| / dist²`. Update reservoir via `UpdateReservoir`. Write final reservoir to `RWStructuredBuffer<Reservoir> uReservoirs`.
- [ ] 1.3 Compute final weight `W = (1/M) * weightSum / p_hat(selected)` and store in reservoir. Handle degenerate case (p_hat = 0 → W = 0).
- [ ] 1.4 Build + verify: initial sampling pass compiles and dispatches. Reservoir buffer populated with plausible light indices (verify via GPU readback of first 10 pixels).

## Session 2: Temporal Reuse

- [ ] 2.1 Create `restir_temporal_cs.hlsl` — for each pixel, reproject to previous frame using motion vectors (multiply by prev view-proj). Sample previous reservoir from `tPrevReservoirs` at reprojected UV.
- [ ] 2.2 Disocclusion validation: reject previous reservoir if depth difference > threshold OR normal dot < 0.9. On rejection, use only current reservoir.
- [ ] 2.3 Pairwise MIS combination: evaluate previous reservoir's selected sample at current pixel (compute `p_hat` at current shading point). Combine via `MIS weight = p_hat_current / (p_hat_current + p_hat_prev)`. Clamp `M` to `min(M_prev + M_cur, 20)` to bound temporal bias. @ref Bitterli et al. 2020 Section 4.3
- [ ] 2.4 Ping-pong reservoir buffers: `RestirDiPass` maintains `reservoirBuffer[2]` swapping each frame. Write combined reservoir to current frame's buffer.
- [ ] 2.5 Build + verify: temporal pass produces stable reservoir selection on a static scene; no flickering (verify via 10-frame screenshot comparison).

## Session 3: Spatial Reuse

- [ ] 3.1 Create `restir_spatial_cs.hlsl` — 5 iterations of spatial resampling. Each iteration: pick 1 random neighbor within 30px radius using low-discrepancy sequence. Sample neighbor's reservoir from `tReservoirs`.
- [ ] 3.2 Jacobian correction: for reusing a neighbor's light sample at current pixel, apply geometry Jacobian `|cosTheta_neighbor / cosTheta_current| * (dist_current² / dist_neighbor²)` to account for different shading point. @ref Bitterli et al. 2020 Appendix A
- [ ] 3.3 Visibility reuse: before accepting neighbor's sample, cast a shadow ray via `TraceRay` with `ACCEPT_FIRST_HIT_AND_END_SEARCH`. Reject sample if occluded (set MIS weight to 0).
- [ ] 3.4 Write final spatial-reused reservoir to output buffer for deferred lighting consumption.
- [ ] 3.5 Build + verify: spatial pass produces visibly reduced noise vs initial sampling alone. Compare screenshots (initial only vs initial+temporal+spatial).

## Session 4: Deferred Lighting Integration + Verification

- [ ] 4.1 Add `--restir-di` CLI flag: activates all 3 ReSTIR passes in engine loop before deferred lighting.
- [ ] 4.2 Update `deferred_lighting_ps.hlsl`: add `#ifdef RESTIR_DI` path — read final reservoir for current pixel, evaluate PBR shading for the selected light. Fallback to existing N-light loop when disabled.
- [ ] 4.3 `RestirDiPass` C++ class: `Init(device, width, height)` allocates reservoir ping-pong buffers. `Execute(cmd, gbufferSRVs, lightBuffer, motionVectors)` runs initial → temporal → spatial passes in sequence. Returns final reservoir SRV.
- [ ] 4.4 Scene with 1000 point lights: populate via Lua script placing lights in grid. Verify: `--restir-di` renders all lights correctly in under 10ms (GPU timestamp from `--gpu-profile`). Without `--restir-di`, same scene should be visibly slower.
- [ ] 4.5 Build + verify: screenshot comparison of 1000-light scene with and without ReSTIR. Run `--flip` to confirm ReSTIR output converges to same reference as brute-force N-light evaluation (with enough spatial iterations).

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
